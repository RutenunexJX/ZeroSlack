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

QHash<QString, QList<SemanticSymbolRecord>> groupRecordsByFile(
    const QList<SemanticSymbolRecord>& records)
{
    QHash<QString, QList<SemanticSymbolRecord>> byFile;
    for (const SemanticSymbolRecord& record : records) {
        const QString normalized =
            normalizedWorkspaceSymbolFileName(record.location.fileName);
        if (!normalized.isEmpty())
            byFile[normalized].append(record);
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
    const QList<SemanticSymbolRecord>& allRecords,
    std::function<bool()> isCancelled)
{
    WorkspaceAnalysisResult result;
    QHash<QString, QList<SemanticSymbolRecord>> byFile =
        groupRecordsByFile(allRecords);
    result.files.reserve(svFiles.size());

    for (const QString& filePath : svFiles) {
        if (isCancelled && isCancelled()) {
            result.cancelled = true;
            break;
        }
        WorkspaceFileAnalysis fileResult;
        fileResult.fileName = filePath;
        fileResult.content = readTextFile(filePath);
        fileResult.symbolRecords =
            byFile.value(normalizedWorkspaceSymbolFileName(filePath));
        result.totalSymbols += fileResult.symbolRecords.size();
        result.files.append(std::move(fileResult));
    }

    return result;
}
