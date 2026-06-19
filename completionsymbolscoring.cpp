#include "completionsymbolquery.h"

#include "completionmatcher.h"

#include <QSet>
#include <Qt>
#include <algorithm>

QVector<QPair<QString, int>> CompletionSymbolQuery::scoredNames(
    const QStringList& names,
    const QString& prefix,
    int maxResults)
{
    QVector<QPair<QString, int>> scored;
    scored.reserve(qMin(names.size(), maxResults > 0 ? maxResults : names.size()));
    for (const QString& name : names) {
        const int score = CompletionMatcher::calculateContextMatchScore(name, prefix);
        if (score > 0)
            scored.append(qMakePair(name, score));
    }

    std::sort(scored.begin(), scored.end(),
              [](const QPair<QString, int>& left,
                 const QPair<QString, int>& right) {
                  if (left.second != right.second)
                      return left.second > right.second;
                  return left.first < right.first;
              });

    if (maxResults > 0 && scored.size() > maxResults)
        scored = scored.mid(0, maxResults);

    return scored;
}

QVector<QPair<QString, int>>
CompletionSymbolQuery::scoredTypedSymbolNames(
    SemanticIndex* semanticIndex,
    sym_list::sym_type_e symbolType,
    const QString& prefix,
    int maxResults)
{
    QVector<QPair<QString, int>> scored;
    if (!semanticIndex)
        return scored;

    const QList<sym_list::SymbolInfo> symbols =
        semanticIndex->getTypedCompletionSymbols(symbolType);

    scored.reserve(qMin(symbols.size(), maxResults > 0 ? maxResults : symbols.size()));
    for (const sym_list::SymbolInfo& symbol : symbols) {
        const int score =
            CompletionMatcher::calculateSymbolTypeScore(symbol.symbolName, prefix);
        if (score > 0)
            scored.append(qMakePair(symbol.symbolName, score));
    }

    std::sort(scored.begin(), scored.end(),
              [](const QPair<QString, int>& left,
                 const QPair<QString, int>& right) {
                  if (left.second != right.second)
                      return left.second > right.second;
                  return left.first < right.first;
              });

    QVector<QPair<QString, int>> result;
    result.reserve(scored.size());
    QSet<QString> seenNames;
    for (const auto& match : scored) {
        const QString key = match.first.toCaseFolded();
        if (seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(match);
    }

    if (maxResults > 0 && result.size() > maxResults)
        result = result.mid(0, maxResults);

    return result;
}

QStringList CompletionSymbolQuery::namesFromScored(
    const QVector<QPair<QString, int>>& scored,
    int maxResults)
{
    QStringList result;
    result.reserve(scored.size());
    for (const auto& match : scored)
        result.append(match.first);

    if (maxResults > 0 && result.size() > maxResults)
        result = result.mid(0, maxResults);

    return result;
}

QStringList CompletionSymbolQuery::namesFromSymbols(
    const QList<sym_list::SymbolInfo>& symbols)
{
    QStringList result;
    QSet<QString> seenNames;
    for (const sym_list::SymbolInfo& symbol : symbols) {
        const QString key = symbol.symbolName.toCaseFolded();
        if (seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(symbol.symbolName);
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

QStringList CompletionSymbolQuery::namesFromRecords(
    const QList<SemanticSymbolRecord>& records)
{
    QStringList result;
    QSet<QString> seenNames;
    for (const SemanticSymbolRecord& record : records) {
        const QString key = record.name.toCaseFolded();
        if (key.isEmpty() || seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(record.name);
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

bool CompletionSymbolQuery::nameMatches(const QString& name, const QString& prefix)
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
