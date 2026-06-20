#include "symbolanalyzer.h"

#include "semanticindex.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>

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
    QStringList analyzedFiles;
    analyzedFiles.reserve(result.files.size());
    QList<SemanticDiagnostic> diagnostics;
    int filesAnalyzed = 0;
    for (const WorkspaceFileAnalysis& fileResult : result.files) {
        if (fileInSet(protectedFiles, fileResult.fileName))
            continue;

        updateFileSymbols(
            fileResult.fileName,
            fileResult.content,
            fileResult.symbolRecords);
        analyzedFiles.append(fileResult.fileName);
        filesAnalyzed++;
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
