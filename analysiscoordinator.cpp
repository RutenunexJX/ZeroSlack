#include "analysiscoordinator.h"

#include "analysisprogresscoordinator.h"
#include "analysisscheduler.h"
#include "mycodeeditor.h"
#include "navigationmanager.h"
#include "semanticruntimecoordinator.h"
#include "tabmanager.h"
#include "workspacemanager.h"

#include <QTimer>

#include <utility>

AnalysisCoordinator::AnalysisCoordinator(
    AnalysisScheduler* scheduler,
    AnalysisProgressCoordinator* progressCoordinator,
    SemanticRuntimeCoordinator* semanticRuntime,
    TabManager* tabManager,
    WorkspaceManager* workspaceManager,
    NavigationManager* navigationManager,
    QObject* parent)
    : QObject(parent)
{
    dependencies.set(scheduler,
                     progressCoordinator,
                     semanticRuntime,
                     tabManager,
                     workspaceManager,
                     navigationManager);
}

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
    if (navigationManager)
        navigationManager->refreshCurrentView();
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

MyCodeEditor* AnalysisCoordinator::AnalysisDependencies::currentEditor() const
{
    return tabManager ? tabManager->getCurrentEditor() : nullptr;
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

void AnalysisCoordinator::setFileChangeDebounceMs(int debounceMs)
{
    fileChangeDebounceMs = debounceMs;
}

void AnalysisCoordinator::setStatusMessageHandler(
    std::function<void(const QString&, int)> handler)
{
    statusMessageHandler = std::move(handler);
}

void AnalysisCoordinator::setProblemsRefreshHandler(
    std::function<void(const QString&)> handler)
{
    problemsRefreshHandler = std::move(handler);
}

void AnalysisCoordinator::connectSignals()
{
    if (signalsConnected)
        return;

    configureScheduler();
    connectSchedulerSignals();
    connectProgressSignals();
    connectWorkspaceSignals();

    signalsConnected = true;
}

void AnalysisCoordinator::configureScheduler()
{
    if (!dependencies.hasScheduler())
        return;

    dependencies.configureScheduler();
    AnalysisScheduler* scheduler = dependencies.schedulerObject();
    scheduler->setWorkspaceOpenProvider([this]() {
        return dependencies.isWorkspaceOpen();
    });
    scheduler->setWorkspaceSymbolCancelProvider([this]() {
        return dependencies.isWorkspaceSymbolAnalysisCancelled();
    });
}

void AnalysisCoordinator::refreshActiveEditorForFile(const QString& fileName) const
{
    MyCodeEditor* editor = dependencies.currentEditor();
    const DocumentSnapshot document = dependencies.currentDocument();
    if (!editor || document.fileName != fileName)
        return;

    editor->refreshScopeAndCurrentLineHighlight();
    QTimer::singleShot(0, editor, [editor]() {
        editor->refreshScopeAndCurrentLineHighlight();
    });
}
