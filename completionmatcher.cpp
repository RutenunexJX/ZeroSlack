#include "completionmatcher.h"

#include <Qt>

bool CompletionMatcher::matchesAbbreviation(
    const QString& text,
    const QString& abbreviation)
{
    if (abbreviation.isEmpty() || text.isEmpty())
        return false;

    if (text.toLower().startsWith(abbreviation.toLower()))
        return true;

    return isValidContextAbbreviationMatch(text, abbreviation);
}

int CompletionMatcher::calculateContextMatchScore(
    const QString& text,
    const QString& abbreviation)
{
    if (abbreviation.isEmpty() || text.isEmpty())
        return 0;

    const QString lowerText = text.toLower();
    const QString lowerAbbreviation = abbreviation.toLower();

    if (lowerText == lowerAbbreviation)
        return 1000;
    if (lowerText.startsWith(lowerAbbreviation))
        return 800 + (100 - abbreviation.length());
    if (lowerText.contains(lowerAbbreviation))
        return 400 + (100 - text.length());
    if (!isValidContextAbbreviationMatch(text, abbreviation))
        return 0;

    const QList<int> positions = abbreviationPositions(text, abbreviation);
    int score = 500;
    int wordBoundaryMatches = 0;
    for (int position : positions) {
        if (position == 0
            || lowerText.at(position - 1) == QLatin1Char('_')
            || lowerText.at(position - 1) == QLatin1Char(' ')) {
            ++wordBoundaryMatches;
        }
        if (position > 0 && position < lowerText.length()) {
            const QChar previous = text.at(position - 1);
            const QChar current = text.at(position);
            if (previous.isLower() && current.isUpper())
                ++wordBoundaryMatches;
        }
    }

    score += wordBoundaryMatches * 50;
    score -= text.length();
    for (int i = 1; i < positions.size(); ++i) {
        if (positions.at(i) == positions.at(i - 1) + 1)
            score += 10;
    }
    return score;
}

int CompletionMatcher::calculateSymbolTypeScore(
    const QString& text,
    const QString& abbreviation)
{
    if (text.isEmpty())
        return 0;
    if (abbreviation.isEmpty())
        return 100;

    const QString lowerText = text.toLower();
    const QString lowerAbbreviation = abbreviation.toLower();
    if (lowerText == lowerAbbreviation)
        return 1000;
    if (lowerText.startsWith(lowerAbbreviation))
        return 800 + (100 - abbreviation.length());
    if (lowerText.contains(lowerAbbreviation))
        return 400 + (100 - text.length());
    if (isValidContextAbbreviationMatch(text, abbreviation))
        return 200;
    return 0;
}

int CompletionMatcher::completionItemScore(
    const QString& text,
    const QString& prefix)
{
    return prefix.isEmpty() ? 100 : calculateContextMatchScore(text, prefix);
}

QList<int> CompletionMatcher::abbreviationPositions(
    const QString& text,
    const QString& abbreviation)
{
    QList<int> positions;
    if (!isValidContextAbbreviationMatch(text, abbreviation))
        return positions;

    const QString lowerText = text.toLower();
    const QString lowerAbbreviation = abbreviation.toLower();
    int textPosition = 0;
    int abbreviationPosition = 0;
    while (abbreviationPosition < lowerAbbreviation.length()
           && textPosition < lowerText.length()) {
        if (lowerAbbreviation.at(abbreviationPosition)
            == lowerText.at(textPosition)) {
            positions.append(textPosition);
            ++abbreviationPosition;
        } else {
            bool separator = text.at(textPosition) == QLatin1Char('_')
                || text.at(textPosition) == QLatin1Char(' ');
            if (textPosition > 0) {
                const QChar previous = text.at(textPosition - 1);
                const QChar current = text.at(textPosition);
                if (previous.isLower() && current.isUpper())
                    separator = true;
            }
            if (separator && textPosition + 1 < text.length()
                && lowerAbbreviation.at(abbreviationPosition)
                    == lowerText.at(textPosition + 1)) {
                ++textPosition;
                continue;
            }
        }
        ++textPosition;
    }
    return positions;
}

bool CompletionMatcher::isValidContextAbbreviationMatch(
    const QString& text,
    const QString& abbreviation)
{
    if (abbreviation.length() > text.length())
        return false;

    const QString lowerText = text.toLower();
    const QString lowerAbbreviation = abbreviation.toLower();
    int textPosition = 0;
    int abbreviationPosition = 0;
    while (abbreviationPosition < lowerAbbreviation.length()
           && textPosition < text.length()) {
        const QChar abbreviationChar = lowerAbbreviation.at(abbreviationPosition);
        const QChar textChar = lowerText.at(textPosition);
        if (abbreviationChar == textChar) {
            ++abbreviationPosition;
            ++textPosition;
            continue;
        }

        bool separator = text.at(textPosition) == QLatin1Char('_')
            || text.at(textPosition) == QLatin1Char(' ');
        if (textPosition > 0) {
            const QChar previous = text.at(textPosition - 1);
            const QChar current = text.at(textPosition);
            if (previous.isLower() && current.isUpper())
                separator = true;
        }
        if (separator && textPosition + 1 < text.length()
            && abbreviationChar == lowerText.at(textPosition + 1)) {
            ++textPosition;
            continue;
        }
        ++textPosition;
    }
    return abbreviationPosition == lowerAbbreviation.length();
}
