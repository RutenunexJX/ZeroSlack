#include "semanticindex.h"
#include "completioncommandkindadapter.h"
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
    CompletionCommandKind requestedKind,
    const QString& moduleName)
{
    const sym_list::sym_type_e requestedType =
        rawCollectorKindForCompletionCommandKind(requestedKind);
    const bool useGlobalScope = moduleName.isEmpty()
        || alwaysGlobalCommandSymbolType(requestedType);
    if (useGlobalScope)
        return record.owner.name.isEmpty();

    return record.owner.name == moduleName
        || (SymbolTaxonomy::isPackageVisibleCommandRequest(requestedType)
            && record.visibility
                == SymbolTaxonomy::SymbolVisibility::PackageVisible);
}

}

QList<SemanticSymbolRecord> SemanticIndex::getCommandCompletionSymbolRecords(
    const QString& moduleName,
    CompletionCommandKind commandKind,
    const QString& prefix) const
{
    QList<SemanticSymbolRecord> result;
    QSet<QString> seenNames;
    const sym_list::sym_type_e rawCollectorKind =
        rawCollectorKindForCompletionCommandKind(commandKind);
    if (moduleName.isEmpty()
        && !commandGlobalCompletionSymbolType(rawCollectorKind))
        return result;

    const QList<SemanticSymbolRecord> records = getSymbolRecords();
    for (const SemanticSymbolRecord& record : records) {
        const SymbolTaxonomy::SemanticMetadata metadata =
            completionMetadataForRecord(record);
        if (!commandCompletionScopeVisibleForRecord(
                record,
                commandKind,
                moduleName)
            || !commandSymbolTypeMatches(
                metadata,
                rawCollectorKind,
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
