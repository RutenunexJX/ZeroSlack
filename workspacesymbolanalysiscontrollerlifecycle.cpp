#include "workspacesymbolanalysiscontroller.h"

#include "documentmodel.h"
#include "semanticindex.h"
#include "symbolanalyzer.h"

#include <QDir>
#include <QFileInfo>

#include <utility>

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
    QStringList includeDirs = project.includeDirs;
    includeDirs.sort(Qt::CaseInsensitive);
    QStringList fileExtensions = project.fileExtensions;
    fileExtensions.sort(Qt::CaseInsensitive);
    QStringList defineKeys = project.defines.keys();
    defineKeys.sort(Qt::CaseInsensitive);
    QStringList defineParts;
    defineParts.reserve(defineKeys.size());
    for (const QString& key : defineKeys) {
        defineParts.append(QStringLiteral("%1=%2")
                               .arg(key, project.defines.value(key)));
    }
    return QStringLiteral("%1\nfiles:%2\nincludes:%3\ndefines:%4\next:%5\ntop:%6")
        .arg(project.workspaceRoot,
             files.join(QLatin1Char('\n')),
             includeDirs.join(QLatin1Char('\n')),
             defineParts.join(QLatin1Char('\n')),
             fileExtensions.join(QLatin1Char('\n')),
             project.topModule);
}

QString workspaceRootIdentity(const QString& workspaceRoot)
{
#ifdef Q_OS_WIN
    return workspaceRoot.toCaseFolded();
#else
    return workspaceRoot;
#endif
}

QString workspaceAnalysisFileIdentity(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    QString identity = QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
#ifdef Q_OS_WIN
    identity = identity.toCaseFolded();
#endif
    return identity;
}

bool isExternalSystemVerilogOverlay(const QString& fileName)
{
    const QString suffix = QFileInfo(fileName).suffix();
    return suffix.compare(QStringLiteral("sv"), Qt::CaseInsensitive) == 0
        || suffix.compare(QStringLiteral("svh"), Qt::CaseInsensitive) == 0;
}
}

void WorkspaceSymbolAnalysisController::requestWorkspaceAnalysis(
    const ProjectSnapshot& project)
{
    if (!project.isOpen() || !symbolAnalyzer)
        return;

    QPointer<WorkspaceSymbolAnalysisController> self(this);
    const QPointer<SymbolAnalyzer> analyzer = symbolAnalyzer;

    if (project.systemVerilogFiles.isEmpty()) {
        workspaceAnalysisActive = false;
        activeRequestedProject = ProjectSnapshot();
        activeProject = ProjectSnapshot();
        ++workspaceStartGeneration;
        analyzer->setWorkspaceFileAnalysisBands({});
        if (self && analyzer)
            analyzer->cancelWorkspaceAnalysisAndInvalidate();
        return;
    }

    const QString projectKey = workspaceAnalysisKey(project);
    if (completedWorkspaceAnalysisKeys.contains(projectKey)) {
        workspaceAnalysisActive = false;
        activeRequestedProject = ProjectSnapshot();
        activeProject = ProjectSnapshot();
        activeWorkspaceAnalysisComplete = true;
        requestQueue.clear();
        ++workspaceStartGeneration;
        if (analyzer)
            analyzer->expireWorkspaceAnalysis();
        if (!self)
            return;
        emit workspaceRelationshipAnalysisCancelRequested();
        if (!self)
            return;
        emit diagnosticsRefreshRequested(QString());
        return;
    }

    if (requestQueue.active()) {
        requestQueue.queueLatest(project);
        ++workspaceStartGeneration;
        const WorkspaceAnalysisRequestTelemetry telemetry =
            requestQueue.telemetry();
        emit workspaceAnalysisRequestQueued(telemetry);
        if (!self || !analyzer || symbolAnalyzer != analyzer)
            return;
        analyzer->expireWorkspaceAnalysis();
        return;
    }

    startWorkspaceAnalysis(project);
}

void WorkspaceSymbolAnalysisController::startWorkspaceAnalysis(
    const ProjectSnapshot& project)
{
    const std::uint64_t startGeneration = ++workspaceStartGeneration;
    QPointer<WorkspaceSymbolAnalysisController> self(this);
    const QPointer<SymbolAnalyzer> analyzer = symbolAnalyzer;
    const QPointer<DocumentModel> documents = documentModel;
    if (!analyzer)
        return;

    WorkspaceAnalysisPlanQuery query;
    query.project = project;
    query.currentFileName = currentFileProvider ? currentFileProvider() : QString();
    if (!self || workspaceStartGeneration != startGeneration)
        return;
    query.openDocuments = documents
        ? documents->openDocuments()
        : QList<DocumentSnapshot>();
    if (!self || !analyzer || workspaceStartGeneration != startGeneration)
        return;
    const WorkspaceAnalysisPlan plan =
        WorkspaceAnalysisPlanService::getInstance()->planForWorkspace(query);
    const WorkspaceAutomaticAnalysisBudget budget =
        automaticAnalysisBudgetForPlan(project, plan);

    QList<OpenDocumentContent> openDocumentContents;
    if (documents) {
        openDocumentContents.reserve(query.openDocuments.size());
        for (const DocumentSnapshot& snapshot : query.openDocuments) {
            if (snapshot.fileName.isEmpty())
                continue;
            const QString content =
                documents->documentTextForFile(snapshot.fileName);
            if (!self || !documents
                || workspaceStartGeneration != startGeneration) {
                return;
            }
            if (content.isNull())
                continue;
            openDocumentContents.append(
                {snapshot.fileName,
                 content,
                 static_cast<std::uint64_t>(snapshot.textVersion)});
        }
    }

    // Open SV documents outside the automatic budget still participate in
    // this one immutable Slang transaction. They are deliberately not added
    // to activeProject: workspace completion keys, automatic budgeting, and
    // relationship analysis continue to describe only the real project.
    ProjectSnapshot analysisProject = budget.project;
    QSet<QString> analysisFileIdentities;
    for (const QString& fileName :
         std::as_const(analysisProject.systemVerilogFiles)) {
        analysisFileIdentities.insert(
            workspaceAnalysisFileIdentity(fileName));
    }
    for (const OpenDocumentContent& document :
         std::as_const(openDocumentContents)) {
        if (document.fileName.isEmpty()
            || document.content.isNull()
            || !isExternalSystemVerilogOverlay(document.fileName)) {
            continue;
        }
        const QString identity =
            workspaceAnalysisFileIdentity(document.fileName);
        if (identity.isEmpty() || analysisFileIdentities.contains(identity))
            continue;
        analysisFileIdentities.insert(identity);
        analysisProject.systemVerilogFiles.append(document.fileName);
    }

    analyzer->setWorkspaceFileAnalysisBands(
        semanticBandMetadataForPlan(plan));
    emit workspaceAnalysisPlanPrepared(plan);
    if (!self || !analyzer || symbolAnalyzer != analyzer
        || workspaceStartGeneration != startGeneration) {
        return;
    }

    if (budget.hasDeferredFiles()) {
        emit workspaceSymbolAnalysisDeferred(project,
                                             budget.profile.totalFiles,
                                             budget.profile.totalBytes,
                                             budget.profile.largestFileBytes);
        if (!self || !analyzer || symbolAnalyzer != analyzer
            || workspaceStartGeneration != startGeneration) {
            return;
        }
    }

    if (!budget.hasAutomaticFiles()) {
        requestQueue.clear();
        activeRequestedProject = ProjectSnapshot();
        activeProject = ProjectSnapshot();
        workspaceAnalysisActive = false;
        activeWorkspaceAnalysisComplete = false;
        emit diagnosticsRefreshRequested(QString());
        return;
    }

    requestQueue.start(project);
    activeRequestedProject = project;
    activeProject = budget.project;
    workspaceAnalysisActive = true;
    activeWorkspaceAnalysisComplete = !budget.hasDeferredFiles();
    emit diagnosticsRefreshRequested(QString());
    if (!self || !analyzer || symbolAnalyzer != analyzer
        || workspaceStartGeneration != startGeneration
        || !workspaceAnalysisActive
        || workspaceAnalysisKey(activeRequestedProject)
               != workspaceAnalysisKey(project)) {
        return;
    }
    emit workspaceSymbolAnalysisStarted(budget.project,
                                        budget.project.systemVerilogFiles.size());
    if (!self || !analyzer || symbolAnalyzer != analyzer
        || workspaceStartGeneration != startGeneration
        || !workspaceAnalysisActive
        || workspaceAnalysisKey(activeRequestedProject)
               != workspaceAnalysisKey(project)) {
        return;
    }
    // Worker cancellation is owned by SymbolAnalyzer's shared atomic token.
    // The UI provider remains a GUI-thread completion policy and is never
    // copied into QtConcurrent where its QObject owner could expire.
    analyzer->startAnalyzeProjectAsync(analysisProject,
                                       {},
                                       openDocumentContents);
}

void WorkspaceSymbolAnalysisController::cancelWorkspaceAnalysis()
{
    if (!workspaceAnalysisActive
        && !requestQueue.active()
        && !requestQueue.hasPending()) {
        return;
    }

    QPointer<WorkspaceSymbolAnalysisController> self(this);
    const QPointer<SymbolAnalyzer> analyzer = symbolAnalyzer;
    const WorkspaceAnalysisRequestTelemetry telemetry = requestQueue.cancel();
    ++workspaceStartGeneration;
    workspaceAnalysisActive = false;
    activeRequestedProject = ProjectSnapshot();
    activeProject = ProjectSnapshot();
    activeWorkspaceAnalysisComplete = true;
    emit workspaceSymbolAnalysisCancelled(telemetry);
    if (self && analyzer)
        analyzer->expireWorkspaceAnalysis();
}

void WorkspaceSymbolAnalysisController::clearProjectSemanticState()
{
    if (projectSemanticStateCleared)
        return;

    QPointer<WorkspaceSymbolAnalysisController> self(this);
    const QPointer<SymbolAnalyzer> analyzer = symbolAnalyzer;

    projectSemanticStateCleared = true;
    ++workspaceStartGeneration;
    workspaceAnalysisActive = false;
    activeWorkspaceAnalysisComplete = true;
    requestQueue.clear();
    activeRequestedProject = ProjectSnapshot();
    activeProject = ProjectSnapshot();
    activeWorkspaceRoot.clear();
    completedWorkspaceAnalysisKeys.clear();
    if (analyzer) {
        analyzer->setWorkspaceFileAnalysisBands({});
        analyzer->cancelWorkspaceAnalysisAndInvalidate();
    }
    if (!self)
        return;
    emit workspaceRelationshipAnalysisCancelRequested();
    if (!self)
        return;
    SemanticIndex::getInstance()->clearSemanticState();
    emit relationshipDataClearRequested();
    if (!self)
        return;
    emit diagnosticsRefreshRequested(QString());
}

void WorkspaceSymbolAnalysisController::onProjectChanged(
    const ProjectSnapshot& project)
{
    if (!project.isOpen()) {
        clearProjectSemanticState();
        return;
    }

    const bool replacingWorkspace =
        !activeWorkspaceRoot.isEmpty()
        && workspaceRootIdentity(activeWorkspaceRoot)
               != workspaceRootIdentity(project.workspaceRoot);
    if (replacingWorkspace)
        clearProjectSemanticState();

    projectSemanticStateCleared = false;
    activeWorkspaceRoot = project.workspaceRoot;
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
    const WorkspaceAnalysisRequestTelemetry telemetry =
        requestQueue.telemetry();
    workspaceAnalysisActive = false;
    activeRequestedProject = ProjectSnapshot();
    activeProject = ProjectSnapshot();
    QPointer<WorkspaceSymbolAnalysisController> self(this);
    emit workspaceAnalysisRequestResolved(telemetry);
    if (!self)
        return;
    if (hasPending) {
        requestWorkspaceAnalysis(pendingProject);
        return;
    }
    const bool cancelled = cancelProvider && cancelProvider();
    if (!self || cancelled)
        return;

    const bool completeWorkspaceAnalysis = activeWorkspaceAnalysisComplete;
    activeWorkspaceAnalysisComplete = true;
    if (completeWorkspaceAnalysis)
        completedWorkspaceAnalysisKeys.insert(workspaceAnalysisKey(project));
    emit workspaceSymbolAnalysisFinished(project, filesAnalyzed, totalSymbols);
    if (!self)
        return;
    if (completeWorkspaceAnalysis)
        emit workspaceRelationshipAnalysisRequested(project);
}

void WorkspaceSymbolAnalysisController::onWorkspaceSymbolAnalysisExpired()
{
    if (!workspaceAnalysisActive)
        return;

    ProjectSnapshot pendingProject;
    const bool hasPending =
        requestQueue.finishAndTakePending(&pendingProject);
    const WorkspaceAnalysisRequestTelemetry telemetry =
        requestQueue.telemetry();
    workspaceAnalysisActive = false;
    activeRequestedProject = ProjectSnapshot();
    activeProject = ProjectSnapshot();
    activeWorkspaceAnalysisComplete = true;
    QPointer<WorkspaceSymbolAnalysisController> self(this);
    emit workspaceAnalysisRequestResolved(telemetry);
    if (!self)
        return;
    if (hasPending)
        requestWorkspaceAnalysis(pendingProject);
}
