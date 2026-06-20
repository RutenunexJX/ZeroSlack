#include "semanticindex.h"
#include "semanticindexcompletionfilters.h"
#include "symboltaxonomy.h"

#include <QSet>
#include <algorithm>

using namespace semantic_index_completion;

namespace {
SymbolTaxonomy::SemanticMetadata completionMetadataForRecord(
    const SemanticSymbolRecord& record)
{
    SymbolTaxonomy::SemanticMetadata metadata;
    metadata.declarationKind = record.declarationKind;
    metadata.usageRole = record.usageRole;
    metadata.ownerScope = record.owner.kind;
    metadata.visibility = record.visibility;
    metadata.sourceRole = record.sourceRole;
    metadata.rawCollectorKind = record.rawCollectorKind;
    metadata.interfaceLikeOwner = record.owner.interfaceLike;
    return metadata;
}

bool commandCompletionScopeVisibleForRecord(
    const SemanticSymbolRecord& record,
    sym_list::sym_type_e requestedType,
    const QString& moduleName)
{
    const bool useGlobalScope = moduleName.isEmpty()
        || alwaysGlobalCommandSymbolType(requestedType);
    if (useGlobalScope)
        return record.owner.name.isEmpty();

    return record.owner.name == moduleName
        || (SymbolTaxonomy::isPackageVisibleCommandRequest(requestedType)
            && record.visibility
                == SymbolTaxonomy::SymbolVisibility::PackageVisible);
}

bool globalSymbolInfoVisibleForRecord(
    const SemanticSymbolRecord& record,
    sym_list::sym_type_e requestedType)
{
    return alwaysGlobalSymbolInfoType(requestedType)
        || record.owner.name.isEmpty();
}

}

QList<sym_list::SymbolInfo> SemanticIndex::getCommandCompletionSymbols(
    const QString& moduleName,
    sym_list::sym_type_e symbolType,
    const QString& prefix) const
{
    return semanticSymbolInfoCarriersForRecords(
        getCommandCompletionSymbolRecords(moduleName, symbolType, prefix));
}

QList<SemanticSymbolRecord> SemanticIndex::getCommandCompletionSymbolRecords(
    const QString& moduleName,
    sym_list::sym_type_e symbolType,
    const QString& prefix) const
{
    QList<SemanticSymbolRecord> result;
    QSet<QString> seenNames;
    if (moduleName.isEmpty() && !commandGlobalCompletionSymbolType(symbolType))
        return result;

    const QList<SemanticSymbolRecord> records = getSymbolRecords();
    for (const SemanticSymbolRecord& record : records) {
        const SymbolTaxonomy::SemanticMetadata metadata =
            completionMetadataForRecord(record);
        if (!commandCompletionScopeVisibleForRecord(
                record,
                symbolType,
                moduleName)
            || !commandSymbolTypeMatches(
                metadata,
                symbolType,
                record.type.rawTypeText)
            || !semanticCompletionNameMatches(record.name, prefix)) {
            continue;
        }

        const QString key = record.name.toCaseFolded();
        if (seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(record);
    }

    std::sort(
        result.begin(),
        result.end(),
        [](const SemanticSymbolRecord& left,
           const SemanticSymbolRecord& right) {
            return QString::compare(
                       left.name,
                       right.name,
                       Qt::CaseInsensitive)
                < 0;
        });
    return result;
}

QList<sym_list::SymbolInfo> SemanticIndex::getGlobalSymbolInfosByType(
    sym_list::sym_type_e symbolType,
    const QString& prefix) const
{
    QList<sym_list::SymbolInfo> result;
    if (!globalSymbolInfoType(symbolType))
        return result;

    const QList<SemanticSymbolRecord> records = getSymbolRecords();
    for (const SemanticSymbolRecord& record : records) {
        const SymbolTaxonomy::SemanticMetadata metadata =
            completionMetadataForRecord(record);
        if (!globalSymbolInfoMetadata(metadata)
            || !commandSymbolTypeMatches(
                metadata,
                symbolType,
                record.type.rawTypeText)
            || !semanticCompletionNameMatches(record.name, prefix)) {
            continue;
        }

        if (globalSymbolInfoVisibleForRecord(record, symbolType))
            result.append(semanticSymbolInfoCarrierForRecord(record));
    }

    return result;
}
