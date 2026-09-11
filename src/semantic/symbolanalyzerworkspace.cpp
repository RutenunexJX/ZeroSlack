#include "symbolanalyzerworkspace.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QTextStream>
#include <utility>

namespace {
constexpr qint64 kWorkspaceCachedContentLimitBytes = 512 * 1024;

QString normalizedWorkspaceSymbolFileName(const QString& fileName)
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

QHash<QString, QList<EffectiveValueFact>> groupFactsByFile(
    const QList<EffectiveValueFact>& facts)
{
    QHash<QString, QList<EffectiveValueFact>> byFile;
    for (const EffectiveValueFact& fact : facts) {
        const QString normalized =
            normalizedWorkspaceSymbolFileName(fact.fileName);
        if (!normalized.isEmpty())
            byFile[normalized].append(fact);
    }
    return byFile;
}

QString readTextFileIfSmall(const QString& filePath)
{
    const QFileInfo fileInfo(filePath);
    if (fileInfo.size() > kWorkspaceCachedContentLimitBytes)
        return QString();

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
    const QList<EffectiveValueFact>& effectiveValueFacts,
    std::function<bool()> isCancelled,
    const QHash<QString, QString>& analyzedFileContents)
{
    WorkspaceAnalysisResult result;
    QHash<QString, QList<SemanticSymbolRecord>> byFile =
        groupRecordsByFile(allRecords);
    const QHash<QString, QList<EffectiveValueFact>> factsByFile =
        groupFactsByFile(effectiveValueFacts);
    QHash<QString, QString> contentsByFile;
    for (auto it = analyzedFileContents.constBegin();
         it != analyzedFileContents.constEnd();
         ++it) {
        contentsByFile.insert(
            normalizedWorkspaceSymbolFileName(it.key()), it.value());
    }
    result.files.reserve(svFiles.size());

    for (const QString& filePath : svFiles) {
        if (isCancelled && isCancelled()) {
            result.cancelled = true;
            break;
        }
        WorkspaceFileAnalysis fileResult;
        fileResult.fileName = filePath;
        const QString normalized =
            normalizedWorkspaceSymbolFileName(filePath);
        fileResult.content = contentsByFile.contains(normalized)
            ? contentsByFile.value(normalized)
            : readTextFileIfSmall(filePath);
        fileResult.symbolRecords =
            byFile.value(normalized);
        fileResult.effectiveValueFacts =
            factsByFile.value(normalized);
        result.totalSymbols += fileResult.symbolRecords.size();
        result.files.append(std::move(fileResult));
    }

    return result;
}
