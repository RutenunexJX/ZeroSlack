#ifndef RTLINSIGHTSPANELCOORDINATOR_H
#define RTLINSIGHTSPANELCOORDINATOR_H

#include <QDockWidget>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QTreeWidget>

#include "insightgraphview.h"
#include "moduleblockdiagramservice.h"

#include <functional>
#include <memory>

class SemanticIndexSnapshot;
class QCheckBox;
class QComboBox;
class QGraphicsItem;
class QGraphicsScene;
class QGraphicsView;
class QLineEdit;
class QPushButton;
class SignalUsageHotspotPanel;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class QToolButton;
class QWidget;
struct ModuleBlockDiagramReport;
struct StateTransitionGraphReport;
struct FsmGraph;
struct FsmGraphReport;

class RtlInsightsPanelCoordinator
{
public:
    explicit RtlInsightsPanelCoordinator(QWidget* parent);

    void setNavigationHandler(std::function<bool(const QString&, int, int)> handler);
    void setStatusMessageHandler(std::function<void(const QString&, int)> handler);

    void updateModuleContext(const QString& fileName,
                             const QString& moduleName,
                             const QString& signalName = QString());
    void showModuleInsights(const QString& fileName,
                            const QString& moduleName,
                            const QString& signalName = QString());
    void showStateTransitionGraphForSignal(const QString& fileName,
                                           const QString& moduleName,
                                           const QString& signalName);
    void showSignalUsageHotspotForSignal(const QString& fileName,
                                         const QString& moduleName,
                                         const QString& signalName,
                                         const QString& signalAccessPath = {});
    void showModuleBlockDiagramForModule(const QString& fileName,
                                         const QString& moduleName);
    void showSemanticDiff(std::shared_ptr<const SemanticIndexSnapshot> beforeSnapshot,
                          std::shared_ptr<const SemanticIndexSnapshot> afterSnapshot,
                          const QString& moduleName = QString(),
                          const QString& beforeFileName = QString(),
                          const QString& afterFileName = QString());
    void refresh();

    QDockWidget* dock() const { return insightsDock; }
    QTreeWidget* tree() const { return insightsTree; }
    QGraphicsView* graphView() const { return insightsGraphView; }
    int graphNodeItemCountForTest() const;
    int graphEdgeItemCountForTest() const;
    QStringList graphTextItemsForTest() const;
    QStringList graphElementSummariesForTest() const;
    QStringList graphElementVisualSummariesForTest() const;
    QStringList graphEdgeGeometrySummariesForTest() const;
    QRectF graphLastFitRectForTest() const;
    qreal graphCurrentZoomForTest() const;
    bool graphNodeRectsOverlapForTest() const;
    int graphSelectedItemCountForTest() const;
    QStringList graphInspectorRowsForTest() const;
    QStringList graphTableRowsForTest() const;
    bool graphItemsReadableForTest() const;
    bool graphNestedNodeStackingReadableForTest() const;
    bool setGraphItemHoveredForTest(const QString& elementKind,
                                    const QString& primaryText,
                                    const QString& secondaryText,
                                    bool hovered);
    bool selectGraphItemForTest(const QString& elementKind,
                                const QString& primaryText,
                                const QString& secondaryText = QString());
    bool selectGraphItemAtScenePointForTest(qreal sceneX, qreal sceneY);
    QString graphItemAtScenePointSummaryForTest(qreal sceneX,
                                                qreal sceneY) const;
    bool selectGraphTableRowForTest(const QString& primaryText,
                                    const QString& secondaryText = QString());
    int graphElementLineForTest(const QString& elementKind,
                                const QString& primaryText,
                                const QString& secondaryText = QString()) const;
    bool triggerGraphNavigationForTest(const QString& elementKind,
                                       const QString& primaryText,
                                       const QString& secondaryText = QString());

private:
    QDockWidget* insightsDock = nullptr;
    QStackedWidget* insightsStack = nullptr;
    QTreeWidget* insightsTree = nullptr;
    QWidget* insightsGraphPanel = nullptr;
    QGraphicsScene* insightsGraphScene = nullptr;
    InsightGraphView* insightsGraphView = nullptr;
    QTreeWidget* graphInspector = nullptr;
    QTableWidget* graphTable = nullptr;
    SignalUsageHotspotPanel* signalUsageHotspotPanel = nullptr;
    QPushButton* moduleBriefButton = nullptr;
    QPushButton* signalJourneyButton = nullptr;
    QPushButton* signalUsageHotspotButton = nullptr;
    QPushButton* clockResetButton = nullptr;
    QPushButton* fsmGraphButton = nullptr;
    QPushButton* moduleBlockDiagramButton = nullptr;
    QPushButton* graphZoomOutButton = nullptr;
    QPushButton* graphFitButton = nullptr;
    QPushButton* graphZoomInButton = nullptr;
    QLineEdit* graphSearchEdit = nullptr;
    QComboBox* moduleBlockTopCombo = nullptr;
    QPushButton* moduleBlockSetSelectionButton = nullptr;
    QSpinBox* moduleBlockDepthSpin = nullptr;
    QCheckBox* moduleBlockCollapsePackagesCheck = nullptr;
    QCheckBox* moduleBlockShowUnresolvedCheck = nullptr;
    QComboBox* stateTransitionSignalCombo = nullptr;
    QComboBox* stateTransitionCurrentCombo = nullptr;
    QComboBox* stateTransitionNextCombo = nullptr;
    QCheckBox* stateTransitionResetCheck = nullptr;
    QCheckBox* stateTransitionErrorCheck = nullptr;
    QCheckBox* stateTransitionUnreachableCheck = nullptr;
    QComboBox* graphLayoutCombo = nullptr;
    QToolButton* graphMoreButton = nullptr;
    QPushButton* graphInspectorJumpButton = nullptr;
    QPushButton* graphInspectorFocusButton = nullptr;
    QPushButton* graphInspectorSetTopButton = nullptr;
    QPushButton* graphInspectorRevealButton = nullptr;
    QString currentFileName;
    QString currentModuleName;
    QString currentSignalName;
    QString graphSearchText;
    QString currentGraphMode;
    QRectF lastGraphFitRect;
    ModuleBlockDiagramReport currentModuleBlockReport;
    int currentModuleBlockSelectedNodeId = -1;

    std::function<bool(const QString&, int, int)> navigationHandler;
    std::function<void(const QString&, int)> statusMessageHandler;

    void renderActionList();
    void renderNoContext();
    void showModuleBrief();
    void showSignalJourney();
    void showSignalUsageHotspot();
    void showClockResetDomainMap();
    void showFsmGraph();
    void showModuleBlockDiagram();
    void showTreeSurface();
    void showGraphSurface();
    void showHotspotSurface();
    void renderStateTransitionGraphScene(
        const StateTransitionGraphReport& report);
    void renderFsmGraphScene(const FsmGraphReport& report,
                             const QString& title);
    void renderModuleBlockDiagramScene(
        const ModuleBlockDiagramReport& report);
    void renderGraphUnavailable(const QString& title,
                                const QString& message);
    void configureGraphToolbarForMode(const QString& mode);
    void clearGraphDetails();
    void renderGenericGraphInspector(const QString& title,
                                     const QStringList& rows);
    bool selectGraphItemForInspector(QGraphicsItem* item);
    bool selectGraphItemAtScenePoint(const QPointF& scenePoint);
    void renderModuleBlockInspector(const ModuleBlockDiagramReport& report,
                                    const ModuleBlockDiagramNode& node);
    void populateModuleBlockInstancesTable(
        const ModuleBlockDiagramReport& report);
    void populateFsmTransitionsTable(const FsmGraph& graph);
    void selectModuleBlockNode(int nodeId,
                               bool centerGraph = true,
                               bool syncTable = true);
    bool navigateGraphItem(QGraphicsItem* item);
    bool navigateSelectedGraphItem();
    bool setModuleBlockTopFromSelected();
    void applyGraphSearchHighlight();
    void updateActionState();
    void logReportStart(const QString& reportName) const;
    void logReportDone(const QString& reportName, int durationMs) const;
    void logReportError(const QString& reportName, const QString& message) const;
};

#endif // RTLINSIGHTSPANELCOORDINATOR_H
