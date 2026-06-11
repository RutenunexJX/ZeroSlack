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
    , scheduler(scheduler)
    , progressCoordinator(progressCoordinator)
    , semanticRuntime(semanticRuntime)
    , tabManager(tabManager)
    , workspaceManager(workspaceManager)
    , navigationManager(navigationManager)
{
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
    if (!scheduler)
        return;

    scheduler->setDocumentModel(tabManager ? tabManager->getDocumentModel() : nullptr);
    scheduler->setProjectModel(workspaceManager ? workspaceManager->getProjectModel() : nullptr);
    scheduler->setSymbolAnalyzer(semanticRuntime ? semanticRuntime->symbolAnalyzer() : nullptr);
    scheduler->setWorkspaceOpenProvider([this]() {
        return workspaceManager && workspaceManager->isWorkspaceOpen();
    });
    scheduler->setWorkspaceSymbolCancelProvider([this]() {
        return progressCoordinator && progressCoordinator->isSymbolAnalysisCancelled();
    });
    scheduler->setRelationshipEngine(semanticRuntime ? semanticRuntime->relationshipEngine() : nullptr);
    scheduler->setRelationshipBuilder(semanticRuntime ? semanticRuntime->relationshipBuilder() : nullptr);
}

void AnalysisCoordinator::connectSchedulerSignals()
{
    if (!scheduler)
        return;

    connect(scheduler, &AnalysisScheduler::relationshipDataRefreshRequested,
            this, [this]() {
                if (navigationManager)
                    navigationManager->refreshCurrentView();
            });
    connect(scheduler, &AnalysisScheduler::fileSymbolAnalysisFinished,
            this, [this](const QString& fileName, int symbolCount) {
                if (navigationManager)
                    navigationManager->onSymbolAnalysisCompleted(fileName,
                                                                 symbolCount);
                refreshActiveEditorForFile(fileName);
            });
    connect(scheduler,
            &AnalysisScheduler::workspaceSymbolAnalysisProgress,
            this,
            [this](const QString& fileName, int filesDone, int totalFiles) {
                if (progressCoordinator) {
                    progressCoordinator->handleWorkspaceSymbolProgress(
                        filesDone,
                        totalFiles,
                        fileName);
                }
            });
    connect(scheduler,
            &AnalysisScheduler::workspaceSymbolAnalysisFinished,
            this,
            [this](const ProjectSnapshot&, int filesAnalyzed, int totalSymbols) {
                if (navigationManager) {
                    navigationManager->onBatchSymbolAnalysisCompleted(
                        filesAnalyzed,
                        totalSymbols);
                }
            });
    connect(scheduler, &AnalysisScheduler::relationshipAnalysisFinished,
            this, [this](const SingleFileRelationshipAnalysisResult& result) {
                showRelationshipAnalysisCompleted(result.fileName,
                                                  result.relationships.size());
            });
    connect(scheduler, &AnalysisScheduler::documentRefreshRequested,
            this, &AnalysisCoordinator::refreshActiveEditorForFile);
    connect(scheduler, &AnalysisScheduler::diagnosticsRefreshRequested,
            this, [this](const QString& fileName) {
                if (problemsRefreshHandler)
                    problemsRefreshHandler(fileName);
            });
}

void AnalysisCoordinator::connectProgressSignals()
{
    if (!progressCoordinator)
        return;

    progressCoordinator->connectToScheduler(scheduler);
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
    if (!workspaceManager || !scheduler)
        return;

    connect(workspaceManager, &WorkspaceManager::fileChanged,
            this, [this](const QString& filePath) {
                if (scheduler)
                    scheduler->handleExternalFileChanged(filePath, fileChangeDebounceMs);
            });
}

void AnalysisCoordinator::refreshActiveEditorForFile(const QString& fileName) const
{
    MyCodeEditor* editor = tabManager ? tabManager->getCurrentEditor() : nullptr;
    if (!editor || editor->getFileName() != fileName)
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
