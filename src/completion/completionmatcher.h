#ifndef COMPLETIONMATCHER_H
#define COMPLETIONMATCHER_H

#include <QList>
#include <QString>

class CompletionMatcher
{
public:
    static bool matchesAbbreviation(const QString& text, const QString& abbreviation);
    static int completionItemScore(const QString& text, const QString& prefix);

private:
    static int calculateContextMatchScore(const QString& text,
                                          const QString& abbreviation);
    static QList<int> abbreviationPositions(const QString& text,
                                            const QString& abbreviation);
    static bool isValidContextAbbreviationMatch(
        const QString& text,
        const QString& abbreviation);
};

#endif // COMPLETIONMATCHER_H
