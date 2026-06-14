#include "definitionservice.h"

#include "completionservice.h"

std::unique_ptr<DefinitionService> DefinitionService::instance = nullptr;

namespace {
SemanticDefinitionQuery toSemanticDefinitionQuery(const DefinitionQuery& query)
{
    SemanticDefinitionQuery semanticQuery;
    semanticQuery.symbolName = query.symbolName;
    semanticQuery.fileName = query.fileName;
    semanticQuery.moduleName = query.moduleName;
    semanticQuery.structTypeNameForMember = query.structTypeNameForMember;
    return semanticQuery;
}

DefinitionResult toDefinitionResult(const SemanticDefinitionResult& semanticResult)
{
    DefinitionResult result;
    result.found = semanticResult.found;
    result.localFile = semanticResult.localFile;
    result.symbol = semanticResult.symbol;
    return result;
}

QString interfaceScopeFromDataType(const QString& dataType)
{
    if (dataType.isEmpty())
        return QString();
    const int dot = dataType.indexOf(QLatin1Char('.'));
    return dot >= 0 ? dataType.left(dot) : dataType;
}

bool interfaceLikeOwnerType(sym_list::sym_type_e type)
{
    return type == sym_list::sym_interface
        || type == sym_list::sym_inst
        || type == sym_list::sym_port_interface
        || type == sym_list::sym_port_interface_modport;
}
}

DefinitionService* DefinitionService::getInstance()
{
    if (!instance)
        instance = std::make_unique<DefinitionService>();
    return instance.get();
}

DefinitionService::DefinitionService(SemanticIndex* semanticIndex)
    : index(semanticIndex ? semanticIndex : SemanticIndex::getInstance())
{
}

DefinitionService::~DefinitionService() = default;

void DefinitionService::setSemanticIndex(SemanticIndex* semanticIndex)
{
    index = semanticIndex ? semanticIndex : SemanticIndex::getInstance();
}

DefinitionResult DefinitionService::resolveDefinition(const DefinitionQuery& query) const
{
    DefinitionResult empty;
    if (query.symbolName.isEmpty())
        return empty;

    const DefinitionQuery resolvedQuery = withResolvedMemberContext(query);
    return toDefinitionResult(
        semanticIndex()->resolveDefinition(toSemanticDefinitionQuery(resolvedQuery)));
}

QList<sym_list::SymbolInfo> DefinitionService::findDefinitions(const DefinitionQuery& query) const
{
    const DefinitionQuery resolvedQuery = withResolvedMemberContext(query);
    return semanticIndex()->findDefinitionSymbols(toSemanticDefinitionQuery(resolvedQuery));
}

bool DefinitionService::canResolveDefinition(const DefinitionQuery& query) const
{
    return resolveDefinition(query).found;
}

bool DefinitionService::isDefinition(const sym_list::SymbolInfo& symbol,
                                     const QString& searchWord) const
{
    if (symbol.symbolName != searchWord)
        return false;

    switch (symbol.symbolType) {
    case sym_list::sym_module:
    case sym_list::sym_interface:
    case sym_list::sym_interface_modport:
    case sym_list::sym_package:
    case sym_list::sym_inst:
    case sym_list::sym_task:
    case sym_list::sym_function:
    case sym_list::sym_port_input:
    case sym_list::sym_port_output:
    case sym_list::sym_port_inout:
    case sym_list::sym_port_ref:
    case sym_list::sym_port_interface:
    case sym_list::sym_port_interface_modport:
    case sym_list::sym_reg:
    case sym_list::sym_wire:
    case sym_list::sym_logic:
    case sym_list::sym_parameter:
    case sym_list::sym_localparam:
    case sym_list::sym_packed_struct:
    case sym_list::sym_unpacked_struct:
    case sym_list::sym_packed_struct_var:
    case sym_list::sym_unpacked_struct_var:
    case sym_list::sym_struct_member:
    case sym_list::sym_typedef:
    case sym_list::sym_enum_var:
    case sym_list::sym_enum_value:
        return true;
    default:
        return false;
    }
}

SemanticIndex* DefinitionService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}

DefinitionQuery DefinitionService::withResolvedMemberContext(const DefinitionQuery& query) const
{
    if (!query.structTypeNameForMember.isEmpty()
        || query.linePrefixBeforeCursor.isEmpty()) {
        return query;
    }

    DefinitionQuery resolved = query;
    QString variableName;
    QString memberPrefix;
    if (!CompletionService::getInstance()->tryParseStructMemberContext(
            query.linePrefixBeforeCursor.trimmed(),
            variableName,
            memberPrefix)) {
        return resolved;
    }

    if (variableName.isEmpty())
        return resolved;

    resolved.structTypeNameForMember =
        semanticIndex()->getStructTypeForVariable(variableName, query.moduleName);
    if (!resolved.structTypeNameForMember.isEmpty())
        return resolved;

    const QList<sym_list::SymbolInfo> candidates = semanticIndex()->getSymbols();
    for (const sym_list::SymbolInfo& symbol : candidates) {
        if (symbol.symbolName != variableName || !interfaceLikeOwnerType(symbol.symbolType))
            continue;
        if (!query.moduleName.isEmpty()
            && !symbol.moduleScope.isEmpty()
            && symbol.moduleScope != query.moduleName) {
            continue;
        }
        if (symbol.symbolType == sym_list::sym_interface) {
            resolved.structTypeNameForMember = symbol.symbolName;
            return resolved;
        }
        resolved.structTypeNameForMember = interfaceScopeFromDataType(symbol.dataType);
        if (!resolved.structTypeNameForMember.isEmpty())
            return resolved;
    }
    return resolved;
}
