#ifndef RTLINSIGHTSPANELCOORDINATOR_H
#define RTLINSIGHTSPANELCOORDINATOR_H

#include <QDockWidget>
#include <QString>
#include <QStringList>
#include <QTreeWidget>

#include <functional>
#include <memory>

class SemanticIndexSnapshot;
class QGraphicsScene;
class QGraphicsView;
class QPushButton;
class QStackedWidget;
struct ModuleBlockDiagramReport;
struct StateTransitionGraphReport;
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
    bool graphNodeRectsOverlapForTest() const;
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
    QGraphicsScene* insightsGraphScene = nullptr;
    QGraphicsView* insightsGraphView = nullptr;
    QPushButton* moduleBriefButton = nullptr;
    QPushButton* signalJourneyButton = nullptr;
    QPushButton* clockResetButton = nullptr;
    QPushButton* fsmGraphButton = nullptr;
    QPushButton* moduleBlockDiagramButton = nullptr;
    QPushButton* graphZoomOutButton = nullptr;
    QPushButton* graphFitButton = nullptr;
    QPushButton* graphZoomInButton = nullptr;
    QString currentFileName;
    QString currentModuleName;
    QString currentSignalName;

    std::function<bool(const QString&, int, int)> navigationHandler;
    std::function<void(const QString&, int)> statusMessageHandler;

    void renderActionList();
    void renderNoContext();
    void showModuleBrief();
    void showSignalJourney();
    void showClockResetDomainMap();
    void showFsmGraph();
    void showModuleBlockDiagram();
    void showTreeSurface();
    void showGraphSurface();
    void renderStateTransitionGraphScene(
        const StateTransitionGraphReport& report);
    void renderFsmGraphScene(const FsmGraphReport& report,
                             const QString& title);
    void renderModuleBlockDiagramScene(
        const ModuleBlockDiagramReport& report);
    void renderGraphUnavailable(const QString& title,
                                const QString& message);
    void updateActionState();
    void logReportStart(const QString& reportName) const;
    void logReportDone(const QString& reportName, int durationMs) const;
    void logReportError(const QString& reportName, const QString& message) const;
};

#endif // RTLINSIGHTSPANELCOORDINATOR_H
