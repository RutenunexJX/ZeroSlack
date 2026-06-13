#include "workspacesymbolanalysiscontroller.h"

#include "semanticindex.h"
#include "symbolanalyzer.h"

WorkspaceSymbolAnalysisController::WorkspaceSymbolAnalysisController(QObject* parent)
    : QObject(parent)
{
}

void WorkspaceSymbolAnalysisController::setProjectModel(ProjectModel* model)
{
    if (projectModel == model)
        return;
    if (projectModel)
        disconnect(projectModel, nullptr, this, nullptr);

    projectModel = model;
    if (!projectModel)
        return;

    connect(projectModel,
            &ProjectModel::projectChanged,
            this,
            &WorkspaceSymbolAnalysisController::onProjectChanged);
    connect(projectModel,
            &ProjectModel::projectClosed,
            this,
            &WorkspaceSymbolAnalysisController::clearProjectSemanticState);
    projectSemanticStateCleared = !projectModel->isOpen();
}

void WorkspaceSymbolAnalysisController::setSymbolAnalyzer(SymbolAnalyzer* analyzer)
{
    if (symbolAnalyzer == analyzer)
        return;
    if (symbolAnalyzer)
        disconnect(symbolAnalyzer, nullptr, this, nullptr);

    symbolAnalyzer = analyzer;
    if (!symbolAnalyzer)
        return;

    connect(symbolAnalyzer,
            &SymbolAnalyzer::analysisStarted,
            this,
            &WorkspaceSymbolAnalysisController::fileSymbolAnalysisStarted);
    connect(symbolAnalyzer,
            &SymbolAnalyzer::analysisCompleted,
            this,
            [this](const QString& fileName, int symbolCount) {
                emit fileSymbolAnalysisFinished(fileName, symbolCount);
                emit diagnosticsRefreshRequested(fileName);
            });
    connect(symbolAnalyzer,
            &SymbolAnalyzer::batchProgress,
            this,
            [this](int filesDone, int totalFiles, const QString& currentFileName) {
                emit workspaceSymbolAnalysisProgress(currentFileName,
                                                     filesDone,
                                                     totalFiles);
            });
    connect(symbolAnalyzer,
            &SymbolAnalyzer::batchAnalysisCompleted,
            this,
            [this](int filesAnalyzed, int totalSymbols) {
                emit diagnosticsRefreshRequested(QString());
                onWorkspaceSymbolAnalysisCompleted(filesAnalyzed, totalSymbols);
            });
}

void WorkspaceSymbolAnalysisController::setCancelProvider(
    std::function<bool()> provider)
{
    cancelProvider = std::move(provider);
}

void WorkspaceSymbolAnalysisController::requestWorkspaceAnalysis(
    const ProjectSnapshot& project)
{
    if (!project.isOpen() || !symbolAnalyzer)
        return;

    SemanticIndex::getInstance()->clearSnapshot();
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
