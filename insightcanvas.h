#ifndef INSIGHTCANVAS_H
#define INSIGHTCANVAS_H

#include "graphexportservice.h"
#include "insightgraphcore.h"
#include "zeroslackexport.h"

#include <QHash>
#include <QPointF>
#include <QStringList>
#include <QWidget>

#include <functional>

class InsightGraphView;
class QGraphicsPathItem;
class QGraphicsRectItem;
class QGraphicsScene;
class QGraphicsSimpleTextItem;

class ZEROSLACK_API InsightCanvas final : public QWidget
{
public:
    using SelectionChangedHandler = std::function<void(const QStringList&)>;
    using ActivationHandler = std::function<void(const InsightGraphNode&)>;

    explicit InsightCanvas(QWidget* parent = nullptr);
    ~InsightCanvas() override;

    void applyUpdate(const InsightGraphUpdate& update);
    void setSnapshot(const InsightGraphSnapshot& snapshot,
                     const InsightGraphDiff& diff = {});
    const InsightGraphSnapshot& snapshot() const;

    QGraphicsScene* scene() const;
    InsightGraphView* view() const;
    InsightGraphView* minimap() const;

    void zoomIn();
    void zoomOut();
    void fit();
    void resetView();
    qreal zoomFactor() const;
    void setPanMode(bool enabled);
    bool panMode() const;
    void setMinimapVisible(bool visible);
    bool isMinimapVisible() const;
    void setSearchText(const QString& text);

    QStringList selectedSymbolIds() const;
    void selectSymbolIds(const QStringList& symbolIds);
    QHash<QString, QPointF> nodePositions() const;
    void setSelectionChangedHandler(SelectionChangedHandler handler);
    void setActivationHandler(ActivationHandler handler);

    GraphExportResult exportGraph(
        const QString& outputPath,
        const GraphExportOptions& options = {}) const;

    int nodeItemCountForTest() const;
    int edgeItemCountForTest() const;

private:
    struct EdgeVisual {
        QGraphicsPathItem* path = nullptr;
        QGraphicsSimpleTextItem* label = nullptr;
    };

    QGraphicsScene* graphScene = nullptr;
    InsightGraphView* graphView = nullptr;
    InsightGraphView* minimapView = nullptr;
    QHash<QString, QGraphicsRectItem*> nodeItems;
    QHash<QString, EdgeVisual> edgeItems;
    InsightGraphSnapshot currentSnapshot;
    QString searchText;
    bool panModeValue = false;
    SelectionChangedHandler selectionChangedHandler;
    ActivationHandler activationHandler;

    void clearVisuals();
    void removeNode(const QString& symbolId);
    void removeEdge(const QString& edgeId);
    void addOrUpdateNode(const InsightGraphNode& node,
                         const QPointF* initialPosition = nullptr);
    void addOrUpdateEdge(const InsightGraphEdge& edge);
    void updateAllEdgePaths();
    void updateEdgePath(const QString& edgeId);
    void updateSceneRect();
    void updateMinimap();
    void applyTheme();
    void applySearch();
    QHash<QString, QPointF> layoutPositions(
        const InsightGraphSnapshot& snapshot) const;
};

#endif // INSIGHTCANVAS_H
