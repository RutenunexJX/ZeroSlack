#include "completionmatcher.h"

#include <Qt>
#include <algorithm>

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

QVector<QPair<QString, int>> CompletionMatcher::scoredKeywordCompletions(
    const QString& prefix)
{
    const QStringList keywords = publicKeywordCompletions();
    QVector<QPair<QString, int>> result;
    result.reserve(keywords.size());

    for (const QString& keyword : keywords) {
        const int score = calculateContextMatchScore(keyword, prefix);
        if (score > 0)
            result.append(qMakePair(keyword, score));
    }

    std::sort(result.begin(), result.end(),
              [](const QPair<QString, int>& left,
                 const QPair<QString, int>& right) {
                  if (left.second != right.second)
                      return left.second > right.second;
                  return left.first < right.first;
              });

    return result;
}

QStringList CompletionMatcher::keywordCompletions(
    const QString& prefix,
    int maxResults)
{
    const QVector<QPair<QString, int>> scored = scoredKeywordCompletions(prefix);

    QStringList result;
    result.reserve(scored.size());
    for (const auto& match : scored)
        result.append(match.first);

    if (maxResults > 0 && result.size() > maxResults)
        result = result.mid(0, maxResults);

    return result;
}

QStringList CompletionMatcher::keywordAbbreviationMatches(
    const QStringList& candidates,
    const QString& abbreviation)
{
    const QVector<QPair<QString, int>> scored =
        scoredKeywordCompletions(abbreviation);

    QStringList result;
    result.reserve(scored.size());
    for (const auto& match : scored) {
        if (candidates.contains(match.first))
            result.append(match.first);
    }

    return result;
}

QStringList CompletionMatcher::svKeywordCompletions(const QString& prefix)
{
    static const QStringList keywords = {
        QStringLiteral("module"), QStringLiteral("endmodule"),
        QStringLiteral("input"), QStringLiteral("output"),
        QStringLiteral("inout"), QStringLiteral("wire"),
        QStringLiteral("reg"), QStringLiteral("logic"),
        QStringLiteral("bit"), QStringLiteral("byte"),
        QStringLiteral("shortint"), QStringLiteral("int"),
        QStringLiteral("longint"), QStringLiteral("always"),
        QStringLiteral("always_ff"), QStringLiteral("always_comb"),
        QStringLiteral("initial"), QStringLiteral("assign"),
        QStringLiteral("case"), QStringLiteral("casex"),
        QStringLiteral("casez"), QStringLiteral("default"),
        QStringLiteral("endcase"), QStringLiteral("if"),
        QStringLiteral("else"), QStringLiteral("for"),
        QStringLiteral("while"), QStringLiteral("repeat"),
        QStringLiteral("forever"), QStringLiteral("task"),
        QStringLiteral("function"), QStringLiteral("endtask"),
        QStringLiteral("endfunction"), QStringLiteral("typedef"),
        QStringLiteral("enum"), QStringLiteral("struct"),
        QStringLiteral("packed"), QStringLiteral("unpacked"),
        QStringLiteral("interface"), QStringLiteral("endinterface"),
        QStringLiteral("modport"), QStringLiteral("generate"),
        QStringLiteral("endgenerate"), QStringLiteral("genvar"),
        QStringLiteral("parameter"), QStringLiteral("localparam"),
        QStringLiteral("`define"), QStringLiteral("`include"),
        QStringLiteral("posedge"), QStringLiteral("negedge"),
        QStringLiteral("and"), QStringLiteral("or"),
        QStringLiteral("not"), QStringLiteral("xor")
    };

    QStringList result;
    for (const QString& keyword : keywords) {
        if (completionNameMatches(keyword, prefix))
            result.append(keyword);
    }
    return result;
}

QStringList CompletionMatcher::publicKeywordCompletions()
{
    return {
        QStringLiteral("always"), QStringLiteral("always_comb"),
        QStringLiteral("always_ff"), QStringLiteral("assign"),
        QStringLiteral("begin"), QStringLiteral("end"),
        QStringLiteral("module"), QStringLiteral("endmodule"),
        QStringLiteral("generate"), QStringLiteral("endgenerate"),
        QStringLiteral("if"), QStringLiteral("else"),
        QStringLiteral("for"), QStringLiteral("define"),
        QStringLiteral("ifdef"), QStringLiteral("ifndef"),
        QStringLiteral("task"), QStringLiteral("endtask"),
        QStringLiteral("initial"), QStringLiteral("reg"),
        QStringLiteral("wire"), QStringLiteral("logic"),
        QStringLiteral("enum"), QStringLiteral("localparam"),
        QStringLiteral("parameter"), QStringLiteral("struct"),
        QStringLiteral("package"), QStringLiteral("endpackage"),
        QStringLiteral("interface"), QStringLiteral("endinterface"),
        QStringLiteral("function"), QStringLiteral("endfunction"),
        QStringLiteral("case"), QStringLiteral("endcase"),
        QStringLiteral("default"), QStringLiteral("posedge"),
        QStringLiteral("negedge"), QStringLiteral("input"),
        QStringLiteral("output"), QStringLiteral("inout")
    };
}

bool CompletionMatcher::completionNameMatches(
    const QString& name,
    const QString& prefix)
{
    if (prefix.isEmpty())
        return true;
    if (name.isEmpty())
        return false;

    const QString lowerName = name.toLower();
    const QString lowerPrefix = prefix.toLower();
    if (lowerName.startsWith(lowerPrefix))
        return true;

    int namePos = 0;
    int prefixPos = 0;
    while (prefixPos < lowerPrefix.length() && namePos < lowerName.length()) {
        if (lowerPrefix.at(prefixPos) == lowerName.at(namePos))
            ++prefixPos;
        ++namePos;
    }
    return prefixPos == lowerPrefix.length();
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
