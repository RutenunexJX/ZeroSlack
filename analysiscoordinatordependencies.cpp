#include "analysiscoordinator.h"

#include "analysisprogresscoordinator.h"
#include "analysisscheduler.h"
#include "documentsnapshot.h"
#include "navigationmanager.h"
#include "semanticruntimecoordinator.h"
#include "tabmanager.h"
#include "workspacemanager.h"

void AnalysisCoordinator::AnalysisDependencies::set(
    AnalysisScheduler* newScheduler,
    AnalysisProgressCoordinator* newProgressCoordinator,
    SemanticRuntimeCoordinator* newSemanticRuntime,
    TabManager* newTabManager,
    WorkspaceManager* newWorkspaceManager,
    NavigationManager* newNavigationManager)
{
    scheduler = newScheduler;
    progressCoordinator = newProgressCoordinator;
    semanticRuntime = newSemanticRuntime;
    tabManager = newTabManager;
    workspaceManager = newWorkspaceManager;
    navigationManager = newNavigationManager;
}

bool AnalysisCoordinator::AnalysisDependencies::hasScheduler() const
{
    return scheduler != nullptr;
}

bool AnalysisCoordinator::AnalysisDependencies::hasProgressCoordinator() const
{
    return progressCoordinator != nullptr;
}

bool AnalysisCoordinator::AnalysisDependencies::hasWorkspaceFileWatcher() const
{
    return workspaceManager && scheduler;
}

void AnalysisCoordinator::AnalysisDependencies::configureScheduler() const
{
    if (!scheduler)
        return;

    scheduler->setDocumentModel(tabManager ? tabManager->getDocumentModel() : nullptr);
    scheduler->setProjectModel(workspaceManager ? workspaceManager->getProjectModel() : nullptr);
    if (semanticRuntime)
        semanticRuntime->configureScheduler(scheduler);
}

void AnalysisCoordinator::AnalysisDependencies::connectProgressToScheduler() const
{
    if (progressCoordinator)
        progressCoordinator->connectToScheduler(scheduler);
}

bool AnalysisCoordinator::AnalysisDependencies::isWorkspaceOpen() const
{
    return workspaceManager && workspaceManager->isWorkspaceOpen();
}

bool AnalysisCoordinator::AnalysisDependencies::isWorkspaceSymbolAnalysisCancelled() const
{
    return progressCoordinator
        && progressCoordinator->isSymbolAnalysisCancelled();
}

void AnalysisCoordinator::AnalysisDependencies::refreshRelationshipDataView() const
{
    if (navigationManager) {
        navigationManager->refreshCurrentView();
        navigationManager->warmDesignHierarchyCache();
    }
}

void AnalysisCoordinator::AnalysisDependencies::handleFileSymbolAnalysisFinished(
    const QString& fileName,
    int symbolCount) const
{
    if (navigationManager)
        navigationManager->onSymbolAnalysisCompleted(fileName, symbolCount);
}

void AnalysisCoordinator::AnalysisDependencies::handleWorkspaceSymbolProgress(
    const QString& fileName,
    int filesDone,
    int totalFiles) const
{
    if (progressCoordinator) {
        progressCoordinator->handleWorkspaceSymbolProgress(
            filesDone,
            totalFiles,
            fileName);
    }
}

void AnalysisCoordinator::AnalysisDependencies::handleWorkspaceSymbolAnalysisFinished(
    int filesAnalyzed,
    int totalSymbols) const
{
    if (navigationManager) {
        navigationManager->onBatchSymbolAnalysisCompleted(
            filesAnalyzed,
            totalSymbols);
    }
}

void AnalysisCoordinator::AnalysisDependencies::handleExternalFileChanged(
    const QString& filePath,
    int debounceMs) const
{
    if (scheduler)
        scheduler->handleExternalFileChanged(filePath, debounceMs);
}

void AnalysisCoordinator::AnalysisDependencies::refreshSemanticPresentations(
    const QString& fileName) const
{
    if (tabManager)
        tabManager->refreshSemanticPresentations(fileName);
}

DocumentSnapshot AnalysisCoordinator::AnalysisDependencies::currentDocument() const
{
    return tabManager ? tabManager->getCurrentDocument() : DocumentSnapshot();
}

AnalysisScheduler*
AnalysisCoordinator::AnalysisDependencies::schedulerObject() const
{
    return scheduler;
}

AnalysisProgressCoordinator*
AnalysisCoordinator::AnalysisDependencies::progressCoordinatorObject() const
{
    return progressCoordinator;
}

WorkspaceManager*
AnalysisCoordinator::AnalysisDependencies::workspaceManagerObject() const
{
    return workspaceManager;
}
