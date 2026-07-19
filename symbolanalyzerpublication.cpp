#include "symbolanalyzer.h"

#include "semanticindex.h"
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

} // namespace

void SymbolAnalyzer::publishOpenDocumentResults(
    const QStringList& fileNames,
    const QList<SemanticDiagnostic>& diagnostics)
{
    SemanticIndex::getInstance()->publishSnapshotReplacingDiagnostics(
        fileNames,
        diagnostics);
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
    semanticIndex->updateSymbolRecordsForFile(
        fileName,
        recordsWithRevisions(symbolRecords, revision, documentRevision),
        content);
    semanticIndex->publishSnapshotReplacingDiagnostics({fileName}, diagnostics);
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
    semanticIndex->publishSnapshotReplacingDiagnostics(
        result.diagnosticFiles.isEmpty()
            ? QStringList{result.fileName}
            : result.diagnosticFiles,
        result.diagnostics);
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

    diagnostics = result.diagnostics;

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
    cancelWorkspacePublication();

    QStringList publicationFiles;
    publicationFiles.reserve(result.files.size());
    for (const WorkspaceFileAnalysis& fileResult : result.files)
        publicationFiles.append(fileResult.fileName);
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
    if (!pendingWorkspacePublication) {
        if (workspacePublicationTimer)
            workspacePublicationTimer->stop();
        return;
    }

    QStringList publicationFiles;
    publicationFiles.reserve(pendingWorkspacePublication->files.size());
    for (const WorkspaceFileAnalysis& fileResult :
         std::as_const(pendingWorkspacePublication->files)) {
        publicationFiles.append(fileResult.fileName);
    }
    if (!EffectiveValueService::getInstance()->isComputationCurrent(
            publicationFiles,
            pendingWorkspacePublication->analysisRevision)) {
        cancelWorkspacePublication();
        emit workspaceAnalysisExpired();
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
    telemetry.diagnostics = result.diagnostics.size();
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
