#include "completionservice.h"

#include "completionsymbolquery.h"
#include "semanticindex.h"

#include <QVector>

namespace {

QString ownerScopeNameForCompletionItem(
    const sym_list::SymbolInfo& symbol,
    const SymbolTaxonomy::SemanticMetadata& metadata)
{
    if (!symbol.moduleScope.isEmpty())
        return symbol.moduleScope;

    switch (metadata.ownerScope) {
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
    const SymbolTaxonomy::SemanticMetadata metadata =
        SymbolTaxonomy::semanticMetadata(symbol);

    CompletionResult::SemanticCompletionItem item;
    item.label = symbol.symbolName;
    item.insertText = symbol.symbolName;
    item.typeDisplayName = SymbolTaxonomy::symbolTypeLabel(metadata);
    item.ownerScopeName = ownerScopeNameForCompletionItem(symbol, metadata);
    item.sourceRoleDisplayName =
        SymbolTaxonomy::sourceRoleDisplayName(metadata.sourceRole);
    item.symbolStableKey = symbolStableKeyForSymbol(symbol);
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
    result.symbols = findCompletionSymbols(query);
    result.names = CompletionSymbolQuery::namesFromSymbols(result.symbols);
    result.items = semanticCompletionItemsForSymbols(result.symbols);
    return result;
}

QList<sym_list::SymbolInfo> CompletionService::findCompletionSymbols(
    const CompletionQuery& query) const
{
    if (!query.structTypeNameForMember.isEmpty()) {
        return CompletionSymbolQuery::structMemberSymbols(
            semanticIndex(), query.structTypeNameForMember, query.prefix);
    }

    if (query.prefix.isEmpty())
        return {};

    if (!query.moduleName.isEmpty())
        return semanticIndex()->getModuleCompletionSymbols(
            query.moduleName, query.prefix);

    return semanticIndex()->getGlobalCompletionSymbols(query.prefix);
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
