#include "analysisscheduler.h"

#include "activitylogservice.h"
#include "semanticindex.h"
#include "symbolanalyzer.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTextStream>

#include <algorithm>
#include <utility>

namespace {
QString projectAnalysisSignature(const ProjectSnapshot& project)
{
    QStringList files = project.systemVerilogFiles;
    QStringList includeDirs = project.includeDirs;
    QStringList extensions = project.fileExtensions;
    QStringList defineKeys = project.defines.keys();
    files.sort(Qt::CaseInsensitive);
    includeDirs.sort(Qt::CaseInsensitive);
    extensions.sort(Qt::CaseInsensitive);
    defineKeys.sort(Qt::CaseInsensitive);
    QStringList defines;
    for (const QString& key : defineKeys)
        defines.append(key + QLatin1Char('=') + project.defines.value(key));
    return QStringLiteral("%1\n%2\n%3\n%4\n%5\n%6")
        .arg(project.workspaceRoot,
             files.join(QLatin1Char('\n')),
             includeDirs.join(QLatin1Char('\n')),
             defines.join(QLatin1Char('\n')),
             extensions.join(QLatin1Char('\n')),
             project.topModule);
}

bool isSystemVerilogSource(const QString& fileName)
{
    const QString suffix = QFileInfo(fileName).suffix().toCaseFolded();
    return suffix == QStringLiteral("sv")
        || suffix == QStringLiteral("svh")
        || suffix == QStringLiteral("v")
        || suffix == QStringLiteral("vh");
}

QHash<QString, SemanticAnalysisBandMetadata> analysisBandsForPlan(
    const WorkspaceAnalysisPlan& plan)
{
    QHash<QString, SemanticAnalysisBandMetadata> bands;
    for (auto it = plan.fileBandMetadataByNormalizedPath.constBegin();
         it != plan.fileBandMetadataByNormalizedPath.constEnd(); ++it) {
        if (!it->isValid())
            continue;
        SemanticAnalysisBandMetadata metadata;
        metadata.label = it->label;
        metadata.displayName = it->displayName;
        metadata.priority = it->priority;
        metadata.publicationCheckpoint = it->publicationCheckpoint;
        bands.insert(it.key(), metadata);
    }
    return bands;
}
}

void AnalysisScheduler::setDocumentModel(DocumentModel* model)
{
    if (documentModel == model)
        return;
    if (documentModel)
        disconnect(documentModel, nullptr, this, nullptr);

    documentModel = model;
    if (workspaceSymbolAnalysis)
        workspaceSymbolAnalysis->setDocumentModel(model);
    if (!documentModel)
        return;

    connect(documentModel,
            &DocumentModel::documentOpened,
            this,
            &AnalysisScheduler::onDocumentOpened);
    connect(documentModel,
            &DocumentModel::documentEdited,
            this,
            &AnalysisScheduler::onDocumentEdited);
    connect(documentModel,
            &DocumentModel::documentSaved,
            this,
            &AnalysisScheduler::onDocumentSaved);
    connect(documentModel,
            &DocumentModel::documentClosed,
            this,
            [this](const QString&, const QString& fileName) {
                handleDocumentClosed(fileName);
            });
}

QString AnalysisScheduler::normalizedFileName(const QString& fileName) const
{
    if (fileName.isEmpty())
        return QString();
    QString path = QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
#ifdef Q_OS_WIN
    path = path.toCaseFolded();
#endif
    return path;
}

bool AnalysisScheduler::requestUsesCurrentDocumentText(
    const SemanticAnalysisRequest& request,
    const DocumentSnapshot& snapshot) const
{
    if (snapshot.fileName.isEmpty())
        return false;
    const QString target = normalizedFileName(snapshot.fileName);
    bool revisionMatches = false;
    for (auto it = request.documentRevisions.constBegin();
         it != request.documentRevisions.constEnd(); ++it) {
        if (normalizedFileName(it.key()) != target)
            continue;
        revisionMatches = it.value()
            == static_cast<std::uint64_t>(snapshot.textVersion);
        break;
    }
    if (!revisionMatches)
        return false;

    for (auto it = request.sourceOverrides.constBegin();
         it != request.sourceOverrides.constEnd(); ++it) {
        if (normalizedFileName(it.key()) == target)
            return it.value() == snapshot.text;
    }
    return false;
}

DocumentSemanticStatus AnalysisScheduler::semanticStatus(
    const QString& fileName) const
{
    const QString key = normalizedFileName(fileName);
    DocumentSemanticStatus status = semanticStatuses.value(key);
    if (status.fileName.isEmpty())
        status.fileName = fileName;
    return status;
}

void AnalysisScheduler::setDocumentSemanticState(
    const QString& fileName,
    DocumentSemanticState state,
    std::uint64_t documentRevision,
    std::uint64_t analysisGeneration,
    const QString& error)
{
    const QString key = normalizedFileName(fileName);
    if (key.isEmpty())
        return;
    DocumentSemanticStatus status;
    status.fileName = fileName;
    status.state = state;
    status.documentRevision = documentRevision;
    status.analysisGeneration = analysisGeneration;
    status.error = error;
    semanticStatuses.insert(key, status);
    emit documentSemanticStateChanged(status);
}

ProjectSnapshot AnalysisScheduler::projectForAnalysis(
    const QString& triggerFile) const
{
    ProjectSnapshot project = projectModel && projectModel->isOpen()
        ? projectModel->snapshot()
        : ProjectSnapshot();
    if (!project.isOpen() && !triggerFile.isEmpty()) {
        const QFileInfo info(triggerFile);
        project.workspaceRoot = info.absolutePath();
        project.allFiles = {info.absoluteFilePath()};
        project.systemVerilogFiles = {info.absoluteFilePath()};
        project.includeDirs = {info.absolutePath()};
        project.fileExtensions = {
            QStringLiteral("sv"),
            QStringLiteral("svh"),
            QStringLiteral("v"),
            QStringLiteral("vh")};
    }
    if (!triggerFile.isEmpty()
        && isSystemVerilogSource(triggerFile)
        && !project.systemVerilogFiles.contains(triggerFile,
                                                Qt::CaseInsensitive)) {
        project.systemVerilogFiles.append(triggerFile);
        project.allFiles.append(triggerFile);
    }
    return project;
}

void AnalysisScheduler::requestSemanticAnalysis(
    SemanticAnalysisReason reason,
    SemanticChangeImpact impactHint,
    const QString& triggerFile,
    const QStringList& changedFiles,
    const ProjectSnapshot& requestedProject,
    const QHash<QString, QString>& requestedSourceOverrides)
{
    if (shuttingDown || !workspaceSymbolAnalysis)
        return;
    const ProjectSnapshot project = requestedProject.isOpen()
        ? requestedProject
        : projectForAnalysis(triggerFile);
    if (!project.isOpen() || project.systemVerilogFiles.isEmpty())
        return;

    if (!semanticRuntimePolicy.enabled) {
        stabilizeSemanticStatesWhenDisabled();
        SemanticAnalysisTelemetry telemetry;
        telemetry.stage = SemanticAnalysisStage::Scheduling;
        telemetry.reason = reason;
        telemetry.impact = impactHint;
        telemetry.files = project.systemVerilogFiles;
        telemetry.changedFiles = changedFiles;
        telemetry.detail = QStringLiteral(
            "suppressed reason=%1 policy=disabled")
                               .arg(semanticAnalysisReasonName(reason));
        emit semanticAnalysisTelemetry(telemetry);
        return;
    }

    if (reason == SemanticAnalysisReason::WorkspaceOpen
        || reason == SemanticAnalysisReason::WorkspaceConfiguration
        || reason == SemanticAnalysisReason::ExplicitRequest) {
        WorkspaceAnalysisPlanQuery query;
        query.project = project;
        query.currentFileName = currentFileProvider
            ? currentFileProvider()
            : QString();
        query.openDocuments = documentModel
            ? documentModel->cachedOpenDocuments()
            : QList<DocumentSnapshot>();
        const WorkspaceAnalysisPlan workspacePlan =
            WorkspaceAnalysisPlanService::getInstance()
                ->planForWorkspace(query);
        if (symbolAnalyzer) {
            symbolAnalyzer->setWorkspaceFileAnalysisBands(
                analysisBandsForPlan(workspacePlan));
        }
        emit workspaceAnalysisPlanPrepared(workspacePlan);
    }

    SemanticAnalysisRequest request;
    request.generation = ++nextSemanticGeneration;
    request.reason = reason;
    request.impactHint = impactHint;
    request.project = project;
    request.triggerFile = triggerFile;
    request.changedFiles = changedFiles.isEmpty()
        ? project.systemVerilogFiles
        : changedFiles;
    if (pendingCleanSemanticChanges.size() == 1) {
        const auto pending = pendingCleanSemanticChanges.constBegin();
        bool alreadyRequested = false;
        for (const QString& fileName : std::as_const(request.changedFiles)) {
            if (normalizedFileName(fileName) == pending.key()) {
                alreadyRequested = true;
                break;
            }
        }
        if (!alreadyRequested) {
            bool belongsToProject = false;
            for (const QString& fileName : project.systemVerilogFiles) {
                if (normalizedFileName(fileName) == pending.key()) {
                    belongsToProject = true;
                    break;
                }
            }
            const DocumentSnapshot snapshot = belongsToProject && documentModel
                ? documentModel->cachedDocumentForFile(pending->fileName)
                : DocumentSnapshot();
            if (belongsToProject
                && (snapshot.fileName.isEmpty() || !snapshot.dirty)) {
                request.changedFiles.append(pending->fileName);
            }
        }
    } else if (pendingCleanSemanticChanges.size() > 1) {
        QSet<QString> projectFiles;
        projectFiles.reserve(project.systemVerilogFiles.size());
        for (const QString& fileName : project.systemVerilogFiles)
            projectFiles.insert(normalizedFileName(fileName));
        QSet<QString> requestedFiles;
        requestedFiles.reserve(request.changedFiles.size());
        for (const QString& fileName : std::as_const(request.changedFiles))
            requestedFiles.insert(normalizedFileName(fileName));
        for (auto it = pendingCleanSemanticChanges.constBegin();
             it != pendingCleanSemanticChanges.constEnd(); ++it) {
            if (!projectFiles.contains(it.key())
                || requestedFiles.contains(it.key())) {
                continue;
            }
            const DocumentSnapshot snapshot = documentModel
                ? documentModel->cachedDocumentForFile(it.value().fileName)
                : DocumentSnapshot();
            if (!snapshot.fileName.isEmpty() && snapshot.dirty)
                continue;
            request.changedFiles.append(it.value().fileName);
            requestedFiles.insert(it.key());
        }
    }
    request.expectedSnapshotRevision =
        SemanticIndex::getInstance()->snapshotRevision();
    request.runtimePolicy = semanticRuntimePolicy;

    if (documentModel) {
        for (const DocumentSnapshot& snapshot :
             documentModel->cachedOpenDocuments()) {
            if (snapshot.fileName.isEmpty())
                continue;
            request.documentRevisions.insert(
                snapshot.fileName,
                static_cast<std::uint64_t>(snapshot.textVersion));
        }
    }
    for (const QString& fileName : std::as_const(request.changedFiles)) {
        const auto pending = pendingCleanSemanticChanges.constFind(
            normalizedFileName(fileName));
        if (pending != pendingCleanSemanticChanges.constEnd()) {
            request.sourceOverrides.insert(fileName, pending->text);
        }
    }
    if (!triggerFile.isEmpty()
        && (reason == SemanticAnalysisReason::Save
            || reason == SemanticAnalysisReason::DocumentOpen)
        && !request.sourceOverrides.contains(triggerFile)) {
        const QString content = contentForOpenFile(triggerFile);
        if (!content.isNull())
            request.sourceOverrides.insert(triggerFile, content);
    }
    for (auto it = requestedSourceOverrides.constBegin();
         it != requestedSourceOverrides.constEnd(); ++it) {
        request.sourceOverrides.insert(it.key(), it.value());
    }

    for (const QString& fileName : request.changedFiles) {
        const DocumentSnapshot snapshot = documentModel
            ? documentModel->cachedDocumentForFile(fileName)
            : DocumentSnapshot();
        if (!snapshot.fileName.isEmpty()) {
            const DocumentSemanticStatus current = semanticStatus(fileName);
            if (current.analysisGeneration > request.generation)
                continue;
            setDocumentSemanticState(
                fileName,
                snapshot.dirty
                        && !requestUsesCurrentDocumentText(request, snapshot)
                    ? DocumentSemanticState::Dirty
                    : DocumentSemanticState::Queued,
                static_cast<std::uint64_t>(snapshot.textVersion),
                request.generation);
        }
    }

    SemanticAnalysisTelemetry telemetry;
    telemetry.generation = request.generation;
    telemetry.stage = SemanticAnalysisStage::Scheduling;
    telemetry.reason = reason;
    telemetry.impact = impactHint;
    telemetry.files = project.systemVerilogFiles;
    telemetry.changedFiles = request.changedFiles;
    telemetry.detail = QStringLiteral("queued reason=%1 impact=%2 files=%3")
                           .arg(semanticAnalysisReasonName(reason),
                                semanticChangeImpactName(impactHint))
                           .arg(project.systemVerilogFiles.size());
    ActivityLogService::getInstance()->append(
        QStringLiteral("Semantic analysis"),
        ActivityLogLevel::Info,
        QStringLiteral("%1 generation=%2 changedFiles=%3")
            .arg(telemetry.detail)
            .arg(request.generation)
            .arg(request.changedFiles.join(',')));
    emit semanticAnalysisTelemetry(telemetry);
    workspaceSymbolAnalysis->requestSemanticAnalysis(request);
}

bool AnalysisScheduler::isSelfWriteWatcherEvent(const QString& fileName) const
{
    const SelfWriteStamp stamp =
        selfWriteStamps.value(normalizedFileName(fileName));
    if (stamp.recordedMs < 0
        || QDateTime::currentMSecsSinceEpoch() - stamp.recordedMs > 5000) {
        return false;
    }
    const QFileInfo info(fileName);
    return info.exists()
        && info.size() == stamp.size
        && info.lastModified().toMSecsSinceEpoch() == stamp.modifiedMs;
}

void AnalysisScheduler::handleExternalFileChanged(const QString& fileName,
                                                  int debounceMs)
{
    if (fileName.isEmpty() || shuttingDown)
        return;
    const DocumentSnapshot snapshot = documentModel
        ? documentModel->cachedDocumentForFile(fileName)
        : DocumentSnapshot();
    if ((!snapshot.fileName.isEmpty() && snapshot.dirty)
        || isSelfWriteWatcherEvent(fileName)) {
        return;
    }
    scheduleExternalFileAnalysis(fileName, debounceMs);
}

void AnalysisScheduler::scheduleExternalFileAnalysis(const QString& fileName,
                                                     int debounceMs)
{
    const QString key = normalizedFileName(fileName);
    QTimer* timer = externalFileTimers.value(key, nullptr);
    if (!timer) {
        timer = new QTimer(this);
        timer->setSingleShot(true);
        externalFileTimers.insert(key, timer);
        connect(timer, &QTimer::timeout, this, [this, fileName]() {
            const DocumentSnapshot snapshot = documentModel
                ? documentModel->cachedDocumentForFile(fileName)
                : DocumentSnapshot();
            if ((!snapshot.fileName.isEmpty() && snapshot.dirty)
                || isSelfWriteWatcherEvent(fileName)) {
                return;
            }
            QFile file(fileName);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                rememberPendingCleanSemanticChange(
                    fileName,
                    QTextStream(&file).readAll(),
                    snapshot.fileName.isEmpty()
                        ? 0
                        : static_cast<std::uint64_t>(snapshot.textVersion),
                    SemanticAnalysisReason::ExternalFileChange);
            }
            requestSemanticAnalysis(
                SemanticAnalysisReason::ExternalFileChange,
                SemanticChangeImpact::Unknown,
                fileName,
                {fileName});
        });
    }
    timer->start(qMax(0, debounceMs));
}

void AnalysisScheduler::handleDocumentClosed(const QString& fileName)
{
    const QString key = normalizedFileName(fileName);
    if (QTimer* timer = externalFileTimers.take(key))
        timer->deleteLater();
    semanticStatuses.remove(key);
    selfWriteStamps.remove(key);
    if (relationshipAnalysisQueue)
        relationshipAnalysisQueue->clearFile(fileName);
}

void AnalysisScheduler::onDocumentOpened(const DocumentSnapshot& snapshot)
{
    ActivityLogService::getInstance()->append(
        QStringLiteral("Editor"),
        ActivityLogLevel::Info,
        QStringLiteral("Opened %1")
            .arg(QFileInfo(snapshot.fileName).fileName()));
    if (snapshot.fileName.isEmpty())
        return;
    if (snapshot.dirty) {
        setDocumentSemanticState(
            snapshot.fileName,
            DocumentSemanticState::Dirty,
            static_cast<std::uint64_t>(snapshot.textVersion));
        return;
    }

    const QString indexedText =
        SemanticIndex::getInstance()->getCachedFileContent(snapshot.fileName);
    if (!indexedText.isNull() && indexedText == snapshot.text) {
        pendingCleanSemanticChanges.remove(
            normalizedFileName(snapshot.fileName));
        setDocumentSemanticState(
            snapshot.fileName,
            DocumentSemanticState::Current,
            static_cast<std::uint64_t>(snapshot.textVersion));
        return;
    }
    setDocumentSemanticState(
        snapshot.fileName,
        DocumentSemanticState::Stale,
        static_cast<std::uint64_t>(snapshot.textVersion));
    rememberPendingCleanSemanticChange(
        snapshot.fileName,
        snapshot.text,
        static_cast<std::uint64_t>(snapshot.textVersion),
        SemanticAnalysisReason::DocumentOpen);
    if (indexedText.isNull() && workspaceSymbolAnalysis) {
        const std::uint64_t activeGeneration =
            workspaceSymbolAnalysis->activeSemanticGenerationForFile(
                snapshot.fileName);
        if (activeGeneration > 0) {
            setDocumentSemanticState(
                snapshot.fileName,
                DocumentSemanticState::Queued,
                static_cast<std::uint64_t>(snapshot.textVersion),
                activeGeneration);
            return;
        }
    }
    requestSemanticAnalysis(SemanticAnalysisReason::DocumentOpen,
                            SemanticChangeImpact::Unknown,
                            snapshot.fileName,
                            {snapshot.fileName});
}

void AnalysisScheduler::onDocumentEdited(const DocumentSnapshot& snapshot)
{
    pendingCleanSemanticChanges.remove(normalizedFileName(snapshot.fileName));
    if (relationshipAnalysisQueue)
        relationshipAnalysisQueue->clearFile(snapshot.fileName);
    if (workspaceSymbolAnalysis) {
        workspaceSymbolAnalysis->invalidateSemanticAnalysis(
            snapshot.fileName,
            static_cast<std::uint64_t>(snapshot.textVersion));
    }
    setDocumentSemanticState(
        snapshot.fileName,
        DocumentSemanticState::Dirty,
        static_cast<std::uint64_t>(snapshot.textVersion));
}

void AnalysisScheduler::onDocumentSaved(const DocumentSnapshot& snapshot)
{
    const QFileInfo info(snapshot.fileName);
    SelfWriteStamp stamp;
    stamp.size = info.size();
    stamp.modifiedMs = info.lastModified().toMSecsSinceEpoch();
    stamp.recordedMs = QDateTime::currentMSecsSinceEpoch();
    selfWriteStamps.insert(normalizedFileName(snapshot.fileName), stamp);
    const QString indexedText =
        SemanticIndex::getInstance()->getCachedFileContent(snapshot.fileName);
    if (!indexedText.isNull() && indexedText == snapshot.text) {
        pendingCleanSemanticChanges.remove(
            normalizedFileName(snapshot.fileName));
        setDocumentSemanticState(
            snapshot.fileName,
            DocumentSemanticState::Current,
            static_cast<std::uint64_t>(snapshot.textVersion));
        return;
    }
    rememberPendingCleanSemanticChange(
        snapshot.fileName,
        snapshot.text,
        static_cast<std::uint64_t>(snapshot.textVersion),
        SemanticAnalysisReason::Save);
    requestSemanticAnalysis(SemanticAnalysisReason::Save,
                            SemanticChangeImpact::Unknown,
                            snapshot.fileName,
                            {snapshot.fileName});
}

void AnalysisScheduler::onProjectChanged(const ProjectSnapshot& project)
{
    if (!project.isOpen()) {
        onProjectClosed();
        return;
    }
    const QString signature = projectAnalysisSignature(project);
    if (signature == lastProjectSignature)
        return;

    const bool replacingWorkspace = lastScheduledProject.isOpen()
        && normalizedFileName(lastScheduledProject.workspaceRoot)
               != normalizedFileName(project.workspaceRoot);
    if (replacingWorkspace && workspaceSymbolAnalysis) {
        pendingCleanSemanticChanges.clear();
        workspaceSymbolAnalysis->clearProjectSemanticState();
        workspaceInitialAnalysisScheduled = false;
    }
    lastProjectSignature = signature;
    lastScheduledProject = project;
    if (project.systemVerilogFiles.isEmpty())
        return;

    const SemanticAnalysisReason reason = workspaceInitialAnalysisScheduled
        ? SemanticAnalysisReason::WorkspaceConfiguration
        : SemanticAnalysisReason::WorkspaceOpen;
    workspaceInitialAnalysisScheduled = true;
    requestSemanticAnalysis(reason,
                            SemanticChangeImpact::WorkspaceConfig,
                            QString(),
                            project.systemVerilogFiles,
                            project);
}

void AnalysisScheduler::onProjectClosed()
{
    if (!lastScheduledProject.isOpen()
        && lastProjectSignature.isEmpty()
        && !workspaceInitialAnalysisScheduled) {
        return;
    }
    lastScheduledProject = ProjectSnapshot();
    lastProjectSignature.clear();
    workspaceInitialAnalysisScheduled = false;
    pendingCleanSemanticChanges.clear();
    if (workspaceSymbolAnalysis)
        workspaceSymbolAnalysis->clearProjectSemanticState();
    if (documentModel) {
        for (const DocumentSnapshot& snapshot :
             documentModel->cachedOpenDocuments()) {
            setDocumentSemanticState(
                snapshot.fileName,
                DocumentSemanticState::Dirty,
                static_cast<std::uint64_t>(snapshot.textVersion));
        }
    }
}

void AnalysisScheduler::onSemanticAnalysisStarted(
    const SemanticAnalysisRequest& request)
{
    for (const QString& fileName : request.changedFiles) {
        const DocumentSnapshot snapshot = documentModel
            ? documentModel->cachedDocumentForFile(fileName)
            : DocumentSnapshot();
        if (!snapshot.fileName.isEmpty()) {
            const DocumentSemanticStatus current = semanticStatus(fileName);
            if (current.analysisGeneration > request.generation)
                continue;
            setDocumentSemanticState(
                fileName,
                snapshot.dirty
                        && !requestUsesCurrentDocumentText(request, snapshot)
                    ? DocumentSemanticState::Dirty
                    : DocumentSemanticState::Analyzing,
                static_cast<std::uint64_t>(snapshot.textVersion),
                request.generation);
        }
    }
}

void AnalysisScheduler::onSemanticAnalysisFinished(
    const SemanticAnalysisRequest& request,
    const IncrementalAnalysisPlan& plan)
{
    acknowledgePublishedCleanSemanticChanges(request);
    if (plan.impact == SemanticChangeImpact::TriviaOnly) {
        for (const QString& fileName : plan.affectedFiles)
            emit documentRefreshRequested(fileName);
    }
    auto requestedRevisionForFile = [&request, this](const QString& fileName,
                                                      bool* found) {
        if (found)
            *found = false;
        const QString target = normalizedFileName(fileName);
        for (auto it = request.documentRevisions.constBegin();
             it != request.documentRevisions.constEnd(); ++it) {
            if (normalizedFileName(it.key()) == target) {
                if (found)
                    *found = true;
                return it.value();
            }
        }
        return std::uint64_t{0};
    };
    for (const QString& fileName : plan.affectedFiles) {
        const DocumentSnapshot snapshot = documentModel
            ? documentModel->cachedDocumentForFile(fileName)
            : DocumentSnapshot();
        if (snapshot.fileName.isEmpty())
            continue;
        const DocumentSemanticStatus current = semanticStatus(fileName);
        if (current.analysisGeneration > request.generation)
            continue;
        const QString publishedText =
            SemanticIndex::getInstance()->getCachedFileContent(fileName);
        const bool dirtyBufferPublished = snapshot.dirty
            && requestUsesCurrentDocumentText(request, snapshot)
            && !publishedText.isNull()
            && publishedText == snapshot.text;
        if (snapshot.dirty && !dirtyBufferPublished) {
            setDocumentSemanticState(
                fileName,
                DocumentSemanticState::Dirty,
                static_cast<std::uint64_t>(snapshot.textVersion),
                request.generation);
            continue;
        }
        bool revisionCaptured = false;
        const std::uint64_t requestedRevision =
            requestedRevisionForFile(fileName, &revisionCaptured);
        const bool capturedRevisionMatches = revisionCaptured
            && static_cast<std::uint64_t>(snapshot.textVersion)
                == requestedRevision;
        const bool openedDuringActiveRequest = !revisionCaptured
            && current.analysisGeneration == request.generation
            && current.state == DocumentSemanticState::Queued
            && current.documentRevision
                == static_cast<std::uint64_t>(snapshot.textVersion);
        if ((dirtyBufferPublished
             || capturedRevisionMatches
             || openedDuringActiveRequest)
            && !publishedText.isNull()
            && publishedText == snapshot.text) {
            setDocumentSemanticState(
                fileName,
                DocumentSemanticState::Current,
                capturedRevisionMatches
                    ? requestedRevision
                    : static_cast<std::uint64_t>(snapshot.textVersion),
                request.generation);
        } else {
            setDocumentSemanticState(
                fileName,
                DocumentSemanticState::Stale,
                static_cast<std::uint64_t>(snapshot.textVersion),
                request.generation);
        }
    }
}

void AnalysisScheduler::onSemanticAnalysisFailed(
    const SemanticAnalysisRequest& request,
    const QString& error)
{
    for (const QString& fileName : request.changedFiles) {
        const DocumentSnapshot snapshot = documentModel
            ? documentModel->cachedDocumentForFile(fileName)
            : DocumentSnapshot();
        const DocumentSemanticStatus current = semanticStatus(fileName);
        if (!snapshot.fileName.isEmpty() && !snapshot.dirty
            && current.analysisGeneration <= request.generation) {
            setDocumentSemanticState(
                fileName,
                DocumentSemanticState::Failed,
                static_cast<std::uint64_t>(snapshot.textVersion),
                request.generation,
                error);
        }
    }
}

void AnalysisScheduler::onSemanticAnalysisDropped(
    const SemanticAnalysisRequest& request,
    SemanticAnalysisRequestDisposition disposition)
{
    Q_UNUSED(disposition)
    for (const QString& fileName : request.changedFiles) {
        const DocumentSemanticStatus current = semanticStatus(fileName);
        if (current.analysisGeneration != request.generation)
            continue;
        const DocumentSnapshot snapshot = documentModel
            ? documentModel->cachedDocumentForFile(fileName)
            : DocumentSnapshot();
        if (snapshot.fileName.isEmpty())
            continue;
        if (snapshot.dirty) {
            setDocumentSemanticState(
                fileName,
                DocumentSemanticState::Dirty,
                static_cast<std::uint64_t>(snapshot.textVersion),
                request.generation);
            continue;
        }
        const QString indexedText =
            SemanticIndex::getInstance()->getCachedFileContent(fileName);
        setDocumentSemanticState(
            fileName,
            !indexedText.isNull() && indexedText == snapshot.text
                ? DocumentSemanticState::Current
                : DocumentSemanticState::Stale,
            static_cast<std::uint64_t>(snapshot.textVersion),
            request.generation);
    }
}

QString AnalysisScheduler::contentForOpenFile(const QString& fileName) const
{
    if (openFileContentProvider)
        return openFileContentProvider(fileName);
    return documentModel
        ? documentModel->documentTextForFile(fileName)
        : QString();
}

void AnalysisScheduler::rememberPendingCleanSemanticChange(
    const QString& fileName,
    const QString& text,
    std::uint64_t documentRevision,
    SemanticAnalysisReason reason)
{
    const QString key = normalizedFileName(fileName);
    if (key.isEmpty())
        return;
    PendingCleanSemanticChange change;
    change.fileName = fileName;
    change.text = text;
    change.documentRevision = documentRevision;
    change.reason = reason;
    pendingCleanSemanticChanges.insert(key, std::move(change));
}

void AnalysisScheduler::acknowledgePublishedCleanSemanticChanges(
    const SemanticAnalysisRequest& request)
{
    for (const QString& fileName : request.changedFiles) {
        const QString key = normalizedFileName(fileName);
        auto pending = pendingCleanSemanticChanges.find(key);
        if (pending == pendingCleanSemanticChanges.end())
            continue;
        const QString publishedText =
            SemanticIndex::getInstance()->getCachedFileContent(fileName);
        if (!publishedText.isNull() && publishedText == pending->text)
            pendingCleanSemanticChanges.erase(pending);
    }
}

void AnalysisScheduler::stabilizeSemanticStatesWhenDisabled()
{
    if (!documentModel)
        return;
    for (const DocumentSnapshot& snapshot :
         documentModel->cachedOpenDocuments()) {
        const DocumentSemanticStatus current =
            semanticStatus(snapshot.fileName);
        if (snapshot.dirty) {
            setDocumentSemanticState(
                snapshot.fileName,
                DocumentSemanticState::Dirty,
                static_cast<std::uint64_t>(snapshot.textVersion),
                current.analysisGeneration);
            continue;
        }
        const QString indexedText =
            SemanticIndex::getInstance()->getCachedFileContent(
                snapshot.fileName);
        setDocumentSemanticState(
            snapshot.fileName,
            !indexedText.isNull() && indexedText == snapshot.text
                ? DocumentSemanticState::Current
                : DocumentSemanticState::Stale,
            static_cast<std::uint64_t>(snapshot.textVersion),
            current.analysisGeneration);
    }
}
