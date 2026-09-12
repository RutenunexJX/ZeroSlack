#include "contextfloatingwindow.h"
#include "roundedicons.h"

#include <QCloseEvent>
#include <QCursor>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QScreen>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>

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
}

ContextFloatingWindow::ContextFloatingWindow(QWidget* mainWindow, QWidget* region)
    : QWidget(mainWindow, Qt::Tool), editorRegion(region)
{
    setObjectName(QStringLiteral("contextFloatingWindow"));
    setAutoFillBackground(true);
    auto* root = new QVBoxLayout(this);
    root->setSizeConstraint(QLayout::SetNoConstraint);
    root->setContentsMargins(8, 6, 8, 8);
    auto* actions = new QHBoxLayout;
    actions->addStretch();
    pinButton = new QToolButton(this);
    pinButton->setObjectName(QStringLiteral("contextFloatingPin"));
    pinButton->setText(tr("Pin"));
    pinButton->setToolTip(tr("Keep in sidebar"));
    pinButton->setIcon(RoundedIcons::icon(RoundedIcons::Pin));
    fullViewButton = new QToolButton(this);
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
    hoverTimer = new QTimer(this);
    hoverTimer->setInterval(100);
    connect(hoverTimer, &QTimer::timeout, this, [this] {
        applyInteractionOpacity(isActiveWindow(), frameGeometry().contains(QCursor::pos()));
    });
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
    hide();
}

bool ContextFloatingWindow::hasResource() const { return currentView && currentResource.isValid(); }
ContextResource ContextFloatingWindow::resource() const { return currentResource; }
QWidget* ContextFloatingWindow::view() const { return currentView; }
void ContextFloatingWindow::setInitialSize(const QSize& size) { initialSize = size; }
int ContextFloatingWindow::idleOpacity() const { return opacityPercentage; }

void ContextFloatingWindow::setIdleOpacity(int percentage)
{
    opacityPercentage = qBound(60, percentage, 100);
    applyInteractionOpacity(isActiveWindow(), frameGeometry().contains(QCursor::pos()));
}

void ContextFloatingWindow::applyInteractionOpacity(bool active, bool hovered)
{
    setWindowOpacity(active || hovered ? 1.0 : opacityPercentage / 100.0);
}

void ContextFloatingWindow::setActionsAvailable(bool pinAvailable, bool fullViewAvailable)
{
    pinButton->setVisible(pinAvailable);
    pinButton->setEnabled(pinAvailable);
    fullViewButton->setVisible(fullViewAvailable);
    fullViewButton->setEnabled(fullViewAvailable);
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
    hoverTimer->start();
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
    hoverTimer->stop();
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
    if (event->type() == QEvent::WindowActivate)
        applyInteractionOpacity(true, false);
    else if (event->type() == QEvent::WindowDeactivate)
        applyInteractionOpacity(false, frameGeometry().contains(QCursor::pos()));
    else if (event->type() == QEvent::Enter)
        applyInteractionOpacity(isActiveWindow(), true);
    else if (event->type() == QEvent::Leave)
        applyInteractionOpacity(isActiveWindow(), frameGeometry().contains(QCursor::pos()));
    return handled;
}
