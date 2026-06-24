#include "workspacesymbolanalysiscontroller.h"

#include "documentmodel.h"
#include "semanticindex.h"
#include "symbolanalyzer.h"

void WorkspaceSymbolAnalysisController::requestWorkspaceAnalysis(
    const ProjectSnapshot& project)
{
    if (!project.isOpen() || !symbolAnalyzer)
        return;

    if (project.systemVerilogFiles.isEmpty()) {
        workspaceAnalysisActive = false;
        symbolAnalyzer->cancelWorkspaceAnalysisAndInvalidate();
        return;
    }

    if (requestQueue.active()) {
        requestQueue.queueLatest(project);
        emit workspaceAnalysisRequestQueued(requestQueue.telemetry());
        symbolAnalyzer->expireWorkspaceAnalysis();
        return;
    }

    startWorkspaceAnalysis(project);
}

void WorkspaceSymbolAnalysisController::startWorkspaceAnalysis(
    const ProjectSnapshot& project)
{
    WorkspaceAnalysisPlanQuery query;
    query.project = project;
    query.currentFileName = currentFileProvider ? currentFileProvider() : QString();
    query.openDocuments = documentModel
        ? documentModel->openDocuments()
        : QList<DocumentSnapshot>();
    const WorkspaceAnalysisPlan plan =
        WorkspaceAnalysisPlanService::getInstance()->planForWorkspace(query);

    symbolAnalyzer->setWorkspaceProtectedFiles(plan.protectedFiles);
    symbolAnalyzer->setWorkspacePriorityPublicationCheckpoints(
        plan.priorityPublicationCheckpoints);
    requestQueue.start(project);
    activeProject = project;
    workspaceAnalysisActive = true;
    emit diagnosticsRefreshRequested(QString());
    emit workspaceAnalysisPlanPrepared(plan);
    emit workspaceSymbolAnalysisStarted(project, project.systemVerilogFiles.size());
    symbolAnalyzer->startAnalyzeProjectAsync(plan.isValid() ? plan.project : project,
                                             cancelProvider);
}

void WorkspaceSymbolAnalysisController::cancelWorkspaceAnalysis()
{
    if (!workspaceAnalysisActive
        && !requestQueue.active()
        && !requestQueue.hasPending()) {
        return;
    }

    const WorkspaceAnalysisRequestTelemetry telemetry = requestQueue.cancel();
    workspaceAnalysisActive = false;
    activeProject = ProjectSnapshot();
    emit workspaceSymbolAnalysisCancelled(telemetry);
    if (symbolAnalyzer)
        symbolAnalyzer->expireWorkspaceAnalysis();
}

void WorkspaceSymbolAnalysisController::clearProjectSemanticState()
{
    if (projectSemanticStateCleared)
        return;

    projectSemanticStateCleared = true;
    workspaceAnalysisActive = false;
    requestQueue.clear();
    activeProject = ProjectSnapshot();
    if (symbolAnalyzer)
        symbolAnalyzer->cancelWorkspaceAnalysisAndInvalidate();
    emit workspaceRelationshipAnalysisCancelRequested();
    SemanticIndex::getInstance()->clearSnapshot();
    emit relationshipDataClearRequested();
    emit diagnosticsRefreshRequested(QString());
}

void WorkspaceSymbolAnalysisController::onProjectChanged(
    const ProjectSnapshot& project)
{
    if (!project.isOpen()) {
        clearProjectSemanticState();
        return;
    }

    projectSemanticStateCleared = false;
    requestWorkspaceAnalysis(project);
}

void WorkspaceSymbolAnalysisController::onWorkspaceSymbolAnalysisCompleted(
    int filesAnalyzed,
    int totalSymbols)
{
    if (!workspaceAnalysisActive)
        return;

    const ProjectSnapshot project = activeProject;
    ProjectSnapshot pendingProject;
    const bool hasPending =
        requestQueue.finishAndTakePending(&pendingProject);
    emit workspaceAnalysisRequestResolved(requestQueue.telemetry());
    workspaceAnalysisActive = false;
    activeProject = ProjectSnapshot();
    if (hasPending) {
        requestWorkspaceAnalysis(pendingProject);
        return;
    }
    if (cancelProvider && cancelProvider())
        return;

    emit workspaceSymbolAnalysisFinished(project, filesAnalyzed, totalSymbols);
    emit workspaceRelationshipAnalysisRequested(project);
}

void WorkspaceSymbolAnalysisController::onWorkspaceSymbolAnalysisExpired()
{
    if (!workspaceAnalysisActive)
        return;

    ProjectSnapshot pendingProject;
    const bool hasPending =
        requestQueue.finishAndTakePending(&pendingProject);
    emit workspaceAnalysisRequestResolved(requestQueue.telemetry());
    workspaceAnalysisActive = false;
    activeProject = ProjectSnapshot();
    if (hasPending)
        requestWorkspaceAnalysis(pendingProject);
}
