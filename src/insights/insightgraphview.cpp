#include "insightgraphview.h"

#include "insightvisualstyle.h"

#include <QGraphicsScene>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QWheelEvent>

#include <cmath>
#include <utility>

InsightGraphView::InsightGraphView(QWidget* parent)
    : QGraphicsView(parent)
{
    initializeThemeConnection();
    applyInsightGraphStyle();
}

InsightGraphView::InsightGraphView(QGraphicsScene* scene, QWidget* parent)
    : QGraphicsView(scene, parent)
{
    initializeThemeConnection();
    applyInsightGraphStyle();
}

void InsightGraphView::initializeThemeConnection()
{
    QObject::connect(
        &ApplicationThemeManager::instance(),
        &ApplicationThemeManager::themeChanged,
        this,
        [this](ThemeMode) {
            applyInsightGraphStyle();
            if (viewport())
                viewport()->update();
        });
}

void InsightGraphView::applyInsightGraphStyle()
{
    setRenderHint(QPainter::Antialiasing, true);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setFocusPolicy(Qt::StrongFocus);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setBackgroundBrush(InsightVisualStyle::canvasBrush());
    setStyleSheet(InsightVisualStyle::graphViewStyleSheet(objectName()));
}

void InsightGraphView::setZoomRange(qreal minimumScale, qreal maximumScale)
{
    if (minimumScale <= 0.0 || maximumScale <= 0.0
        || minimumScale > maximumScale) {
        return;
    }
    minimumZoom = minimumScale;
    maximumZoom = maximumScale;
}

void InsightGraphView::setGridVisible(bool visible)
{
    if (gridVisible == visible)
        return;
    gridVisible = visible;
    viewport()->update();
}

void InsightGraphView::setClearSelectionOnEmptyLeftClick(bool enabled)
{
    clearSelectionOnEmptyLeftClick = enabled;
}

void InsightGraphView::setPressHandler(
    std::function<bool(const QPoint&,
                       Qt::MouseButton,
                       Qt::KeyboardModifiers)> handler)
{
    pressHandler = std::move(handler);
}

void InsightGraphView::setDoubleClickHandler(
    std::function<bool(const QPoint&)> handler)
{
    doubleClickHandler = std::move(handler);
}

void InsightGraphView::setZoomChangedHandler(std::function<void(qreal)> handler)
{
    zoomChangedHandler = std::move(handler);
}

qreal InsightGraphView::currentZoom() const
{
    return transform().m11();
}

void InsightGraphView::zoomBy(qreal factor)
{
    if (factor <= 0.0)
        return;
    const qreal nextScale = currentZoom() * factor;
    if ((nextScale < minimumZoom && factor < 1.0)
        || (nextScale > maximumZoom && factor > 1.0)) {
        return;
    }
    scale(factor, factor);
    notifyZoomChanged();
}

void InsightGraphView::zoomIn()
{
    zoomBy(zoomStep);
}

void InsightGraphView::zoomOut()
{
    zoomBy(1.0 / zoomStep);
}

void InsightGraphView::resetView()
{
    resetTransform();
    notifyZoomChanged();
}

void InsightGraphView::fitScene(Qt::AspectRatioMode mode)
{
    if (!scene() || scene()->sceneRect().isEmpty())
        return;
    fitRect(scene()->sceneRect(), mode);
}

void InsightGraphView::fitRect(const QRectF& rect, Qt::AspectRatioMode mode)
{
    if (rect.isEmpty())
        return;
    fitInView(rect, mode);
    notifyZoomChanged();
}

void InsightGraphView::centerOnRect(const QRectF& rect)
{
    if (!rect.isEmpty())
        centerOnPoint(rect.center());
}

void InsightGraphView::centerOnPoint(const QPointF& point)
{
    centerOn(point);
}

void InsightGraphView::drawBackground(QPainter* painter, const QRectF& rect)
{
    // A floating host supplies the material underneath the canvas. Keep scene
    // items and grid intact, and keep the normal brush for docking and export.
    if (window()->objectName() != QStringLiteral("contextFloatingWindow"))
        QGraphicsView::drawBackground(painter, rect);
    if (!gridVisible || !painter)
        return;

    const qreal left = std::floor(rect.left() / gridSize) * gridSize;
    const qreal top = std::floor(rect.top() / gridSize) * gridSize;
    QPen pen(InsightVisualStyle::theme().graph.gridLine);
    pen.setWidthF(0.0);
    painter->setPen(pen);
    for (qreal x = left; x <= rect.right(); x += gridSize)
        painter->drawLine(QLineF(x, rect.top(), x, rect.bottom()));
    for (qreal y = top; y <= rect.bottom(); y += gridSize)
        painter->drawLine(QLineF(rect.left(), y, rect.right(), y));
}

void InsightGraphView::wheelEvent(QWheelEvent* event)
{
    if (!event) {
        QGraphicsView::wheelEvent(event);
        return;
    }
    zoomBy(event->angleDelta().y() > 0 ? zoomStep : 1.0 / zoomStep);
    event->accept();
}

void InsightGraphView::mousePressEvent(QMouseEvent* event)
{
    if (event && pressHandler
        && pressHandler(event->pos(), event->button(), event->modifiers())) {
        event->accept();
        return;
    }
    if (event && clearSelectionOnEmptyLeftClick
        && event->button() == Qt::LeftButton
        && items(event->pos()).isEmpty() && scene()) {
        scene()->clearSelection();
    }
    QGraphicsView::mousePressEvent(event);
}

void InsightGraphView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event && event->button() == Qt::LeftButton && doubleClickHandler
        && doubleClickHandler(event->pos())) {
        event->accept();
        return;
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}

void InsightGraphView::notifyZoomChanged()
{
    if (zoomChangedHandler)
        zoomChangedHandler(currentZoom());
}
