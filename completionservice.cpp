#include "completionservice.h"

#include "completionsemanticquery.h"
#include "completionsymbolquery.h"
#include "semanticindex.h"

#include <QVector>

namespace {

SymbolTaxonomy::SemanticMetadata metadataForRecord(
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

QString ownerScopeNameForRecord(const SemanticSymbolRecord& record)
{
    if (!record.owner.name.isEmpty())
        return record.owner.name;

    switch (record.owner.kind) {
    case SymbolTaxonomy::SymbolOwnerScope::Global:
        return QStringLiteral("global");
    case SymbolTaxonomy::SymbolOwnerScope::Module:
        return QStringLiteral("module");
    case SymbolTaxonomy::SymbolOwnerScope::Interface:
        return QStringLiteral("interface");
    case SymbolTaxonomy::SymbolOwnerScope::Package:
        return QStringLiteral("package");
    case SymbolTaxonomy::SymbolOwnerScope::Struct:
        return QStringLiteral("struct");
    case SymbolTaxonomy::SymbolOwnerScope::Unknown:
        break;
    }
    return QString();
}

CompletionResult::SemanticCompletionItem semanticCompletionItemForRecord(
    const SemanticSymbolRecord& record)
{
    const SymbolTaxonomy::SemanticMetadata metadata =
        metadataForRecord(record);
    const QString displayName = record.name;

    CompletionResult::SemanticCompletionItem item;
    item.label = displayName;
    item.insertText = displayName;
    item.typeDisplayName = SymbolTaxonomy::symbolTypeLabel(metadata);
    item.ownerScopeName = ownerScopeNameForRecord(record);
    item.sourceRoleDisplayName =
        SymbolTaxonomy::sourceRoleDisplayName(metadata.sourceRole);
    item.symbolRecord = record;
    item.symbolStableKey = record.stableKey;
    item.declarationKind = metadata.declarationKind;
    item.usageRole = metadata.usageRole;
    item.ownerScope = metadata.ownerScope;
    item.sourceRole = metadata.sourceRole;
    return item;
}

QList<CompletionResult::SemanticCompletionItem> semanticCompletionItemsForRecords(
    const QList<SemanticSymbolRecord>& records)
{
    QList<CompletionResult::SemanticCompletionItem> items;
    items.reserve(records.size());
    for (const SemanticSymbolRecord& record : records)
        items.append(semanticCompletionItemForRecord(record));
    return items;
}

QList<SemanticSymbolRecord> completionRecords(
    SemanticIndex* semanticIndex,
    const CompletionQuery& query)
{
    if (!semanticIndex)
        return {};

    if (!query.structTypeNameForMember.isEmpty()) {
        return CompletionSymbolQuery::structMemberRecords(
            semanticIndex, query.structTypeNameForMember, query.prefix);
    }

    if (query.prefix.isEmpty())
        return {};

    if (!query.moduleName.isEmpty())
        return semanticIndex->getModuleCompletionSymbolRecords(
            query.moduleName, query.prefix);

    return semanticIndex->getGlobalCompletionSymbolRecords(query.prefix);
}

} // namespace

std::unique_ptr<CompletionService> CompletionService::instance = nullptr;

CompletionService* CompletionService::getInstance()
{
    if (!instance)
        instance = std::make_unique<CompletionService>();
    return instance.get();
}

CompletionService::CompletionService(SemanticIndex* semanticIndex)
    : index(semanticIndex ? semanticIndex : SemanticIndex::getInstance())
{
}

CompletionService::~CompletionService() = default;

void CompletionService::setSemanticIndex(SemanticIndex* semanticIndex)
{
    index = semanticIndex ? semanticIndex : SemanticIndex::getInstance();
}

QStringList CompletionService::findCompletions(const CompletionQuery& query) const
{
    return findCompletionResult(query).names;
}

CompletionResult CompletionService::findCompletionResult(
    const CompletionQuery& query) const
{
    CompletionResult result;
    const QList<SemanticSymbolRecord> records =
        completionRecords(semanticIndex(), query);
    result.names = CompletionSymbolQuery::namesFromRecords(records);
    result.items = semanticCompletionItemsForRecords(records);
    return result;
}

QVector<QPair<QString, int>> CompletionService::findScoredAllSymbolCompletions(
    const QString& prefix,
    int maxResults) const
{
    return CompletionSymbolQuery::scoredNames(
        semanticIndex()->getCompletionSymbolNames(), prefix, maxResults);
}

QStringList CompletionService::findAllSymbolCompletions(
    const QString& prefix,
    int maxResults) const
{
    const QVector<QPair<QString, int>> scored =
        findScoredAllSymbolCompletions(prefix, maxResults);
    return CompletionSymbolQuery::namesFromScored(scored, maxResults);
}

QStringList CompletionService::findSymbolCompletionsByKind(
    CompletionCommandKind commandKind,
    const QString& prefix,
    int maxResults) const
{
    const QStringList names = CompletionSymbolQuery::namesFromRecords(
        CompletionSemanticQuery::typedSymbolRecords(
            semanticIndex(),
            commandKind,
            prefix));
    return CompletionSymbolQuery::namesFromScored(
        CompletionSymbolQuery::scoredNames(names, prefix, maxResults),
        maxResults);
}

SemanticIndex* CompletionService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}
