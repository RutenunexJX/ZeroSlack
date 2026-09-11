#include "completionservice.h"

#include "completionmatcher.h"

bool CompletionService::matchesCompletionAbbreviation(
    const QString& text,
    const QString& abbreviation) const
{
    return CompletionMatcher::matchesAbbreviation(text, abbreviation);
}

int CompletionService::completionItemScore(const QString& text, const QString& prefix) const
{
    return CompletionMatcher::completionItemScore(text, prefix);
}
