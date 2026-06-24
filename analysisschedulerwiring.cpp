#include "analysisscheduler.h"

#include "diagnosticsrefreshcontroller.h"

void AnalysisScheduler::setupOpenDocumentAnalysis()
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
}

void AnalysisScheduler::refreshOpenDocumentsForForegroundAnalysis()
{
    if (openDocumentAnalysis)
        openDocumentAnalysis->analyzeOpenDocumentsNow();
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
            &WorkspaceSymbolAnalysisController::workspaceSymbolAnalysisStarted,
            this,
            [this](const ProjectSnapshot& project, int totalFiles) {
                refreshOpenDocumentsForForegroundAnalysis();
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
            &WorkspaceSymbolAnalysisController::workspaceSymbolAnalysisFinished,
            this,
            [this](const ProjectSnapshot& project, int filesAnalyzed, int totalSymbols) {
                refreshOpenDocumentsForForegroundAnalysis();
                emit workspaceSymbolAnalysisFinished(project, filesAnalyzed, totalSymbols);
            });
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
    connect(workspaceSymbolAnalysis,
            &WorkspaceSymbolAnalysisController::workspaceRelationshipAnalysisRequested,
            this,
            &AnalysisScheduler::requestWorkspaceRelationshipAnalysis);
    connect(workspaceSymbolAnalysis,
            &WorkspaceSymbolAnalysisController::workspaceRelationshipAnalysisCancelRequested,
            this,
            &AnalysisScheduler::cancelWorkspaceRelationshipAnalysis);
    connect(workspaceSymbolAnalysis,
            &WorkspaceSymbolAnalysisController::relationshipDataClearRequested,
            relationshipResultPublisher,
            &RelationshipResultPublisher::clearAllRelationships);
}
