#include "relationshipanalysiscontroller.h"
#include "symbolanalyzer.h"
#include "semanticanalysisinput.h"

#include <QFileInfo>
#include <QPointer>

RelationshipAnalysisController::RelationshipAnalysisController(QObject* parent)
    : QObject(parent)
{
}

RelationshipAnalysisController::~RelationshipAnalysisController()
{
    if (cancellation)
        cancellation->store(true, std::memory_order_relaxed);
}

void RelationshipAnalysisController::requestSingleFileAnalysis(
    const QString& fileName, const QString& content, const ProjectSnapshot& project,
    std::uint64_t documentRevision)
{
    if (fileName.isEmpty() || content.isNull() || !symbolAnalyzer)
        return;
    SemanticAnalysisRequest request;
    request.project = project;
    request.triggerFile = fileName;
    request.changedFiles = {fileName};
    request.sourceOverrides.insert(fileName, content);
    request.documentRevisions.insert(fileName, documentRevision);
    // An isolated document has no workspace root and cannot replace a
    // workspace's configuration, facts, watchers or unrelated shards.
    if (!project.isOpen()) {
        request.project.systemVerilogFiles = {fileName};
        request.project.includeDirs = {QFileInfo(fileName).absolutePath()};
    }
    submit(std::move(request), RequestKind::SingleFile);
}

void RelationshipAnalysisController::requestWorkspaceAnalysis(const ProjectSnapshot& project)
{
    if (!project.isOpen() || !symbolAnalyzer)
        return;
    SemanticAnalysisRequest request;
    request.project = project;
    request.changedFiles = project.systemVerilogFiles;
    request.impactHint = SemanticChangeImpact::WorkspaceConfig;
    submit(std::move(request), RequestKind::Workspace);
}

void RelationshipAnalysisController::submit(SemanticAnalysisRequest request, RequestKind kind)
{
    if (!runtimePolicy.enabled)
        return;
    request.generation = symbolAnalyzer->nextCompatibilityRequestGeneration();
    request.compatibilityRequest = true;
    request.relationshipProjectionRequested = true;
    request.reason = SemanticAnalysisReason::ExplicitRequest;
    request.runtimePolicy = runtimePolicy;
    const RequestKind previousKind = activeKind;
    if (cancellation)
        cancellation->store(true, std::memory_order_relaxed);
    const auto cancelled = std::make_shared<std::atomic_bool>(false);
    cancellation = cancelled;
    activeRequest = request;
    activeKind = kind;
    QPointer<RelationshipAnalysisController> self(this);
    if (previousKind == RequestKind::SingleFile)
        emit relationshipAnalysisCancelled();
    else if (previousKind == RequestKind::Workspace)
        emit workspaceRelationshipAnalysisCancelled();
    if (!self || cancellation != cancelled)
        return;
    if (kind == RequestKind::Workspace)
        emit workspaceRelationshipAnalysisStarted(request.project, request.project.systemVerilogFiles.size());
    if (!self || cancellation != cancelled || !symbolAnalyzer)
        return;
    const auto gate = workspaceWorkerStartGateForTesting;
    const auto gateEntered = std::make_shared<std::atomic_bool>(false);
    symbolAnalyzer->startSemanticAnalysisAsync(request, [cancelled, gate, gateEntered] {
        if (gate && !gateEntered->exchange(true, std::memory_order_relaxed))
            gate([cancelled] { return cancelled->load(std::memory_order_relaxed); });
        return cancelled->load(std::memory_order_relaxed);
    });
}

void RelationshipAnalysisController::cancel(RequestKind kind)
{
    if (activeKind != kind)
        return;
    if (cancellation)
        cancellation->store(true, std::memory_order_relaxed);
    cancellation.reset();
    activeKind = RequestKind::None;
    activeRequest = {};
    if (kind == RequestKind::SingleFile)
        emit relationshipAnalysisCancelled();
    else if (kind == RequestKind::Workspace)
        emit workspaceRelationshipAnalysisCancelled();
}

void RelationshipAnalysisController::cancelSingleFileAnalysis()
{
    cancel(RequestKind::SingleFile);
}

void RelationshipAnalysisController::cancelWorkspaceAnalysis()
{
    cancel(RequestKind::Workspace);
}

void RelationshipAnalysisController::requestCancelAllAnalyses()
{
    cancel(activeKind);
}

void RelationshipAnalysisController::waitForAllAnalyses()
{
    // No worker/future/builder lease belongs to this adapter. The analyzer
    // drains its one owned semantic pool and retirement pool during shutdown.
    requestCancelAllAnalyses();
}
