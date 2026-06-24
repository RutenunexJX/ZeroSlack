#include "completionsymbolquery.h"

#include "completionmatcher.h"

#include <QHash>
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

QStringList CompletionSymbolQuery::namesFromRecords(
    const QList<SemanticSymbolRecord>& records)
{
    struct CompletionNameCandidate {
        QString name;
        int bandPriority = 4;
    };
    QHash<QString, CompletionNameCandidate> candidatesByName;
    for (const SemanticSymbolRecord& record : records) {
        const QString key = record.name.toCaseFolded();
        if (key.isEmpty())
            continue;
        CompletionNameCandidate candidate;
        candidate.name = record.name;
        candidate.bandPriority =
            semanticSymbolAnalysisBandSortPriority(record);
        if (!candidatesByName.contains(key)
            || candidate.bandPriority
                < candidatesByName.value(key).bandPriority) {
            candidatesByName.insert(key, candidate);
        }
    }

    QList<CompletionNameCandidate> candidates = candidatesByName.values();
    std::sort(candidates.begin(), candidates.end(),
              [](const CompletionNameCandidate& left,
                 const CompletionNameCandidate& right) {
        if (left.bandPriority != right.bandPriority)
            return left.bandPriority < right.bandPriority;
        return QString::compare(left.name,
                                right.name,
                                Qt::CaseInsensitive) < 0;
    });

    QStringList result;
    result.reserve(candidates.size());
    for (const CompletionNameCandidate& candidate : candidates)
        result.append(candidate.name);
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
