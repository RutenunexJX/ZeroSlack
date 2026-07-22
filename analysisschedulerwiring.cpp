#include "analysisscheduler.h"

#include "diagnosticsrefreshcontroller.h"

void AnalysisScheduler::setupOpenDocumentAnalysis()
{
    openDocumentAnalysis = new OpenDocumentAnalysisController(this);
    openDocumentAnalysis->setWorkspaceAnalysisActiveProvider([this]() {
        return workspaceSymbolAnalysis
            && workspaceSymbolAnalysis->isWorkspaceAnalysisActive();
    });
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
}

void AnalysisScheduler::setupRelationshipAnalysis()
{
    relationshipAnalysisQueue = new RelationshipAnalysisQueue(this);
    relationshipAnalysisQueue->setContentProvider([this](const QString& fileName) {
        return contentForOpenFile(fileName);
    });
    connect(relationshipAnalysisQueue,
            &RelationshipAnalysisQueue::relationshipAnalysisRequested,
            this,
            &AnalysisScheduler::requestRelationshipAnalysis);

    relationshipAnalysis = new RelationshipAnalysisController(this);
    relationshipAnalysis->setRelationshipQueue(relationshipAnalysisQueue);
    connect(relationshipAnalysis,
            &RelationshipAnalysisController::relationshipAnalysisProgress,
            this,
            &AnalysisScheduler::relationshipAnalysisProgress);
    connect(relationshipAnalysis,
            &RelationshipAnalysisController::relationshipAnalysisError,
            this,
            &AnalysisScheduler::relationshipAnalysisError);
    connect(relationshipAnalysis,
            &RelationshipAnalysisController::relationshipAnalysisCancelled,
            this,
            &AnalysisScheduler::relationshipAnalysisCancelled);
    connect(relationshipAnalysis,
            &RelationshipAnalysisController::relationshipAnalysisFinished,
            this,
            &AnalysisScheduler::relationshipAnalysisFinished);
    connect(relationshipAnalysis,
            &RelationshipAnalysisController::workspaceRelationshipAnalysisStarted,
            this,
            &AnalysisScheduler::workspaceRelationshipAnalysisStarted);
    connect(relationshipAnalysis,
            &RelationshipAnalysisController::workspaceRelationshipAnalysisProgress,
            this,
            &AnalysisScheduler::workspaceRelationshipAnalysisProgress);
    connect(relationshipAnalysis,
            &RelationshipAnalysisController::workspaceRelationshipAnalysisFinished,
            this,
            &AnalysisScheduler::workspaceRelationshipAnalysisFinished);
    connect(relationshipAnalysis,
            &RelationshipAnalysisController::workspaceRelationshipAnalysisCancelled,
            this,
            &AnalysisScheduler::workspaceRelationshipAnalysisCancelled);

    relationshipResultPublisher = new RelationshipResultPublisher(this);
    relationshipAnalysis->setResultPublisher(relationshipResultPublisher);
    connect(relationshipResultPublisher,
            &RelationshipResultPublisher::relationshipDataInvalidated,
            this,
            &AnalysisScheduler::relationshipDataInvalidated);
    connect(relationshipResultPublisher,
            &RelationshipResultPublisher::relationshipDataRefreshRequested,
            this,
            &AnalysisScheduler::relationshipDataRefreshRequested);
}

void AnalysisScheduler::setupWorkspaceSymbolAnalysis()
{
    workspaceSymbolAnalysis = new WorkspaceSymbolAnalysisController(this);
    connect(workspaceSymbolAnalysis,
            &WorkspaceSymbolAnalysisController::fileSymbolAnalysisStarted,
            this,
            &AnalysisScheduler::fileSymbolAnalysisStarted);
    connect(workspaceSymbolAnalysis,
            &WorkspaceSymbolAnalysisController::fileSymbolAnalysisFinished,
            this,
            &AnalysisScheduler::fileSymbolAnalysisFinished);
    connect(workspaceSymbolAnalysis,
            &WorkspaceSymbolAnalysisController::workspaceAnalysisPlanPrepared,
            this,
            &AnalysisScheduler::workspaceAnalysisPlanPrepared);
    connect(workspaceSymbolAnalysis,
            &WorkspaceSymbolAnalysisController::workspaceSymbolAnalysisStarted,
            this,
            [this](const ProjectSnapshot& project, int totalFiles) {
                emit workspaceSymbolAnalysisStarted(project, totalFiles);
            });
    connect(workspaceSymbolAnalysis,
            &WorkspaceSymbolAnalysisController::workspaceSymbolAnalysisProgress,
            this,
            &AnalysisScheduler::workspaceSymbolAnalysisProgress);
    connect(workspaceSymbolAnalysis,
            &WorkspaceSymbolAnalysisController::workspaceAnalysisRequestQueued,
            this,
            &AnalysisScheduler::workspaceAnalysisRequestQueued);
    connect(workspaceSymbolAnalysis,
            &WorkspaceSymbolAnalysisController::workspaceAnalysisRequestResolved,
            this,
            &AnalysisScheduler::workspaceAnalysisRequestResolved);
    connect(workspaceSymbolAnalysis,
            &WorkspaceSymbolAnalysisController::workspaceSymbolAnalysisCancelled,
            this,
            &AnalysisScheduler::workspaceSymbolAnalysisCancelled);
    connect(workspaceSymbolAnalysis,
            &WorkspaceSymbolAnalysisController::workspaceSymbolAnalysisFinished,
            this,
            [this](const ProjectSnapshot& project, int filesAnalyzed, int totalSymbols) {
                emit workspaceSymbolAnalysisFinished(project, filesAnalyzed, totalSymbols);
            });
    connect(workspaceSymbolAnalysis,
            &WorkspaceSymbolAnalysisController::workspaceSymbolAnalysisDeferred,
            this,
            &AnalysisScheduler::workspaceSymbolAnalysisDeferred);
    connect(workspaceSymbolAnalysis,
            &WorkspaceSymbolAnalysisController::semanticAnalysisRequestStarted,
            this,
            &AnalysisScheduler::onSemanticAnalysisStarted);
    connect(workspaceSymbolAnalysis,
            &WorkspaceSymbolAnalysisController::semanticAnalysisRequestFinished,
            this,
            &AnalysisScheduler::onSemanticAnalysisFinished);
    connect(workspaceSymbolAnalysis,
            &WorkspaceSymbolAnalysisController::semanticAnalysisRequestFailed,
            this,
            &AnalysisScheduler::onSemanticAnalysisFailed);
    connect(workspaceSymbolAnalysis,
            &WorkspaceSymbolAnalysisController::semanticAnalysisRequestDropped,
            this,
            &AnalysisScheduler::onSemanticAnalysisDropped);
    connect(workspaceSymbolAnalysis,
            &WorkspaceSymbolAnalysisController::
                semanticAnalysisContinuationRequired,
            this,
            [this](const SemanticAnalysisRequest& remaining) {
                if (remaining.changedFiles.isEmpty())
                    return;
                requestSemanticAnalysis(
                    remaining.reason,
                    SemanticChangeImpact::Unknown,
                    remaining.triggerFile,
                    remaining.changedFiles,
                    remaining.project);
            });
    connect(workspaceSymbolAnalysis,
            &WorkspaceSymbolAnalysisController::semanticAnalysisPlanPrepared,
            this,
            &AnalysisScheduler::semanticAnalysisPlanPrepared);
    connect(workspaceSymbolAnalysis,
            &WorkspaceSymbolAnalysisController::semanticAnalysisTelemetry,
            this,
            &AnalysisScheduler::semanticAnalysisTelemetry);
}

void AnalysisScheduler::setupDiagnosticsRefreshAndWorkspaceRequests()
{
    diagnosticsRefresh = new DiagnosticsRefreshController(this);
    connect(diagnosticsRefresh,
            &DiagnosticsRefreshController::diagnosticsRefreshRequested,
            this,
            &AnalysisScheduler::diagnosticsRefreshRequested);
    connect(workspaceSymbolAnalysis,
            &WorkspaceSymbolAnalysisController::diagnosticsRefreshRequested,
            diagnosticsRefresh,
            &DiagnosticsRefreshController::requestRefresh);
    // Relationships are part of the same worker transaction and publication
    // diff. A second post-symbol workspace pass would duplicate both Slang
    // work and UI refreshes.
    connect(workspaceSymbolAnalysis,
            &WorkspaceSymbolAnalysisController::workspaceRelationshipAnalysisCancelRequested,
            this,
            &AnalysisScheduler::cancelWorkspaceRelationshipAnalysis);
    connect(workspaceSymbolAnalysis,
            &WorkspaceSymbolAnalysisController::relationshipDataClearRequested,
            relationshipResultPublisher,
            &RelationshipResultPublisher::clearAllRelationships);
}
