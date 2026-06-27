#include "workspacesymbolanalysiscontroller.h"

#include "documentmodel.h"
#include "semanticindex.h"
#include "symbolanalyzer.h"

#include <QFileInfo>

namespace {
struct WorkspaceSizeProfile {
    int totalFiles = 0;
    qint64 totalBytes = 0;
    qint64 largestFileBytes = 0;
};

WorkspaceSizeProfile sizeProfileForProject(const ProjectSnapshot& project)
{
    WorkspaceSizeProfile profile;
    profile.totalFiles = project.systemVerilogFiles.size();
    for (const QString& fileName : project.systemVerilogFiles) {
        const qint64 size = QFileInfo(fileName).size();
        if (size <= 0)
            continue;
        profile.totalBytes += size;
        profile.largestFileBytes = qMax(profile.largestFileBytes, size);
    }
    return profile;
}

struct WorkspaceAutomaticAnalysisBudget {
    ProjectSnapshot project;
    WorkspaceSizeProfile profile;
    int automaticFileCount = 0;

    int deferredFileCount() const
    {
        return qMax(0, profile.totalFiles - automaticFileCount);
    }

    bool hasDeferredFiles() const
    {
        return deferredFileCount() > 0;
    }

    bool hasAutomaticFiles() const
    {
        return automaticFileCount > 0;
    }
};

WorkspaceAutomaticAnalysisBudget automaticAnalysisBudgetForPlan(
    const ProjectSnapshot& originalProject,
    const WorkspaceAnalysisPlan& plan)
{
    WorkspaceAutomaticAnalysisBudget budget;
    budget.profile = sizeProfileForProject(originalProject);
    budget.project = plan.project;
    budget.automaticFileCount = budget.project.systemVerilogFiles.size();
    return budget;
}

QHash<QString, SemanticAnalysisBandMetadata> semanticBandMetadataForPlan(
    const WorkspaceAnalysisPlan& plan)
{
    QHash<QString, SemanticAnalysisBandMetadata> bands;
    for (auto it = plan.fileBandMetadataByNormalizedPath.constBegin();
         it != plan.fileBandMetadataByNormalizedPath.constEnd();
         ++it) {
        const WorkspaceAnalysisFileBandMetadata planMetadata = it.value();
        if (!planMetadata.isValid())
            continue;
        SemanticAnalysisBandMetadata metadata;
        metadata.label = planMetadata.label;
        metadata.displayName = planMetadata.displayName;
        metadata.priority = planMetadata.priority;
        metadata.publicationCheckpoint = planMetadata.publicationCheckpoint;
        bands.insert(it.key(), metadata);
    }
    return bands;
}

QString workspaceAnalysisKey(const ProjectSnapshot& project)
{
    QStringList files = project.systemVerilogFiles;
    files.sort(Qt::CaseInsensitive);
    return QStringLiteral("%1\n%2")
        .arg(project.workspaceRoot, files.join(QLatin1Char('\n')));
}
}

void WorkspaceSymbolAnalysisController::requestWorkspaceAnalysis(
    const ProjectSnapshot& project)
{
    if (!project.isOpen() || !symbolAnalyzer)
        return;

    if (project.systemVerilogFiles.isEmpty()) {
        workspaceAnalysisActive = false;
        symbolAnalyzer->setWorkspaceFileAnalysisBands({});
        symbolAnalyzer->cancelWorkspaceAnalysisAndInvalidate();
        return;
    }

    const QString projectKey = workspaceAnalysisKey(project);
    if (completedWorkspaceAnalysisKeys.contains(projectKey)) {
        workspaceAnalysisActive = false;
        activeProject = ProjectSnapshot();
        activeWorkspaceAnalysisComplete = true;
        requestQueue.clear();
        if (symbolAnalyzer)
            symbolAnalyzer->expireWorkspaceAnalysis();
        emit workspaceRelationshipAnalysisCancelRequested();
        emit diagnosticsRefreshRequested(QString());
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
    const WorkspaceAutomaticAnalysisBudget budget =
        automaticAnalysisBudgetForPlan(project, plan);

    symbolAnalyzer->setWorkspaceProtectedFiles(plan.protectedFiles);
    symbolAnalyzer->setWorkspacePriorityPublicationCheckpoints(
        plan.priorityPublicationCheckpoints);
    symbolAnalyzer->setWorkspaceFileAnalysisBands(
        semanticBandMetadataForPlan(plan));
    emit workspaceAnalysisPlanPrepared(plan);

    if (budget.hasDeferredFiles()) {
        emit workspaceSymbolAnalysisDeferred(project,
                                             budget.profile.totalFiles,
                                             budget.profile.totalBytes,
                                             budget.profile.largestFileBytes);
    }

    if (!budget.hasAutomaticFiles()) {
        requestQueue.clear();
        activeProject = ProjectSnapshot();
        workspaceAnalysisActive = false;
        activeWorkspaceAnalysisComplete = false;
        emit diagnosticsRefreshRequested(QString());
        return;
    }

    requestQueue.start(project);
    activeProject = budget.project;
    workspaceAnalysisActive = true;
    activeWorkspaceAnalysisComplete = !budget.hasDeferredFiles();
    emit diagnosticsRefreshRequested(QString());
    emit workspaceSymbolAnalysisStarted(budget.project,
                                        budget.project.systemVerilogFiles.size());
    symbolAnalyzer->startAnalyzeProjectAsync(budget.project,
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
    activeWorkspaceAnalysisComplete = true;
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
    activeWorkspaceAnalysisComplete = true;
    requestQueue.clear();
    activeProject = ProjectSnapshot();
    activeWorkspaceRoot.clear();
    completedWorkspaceAnalysisKeys.clear();
    if (symbolAnalyzer) {
        symbolAnalyzer->setWorkspaceFileAnalysisBands({});
        symbolAnalyzer->cancelWorkspaceAnalysisAndInvalidate();
    }
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
    if (activeWorkspaceRoot != project.workspaceRoot) {
        if (!activeWorkspaceRoot.isEmpty())
            emit workspaceRelationshipAnalysisCancelRequested();
        activeWorkspaceRoot = project.workspaceRoot;
    }
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

    const bool completeWorkspaceAnalysis = activeWorkspaceAnalysisComplete;
    if (completeWorkspaceAnalysis)
        completedWorkspaceAnalysisKeys.insert(workspaceAnalysisKey(project));
    emit workspaceSymbolAnalysisFinished(project, filesAnalyzed, totalSymbols);
    if (completeWorkspaceAnalysis)
        emit workspaceRelationshipAnalysisRequested(project);
    activeWorkspaceAnalysisComplete = true;
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
    activeWorkspaceAnalysisComplete = true;
    if (hasPending)
        requestWorkspaceAnalysis(pendingProject);
}
