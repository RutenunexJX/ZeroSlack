#ifndef RTLINSIGHTSGRAPHCONTROLLER_H
#define RTLINSIGHTSGRAPHCONTROLLER_H

#include "moduleblockdiagramservice.h"
#include "rtlinsightlink.h"

#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>

#include <memory>

struct FsmGraph;
struct FsmGraphReport;
struct StateTransitionGraphReport;
struct RtlInsightsPanelViewState;
class QGraphicsItem;
class RtlInsightsGraphSceneMapper;
struct RtlInsightsThemeViewportState;

class RtlInsightsGraphController
{
public:
    explicit RtlInsightsGraphController(
        RtlInsightsPanelViewState& state);
    ~RtlInsightsGraphController();

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

    void configureToolbarForMode(const QString& mode);
    void clearDetails();
    void renderGenericInspector(
        const QString& title,
        const QStringList& rows);
    void populateFsmTransitionsTable(
        const FsmGraph& graph);
    bool selectItemForInspector(QGraphicsItem* item);
    bool selectItemAtScenePoint(
        const QPointF& scenePoint);
    bool navigateItem(QGraphicsItem* item);
    bool navigateSelectedItem();
    RtlInsightSourceLocation selectedSourceLocation() const;
    bool enterModuleBlockNode(int nodeId);
    bool navigateModuleBlockBreadcrumb(int index);
    bool navigateModuleBlockHistory(int direction);
    void refreshModuleBlockSelectionActions();
    bool selectSourceLocation(
        const RtlInsightSourceLocation& location,
        bool centerGraph = true);
    void showTreeSurface();
    void showGraphSurface();
    void showHotspotSurface();
    void applySearchHighlight();
    void renderUnavailable(const QString& title,
                           const QString& message);

    void renderStateTransitionGraphScene(
        const StateTransitionGraphReport& report);
    void renderFsmGraphScene(
        const FsmGraphReport& report,
        const QString& title);
    void renderFsmGraphLayoutScene(
        const FsmGraph& graph,
        const QString& title,
        const QString& mode);
    void mapModuleBlockDiagram(
        const ModuleBlockDiagramReport& report);

    void focusFit();
    void focusZoomIn();
    void focusZoomOut();
    void setFocusSearchText(const QString& text);
    QString focusSearchText() const;
    void focusInspector();
    void captureThemeViewportState();
    void refreshThemePresentation();
    quint64 graphBuildGeneration() const;

private:
    RtlInsightsPanelViewState& state;
    std::unique_ptr<RtlInsightsGraphSceneMapper>
        sceneMapper;
    std::unique_ptr<RtlInsightsThemeViewportState>
        themeViewportState;
};

#endif // RTLINSIGHTSGRAPHCONTROLLER_H
