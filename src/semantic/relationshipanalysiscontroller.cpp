#include "relationshipanalysiscontroller.h"

#include "smartrelationshipbuilder.h"
#include "symbolanalyzer.h"

#include <exception>
#include <QDir>
#include <QPointer>
#include <utility>

namespace {
QString normalizedFinishedRelationshipPath(const QString& path)
{
    QString normalized = QDir::cleanPath(QDir::fromNativeSeparators(path));
#ifdef Q_OS_WIN
    normalized = normalized.toCaseFolded();
#endif
    return normalized;
}
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
    if (relationshipBuilder) {
        // A worker captures the builder as a raw pointer. Join it before the
        // old builder can be detached or destroyed.
        QPointer<RelationshipAnalysisController> self(this);
        SmartRelationshipBuilder* const previousBuilder =
            relationshipBuilder;
        QPointer<SmartRelationshipBuilder> oldBuilder(previousBuilder);
        requestCancelAllAnalyses();
        if (!self)
            return;
        waitForAllAnalyses();
        if (!self)
            return;
        // A synchronous cancellation observer may have installed a newer
        // builder. The reentrant request is authoritative; never detach it or
        // overwrite it from this older call.
        if (relationshipBuilder != previousBuilder)
            return;
        if (!oldBuilder) {
            relationshipBuilder = nullptr;
            return;
        }
        disconnect(oldBuilder.data(), nullptr, this, nullptr);
    }

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

void RelationshipAnalysisController::setWorkspaceWorkerStartGateForTesting(
    WorkspaceWorkerStartGateForTesting gate)
{
    workspaceWorkerStartGateForTesting = std::move(gate);
}

void RelationshipAnalysisController::handleSingleFileFinished(
    QFutureWatcher<SingleFileRelationshipAnalysisResult>* watcher,
    std::uint64_t requestGeneration,
    const QString& fileKey)
{
    if (!watcher)
        return;
    if (watcher->isCanceled())
        return;

    SingleFileRelationshipAnalysisResult result;
    try {
        result = watcher->result();
    } catch (const std::exception& error) {
        emit relationshipAnalysisError(
            QString(), QString::fromUtf8(error.what()));
        return;
    } catch (...) {
        emit relationshipAnalysisError(
            QString(),
            QStringLiteral("Relationship analysis failed with an unknown exception."));
        return;
    }
    if (singleFileRequestGeneration != requestGeneration
        || normalizedFinishedRelationshipPath(result.fileName) != fileKey) {
        return;
    }
    QPointer<RelationshipAnalysisController> self(this);
    RelationshipResultPublisher* publisher = resultPublisher;
    if (!publisher || !publisher->applySingleFileResult(result))
        return;
    if (!self || singleFileRequestGeneration != requestGeneration)
        return;

    emit relationshipAnalysisProgress(
        result.fileName,
        result.relationships.size());
    if (!self || singleFileRequestGeneration != requestGeneration)
        return;
    emit relationshipAnalysisFinished(result);
}

void RelationshipAnalysisController::handleWorkspaceFinished(
    QFutureWatcher<WorkspaceRelationshipAnalysisResult>* watcher,
    std::uint64_t requestGeneration,
    const QString& projectKey)
{
    if (!watcher)
        return;
    if (watcher->isCanceled()) {
        emit workspaceRelationshipAnalysisCancelled();
        return;
    }

    WorkspaceRelationshipAnalysisResult result;
    try {
        result = watcher->result();
    } catch (const std::exception& error) {
        emit relationshipAnalysisError(
            QStringLiteral("workspace"),
            QString::fromUtf8(error.what()));
        return;
    } catch (...) {
        emit relationshipAnalysisError(
            QStringLiteral("workspace"),
            QStringLiteral("Workspace relationship analysis failed with an unknown exception."));
        return;
    }
    if (result.requestGeneration != requestGeneration
        || result.projectKey != projectKey) {
        return;
    }
    if (workspaceRequestGeneration != requestGeneration)
        return;
    QPointer<RelationshipAnalysisController> self(this);
    if (result.cancelled) {
        emit workspaceRelationshipAnalysisCancelled();
        return;
    }
    RelationshipResultPublisher* publisher = resultPublisher;
    if (!publisher || !publisher->applyWorkspaceResult(result))
        return;
    if (!self || workspaceRequestGeneration != requestGeneration)
        return;

    const int totalFiles = result.totalFiles > 0
        ? result.totalFiles
        : result.fileRelationships.size();
    int processedFiles = 0;
    for (const auto& pair : result.fileRelationships) {
        ++processedFiles;
        emit relationshipAnalysisProgress(pair.first, pair.second.size());
        if (!self || workspaceRequestGeneration != requestGeneration)
            return;
        emit workspaceRelationshipAnalysisProgress(pair.first,
                                                   pair.second.size(),
                                                   processedFiles,
                                                   totalFiles);
        if (!self || workspaceRequestGeneration != requestGeneration)
            return;
    }
    if (workspaceRequestGeneration != requestGeneration)
        return;
    emit workspaceRelationshipAnalysisFinished(result);
}
