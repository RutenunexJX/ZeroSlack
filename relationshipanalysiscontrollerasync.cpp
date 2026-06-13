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
