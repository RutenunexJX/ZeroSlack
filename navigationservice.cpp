#include "navigationservice.h"

#include "semanticindexsnapshot.h"
#include "symboltaxonomy.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <algorithm>

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

QString normalizedDesignFingerprintFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()))
        .toCaseFolded();
}

bool designFingerprintScopeContains(const QSet<QString>& fileScope,
                                    const QString& fileName)
{
    if (fileScope.isEmpty())
        return true;
    return fileScope.contains(normalizedDesignFingerprintFileName(fileName));
}

QString fingerprintToken(const QStringList& fields)
{
    QString token;
    for (const QString& field : fields) {
        token += QString::number(field.size());
        token += QLatin1Char(':');
        token += field;
    }
    return token;
}

QString designRecordFingerprintToken(const SemanticSymbolRecord& record)
{
    return fingerprintToken({
        QStringLiteral("record"),
        symbolStableKeyText(record.stableKey),
        record.name,
        QString::number(static_cast<int>(record.declarationKind)),
        QString::number(static_cast<int>(record.collectorKind)),
        normalizedDesignFingerprintFileName(record.location.fileName),
        QString::number(record.location.startLine),
        QString::number(record.location.startColumn),
        QString::number(record.location.endLine),
        QString::number(record.location.endColumn),
        QString::number(record.location.position),
        QString::number(record.location.length),
        QString::number(static_cast<int>(record.owner.kind)),
        record.owner.name,
        symbolStableKeyText(record.owner.stableKey),
        record.type.rawTypeText,
        record.type.resolvedTypeName,
        QString::number(static_cast<int>(record.type.resolvedTypeKind)),
        symbolStableKeyText(record.type.stableKey),
    });
}

QString designRelationshipFingerprintToken(
    const SemanticRelationship& relationship)
{
    return fingerprintToken({
        QStringLiteral("relationship"),
        symbolStableKeyText(relationship.fromStableKey),
        symbolStableKeyText(relationship.toStableKey),
        relationship.fromAccessPath,
        relationship.toAccessPath,
        normalizedDesignFingerprintFileName(
            relationship.evidenceRange.fileName),
        QString::number(relationship.evidenceRange.line),
        QString::number(relationship.evidenceRange.column),
        QString::number(relationship.evidenceRange.endLine),
        QString::number(relationship.evidenceRange.endColumn),
    });
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

QString NavigationService::inferDesignTopModule() const
{
    return hierarchyService.inferDesignTopModule();
}

QString NavigationService::inferDesignTopModule(
    const QSet<QString>& fileScope) const
{
    return hierarchyService.inferDesignTopModule(fileScope);
}

QStringList NavigationService::inferDesignTopModules() const
{
    return hierarchyService.inferDesignTopModules();
}

QStringList NavigationService::inferDesignTopModules(
    const QSet<QString>& fileScope) const
{
    return hierarchyService.inferDesignTopModules(fileScope);
}

DesignHierarchyReport NavigationService::findDesignHierarchy(
    const QString& topModule) const
{
    return hierarchyService.getDesignHierarchyReport(topModule);
}

DesignHierarchyReport NavigationService::findDesignHierarchy(
    const QString& topModule,
    const QSet<QString>& fileScope) const
{
    return hierarchyService.getDesignHierarchyReport(topModule, fileScope);
}

DesignHierarchyReport NavigationService::findDesignHierarchy(
    const QStringList& topModules,
    const QString& selectedTopModule) const
{
    return hierarchyService.getDesignHierarchyReport(topModules, selectedTopModule);
}

DesignHierarchyReport NavigationService::findDesignHierarchy(
    const QStringList& topModules,
    const QString& selectedTopModule,
    const QSet<QString>& fileScope) const
{
    return hierarchyService.getDesignHierarchyReport(topModules,
                                                     selectedTopModule,
                                                     fileScope);
}

QByteArray NavigationService::designStructureFingerprint(
    const QSet<QString>& fileScope) const
{
    if (!index)
        return {};

    QSet<QString> normalizedFileScope;
    normalizedFileScope.reserve(fileScope.size());
    for (const QString& fileName : fileScope) {
        const QString normalized =
            normalizedDesignFingerprintFileName(fileName);
        if (!normalized.isEmpty())
            normalizedFileScope.insert(normalized);
    }

    QStringList tokens;
    QSet<QString> moduleStableKeys;
    const QList<SemanticSymbolRecord> records = index->getSymbolRecords();
    tokens.reserve(records.size());
    for (const SemanticSymbolRecord& record : records) {
        if (!designFingerprintScopeContains(normalizedFileScope,
                                            record.location.fileName)) {
            continue;
        }

        const SymbolTaxonomy::SemanticMetadata metadata =
            semanticMetadataForSymbolRecord(record);
        const bool module = SymbolTaxonomy::isModuleDeclaration(metadata);
        const bool interfaceLike =
            metadata.declarationKind
                == SymbolTaxonomy::DeclarationKind::Interface;
        const bool instance = SymbolTaxonomy::isInstanceDeclaration(metadata);
        if (!module && !interfaceLike && !instance)
            continue;

        tokens.append(designRecordFingerprintToken(record));
        if (module && record.stableKey.isValid())
            moduleStableKeys.insert(symbolStableKeyText(record.stableKey));
    }

    QList<SemanticRelationship> relationships;
    const std::shared_ptr<const SemanticIndexSnapshot> snapshot =
        index->snapshot();
    if (snapshot) {
        relationships = snapshot->relationships();
    } else {
        for (const SemanticSymbolRecord& record : records) {
            const SymbolTaxonomy::SemanticMetadata metadata =
                semanticMetadataForSymbolRecord(record);
            if (!SymbolTaxonomy::isModuleDeclaration(metadata)
                || !record.stableKey.isValid()
                || !moduleStableKeys.contains(
                    symbolStableKeyText(record.stableKey))) {
                continue;
            }
            relationships.append(
                index->relationshipsForStableKey(record.stableKey, true));
        }
    }

    for (const SemanticRelationship& relationship : relationships) {
        if (relationship.type != SymbolRelationshipEngine::INSTANTIATES)
            continue;
        if (!moduleStableKeys.contains(
                symbolStableKeyText(relationship.fromStableKey))) {
            continue;
        }
        tokens.append(designRelationshipFingerprintToken(relationship));
    }

    std::sort(tokens.begin(), tokens.end());
    return QCryptographicHash::hash(tokens.join(QLatin1Char('\n')).toUtf8(),
                                    QCryptographicHash::Sha256);
}

std::uint64_t NavigationService::semanticSnapshotRevision() const
{
    return index ? index->snapshotRevision() : 0;
}

QStringList NavigationService::modulesDefinedInFile(const QString& fileName) const
{
    return hierarchyService.modulesDefinedInFile(fileName);
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
