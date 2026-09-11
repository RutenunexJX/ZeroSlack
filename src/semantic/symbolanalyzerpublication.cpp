#include "symbolanalyzer.h"

#include "diagnosticpublicationpolicy.h"
#include "semanticindex.h"
#include "semanticindexsnapshot.h"
#include "effectivevalueservice.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QTimer>
#include <algorithm>
#include <utility>

namespace {
QString normalizedSymbolAnalyzerFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    QString result = QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
#ifdef Q_OS_WIN
    result = result.toCaseFolded();
#endif
    return result;
}

QList<SemanticSymbolRecord> recordsWithRevisions(
    const QList<SemanticSymbolRecord>& records,
    std::uint64_t computationRevision,
    std::uint64_t documentRevision)
{
    QList<SemanticSymbolRecord> result = records;
    for (SemanticSymbolRecord& record : result) {
        record.presentation.computationRevision = computationRevision;
        record.presentation.documentRevision = documentRevision;
    }
    return result;
}

int diagnosticLimitForResult(const WorkspaceAnalysisResult& result,
                             int fallbackLimit)
{
    if (result.request.isValid())
        return result.request.runtimePolicy.normalized().maxDiagnostics;
    return qMax(1, fallbackLimit);
}

void applyDiagnosticPublicationPolicy(WorkspaceAnalysisResult* result,
                                      int fallbackLimit)
{
    if (!result)
        return;

    const int limit = diagnosticLimitForResult(*result, fallbackLimit);
    if (result->preparedSnapshot) {
        const QList<SemanticDiagnostic> produced =
            result->preparedSnapshot->diagnostics();
        const DiagnosticPublicationSelection selection =
            DiagnosticPublicationPolicy::select(produced, limit);
        QStringList diagnosticFiles =
            result->incrementalPlan.affectedFiles;
        for (const SemanticDiagnostic& diagnostic : produced) {
            if (!diagnostic.fileName.isEmpty()
                && !diagnosticFiles.contains(diagnostic.fileName,
                                             Qt::CaseInsensitive)) {
                diagnosticFiles.append(diagnostic.fileName);
            }
        }
        result->preparedSnapshot =
            std::make_shared<const SemanticIndexSnapshot>(
                result->preparedSnapshot->withReplacedDiagnostics(
                    diagnosticFiles,
                    selection.diagnostics));
        result->diagnostics = selection.diagnostics;
        result->diagnosticsProduced = selection.producedCount;
        result->diagnosticsPublished = selection.publishedCount;
        result->diagnosticsSuppressed = selection.suppressedCount;
        return;
    }

    const DiagnosticPublicationSelection selection =
        DiagnosticPublicationPolicy::select(result->diagnostics, limit);
    result->diagnostics = selection.diagnostics;
    result->diagnosticsProduced = selection.producedCount;
    result->diagnosticsPublished = selection.publishedCount;
    result->diagnosticsSuppressed = selection.suppressedCount;
}

} // namespace

void SymbolAnalyzer::retirePublicationState(
    SemanticPublicationRetirementPayload payload)
{
    if (payload.isEmpty())
        return;
    if (!publicationRetirementQueueOpen) {
        rejectedPublicationRetirements.fetch_add(1,
                                                 std::memory_order_acq_rel);
        Q_ASSERT_X(false,
                   "SymbolAnalyzer::retirePublicationState",
                   "publication retirement submitted after shutdown seal");
        return;
    }
    const PublicationRetirementGateForTesting gate =
        publicationRetirementGateForTesting;
    const std::shared_ptr<std::atomic<int>> pending =
        pendingPublicationRetirements;
    publicationRetirementEnqueueCount.fetch_add(1,
                                                std::memory_order_acq_rel);
    pending->fetch_add(1, std::memory_order_acq_rel);
    semanticRetirementThreadPool.start(
        [payload = std::move(payload),
         gate,
         pending]() mutable {
            if (gate)
                gate();
            payload.effectiveFacts.reset();
            payload.semanticIndex.relationshipState.reset();
            payload.semanticIndex.snapshot.reset();
            pending->fetch_sub(1, std::memory_order_acq_rel);
        },
        -1);
}

void SymbolAnalyzer::waitForPublicationRetirements()
{
    semanticRetirementThreadPool.waitForDone();
}

void SymbolAnalyzer::publishOpenDocumentResults(
    const QStringList& fileNames,
    const QList<SemanticDiagnostic>& diagnostics)
{
    const DiagnosticPublicationSelection selection =
        DiagnosticPublicationPolicy::select(
            diagnostics,
            publishedDiagnosticLimit);
    SemanticIndex::getInstance()->publishSnapshotReplacingDiagnostics(
        fileNames,
        selection.diagnostics);
}

void SymbolAnalyzer::updateFileSymbols(
    const QString& fileName,
    const QString& content,
    const QList<SemanticSymbolRecord>& symbolRecords,
    const QList<EffectiveValueFact>& effectiveValueFacts,
    std::uint64_t computationRevision,
    std::uint64_t documentRevision)
{
    const std::uint64_t revision = computationRevision > 0
        ? computationRevision
        : EffectiveValueService::getInstance()->beginComputation({fileName});
    EffectiveValueService* values =
        EffectiveValueService::getInstance();
    if (!values->isComputationCurrent(fileName, revision))
        return;
    SemanticIndex::getInstance()->updateSymbolRecordsForFile(
        fileName,
        recordsWithRevisions(symbolRecords, revision, documentRevision),
        content);
    values->publishDocumentFacts(
        fileName,
        content,
        effectiveValueFacts,
        revision,
        documentRevision);
}

void SymbolAnalyzer::publishFileAnalysisResult(
    const QString& fileName,
    const QString& content,
    const QList<SemanticSymbolRecord>& symbolRecords,
    const QList<SemanticDiagnostic>& diagnostics,
    const QList<EffectiveValueFact>& effectiveValueFacts,
    std::uint64_t computationRevision,
    std::uint64_t documentRevision)
{
    const std::uint64_t revision = computationRevision > 0
        ? computationRevision
        : EffectiveValueService::getInstance()->beginComputation({fileName});
    EffectiveValueService* values =
        EffectiveValueService::getInstance();
    if (!values->isComputationCurrent(fileName, revision))
        return;
    SemanticIndex* semanticIndex = SemanticIndex::getInstance();
    const DiagnosticPublicationSelection selection =
        DiagnosticPublicationPolicy::select(
            diagnostics,
            publishedDiagnosticLimit);
    semanticIndex->updateSymbolRecordsForFile(
        fileName,
        recordsWithRevisions(symbolRecords, revision, documentRevision),
        content);
    semanticIndex->publishSnapshotReplacingDiagnostics(
        {fileName},
        selection.diagnostics);
    values->publishDocumentFacts(
        fileName,
        content,
        effectiveValueFacts,
        revision,
        documentRevision);
}

void SymbolAnalyzer::publishOverlayAnalysisResult(
    const FileAnalysisResult& result)
{
    if (result.workspaceFiles.isEmpty()) {
        publishFileAnalysisResult(result.fileName,
                                  result.content,
                                  result.symbolRecords,
                                  result.diagnostics,
                                  result.effectiveValueFacts,
                                  result.analysisRevision,
                                  result.documentRevision);
        return;
    }

    QStringList publicationFiles;
    publicationFiles.reserve(result.workspaceFiles.size());
    for (const WorkspaceFileAnalysis& fileResult : result.workspaceFiles)
        publicationFiles.append(fileResult.fileName);
    EffectiveValueService* values =
        EffectiveValueService::getInstance();
    if (!values->isComputationCurrent(publicationFiles,
                                      result.analysisRevision)) {
        return;
    }

    QList<SemanticFileSymbolUpdate> updates;
    updates.reserve(result.workspaceFiles.size());
    for (const WorkspaceFileAnalysis& fileResult : result.workspaceFiles) {
        SemanticFileSymbolUpdate update;
        update.fileName = fileResult.fileName;
        update.content = fileResult.content;
        const QString normalizedFile =
            normalizedSymbolAnalyzerFileName(fileResult.fileName);
        update.symbolRecords = recordsWithRevisions(
            fileResult.symbolRecords,
            result.analysisRevision,
            result.documentRevisionsByFile.value(normalizedFile, 0));
        updates.append(std::move(update));
    }

    // A full overlay compilation is authoritative for all instance maps.
    // Publish it in one GUI-thread turn and never merge presentations from
    // an older workspace snapshot.
    SemanticIndex* semanticIndex = SemanticIndex::getInstance();
    semanticIndex->updateSymbolRecordsForFiles(updates, false);
    for (const WorkspaceFileAnalysis& fileResult : result.workspaceFiles) {
        values->publishDocumentFacts(
            fileResult.fileName,
            fileResult.content,
            fileResult.effectiveValueFacts,
            result.analysisRevision,
            result.documentRevisionsByFile.value(
                normalizedSymbolAnalyzerFileName(fileResult.fileName), 0));
    }
    const DiagnosticPublicationSelection selection =
        DiagnosticPublicationPolicy::select(
            result.diagnostics,
            publishedDiagnosticLimit);
    semanticIndex->publishSnapshotReplacingDiagnostics(
        result.diagnosticFiles.isEmpty()
            ? QStringList{result.fileName}
            : result.diagnosticFiles,
        selection.diagnostics);
}

int SymbolAnalyzer::publishWorkspaceAnalysisResult(
    const WorkspaceAnalysisResult& result,
    int totalFiles,
    WorkspaceAnalysisTelemetry* telemetry)
{
    SemanticIndex* semanticIndex = SemanticIndex::getInstance();
    semanticIndex->setWorkspaceFileAnalysisBands(result.fileAnalysisBands);
    const std::uint64_t revision = result.analysisRevision > 0
        ? result.analysisRevision
        : result.generation;
    QList<SemanticFileSymbolUpdate> updates;
    updates.reserve(result.files.size());
    QStringList diagnosticFiles;
    diagnosticFiles.reserve(result.files.size());
    QList<SemanticDiagnostic> diagnostics;
    for (const WorkspaceFileAnalysis& fileResult : result.files) {
        SemanticFileSymbolUpdate update;
        update.fileName = fileResult.fileName;
        update.content = fileResult.content;
        update.symbolRecords = recordsWithRevisions(
            fileResult.symbolRecords,
            revision,
            result.documentRevisionsByFile.value(
                normalizedSymbolAnalyzerFileName(fileResult.fileName), 0));
        updates.append(std::move(update));
        diagnosticFiles.append(fileResult.fileName);
    }

    const DiagnosticPublicationSelection diagnosticSelection =
        DiagnosticPublicationPolicy::select(
            result.diagnostics,
            diagnosticLimitForResult(result,
                                     publishedDiagnosticLimit));
    diagnostics = diagnosticSelection.diagnostics;
    if (telemetry) {
        telemetry->diagnostics = diagnosticSelection.publishedCount;
        telemetry->diagnosticsProduced =
            diagnosticSelection.producedCount;
        telemetry->diagnosticsSuppressed =
            diagnosticSelection.suppressedCount;
    }

    QElapsedTimer updateTimer;
    updateTimer.start();
    semanticIndex->updateSymbolRecordsForFiles(updates, false);
    EffectiveValueService* values = EffectiveValueService::getInstance();
    for (const WorkspaceFileAnalysis& fileResult : result.files) {
        values->publishDocumentFacts(
            fileResult.fileName,
            fileResult.content,
            fileResult.effectiveValueFacts,
            revision,
            result.documentRevisionsByFile.value(
                normalizedSymbolAnalyzerFileName(fileResult.fileName), 0));
    }
    if (telemetry)
        telemetry->publicationUpdateMs += updateTimer.elapsed();

    QElapsedTimer finalSnapshotTimer;
    finalSnapshotTimer.start();
    semanticIndex->publishSnapshotReplacingDiagnostics(diagnosticFiles,
                                                       diagnostics);
    if (telemetry)
        telemetry->finalSnapshotMs += finalSnapshotTimer.elapsed();
    int filesAnalyzed = 0;
    for (const WorkspaceFileAnalysis& fileResult : result.files) {
        ++filesAnalyzed;
        emit batchProgress(filesAnalyzed, totalFiles, fileResult.fileName);
    }
    return filesAnalyzed;
}

void SymbolAnalyzer::startWorkspacePublication(
    WorkspaceAnalysisResult result,
    int totalFiles,
    const QString& workspacePath)
{
    if (shutdownStarted)
        return;
    cancelWorkspacePublication();
    applyDiagnosticPublicationPolicy(&result,
                                     publishedDiagnosticLimit);

    QStringList publicationFiles = result.preparedSnapshot
        ? result.incrementalPlan.affectedFiles
        : QStringList{};
    if (!result.preparedSnapshot) {
        publicationFiles.reserve(result.files.size());
        for (const WorkspaceFileAnalysis& fileResult : result.files)
            publicationFiles.append(fileResult.fileName);
    }
    if (!EffectiveValueService::getInstance()->isComputationCurrent(
            publicationFiles, result.analysisRevision)) {
        emit workspaceAnalysisExpired();
        return;
    }

    pendingWorkspacePublication =
        std::make_unique<WorkspaceAnalysisResult>(std::move(result));
    pendingWorkspacePublicationTotalFiles = totalFiles;
    pendingWorkspacePublicationPath = workspacePath;
    pendingWorkspacePublicationDiagnostics =
        pendingWorkspacePublication->diagnostics;
    pendingWorkspacePublicationTimer.restart();
    pendingWorkspacePublicationUpdateMs = 0;
    pendingWorkspacePublicationFinalSnapshotMs = 0;

    SemanticIndex::getInstance()->setWorkspaceFileAnalysisBands(
        pendingWorkspacePublication->fileAnalysisBands);

    if (workspacePublicationTimer)
        workspacePublicationTimer->start(0);
}

void SymbolAnalyzer::cancelWorkspacePublication()
{
    if (workspacePublicationTimer)
        workspacePublicationTimer->stop();
    pendingWorkspacePublication.reset();
    pendingWorkspacePublicationPath.clear();
    pendingWorkspacePublicationTotalFiles = 0;
    pendingWorkspacePublicationFilesAnalyzed = 0;
    pendingWorkspacePublicationUpdateMs = 0;
    pendingWorkspacePublicationFinalSnapshotMs = 0;
    pendingWorkspacePublicationDiagnostics.clear();
}

void SymbolAnalyzer::publishPendingWorkspaceAnalysis()
{
    if (shutdownStarted) {
        cancelWorkspacePublication();
        return;
    }
    if (!pendingWorkspacePublication) {
        if (workspacePublicationTimer)
            workspacePublicationTimer->stop();
        return;
    }

    QStringList publicationFiles = pendingWorkspacePublication->preparedSnapshot
        ? pendingWorkspacePublication->incrementalPlan.affectedFiles
        : QStringList{};
    if (!pendingWorkspacePublication->preparedSnapshot) {
        publicationFiles.reserve(pendingWorkspacePublication->files.size());
        for (const WorkspaceFileAnalysis& fileResult :
             std::as_const(pendingWorkspacePublication->files)) {
            publicationFiles.append(fileResult.fileName);
        }
    }
    if (!EffectiveValueService::getInstance()->isComputationCurrent(
            publicationFiles,
            pendingWorkspacePublication->analysisRevision)) {
        cancelWorkspacePublication();
        emit workspaceAnalysisExpired();
        return;
    }

    if (pendingWorkspacePublication->preparedSnapshot) {
        const WorkspaceAnalysisResult& result =
            *pendingWorkspacePublication;
        const QStringList affectedFiles = result.incrementalPlan.affectedFiles;
        const auto snapshot = result.preparedSnapshot;
        QElapsedTimer publicationTimer;
        publicationTimer.start();
        QElapsedTimer stageTimer;
        stageTimer.start();
        EffectiveValueService* values = EffectiveValueService::getInstance();
        std::shared_ptr<EffectiveValueService::RetiredFactsState>
            retiredFacts = values->installPreparedDocumentFacts(
            result.preparedEffectiveFactsState,
            result.incrementalPlan.authoritativeWorkspaceReplace);
        const qint64 effectiveFactsMs = stageTimer.elapsed();
        stageTimer.restart();
        SemanticIndexRetirementPayload retiredIndex =
            SemanticIndex::getInstance()->installPreparedSnapshot(
            snapshot,
            affectedFiles,
            result.preparedRelationshipHandlesByFile,
            result.preparedRelationships,
            result.relationshipDeltaPrepared,
            result.preparedRelationshipState,
            result.preparedAnalysisBandReport);
        SemanticPublicationRetirementPayload retirement;
        retirement.semanticIndex = std::move(retiredIndex);
        retirement.effectiveFacts = std::move(retiredFacts);
        retirePublicationState(std::move(retirement));
        if (result.dependencyGraph.isValidFor(result.request.project))
            semanticDependencyGraph = result.dependencyGraph;
        const qint64 snapshotInstallMs = stageTimer.elapsed();
        const qint64 publicationMs = publicationTimer.elapsed();
        const int totalSymbols = result.totalSymbols;
        const int totalFiles = pendingWorkspacePublicationTotalFiles;
        const QString workspacePath = pendingWorkspacePublicationPath;
        const QString completionFile = result.request.triggerFile.isEmpty()
            ? workspacePath
            : result.request.triggerFile;

        SemanticAnalysisTelemetry stageTelemetry;
        stageTelemetry.generation = result.request.generation;
        stageTelemetry.stage = SemanticAnalysisStage::Publication;
        stageTelemetry.reason = result.request.reason;
        stageTelemetry.impact = result.incrementalPlan.impact;
        stageTelemetry.files = affectedFiles;
        stageTelemetry.changedFiles = result.incrementalPlan.changedFiles;
        stageTelemetry.workerMs = result.workerElapsedMs;
        stageTelemetry.publicationMs = publicationMs;
        stageTelemetry.effectiveFactsMs = effectiveFactsMs;
        stageTelemetry.snapshotInstallMs = snapshotInstallMs;
        stageTelemetry.diagnosticsProduced =
            result.diagnosticsProduced;
        stageTelemetry.diagnosticsPublished =
            result.diagnosticsPublished;
        stageTelemetry.diagnosticsSuppressed =
            result.diagnosticsSuppressed;
        stageTelemetry.slangInvoked = result.slangInvoked;
        stageTelemetry.detail = QStringLiteral(
            "publish changedFiles=%1 effectiveFactsMs=%2 snapshotInstallMs=%3 diagnostics=%4 suppressed=%5")
                                    .arg(affectedFiles.join(','))
                                    .arg(effectiveFactsMs)
                                    .arg(snapshotInstallMs)
                                    .arg(result.diagnosticsPublished)
                                    .arg(result.diagnosticsSuppressed);
        emit semanticAnalysisTelemetry(stageTelemetry);
        emitWorkspaceAnalysisTelemetry(workspacePath,
                                       result,
                                       totalFiles,
                                       affectedFiles.size(),
                                       publicationMs,
                                       publicationMs,
                                       0);
        cancelWorkspacePublication();

        if (!affectedFiles.isEmpty()) {
            emit batchProgress(affectedFiles.size(),
                               totalFiles,
                               affectedFiles.constLast());
        }
        emit batchAnalysisCompleted(affectedFiles.size(), totalSymbols);
        emit analysisCompleted(completionFile, totalSymbols);
        return;
    }

    SemanticIndex* semanticIndex = SemanticIndex::getInstance();
    QList<SemanticFileSymbolUpdate> updates;
    updates.reserve(pendingWorkspacePublication->files.size());
    QList<const WorkspaceFileAnalysis*> factPublications;
    factPublications.reserve(pendingWorkspacePublication->files.size());
    QList<QPair<int, QString>> progressEvents;
    progressEvents.reserve(pendingWorkspacePublication->files.size());
    const std::uint64_t revision =
        pendingWorkspacePublication->analysisRevision > 0
        ? pendingWorkspacePublication->analysisRevision
        : pendingWorkspacePublication->generation;
    QStringList diagnosticFiles;
    diagnosticFiles.reserve(pendingWorkspacePublication->files.size());
    for (const WorkspaceFileAnalysis& fileResult :
         std::as_const(pendingWorkspacePublication->files)) {
        SemanticFileSymbolUpdate update;
        update.fileName = fileResult.fileName;
        update.symbolRecords = recordsWithRevisions(
            fileResult.symbolRecords,
            revision,
            pendingWorkspacePublication->documentRevisionsByFile.value(
                normalizedSymbolAnalyzerFileName(fileResult.fileName), 0));
        update.content = fileResult.content;
        updates.append(std::move(update));
        factPublications.append(&fileResult);
        ++pendingWorkspacePublicationFilesAnalyzed;
        progressEvents.append(
            {pendingWorkspacePublicationFilesAnalyzed,
             fileResult.fileName});
        diagnosticFiles.append(fileResult.fileName);
    }

    QElapsedTimer updateTimer;
    updateTimer.start();
    semanticIndex->updateSymbolRecordsForFiles(updates, false);
    for (const WorkspaceFileAnalysis* fileResult :
         std::as_const(factPublications)) {
        if (!fileResult)
            continue;
        EffectiveValueService::getInstance()->publishDocumentFacts(
            fileResult->fileName,
            fileResult->content,
            fileResult->effectiveValueFacts,
            revision,
            pendingWorkspacePublication->documentRevisionsByFile.value(
                normalizedSymbolAnalyzerFileName(fileResult->fileName), 0));
    }
    pendingWorkspacePublicationUpdateMs += updateTimer.elapsed();

    const int filesAnalyzed = pendingWorkspacePublicationFilesAnalyzed;
    const int totalSymbols = pendingWorkspacePublication->totalSymbols;
    const int totalFiles = pendingWorkspacePublicationTotalFiles;
    const QString workspacePath = pendingWorkspacePublicationPath;

    QElapsedTimer finalSnapshotTimer;
    finalSnapshotTimer.start();
    semanticIndex->publishSnapshotReplacingDiagnostics(
        diagnosticFiles,
        pendingWorkspacePublicationDiagnostics);
    pendingWorkspacePublicationFinalSnapshotMs += finalSnapshotTimer.elapsed();
    const qint64 publicationMs = pendingWorkspacePublicationTimer.elapsed();
    emitWorkspaceAnalysisTelemetry(
        workspacePath,
        *pendingWorkspacePublication,
        pendingWorkspacePublicationTotalFiles,
        filesAnalyzed,
        publicationMs,
        pendingWorkspacePublicationUpdateMs,
        pendingWorkspacePublicationFinalSnapshotMs);
    cancelWorkspacePublication();

    for (const auto& progressEvent : std::as_const(progressEvents)) {
        emit batchProgress(progressEvent.first,
                           totalFiles,
                           progressEvent.second);
    }
    emit batchAnalysisCompleted(filesAnalyzed, totalSymbols);
    emit analysisCompleted(workspacePath, totalSymbols);
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
