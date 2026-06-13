#include "semanticpanelrefreshcoordinator.h"

#include "problemspanelcoordinator.h"
#include "referencespanelcoordinator.h"
#include "relationshipspanelcoordinator.h"

void SemanticPanelRefreshCoordinator::PanelSet::set(
    ProblemsPanelCoordinator* newProblemsPanel,
    ReferencesPanelCoordinator* newReferencesPanel,
    RelationshipsPanelCoordinator* newRelationshipsPanel)
{
    problemsPanel = newProblemsPanel;
    referencesPanel = newReferencesPanel;
    relationshipsPanel = newRelationshipsPanel;
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
    const NavigationHandler& navigationHandler) const
{
    if (!problemsPanel)
        return;

    problemsPanel->setCurrentFileProvider(currentFileProvider);
    problemsPanel->setWorkspaceFilesProvider(workspaceFilesProvider);
    problemsPanel->setNavigationHandler(navigationHandler);
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
    const StatusMessageHandler& statusMessageHandler) const
{
    if (!relationshipsPanel)
        return;

    relationshipsPanel->setNavigationHandler(navigationHandler);
    relationshipsPanel->setStatusMessageHandler(statusMessageHandler);
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

bool SemanticPanelRefreshCoordinator::PanelSet::problemsPanelShowsCurrentFile() const
{
    return problemsPanel && problemsPanel->showsCurrentFileScope();
}
