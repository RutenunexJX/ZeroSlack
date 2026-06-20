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
