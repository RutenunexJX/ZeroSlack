#include "symbolanalyzer.h"

#include <QFileInfo>
#include <algorithm>

bool SymbolAnalyzer::isAnalysisNeeded(const QString& fileName, const QString& content) const
{
    if (!lastAnalyzedContent.contains(fileName)) return true;

    return lastAnalyzedContent[fileName] != content;
}

void SymbolAnalyzer::invalidateCache()
{
    lastAnalyzedContent.clear();
    fileAnalysisGenerations.clear();
    ++workspaceAnalysisGeneration;
}

void SymbolAnalyzer::setWorkspaceProtectedFiles(const QStringList& fileNames)
{
    workspaceProtectedFiles = fileNames;
}

void SymbolAnalyzer::setWorkspacePriorityFileCount(int fileCount)
{
    setWorkspacePriorityPublicationCheckpoints(
        fileCount > 0 ? QList<int>{fileCount} : QList<int>{});
}

void SymbolAnalyzer::setWorkspacePriorityPublicationCheckpoints(
    const QList<int>& checkpoints)
{
    QList<int> sortedCheckpoints = checkpoints;
    std::sort(sortedCheckpoints.begin(), sortedCheckpoints.end());

    workspacePriorityPublicationCheckpoints.clear();
    int lastCheckpoint = 0;
    for (int checkpoint : sortedCheckpoints) {
        if (checkpoint <= 0 || checkpoint <= lastCheckpoint)
            continue;
        workspacePriorityPublicationCheckpoints.append(checkpoint);
        lastCheckpoint = checkpoint;
    }
}

QString SymbolAnalyzer::contentHash(const QString& content) const
{
    return QString::number(qHash(content));
}

QStringList SymbolAnalyzer::filterSystemVerilogFiles(const QStringList& files) const
{
    QStringList svFiles;
    svFiles.reserve(files.size());

    for (const QString& fileName : files) {
        if (isSystemVerilogFile(fileName)) {
            svFiles.append(fileName);
        }
    }
    return svFiles;
}

static bool lineContainsKeywordAsWord(const QString& line, const QString& keyword)
{
    int pos = 0;
    while ((pos = line.indexOf(keyword, pos)) >= 0) {
        bool startOk = (pos == 0) || (!line[pos - 1].isLetterOrNumber() && line[pos - 1] != QLatin1Char('_'));
        bool endOk = (pos + keyword.size() >= line.size()) || (!line[pos + keyword.size()].isLetterOrNumber() && line[pos + keyword.size()] != QLatin1Char('_'));
        if (startOk && endOk)
            return true;
        pos += keyword.size();
    }
    return false;
}

bool SymbolAnalyzer::hasSignificantChanges(const QString& oldContent, const QString& newContent) const
{
    static const QStringList significantKeywords = {
        QLatin1String("module"), QLatin1String("endmodule"), QLatin1String("reg"), QLatin1String("wire"), QLatin1String("logic"),
        QLatin1String("task"), QLatin1String("endtask"), QLatin1String("function"), QLatin1String("endfunction")
    };

    auto significantLines = [&](const QString& content) {
        QStringList out;
        const QStringList lines = content.split(QLatin1Char('\n'));
        for (const QString& raw : lines) {
            const QString line = raw.trimmed();
            if (line.isEmpty())
                continue;
            for (const QString& keyword : significantKeywords) {
                if (lineContainsKeywordAsWord(line, keyword)) {
                    out.append(line);
                    break;
                }
            }
        }
        out.sort();
        return out;
    };

    return significantLines(oldContent) != significantLines(newContent);
}

bool SymbolAnalyzer::isSystemVerilogFile(const QString& fileName) const
{
    if (fileName.isEmpty()) return false;

    static const QStringList svExtensions = {"sv", "v", "vh", "svh", "vp", "svp"};
    const QString suffix = QFileInfo(fileName).suffix().toLower();
    return svExtensions.contains(suffix);
}
