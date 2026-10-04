#include "symbolanalyzer.h"

#include "diagnosticpublicationpolicy.h"
#include "semanticindex.h"
#include "semanticindexsnapshot.h"
#include "effectivevalueservice.h"
#include "relationshipanalysisworker.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QTimer>
#include <QPointer>
#include <QScopeGuard>
#include <algorithm>
#include <utility>

namespace {
int diagnosticLimitForResult(const WorkspaceAnalysisResult& result,
                             int fallbackLimit)
{
    if (result.request.isValid())
        return result.request.runtimePolicy.normalized().maxDiagnostics;
    return qMax(1, fallbackLimit);
}

} // namespace

void SymbolAnalyzer::prepareWorkspaceDiagnostics(WorkspaceAnalysisResult* result, int fallbackLimit)
{
    if (!result || !result->preparedSnapshot || result->diagnosticsPrepared)
        return;
    const int limit = diagnosticLimitForResult(*result, fallbackLimit);
    if (result->preparedSnapshot->diagnosticDisplayLimit() != limit)
        result->preparedSnapshot = std::make_shared<const SemanticIndexSnapshot>(
            result->preparedSnapshot->withDiagnosticDisplayLimit(limit));
    result->diagnostics = result->preparedSnapshot->diagnostics();
    result->diagnosticsProduced = result->preparedSnapshot->rawDiagnosticCount();
    result->diagnosticsPublished = result->diagnostics.size();
    result->diagnosticsSuppressed = result->preparedSnapshot->suppressedDiagnosticCount();
    result->diagnosticsPrepared = true;
}

void SymbolAnalyzer::forgetWorkspace(const QString& root)
{
    forgetRetainedState(SemanticInputCapture::pathKey(root));
}

void SymbolAnalyzer::forgetRetainedState(const QString& key)
{
    // Callers can pass the LRU's first element. Keep an owning copy before
    // removing that element, otherwise the lookup below uses a dead reference.
    const QString stateKey = key;
    retainedWorkspaceLru.removeAll(stateKey);
    SemanticPublicationRetirementPayload retired;
    retired.workspaceState = retainedWorkspaces.take(stateKey);
    if (activeWorkspaceState && SemanticInputCapture::pathKey(activeWorkspaceState->project.workspaceRoot) == stateKey) {
        if (!retired.workspaceState)
            retired.workspaceState = activeWorkspaceState;
        activeWorkspaceState.reset();
    }
    retirePublicationState(std::move(retired));
}

void SymbolAnalyzer::rememberWorkspace(std::shared_ptr<const PublishedWorkspaceSemanticState> state)
{
    if (!state || !state->input || !state->snapshot)
        return;
    const QString key = state->scopeKey;
    forgetRetainedState(key);
    if (state->project.isOpen())
        activeWorkspaceState = state;
    if (state->logicalBytes > maximumRetainedWorkspaceBytes)
        return;
    retainedWorkspaces.insert(key, std::move(state));
    retainedWorkspaceLru.append(key);
    auto retainedBytes = [&] {
        qsizetype bytes = 0;
        for (auto it = retainedWorkspaces.cbegin(); it != retainedWorkspaces.cend(); ++it)
            bytes += it.value()->logicalBytes;
        return bytes;
    };
    while (retainedWorkspaceLru.size() > maximumRetainedWorkspaces
           || retainedBytes() > maximumRetainedWorkspaceBytes)
        forgetRetainedState(retainedWorkspaceLru.first());
}

void SymbolAnalyzer::retireWorkspaceResult(WorkspaceAnalysisResult result)
{
    SemanticPublicationRetirementPayload retired;
    retired.analysisResult = std::make_unique<WorkspaceAnalysisResult>(std::move(result));
    retirePublicationState(std::move(retired));
}

bool SymbolAnalyzer::bindPublishedRelationships(SymbolRelationshipEngine* engine,
                                                 const SemanticSnapshotToken& publication)
{
    const auto current = SemanticIndex::getInstance()->snapshotToken();
    if (shutdownStarted || !engine || !publication.snapshot
        || current.snapshot != publication.snapshot || current.revision != publication.revision)
        return false;
    if (engine != SemanticIndex::getInstance()->relationshipEngine()) {
        auto state = std::make_shared<SymbolRelationshipEngine::PreparedRelationshipState>();
        state->snapshot = publication.snapshot;
        SemanticPublicationRetirementPayload retired;
        retired.semanticIndex.relationshipState = engine->installPreparedRelationshipState(std::move(state), false);
        retirePublicationState(std::move(retired));
        emit engine->relationshipsReplaced();
    }
    return true;
}

void SymbolAnalyzer::retirePublicationState(
    SemanticPublicationRetirementPayload payload)
{
    if (payload.isEmpty())
        return;
    const PublicationRetirementGateForTesting gate = publicationRetirementQueueOpen
        ? publicationRetirementGateForTesting : PublicationRetirementGateForTesting{};
    const std::shared_ptr<std::atomic<int>> pending =
        pendingPublicationRetirements;
    const QPointer<SymbolAnalyzer> owner(this);
    publicationRetirementEnqueueCount.fetch_add(1,
                                                std::memory_order_acq_rel);
    pending->fetch_add(1, std::memory_order_acq_rel);
    semanticRetirementThreadPool.start(
        [payload = std::move(payload),
         gate,
         pending,
         owner]() mutable {
            if (gate)
                gate();
            // Results retain per-file symbols, facts and dependency edges even
            // after the prepared snapshot is installed. Release those final
            // owners off the GUI thread too, before marking retirement done.
            payload.analysisResult.reset();
            payload.dependencyGraph = SemanticDependencyGraph();
            payload.effectiveFacts.reset();
            payload.semanticIndex.relationshipState.reset();
            payload.semanticIndex.snapshot.reset();
            payload.semanticIndex.nativeStore.reset();
            payload.workspaceState.reset();
            pending->fetch_sub(1, std::memory_order_acq_rel);
            if (owner) {
                QMetaObject::invokeMethod(owner, [owner] {
                    if (owner && !owner->shutdownStarted)
                        emit owner->workspaceAnalysisExpired();
                }, Qt::QueuedConnection);
            }
        },
        -1);
}

void SymbolAnalyzer::flushDeferredWorkspaceRetirements()
{
    if (workspaceRetirementTimer)
        workspaceRetirementTimer->stop();
    auto retirements = std::exchange(
        deferredWorkspaceRetirements,
        std::vector<SemanticPublicationRetirementPayload>{});
    for (auto& retirement : retirements)
        retirePublicationState(std::move(retirement));
}

void SymbolAnalyzer::waitForPublicationRetirements()
{
    flushDeferredWorkspaceRetirements();
    semanticRetirementThreadPool.waitForDone();
}

void SymbolAnalyzer::clearSemanticIndex()
{
    ++workspaceEpoch;
    ++workspaceAnalysisGeneration;
    overlayProject = {};
    auto* index = SemanticIndex::getInstance();
    SemanticPublicationRetirementPayload retirement;
    retirement.semanticIndex.snapshot = index->snapshot();
    retirement.semanticIndex.nativeStore = index->takeNativeStoreForRetirement();
    retirement.workspaceState = std::exchange(activeWorkspaceState, {});
    index->clearSemanticState();
    if (shutdownStarted) {
        retirePublicationState(std::move(retirement));
        return;
    }
    if (!retirement.isEmpty()) {
        // Let workspace activation finish registering watches and notifying
        // views before bulk destruction competes with those GUI allocations.
        // Ownership stays here until the timer or a synchronous drain runs.
        deferredWorkspaceRetirements.push_back(std::move(retirement));
        workspaceRetirementTimer->start(0);
    }
}

bool SymbolAnalyzer::isWorkspacePublicationCurrent(const WorkspaceAnalysisResult& result,
                                                  bool checkExternalCancellation) const
{
    const auto* index = SemanticIndex::getInstance();
    return !shutdownStarted
        && (!checkExternalCancellation || !result.publicationCancelled || !result.publicationCancelled())
        && result.deliveryGeneration == workspaceAnalysisGeneration
        && result.workspaceEpoch == workspaceEpoch
        && result.generation == result.request.generation
        && result.publicationProjectIdentity == result.request.project.semanticIdentity()
        && result.basePublication.revision == result.request.expectedSnapshotRevision
        && index->snapshotRevision() == result.basePublication.revision
        && index->snapshot() == result.basePublication.snapshot;
}

bool SymbolAnalyzer::startWorkspacePublication(
    WorkspaceAnalysisResult result, int totalFiles, const QString& workspacePath)
{
    // This is also the rejection boundary after synchronous worker observers.
    // Never replace an independently newer pending publication.
    if (!isWorkspacePublicationCurrent(result)
        || !result.preparedSnapshot || !result.preparedRelationshipState
        || !result.preparedEffectiveFactsState) {
        const auto request = result.request;
        retireWorkspaceResult(std::move(result));
        emit semanticAnalysisDropped(request, SemanticAnalysisRequestDisposition::Expired);
        if (!shutdownStarted) {
            emit workspaceAnalysisExpired();
        }
        return false;
    }
    if (pendingWorkspacePublication) {
        const auto request = result.request;
        retireWorkspaceResult(std::move(result));
        emit semanticAnalysisDropped(request, SemanticAnalysisRequestDisposition::Superseded);
        return false;
    }
    pendingWorkspacePublication = std::make_unique<WorkspaceAnalysisResult>(std::move(result));
    pendingWorkspacePublicationTotalFiles = totalFiles;
    pendingWorkspacePublicationPath = workspacePath;
    if (workspacePublicationTimer)
        workspacePublicationTimer->start(0);
    return true;
}

void SymbolAnalyzer::cancelWorkspacePublication()
{
    if (workspacePublicationTimer)
        workspacePublicationTimer->stop();
    SemanticPublicationRetirementPayload retirement;
    retirement.analysisResult = std::move(pendingWorkspacePublication);
    const auto request = retirement.analysisResult
        ? retirement.analysisResult->request : SemanticAnalysisRequest{};
    pendingWorkspacePublicationPath.clear();
    pendingWorkspacePublicationTotalFiles = 0;
    retirePublicationState(std::move(retirement));
    if (request.isValid())
        emit semanticAnalysisDropped(request, SemanticAnalysisRequestDisposition::Expired);
}

void SymbolAnalyzer::publishPendingWorkspaceAnalysis()
{
    if (!pendingWorkspacePublication)
        return;
    // Detach ownership before any notification. An observer may enqueue a new
    // request; finishing this transaction must never cancel that new request.
    SemanticPublicationRetirementPayload retirement;
    retirement.analysisResult = std::move(pendingWorkspacePublication);
    const QString workspacePath = std::exchange(pendingWorkspacePublicationPath, {});
    const int totalFiles = std::exchange(pendingWorkspacePublicationTotalFiles, 0);
    const auto request = retirement.analysisResult->request;
    bool requestNotified = false;
    workspacePublicationActive = true;
    const auto completion = qScopeGuard([this, &retirement, &requestNotified, &request] {
        retirePublicationState(std::move(retirement));
        workspacePublicationActive = false;
        if (!requestNotified)
            emit semanticAnalysisDropped(request, SemanticAnalysisRequestDisposition::Expired);
        if (!shutdownStarted) {
            emit workspaceAnalysisExpired();
        }
    });
    const WorkspaceAnalysisResult& result = *retirement.analysisResult;
    auto reject = [&] {
        requestNotified = true;
        emit semanticAnalysisDropped(result.request, SemanticAnalysisRequestDisposition::Expired);
    };
    if (!isWorkspacePublicationCurrent(result)) {
        reject();
        return;
    }

    QElapsedTimer timer;
    timer.start();
    auto* index = SemanticIndex::getInstance();
    qint64 snapshotInstallMs = 0;
    // The facts write lock spans final validation and BOTH quiet installs.
    // No signals/logging are allowed inside this callback. A rejected facts
    // state therefore cannot publish any symbols, diagnostics or relationships.
    auto facts = EffectiveValueService::getInstance()->installPreparedDocumentFacts(
        result.preparedEffectiveFactsState,
        result.incrementalPlan.authoritativeWorkspaceReplace,
        [&] {
            if (!isWorkspacePublicationCurrent(result, false))
                return false;
            QElapsedTimer snapshotTimer;
            snapshotTimer.start();
            retirement.semanticIndex = index->installPreparedSnapshot(
                result.preparedSnapshot, result.incrementalPlan.affectedFiles,
                result.preparedRelationshipHandlesByFile, result.preparedRelationships,
                result.relationshipDeltaPrepared, result.preparedRelationshipState,
                result.preparedAnalysisBandReport, false);
            snapshotInstallMs = snapshotTimer.elapsed();
            return true;
        });
    if (!facts.accepted) {
        reject();
        return;
    }
    retirement.effectiveFacts = std::move(facts.retired);
    const qint64 effectiveFactsMs = timer.elapsed() - snapshotInstallMs;
    index->setWorkspaceFileAnalysisBands(result.fileAnalysisBands);
    rememberWorkspace(result.publishedState);
    const std::uint64_t committedRevision = index->snapshotRevision();
    auto stillCurrent = [&] {
        return !shutdownStarted
            && result.deliveryGeneration == workspaceAnalysisGeneration
            && result.workspaceEpoch == workspaceEpoch
            && index->snapshotRevision() == committedRevision
            && index->snapshot() == result.preparedSnapshot;
    };
    const qint64 publicationMs = timer.elapsed();
    index->notifyPreparedSnapshotPublished(committedRevision, result.incrementalPlan.affectedFiles);
    if (!stillCurrent())
        return;
    if (result.input && result.request.project.isOpen()) {
        QStringList observedFiles = result.input->watchFiles;
        QSet<QString> roots;
        for (const QString& file : result.request.project.systemVerilogFiles)
            roots.insert(SemanticInputCapture::pathKey(file));
        observedFiles.removeIf([&](const QString& file) { return roots.contains(file); });
        emit semanticInputWatchPathsChanged(result.request.project.workspaceRoot,
                                           observedFiles, result.input->watchDirectories);
        if (!stillCurrent())
            return;
    }

    SemanticAnalysisTelemetry telemetry;
    telemetry.generation = result.request.generation;
    telemetry.stage = SemanticAnalysisStage::Publication;
    telemetry.reason = result.request.reason;
    telemetry.impact = result.incrementalPlan.impact;
    telemetry.files = result.incrementalPlan.affectedFiles;
    telemetry.changedFiles = result.incrementalPlan.changedFiles;
    telemetry.workerMs = result.workerElapsedMs;
    telemetry.publicationMs = publicationMs;
    telemetry.effectiveFactsMs = effectiveFactsMs;
    telemetry.snapshotInstallMs = snapshotInstallMs;
    telemetry.diagnosticsProduced = result.diagnosticsProduced;
    telemetry.diagnosticsPublished = result.diagnosticsPublished;
    telemetry.diagnosticsSuppressed = result.diagnosticsSuppressed;
    telemetry.slangInvoked = result.slangInvoked;
    telemetry.detail = QStringLiteral("Committed semantic publication revision=%1").arg(committedRevision);
    emit semanticAnalysisTelemetry(telemetry);
    if (!stillCurrent())
        return;
    emitWorkspaceAnalysisTelemetry(workspacePath, result, totalFiles,
        result.incrementalPlan.affectedFiles.size(), publicationMs, publicationMs, 0);
    if (!stillCurrent())
        return;
    if (result.request.project.isOpen() && !result.incrementalPlan.affectedFiles.isEmpty()) {
        emit batchProgress(result.incrementalPlan.affectedFiles.size(), totalFiles,
                           result.incrementalPlan.affectedFiles.constLast());
        if (!stillCurrent())
            return;
    }
    requestNotified = true;
    emit semanticAnalysisCommitted(result.request, index->snapshotToken());
    if (!stillCurrent())
        return;
    if (result.relationshipProjection) {
        result.relationshipProjection->baseSnapshot = index->snapshotToken();
        requestNotified = true;
        emit relationshipAnalysisCommitted(result.request, *result.relationshipProjection);
        if (!stillCurrent())
            return;
    }
    if (result.request.project.isOpen()) {
        requestNotified = true;
        emit batchAnalysisCompleted(result.incrementalPlan.affectedFiles.size(), result.totalSymbols);
        if (!stillCurrent())
            return;
    }
    if (!result.request.compatibilityCompletionFiles.isEmpty()) {
        for (const auto& file : result.request.compatibilityCompletionFiles) {
            emit analysisCompleted(file, result.preparedSnapshot->symbolRecordCount(file));
            if (!stillCurrent())
                return;
        }
    } else {
        emit analysisCompleted(result.request.triggerFile.isEmpty() ? workspacePath : result.request.triggerFile,
                               result.totalSymbols);
    }
}

void SymbolAnalyzer::emitWorkspaceAnalysisTelemetry(
    const QString& workspacePath,
    const WorkspaceAnalysisResult& result,
    int totalFiles,
    int filesAnalyzed,
    qint64 publicationMs,
    qint64 publicationUpdateMs,
    qint64 finalSnapshotMs)
{
    WorkspaceAnalysisTelemetry telemetry;
    telemetry.workspaceRoot = workspacePath;
    telemetry.totalFiles = totalFiles;
    telemetry.filesAnalyzed = filesAnalyzed;
    telemetry.totalSymbols = result.totalSymbols;
    const bool publicationCountsRecorded =
        result.diagnosticsProduced > 0
        || result.diagnosticsPublished > 0
        || result.diagnosticsSuppressed > 0;
    const DiagnosticPublicationSelection selection =
        publicationCountsRecorded
        ? DiagnosticPublicationSelection{
              result.diagnostics,
              result.diagnosticsProduced,
              result.diagnosticsPublished,
              result.diagnosticsSuppressed}
        : DiagnosticPublicationPolicy::select(
              result.diagnostics,
              diagnosticLimitForResult(result,
                                       publishedDiagnosticLimit));
    telemetry.diagnostics = selection.publishedCount;
    telemetry.diagnosticsProduced = selection.producedCount;
    telemetry.diagnosticsSuppressed = selection.suppressedCount;
    telemetry.workerElapsedMs = result.workerElapsedMs;
    telemetry.symbolExtractionMs = result.symbolExtractionMs;
    telemetry.resultAssemblyMs = result.resultAssemblyMs;
    telemetry.diagnosticsExtractionMs = result.diagnosticsExtractionMs;
    telemetry.publicationMs = publicationMs;
    telemetry.publicationUpdateMs = publicationUpdateMs;
    telemetry.finalSnapshotMs = finalSnapshotMs;
    telemetry.totalElapsedMs = result.workerElapsedMs + publicationMs;
    emit workspaceAnalysisTelemetry(telemetry);
}
