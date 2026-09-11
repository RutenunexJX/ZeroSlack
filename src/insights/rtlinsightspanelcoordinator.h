#ifndef RTLINSIGHTSPANELCOORDINATOR_H
#define RTLINSIGHTSPANELCOORDINATOR_H

#include "actionregistry.h"
#include "graphexportservice.h"
#include "rtlinsightlink.h"
#include "zeroslackexport.h"

#include <QDockWidget>
#include <QGraphicsView>
#include <QMetaObject>
#include <QRectF>
#include <QStackedWidget>
#include <QString>
#include <QStringList>
#include <QTreeWidget>

#include <functional>
#include <memory>

class InsightGraphView;
class QAction;
class SemanticIndexSnapshot;
class SignalUsageHotspotPanel;
class RtlInsightsGraphController;
class RtlInsightsPresenter;
struct RtlInsightsPanelViewState;
class QToolButton;
class QWidget;

class ZEROSLACK_API RtlInsightsPanelCoordinator : public ActionExecutionHost
{
public:
    explicit RtlInsightsPanelCoordinator(
        QWidget* parent);
    ~RtlInsightsPanelCoordinator();

    void setNavigationHandler(
        std::function<bool(
            const QString&,
            int,
            int)> handler);
    void setSourceNavigationHandler(
        std::function<bool(
            const RtlInsightSourceLocation&)> handler);
    void setStatusMessageHandler(
        std::function<void(
            const QString&,
            int)> handler);
    void setRegisteredActionRequestHandler(
        RegisteredActionRequestHandler handler);

    void updateModuleContext(
        const QString& fileName,
        const QString& moduleName,
        const QString& signalName = QString());
    bool syncSourceLocation(
        const RtlInsightSourceLocation& location);
    void setPinned(bool pinned);
    bool isPinned() const;
    void setStateViewEnabled(bool enabled);
    bool stateViewEnabledForTest() const;
    void showModuleInsights(
        const QString& fileName,
        const QString& moduleName,
        const QString& signalName = QString());
    void showStateTransitionGraphForSignal(
        const QString& fileName,
        const QString& moduleName,
        const QString& signalName);
    void showSignalUsageHotspotForSignal(
        const QString& fileName,
        const QString& moduleName,
        const QString& signalName,
        const QString& signalAccessPath = {});
    void showModuleBlockDiagramForModule(
        const QString& fileName,
        const QString& moduleName);
    void showSemanticDiff(
        std::shared_ptr<const SemanticIndexSnapshot>
            beforeSnapshot,
        std::shared_ptr<const SemanticIndexSnapshot>
            afterSnapshot,
        const QString& moduleName = QString(),
        const QString& beforeFileName = QString(),
        const QString& afterFileName = QString());
    void refresh();
    void refreshThemePresentation();
    void showModuleBrief();
    void showSignalJourney();
    void showSignalUsageHotspot();
    void showClockResetDomainMap();
    void showFsmGraph();
    void showModuleBlockDiagram();

    void focusFit();
    void focusZoomIn();
    void focusZoomOut();
    void setFocusSearchText(const QString& text);
    QString focusSearchText() const;
    void focusInspector();

    GraphExportResult exportCurrentGraph(
        const QString& outputPath,
        const GraphExportOptions& options = {}) const;
    QAction* graphExportAction() const;
    QAction* graphActionForTest(
        const QString& actionId) const;
    ActionExecutionResult triggerGraphActionForTest(
        const QString& actionId);

    QDockWidget* dock() const;
    QTreeWidget* tree() const;
    QGraphicsView* graphView() const;
    QStackedWidget* stackForTest() const;
    SignalUsageHotspotPanel*
        signalUsageHotspotPanelForTest() const;

    int graphNodeItemCountForTest() const;
    int graphEdgeItemCountForTest() const;
    QStringList graphTextItemsForTest() const;
    QStringList graphElementSummariesForTest() const;
    QStringList graphElementVisualSummariesForTest() const;
    QStringList graphHoveredElementSummariesForTest() const;
    QString graphItemToolTipForTest(
        const QString& elementKind,
        const QString& primaryText,
        const QString& secondaryText = QString()) const;
    QRectF graphLastFitRectForTest() const;
    int graphSelectedItemCountForTest() const;
    QStringList graphSelectedElementSummariesForTest() const;
    QStringList graphInspectorRowsForTest() const;
    QStringList graphTableRowsForTest() const;
    bool graphItemsReadableForTest() const;
    bool graphNestedNodeStackingReadableForTest() const;
    bool setGraphItemHoveredForTest(
        const QString& elementKind,
        const QString& primaryText,
        const QString& secondaryText,
        bool hovered);
    bool selectGraphItemForTest(
        const QString& elementKind,
        const QString& primaryText,
        const QString& secondaryText = QString());
    bool selectGraphTableRowForTest(
        const QString& primaryText,
        const QString& secondaryText = QString());
    bool triggerGraphNavigationForTest(
        const QString& elementKind,
        const QString& primaryText,
        const QString& secondaryText = QString());
    quint64 graphBuildGenerationForTest() const;
    quint64 graphBuildRequestCountForTest() const;
    QString graphModeForTest() const;
    QString currentFileNameForTest() const;
    QString currentModuleNameForTest() const;
    QString currentSignalNameForTest() const;
    QToolButton* pinButtonForTest() const;

private:
    QAction* createGraphAction(
        QWidget* owner,
        const QString& actionId);
    QAction* createSelectedSourceAction(
        QWidget* owner,
        const QString& actionId);
    void bindGraphActionButton(
        class QPushButton* button,
        QAction* action);
    QAction* graphAction(
        const QString& actionId) const;
    void refreshGraphActionAvailability() const;
    ActionExecutionResult requestGraphAction(
        const QString& actionId);
    ActionExecutionResult executeActionRoute(
        const ActionDescriptor& descriptor,
        const ActionInvocation& invocation) override;
    bool hasExportableGraph() const;
    void refreshGraphExportActionAvailability() const;

    std::unique_ptr<RtlInsightsPanelViewState>
        viewState;
    std::unique_ptr<RtlInsightsGraphController>
        graphController;
    std::unique_ptr<RtlInsightsPresenter>
        presenter;
    RegisteredActionRequestHandler
        registeredActionRequestHandler;
    QMetaObject::Connection themeAboutToChangeConnection;
};

#endif // RTLINSIGHTSPANELCOORDINATOR_H
