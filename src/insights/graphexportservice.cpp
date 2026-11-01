#include "graphexportservice.h"
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QPointer>
#include <QThread>
#include <QWidget>
#include <QPainter>
#include <QRegion>

namespace {
bool finiteRect(const QRectF& rect)
{
    return qIsFinite(rect.left()) && qIsFinite(rect.top())
        && qIsFinite(rect.width()) && qIsFinite(rect.height());
}
QRectF visibleSceneBounds(QGraphicsScene* scene)
{
    if (!scene)
        return {};

    QRectF bounds;
    bool found = false;
    const QList<QGraphicsItem*> items = scene->items(Qt::AscendingOrder);
    for (QGraphicsItem* item : items) {
        if (!item || !item->isVisible() || item->effectiveOpacity() <= 0.0)
            continue;
        QRectF itemBounds = item->sceneBoundingRect().normalized();
        if (!finiteRect(itemBounds))
            continue;
        if (itemBounds.width() <= 0.0)
            itemBounds.adjust(-0.5, 0.0, 0.5, 0.0);
        if (itemBounds.height() <= 0.0)
            itemBounds.adjust(0.0, -0.5, 0.0, 0.5);
        bounds = found ? bounds.united(itemBounds) : itemBounds;
        found = true;
    }
    return found ? bounds : QRectF();
}

bool sceneHasVisibleContent(QGraphicsScene* scene)
{
    return scene && !visibleSceneBounds(scene).isEmpty();
}

}
GraphExportSource GraphExportSource::fromGraphicsScene(
    QGraphicsScene* scene)
{
    if (!scene)
        return {};
    QPointer<QGraphicsScene> guardedScene(scene);
    return fromRenderer(
        [guardedScene]() {
            return visibleSceneBounds(guardedScene.data());
        },
        [guardedScene]() {
            return sceneHasVisibleContent(guardedScene.data());
        },
        [guardedScene](
            QPainter& painter,
            const QRectF& targetRect,
            const QRectF& sourceRect,
            QString* failureReason) {
            QGraphicsScene* currentScene = guardedScene.data();
            if (!currentScene) {
                if (failureReason)
                    *failureReason = QStringLiteral("graphics scene was destroyed");
                return false;
            }
            if (currentScene->thread() != QThread::currentThread()) {
                if (failureReason) {
                    *failureReason = QStringLiteral(
                        "graphics scene export must run on its owning thread");
                }
                return false;
            }
            currentScene->render(
                &painter,
                targetRect,
                sourceRect,
                Qt::IgnoreAspectRatio);
            return painter.isActive();
        });
}
GraphExportSource GraphExportSource::fromWidget(QWidget* widget)
{
    if (!widget)
        return {};
    QPointer<QWidget> guardedWidget(widget);
    return fromRenderer(
        [guardedWidget]() {
            const QWidget* currentWidget = guardedWidget.data();
            return currentWidget
                ? QRectF(currentWidget->rect())
                : QRectF();
        },
        [guardedWidget]() {
            const QWidget* currentWidget = guardedWidget.data();
            return currentWidget && !currentWidget->size().isEmpty();
        },
        [guardedWidget](
            QPainter& painter,
            const QRectF& targetRect,
            const QRectF& sourceRect,
            QString* failureReason) {
            QWidget* currentWidget = guardedWidget.data();
            if (!currentWidget) {
                if (failureReason)
                    *failureReason = QStringLiteral("widget was destroyed");
                return false;
            }
            if (currentWidget->thread() != QThread::currentThread()) {
                if (failureReason) {
                    *failureReason = QStringLiteral(
                        "widget export must run on its owning thread");
                }
                return false;
            }
            const qreal xScale =
                targetRect.width() / sourceRect.width();
            const qreal yScale =
                targetRect.height() / sourceRect.height();
            painter.save();
            painter.setClipRect(targetRect);
            painter.translate(targetRect.topLeft());
            painter.scale(xScale, yScale);
            painter.translate(-sourceRect.topLeft());
            currentWidget->render(
                &painter,
                QPoint(),
                QRegion(sourceRect.toAlignedRect()),
                QWidget::DrawWindowBackground | QWidget::DrawChildren);
            painter.restore();
            return painter.isActive();
        });
}
GraphExportResult GraphExportService::exportGraphicsScene(
    QGraphicsScene* scene,
    const QString& outputPath,
    const GraphExportOptions& options)
{
    return exportSource(
        GraphExportSource::fromGraphicsScene(scene),
        outputPath,
        options);
}
GraphExportResult GraphExportService::exportWidget(
    QWidget* widget,
    const QString& outputPath,
    const GraphExportOptions& options)
{
    return exportSource(
        GraphExportSource::fromWidget(widget),
        outputPath,
        options);
}
GraphExportResult GraphExportService::exportSource(const GraphExportSource& source,
    const QString& path, const GraphExportOptions& options)
{ return insightgraph::ExportService::exportSource(source, path, options); }
QString GraphExportService::formatName(GraphExportFormat format)
{ return insightgraph::ExportService::formatName(format); }
QString GraphExportService::failureName(GraphExportFailure failure)
{ return insightgraph::ExportService::failureName(failure); }
