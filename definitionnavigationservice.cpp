#include "definitionnavigationservice.h"

#include "definitionservice.h"
#include "symboltaxonomy.h"

#include <QFileInfo>
#include <QtGlobal>

std::unique_ptr<DefinitionNavigationService> DefinitionNavigationService::instance = nullptr;

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

QString symbolNameForRecord(const SemanticSymbolRecord& record)
{
    if (!record.name.isEmpty())
        return record.name;
    return QStringLiteral("<unnamed>");
}

QString fileNameForRecord(const SemanticSymbolRecord& record)
{
    return record.location.fileName;
}

int startLineForRecord(const SemanticSymbolRecord& record)
{
    return record.location.startLine;
}

int startColumnForRecord(const SemanticSymbolRecord& record)
{
    return record.location.startColumn;
}

QString ownerDisplayNameForRecord(const SemanticSymbolRecord& record)
{
    if (!record.owner.name.isEmpty())
        return record.owner.name;
    if (record.owner.kind == SymbolTaxonomy::SymbolOwnerScope::Global)
        return QStringLiteral("global");
    return QStringLiteral("global");
}
}

DefinitionNavigationService* DefinitionNavigationService::getInstance()
{
    if (!instance)
        instance = std::make_unique<DefinitionNavigationService>();
    return instance.get();
}

DefinitionNavigationService::DefinitionNavigationService(SemanticIndex* semanticIndex)
    : index(semanticIndex ? semanticIndex : SemanticIndex::getInstance()),
      definitionService(std::make_unique<DefinitionService>(index))
{
}

DefinitionNavigationService::~DefinitionNavigationService() = default;

void DefinitionNavigationService::setSemanticIndex(SemanticIndex* semanticIndex)
{
    index = semanticIndex ? semanticIndex : SemanticIndex::getInstance();
    definitionService->setSemanticIndex(index);
}

DefinitionNavigationTarget DefinitionNavigationService::resolveTarget(
    const DefinitionNavigationQuery& query) const
{
    if (query.symbolName.isEmpty())
        return {};
    return toNavigationTarget(definitionService->resolveDefinition(toDefinitionQuery(query)));
}

bool DefinitionNavigationService::canResolveTarget(
    const DefinitionNavigationQuery& query) const
{
    return resolveTarget(query).found;
}

QString DefinitionNavigationService::tooltipText(
    const DefinitionNavigationQuery& query) const
{
    const DefinitionNavigationTarget target = resolveTarget(query);
    if (!target.found)
        return {};

    return QStringLiteral("Definition: %1 (%2)\nLocation: %3:%4")
        .arg(target.symbolName,
             target.symbolTypeText,
             QFileInfo(target.fileName).fileName())
        .arg(target.line);
}

DefinitionNavigationQuery DefinitionNavigationService::navigationQueryForContext(
    const DefinitionNavigationContext& context) const
{
    DefinitionNavigationQuery query;
    query.symbolName = context.symbolName;
    query.fileName = context.fileName;
    query.moduleName = context.moduleName;

    if (context.column >= 0) {
        const int prefixColumn =
            qBound(0, context.column, context.lineText.size());
        query.linePrefixBeforeCursor = context.lineText.left(prefixColumn);
    }

    return query;
}

DefinitionQuery DefinitionNavigationService::toDefinitionQuery(
    const DefinitionNavigationQuery& query)
{
    DefinitionQuery definitionQuery;
    definitionQuery.symbolName = query.symbolName;
    definitionQuery.fileName = query.fileName;
    definitionQuery.moduleName = query.moduleName;
    definitionQuery.linePrefixBeforeCursor = query.linePrefixBeforeCursor;
    return definitionQuery;
}

DefinitionNavigationTarget DefinitionNavigationService::toNavigationTarget(
    const DefinitionResult& result)
{
    DefinitionNavigationTarget target;
    if (!result.found)
        return target;

    target.found = true;
    target.localFile = result.localFile;
    target.symbolRecord = result.symbolRecord;
    target.symbolStableKey = target.symbolRecord.stableKey.isValid()
        ? target.symbolRecord.stableKey
        : result.symbolStableKey;
    target.symbolName = symbolNameForRecord(target.symbolRecord);
    target.fileName = fileNameForRecord(target.symbolRecord);
    target.line = startLineForRecord(target.symbolRecord);
    target.column = startColumnForRecord(target.symbolRecord);
    target.symbolTypeText =
        SymbolTaxonomy::symbolTypeLabel(
            metadataForRecord(target.symbolRecord));
    target.ownerDisplayName =
        ownerDisplayNameForRecord(target.symbolRecord);
    target.sourceRoleDisplayName =
        SymbolTaxonomy::sourceRoleDisplayName(
            metadataForRecord(target.symbolRecord).sourceRole);
    return target;
}
