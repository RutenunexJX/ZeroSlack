#include "rtlinsightspanelcoordinator.h"

#include "rtlinsightsgraphcontroller.h"
#include "rtlinsightspanelviewstate.h"

QAction* RtlInsightsPanelCoordinator::graphActionForTest(
    const QString& actionId) const
{
    refreshGraphActionAvailability();
    return graphAction(actionId);
}

ActionExecutionResult
RtlInsightsPanelCoordinator::triggerGraphActionForTest(
    const QString& actionId)
{
    return requestGraphAction(actionId);
}

QStackedWidget*
RtlInsightsPanelCoordinator::stackForTest() const
{
    return viewState->insightsStack;
}

SignalUsageHotspotPanel*
RtlInsightsPanelCoordinator::
    signalUsageHotspotPanelForTest() const
{
    return viewState->signalUsageHotspotPanel;
}

int RtlInsightsPanelCoordinator::
    graphNodeItemCountForTest() const
{
    return graphController->nodeItemCountForTest();
}

int RtlInsightsPanelCoordinator::
    graphEdgeItemCountForTest() const
{
    return graphController->edgeItemCountForTest();
}

QStringList RtlInsightsPanelCoordinator::
    graphTextItemsForTest() const
{
    return graphController->textItemsForTest();
}

QStringList RtlInsightsPanelCoordinator::
    graphElementSummariesForTest() const
{
    return graphController
        ->elementSummariesForTest();
}

QStringList RtlInsightsPanelCoordinator::
    graphElementVisualSummariesForTest() const
{
    return graphController
        ->elementVisualSummariesForTest();
}

QStringList RtlInsightsPanelCoordinator::
    graphHoveredElementSummariesForTest() const
{
    return graphController
        ->hoveredElementSummariesForTest();
}

QString RtlInsightsPanelCoordinator::
    graphItemToolTipForTest(
        const QString& elementKind,
        const QString& primaryText,
        const QString& secondaryText) const
{
    return graphController->itemToolTipForTest(
        elementKind,
        primaryText,
        secondaryText);
}

QRectF RtlInsightsPanelCoordinator::
    graphLastFitRectForTest() const
{
    return graphController->lastFitRectForTest();
}

int RtlInsightsPanelCoordinator::
    graphSelectedItemCountForTest() const
{
    return graphController
        ->selectedItemCountForTest();
}

QStringList RtlInsightsPanelCoordinator::
    graphSelectedElementSummariesForTest() const
{
    return graphController
        ->selectedElementSummariesForTest();
}

QStringList RtlInsightsPanelCoordinator::
    graphInspectorRowsForTest() const
{
    return graphController->inspectorRowsForTest();
}

QStringList RtlInsightsPanelCoordinator::
    graphTableRowsForTest() const
{
    return graphController->tableRowsForTest();
}

bool RtlInsightsPanelCoordinator::
    graphItemsReadableForTest() const
{
    return graphController->itemsReadableForTest();
}

bool RtlInsightsPanelCoordinator::
    graphNestedNodeStackingReadableForTest() const
{
    return graphController
        ->nestedNodeStackingReadableForTest();
}

bool RtlInsightsPanelCoordinator::
    setGraphItemHoveredForTest(
        const QString& elementKind,
        const QString& primaryText,
        const QString& secondaryText,
        bool hovered)
{
    return graphController->setItemHoveredForTest(
        elementKind,
        primaryText,
        secondaryText,
        hovered);
}

bool RtlInsightsPanelCoordinator::
    selectGraphItemForTest(
        const QString& elementKind,
        const QString& primaryText,
        const QString& secondaryText)
{
    return graphController->selectItemForTest(
        elementKind,
        primaryText,
        secondaryText);
}

bool RtlInsightsPanelCoordinator::
    selectGraphTableRowForTest(
        const QString& primaryText,
        const QString& secondaryText)
{
    return graphController->selectTableRowForTest(
        primaryText,
        secondaryText);
}

bool RtlInsightsPanelCoordinator::
    triggerGraphNavigationForTest(
        const QString& elementKind,
        const QString& primaryText,
        const QString& secondaryText)
{
    return graphController->triggerNavigationForTest(
        elementKind,
        primaryText,
        secondaryText);
}

quint64 RtlInsightsPanelCoordinator::
    graphBuildGenerationForTest() const
{
    return graphController->graphBuildGeneration();
}

QString RtlInsightsPanelCoordinator::
    graphModeForTest() const
{
    return viewState->currentGraphMode;
}

QString RtlInsightsPanelCoordinator::
    currentFileNameForTest() const
{
    return viewState->currentFileName;
}

QString RtlInsightsPanelCoordinator::
    currentModuleNameForTest() const
{
    return viewState->currentModuleName;
}

QString RtlInsightsPanelCoordinator::
    currentSignalNameForTest() const
{
    return viewState->currentSignalName;
}

QToolButton* RtlInsightsPanelCoordinator::
    pinButtonForTest() const
{
    return viewState->pinButton;
}
