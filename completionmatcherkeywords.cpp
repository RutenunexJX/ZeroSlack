#include "completionmatcher.h"

#include <algorithm>

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
