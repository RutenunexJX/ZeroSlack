#include "symbolanalyzer.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>

void SymbolAnalyzer::invalidateCache()
{
    lastAnalyzedContent.clear();
    fileAnalysisGenerations.clear();
    overlayWorkspaceFiles.clear();
    overlayWorkspaceIncludeDirs.clear();
    overlayWorkspaceDefines.clear();
    ++workspaceEpoch;
    ++workspaceAnalysisGeneration;
    EffectiveValueService::getInstance()->clearPublishedFacts();
}

void SymbolAnalyzer::setWorkspaceFileAnalysisBands(
    const QHash<QString, SemanticAnalysisBandMetadata>& bands)
{
    workspaceFileAnalysisBands = bands;
}

QString SymbolAnalyzer::contentHash(const QString& content) const
{
    return QString::fromLatin1(
        QCryptographicHash::hash(content.toUtf8(),
                                 QCryptographicHash::Sha256)
            .toHex());
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
