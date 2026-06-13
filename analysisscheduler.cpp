#include "analysisscheduler.h"

#include "semanticindex.h"
#include "symbolrelationshipengine.h"
#include "symbolanalyzer.h"

#include <QtConcurrent/QtConcurrent>
#include <QFuture>
#include <QTimer>
#include <utility>

AnalysisScheduler::AnalysisScheduler(QObject* parent)
    : QObject(parent)
{
    openDocumentAnalysis = new OpenDocumentAnalysisController(this);
    connect(openDocumentAnalysis,
            &OpenDocumentAnalysisController::documentRefreshRequested,
            this,
            &AnalysisScheduler::documentRefreshRequested);
    connect(openDocumentAnalysis,
            &OpenDocumentAnalysisController::relationshipAnalysisRequested,
            this,
            &AnalysisScheduler::requestRelationshipAnalysis);
    connect(openDocumentAnalysis,
            &OpenDocumentAnalysisController::relationshipAnalysisScheduled,
            this,
            &AnalysisScheduler::scheduleRelationshipAnalysis);

    relationshipAnalysisQueue = new RelationshipAnalysisQueue(this);
    relationshipAnalysisQueue->setContentProvider([this](const QString& fileName) {
        return contentForOpenFile(fileName);
    });
    connect(relationshipAnalysisQueue,
            &RelationshipAnalysisQueue::relationshipAnalysisRequested,
            this,
            &AnalysisScheduler::requestRelationshipAnalysis);

    relationshipResultPublisher = new RelationshipResultPublisher(this);
    connect(relationshipResultPublisher,
            &RelationshipResultPublisher::relationshipDataInvalidated,
            this,
            &AnalysisScheduler::relationshipDataInvalidated);
    connect(relationshipResultPublisher,
            &RelationshipResultPublisher::relationshipDataRefreshRequested,
            this,
            &AnalysisScheduler::relationshipDataRefreshRequested);

    diagnosticsRefreshTimer = new QTimer(this);
    diagnosticsRefreshTimer->setSingleShot(true);
    diagnosticsRefreshTimer->setInterval(100);
    connect(diagnosticsRefreshTimer, &QTimer::timeout, this, [this]() {
        const QString fileName = pendingDiagnosticsRefreshFileName;
        pendingDiagnosticsRefreshFileName.clear();
        emit diagnosticsRefreshRequested(fileName);
    });

    singleFileRelationshipWatcher =
        new QFutureWatcher<SingleFileRelationshipAnalysisResult>(this);
    connect(singleFileRelationshipWatcher,
            &QFutureWatcher<SingleFileRelationshipAnalysisResult>::finished,
            this,
            [this]() {
                if (!singleFileRelationshipWatcher)
                    return;
                if (singleFileRelationshipWatcher->isCanceled())
                    return;
                const SingleFileRelationshipAnalysisResult result =
                    singleFileRelationshipWatcher->result();
                if (!relationshipResultPublisher
                    || !relationshipResultPublisher->applySingleFileResult(result)) {
                    return;
                }
                emit relationshipAnalysisProgress(
                    result.fileName, result.relationships.size());
                emit relationshipAnalysisFinished(result);
            });

    workspaceRelationshipWatcher = new QFutureWatcher<WorkspaceRelationshipAnalysisResult>(this);
    connect(workspaceRelationshipWatcher,
            &QFutureWatcher<WorkspaceRelationshipAnalysisResult>::finished,
            this,
            [this]() {
                if (!workspaceRelationshipWatcher)
                    return;
                if (workspaceRelationshipWatcher->isCanceled()) {
                    emit workspaceRelationshipAnalysisCancelled();
                    return;
                }
                const WorkspaceRelationshipAnalysisResult result =
                    workspaceRelationshipWatcher->result();
                if (!relationshipResultPublisher
                    || !relationshipResultPublisher->applyWorkspaceResult(result)) {
                    return;
                }
                const int totalFiles = result.totalFiles > 0
                    ? result.totalFiles
                    : result.fileRelationships.size();
                int processedFiles = 0;
                for (const auto& pair : result.fileRelationships) {
                    ++processedFiles;
                    emit relationshipAnalysisProgress(pair.first, pair.second.size());
                    emit workspaceRelationshipAnalysisProgress(pair.first,
                                                               pair.second.size(),
                                                               processedFiles,
                                                               totalFiles);
                }
                emit workspaceRelationshipAnalysisFinished(result);
            });
}

AnalysisScheduler::~AnalysisScheduler()
{
    cancelRelationshipAnalysis();
    cancelWorkspaceRelationshipAnalysis();
}

void AnalysisScheduler::setDocumentModel(DocumentModel* model)
{
    if (documentModel == model)
        return;
    if (documentModel)
        disconnect(documentModel, nullptr, this, nullptr);

    documentModel = model;
    if (openDocumentAnalysis)
        openDocumentAnalysis->setDocumentModel(model);
    if (!documentModel)
        return;

    connect(documentModel, &DocumentModel::documentOpened,
            this, &AnalysisScheduler::onDocumentOpened);
    connect(documentModel, &DocumentModel::documentEdited,
            this, &AnalysisScheduler::onDocumentEdited);
    connect(documentModel, &DocumentModel::documentSaved,
            this, &AnalysisScheduler::onDocumentSaved);
    connect(documentModel, &DocumentModel::documentClosed,
            this, [this](const QString&, const QString& fileName) {
                handleDocumentClosed(fileName);
            });
}

void AnalysisScheduler::setProjectModel(ProjectModel* model)
{
    if (projectModel == model)
        return;
    if (projectModel)
        disconnect(projectModel, nullptr, this, nullptr);

    projectModel = model;
    if (!projectModel)
        return;

    connect(projectModel, &ProjectModel::projectChanged,
            this, &AnalysisScheduler::onProjectChanged);
    connect(projectModel, &ProjectModel::projectClosed,
            this, &AnalysisScheduler::clearProjectSemanticState);
    projectSemanticStateCleared = !projectModel->isOpen();
}

void AnalysisScheduler::setSymbolAnalyzer(SymbolAnalyzer* analyzer)
{
    if (symbolAnalyzer == analyzer)
        return;
    if (symbolAnalyzer)
        disconnect(symbolAnalyzer, nullptr, this, nullptr);

    symbolAnalyzer = analyzer;
    if (openDocumentAnalysis)
        openDocumentAnalysis->setSymbolAnalyzer(analyzer);
    if (!symbolAnalyzer)
        return;

    connect(symbolAnalyzer,
            &SymbolAnalyzer::analysisStarted,
            this,
            &AnalysisScheduler::fileSymbolAnalysisStarted);
    connect(symbolAnalyzer, &SymbolAnalyzer::analysisCompleted,
            this, [this](const QString& fileName, int symbolCount) {
                emit fileSymbolAnalysisFinished(fileName, symbolCount);
                scheduleDiagnosticsRefresh(fileName);
            });
    connect(symbolAnalyzer,
            &SymbolAnalyzer::batchProgress,
            this,
            [this](int filesDone, int totalFiles, const QString& currentFileName) {
                emit workspaceSymbolAnalysisProgress(currentFileName,
                                                     filesDone,
                                                     totalFiles);
            });
    connect(symbolAnalyzer,
            &SymbolAnalyzer::batchAnalysisCompleted,
            this,
            [this](int filesAnalyzed, int totalSymbols) {
                scheduleDiagnosticsRefresh(QString());
                onWorkspaceSymbolAnalysisCompleted(filesAnalyzed, totalSymbols);
            });
}

void AnalysisScheduler::setOpenFileContentProvider(std::function<QString(const QString&)> provider)
{
    openFileContentProvider = std::move(provider);
    if (openDocumentAnalysis)
        openDocumentAnalysis->setOpenFileContentProvider(openFileContentProvider);
}

void AnalysisScheduler::setWorkspaceOpenProvider(std::function<bool()> provider)
{
    workspaceOpenProvider = std::move(provider);
    if (openDocumentAnalysis)
        openDocumentAnalysis->setWorkspaceOpenProvider(workspaceOpenProvider);
}

void AnalysisScheduler::setWorkspaceSymbolCancelProvider(std::function<bool()> provider)
{
    workspaceSymbolCancelProvider = std::move(provider);
}

void AnalysisScheduler::setRelationshipEngine(SymbolRelationshipEngine* engine)
{
    if (relationshipResultPublisher)
        relationshipResultPublisher->setRelationshipEngine(engine);
}

void AnalysisScheduler::setRelationshipBuilder(SmartRelationshipBuilder* builder)
{
    if (relationshipBuilder == builder)
        return;
    if (relationshipBuilder)
        disconnect(relationshipBuilder, nullptr, this, nullptr);

    relationshipBuilder = builder;
    if (!relationshipBuilder)
        return;

    connect(relationshipBuilder,
            &SmartRelationshipBuilder::analysisError,
            this,
            [this](const QString& fileName, const QString& error) {
                emit relationshipAnalysisError(fileName, error);
            });
    connect(relationshipBuilder,
            &SmartRelationshipBuilder::analysisCancelled,
            this,
            [this]() {
                emit relationshipAnalysisCancelled();
            });
}

void AnalysisScheduler::scheduleOpenFileAnalysis(const QString& fileName, int delayMs)
{
    if (openDocumentAnalysis)
        openDocumentAnalysis->scheduleOpenFileAnalysis(fileName, delayMs);
}

void AnalysisScheduler::cancelScheduledOpenFileAnalysis(const QString& fileName)
{
    if (openDocumentAnalysis)
        openDocumentAnalysis->cancelScheduledOpenFileAnalysis(fileName);
}

void AnalysisScheduler::scheduleRelationshipAnalysis(const QString& fileName,
                                                     const QString& content,
                                                     int delayMs)
{
    if (fileName.isEmpty() || content.isEmpty() || !relationshipBuilder)
        return;

    if (relationshipAnalysisQueue)
        relationshipAnalysisQueue->schedule(fileName, content, delayMs);
}

void AnalysisScheduler::cancelScheduledRelationshipAnalysis(const QString& fileName)
{
    if (relationshipAnalysisQueue)
        relationshipAnalysisQueue->clearFile(fileName);
}

void AnalysisScheduler::cancelAllScheduledRelationshipAnalyses()
{
    if (relationshipAnalysisQueue)
        relationshipAnalysisQueue->cancelAll();
}

bool AnalysisScheduler::hasScheduledRelationshipAnalysis(
    const QString& fileName) const
{
    return relationshipAnalysisQueue
        ? relationshipAnalysisQueue->hasScheduled(fileName)
        : false;
}

void AnalysisScheduler::requestRelationshipAnalysis(const QString& fileName, const QString& content)
{
    if (fileName.isEmpty()
        || content.isEmpty()
        || !relationshipBuilder
        || !singleFileRelationshipWatcher) {
        return;
    }

    if (symbolAnalyzer) {
        const QString lastContent = relationshipAnalysisQueue
            ? relationshipAnalysisQueue->lastContent(fileName)
            : QString();
        if (!lastContent.isNull() && !symbolAnalyzer->hasSignificantChanges(lastContent, content))
            return;
    }

    if (relationshipAnalysisQueue)
        relationshipAnalysisQueue->rememberRequestedContent(fileName, content);
    cancelRelationshipAnalysis();

    relationshipBuilder->resetCancellation();
    const auto baseSnapshot =
        SemanticIndex::getInstance()->beginRelationshipAnalysisSnapshot();

    QFuture<SingleFileRelationshipAnalysisResult> future =
        QtConcurrent::run([this, fileName, content, baseSnapshot]() {
            return RelationshipAnalysisWorker::analyzeSingleFile(
                relationshipBuilder,
                fileName,
                content,
                baseSnapshot);
        });
    singleFileRelationshipWatcher->setFuture(future);
}

void AnalysisScheduler::cancelRelationshipAnalysis()
{
    if (!singleFileRelationshipWatcher || !singleFileRelationshipWatcher->isRunning())
        return;

    if (relationshipBuilder)
        relationshipBuilder->cancelAnalysis();

    QFuture<SingleFileRelationshipAnalysisResult> future =
        singleFileRelationshipWatcher->future();
    singleFileRelationshipWatcher->cancel();
    future.waitForFinished();
}

void AnalysisScheduler::requestWorkspaceAnalysis(const ProjectSnapshot& project)
{
    if (!project.isOpen() || !symbolAnalyzer)
        return;

    SemanticIndex::getInstance()->clearSnapshot();
    if (project.systemVerilogFiles.isEmpty())
        return;

    activeWorkspaceProject = project;
    workspaceSymbolAnalysisActive = true;
    scheduleDiagnosticsRefresh(QString());
    emit workspaceSymbolAnalysisStarted(project, project.systemVerilogFiles.size());
    symbolAnalyzer->startAnalyzeProjectAsync(project, workspaceSymbolCancelProvider);
}

void AnalysisScheduler::requestWorkspaceRelationshipAnalysis(const ProjectSnapshot& project)
{
    if (!project.isOpen()
        || project.systemVerilogFiles.isEmpty()
        || !relationshipBuilder
        || !workspaceRelationshipWatcher) {
        return;
    }

    cancelWorkspaceRelationshipAnalysis();

    emit workspaceRelationshipAnalysisStarted(project, project.systemVerilogFiles.size());

    const auto baseSnapshot =
        SemanticIndex::getInstance()->beginRelationshipAnalysisSnapshot();

    QFuture<WorkspaceRelationshipAnalysisResult> future =
        QtConcurrent::run([this, project, baseSnapshot]() {
            return RelationshipAnalysisWorker::analyzeWorkspace(
                relationshipBuilder,
                project,
                baseSnapshot);
        });
    workspaceRelationshipWatcher->setFuture(future);
}

void AnalysisScheduler::cancelWorkspaceRelationshipAnalysis()
{
    if (!workspaceRelationshipWatcher || !workspaceRelationshipWatcher->isRunning())
        return;

    if (relationshipBuilder)
        relationshipBuilder->cancelAnalysis();

    QFuture<WorkspaceRelationshipAnalysisResult> future = workspaceRelationshipWatcher->future();
    workspaceRelationshipWatcher->cancel();
    future.waitForFinished();
}

void AnalysisScheduler::handleExternalFileChanged(const QString& fileName, int debounceMs)
{
    if (openDocumentAnalysis)
        openDocumentAnalysis->handleExternalFileChanged(fileName, debounceMs);
}

void AnalysisScheduler::handleDocumentClosed(const QString& fileName)
{
    if (openDocumentAnalysis)
        openDocumentAnalysis->handleDocumentClosed(fileName);
    if (relationshipAnalysisQueue)
        relationshipAnalysisQueue->clearFile(fileName);
    if (openDocumentAnalysis)
        openDocumentAnalysis->analyzeOpenDocumentsNow();

    if (relationshipResultPublisher)
        relationshipResultPublisher->invalidateFileRelationships(fileName);
}

void AnalysisScheduler::onDocumentOpened(const DocumentSnapshot& snapshot)
{
    if (openDocumentAnalysis)
        openDocumentAnalysis->analyzeOpenDocumentNow(snapshot, false);
}

void AnalysisScheduler::onDocumentEdited(const DocumentSnapshot& snapshot)
{
    if (openDocumentAnalysis) {
        openDocumentAnalysis->handleDocumentEdited(
            snapshot,
            kOpenDocumentRelationshipAnalysisDebounceMs);
    }
}

void AnalysisScheduler::onDocumentSaved(const DocumentSnapshot& snapshot)
{
    if (openDocumentAnalysis)
        openDocumentAnalysis->analyzeOpenDocumentNow(snapshot, true);
}

void AnalysisScheduler::onProjectChanged(const ProjectSnapshot& project)
{
    if (!project.isOpen()) {
        clearProjectSemanticState();
        return;
    }

    projectSemanticStateCleared = false;
    requestWorkspaceAnalysis(project);
}

void AnalysisScheduler::clearProjectSemanticState()
{
    if (projectSemanticStateCleared)
        return;

    projectSemanticStateCleared = true;
    workspaceSymbolAnalysisActive = false;
    activeWorkspaceProject = ProjectSnapshot();
    cancelWorkspaceRelationshipAnalysis();
    SemanticIndex::getInstance()->clearSnapshot();

    if (relationshipResultPublisher)
        relationshipResultPublisher->clearAllRelationships();

    scheduleDiagnosticsRefresh(QString());
}

void AnalysisScheduler::onWorkspaceSymbolAnalysisCompleted(int filesAnalyzed, int totalSymbols)
{
    if (!workspaceSymbolAnalysisActive)
        return;

    workspaceSymbolAnalysisActive = false;
    const ProjectSnapshot project = activeWorkspaceProject;
    if (workspaceSymbolCancelProvider && workspaceSymbolCancelProvider())
        return;

    emit workspaceSymbolAnalysisFinished(project, filesAnalyzed, totalSymbols);
    requestWorkspaceRelationshipAnalysis(project);
}

void AnalysisScheduler::scheduleDiagnosticsRefresh(const QString& fileName)
{
    pendingDiagnosticsRefreshFileName = fileName;
    if (diagnosticsRefreshTimer)
        diagnosticsRefreshTimer->start();
}

QString AnalysisScheduler::contentForOpenFile(const QString& fileName) const
{
    return openDocumentAnalysis
        ? openDocumentAnalysis->contentForOpenFile(fileName)
        : QString();
}
