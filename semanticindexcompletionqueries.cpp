#include "semanticindex.h"
#include "semanticindexcompletionfilters.h"
#include "symboltaxonomy.h"

#include <QSet>
#include <algorithm>

using namespace semantic_index_completion;

namespace {
SymbolTaxonomy::SemanticMetadata metadataForCompletionQueryRecord(
    const SemanticSymbolRecord& record)
{
    return semanticMetadataForSymbolRecord(record);
}

void sortCompletionQueryRecordsByName(
    QList<SemanticSymbolRecord>& records)
{
    std::sort(records.begin(), records.end(),
              [](const SemanticSymbolRecord& left,
                 const SemanticSymbolRecord& right) {
        const int leftBandPriority =
            semanticSymbolAnalysisBandSortPriority(left);
        const int rightBandPriority =
            semanticSymbolAnalysisBandSortPriority(right);
        if (leftBandPriority != rightBandPriority)
            return leftBandPriority < rightBandPriority;
        return QString::compare(left.name,
                                right.name,
                                Qt::CaseInsensitive) < 0;
    });
}
}

QList<SemanticSymbolRecord> SemanticIndex::getModuleCompletionSymbolRecords(
    const QString& moduleName,
    const QString& prefix) const
{
    QList<SemanticSymbolRecord> result;
    QSet<QString> seenNames;
    if (moduleName.isEmpty())
        return {};

    const QList<SemanticSymbolRecord> records =
        getSymbolRecordsByOwner(moduleName);
    for (const SemanticSymbolRecord& record : records) {
        if (record.owner.name != moduleName
            || !SymbolTaxonomy::isInternalCompletionCandidate(
                metadataForCompletionQueryRecord(record))
            || !semanticCompletionNameMatches(record.name, prefix)) {
            continue;
        }

        const QString key = record.name.toCaseFolded();
        if (seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(record);
    }

    sortCompletionQueryRecordsByName(result);
    return result;
}

QList<SemanticSymbolRecord> SemanticIndex::getGlobalCompletionSymbolRecords(
    const QString& prefix) const
{
    QList<SemanticSymbolRecord> result;
    QSet<QString> seenNames;
    const QList<SemanticSymbolRecord> records =
        getSymbolRecordsByOwner(QString());
    for (const SemanticSymbolRecord& record : records) {
        if (!SymbolTaxonomy::isGlobalCompletionCandidate(
                metadataForCompletionQueryRecord(record))
            || !semanticCompletionNameMatches(record.name, prefix)) {
            continue;
        }

        const QString key = record.name.toCaseFolded();
        if (seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(record);
    }

    sortCompletionQueryRecordsByName(result);
    return result;
}

QStringList SemanticIndex::getCompletionSymbolNames() const
{
    struct CompletionNameCandidate {
        QString name;
        int bandPriority = 4;
    };
    QHash<QString, CompletionNameCandidate> uniqueNames;
    const QList<SemanticSymbolRecord> records = getSymbolRecords();
    for (const SemanticSymbolRecord& record : records) {
        const QString key = record.name.toCaseFolded();
        if (key.isEmpty())
            continue;
        CompletionNameCandidate candidate;
        candidate.name = record.name;
        candidate.bandPriority =
            semanticSymbolAnalysisBandSortPriority(record);
        if (!uniqueNames.contains(key)
            || candidate.bandPriority < uniqueNames.value(key).bandPriority) {
            uniqueNames.insert(key, candidate);
        }
    }

    QList<CompletionNameCandidate> candidates = uniqueNames.values();
    std::sort(candidates.begin(), candidates.end(),
              [](const CompletionNameCandidate& left,
                 const CompletionNameCandidate& right) {
        if (left.bandPriority != right.bandPriority)
            return left.bandPriority < right.bandPriority;
        return QString::compare(left.name,
                                right.name,
                                Qt::CaseInsensitive) < 0;
    });
    QStringList names;
    names.reserve(candidates.size());
    for (const CompletionNameCandidate& candidate : candidates)
        names.append(candidate.name);
    return names;
}
