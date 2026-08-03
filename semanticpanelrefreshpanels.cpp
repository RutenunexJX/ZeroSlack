#include "semanticpanelrefreshcoordinator.h"

#include "problemspanelcoordinator.h"
#include "rtlinsightspanelcoordinator.h"
#include "signalkernelgraphpanelcoordinator.h"

void SemanticPanelRefreshCoordinator::PanelSet::set(
    ProblemsPanelCoordinator* newProblemsPanel,
    RtlInsightsPanelCoordinator* newRtlInsightsPanel,
    SignalKernelGraphPanelCoordinator* newSignalKernelGraphPanel)
{
    problemsPanel = newProblemsPanel;
    rtlInsightsPanel = newRtlInsightsPanel;
    signalKernelGraphPanel = newSignalKernelGraphPanel;
}

bool SemanticPanelRefreshCoordinator::PanelSet::isConfigured() const
{
    return configured;
}

void SemanticPanelRefreshCoordinator::PanelSet::markConfigured()
{
    configured = true;
}

void SemanticPanelRefreshCoordinator::PanelSet::configureProblemsPanel(
    const CurrentFileProvider& currentFileProvider,
    const WorkspaceFilesProvider& workspaceFilesProvider,
    const ProblemsNavigationHandler& navigationHandler,
    const StatusMessageHandler& statusMessageHandler) const
{
    if (!problemsPanel)
        return;

    problemsPanel->setCurrentFileProvider(currentFileProvider);
    problemsPanel->setWorkspaceFilesProvider(workspaceFilesProvider);
    problemsPanel->setNavigationHandler(navigationHandler);
    problemsPanel->setStatusMessageHandler(statusMessageHandler);
}

void SemanticPanelRefreshCoordinator::PanelSet::configureRtlInsightsPanel(
    const SourceNavigationHandler&
        sourceNavigationHandler,
    const StatusMessageHandler& statusMessageHandler) const
{
    if (!rtlInsightsPanel)
        return;

    rtlInsightsPanel->setSourceNavigationHandler(
        sourceNavigationHandler);
    rtlInsightsPanel->setStatusMessageHandler(statusMessageHandler);
}

void SemanticPanelRefreshCoordinator::PanelSet::configureSignalKernelGraphPanel(
    DocumentModel* documentModel,
    const NavigationHandler& navigationHandler,
    const NavigationHandler& revealHandler,
    const StatusMessageHandler& statusMessageHandler) const
{
    if (!signalKernelGraphPanel)
        return;

    signalKernelGraphPanel->setDocumentModel(documentModel);
    Q_UNUSED(revealHandler)
    signalKernelGraphPanel->setNavigationHandler(navigationHandler);
    signalKernelGraphPanel->setStatusMessageHandler(statusMessageHandler);
}

void SemanticPanelRefreshCoordinator::PanelSet::updateProblemsPanel() const
{
    if (problemsPanel)
        problemsPanel->update();
}

void SemanticPanelRefreshCoordinator::PanelSet::showSignalKernelGraphForSymbol(
    const QString& symbolName,
    const QString& fileName,
    const QString& moduleName,
    const QString& signalAccessPath) const
{
    if (signalKernelGraphPanel)
        signalKernelGraphPanel->showSignalKernelGraphForSymbol(symbolName,
                                                               fileName,
                                                               moduleName,
                                                               signalAccessPath);
}

void SemanticPanelRefreshCoordinator::PanelSet::showSignalUsageHotspotForSymbol(
    const QString& symbolName,
    const QString& fileName,
    const QString& moduleName,
    const QString& signalAccessPath) const
{
    if (rtlInsightsPanel)
        rtlInsightsPanel->showSignalUsageHotspotForSignal(fileName,
                                                          moduleName,
                                                          symbolName,
                                                          signalAccessPath);
}

void SemanticPanelRefreshCoordinator::PanelSet::showStateTransitionGraphForSymbol(
    const QString& symbolName,
    const QString& fileName,
    const QString& moduleName) const
{
    if (rtlInsightsPanel)
        rtlInsightsPanel->showStateTransitionGraphForSignal(fileName,
                                                            moduleName,
                                                            symbolName);
}

void SemanticPanelRefreshCoordinator::PanelSet::showModuleBlockDiagramForSymbol(
    const QString& symbolName,
    const QString& fileName,
    const QString& moduleName) const
{
    Q_UNUSED(moduleName)
    if (rtlInsightsPanel)
        rtlInsightsPanel->showModuleBlockDiagramForModule(fileName,
                                                          symbolName);
}

bool SemanticPanelRefreshCoordinator::PanelSet::
    syncRtlInsightsSourceLocation(
        const RtlInsightSourceLocation& location) const
{
    return rtlInsightsPanel
        && rtlInsightsPanel->syncSourceLocation(
            location);
}

bool SemanticPanelRefreshCoordinator::PanelSet::problemsPanelShowsCurrentFile() const
{
    return problemsPanel && problemsPanel->showsCurrentFileScope();
}
