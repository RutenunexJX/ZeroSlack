#ifndef RTLINSIGHTSGRAPHSCENEMAPPER_H
#define RTLINSIGHTSGRAPHSCENEMAPPER_H

#include "rtlinsightlink.h"

#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>

struct FsmGraph;
struct FsmGraphReport;
struct ModuleBlockDiagramReport;
struct ModuleBlockDiagramNode;
struct StateTransitionGraphReport;
struct RtlInsightsPanelViewState;
class QGraphicsItem;
class RtlInsightsGraphController;

class RtlInsightsGraphSceneMapper
{
public:
    RtlInsightsGraphSceneMapper(
        RtlInsightsPanelViewState& state,
        RtlInsightsGraphController& controller);

    int nodeItemCountForTest() const;
    int edgeItemCountForTest() const;
    QStringList textItemsForTest() const;
    QStringList elementSummariesForTest() const;
    QStringList elementVisualSummariesForTest() const;
    QStringList hoveredElementSummariesForTest() const;
    QString itemToolTipForTest(
        const QString& elementKind,
        const QString& primaryText,
        const QString& secondaryText = QString()) const;
    QRectF lastFitRectForTest() const;
    int selectedItemCountForTest() const;
    QStringList selectedElementSummariesForTest() const;
    QStringList inspectorRowsForTest() const;
    QStringList tableRowsForTest() const;
    bool itemsReadableForTest() const;
    bool nestedNodeStackingReadableForTest() const;
    bool setItemHoveredForTest(
        const QString& elementKind,
        const QString& primaryText,
        const QString& secondaryText,
        bool hovered);
    bool selectItemForTest(
        const QString& elementKind,
        const QString& primaryText,
        const QString& secondaryText = QString());
    bool selectTableRowForTest(
        const QString& primaryText,
        const QString& secondaryText = QString());
    bool triggerNavigationForTest(
        const QString& elementKind,
        const QString& primaryText,
        const QString& secondaryText = QString());

    QGraphicsItem* itemAtScenePoint(
        const QPointF& scenePoint) const;
    void applySearchHighlight();
    void clearDetails();
    void renderGenericInspector(
        const QString& title,
        const QStringList& rows);
    void renderModuleBlockInspector(
        const ModuleBlockDiagramReport& report,
        const ModuleBlockDiagramNode& node);
    void populateModuleBlockInstancesTable(
        const ModuleBlockDiagramReport& report);
    void populateFsmTransitionsTable(
        const FsmGraph& graph);
    void selectModuleBlockNode(
        int nodeId,
        bool centerGraph = true,
        bool syncTable = true);
    bool selectItemForInspector(QGraphicsItem* item);
    bool selectItemAtScenePoint(
        const QPointF& scenePoint);
    bool navigateItem(QGraphicsItem* item);
    bool navigateSelectedItem();
    bool setModuleBlockTopFromSelected();
    bool selectSourceLocation(
        const RtlInsightSourceLocation& location,
        bool centerGraph = true);

    void renderStateTransitionGraphScene(
        const StateTransitionGraphReport& report);
    void renderFsmGraphScene(
        const FsmGraphReport& report,
        const QString& title);
    void renderFsmGraphLayoutScene(
        const FsmGraph& graph,
        const QString& title,
        const QString& mode);
    void renderModuleBlockDiagramScene(
        const ModuleBlockDiagramReport& report);

private:
    RtlInsightsPanelViewState& state;
    RtlInsightsGraphController& controller;
};

#endif // RTLINSIGHTSGRAPHSCENEMAPPER_H
