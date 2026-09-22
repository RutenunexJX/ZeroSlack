#include "uicontrols.h"
#include "contextfloatingwindow.h"
#include "roundedicons.h"
#include "contextdockhost.h"
#include "applicationthememanager.h"
#include "insightvisualstyle.h"
#ifdef ZEROSLACK_ENABLE_ELA
#include "ElaAppBar.h"
#include "ElaDragHandle.h"
#endif

#include <QCloseEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QScreen>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>
#include <QApplication>
#include <QDrag>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QSettings>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#endif

namespace {
QRect resolve(const QRect& saved, const QString& screenName, QScreen* primary)
{
    QList<QRect> geometries;
    QList<QString> names;
    for (QScreen* screen : QGuiApplication::screens()) {
        geometries.append(screen->availableGeometry());
        names.append(screen->name());
    }
    return ContextWorkspaceState::resolvedFloatingGeometry(saved, screenName, geometries,
        primary ? primary->availableGeometry() : QRect(0, 0, 520, 440), names);
}

#ifdef Q_OS_WIN
// Keep compatibility with MinGW headers predating Windows 11 22H2.
constexpr DWORD kSystemBackdropType = 38;
constexpr DWORD kImmersiveDarkMode = 20;
constexpr int kNoBackdrop = 1;
constexpr int kDesktopAcrylic = 3;

bool systemAllowsAcrylic()
{
    BOOL composition = FALSE;
    if (FAILED(DwmIsCompositionEnabled(&composition)) || !composition)
        return false;
    HIGHCONTRASTW contrast{};
    contrast.cbSize = sizeof(contrast);
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0)
        && (contrast.dwFlags & HCF_HIGHCONTRASTON))
        return false;
    SYSTEM_POWER_STATUS power{};
    if (GetSystemPowerStatus(&power) && power.SystemStatusFlag == 1)
        return false;
    const QSettings personalization(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
        QSettings::NativeFormat);
    return personalization.value(QStringLiteral("EnableTransparency"), 1).toBool();
}
#endif
}

ContextFloatingWindow::ContextFloatingWindow(QWidget* mainWindow, QWidget* region)
    : QWidget(mainWindow, Qt::Tool), editorRegion(region)
{
    setObjectName(QStringLiteral("contextFloatingWindow"));
    // Keep the native Tool surface and DWM backdrop. Ela takes over caption
    // hit testing and window controls without adding a layered window flag.
    if (QGuiApplication::platformName() == QStringLiteral("windows"))
        setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
    // Content surfaces share this host's backdrop. Local theme helpers carry
    // the same ancestor rule so their own styles cannot cover it again.
    setStyleSheet(QStringLiteral(
        "#contextFloatingWindow QDockWidget, "
        "#contextFloatingWindow QToolBar, "
        "#contextFloatingWindow QAbstractScrollArea, "
        "#contextFloatingWindow QLineEdit, "
        "#contextFloatingWindow QComboBox, "
        "#contextFloatingWindow QAbstractSpinBox, "
        "#contextFloatingWindow QTabBar, "
        "#contextFloatingWindow QHeaderView { background: transparent; }"
        "#contextFloatingWindow QScrollArea > QWidget > QWidget { background: transparent; }"
        "#contextFloatingWindow QAbstractItemView { alternate-background-color: transparent; }"
        "#contextFloatingWindow QTabWidget::pane, "
        "#contextFloatingWindow QHeaderView::section { background: transparent; }"));
    auto* root = new QVBoxLayout(this);
    root->setSizeConstraint(QLayout::SetNoConstraint);
    root->setContentsMargins(8, 6, 8, 8);
    auto* actions = new QHBoxLayout;
    dragButton = UiControls::toolButton(this);
    dragButton->setObjectName(QStringLiteral("contextFloatingDrag"));
    dragButton->setIcon(style()->standardIcon(QStyle::SP_TitleBarNormalButton));
    dragButton->setToolTip(tr("Drag this handle into the sidebar"));
    dragButton->setCursor(Qt::OpenHandCursor);
    dragButton->installEventFilter(this);
    actions->addWidget(dragButton);
    actions->addStretch();
    pinButton = UiControls::toolButton(this);
    pinButton->setObjectName(QStringLiteral("contextFloatingPin"));
    pinButton->setText(tr("Pin"));
    pinButton->setToolTip(tr("Keep in sidebar"));
    pinButton->setIcon(RoundedIcons::icon(RoundedIcons::Pin));
    fullViewButton = UiControls::toolButton(this);
    fullViewButton->setObjectName(QStringLiteral("contextFloatingFullView"));
    fullViewButton->setText(tr("Full view"));
    fullViewButton->setToolTip(tr("Open current view in main area"));
    fullViewButton->setIcon(RoundedIcons::icon(RoundedIcons::Expand));
    actions->addWidget(pinButton);
    actions->addWidget(fullViewButton);
    root->addLayout(actions);
    contentLayout = new QVBoxLayout;
    root->addLayout(contentLayout, 1);
    connect(pinButton, &QToolButton::clicked, this, &ContextFloatingWindow::pinRequested);
    connect(fullViewButton, &QToolButton::clicked, this, &ContextFloatingWindow::fullViewRequested);
#ifdef ZEROSLACK_ENABLE_ELA
    if (ApplicationThemeManager::instance().backend() == UiStyleBackend::Ela) {
        auto* appBar = new ElaAppBar(this);
        floatingAppBar = appBar;
        appBar->setWindowButtonFlags(ElaAppBarType::MinimizeButtonHint
            | ElaAppBarType::MaximizeButtonHint | ElaAppBarType::CloseButtonHint);
        auto* gesture = new ElaDragHandle(dragButton, this);
        gesture->setMimeDataFactory([this]() -> QMimeData* {
            if (!hasResource()) return nullptr;
            auto* mime = new QMimeData;
            mime->setData(ContextDockHost::resourceMimeType(), resource().stableKey().toUtf8());
            return mime;
        });
        connect(gesture, &ElaDragHandle::dragStarted, this, &ContextFloatingWindow::sidebarDragStarted);
        connect(gesture, &ElaDragHandle::dragFinished, this, &ContextFloatingWindow::sidebarDragFinished);
    }
#endif
    connect(&ApplicationThemeManager::instance(), &ApplicationThemeManager::themeChanged,
            this, [this] { refreshBackdrop(); });
    // Native frame margins are needed when restoring the saved outer rectangle.
    winId();
    connect(windowHandle(), &QWindow::screenChanged, this, [this] { handleScreenChange(); });
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this] { handleScreenChange(); });
    connect(qGuiApp, &QGuiApplication::screenAdded, this, [this](QScreen* screen) {
        watchScreen(screen);
        handleScreenChange();
    });
    for (QScreen* screen : QGuiApplication::screens())
        watchScreen(screen);
    backdropReady = true;
    refreshBackdrop();
    hide();
}

bool ContextFloatingWindow::hasResource() const { return currentView && currentResource.isValid(); }
ContextResource ContextFloatingWindow::resource() const { return currentResource; }
QWidget* ContextFloatingWindow::view() const { return currentView; }
void ContextFloatingWindow::setInitialSize(const QSize& size) { initialSize = size; }
int ContextFloatingWindow::backgroundOpacity() const { return opacityPercentage; }
bool ContextFloatingWindow::hasAcrylicBackdrop() const { return acrylicBackdrop; }

void ContextFloatingWindow::setBackgroundOpacity(int percentage)
{
    opacityPercentage = qBound(60, percentage, 100);
    update();
}

void ContextFloatingWindow::refreshBackdrop()
{
    if (!backdropReady)
        return;
    acrylicBackdrop = false;
#ifdef Q_OS_WIN
    if (QGuiApplication::platformName() == QStringLiteral("windows") && internalWinId()) {
        const HWND hwnd = reinterpret_cast<HWND>(internalWinId());
        const BOOL dark = isDarkTheme(ApplicationThemeManager::instance().mode());
        DwmSetWindowAttribute(hwnd, kImmersiveDarkMode, &dark, sizeof(dark));
        if (systemAllowsAcrylic()) {
            const MARGINS glass{-1, -1, -1, -1};
            if (SUCCEEDED(DwmSetWindowAttribute(hwnd, kSystemBackdropType,
                                               &kDesktopAcrylic, sizeof(kDesktopAcrylic)))
                && SUCCEEDED(DwmExtendFrameIntoClientArea(hwnd, &glass)))
                acrylicBackdrop = true;
        }
        if (!acrylicBackdrop) {
            const MARGINS solid{};
            DwmSetWindowAttribute(hwnd, kSystemBackdropType, &kNoBackdrop, sizeof(kNoBackdrop));
            DwmExtendFrameIntoClientArea(hwnd, &solid);
        }
    }
#endif
    // Never attenuate the text, icons or graph together with the background.
    setWindowOpacity(1.0);
    update();
}

void ContextFloatingWindow::scheduleBackdropRefresh()
{
    if (!backdropReady || backdropUpdatePending)
        return;
    backdropUpdatePending = true;
    QTimer::singleShot(0, this, [this] {
        backdropUpdatePending = false;
        refreshBackdrop();
    });
}

void ContextFloatingWindow::paintEvent(QPaintEvent*)
{
    QColor background = InsightVisualStyle::theme().panelBackground;
    if (acrylicBackdrop)
        background.setAlphaF(opacityPercentage / 100.0);
    QPainter painter(this);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(rect(), background);
}

bool ContextFloatingWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
#ifdef Q_OS_WIN
    const auto* native = static_cast<MSG*>(message);
    if (native && (native->message == WM_DWMCOMPOSITIONCHANGED
                   || native->message == WM_THEMECHANGED
                   || native->message == WM_SETTINGCHANGE
                   || native->message == WM_POWERBROADCAST))
        scheduleBackdropRefresh();
#endif
#if defined(ZEROSLACK_ENABLE_ELA) && defined(Q_OS_WIN)
    if (auto* appBar = qobject_cast<ElaAppBar*>(floatingAppBar)) {
        const int handled = appBar->takeOverNativeEvent(eventType, message, result);
        if (handled >= 0) return handled != 0;
    }
#endif
    return QWidget::nativeEvent(eventType, message, result);
}

void ContextFloatingWindow::setActionsAvailable(bool pinAvailable, bool fullViewAvailable)
{
    dragButton->setEnabled(pinAvailable);
    pinButton->setVisible(pinAvailable);
    pinButton->setEnabled(pinAvailable);
    fullViewButton->setVisible(fullViewAvailable);
    fullViewButton->setEnabled(fullViewAvailable);
}

QWidget* ContextFloatingWindow::sidebarDragHandle() const { return dragButton; }
bool ContextFloatingWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == dragButton && !dragButton->property("elaDragManaged").toBool()
        && dragButton->isEnabled() && hasResource()) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton) dragStart = mouse->position().toPoint();
        } else if (event->type() == QEvent::MouseMove) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if ((mouse->buttons() & Qt::LeftButton)
                && (mouse->position().toPoint() - dragStart).manhattanLength() >= QApplication::startDragDistance()) {
                QPointer<ContextFloatingWindow> owner(this);
                QPointer<QDrag> drag = new QDrag(this);
                auto* mime = new QMimeData;
                mime->setData(ContextDockHost::resourceMimeType(), resource().stableKey().toUtf8());
                drag->setMimeData(mime);
                emit sidebarDragStarted();
                if (!owner || !drag) return true;
                const bool accepted = drag->exec(Qt::MoveAction) == Qt::MoveAction;
                if (owner) emit sidebarDragFinished(accepted);
                if (drag) drag->deleteLater();
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ContextFloatingWindow::setView(const ContextResource& resource, QWidget* view)
{
    if (!resource.isValid() || !view)
        return;
    clearView();
    currentResource = resource;
    currentView = view;
    contentLayout->addWidget(view);
    setWindowTitle(resource.title.isEmpty() ? resource.uri.fileName() : resource.title);
    if (!geometryValid) {
        QScreen* screen = parentWidget() ? parentWidget()->screen() : QGuiApplication::primaryScreen();
        const QPoint corner = editorRegion
            ? editorRegion->mapToGlobal(editorRegion->rect().bottomRight()) : QPoint(520, 440);
        // Empty name deliberately requests a fully visible first placement on the owner's screen.
        storedGeometry = resolve(QRect(corner - QPoint(initialSize.width(), initialSize.height()), initialSize), {}, screen);
        storedScreenName = screen ? screen->name() : QString();
    }
    applyGeometry();
    // Ignore show-time move/resize notifications until the saved outer frame is applied.
    const QRect requestedGeometry = storedGeometry;
    applyingGeometry = true;
    view->show();
    show();
    applyingGeometry = false;
    if (frameGeometry() != requestedGeometry) {
        storedGeometry = requestedGeometry;
        applyGeometry();
    }
    raise();
    activateWindow();
    rememberGeometry();
}

QWidget* ContextFloatingWindow::takeView()
{
    rememberGeometry();
    QWidget* view = currentView;
    if (view) {
        contentLayout->removeWidget(view);
        view->hide();
        view->setParent(nullptr);
    }
    currentView.clear();
    currentResource = {};
    setWindowTitle({});
    hide();
    return view;
}

void ContextFloatingWindow::clearView()
{
    if (QWidget* oldView = takeView())
        oldView->deleteLater();
}

bool ContextFloatingWindow::updateResource(const ContextResource& resource)
{
    if (!hasResource() || currentResource.stableKey() != resource.stableKey())
        return false;
    currentResource = resource;
    setWindowTitle(resource.title.isEmpty() ? resource.uri.fileName() : resource.title);
    return true;
}

void ContextFloatingWindow::closeEvent(QCloseEvent* event)
{
    event->ignore();
    emit closeRequested();
}

void ContextFloatingWindow::rememberGeometry()
{
    if (applyingGeometry || !hasResource() || !isVisible() || isMinimized())
        return;
    const QRect geometry = frameGeometry();
    const QString screenName = screen() ? screen()->name() : QString();
    const bool changed = !geometryValid || storedGeometry != geometry || storedScreenName != screenName;
    storedGeometry = geometry;
    storedScreenName = screenName;
    geometryValid = true;
    if (changed)
        emit geometryChanged();
}

void ContextFloatingWindow::applyGeometry()
{
    applyingGeometry = true;
    storedGeometry = resolve(storedGeometry, storedScreenName, QGuiApplication::primaryScreen());
    const QMargins margins = windowHandle()->frameMargins();
    const int frameWidth = margins.left() + margins.right();
    const int frameHeight = margins.top() + margins.bottom();
    setMinimumSize(qMax(1, qMin(ContextWorkspaceState::kMinimumPeekWidth, storedGeometry.width()) - frameWidth),
                   qMax(1, qMin(ContextWorkspaceState::kMinimumPeekHeight, storedGeometry.height()) - frameHeight));
    setMaximumSize(qMax(1, ContextWorkspaceState::kMaximumPeekWidth - frameWidth),
                   qMax(1, ContextWorkspaceState::kMaximumStoredPeekHeight - frameHeight));
    resize(qMax(1, storedGeometry.width() - frameWidth), qMax(1, storedGeometry.height() - frameHeight));
    move(storedGeometry.topLeft());
    applyingGeometry = false;
}

void ContextFloatingWindow::captureGeometry(ContextWorkspaceState& state) const
{
    state.floatingX = storedGeometry.x();
    state.floatingY = storedGeometry.y();
    state.floatingWidth = storedGeometry.width();
    state.floatingHeight = storedGeometry.height();
    state.floatingScreenName = storedScreenName;
    state.floatingGeometryValid = geometryValid;
}

void ContextFloatingWindow::restoreGeometry(const ContextWorkspaceState& state)
{
    storedGeometry = QRect(state.floatingX, state.floatingY, state.floatingWidth, state.floatingHeight);
    storedScreenName = state.floatingScreenName;
    geometryValid = state.floatingGeometryValid;
    if (geometryValid)
        storedGeometry = resolve(storedGeometry, storedScreenName, QGuiApplication::primaryScreen());
}

void ContextFloatingWindow::watchScreen(QScreen* screen)
{
    connect(screen, &QScreen::availableGeometryChanged, this, [this] { handleScreenChange(); });
}

void ContextFloatingWindow::handleScreenChange()
{
    QTimer::singleShot(0, this, [this] {
        if (hasResource()) {
            applyGeometry();
            rememberGeometry();
        }
    });
}

void ContextFloatingWindow::moveEvent(QMoveEvent* event)
{
    QWidget::moveEvent(event);
    rememberGeometry();
}

void ContextFloatingWindow::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    rememberGeometry();
}

bool ContextFloatingWindow::event(QEvent* event)
{
    const bool handled = QWidget::event(event);
    if (event->type() == QEvent::WinIdChange || event->type() == QEvent::Show
        || event->type() == QEvent::ApplicationPaletteChange)
        scheduleBackdropRefresh();
    return handled;
}
