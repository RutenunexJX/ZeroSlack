#ifndef INSIGHTGRAPHVIEW_H
#define INSIGHTGRAPHVIEW_H

#include "zeroslackexport.h"

#include <QGraphicsView>
#include <QPoint>
#include <QPointF>
#include <QRectF>
#include <Qt>

#include <functional>

class ZEROSLACK_API InsightGraphView : public QGraphicsView
{
public:
    explicit InsightGraphView(QWidget* parent = nullptr);
    explicit InsightGraphView(QGraphicsScene* scene, QWidget* parent = nullptr);

    void applyInsightGraphStyle();
    void setZoomRange(qreal minimumScale, qreal maximumScale);
    void setGridVisible(bool visible);
    void setBorderVisible(bool visible);
    void setClearSelectionOnEmptyLeftClick(bool enabled);

    void setPressHandler(
        std::function<bool(const QPoint&,
                           Qt::MouseButton,
                           Qt::KeyboardModifiers)> handler);
    void setDoubleClickHandler(std::function<bool(const QPoint&)> handler);
    void setZoomChangedHandler(std::function<void(qreal)> handler);
    void setViewportResizeHandler(std::function<void()> handler);
    void setViewportInteractionHandler(std::function<void()> handler);
    void setFitUpscalingEnabled(bool enabled);

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
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    bool gridVisible = false;
    bool borderVisible = true;
    QPoint pressPosition;
    bool clearSelectionOnEmptyLeftClick = false;
    bool fitUpscalingEnabled = true;
    qreal minimumZoom = 0.18;
    qreal maximumZoom = 4.5;
    qreal zoomStep = 1.15;
    qreal gridSize = 28.0;
    std::function<bool(const QPoint&,
                       Qt::MouseButton,
                       Qt::KeyboardModifiers)> pressHandler;
    std::function<bool(const QPoint&)> doubleClickHandler;
    std::function<void(qreal)> zoomChangedHandler;
    std::function<void()> viewportResizeHandler;
    std::function<void()> viewportInteractionHandler;

    void initializeThemeConnection();
    void notifyZoomChanged();
};

#endif // INSIGHTGRAPHVIEW_H
