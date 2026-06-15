#include "navigationservice.h"

#include "symboltaxonomy.h"

#include <QSet>

namespace {
QString outlineDisplayName(const sym_list::SymbolInfo& symbol)
{
    const SymbolTaxonomy::SemanticMetadata metadata =
        SymbolTaxonomy::semanticMetadata(symbol);
    QString label = SymbolTaxonomy::symbolTypeLabel(metadata.rawCollectorKind);
    if (label.isEmpty() || label == QLatin1String("symbol"))
        return QStringLiteral("Symbols");
    label[0] = label.at(0).toUpper();
    return label;
}

SymbolOutlineIconKind outlineIconKind(const sym_list::SymbolInfo& symbol)
{
    const SymbolTaxonomy::SemanticMetadata metadata =
        SymbolTaxonomy::semanticMetadata(symbol);
    switch (metadata.declarationKind) {
    case SymbolTaxonomy::DeclarationKind::Module:
        return SymbolOutlineIconKind::Module;
    case SymbolTaxonomy::DeclarationKind::Signal:
        return SymbolOutlineIconKind::Signal;
    case SymbolTaxonomy::DeclarationKind::Task:
    case SymbolTaxonomy::DeclarationKind::Function:
        return SymbolOutlineIconKind::Subroutine;
    case SymbolTaxonomy::DeclarationKind::Parameter:
    case SymbolTaxonomy::DeclarationKind::Localparam:
        return SymbolOutlineIconKind::Parameter;
    case SymbolTaxonomy::DeclarationKind::Port:
        return SymbolOutlineIconKind::Port;
    case SymbolTaxonomy::DeclarationKind::Instance:
        return SymbolOutlineIconKind::Instance;
    case SymbolTaxonomy::DeclarationKind::Typedef:
    case SymbolTaxonomy::DeclarationKind::Enum:
    case SymbolTaxonomy::DeclarationKind::Struct:
    case SymbolTaxonomy::DeclarationKind::StructVariable:
    case SymbolTaxonomy::DeclarationKind::StructMember:
        return SymbolOutlineIconKind::Type;
    case SymbolTaxonomy::DeclarationKind::Interface:
    case SymbolTaxonomy::DeclarationKind::Package:
    case SymbolTaxonomy::DeclarationKind::Modport:
    case SymbolTaxonomy::DeclarationKind::Macro:
    case SymbolTaxonomy::DeclarationKind::Process:
    case SymbolTaxonomy::DeclarationKind::Generate:
    case SymbolTaxonomy::DeclarationKind::Constraint:
    case SymbolTaxonomy::DeclarationKind::User:
    case SymbolTaxonomy::DeclarationKind::Unknown:
    default:
        return SymbolOutlineIconKind::Symbol;
    }
}

QString outlineDetailDisplayName(const sym_list::SymbolInfo& symbol)
{
    if (!symbol.dataType.isEmpty())
        return symbol.dataType;
    if (!symbol.moduleScope.isEmpty())
        return symbol.moduleScope;
    return symbol.fileName;
}

QList<SymbolOutlineSymbolRow> outlineRows(
    const QList<sym_list::SymbolInfo>& symbols,
    const QString& groupDisplayName)
{
    QList<SymbolOutlineSymbolRow> rows;
    rows.reserve(symbols.size());
    for (const sym_list::SymbolInfo& symbol : symbols) {
        SymbolOutlineSymbolRow row;
        row.symbol = symbol;
        row.displayName = symbol.symbolName;
        row.typeDisplayName = groupDisplayName;
        row.detailDisplayName = outlineDetailDisplayName(symbol);
        row.iconKind = outlineIconKind(symbol);
        rows.append(row);
    }
    return rows;
}
}

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
            group.displayName = outlineDisplayName(outlineSymbols.first());
            group.iconKind = outlineIconKind(outlineSymbols.first());
            group.symbols = outlineSymbols;
            group.symbolRows = outlineRows(outlineSymbols, group.displayName);
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
