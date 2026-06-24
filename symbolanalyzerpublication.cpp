#include "symbolanalyzer.h"

#include "semanticindex.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <algorithm>

namespace {

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
