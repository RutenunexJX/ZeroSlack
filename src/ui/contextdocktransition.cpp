#include "contextdocktransition.h"
#include "contextdockhost.h"
#include "contextfloatingwindow.h"
#include "nativepanelcomposition.h"
#include "panelcompositor.h"
#include <QApplication>
#include <QDockWidget>
#include <QLayout>
#include <QMainWindow>
#include <QPainter>
#include <QScreen>
#include <QScopedValueRollback>
#include <QWindow>

namespace {
constexpr qint64 maximumBytes = 96 * 1024 * 1024;
QRect globalRect(QWidget* widget) { return {widget->mapToGlobal(QPoint()), widget->size()}; }
struct Snapshot { QImage image; QRect rect; };
Snapshot capture(QWidget* widget, qreal scale, const QRect& clip = {})
{
    const QRect area = clip.isEmpty() ? widget->rect() : clip;
    if (area.isEmpty() || qint64(qCeil(area.width() * scale)) * qCeil(area.height() * scale) * 4 > maximumBytes / 3)
        return {};
    auto image = widget->grab(area).toImage();
    if (!qFuzzyCompare(image.devicePixelRatio(), scale)) {
        image = image.scaled(qCeil(area.width() * scale), qCeil(area.height() * scale),
                             Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        image.setDevicePixelRatio(scale);
    }
    return {image, QRect(widget->mapToGlobal(area.topLeft()), area.size())};
}
QRect workspaceRect(QMainWindow* window)
{
    QRect area = window->centralWidget()->geometry();
    for (auto* dock : window->findChildren<QDockWidget*>(QString(), Qt::FindDirectChildrenOnly))
        if (dock->isVisible() && !dock->isFloating()) area = area.united(dock->geometry());
    return area.intersected(window->rect());
}
QHash<QString, Snapshot> captureSections(ContextDockHost* dock, qreal scale)
{
    QHash<QString, Snapshot> result;
    for (const auto& key : dock->resourceKeys()) {
        auto* section = dock->sectionWidget(key);
        const QRect viewport = dock->viewportGlobalRect(dock->isBottomResource(key));
        if (!section || !section->isVisible() || viewport.isEmpty()) continue;
        const auto visible = globalRect(section).intersected(viewport);
        // Capture the composited parent background as well as transparent section children.
        auto* window = dock->window();
        if (!visible.isEmpty()) result.insert(key, capture(window, scale, visible.translated(-globalRect(window).topLeft())));
    }
    return result;
}
void eraseSections(Snapshot& background, const QHash<QString, Snapshot>& sections, const QColor& color)
{
    QPainter painter(&background.image);
    for (const auto& item : sections)
        painter.fillRect(item.rect.translated(-background.rect.topLeft()), color);
}
void appendTransition(QList<PanelMotionLayer>& layers, const Snapshot& before, const Snapshot& after,
                      const QPoint& origin)
{
    if (!before.image.isNull()) {
        auto layer = PanelMotionLayer::stationary(before.image);
        layer.closedPosition = before.rect.topLeft() - origin;
        layer.openPosition = after.rect.topLeft() - origin;
        layer.openClip = after.rect.size();
        layer.openOpacity = 0;
        layer.opacityStart = .97;
        layers.append(layer);
    }
    if (!after.image.isNull()) {
        auto layer = PanelMotionLayer::stationary(after.image);
        layer.closedPosition = before.rect.topLeft() - origin;
        layer.openPosition = after.rect.topLeft() - origin;
        layer.closedClip = before.rect.size();
        layer.closedOpacity = 0;
        layer.opacityStart = .97;
        layers.append(layer);
    }
}
}

ContextDockTransition::ContextDockTransition(QMainWindow* window, ContextDockHost* dockHost)
    : QWidget(window, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus | Qt::WindowTransparentForInput),
      host(window), dock(dockHost), native(std::make_unique<NativePanelComposition>())
{
    setObjectName(QStringLiteral("contextDockTransition"));
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);
    completionTimer.setSingleShot(true);
    completionTimer.setTimerType(Qt::PreciseTimer);
    rasterTimer.setInterval(16);
    rasterTimer.setTimerType(Qt::PreciseTimer);
    connect(&completionTimer, &QTimer::timeout, this, &ContextDockTransition::finish);
    connect(&rasterTimer, &QTimer::timeout, this, qOverload<>(&QWidget::update));
    connect(PanelCompositor::forWindow(window), &PanelCompositor::started, this, &ContextDockTransition::finish);
    hide();
}
ContextDockTransition::~ContextDockTransition()
{
    qApp->removeEventFilter(this);
}

ContextDockTarget ContextDockTransition::targetAt(const QPoint& position, QWidget* incoming,
                                                 QDockWidget* side, QDockWidget* bottom,
                                                 int preferredWidth, int preferredHeight) const
{
    if (!host || !host->isVisible() || host->isMinimized() || !host->centralWidget() || !dock || !incoming) return {};
    ContextDockTarget target;
    QRect area;
    const QRect sideViewport = dock->viewportGlobalRect(false);
    const QRect bottomViewport = dock->viewportGlobalRect(true);
    if (bottomViewport.contains(position)) {
        target.bottom = true;
        area = bottomViewport;
    } else if (sideViewport.contains(position)) {
        area = sideViewport;
    } else {
        const QRect center = globalRect(host->centralWidget());
        if (!center.contains(position)) return {};
        constexpr int edgeZone = 48;
        if (!bottom->isVisible() && position.y() >= center.bottom() - edgeZone) {
            target.bottom = true;
            area = center;
            const int height = qMin(preferredHeight, qMax(80, center.height() - 160));
            area.setTop(area.bottom() - height + 1);
        } else if (!side->isVisible()) {
            const bool left = host->dockWidgetArea(side) == Qt::LeftDockWidgetArea;
            if (left ? position.x() > center.left() + edgeZone : position.x() < center.right() - edgeZone) return {};
            area = center;
            const int width = qMin(preferredWidth, qMax(120, center.width() - 240));
            if (left) area.setRight(area.left() + width - 1);
            else area.setLeft(area.right() - width + 1);
        } else return {};
    }
    const bool visible = target.bottom ? bottom->isVisible() : side->isVisible();
    target.index = visible ? dock->insertionIndex(position) : dock->resourceCount();
    target.rect = dock->projectedSectionRect(target.bottom, target.index, area, incoming);
    return target;
}

void ContextDockTransition::preview(const ContextDockTarget& target)
{
    if (animating) finish();
    if (!target.isValid()) { clearPreview(); return; }
    native->warmUp();
    previewing = true;
    setGeometry(target.rect);
    qApp->installEventFilter(this);
    show(); raise(); update();
}
void ContextDockTransition::clearPreview()
{
    if (!previewing) return;
    previewing = false;
    hide();
    qApp->removeEventFilter(this);
}
qint64 ContextDockTransition::snapshotBytes() const
{
    qint64 result = 0;
    for (const auto& layer : layers) result += layer.image.sizeInBytes();
    return result;
}
qreal ContextDockTransition::progress() const
{
    const qreal remaining = 1 - qBound<qreal>(0, elapsed.nsecsElapsed() / (duration * 1e6), 1);
    return 1 - remaining * remaining * remaining;
}

bool ContextDockTransition::transfer(ContextFloatingWindow* source, const QString& key, const std::function<bool()>& apply)
{
    finish(); clearPreview();
    if (!source || !host || !dock || !host->isVisible() || !source->isVisible()) return apply();
    native->warmUp();
    PanelCompositor::forWindow(host)->settle();
    const QScopedValueRollback<bool> guard(preparing, true);
    winId();
    windowHandle()->setScreen(host->screen());
    const qreal scale = host->devicePixelRatioF();
    const QRect body = workspaceRect(host);
    auto background = capture(host, scale, body);
    auto floating = capture(source, scale);
    const QRect envelope = background.rect.united(floating.rect);
    if (background.image.isNull() || floating.image.isNull()
        || qint64(qCeil(envelope.width() * scale)) * qCeil(envelope.height() * scale) * 4 > maximumBytes)
        return apply();
    const auto before = captureSections(dock, scale);
    setGeometry(envelope);
    for (const auto& shot : {background, floating}) {
        auto layer = PanelMotionLayer::stationary(shot.image);
        layer.closedPosition = layer.openPosition = shot.rect.topLeft() - envelope.topLeft();
        layers.append(layer);
    }
    // Cover both windows before reparenting. This surface never receives focus or input.
    animating = true;
    elapsed.start();
    show(); raise(); repaint();
    const bool accepted = apply();
    if (!accepted || !dock->containsResource(key)) {
        animating = false; layers.clear(); hide(); return accepted;
    }
    host->layout()->activate();
    dock->activateResource(key);
    destination = dock->sectionWidget(key);
    if (destination && destination->layout()) destination->layout()->activate();
    auto afterBackground = capture(host, scale, body);
    const auto after = captureSections(dock, scale);
    if (afterBackground.image.isNull() || !after.contains(key) || after.value(key).image.isNull()) {
        animating = false; layers.clear(); host->repaint(body); hide(); return true;
    }
    eraseSections(background, before, dock->palette().color(QPalette::Window));
    eraseSections(afterBackground, after, dock->palette().color(QPalette::Window));
    layers.clear();
    auto backdrop = PanelMotionLayer::stationary(afterBackground.image);
    backdrop.closedPosition = backdrop.openPosition = afterBackground.rect.topLeft() - envelope.topLeft();
    layers.append(backdrop);
    auto oldBackdrop = PanelMotionLayer::stationary(background.image);
    oldBackdrop.closedPosition = oldBackdrop.openPosition = background.rect.topLeft() - envelope.topLeft();
    oldBackdrop.openOpacity = 0;
    layers.append(oldBackdrop);
    for (auto it = after.cbegin(); it != after.cend(); ++it) {
        if (it.key() == key) continue;
        appendTransition(layers, before.value(it.key(), Snapshot{{}, it.value().rect}), it.value(), envelope.topLeft());
    }
    appendTransition(layers, floating, after.value(key), envelope.topLeft());
    if (snapshotBytes() > maximumBytes) {
        animating = false; layers.clear(); host->repaint(body); hide(); return true;
    }
    nativeActive = native->prepare(this, rect(), layers) && native->animate(layers, 0, 1, duration);
    if (!nativeActive) { native->clear(); rasterTimer.start(); }
    // Clear the raster backing only after the native visual has been committed.
    elapsed.restart();
    repaint();
    completionTimer.start(duration);
    qApp->installEventFilter(this);
    destinationDestroyed = connect(destination, &QObject::destroyed, this, &ContextDockTransition::finish);
    emit started();
    return true;
}

void ContextDockTransition::finish()
{
    if (preparing) return;
    clearPreview();
    if (!animating) return;
    const QScopedValueRollback<bool> guard(preparing, true);
    completionTimer.stop(); rasterTimer.stop();
    // Paint the live endpoint underneath the overlay before removing it.
    if (host && qobject_cast<QMainWindow*>(host.data()) && host->layout()) {
        host->layout()->activate();
        host->repaint();
    }
    hide(); native->clear();
    nativeActive = false;
    layers.clear(); destination.clear();
    disconnect(destinationDestroyed);
    animating = false;
    qApp->removeEventFilter(this);
    emit finished();
}

void ContextDockTransition::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(rect(), Qt::transparent);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    if (previewing) {
        painter.setRenderHint(QPainter::Antialiasing);
        QColor color = host ? host->palette().color(QPalette::Highlight) : palette().color(QPalette::Highlight);
        color.setAlpha(42); painter.setBrush(color);
        color.setAlpha(165); painter.setPen(QPen(color, 1));
        painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 6, 6);
    } else if (animating && !nativeActive) {
        const qreal value = preparing ? 0 : progress();
        for (const auto& layer : layers) {
            painter.save();
            painter.setOpacity(layer.opacity(value));
            painter.translate(layer.position(value));
            painter.setClipRect(QRectF(QPointF(), layer.clip(value)));
            painter.drawImage(QPointF(), layer.image);
            painter.restore();
        }
    }
}

bool ContextDockTransition::eventFilter(QObject* watched, QEvent* event)
{
    if (preparing || (!animating && !previewing)) return false;
    const auto type = event->type();
    auto* area = qobject_cast<QDockWidget*>(watched);
    const bool layoutOwner = watched == host || (animating && area && area->parentWidget() == host);
    if (type == QEvent::ApplicationDeactivate || type == QEvent::ApplicationPaletteChange
        || (animating && watched == destination && type == QEvent::Hide)
        || (layoutOwner && (type == QEvent::Resize || type == QEvent::Move || type == QEvent::Hide
            || type == QEvent::Close || type == QEvent::WindowStateChange || type == QEvent::DevicePixelRatioChange
            || type == QEvent::PaletteChange || type == QEvent::StyleChange || type == QEvent::FontChange))) {
        finish();
    } else if (animating && (type == QEvent::MouseButtonPress || type == QEvent::MouseButtonDblClick
        || type == QEvent::Wheel || type == QEvent::KeyPress || type == QEvent::ShortcutOverride
        || type == QEvent::TouchBegin || type == QEvent::InputMethod || type == QEvent::DragEnter)) {
        finish();
    }
    return false;
}
