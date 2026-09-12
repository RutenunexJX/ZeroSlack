#include "symbolanalyzer.h"

#include "incrementalsemanticanalysisworker.h"
#include "semanticindexsnapshot.h"

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
    QList<SemanticSymbolRecord> records = snapshot->getSymbolRecords();
    for (SemanticSymbolRecord& record : records) {
        record.analysisBand = bands.value(
            normalizedIncrementalBandFileName(record.location.fileName));
    }
    return semanticAnalysisBandReportForRecords(records);
}
}

void SymbolAnalyzer::startSemanticAnalysisAsync(
    const SemanticAnalysisRequest& originalRequest)
{
    if (shutdownStarted
        || !originalRequest.isValid()
        || !originalRequest.runtimePolicy.normalized().enabled) {
        return;
    }

    // The controller serializes requests. A legacy overlap is expired without
    // joining on the GUI thread; generation checks reject its result.
    if (workspaceAnalysisWatcher || pendingWorkspacePublication) {
        expireWorkspaceAnalysis();
        return;
    }

    SemanticAnalysisRequest request = originalRequest;
    const std::uint64_t deliveryGeneration = ++workspaceAnalysisGeneration;
    const std::uint64_t epoch = ++workspaceEpoch;
    const auto cancellation = std::make_shared<std::atomic_bool>(false);
    workspaceAnalysisCancelFlag = cancellation;
    overlayWorkspaceFiles = request.project.systemVerilogFiles;
    overlayWorkspaceIncludeDirs = request.project.includeDirs;
    overlayWorkspaceDefines = request.project.defines;

    const std::shared_ptr<const SemanticIndexSnapshot> baseSnapshot =
        SemanticIndex::getInstance()->snapshot();
    const std::uint64_t expectedSnapshotRevision =
        request.expectedSnapshotRevision > 0
            ? request.expectedSnapshotRevision
            : SemanticIndex::getInstance()->snapshotRevision();
    request.expectedSnapshotRevision = expectedSnapshotRevision;
    const SemanticDependencyGraph graph = semanticDependencyGraph;
    const QHash<QString, SemanticAnalysisBandMetadata> fileAnalysisBands =
        workspaceFileAnalysisBands;
    const WorkspaceWorkerStartGateForTesting workerStartGate =
        workspaceWorkerStartGateForTesting;
    const QString workspacePath = request.project.workspaceRoot.isEmpty()
        ? request.triggerFile
        : request.project.workspaceRoot;

    emit analysisStarted(workspacePath);
    QFuture<WorkspaceAnalysisResult> future = QtConcurrent::run(
        &semanticAnalysisThreadPool,
        [request,
         baseSnapshot,
         graph,
         fileAnalysisBands,
         cancellation,
         workerStartGate]() {
            auto cancelled = [&]() {
                return cancellation->load(std::memory_order_relaxed);
            };
            if (workerStartGate)
                workerStartGate(cancelled);
            WorkspaceAnalysisResult result =
                IncrementalSemanticAnalysisWorker::analyze(
                request, baseSnapshot, graph, cancelled);
            result.fileAnalysisBands = fileAnalysisBands;
            if (result.preparedSnapshot) {
                result.preparedEffectiveFactsState =
                    EffectiveValueService::prepareDocumentFactsState(
                        std::move(result.effectiveFactsByFile),
                        result.preparedSnapshot->fileContentsView(),
                        result.effectiveContentFingerprintsByFile,
                        result.documentRevisionsByFile,
                        result.analysisRevision);
            }
            result.preparedAnalysisBandReport = preparedBandReport(
                result.preparedSnapshot, fileAnalysisBands);
            return result;
        });

    auto* watcher = new QFutureWatcher<WorkspaceAnalysisResult>(this);
    workspaceAnalysisWatcher = watcher;
    connect(watcher,
            &QFutureWatcher<WorkspaceAnalysisResult>::finished,
            this,
            [this,
             watcher,
             request,
             cancellation,
             deliveryGeneration,
             epoch,
             expectedSnapshotRevision,
             workspacePath]() {
                const bool watcherCancelled = watcher->isCanceled();
                WorkspaceAnalysisResult result;
                if (!watcherCancelled)
                    result = watcher->result();
                if (workspaceAnalysisWatcher == watcher)
                    workspaceAnalysisWatcher = nullptr;
                watcher->deleteLater();
                if (workspaceAnalysisCancelFlag == cancellation)
                    workspaceAnalysisCancelFlag.reset();

                if (watcherCancelled
                    || cancellation->load(std::memory_order_relaxed)
                    || result.cancelled
                    || deliveryGeneration != workspaceAnalysisGeneration
                    || epoch != workspaceEpoch
                    || result.request.generation != request.generation
                    || SemanticIndex::getInstance()->snapshotRevision()
                           != expectedSnapshotRevision) {
                    emit workspaceAnalysisExpired();
                    return;
                }

                if (result.disposition) {
                    emit semanticAnalysisDropped(result.request,
                                                 *result.disposition);
                    return;
                }

                if (!result.error.isEmpty() || !result.preparedSnapshot) {
                    const QString error = result.error.isEmpty()
                        ? QStringLiteral("Semantic worker produced no snapshot")
                        : result.error;
                    emit semanticAnalysisFailed(result.request, error);
                    return;
                }

                emit semanticAnalysisPlanPrepared(result.incrementalPlan);
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
                    "symbols=%1 diagnostics=%2 slang=%3 relationships=%4ms relationshipState=%5ms")
                    .arg(result.totalSymbols)
                    .arg(result.diagnostics.size())
                    .arg(result.slangInvoked ? QStringLiteral("yes")
                                             : QStringLiteral("no"))
                    .arg(result.relationshipExtractionMs
                         + result.relationshipBuildMs)
                    .arg(result.relationshipStateBuildMs);
                emit semanticAnalysisTelemetry(telemetry);

                const int affectedFiles =
                    result.incrementalPlan.affectedFiles.size();
                startWorkspacePublication(std::move(result),
                                          affectedFiles,
                                          workspacePath);
            });
    watcher->setFuture(future);
}
