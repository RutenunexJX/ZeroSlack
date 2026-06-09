#include "analysiscoordinator.h"

#include "analysisprogresscoordinator.h"
#include "analysisscheduler.h"
#include "completionmanager.h"
#include "mycodeeditor.h"
#include "navigationmanager.h"
#include "symbolanalyzer.h"
#include "tabmanager.h"
#include "workspacemanager.h"

#include <QFileInfo>
#include <QTimer>

#include <utility>

AnalysisCoordinator::AnalysisCoordinator(
    AnalysisScheduler* scheduler,
    AnalysisProgressCoordinator* progressCoordinator,
    SymbolAnalyzer* symbolAnalyzer,
    TabManager* tabManager,
    WorkspaceManager* workspaceManager,
    NavigationManager* navigationManager,
    SymbolRelationshipEngine* relationshipEngine,
    SmartRelationshipBuilder* relationshipBuilder,
    QObject* parent)
    : QObject(parent)
    , scheduler(scheduler)
    , progressCoordinator(progressCoordinator)
    , symbolAnalyzer(symbolAnalyzer)
    , tabManager(tabManager)
    , workspaceManager(workspaceManager)
    , navigationManager(navigationManager)
    , relationshipEngine(relationshipEngine)
    , relationshipBuilder(relationshipBuilder)
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
    connectSymbolSignals();

    signalsConnected = true;
}

void AnalysisCoordinator::configureScheduler()
{
    if (!scheduler)
        return;

    scheduler->setDocumentModel(tabManager ? tabManager->getDocumentModel() : nullptr);
    scheduler->setProjectModel(workspaceManager ? workspaceManager->getProjectModel() : nullptr);
    scheduler->setSymbolAnalyzer(symbolAnalyzer);
    scheduler->setOpenFileContentProvider([this](const QString& fileName) {
        return tabManager ? tabManager->getPlainTextFromOpenFile(fileName) : QString();
    });
    scheduler->setWorkspaceOpenProvider([this]() {
        return workspaceManager && workspaceManager->isWorkspaceOpen();
    });
    scheduler->setWorkspaceSymbolCancelProvider([this]() {
        return progressCoordinator && progressCoordinator->isSymbolAnalysisCancelled();
    });
    scheduler->setRelationshipEngine(relationshipEngine);
    scheduler->setRelationshipBuilder(relationshipBuilder);
}

void AnalysisCoordinator::connectSchedulerSignals()
{
    if (!scheduler)
        return;

    connect(scheduler, &AnalysisScheduler::relationshipDataInvalidated,
            this, []() {
                CompletionManager::getInstance()->invalidateRelationshipCaches();
            });
    connect(scheduler, &AnalysisScheduler::relationshipDataRefreshRequested,
            this, [this]() {
                CompletionManager::getInstance()->refreshRelationshipData();
                if (navigationManager)
                    navigationManager->refreshCurrentView();
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
    progressCoordinator->connectToSymbolAnalyzer(symbolAnalyzer);
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

void AnalysisCoordinator::connectSymbolSignals()
{
    if (!symbolAnalyzer)
        return;

    connect(symbolAnalyzer, &SymbolAnalyzer::analysisCompleted,
            this, [this](const QString& fileName, int symbolCount) {
                Q_UNUSED(symbolCount)
                refreshActiveEditorForFile(fileName);
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
