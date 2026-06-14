#include "navigationservice.h"

#include "symboltaxonomy.h"

#include <QSet>

std::unique_ptr<NavigationService> NavigationService::instance = nullptr;

NavigationService* NavigationService::getInstance()
{
    if (!instance)
        instance = std::make_unique<NavigationService>();
    return instance.get();
}

NavigationService::NavigationService(SemanticIndex* semanticIndex)
    : index(semanticIndex ? semanticIndex : SemanticIndex::getInstance()),
      definitionNavigationService(index),
      searchService(index),
      hierarchyService(index)
{
}

NavigationService::~NavigationService() = default;

void NavigationService::setSemanticIndex(SemanticIndex* semanticIndex)
{
    index = semanticIndex ? semanticIndex : SemanticIndex::getInstance();
    definitionNavigationService.setSemanticIndex(index);
    searchService.setSemanticIndex(index);
    hierarchyService.setSemanticIndex(index);
}

QList<ModuleHierarchyGroup> NavigationService::findModuleHierarchy(
    const NavigationModuleQuery& query) const
{
    const QList<sym_list::SymbolInfo> modules = moduleSymbols();
    QList<ModuleHierarchyGroup> hierarchy = buildModuleInstantiationHierarchy(modules);
    if (hierarchy.isEmpty())
        hierarchy = buildModuleFileGroups(modules);
    return filterModuleHierarchy(hierarchy, query.filter);
}

QList<SymbolOutlineGroup> NavigationService::findSymbolOutline(
    const NavigationSymbolOutlineQuery& query) const
{
    SearchQuery outlineQuery;
    outlineQuery.fileName = query.fileName;
    outlineQuery.intent = SymbolTaxonomy::SymbolSearchIntent::OutlineSymbols;

    QList<sym_list::SymbolInfo> symbols;
    const QList<SearchResult> searchResults = searchService.findSymbols(outlineQuery);
    symbols.reserve(searchResults.size());
    for (const SearchResult& result : searchResults)
        symbols.append(result.symbol);

    QSet<QString> subroutineScopes;
    for (const sym_list::SymbolInfo& symbol : std::as_const(symbols)) {
        if (SymbolTaxonomy::isSubroutineDeclaration(symbol.symbolType)) {
            subroutineScopes.insert(symbol.symbolName);
        }
    }

    QHash<sym_list::sym_type_e, QList<sym_list::SymbolInfo>> byType;
    for (const sym_list::SymbolInfo& symbol : std::as_const(symbols)) {
        const bool isSubroutine =
            SymbolTaxonomy::isSubroutineDeclaration(symbol.symbolType);
        if (!isSubroutine && subroutineScopes.contains(symbol.moduleScope))
            continue;
        byType[symbol.symbolType].append(symbol);
    }

    QList<SymbolOutlineGroup> result;
    for (sym_list::sym_type_e symbolType : SymbolTaxonomy::outlineSymbolTypes()) {
        QList<sym_list::SymbolInfo> outlineSymbols = byType.value(symbolType);
        if (outlineSymbols.isEmpty())
            continue;

        if (!query.filter.isEmpty()) {
            QList<sym_list::SymbolInfo> filteredSymbols;
            filteredSymbols.reserve(outlineSymbols.size());
            for (const sym_list::SymbolInfo& symbol : std::as_const(outlineSymbols)) {
                if (symbol.symbolName.contains(query.filter, Qt::CaseInsensitive))
                    filteredSymbols.append(symbol);
            }
            outlineSymbols = filteredSymbols;
        }

        if (!outlineSymbols.isEmpty()) {
            SymbolOutlineGroup group;
            group.symbolType = symbolType;
            group.symbols = outlineSymbols;
            result.append(group);
        }
    }
    return result;
}

NavigationModuleTarget NavigationService::resolveModuleTarget(
    const QString& moduleName) const
{
    NavigationModuleTarget result;
    if (moduleName.isEmpty())
        return result;

    DefinitionNavigationQuery query;
    query.symbolName = moduleName;
    const DefinitionNavigationTarget target =
        definitionNavigationService.resolveTarget(query);
    if (!target.found || !SymbolTaxonomy::isModuleDeclaration(target.symbolType))
        return result;

    result.found = true;
    result.symbol = target.symbol;
    return result;
}
