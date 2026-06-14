#include "symbolanalyzerworkspace.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QTextStream>
#include <utility>

namespace {

QString normalizedWorkspaceSymbolFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

QHash<QString, QList<sym_list::SymbolInfo>> groupSymbolsByFile(
    const QList<sym_list::SymbolInfo>& list)
{
    QHash<QString, QList<sym_list::SymbolInfo>> byFile;
    for (const sym_list::SymbolInfo& symbol : list) {
        const QString normalized = normalizedWorkspaceSymbolFileName(symbol.fileName);
        if (!normalized.isEmpty())
            byFile[normalized].append(symbol);
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
        fileResult.symbols =
            byFile.value(normalizedWorkspaceSymbolFileName(filePath));
        result.totalSymbols += fileResult.symbols.size();
        result.files.append(std::move(fileResult));
    }

    return result;
}
