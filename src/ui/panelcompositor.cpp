#include "panelcompositor.h"
#include "nativepanelcomposition.h"

#include <QApplication>
#include <QDockWidget>
#include <QKeyEvent>
#include <QLayout>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPainter>
#include <QShortcutEvent>
#include <QStyle>
#include <QScopedValueRollback>

PanelCompositor::PanelCompositor(QMainWindow* window)
    : QWidget(window), host(window), native(std::make_unique<NativePanelComposition>())
{
    setObjectName(QStringLiteral("panelCompositor"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setFocusPolicy(Qt::NoFocus);
    hide();
    completionTimer.setSingleShot(true);
    completionTimer.setTimerType(Qt::PreciseTimer);
    rasterTimer.setTimerType(Qt::PreciseTimer);
    rasterTimer.setInterval(16);
    connect(&completionTimer, &QTimer::timeout, this, &PanelCompositor::completeNow);
    connect(&rasterTimer, &QTimer::timeout, this, qOverload<>(&QWidget::update));
    native->warmUp();
}

PanelCompositor::~PanelCompositor()
{
    qApp->removeEventFilter(this);
}

PanelCompositor* PanelCompositor::forWindow(QMainWindow* window)
{
    if (!window) return nullptr;
    if (auto* existing = window->findChild<PanelCompositor*>(QString(), Qt::FindDirectChildrenOnly)) return existing;
    return new PanelCompositor(window);
}

qint64 PanelCompositor::snapshotBytes() const
{
    qint64 bytes = 0;
    for (const auto& layer : layers) bytes += layer.image.sizeInBytes();
    return bytes;
}

qreal PanelCompositor::progress() const
{
    if (!active) return targetExtent;
    const qreal remaining = 1 - qBound<qreal>(0, elapsed.nsecsElapsed() / (durationMs * 1e6), 1);
    return targetExtent + (startExtent - targetExtent) * remaining * remaining * remaining;
}

qreal PanelCompositor::extent() const
{
    return progress() * fullExtent;
}

QRect PanelCompositor::workspaceRect() const
{
    QRect area = host->centralWidget()->geometry();
    for (auto* dock : host->findChildren<QDockWidget*>(QString(), Qt::FindDirectChildrenOnly)) {
        if (dock->isVisible() && !dock->isFloating() && host->dockWidgetArea(dock) == Qt::BottomDockWidgetArea)
            area = area.united(dock->geometry());
    }
    return area;
}

void PanelCompositor::settle() { completeNow(); }
void PanelCompositor::settleFor(QObject* owner) { if (isActiveFor(owner)) settle(); }

bool PanelCompositor::present(QObject* owner, const QRect& area, QList<PanelMotionLayer> images,
                              bool fromOpen, bool toOpen)
{
    settle();
    if (!host->isVisible() || area.isEmpty() || images.isEmpty() || pointerTarget) return false;
    qint64 bytes = 0;
    for (const auto& layer : images) {
        if (layer.image.isNull()) return false;
        bytes += layer.image.sizeInBytes();
    }
    if (bytes > 96 * 1024 * 1024) return false;
    setGeometry(area);
    motionOwner = owner;
    navigationDock.clear(); priorFocus.clear();
    layers = std::move(images);
    fullExtent = 1;
    lastRenderer = native->prepare(host, area, layers) ? QStringLiteral("direct-composition") : QStringLiteral("raster");
    return startMotion(fromOpen ? 1 : 0, toOpen ? 1 : 0, duration, [this] { finish(); });
}

bool PanelCompositor::reverse(QObject* owner, bool open, const std::function<void()>& apply)
{
    if (!isActiveFor(owner)) return false;
    const qreal from = progress();
    {
        QScopedValueRollback<bool> guard(preparing, true);
        apply(); host->layout()->activate();
    }
    return startMotion(from, open ? 1 : 0, duration, [this] { finish(); });
}

void PanelCompositor::reveal(QObject* owner, Qt::Edge edge, bool open,
                            const std::function<QRect()>& panelRect, const std::function<void()>& apply,
                            int stationaryBottom)
{
    if (reverse(owner, open, apply)) return;
    settle();
    if (!host->isVisible() || !host->centralWidget()) { apply(); return; }
    QElapsedTimer preparation; preparation.start();
    QRect area, panel;
    QImage background, foreground;
    const auto captureBackground = [&] {
        host->layout()->activate();
        area = workspaceRect();
        area.setBottom(area.bottom() - stationaryBottom);
        if (area.width() * host->devicePixelRatioF() * area.height() * host->devicePixelRatioF() > 20 * 1024 * 1024) return;
        background = host->grab(area).toImage();
    };
    const auto capturePanel = [&] {
        host->layout()->activate();
        panel = panelRect();
        if (!panel.isEmpty() && panel.width() * host->devicePixelRatioF() * panel.height() * host->devicePixelRatioF() <= 20 * 1024 * 1024)
            foreground = host->grab(panel).toImage();
    };
    if (open) captureBackground(); else capturePanel();
    apply();
    if (open) capturePanel(); else captureBackground();
    if (background.isNull() || foreground.isNull()) return;
    auto backdrop = PanelMotionLayer::stationary(background);
    auto pane = PanelMotionLayer::stationary(foreground);
    pane.openPosition = panel.topLeft() - area.topLeft();
    pane.closedPosition = pane.openPosition;
    if (edge == Qt::RightEdge) pane.closedPosition.setX(area.width());
    else if (edge == Qt::BottomEdge) pane.closedPosition.setY(area.height());
    else if (edge == Qt::LeftEdge) {
        pane.closedPosition.setX(-panel.width());
        const int separator = host->style()->pixelMetric(QStyle::PM_DockWidgetSeparatorExtent);
        backdrop.openPosition.setX(panel.width() + separator);
    }
    present(owner, area, {backdrop, pane}, !open, open);
    lastPreparationMs = preparation.nsecsElapsed() / 1e6;
}

bool PanelCompositor::begin(QDockWidget* dock, QWidget* content, int expandedWidth,
                              int targetWidth, int duration, std::function<void()> complete)
{
    if (!host->isVisible() || !host->centralWidget() || !content || duration <= 0 || pointerTarget) return false;
    QElapsedTimer preparation; preparation.start();
    const bool reversing = isActiveFor(dock);
    if (!reversing) settle();
    const qreal previous = progress();
    preparing = true;
    if (!reversing) {
        navigationDock = dock;
        motionOwner = dock;
        priorFocus = QApplication::focusWidget();
        const bool wasVisible = !dock->isHidden();
        const auto centralBefore = host->centralWidget()->geometry();
        const int separator = host->style()->pixelMetric(QStyle::PM_DockWidgetSeparatorExtent);
        fullExtent = wasVisible ? centralBefore.left() - dock->x() : expandedWidth + separator;
        fullExtent = qMax<qreal>(expandedWidth, fullExtent);
        // The live workspace is wide and stable for the entire transition.
        dock->hide();
        host->layout()->activate();
        const QRect area = workspaceRect();
        if (area.width() <= fullExtent || area.height() <= 0) {
            preparing = false;
            return false;
        }
        setGeometry(area);
        const qreal dpr = host->devicePixelRatioF();
        // Keep transient storage bounded, including high-DPI and very large desktops.
        const qint64 pixels = qint64(qCeil((area.width() + fullExtent) * dpr)) * qCeil(area.height() * dpr);
        if (pixels > 24 * 1024 * 1024) {
            preparing = false;
            return false;
        }
        auto workspaceImage = host->grab(area).toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        auto sidebarImage = QImage(QSize(qCeil(fullExtent * dpr), qCeil(area.height() * dpr)),
                              QImage::Format_ARGB32_Premultiplied);
        if (workspaceImage.isNull() || sidebarImage.isNull()) {
            workspaceImage = {}; sidebarImage = {};
            preparing = false;
            return false;
        }
        sidebarImage.setDevicePixelRatio(dpr);
        sidebarImage.fill(host->palette().color(QPalette::Window));
        // Rendering a hidden child temporarily exposes its ancestors inside Qt.
        // Do not let that preparation resize the real central widget twice.
        const bool layoutEnabled = host->layout()->isEnabled();
        host->layout()->setEnabled(false);
        const auto previousSize = content->size();
        const int minimumWidth = content->minimumWidth();
        const int maximumWidth = content->maximumWidth();
        // Include the actual Ela frame/background, not just its transparent body.
        // A collapsed bar has a fixed zero width, so expand it only for rendering.
        content->setFixedWidth(expandedWidth);
        content->resize(expandedWidth, area.height());
        content->ensurePolished();
        if (content->layout()) content->layout()->activate();
        content->render(&sidebarImage);
        content->setMinimumWidth(minimumWidth);
        content->setMaximumWidth(maximumWidth);
        content->resize(previousSize);
        host->layout()->setEnabled(layoutEnabled);
        auto workspace = PanelMotionLayer::stationary(workspaceImage);
        workspace.openPosition = QPointF(fullExtent, 0);
        auto sidebar = PanelMotionLayer::stationary(sidebarImage);
        sidebar.closedPosition = QPointF(-fullExtent, 0);
        layers = {workspace, sidebar};
        startExtent = wasVisible ? 1 : 0;
        lastRenderer = native->prepare(host, area, layers)
            ? QStringLiteral("direct-composition") : QStringLiteral("raster");
    } else {
        startExtent = previous;
    }
    preparing = false;
    const bool result = startMotion(startExtent, targetWidth > 0 ? 1 : 0, duration, std::move(complete));
    lastPreparationMs = preparation.nsecsElapsed() / 1e6;
    return result;
}

bool PanelCompositor::startMotion(qreal from, qreal to, int duration, std::function<void()> complete)
{
    startExtent = from; targetExtent = to;
    durationMs = duration;
    completion = std::move(complete);
    active = true;
    elapsed.start();
    if (lastRenderer == QStringLiteral("direct-composition")
        && !native->animate(layers, startExtent, targetExtent, durationMs)) {
        native->clear();
        lastRenderer = QStringLiteral("raster");
    }
    if (lastRenderer == QStringLiteral("raster")) {
        show(); raise();
        rasterTimer.start();
    }
    completionTimer.start(durationMs);
    qApp->installEventFilter(this);
    preparing = false;
    disconnect(ownerDestroyed);
    if (motionOwner) ownerDestroyed = connect(motionOwner, &QObject::destroyed, this, [this] { finish(); });
    emit started();
    return true;
}

void PanelCompositor::completeNow()
{
    if (!active || preparing) return;
    completionTimer.stop(); rasterTimer.stop();
    const auto callback = std::move(completion);
    if (callback) callback();
}

void PanelCompositor::finish()
{
    if (!active) return;
    preparing = true;
    completionTimer.stop(); rasterTimer.stop(); completion = {};
    if (!qobject_cast<QMainWindow*>(host) || !host->layout()) {
        hide(); native->clear(); layers.clear(); active = false;
        qApp->removeEventFilter(this); preparing = false; return;
    }
    host->layout()->activate();
    if (targetExtent > 0 && priorFocus && priorFocus->isVisible()
        && navigationDock && navigationDock->isAncestorOf(priorFocus)) priorFocus->setFocus();
    priorFocus.clear();
    // On Windows the independent visual stays above the host while Qt paints
    // the final layout, including restored focus. Detach it only afterwards.
    hide();
    host->repaint(geometry());
    native->clear();
    layers.clear();
    active = false;
    disconnect(ownerDestroyed);
    motionOwner.clear();
    qApp->removeEventFilter(this);
    preparing = false;
    emit finished();
}

double PanelCompositor::compositionRefreshRate() const { return native->refreshRate(); }

void PanelCompositor::paintEvent(QPaintEvent*)
{
    if (!active) return;
    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Window));
    const qreal value = progress();
    // No scaling or text re-rasterization between frames.
    for (const auto& layer : layers) {
        painter.save();
        painter.setOpacity(layer.opacity(value));
        painter.translate(layer.position(value));
        painter.setClipRect(QRectF(QPointF(), layer.clip(value)), Qt::IntersectClip);
        painter.drawImage(QPointF(), layer.image);
        painter.restore();
    }
}

bool PanelCompositor::eventFilter(QObject* watched, QEvent* event)
{
    if (preparing || forwardingInput) return false;
    if (pointerTarget && (event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonRelease)) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        auto* target = pointerTarget.data();
        if (event->type() == QEvent::MouseButtonRelease && mouse->button() == pointerButton) {
            pointerTarget.clear();
            pointerButton = Qt::NoButton;
            qApp->removeEventFilter(this);
        }
        forwardMouseEvent(target, mouse);
        return true;
    }
    if (!active) {
        if (!pointerTarget) qApp->removeEventFilter(this);
        return false;
    }
    if (!qobject_cast<QMainWindow*>(host)) {
        // QWidget's destructor sends Hide after QMainWindow's layout is gone.
        completionTimer.stop(); rasterTimer.stop(); completion = {};
        active = false;
        qApp->removeEventFilter(this);
        native->clear();
        return false;
    }
    auto* widget = qobject_cast<QWidget*>(watched);
    if (!widget || widget->window() != host) return false;
    const auto type = event->type();
    if (watched == host && (type == QEvent::Resize || type == QEvent::Hide
        || type == QEvent::Close || type == QEvent::WindowDeactivate
        || type == QEvent::WindowStateChange || type == QEvent::DevicePixelRatioChange
        || type == QEvent::PaletteChange || type == QEvent::FontChange || type == QEvent::StyleChange)) {
        completeNow();
    } else if (type == QEvent::KeyPress || type == QEvent::ShortcutOverride) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->modifiers() != Qt::ControlModifier
            || (key->key() != Qt::Key_1 && key->key() != Qt::Key_2 && key->key() != Qt::Key_J)) completeNow();
    } else if (type == QEvent::Shortcut) {
        const auto key = static_cast<QShortcutEvent*>(event)->key();
        if (key != QKeySequence(Qt::CTRL | Qt::Key_1) && key != QKeySequence(Qt::CTRL | Qt::Key_2)
            && key != QKeySequence(Qt::CTRL | Qt::Key_J)) completeNow();
    } else if (type == QEvent::Wheel) {
        const auto* wheel = static_cast<QWheelEvent*>(event);
        const QPoint global = wheel->globalPosition().toPoint();
        completeNow();
        if (auto* target = host->childAt(host->mapFromGlobal(global))) {
            forwardingInput = true;
            QWheelEvent copy(target->mapFromGlobal(global), global, wheel->pixelDelta(), wheel->angleDelta(),
                             wheel->buttons(), wheel->modifiers(), wheel->phase(), wheel->inverted(),
                             wheel->source(), wheel->pointingDevice());
            QApplication::sendEvent(target, &copy);
            forwardingInput = false;
            return true;
        }
    } else if (type == QEvent::InputMethod || type == QEvent::DragEnter
        || type == QEvent::ContextMenu || type == QEvent::TouchBegin
        || type == QEvent::ApplicationPaletteChange || type == QEvent::ThemeChange) {
        completeNow();
    } else if (type == QEvent::MouseButtonPress || type == QEvent::MouseButtonDblClick) {
        if (widget->objectName() == QStringLiteral("collapseProjectSidebarButton")
            || widget->objectName() == QStringLiteral("expandProjectSidebarButton")
            || widget->property("panelMotionToggle").toBool()) return false;
        const auto* mouse = static_cast<QMouseEvent*>(event);
        const QPoint global = mouse->globalPosition().toPoint();
        completeNow();
        // QWidget hit testing happened before the layout settled. Retarget this press.
        if (auto* target = host->childAt(host->mapFromGlobal(global))) {
            if (target != widget) {
                // Qt may still send the release to the receiver of the original press.
                pointerTarget = target;
                pointerButton = mouse->button();
                qApp->installEventFilter(this);
            }
            forwardMouseEvent(target, const_cast<QMouseEvent*>(mouse));
            return true;
        }
    }
    return false;
}

void PanelCompositor::forwardMouseEvent(QWidget* target, QMouseEvent* event)
{
    const QPointF global = event->globalPosition();
    QMouseEvent copy(event->type(), target->mapFromGlobal(global), host->mapFromGlobal(global), global,
                     event->button(), event->buttons(), event->modifiers(), event->pointingDevice());
    forwardingInput = true;
    const QPointer<PanelCompositor> alive(this);
    QApplication::sendEvent(target, &copy);
    if (alive) forwardingInput = false;
}
