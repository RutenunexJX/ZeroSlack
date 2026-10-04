#include "symbolanalyzer.h"

#include "incrementalsemanticanalysisworker.h"
#include "semanticindexsnapshot.h"
#include "relationshipanalysisworker.h"

#include <QtConcurrent/QtConcurrent>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>

namespace {
QString normalizedIncrementalBandFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    QString normalized = QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
#ifdef Q_OS_WIN
    normalized = normalized.toCaseFolded();
#endif
    return normalized;
}

SemanticAnalysisBandReport preparedBandReport(
    const std::shared_ptr<const SemanticIndexSnapshot>& snapshot,
    const QHash<QString, SemanticAnalysisBandMetadata>& bands)
{
    if (!snapshot)
        return {};
    QList<SemanticSymbolRecord> fileRepresentatives;
    for (const QString& file : snapshot->symbolFiles()) {
        if (snapshot->symbolRecordCount(file) == 0)
            continue;
        SemanticSymbolRecord representative;
        representative.location.fileName = file;
        representative.analysisBand = bands.value(normalizedIncrementalBandFileName(file));
        fileRepresentatives.append(std::move(representative));
    }
    auto report = semanticAnalysisBandReportForRecords(fileRepresentatives);
    report.totalSymbolCount = snapshot->symbolRecordCount();
    for (auto& band : report.bands) {
        band.symbolCount = 0;
        for (const QString& file : band.files)
            band.symbolCount += snapshot->symbolRecordCount(file);
    }
    return report;
}
}

void SymbolAnalyzer::startSemanticAnalysisAsync(
    const SemanticAnalysisRequest& originalRequest,
    std::function<bool()> externalCancellation)
{
    if (shutdownStarted
        || !originalRequest.isValid()
        || !originalRequest.runtimePolicy.normalized().enabled) {
        if (originalRequest.isValid())
            emit semanticAnalysisDropped(originalRequest, SemanticAnalysisRequestDisposition::Cancelled);
        return;
    }

    // The controller serializes requests. A legacy overlap is expired without
    // joining on the GUI thread; generation checks reject its result.
    if (hasWorkspaceAnalysisInFlight()) {
        const auto superseded = pendingCompatibilityRequest;
        pendingCompatibilityRequest = originalRequest;
        pendingCompatibilityCancellation = std::move(externalCancellation);
        expireWorkspaceAnalysis();
        if (superseded)
            emit semanticAnalysisDropped(*superseded, SemanticAnalysisRequestDisposition::Superseded);
        return;
    }

    pendingCompatibilityRequest.reset();
    pendingCompatibilityCancellation = {};
    SemanticAnalysisRequest request = originalRequest;
    const std::uint64_t deliveryGeneration = ++workspaceAnalysisGeneration;
    const std::uint64_t epoch = workspaceEpoch;
    const auto cancellation = std::make_shared<std::atomic_bool>(false);
    workspaceAnalysisCancelFlag = cancellation;
    if (request.project.isOpen()) {
        overlayProject = request.project;
    }

    const QString workspaceKey = SemanticInputCapture::pathKey(request.project.workspaceRoot);
    const QString scopeKey = request.project.isOpen() ? workspaceKey
        : QStringLiteral("document:") + SemanticInputCapture::pathKey(request.triggerFile);
    auto retained = !request.project.isOpen() ? retainedWorkspaces.value(scopeKey)
        : request.reason == SemanticAnalysisReason::WorkspaceOpen
            ? retainedWorkspaces.value(workspaceKey) : activeWorkspaceState;
    const auto basePublication = SemanticIndex::getInstance()->snapshotToken();
    const auto visibleSnapshot = basePublication.snapshot;
    if (retained && retained->scopeKey != scopeKey)
        retained.reset();
    const auto baseSnapshot = retained && request.reason == SemanticAnalysisReason::WorkspaceOpen
        ? retained->snapshot : visibleSnapshot;
    if (!retained && baseSnapshot) {
        auto initial = std::make_shared<PublishedWorkspaceSemanticState>();
        initial->scopeKey = scopeKey;
        initial->snapshot = baseSnapshot;
        initial->facts = EffectiveValueService::getInstance()->capturePublishedFacts();
        retained = std::move(initial);
    }
    const std::uint64_t expectedSnapshotRevision =
        request.expectedSnapshotRevision > 0
            ? request.expectedSnapshotRevision
            : SemanticIndex::getInstance()->snapshotRevision();
    request.expectedSnapshotRevision = expectedSnapshotRevision;
    const SemanticDependencyGraph graph = retained ? retained->dependencyGraph : SemanticDependencyGraph{};
    const QHash<QString, SemanticAnalysisBandMetadata> fileAnalysisBands =
        workspaceFileAnalysisBands;
    const WorkspaceWorkerStartGateForTesting workerStartGate =
        workspaceWorkerStartGateForTesting;
    const QString workspacePath = request.project.workspaceRoot.isEmpty()
        ? request.triggerFile
        : request.project.workspaceRoot;

    QFuture<WorkspaceAnalysisResult> future = QtConcurrent::run(
        &semanticAnalysisThreadPool,
        [request,
         baseSnapshot,
         retained,
         graph,
         fileAnalysisBands,
         cancellation,
         externalCancellation,
         workerStartGate,
         basePublication,
         deliveryGeneration,
         scopeKey,
         epoch]() {
            auto cancelled = [&]() {
                return cancellation->load(std::memory_order_relaxed)
                    || (externalCancellation && externalCancellation());
            };
            if (workerStartGate)
                workerStartGate(cancelled);
            WorkspaceAnalysisResult result =
                IncrementalSemanticAnalysisWorker::analyze(
                request, baseSnapshot, graph, cancelled, retained ? retained->input : nullptr, retained);
            result.basePublication = basePublication;
            result.deliveryGeneration = deliveryGeneration;
            result.workspaceEpoch = epoch;
            result.publicationProjectIdentity = request.project.semanticIdentity();
            result.publicationCancelled = [cancellation, externalCancellation] {
                return cancellation->load(std::memory_order_relaxed)
                    || (externalCancellation && externalCancellation());
            };
            result.fileAnalysisBands = fileAnalysisBands;
            if (result.preparedSnapshot && !result.preparedEffectiveFactsState) {
                result.preparedEffectiveFactsState =
                    EffectiveValueService::prepareDocumentFactsState(
                        std::move(result.effectiveFactsByFile),
                        result.preparedSnapshot->fileContentsView(),
                        result.effectiveContentFingerprintsByFile,
                        result.documentRevisionsByFile,
                        result.analysisRevision);
            }
            if (result.preparedSnapshot && !result.cancelled && result.error.isEmpty()) {
                prepareWorkspaceDiagnostics(&result, request.runtimePolicy.normalized().maxDiagnostics);
                auto state = std::make_shared<PublishedWorkspaceSemanticState>();
                state->scopeKey = scopeKey;
                state->project = request.project;
                state->policy = request.runtimePolicy.normalized();
                state->input = result.input;
                state->snapshot = result.preparedSnapshot;
                state->dependencyGraph = result.dependencyGraph;
                state->analysisRevision = result.analysisRevision;
                state->facts = EffectiveValueService::mergedFactsState(
                    !result.incrementalPlan.authoritativeWorkspaceReplace && retained ? retained->facts : nullptr,
                    result.preparedEffectiveFactsState);
                state->logicalBytes = result.preparedSnapshot->logicalBytes()
                    + (result.input ? result.input->logicalBytes() : 0)
                    + EffectiveValueService::logicalFactsBytes(state->facts)
                    + result.dependencyGraph.logicalBytes();
                result.publishedState = std::move(state);
                // Diagnostics selection may have created a new immutable envelope.
                if (result.preparedRelationshipState)
                    result.preparedRelationshipState->snapshot = result.preparedSnapshot;
            }
            result.preparedAnalysisBandReport = preparedBandReport(
                result.preparedSnapshot, fileAnalysisBands);
            if (request.relationshipProjectionRequested && result.preparedSnapshot && !result.cancelled) {
                ProjectSnapshot projectionProject = request.project;
                if (!request.triggerFile.isEmpty()
                    && !projectionProject.systemVerilogFiles.contains(request.triggerFile, Qt::CaseInsensitive))
                    projectionProject.systemVerilogFiles.append(request.triggerFile);
                result.relationshipProjection = std::make_shared<WorkspaceRelationshipAnalysisResult>(
                    RelationshipAnalysisWorker::analyzeWorkspace(nullptr, projectionProject,
                        {result.preparedSnapshot, 0}, request.generation, request.project.semanticIdentity(), cancelled));
                result.cancelled |= result.relationshipProjection->cancelled;
            }
            return result;
        });

    auto* watcher = new QFutureWatcher<WorkspaceAnalysisResult>(this);
    workspaceAnalysisWatcher = watcher;
    workspaceAnalysisRequest = request;
    connect(watcher,
            &QFutureWatcher<WorkspaceAnalysisResult>::finished,
            this,
            [this,
             watcher,
             request,
             cancellation,
             deliveryGeneration,
             epoch,
             workspacePath]() {
                const bool watcherCancelled = watcher->isCanceled();
                WorkspaceAnalysisResult result;
                if (!watcherCancelled)
                    // Transfer the single result out of the future. The
                    // publication retirement pool owns its eventual disposal.
                    result = watcher->future().takeResult();
                if (workspaceAnalysisWatcher == watcher) {
                    workspaceAnalysisWatcher = nullptr;
                    workspaceAnalysisRequest.reset();
                }
                watcher->deleteLater();
                if (workspaceAnalysisCancelFlag == cancellation)
                    workspaceAnalysisCancelFlag.reset();

                if (watcherCancelled
                    || cancellation->load(std::memory_order_relaxed)
                    || result.cancelled
                    || deliveryGeneration != workspaceAnalysisGeneration
                    || epoch != workspaceEpoch
                    || result.request.generation != request.generation
                    || !isWorkspacePublicationCurrent(result)) {
                    retireWorkspaceResult(std::move(result));
                    emit semanticAnalysisDropped(request, SemanticAnalysisRequestDisposition::Expired);
                    emit workspaceAnalysisExpired();
                    return;
                }

                if (result.disposition) {
                    const auto droppedRequest = result.request;
                    const auto disposition = *result.disposition;
                    retireWorkspaceResult(std::move(result));
                    emit semanticAnalysisDropped(droppedRequest, disposition);
                    return;
                }

                if (!result.error.isEmpty() || !result.preparedSnapshot) {
                    const QString error = result.error.isEmpty()
                        ? QStringLiteral("Semantic worker produced no snapshot")
                        : result.error;
                    const auto failedRequest = result.request;
                    retireWorkspaceResult(std::move(result));
                    emit semanticAnalysisFailed(failedRequest, error);
                    return;
                }

                const IncrementalAnalysisPlan plan = result.incrementalPlan;
                SemanticAnalysisTelemetry telemetry;
                telemetry.generation = result.request.generation;
                telemetry.stage = SemanticAnalysisStage::Worker;
                telemetry.reason = result.request.reason;
                telemetry.impact = result.incrementalPlan.impact;
                telemetry.files = result.incrementalPlan.compilationFiles;
                telemetry.changedFiles = result.incrementalPlan.changedFiles;
                telemetry.workerMs = result.workerElapsedMs;
                telemetry.slangInvoked = result.slangInvoked;
                telemetry.detail = QStringLiteral(
                    "symbols=%1 diagnostics=%2 slang=%3 relationships=%4ms relationshipState=%5ms; %6")
                    .arg(result.totalSymbols)
                    .arg(result.diagnostics.size())
                    .arg(result.slangInvoked ? QStringLiteral("yes")
                                             : QStringLiteral("no"))
                    .arg(result.relationshipExtractionMs
                         + result.relationshipBuildMs)
                    .arg(result.relationshipStateBuildMs)
                    .arg(result.incrementalPlan.compilationContextReason);
                const int affectedFiles =
                    result.incrementalPlan.affectedFiles.size();
                if (!startWorkspacePublication(std::move(result), affectedFiles, workspacePath))
                    return;
                if (!request.compatibilityRequest)
                    emit semanticAnalysisPlanPrepared(plan);
                if (pendingWorkspacePublication
                    && isWorkspacePublicationCurrent(*pendingWorkspacePublication))
                    emit semanticAnalysisTelemetry(telemetry);
            });
    watcher->setFuture(future);
    // The real slot and cancellation owner exist before synchronous observers
    // can submit another request or start shutdown.
    emit analysisStarted(workspacePath);
}
