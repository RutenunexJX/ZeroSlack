#include "completionservice.h"

#include "completionmatcher.h"

bool CompletionService::matchesCompletionAbbreviation(
    const QString& text,
    const QString& abbreviation) const
{
    return CompletionMatcher::matchesAbbreviation(text, abbreviation);
}

int CompletionService::calculateCompletionMatchScore(
    const QString& text,
    const QString& abbreviation) const
{
    return CompletionMatcher::calculateContextMatchScore(text, abbreviation);
}

int CompletionService::completionItemScore(const QString& text, const QString& prefix) const
{
    return CompletionMatcher::completionItemScore(text, prefix);
}

QList<int> CompletionService::findCompletionAbbreviationPositions(
    const QString& text,
    const QString& abbreviation) const
{
    return CompletionMatcher::abbreviationPositions(text, abbreviation);
}

QVector<QPair<QString, int>> CompletionService::findScoredKeywordCompletions(
    const QString& prefix) const
{
    return CompletionMatcher::scoredKeywordCompletions(prefix);
}

QStringList CompletionService::findKeywordCompletions(
    const QString& prefix,
    int maxResults) const
{
    return CompletionMatcher::keywordCompletions(prefix, maxResults);
}

QStringList CompletionService::findKeywordAbbreviationMatches(
    const QStringList& candidates,
    const QString& abbreviation) const
{
    return CompletionMatcher::keywordAbbreviationMatches(candidates, abbreviation);
}
