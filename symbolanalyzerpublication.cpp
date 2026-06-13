#include "symbolanalyzer.h"

#include "semanticindex.h"

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
    const QList<sym_list::SymbolInfo>& symbols)
{
    SemanticIndex::getInstance()->updateSymbolsForFile(fileName, symbols, content);
}

void SymbolAnalyzer::publishFileAnalysisResult(
    const QString& fileName,
    const QString& content,
    const QList<sym_list::SymbolInfo>& symbols,
    const QList<SemanticDiagnostic>& diagnostics)
{
    SemanticIndex* semanticIndex = SemanticIndex::getInstance();
    semanticIndex->updateSymbolsForFile(fileName, symbols, content);
    semanticIndex->publishSnapshotReplacingDiagnostics({fileName}, diagnostics);
}

int SymbolAnalyzer::publishWorkspaceAnalysisResult(
    const WorkspaceAnalysisResult& result,
    int totalFiles)
{
    SemanticIndex* semanticIndex = SemanticIndex::getInstance();
    int filesAnalyzed = 0;
    for (const WorkspaceFileAnalysis& fileResult : result.files) {
        updateFileSymbols(
            fileResult.fileName,
            fileResult.content,
            fileResult.symbols);
        filesAnalyzed++;
        emit batchProgress(filesAnalyzed, totalFiles, fileResult.fileName);
    }

    semanticIndex->publishCompleteSnapshot(result.diagnostics);
    return filesAnalyzed;
}
