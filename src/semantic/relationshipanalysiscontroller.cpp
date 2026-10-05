#include "relationshipanalysiscontroller.h"
#include "smartrelationshipbuilder.h"
#include "symbolanalyzer.h"
#include "semanticanalysisinput.h"
#include <QScopeGuard>

void RelationshipAnalysisController::setSymbolAnalyzer(SymbolAnalyzer* analyzer)
{
    if (symbolAnalyzer == analyzer)
        return;
    requestCancelAllAnalyses();
    if (symbolAnalyzer)
        disconnect(symbolAnalyzer, nullptr, this, nullptr);
    symbolAnalyzer = analyzer;
    if (!symbolAnalyzer)
        return;
    connect(symbolAnalyzer, &SymbolAnalyzer::relationshipAnalysisCommitted,
            this, &RelationshipAnalysisController::finish);
    connect(symbolAnalyzer, &SymbolAnalyzer::semanticAnalysisDropped, this,
            [this](const SemanticAnalysisRequest& request, SemanticAnalysisRequestDisposition) {
                if (matches(request))
                    cancel(activeKind);
            });
    connect(symbolAnalyzer, &SymbolAnalyzer::semanticAnalysisFailed, this,
            [this](const SemanticAnalysisRequest& request, const QString& error) {
                if (!matches(request))
                    return;
                activeKind = RequestKind::None;
                activeRequest = {};
                cancellation.reset();
                emit relationshipAnalysisError(request.triggerFile, error);
            });
}

void RelationshipAnalysisController::setRelationshipBuilder(SmartRelationshipBuilder* builder)
{
    // Kept as an API capability handle; workers never borrow this QObject.
    relationshipBuilder = builder;
}

void RelationshipAnalysisController::setResultPublisher(RelationshipResultPublisher* publisher)
{
    resultPublisher = publisher;
}

bool RelationshipAnalysisController::hasRelationshipBuilder() const
{
    return relationshipBuilder != nullptr;
}

void RelationshipAnalysisController::setRuntimePolicy(const SemanticAnalysisRuntimePolicy& policy)
{
    runtimePolicy = policy.normalized();
    if (!runtimePolicy.enabled)
        requestCancelAllAnalyses();
}

void RelationshipAnalysisController::setWorkspaceWorkerStartGateForTesting(
    WorkspaceWorkerStartGateForTesting gate)
{
    workspaceWorkerStartGateForTesting = std::move(gate);
}

bool RelationshipAnalysisController::matches(const SemanticAnalysisRequest& request) const
{
    return activeKind != RequestKind::None && request.compatibilityRequest
        && request.generation == activeRequest.generation
        && request.project.semanticIdentity() == activeRequest.project.semanticIdentity();
}

void RelationshipAnalysisController::finish(
    const SemanticAnalysisRequest& request, const WorkspaceRelationshipAnalysisResult& publication)
{
    if (!matches(request) || !cancellation
        || cancellation->load(std::memory_order_relaxed))
        return;
    const auto generation = activeRequest.generation;
    const auto kind = activeKind;
    const QString file = activeRequest.triggerFile;
    QPointer<RelationshipAnalysisController> self(this);
    bool completed = false;
    const auto completion = qScopeGuard([this, self, generation, &completed] {
        if (!self || activeRequest.generation != generation)
            return;
        if (!completed) {
            cancel(activeKind);
            return;
        }
        activeKind = RequestKind::None;
        activeRequest = {};
        cancellation.reset();
    });
    auto current = [&] {
        const auto token = SemanticIndex::getInstance()->snapshotToken();
        return self && activeRequest.generation == generation
            && token.revision == publication.baseSnapshot.revision
            && token.snapshot == publication.semanticSnapshot;
    };
    if (!current())
        return;
    if (kind == RequestKind::SingleFile) {
        SingleFileRelationshipAnalysisResult result;
        result.fileName = file;
        result.baseSnapshot = publication.baseSnapshot;
        result.semanticSnapshot = publication.semanticSnapshot;
        for (const auto& item : publication.fileRelationships)
            if (SemanticInputCapture::pathKey(item.first) == SemanticInputCapture::pathKey(file)) {
                result.relationships = item.second;
                break;
            }
        if (resultPublisher && !resultPublisher->applySingleFileResult(result, symbolAnalyzer))
            return;
        emit relationshipAnalysisProgress(file, result.relationships.size());
        if (!current())
            return;
        completed = true;
        emit relationshipAnalysisFinished(result);
    } else {
        if (resultPublisher && !resultPublisher->applyWorkspaceResult(publication, symbolAnalyzer))
            return;
        int processed = 0;
        for (const auto& item : publication.fileRelationships) {
            emit relationshipAnalysisProgress(item.first, item.second.size());
            if (!current())
                return;
            emit workspaceRelationshipAnalysisProgress(item.first, item.second.size(),
                                                        ++processed, publication.totalFiles);
            if (!current())
                return;
        }
        completed = true;
        emit workspaceRelationshipAnalysisFinished(publication);
    }
}
