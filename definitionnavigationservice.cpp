#include "definitionnavigationservice.h"

#include "definitionservice.h"

#include <QFileInfo>
#include <QtGlobal>

std::unique_ptr<DefinitionNavigationService> DefinitionNavigationService::instance = nullptr;

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
    target.symbolName = result.symbol.symbolName;
    target.fileName = result.symbol.fileName;
    target.line = result.symbol.startLine;
    target.column = result.symbol.startColumn;
    target.symbolType = result.symbol.symbolType;
    target.symbolTypeText = symbolTypeText(result.symbol.symbolType);
    return target;
}

QString DefinitionNavigationService::symbolTypeText(sym_list::sym_type_e symbolType)
{
    switch (symbolType) {
    case sym_list::sym_reg: return QStringLiteral("reg");
    case sym_list::sym_wire: return QStringLiteral("wire");
    case sym_list::sym_logic: return QStringLiteral("logic");
    case sym_list::sym_module: return QStringLiteral("module");
    case sym_list::sym_task: return QStringLiteral("task");
    case sym_list::sym_function: return QStringLiteral("function");
    default:
        return QStringLiteral("unknown_%1").arg(static_cast<int>(symbolType));
    }
}
