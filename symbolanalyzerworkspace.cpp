#include "symbolanalyzerworkspace.h"

#include <QFile>
#include <QHash>
#include <QTextStream>
#include <utility>

namespace {

QHash<QString, QList<sym_list::SymbolInfo>> groupSymbolsByFile(
    const QList<sym_list::SymbolInfo>& list)
{
    QHash<QString, QList<sym_list::SymbolInfo>> byFile;
    for (const sym_list::SymbolInfo& symbol : list) {
        if (!symbol.fileName.isEmpty())
            byFile[symbol.fileName].append(symbol);
    }
    return byFile;
}

QString readTextFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QFile::Text))
        return QString();
    QTextStream stream(&file);
    return stream.readAll();
}

} // namespace

WorkspaceAnalysisResult SymbolAnalyzerWorkspace::buildWorkspaceAnalysisResult(
    const QStringList& svFiles,
    const QList<sym_list::SymbolInfo>& allSymbols,
    std::function<bool()> isCancelled)
{
    WorkspaceAnalysisResult result;
    QHash<QString, QList<sym_list::SymbolInfo>> byFile = groupSymbolsByFile(allSymbols);
    result.files.reserve(svFiles.size());

    for (const QString& filePath : svFiles) {
        if (isCancelled && isCancelled())
            break;
        WorkspaceFileAnalysis fileResult;
        fileResult.fileName = filePath;
        fileResult.content = readTextFile(filePath);
        fileResult.symbols = byFile.value(filePath);
        result.totalSymbols += fileResult.symbols.size();
        result.files.append(std::move(fileResult));
    }

    return result;
}
