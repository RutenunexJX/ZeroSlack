#include "navigationservice.h"

#include "symboltaxonomy.h"

#include <QSet>

namespace {
SemanticSymbolRecord outlineSymbolRecord(const SearchResult& result)
{
    return result.symbolRecord;
}

SymbolTaxonomy::SemanticMetadata outlineMetadata(
    const SemanticSymbolRecord& record)
{
    return semanticMetadataForSymbolRecord(record);
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

SymbolTaxonomy::DeclarationKind outlineGroupKindForRecord(
    const SemanticSymbolRecord& record)
{
    return outlineMetadata(record).declarationKind;
}

QList<SymbolTaxonomy::DeclarationKind> outlineDeclarationKindOrder()
{
    using DeclarationKind = SymbolTaxonomy::DeclarationKind;
    return {
        DeclarationKind::Module,
        DeclarationKind::Interface,
        DeclarationKind::Package,
        DeclarationKind::Typedef,
        DeclarationKind::Enum,
        DeclarationKind::Struct,
        DeclarationKind::StructVariable,
        DeclarationKind::StructMember,
        DeclarationKind::Signal,
        DeclarationKind::Port,
        DeclarationKind::Parameter,
        DeclarationKind::Localparam,
        DeclarationKind::Instance,
        DeclarationKind::Task,
        DeclarationKind::Function,
        DeclarationKind::Macro,
        DeclarationKind::Process,
        DeclarationKind::Generate,
        DeclarationKind::Constraint,
        DeclarationKind::User,
        DeclarationKind::Unknown,
    };
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
        row.symbolRecord = record;
        row.symbolStableKey = record.stableKey;
        row.displayName = record.name;
        row.typeDisplayName = groupDisplayName;
        row.detailDisplayName = outlineDetailDisplayName(record);
        row.iconKind = outlineIconKind(record);
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
    const QList<SemanticSymbolRecord> modules = moduleRecords();
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

    QHash<int, QList<SearchResult>> byType;
    for (const SearchResult& result : searchResults) {
        const SemanticSymbolRecord record = outlineSymbolRecord(result);
        const SymbolTaxonomy::SemanticMetadata metadata =
            outlineMetadata(record);
        const bool isSubroutine =
            SymbolTaxonomy::isSubroutineDeclaration(metadata);
        if (!isSubroutine && subroutineScopes.contains(record.owner.name))
            continue;
        byType[static_cast<int>(metadata.declarationKind)].append(result);
    }

    QList<SymbolOutlineGroup> result;
    for (SymbolTaxonomy::DeclarationKind declarationKind :
         outlineDeclarationKindOrder()) {
        QList<SearchResult> outlineResults =
            byType.value(static_cast<int>(declarationKind));
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
            group.declarationKind = outlineGroupKindForRecord(firstRecord);
            group.displayName = outlineDisplayName(firstRecord);
            group.iconKind = outlineIconKind(firstRecord);
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
    if (!target.found
        || !SymbolTaxonomy::isModuleDeclaration(outlineMetadata(target.symbolRecord)))
        return result;

    result.found = true;
    result.symbolRow.symbolRecord = target.symbolRecord;
    result.symbolRow.symbolStableKey = target.symbolRecord.stableKey;
    result.symbolRow.displayName = target.symbolName;
    result.symbolRow.typeDisplayName = target.symbolTypeText;
    result.symbolRow.detailDisplayName = target.fileName;
    result.symbolRow.iconKind = SymbolOutlineIconKind::Module;
    return result;
}
