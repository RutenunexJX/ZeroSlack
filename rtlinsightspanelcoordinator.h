#ifndef RTLINSIGHTSPANELCOORDINATOR_H
#define RTLINSIGHTSPANELCOORDINATOR_H

#include <QDockWidget>
#include <QGraphicsView>
#include <QRectF>
#include <QStackedWidget>
#include <QString>
#include <QStringList>
#include <QTreeWidget>

#include <functional>
#include <memory>

class InsightGraphView;
class SemanticIndexSnapshot;
class SignalUsageHotspotPanel;
class RtlInsightsGraphController;
class RtlInsightsPresenter;
struct RtlInsightsPanelViewState;
class QWidget;

class RtlInsightsPanelCoordinator
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
    void setStatusMessageHandler(
        std::function<void(
            const QString&,
            int)> handler);

    void updateModuleContext(
        const QString& fileName,
        const QString& moduleName,
        const QString& signalName = QString());
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

private:
    std::unique_ptr<RtlInsightsPanelViewState>
        viewState;
    std::unique_ptr<RtlInsightsGraphController>
        graphController;
    std::unique_ptr<RtlInsightsPresenter>
        presenter;
};

#endif // RTLINSIGHTSPANELCOORDINATOR_H
