#ifndef INSIGHTGRAPHVIEW_H
#define INSIGHTGRAPHVIEW_H

#include <QGraphicsView>
#include <QPoint>
#include <QPointF>
#include <QRectF>
#include <Qt>

#include <functional>

class InsightGraphView : public QGraphicsView
{
public:
    explicit InsightGraphView(QWidget* parent = nullptr);
    explicit InsightGraphView(QGraphicsScene* scene, QWidget* parent = nullptr);

    void applyInsightGraphStyle();
    void setZoomRange(qreal minimumScale, qreal maximumScale);
    void setGridVisible(bool visible);
    void setClearSelectionOnEmptyLeftClick(bool enabled);

    void setPressHandler(
        std::function<bool(const QPoint&,
                           Qt::MouseButton,
                           Qt::KeyboardModifiers)> handler);
    void setDoubleClickHandler(std::function<bool(const QPoint&)> handler);
    void setZoomChangedHandler(std::function<void(qreal)> handler);

    qreal currentZoom() const;
    void zoomBy(qreal factor);
    void zoomIn();
    void zoomOut();
    void resetView();
    void fitScene(Qt::AspectRatioMode mode = Qt::KeepAspectRatio);
    void fitRect(const QRectF& rect,
                 Qt::AspectRatioMode mode = Qt::KeepAspectRatio);
    void centerOnPoint(const QPointF& point);
    void centerOnRect(const QRectF& rect);

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    bool gridVisible = false;
    bool clearSelectionOnEmptyLeftClick = false;
    qreal minimumZoom = 0.18;
    qreal maximumZoom = 4.5;
    qreal zoomStep = 1.15;
    qreal gridSize = 28.0;
    std::function<bool(const QPoint&,
                       Qt::MouseButton,
                       Qt::KeyboardModifiers)> pressHandler;
    std::function<bool(const QPoint&)> doubleClickHandler;
    std::function<void(qreal)> zoomChangedHandler;

    void notifyZoomChanged();
};

#endif // INSIGHTGRAPHVIEW_H
