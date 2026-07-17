#ifndef COMPLETIONMATCHER_H
#define COMPLETIONMATCHER_H

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

class CompletionMatcher
{
public:
    static bool matchesAbbreviation(const QString& text, const QString& abbreviation);
    static int calculateContextMatchScore(const QString& text, const QString& abbreviation);
    static int completionItemScore(const QString& text, const QString& prefix);
    static QList<int> abbreviationPositions(const QString& text, const QString& abbreviation);
    static QVector<QPair<QString, int>> scoredKeywordCompletions(const QString& prefix);
    static QStringList keywordCompletions(const QString& prefix, int maxResults);
    static QStringList keywordAbbreviationMatches(
        const QStringList& candidates,
        const QString& abbreviation);
    static QStringList svKeywordCompletions(const QString& prefix);

private:
    static QStringList publicKeywordCompletions();
    static bool completionNameMatches(const QString& name, const QString& prefix);
    static bool isValidContextAbbreviationMatch(
        const QString& text,
        const QString& abbreviation);
};

#endif // COMPLETIONMATCHER_H
