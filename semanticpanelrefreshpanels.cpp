#include "semanticpanelrefreshcoordinator.h"

#include "problemspanelcoordinator.h"
#include "referencespanelcoordinator.h"
#include "relationshipspanelcoordinator.h"
#include "rtlinsightspanelcoordinator.h"
#include "signalkernelgraphpanelcoordinator.h"

void SemanticPanelRefreshCoordinator::PanelSet::set(
    ProblemsPanelCoordinator* newProblemsPanel,
    ReferencesPanelCoordinator* newReferencesPanel,
    RelationshipsPanelCoordinator* newRelationshipsPanel,
    RtlInsightsPanelCoordinator* newRtlInsightsPanel,
    SignalKernelGraphPanelCoordinator* newSignalKernelGraphPanel)
{
    problemsPanel = newProblemsPanel;
    referencesPanel = newReferencesPanel;
    relationshipsPanel = newRelationshipsPanel;
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

void SemanticPanelRefreshCoordinator::PanelSet::configureReferencesPanel(
    const WorkspaceFilesProvider& workspaceFilesProvider,
    const NavigationHandler& navigationHandler,
    const StatusMessageHandler& statusMessageHandler) const
{
    if (!referencesPanel)
        return;

    referencesPanel->setWorkspaceFilesProvider(workspaceFilesProvider);
    referencesPanel->setNavigationHandler(navigationHandler);
    referencesPanel->setStatusMessageHandler(statusMessageHandler);
}

void SemanticPanelRefreshCoordinator::PanelSet::configureRelationshipsPanel(
    const NavigationHandler& navigationHandler,
    const SignalGraphHandler& signalKernelGraphHandler,
    const SignalGraphHandler& stateTransitionGraphHandler,
    const ModuleGraphHandler& moduleBlockDiagramHandler,
    const StatusMessageHandler& statusMessageHandler) const
{
    if (!relationshipsPanel)
        return;

    relationshipsPanel->setNavigationHandler(navigationHandler);
    relationshipsPanel->setSignalKernelGraphHandler(signalKernelGraphHandler);
    relationshipsPanel->setStateTransitionGraphHandler(stateTransitionGraphHandler);
    relationshipsPanel->setModuleBlockDiagramHandler(moduleBlockDiagramHandler);
    relationshipsPanel->setStatusMessageHandler(statusMessageHandler);
}

void SemanticPanelRefreshCoordinator::PanelSet::configureRtlInsightsPanel(
    const NavigationHandler& navigationHandler,
    const StatusMessageHandler& statusMessageHandler) const
{
    if (!rtlInsightsPanel)
        return;

    rtlInsightsPanel->setNavigationHandler(navigationHandler);
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

void SemanticPanelRefreshCoordinator::PanelSet::updateProblemsPanel(
    const QString& fileName) const
{
    if (problemsPanel)
        problemsPanel->update(fileName);
}

void SemanticPanelRefreshCoordinator::PanelSet::showReferencesForSymbol(
    const QString& symbolName,
    const QString& fileName,
    const QString& moduleName) const
{
    if (referencesPanel)
        referencesPanel->showReferencesForSymbol(symbolName, fileName, moduleName);
}

void SemanticPanelRefreshCoordinator::PanelSet::refreshReferencesPanel() const
{
    if (referencesPanel)
        referencesPanel->refresh();
}

void SemanticPanelRefreshCoordinator::PanelSet::showRelationshipsForSymbol(
    const QString& symbolName,
    const QString& fileName,
    const QString& moduleName) const
{
    if (relationshipsPanel)
        relationshipsPanel->showRelationshipsForSymbol(symbolName, fileName, moduleName);
}

void SemanticPanelRefreshCoordinator::PanelSet::refreshRelationshipsPanel() const
{
    if (relationshipsPanel)
        relationshipsPanel->refresh();
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

void SemanticPanelRefreshCoordinator::PanelSet::updateRtlInsightsPanel(
    const QString& fileName,
    const QString& moduleName,
    const QString& signalName) const
{
    if (rtlInsightsPanel)
        rtlInsightsPanel->updateModuleContext(fileName, moduleName, signalName);
}

bool SemanticPanelRefreshCoordinator::PanelSet::problemsPanelShowsCurrentFile() const
{
    return problemsPanel && problemsPanel->showsCurrentFileScope();
}
