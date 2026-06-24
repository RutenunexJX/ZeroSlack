#include "relationshipanalysiscontroller.h"

#include "smartrelationshipbuilder.h"

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
    if (result.cancelled) {
        emit workspaceRelationshipAnalysisCancelled();
        return;
    }
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
