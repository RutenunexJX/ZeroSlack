#include "symbolanalyzer.h"

#include "semanticindex.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSet>
#include <QTimer>
#include <algorithm>
#include <utility>

namespace {
constexpr int kWorkspacePublicationChunkFiles = 12;
constexpr qint64 kWorkspacePublicationChunkBudgetMs = 8;

QString normalizedSymbolAnalyzerFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

QSet<QString> normalizedFileSet(const QStringList& fileNames)
{
    QSet<QString> result;
    for (const QString& fileName : fileNames) {
        const QString normalized = normalizedSymbolAnalyzerFileName(fileName);
        if (!normalized.isEmpty())
            result.insert(normalized);
    }
    return result;
}

bool fileInSet(const QSet<QString>& files, const QString& fileName)
{
    return files.contains(normalizedSymbolAnalyzerFileName(fileName));
}

QList<int> publicationCheckpoints(const QList<int>& checkpoints,
                                  int resultFileCount)
{
    QList<int> result;
    int lastCheckpoint = 0;
    for (int checkpoint : checkpoints) {
        const int clamped = std::clamp(checkpoint, 0, resultFileCount);
        if (clamped <= 0
            || clamped >= resultFileCount
            || clamped <= lastCheckpoint) {
            continue;
        }
        result.append(clamped);
        lastCheckpoint = clamped;
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
    const QList<SemanticSymbolRecord>& symbolRecords)
{
    SemanticIndex::getInstance()->updateSymbolRecordsForFile(fileName,
                                                             symbolRecords,
                                                             content);
}

void SymbolAnalyzer::publishFileAnalysisResult(
    const QString& fileName,
    const QString& content,
    const QList<SemanticSymbolRecord>& symbolRecords,
    const QList<SemanticDiagnostic>& diagnostics)
{
    SemanticIndex* semanticIndex = SemanticIndex::getInstance();
    semanticIndex->updateSymbolRecordsForFile(fileName, symbolRecords, content);
    semanticIndex->publishSnapshotReplacingDiagnostics({fileName}, diagnostics);
}

int SymbolAnalyzer::publishWorkspaceAnalysisResult(
    const WorkspaceAnalysisResult& result,
    int totalFiles)
{
    SemanticIndex* semanticIndex = SemanticIndex::getInstance();
    semanticIndex->setWorkspaceFileAnalysisBands(result.fileAnalysisBands);
    const QSet<QString> protectedFiles = normalizedFileSet(result.protectedFiles);
    const int resultFileCount = static_cast<int>(result.files.size());
    const QList<int> checkpoints =
        publicationCheckpoints(result.priorityPublicationCheckpoints,
                               resultFileCount);
    QStringList analyzedFiles;
    analyzedFiles.reserve(result.files.size());
    QList<SemanticDiagnostic> diagnostics;
    int filesAnalyzed = 0;
    int plannedFilesVisited = 0;
    int nextCheckpointIndex = 0;
    int lastPublishedFilesAnalyzed = 0;
    auto publishCrossedCheckpoints = [&]() {
        while (nextCheckpointIndex < checkpoints.size()
               && plannedFilesVisited >= checkpoints.at(nextCheckpointIndex)) {
            ++nextCheckpointIndex;
            if (filesAnalyzed <= lastPublishedFilesAnalyzed)
                continue;
            semanticIndex->setSnapshot(
                semanticIndex->captureSnapshotPreservingDiagnostics());
            lastPublishedFilesAnalyzed = filesAnalyzed;
        }
    };
    for (const WorkspaceFileAnalysis& fileResult : result.files) {
        ++plannedFilesVisited;
        const bool protectedFile =
            fileInSet(protectedFiles, fileResult.fileName);
        if (protectedFile) {
            publishCrossedCheckpoints();
            continue;
        }

        updateFileSymbols(
            fileResult.fileName,
            fileResult.content,
            fileResult.symbolRecords);
        analyzedFiles.append(fileResult.fileName);
        filesAnalyzed++;
        publishCrossedCheckpoints();
        emit batchProgress(filesAnalyzed, totalFiles, fileResult.fileName);
    }

    for (const SemanticDiagnostic& diagnostic : result.diagnostics) {
        if (!fileInSet(protectedFiles, diagnostic.fileName))
            diagnostics.append(diagnostic);
    }

    semanticIndex->publishSnapshotReplacingDiagnostics(analyzedFiles,
                                                       diagnostics);
    return filesAnalyzed;
}

void SymbolAnalyzer::startWorkspacePublication(
    WorkspaceAnalysisResult result,
    int totalFiles,
    const QString& workspacePath)
{
    cancelWorkspacePublication();

    pendingWorkspacePublication =
        std::make_unique<WorkspaceAnalysisResult>(std::move(result));
    pendingWorkspacePublicationTotalFiles = totalFiles;
    pendingWorkspacePublicationPath = workspacePath;
    pendingWorkspacePublicationProtectedFiles =
        normalizedFileSet(pendingWorkspacePublication->protectedFiles);
    pendingWorkspacePublicationCheckpoints =
        publicationCheckpoints(
            pendingWorkspacePublication->priorityPublicationCheckpoints,
            pendingWorkspacePublication->files.size());
    pendingWorkspacePublicationAnalyzedFiles.reserve(
        pendingWorkspacePublication->files.size());
    pendingWorkspacePublicationDiagnostics.reserve(
        pendingWorkspacePublication->diagnostics.size());

    for (const SemanticDiagnostic& diagnostic :
         std::as_const(pendingWorkspacePublication->diagnostics)) {
        if (!fileInSet(pendingWorkspacePublicationProtectedFiles,
                       diagnostic.fileName)) {
            pendingWorkspacePublicationDiagnostics.append(diagnostic);
        }
    }

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
    pendingWorkspacePublicationIndex = 0;
    pendingWorkspacePublicationFilesAnalyzed = 0;
    pendingWorkspacePublicationPlannedVisited = 0;
    pendingWorkspacePublicationLastSnapshotFiles = 0;
    pendingWorkspacePublicationNextCheckpoint = 0;
    pendingWorkspacePublicationProtectedFiles.clear();
    pendingWorkspacePublicationCheckpoints.clear();
    pendingWorkspacePublicationAnalyzedFiles.clear();
    pendingWorkspacePublicationDiagnostics.clear();
}

void SymbolAnalyzer::processWorkspacePublicationChunk()
{
    if (!pendingWorkspacePublication) {
        if (workspacePublicationTimer)
            workspacePublicationTimer->stop();
        return;
    }

    SemanticIndex* semanticIndex = SemanticIndex::getInstance();
    QElapsedTimer chunkTimer;
    chunkTimer.start();
    QList<SemanticFileSymbolUpdate> updates;
    updates.reserve(kWorkspacePublicationChunkFiles);
    QList<QPair<int, QString>> progressEvents;
    progressEvents.reserve(kWorkspacePublicationChunkFiles);
    bool crossedCheckpoint = false;
    int filesThisChunk = 0;

    auto noteCrossedCheckpoints = [&]() {
        while (pendingWorkspacePublicationNextCheckpoint
                   < pendingWorkspacePublicationCheckpoints.size()
               && pendingWorkspacePublicationPlannedVisited
                      >= pendingWorkspacePublicationCheckpoints.at(
                          pendingWorkspacePublicationNextCheckpoint)) {
            ++pendingWorkspacePublicationNextCheckpoint;
            crossedCheckpoint = true;
        }
    };

    while (pendingWorkspacePublicationIndex
           < pendingWorkspacePublication->files.size()) {
        const WorkspaceFileAnalysis& fileResult =
            pendingWorkspacePublication->files.at(
                pendingWorkspacePublicationIndex);
        ++pendingWorkspacePublicationIndex;
        ++pendingWorkspacePublicationPlannedVisited;
        ++filesThisChunk;

        const bool protectedFile =
            fileInSet(pendingWorkspacePublicationProtectedFiles,
                      fileResult.fileName);
        if (protectedFile) {
            noteCrossedCheckpoints();
        } else {
            SemanticFileSymbolUpdate update;
            update.fileName = fileResult.fileName;
            update.symbolRecords = fileResult.symbolRecords;
            update.content = fileResult.content;
            updates.append(std::move(update));
            pendingWorkspacePublicationAnalyzedFiles.append(
                fileResult.fileName);
            ++pendingWorkspacePublicationFilesAnalyzed;
            progressEvents.append(
                {pendingWorkspacePublicationFilesAnalyzed,
                 fileResult.fileName});
            noteCrossedCheckpoints();
        }

        if (filesThisChunk >= kWorkspacePublicationChunkFiles
            || chunkTimer.elapsed() >= kWorkspacePublicationChunkBudgetMs) {
            break;
        }
    }

    semanticIndex->updateSymbolRecordsForFiles(updates, false);
    for (const auto& progressEvent : std::as_const(progressEvents)) {
        emit batchProgress(progressEvent.first,
                           pendingWorkspacePublicationTotalFiles,
                           progressEvent.second);
    }

    if (crossedCheckpoint
        && pendingWorkspacePublicationFilesAnalyzed
               > pendingWorkspacePublicationLastSnapshotFiles) {
        semanticIndex->setSnapshot(
            semanticIndex->captureSnapshotPreservingDiagnostics());
        pendingWorkspacePublicationLastSnapshotFiles =
            pendingWorkspacePublicationFilesAnalyzed;
    }

    if (pendingWorkspacePublicationIndex
        < pendingWorkspacePublication->files.size()) {
        return;
    }

    const int filesAnalyzed = pendingWorkspacePublicationFilesAnalyzed;
    const int totalSymbols = pendingWorkspacePublication->totalSymbols;
    const QString workspacePath = pendingWorkspacePublicationPath;

    semanticIndex->publishSnapshotReplacingDiagnostics(
        pendingWorkspacePublicationAnalyzedFiles,
        pendingWorkspacePublicationDiagnostics);
    cancelWorkspacePublication();

    emit batchAnalysisCompleted(filesAnalyzed, totalSymbols);
    emit analysisCompleted(workspacePath, totalSymbols);
}
