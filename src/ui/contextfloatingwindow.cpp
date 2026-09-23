#include "uicontrols.h"
#include "contextfloatingwindow.h"
#include "roundedicons.h"
#include "contextdockhost.h"
#include "applicationthememanager.h"
#include "insightvisualstyle.h"
#ifdef ZEROSLACK_ENABLE_ELA
#include "ElaApplication.h"
#include "ElaDragHandle.h"
#endif

#include <QCloseEvent>
#include <QDynamicPropertyChangeEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
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
#include <QShortcut>

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
#ifndef ZEROSLACK_ENABLE_ELA
// Keep compatibility with MinGW headers predating Windows 11 22H2.
constexpr DWORD kSystemBackdropType = 38;
constexpr DWORD kImmersiveDarkMode = 20;
constexpr int kNoBackdrop = 1;
constexpr int kDesktopAcrylic = 3;
#endif

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
    : ContextFloatingWindowBase(nullptr), editorRegion(region)
{
#ifdef ZEROSLACK_ENABLE_ELA
    // ElaWidget constructs its app bar against window(). Become a top-level
    // window first, then attach the owner so it cannot decorate the main window.
    setParent(mainWindow, Qt::Tool | (windowFlags() & ~Qt::WindowType_Mask));
    setIsStayTop(false);
    setWindowButtonFlags(ElaAppBarType::MinimizeButtonHint
        | ElaAppBarType::MaximizeButtonHint | ElaAppBarType::CloseButtonHint);
    // This material is scoped to context windows, independently of global Ela windows.
    eApp->syncWindowDisplayMode(this, false);
#else
    setParent(mainWindow, Qt::Tool);
#endif
    setObjectName(QStringLiteral("contextFloatingWindow"));
    // The content and title bar share one window material; text stays opaque.
    if (QGuiApplication::platformName() == QStringLiteral("windows"))
        setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
    auto* root = new QVBoxLayout(this);
    root->setSizeConstraint(QLayout::SetNoConstraint);
    root->setContentsMargins(8, 0, 8, 8);
    titleActions = new QWidget(this);
    titleActions->setObjectName(QStringLiteral("contextFloatingActions"));
    titleActions->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto* actions = new QHBoxLayout(titleActions);
    actions->setContentsMargins(0, 0, 4, 0);
    actions->setSpacing(2);
    dragButton = UiControls::toolButton(titleActions);
    dragButton->setObjectName(QStringLiteral("contextFloatingDrag"));
    dragButton->setIcon(style()->standardIcon(QStyle::SP_TitleBarNormalButton));
    dragButton->setToolTip(tr("Drag this handle into the sidebar or bottom area"));
    dragButton->setCursor(Qt::OpenHandCursor);
    dragButton->installEventFilter(this);
    actions->addWidget(dragButton);
    fitButton = UiControls::toolButton(titleActions);
    fitButton->setObjectName(QStringLiteral("contextFloatingFit"));
    fitButton->setText(tr("Fit"));
    fitButton->setToolTip(tr("Fit diagram to view"));
    fitButton->hide();
    actions->addWidget(fitButton);
    connect(fitButton, &QToolButton::clicked, this, [this] {
        if (currentView) QMetaObject::invokeMethod(currentView, "fitGraph");
    });
    pinButton = UiControls::toolButton(titleActions);
    pinButton->setObjectName(QStringLiteral("contextFloatingPin"));
    pinButton->setText(tr("Pin"));
    pinButton->setToolTip(tr("Keep in sidebar"));
    pinButton->setIcon(RoundedIcons::icon(RoundedIcons::Pin));
    fullViewButton = UiControls::toolButton(titleActions);
    fullViewButton->setObjectName(QStringLiteral("contextFloatingFullView"));
    fullViewButton->setText(tr("Full view"));
    fullViewButton->setToolTip(tr("Open current view in main area"));
    fullViewButton->setIcon(RoundedIcons::icon(RoundedIcons::Expand));
    actions->addWidget(pinButton);
    actions->addWidget(fullViewButton);
    for (auto* button : {dragButton, fitButton, pinButton, fullViewButton}) {
        button->setAccessibleName(button->toolTip());
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }
#ifdef ZEROSLACK_ENABLE_ELA
    appBar = findChild<ElaAppBar*>(QString(), Qt::FindDirectChildrenOnly);
    appBar->setCustomWidget(ElaAppBarType::RightArea, titleActions);
    appBar->titleLabel()->setTextFormat(Qt::PlainText);
    appBar->titleLabel()->setWordWrap(false);
    appBar->titleLabel()->setMinimumWidth(0);
    appBar->titleLabel()->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    connect(this, &QWidget::windowTitleChanged, this, &ContextFloatingWindow::layoutTitleBar);
#else
    root->addWidget(titleActions, 0, Qt::AlignRight);
#endif
    contentLayout = new QVBoxLayout;
    root->addLayout(contentLayout, 1);
    connect(pinButton, &QToolButton::clicked, this, &ContextFloatingWindow::pinRequested);
    connect(fullViewButton, &QToolButton::clicked, this, &ContextFloatingWindow::fullViewRequested);
    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, &ContextFloatingWindow::closeRequested);
#ifdef ZEROSLACK_ENABLE_ELA
    if (ApplicationThemeManager::instance().backend() == UiStyleBackend::Ela) {
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
            this, [this] { layoutTitleBar(); refreshBackdrop(); });
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
    layoutTitleBar();
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
    // Source editors paint their own wallpaper from Palette::Base. A transparent
    // base would be blended as black; ordinary panels still share the Acrylic surface.
    const QString sheet = QStringLiteral(
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
        "#contextFloatingWindow QHeaderView::section { background: transparent; }"
        "#contextFloatingWindow QAbstractScrollArea[codeEditorSurface=true] { background: %1; }")
        .arg(InsightVisualStyle::theme().input.background.name());
    if (styleSheet() != sheet) setStyleSheet(sheet);
#if defined(ZEROSLACK_ENABLE_ELA) && defined(Q_OS_WIN)
    const auto previousMode = acrylicBackdrop ? ElaApplicationType::Acrylic : ElaApplicationType::Normal;
#endif
    acrylicBackdrop = false;
#ifdef Q_OS_WIN
    if (QGuiApplication::platformName() == QStringLiteral("windows") && internalWinId()) {
#ifdef ZEROSLACK_ENABLE_ELA
        if (systemAllowsAcrylic())
            acrylicBackdrop = eApp->applyWindowDisplayMode(this, ElaApplicationType::Acrylic, previousMode);
        if (!acrylicBackdrop)
            eApp->applyWindowDisplayMode(this, ElaApplicationType::Normal, previousMode);
#else
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
#endif
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
    return ContextFloatingWindowBase::nativeEvent(eventType, message, result);
}

void ContextFloatingWindow::setActionsAvailable(bool pinAvailable, bool fullViewAvailable)
{
    dragButton->setEnabled(pinAvailable);
    pinButton->setVisible(pinAvailable);
    pinButton->setEnabled(pinAvailable);
    fullViewButton->setVisible(fullViewAvailable);
    fullViewButton->setEnabled(fullViewAvailable);
    layoutTitleBar();
}

void ContextFloatingWindow::refreshTitle()
{
    const QString displayTitle = currentView ? currentView->property("contextDisplayTitle").toString() : QString();
    setWindowTitle(!displayTitle.isEmpty() ? displayTitle
        : currentResource.title.isEmpty() ? currentResource.uri.fileName() : currentResource.title);
    layoutTitleBar();
}

void ContextFloatingWindow::layoutTitleBar()
{
#ifdef ZEROSLACK_ENABLE_ELA
    if (!appBar || !titleActions) return;
    int height = 30;
    int controlsWidth = 0;
    for (const auto type : {ElaAppBarType::MinimizeButtonHint,
                           ElaAppBarType::MaximizeButtonHint, ElaAppBarType::CloseButtonHint}) {
        auto* button = appBar->windowButton(type);
        const QSize size = button->minimumSizeHint().expandedTo(QSize(40, 30));
        button->setFixedSize(size);
        height = qMax(height, size.height());
        if (!button->isHidden()) controlsWidth += size.width();
    }
    titleActions->layout()->invalidate();
    const QSize actionsSize = titleActions->sizeHint();
    height = qMax(height, actionsSize.height());
    appBar->setAppBarHeight(height + 4);
    titleActions->setFixedSize(actionsSize);
    const int available = qMax(0, width() - controlsWidth - actionsSize.width() - 26);
    auto* label = appBar->titleLabel();
    const QString title = label->fontMetrics().elidedText(windowTitle(), Qt::ElideMiddle, available);
    label->setFixedWidth(qMin(available, label->fontMetrics().horizontalAdvance(title)));
    label->setText(title);
    label->setToolTip(windowTitle());
#endif
}

QWidget* ContextFloatingWindow::sidebarDragHandle() const { return dragButton; }
bool ContextFloatingWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == currentView && event->type() == QEvent::DynamicPropertyChange) {
        const auto name = static_cast<QDynamicPropertyChangeEvent*>(event)->propertyName();
        if (name == "contextFitAvailable") {
            fitButton->setVisible(currentView->property("contextFitAvailable").toBool());
            layoutTitleBar();
        } else if (name == "contextDisplayTitle") {
            refreshTitle();
        }
    }
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
    return ContextFloatingWindowBase::eventFilter(watched, event);
}

void ContextFloatingWindow::setView(const ContextResource& resource, QWidget* view)
{
    if (!resource.isValid() || !view)
        return;
    clearView();
    currentResource = resource;
    currentView = view;
    view->installEventFilter(this);
    fitButton->setVisible(view->property("contextFitAvailable").toBool());
    contentLayout->addWidget(view);
    refreshTitle();
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
        view->removeEventFilter(this);
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
    refreshTitle();
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
    ContextFloatingWindowBase::moveEvent(event);
    rememberGeometry();
}

void ContextFloatingWindow::resizeEvent(QResizeEvent* event)
{
    ContextFloatingWindowBase::resizeEvent(event);
    layoutTitleBar();
    rememberGeometry();
}

bool ContextFloatingWindow::event(QEvent* event)
{
    const bool handled = ContextFloatingWindowBase::event(event);
    if (event->type() == QEvent::WinIdChange || event->type() == QEvent::Show
        || event->type() == QEvent::ApplicationPaletteChange)
        scheduleBackdropRefresh();
    return handled;
}
