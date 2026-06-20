#include "syminfo.h"

#include <QRegularExpression>

#include <algorithm>

bool sym_list::isPositionInComment(int position)
{
    auto it = std::lower_bound(commentRegions.begin(), commentRegions.end(), position,
        [](const CommentRegion& region, int pos) {
            return region.endPos <= pos;
        });

    return it != commentRegions.end() && position >= it->startPos;
}

void sym_list::calculateLineColumn(const QString &text, int position, int &line, int &column)
{
    line = 0;
    column = 0;
    const int maxPos = qMin(position, text.length());

    for (int i = 0; i < maxPos; i++) {
        if (text[i] == '\n') {
            line++;
            column = 0;
        } else {
            column++;
        }
    }
}

bool sym_list::isMatchInComment(int matchStart, int matchLength)
{
    const int matchEnd = matchStart + matchLength;
    auto it = std::lower_bound(commentRegions.begin(), commentRegions.end(), matchStart,
        [](const CommentRegion& region, int pos) {
            return region.endPos <= pos;
        });
    for (auto iter = it; iter != commentRegions.end() && iter->startPos < matchEnd; ++iter) {
        if (matchStart < iter->endPos && matchEnd > iter->startPos) {
            return true;
        }
    }
    return false;
}

bool sym_list::isPositionInMultiLineComment(int pos)
{
    return isPositionInComment(pos);
}

QList<sym_list::CommentRegion> sym_list::getCommentRegions() const
{
    return commentRegions;
}

QString sym_list::calculateContentHash(const QString& content)
{
    return QString::number(qHash(content));
}

QString sym_list::calculateSymbolRelevantHash(const QString& content)
{
    QString work = content;
    int i = 0;
    while (i < work.length()) {
        int start = work.indexOf("/*", i);
        if (start < 0)
            break;
        int end = work.indexOf("*/", start + 2);
        if (end < 0)
            end = work.length();
        work.replace(start, end - start + 2, " ");
        i = start + 1;
    }
    QStringList lines = work.split('\n');
    QStringList kept;
    for (const QString& line : std::as_const(lines)) {
        QString t = line.trimmed();
        if (t.isEmpty() || t.startsWith("//"))
            continue;
        kept.append(QString(t).replace(QRegularExpression("\\s+"), " "));
    }
    QString joined = kept.join(" ").trimmed();
    return QString::number(qHash(joined));
}

bool sym_list::needsAnalysis(const QString& fileName, const QString& content)
{
    if (!fileStates.contains(fileName))
        return true;

    int currentLineCount = content.count('\n') + 1;
    if (fileStates[fileName].lastAnalyzedLineCount != currentLineCount)
        return true;

    QString newSymbolHash = calculateSymbolRelevantHash(content);
    const QString& stored = fileStates[fileName].symbolRelevantHash;
    if (stored.isEmpty())
        return true;
    return newSymbolHash != stored;
}

bool sym_list::contentAffectsSymbols(const QString& fileName, const QString& content)
{
    return needsAnalysis(fileName, content);
}

void sym_list::updateLineBasedSymbols(const SymbolInfo& symbol)
{
    lineBasedSymbols[symbol.fileName][symbol.startLine].append(symbol);
}

QList<int> sym_list::detectChangedLines(const QString& fileName, const QString& newContent)
{
    QList<int> changedLines;

    if (!previousFileContents.contains(fileName)) {
        previousFileContents[fileName] = newContent;

        return changedLines;
    }

    QString oldContent = previousFileContents[fileName];
    QStringList oldLines = oldContent.split('\n');
    QStringList newLines = newContent.split('\n');

    int maxLines = qMax(oldLines.size(), newLines.size());
    int actualChanges = 0;

    for (int i = 0; i < maxLines; ++i) {
        QString oldLine = (i < oldLines.size()) ? oldLines[i] : QString();
        QString newLine = (i < newLines.size()) ? newLines[i] : QString();

        if (oldLine.trimmed() != newLine.trimmed()) {
            changedLines.append(i);
            actualChanges++;

            static QStringList declarationKeywords = {"module", "reg", "wire", "logic", "task", "function"};
            for (const QString &keyword : declarationKeywords) {
                if (newLine.contains(QRegularExpression("\\b" + QRegularExpression::escape(keyword) + "\\b"))) {
                    if (i > 0 && !changedLines.contains(i - 1)) {
                        changedLines.append(i - 1);
                    }
                    if (i < maxLines - 1 && !changedLines.contains(i + 1)) {
                        changedLines.append(i + 1);
                    }
                    break;
                }
            }
        }
    }

    previousFileContents[fileName] = newContent;
    {
        QSet<int> uniqueLines(changedLines.begin(), changedLines.end());
        changedLines = QList<int>(uniqueLines.begin(), uniqueLines.end());
    }
    std::sort(changedLines.begin(), changedLines.end());

    return changedLines;
}

void sym_list::clearSymbolsForLines(const QString& fileName, const QList<int>& lines)
{
    int removedCount = 0;

    if (lineBasedSymbols.contains(fileName)) {
        for (int lineNum : lines) {
            if (lineBasedSymbols[fileName].contains(lineNum)) {
                removedCount += lineBasedSymbols[fileName][lineNum].size();
                lineBasedSymbols[fileName].remove(lineNum);
            }
        }
    }

    QList<int> indicesToRemove;

    if (fileNameIndex.contains(fileName)) {
        const QList<int>& fileIndices = fileNameIndex[fileName];

        for (int index : fileIndices) {
            if (index < symbolDatabase.size()) {
                const SymbolInfo& symbol = symbolDatabase[index];
                if (lines.contains(symbol.startLine)) {
                    indicesToRemove.append(index);
                }
            }
        }
    }

    std::sort(indicesToRemove.begin(), indicesToRemove.end(), std::greater<int>());

    for (int index : indicesToRemove) {
        if (index < symbolDatabase.size()) {
            removeFromIndexes(index);
            symbolDatabase.removeAt(index);
        }
    }

    if (!indicesToRemove.isEmpty()) {
        rebuildAllIndexes();
    }
}
