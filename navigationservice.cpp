#include "navigationservice.h"

#include "symboltaxonomy.h"

#include <QSet>

namespace {
SemanticSymbolRecord outlineSymbolRecord(const SearchResult& result)
{
    if (result.symbolRecord.isValid())
        return result.symbolRecord;
    return semanticSymbolRecordForSymbol(result.symbol);
}

SymbolTaxonomy::SemanticMetadata outlineMetadata(
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

QString outlineDisplayName(const SemanticSymbolRecord& record)
{
    const SymbolTaxonomy::SemanticMetadata metadata = outlineMetadata(record);
    QString label = SymbolTaxonomy::symbolTypeLabel(metadata);
    if (label.isEmpty() || label == QLatin1String("symbol"))
        return QStringLiteral("Symbols");
    label[0] = label.at(0).toUpper();
    return label;
}

SymbolOutlineIconKind outlineIconKind(const SemanticSymbolRecord& record)
{
    switch (record.declarationKind) {
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

QString outlineDetailDisplayName(const SemanticSymbolRecord& record)
{
    if (!record.type.rawTypeText.isEmpty())
        return record.type.rawTypeText;
    if (!record.owner.name.isEmpty())
        return record.owner.name;
    return record.location.fileName;
}

QList<SymbolOutlineSymbolRow> outlineRows(
    const QList<SearchResult>& results,
    const QString& groupDisplayName)
{
    QList<SymbolOutlineSymbolRow> rows;
    rows.reserve(results.size());
    for (const SearchResult& result : results) {
        const SemanticSymbolRecord record = outlineSymbolRecord(result);
        SymbolOutlineSymbolRow row;
        row.symbol = result.symbol;
        row.symbolRecord = record;
        row.displayName = record.name;
        row.typeDisplayName = groupDisplayName;
        row.detailDisplayName = outlineDetailDisplayName(record);
        row.iconKind = outlineIconKind(record);
        rows.append(row);
    }
    return rows;
}

QList<sym_list::SymbolInfo> symbolsForResults(const QList<SearchResult>& results)
{
    QList<sym_list::SymbolInfo> symbols;
    symbols.reserve(results.size());
    for (const SearchResult& result : results)
        symbols.append(result.symbol);
    return symbols;
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

    const QList<SearchResult> searchResults = searchService.findSymbols(outlineQuery);

    QSet<QString> subroutineScopes;
    for (const SearchResult& result : searchResults) {
        const SemanticSymbolRecord record = outlineSymbolRecord(result);
        const SymbolTaxonomy::SemanticMetadata metadata =
            outlineMetadata(record);
        if (SymbolTaxonomy::isSubroutineDeclaration(metadata))
            subroutineScopes.insert(record.name);
    }

    QHash<sym_list::sym_type_e, QList<SearchResult>> byType;
    for (const SearchResult& result : searchResults) {
        const SemanticSymbolRecord record = outlineSymbolRecord(result);
        const SymbolTaxonomy::SemanticMetadata metadata =
            outlineMetadata(record);
        const bool isSubroutine =
            SymbolTaxonomy::isSubroutineDeclaration(metadata);
        if (!isSubroutine && subroutineScopes.contains(record.owner.name))
            continue;
        byType[SymbolTaxonomy::outlineGroupType(metadata)].append(result);
    }

    QList<SymbolOutlineGroup> result;
    for (sym_list::sym_type_e symbolType : SymbolTaxonomy::outlineSymbolTypes()) {
        QList<SearchResult> outlineResults = byType.value(symbolType);
        if (outlineResults.isEmpty())
            continue;

        if (!query.filter.isEmpty()) {
            QList<SearchResult> filteredResults;
            filteredResults.reserve(outlineResults.size());
            for (const SearchResult& searchResult : std::as_const(outlineResults)) {
                const SemanticSymbolRecord record = outlineSymbolRecord(searchResult);
                if (record.name.contains(query.filter, Qt::CaseInsensitive))
                    filteredResults.append(searchResult);
            }
            outlineResults = filteredResults;
        }

        if (!outlineResults.isEmpty()) {
            const SemanticSymbolRecord firstRecord =
                outlineSymbolRecord(outlineResults.first());
            SymbolOutlineGroup group;
            group.symbolType = symbolType;
            group.displayName = outlineDisplayName(firstRecord);
            group.iconKind = outlineIconKind(firstRecord);
            group.symbols = symbolsForResults(outlineResults);
            group.symbolRows = outlineRows(outlineResults, group.displayName);
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
    if (!target.found || !SymbolTaxonomy::isModuleDeclaration(target.symbol))
        return result;

    result.found = true;
    result.symbol = target.symbol;
    return result;
}
