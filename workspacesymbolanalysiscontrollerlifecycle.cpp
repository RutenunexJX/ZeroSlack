#include "workspacesymbolanalysiscontroller.h"

#include "semanticindex.h"
#include "symbolanalyzer.h"

void WorkspaceSymbolAnalysisController::requestWorkspaceAnalysis(
    const ProjectSnapshot& project)
{
    if (!project.isOpen() || !symbolAnalyzer)
        return;

    if (project.systemVerilogFiles.isEmpty())
        return;

    activeProject = project;
    workspaceAnalysisActive = true;
    emit diagnosticsRefreshRequested(QString());
    emit workspaceSymbolAnalysisStarted(project, project.systemVerilogFiles.size());
    symbolAnalyzer->startAnalyzeProjectAsync(project, cancelProvider);
}

void WorkspaceSymbolAnalysisController::clearProjectSemanticState()
{
    if (projectSemanticStateCleared)
        return;

    projectSemanticStateCleared = true;
    workspaceAnalysisActive = false;
    activeProject = ProjectSnapshot();
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

    workspaceAnalysisActive = false;
    const ProjectSnapshot project = activeProject;
    if (cancelProvider && cancelProvider())
        return;

    emit workspaceSymbolAnalysisFinished(project, filesAnalyzed, totalSymbols);
    emit workspaceRelationshipAnalysisRequested(project);
}
