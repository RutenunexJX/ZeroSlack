#include "semanticindex.h"
#include "semanticindexcompletionfilters.h"
#include "symboltaxonomy.h"

#include <QHash>
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

bool isImportedPackageCompletionCandidate(
    const SemanticSymbolRecord& record,
    const SemanticQueryContext& context,
    const SemanticIndex* index)
{
    if (!index
        || record.visibility != SymbolTaxonomy::SymbolVisibility::PackageVisible
        || !index->packageVisibleRecordImported(record, context)) {
        return false;
    }
    return SymbolTaxonomy::isPackageVisibleDefinition(
        metadataForCompletionQueryRecord(record));
}

QList<SemanticSymbolRecord> uniqueImportedPackageCompletionRecords(
    const SemanticIndex* index,
    const SemanticQueryContext& context)
{
    QList<SemanticSymbolRecord> result;
    QHash<QString, QList<SemanticSymbolRecord>> recordsByName;
    const QSet<QString> activePackages =
        index ? index->activeImportedPackageNames(context) : QSet<QString>();
    for (const QString& packageName : activePackages) {
        for (const SemanticSymbolRecord& record :
             index->getSymbolRecordsByOwner(packageName)) {
            if (!isImportedPackageCompletionCandidate(record, context, index)
                || !semanticCompletionNameMatches(record.name, context.prefix)) {
                continue;
            }
            recordsByName[record.name.toCaseFolded()].append(record);
        }
    }

    for (auto it = recordsByName.constBegin();
         it != recordsByName.constEnd();
         ++it) {
        QSet<QString> owners;
        for (const SemanticSymbolRecord& record : it.value())
            owners.insert(record.owner.name);
        if (owners.size() == 1 && !it.value().isEmpty())
            result.append(it.value().first());
    }
    sortCompletionQueryRecordsByName(result);
    return result;
}
}

QList<SemanticSymbolRecord> SemanticIndex::getModuleCompletionSymbolRecords(
    const QString& moduleName,
    const QString& prefix) const
{
    SemanticQueryContext context;
    context.moduleName = moduleName;
    context.prefix = prefix;
    return getModuleCompletionSymbolRecords(context);
}

QList<SemanticSymbolRecord> SemanticIndex::getModuleCompletionSymbolRecords(
    const SemanticQueryContext& context) const
{
    QList<SemanticSymbolRecord> result;
    QSet<QString> seenNames;
    if (context.moduleName.isEmpty())
        return {};

    const QList<SemanticSymbolRecord> records =
        getSymbolRecordsByOwner(context.moduleName);
    for (const SemanticSymbolRecord& record : records) {
        if (record.owner.name != context.moduleName
            || !SymbolTaxonomy::isInternalCompletionCandidate(
                metadataForCompletionQueryRecord(record))
            || !semanticCompletionNameMatches(record.name, context.prefix)) {
            continue;
        }

        const QString key = record.name.toCaseFolded();
        if (seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(record);
    }

    const QList<SemanticSymbolRecord> importedRecords =
        uniqueImportedPackageCompletionRecords(this, context);
    for (const SemanticSymbolRecord& record : importedRecords) {
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
