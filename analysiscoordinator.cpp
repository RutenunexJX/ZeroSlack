#include "analysiscoordinator.h"

#include "analysisprogresscoordinator.h"
#include "analysisscheduler.h"
#include "mycodeeditor.h"
#include "navigationmanager.h"
#include "semanticruntimecoordinator.h"
#include "tabmanager.h"
#include "workspacemanager.h"

#include <QFileInfo>
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

void AnalysisCoordinator::connectSchedulerSignals()
{
    if (!dependencies.hasScheduler())
        return;

    AnalysisScheduler* scheduler = dependencies.schedulerObject();
    connect(scheduler,
            &AnalysisScheduler::relationshipDataRefreshRequested,
            this, [this]() {
                dependencies.refreshRelationshipDataView();
            });
    connect(scheduler,
            &AnalysisScheduler::fileSymbolAnalysisFinished,
            this, [this](const QString& fileName, int symbolCount) {
                dependencies.handleFileSymbolAnalysisFinished(fileName,
                                                              symbolCount);
                refreshActiveEditorForFile(fileName);
            });
    connect(scheduler,
            &AnalysisScheduler::workspaceSymbolAnalysisProgress,
            this,
            [this](const QString& fileName, int filesDone, int totalFiles) {
                dependencies.handleWorkspaceSymbolProgress(fileName,
                                                           filesDone,
                                                           totalFiles);
            });
    connect(scheduler,
            &AnalysisScheduler::workspaceSymbolAnalysisFinished,
            this,
            [this](const ProjectSnapshot&, int filesAnalyzed, int totalSymbols) {
                dependencies.handleWorkspaceSymbolAnalysisFinished(
                    filesAnalyzed,
                    totalSymbols);
            });
    connect(scheduler,
            &AnalysisScheduler::relationshipAnalysisFinished,
            this, [this](const SingleFileRelationshipAnalysisResult& result) {
                showRelationshipAnalysisCompleted(result.fileName,
                                                  result.relationships.size());
            });
    connect(scheduler,
            &AnalysisScheduler::documentRefreshRequested,
            this, &AnalysisCoordinator::refreshActiveEditorForFile);
    connect(scheduler,
            &AnalysisScheduler::diagnosticsRefreshRequested,
            this, [this](const QString& fileName) {
                if (problemsRefreshHandler)
                    problemsRefreshHandler(fileName);
            });
}

void AnalysisCoordinator::connectProgressSignals()
{
    if (!dependencies.hasProgressCoordinator())
        return;

    dependencies.connectProgressToScheduler();
    AnalysisProgressCoordinator* progressCoordinator =
        dependencies.progressCoordinatorObject();
    connect(progressCoordinator,
            &AnalysisProgressCoordinator::statusMessageRequested,
            this,
            [this](const QString& message, int timeoutMs) {
                if (statusMessageHandler)
                    statusMessageHandler(message, timeoutMs);
            });
    connect(progressCoordinator,
            &AnalysisProgressCoordinator::relationshipAnalysisErrorReported,
            this,
            [this](const QString&, const QString& error) {
                showRelationshipAnalysisError(error);
            });
}

void AnalysisCoordinator::connectWorkspaceSignals()
{
    if (!dependencies.hasWorkspaceFileWatcher())
        return;

    connect(dependencies.workspaceManagerObject(),
            &WorkspaceManager::fileChanged,
            this, [this](const QString& filePath) {
                dependencies.handleExternalFileChanged(filePath,
                                                       fileChangeDebounceMs);
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

void AnalysisCoordinator::showRelationshipAnalysisCompleted(
    const QString& fileName,
    int relationshipsFound) const
{
    if (!statusMessageHandler)
        return;

    statusMessageHandler(
        QString("Smart analysis completed: %1 relationships in %2")
            .arg(relationshipsFound)
            .arg(QFileInfo(fileName).fileName()),
        2000);
}

void AnalysisCoordinator::showRelationshipAnalysisError(const QString& error) const
{
    if (statusMessageHandler)
        statusMessageHandler(QString("Analysis error: %1").arg(error), 3000);
}
