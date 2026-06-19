#include "completionservice.h"

#include "completionsymbolquery.h"
#include "semanticindex.h"

#include <QVector>

namespace {

SymbolTaxonomy::SemanticMetadata metadataForRecord(
    const SemanticSymbolRecord& record,
    const sym_list::SymbolInfo& fallback)
{
    SymbolTaxonomy::SemanticMetadata metadata =
        SymbolTaxonomy::semanticMetadata(fallback);
    if (!record.isValid())
        return metadata;

    metadata.declarationKind = record.declarationKind;
    metadata.usageRole = record.usageRole;
    metadata.ownerScope = record.owner.kind;
    metadata.visibility = record.visibility;
    metadata.sourceRole = record.sourceRole;
    metadata.rawCollectorKind = record.rawCollectorKind;
    metadata.interfaceLikeOwner = record.owner.interfaceLike;
    return metadata;
}

QString displayNameForRecord(
    const SemanticSymbolRecord& record,
    const sym_list::SymbolInfo& fallback)
{
    if (!record.name.isEmpty())
        return record.name;
    return fallback.symbolName;
}

QString ownerScopeNameForRecord(
    const SemanticSymbolRecord& record,
    const sym_list::SymbolInfo& fallback)
{
    if (!record.owner.name.isEmpty())
        return record.owner.name;
    const SemanticSymbolRecord fallbackRecord =
        semanticSymbolRecordForSymbol(fallback);
    if (!fallbackRecord.owner.name.isEmpty())
        return fallbackRecord.owner.name;

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

CompletionResult::SemanticCompletionItem semanticCompletionItemForSymbol(
    const sym_list::SymbolInfo& symbol)
{
    const SemanticSymbolRecord record = semanticSymbolRecordForSymbol(symbol);
    const SymbolTaxonomy::SemanticMetadata metadata =
        metadataForRecord(record, symbol);
    const QString displayName = displayNameForRecord(record, symbol);

    CompletionResult::SemanticCompletionItem item;
    item.label = displayName;
    item.insertText = displayName;
    item.typeDisplayName = SymbolTaxonomy::symbolTypeLabel(metadata);
    item.ownerScopeName = ownerScopeNameForRecord(record, symbol);
    item.sourceRoleDisplayName =
        SymbolTaxonomy::sourceRoleDisplayName(metadata.sourceRole);
    item.symbolRecord = record;
    item.symbolStableKey = record.stableKey.isValid()
        ? record.stableKey
        : symbolStableKeyForSymbol(symbol);
    item.declarationKind = metadata.declarationKind;
    item.usageRole = metadata.usageRole;
    item.ownerScope = metadata.ownerScope;
    item.sourceRole = metadata.sourceRole;
    return item;
}

QList<CompletionResult::SemanticCompletionItem> semanticCompletionItemsForSymbols(
    const QList<sym_list::SymbolInfo>& symbols)
{
    QList<CompletionResult::SemanticCompletionItem> items;
    items.reserve(symbols.size());
    for (const sym_list::SymbolInfo& symbol : symbols)
        items.append(semanticCompletionItemForSymbol(symbol));
    return items;
}

QList<sym_list::SymbolInfo> completionSymbols(
    SemanticIndex* semanticIndex,
    const CompletionQuery& query)
{
    if (!semanticIndex)
        return {};

    if (!query.structTypeNameForMember.isEmpty()) {
        return CompletionSymbolQuery::structMemberSymbols(
            semanticIndex, query.structTypeNameForMember, query.prefix);
    }

    if (query.prefix.isEmpty())
        return {};

    if (!query.moduleName.isEmpty())
        return semanticIndex->getModuleCompletionSymbols(
            query.moduleName, query.prefix);

    return semanticIndex->getGlobalCompletionSymbols(query.prefix);
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
    const QList<sym_list::SymbolInfo> symbols =
        completionSymbols(semanticIndex(), query);
    result.names = CompletionSymbolQuery::namesFromSymbols(symbols);
    result.items = semanticCompletionItemsForSymbols(symbols);
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

QVector<QPair<sym_list::SymbolInfo, int>>
CompletionService::findScoredSymbolCompletionsByType(
    sym_list::sym_type_e symbolType,
    const QString& prefix,
    int maxResults) const
{
    return CompletionSymbolQuery::scoredTypedSymbols(
        semanticIndex(), symbolType, prefix, maxResults);
}

QStringList CompletionService::findSymbolCompletionsByType(
    sym_list::sym_type_e symbolType,
    const QString& prefix,
    int maxResults) const
{
    const QVector<QPair<sym_list::SymbolInfo, int>> scored =
        findScoredSymbolCompletionsByType(symbolType, prefix, maxResults);
    return CompletionSymbolQuery::symbolNamesFromScored(scored, maxResults);
}

SemanticIndex* CompletionService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}
