#include "relationshipanalysiscontroller.h"

#include "semanticindex.h"
#include "smartrelationshipbuilder.h"
#include "symbolanalyzer.h"

#include <QtConcurrent/QtConcurrent>
#include <QFuture>

RelationshipAnalysisController::RelationshipAnalysisController(QObject* parent)
    : QObject(parent)
{
    singleFileWatcher =
        new QFutureWatcher<SingleFileRelationshipAnalysisResult>(this);
    connect(singleFileWatcher,
            &QFutureWatcher<SingleFileRelationshipAnalysisResult>::finished,
            this,
            &RelationshipAnalysisController::handleSingleFileFinished);

    workspaceWatcher =
        new QFutureWatcher<WorkspaceRelationshipAnalysisResult>(this);
    connect(workspaceWatcher,
            &QFutureWatcher<WorkspaceRelationshipAnalysisResult>::finished,
            this,
            &RelationshipAnalysisController::handleWorkspaceFinished);
}

RelationshipAnalysisController::~RelationshipAnalysisController()
{
    cancelSingleFileAnalysis();
    cancelWorkspaceAnalysis();
}

void RelationshipAnalysisController::setSymbolAnalyzer(SymbolAnalyzer* analyzer)
{
    symbolAnalyzer = analyzer;
}

void RelationshipAnalysisController::setRelationshipBuilder(
    SmartRelationshipBuilder* builder)
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
            &RelationshipAnalysisController::relationshipAnalysisError);
    connect(relationshipBuilder,
            &SmartRelationshipBuilder::analysisCancelled,
            this,
            &RelationshipAnalysisController::relationshipAnalysisCancelled);
}

void RelationshipAnalysisController::setRelationshipQueue(
    RelationshipAnalysisQueue* queue)
{
    relationshipQueue = queue;
}

void RelationshipAnalysisController::setResultPublisher(
    RelationshipResultPublisher* publisher)
{
    resultPublisher = publisher;
}

bool RelationshipAnalysisController::hasRelationshipBuilder() const
{
    return relationshipBuilder != nullptr;
}

void RelationshipAnalysisController::requestSingleFileAnalysis(
    const QString& fileName,
    const QString& content)
{
    if (fileName.isEmpty()
        || content.isEmpty()
        || !relationshipBuilder
        || !singleFileWatcher) {
        return;
    }

    if (symbolAnalyzer && relationshipQueue) {
        const QString lastContent = relationshipQueue->lastContent(fileName);
        if (!lastContent.isNull()
            && !symbolAnalyzer->hasSignificantChanges(lastContent, content)) {
            return;
        }
    }

    if (relationshipQueue)
        relationshipQueue->rememberRequestedContent(fileName, content);
    cancelSingleFileAnalysis();

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
    singleFileWatcher->setFuture(future);
}

void RelationshipAnalysisController::cancelSingleFileAnalysis()
{
    if (!singleFileWatcher || !singleFileWatcher->isRunning())
        return;

    if (relationshipBuilder)
        relationshipBuilder->cancelAnalysis();

    QFuture<SingleFileRelationshipAnalysisResult> future =
        singleFileWatcher->future();
    singleFileWatcher->cancel();
    future.waitForFinished();
}

void RelationshipAnalysisController::requestWorkspaceAnalysis(
    const ProjectSnapshot& project)
{
    if (!project.isOpen()
        || project.systemVerilogFiles.isEmpty()
        || !relationshipBuilder
        || !workspaceWatcher) {
        return;
    }

    cancelWorkspaceAnalysis();

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
    workspaceWatcher->setFuture(future);
}

void RelationshipAnalysisController::cancelWorkspaceAnalysis()
{
    if (!workspaceWatcher || !workspaceWatcher->isRunning())
        return;

    if (relationshipBuilder)
        relationshipBuilder->cancelAnalysis();

    QFuture<WorkspaceRelationshipAnalysisResult> future = workspaceWatcher->future();
    workspaceWatcher->cancel();
    future.waitForFinished();
}

void RelationshipAnalysisController::handleSingleFileFinished()
{
    if (!singleFileWatcher)
        return;
    if (singleFileWatcher->isCanceled())
        return;

    const SingleFileRelationshipAnalysisResult result =
        singleFileWatcher->result();
    if (!resultPublisher || !resultPublisher->applySingleFileResult(result))
        return;

    emit relationshipAnalysisProgress(
        result.fileName,
        result.relationships.size());
    emit relationshipAnalysisFinished(result);
}

void RelationshipAnalysisController::handleWorkspaceFinished()
{
    if (!workspaceWatcher)
        return;
    if (workspaceWatcher->isCanceled()) {
        emit workspaceRelationshipAnalysisCancelled();
        return;
    }

    const WorkspaceRelationshipAnalysisResult result =
        workspaceWatcher->result();
    if (!resultPublisher || !resultPublisher->applyWorkspaceResult(result))
        return;

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
}
