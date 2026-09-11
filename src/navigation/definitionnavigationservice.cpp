#include "definitionnavigationservice.h"

#include "definitionservice.h"
#include "sourcenavigationservice.h"
#include "symboltaxonomy.h"

#include <QFileInfo>
#include <QtGlobal>

std::unique_ptr<DefinitionNavigationService> DefinitionNavigationService::instance = nullptr;

namespace {
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

int linePrefixColumnForContext(const DefinitionNavigationContext& context)
{
    int prefixColumn = qBound(0, context.column, context.lineText.size());
    const SourceIdentifierTarget identifier =
        SourceNavigationService::getInstance()->identifierAtColumn(
            context.lineText,
            context.column);
    if (identifier.matched)
        prefixColumn = qMax(prefixColumn, identifier.endColumn);
    return prefixColumn;
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
    query.cursorLine = context.cursorLine;
    query.cursorColumn = context.column;

    if (context.column >= 0) {
        const int prefixColumn = linePrefixColumnForContext(context);
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
    definitionQuery.cursorLine = query.cursorLine;
    definitionQuery.cursorColumn = query.cursorColumn;
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
            semanticMetadataForSymbolRecord(target.symbolRecord));
    target.ownerDisplayName =
        ownerDisplayNameForRecord(target.symbolRecord);
    target.sourceRoleDisplayName =
        SymbolTaxonomy::sourceRoleDisplayName(
            semanticMetadataForSymbolRecord(target.symbolRecord).sourceRole);
    return target;
}
