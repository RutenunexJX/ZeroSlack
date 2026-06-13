#include "completionservice.h"

#include "completioncontexthelper.h"
#include "completionsemanticquery.h"
#include "completionsymbolquery.h"
#include "semanticindex.h"

QString CompletionService::symbolTypeDescription(sym_list::sym_type_e symbolType) const
{
    switch (symbolType) {
    case sym_list::sym_module:
        return QStringLiteral("module");
    case sym_list::sym_reg:
        return QStringLiteral("reg");
    case sym_list::sym_wire:
        return QStringLiteral("wire");
    case sym_list::sym_logic:
        return QStringLiteral("logic");
    case sym_list::sym_task:
        return QStringLiteral("task");
    case sym_list::sym_function:
        return QStringLiteral("function");
    case sym_list::sym_parameter:
        return QStringLiteral("parameter");
    case sym_list::sym_localparam:
        return QStringLiteral("localparam");
    case sym_list::sym_struct_member:
        return QStringLiteral("member");
    default:
        return QStringLiteral("symbol");
    }
}

QStringList CompletionService::findModuleInternalVariableCompletions(
    const QString& moduleName,
    const QString& prefix) const
{
    return CompletionSymbolQuery::namesFromSymbols(
        semanticIndex()->getModuleCompletionSymbols(moduleName, prefix));
}

QStringList CompletionService::findModuleSymbolsByType(
    const QString& moduleName,
    sym_list::sym_type_e symbolType,
    const QString& prefix) const
{
    return CompletionSymbolQuery::moduleSymbolsByType(
        semanticIndex(), moduleName, symbolType, prefix);
}

QStringList CompletionService::findGlobalSymbolCompletions(const QString& prefix) const
{
    return CompletionSymbolQuery::namesFromSymbols(
        semanticIndex()->getGlobalCompletionSymbols(prefix));
}

QStringList CompletionService::findGlobalSymbolsByType(
    sym_list::sym_type_e symbolType,
    const QString& prefix) const
{
    return CompletionSymbolQuery::globalSymbolsByType(
        semanticIndex(), symbolType, prefix);
}

QStringList CompletionService::findVariableCompletionsInScope(
    const QString& moduleName,
    sym_list::sym_type_e variableType,
    const QString& prefix) const
{
    if (moduleName.isEmpty()) {
        return CompletionSymbolQuery::namesFromSymbols(
            semanticIndex()->getTypedCompletionSymbols(variableType, prefix));
    }
    return findModuleSymbolsByType(moduleName, variableType, prefix);
}

QStringList CompletionService::findTaskFunctionCompletions(const QString& prefix) const
{
    return CompletionSymbolQuery::taskFunctionCompletions(semanticIndex(), prefix);
}

QStringList CompletionService::findInstantiableModuleCompletions(const QString& prefix) const
{
    return findGlobalSymbolsByType(sym_list::sym_module, prefix);
}

QStringList CompletionService::findStructMemberCompletions(
    const QString& prefix,
    const QString& structTypeName) const
{
    CompletionQuery query;
    query.prefix = prefix;
    query.structTypeNameForMember = structTypeName;
    return CompletionSymbolQuery::namesFromSymbols(
        CompletionSymbolQuery::structMemberSymbols(
            semanticIndex(), query.structTypeNameForMember, query.prefix));
}

QStringList CompletionService::findEnumValueCompletions(
    const QString& prefix,
    const QString& enumTypeName) const
{
    return CompletionSemanticQuery::enumValueCompletions(
        semanticIndex(), prefix, enumTypeName);
}

QString CompletionService::findEnumTypeForVariable(
    const QString& variableName,
    const QString& moduleName) const
{
    return CompletionSemanticQuery::enumTypeForVariable(
        semanticIndex(), variableName, moduleName);
}

QStringList CompletionService::findModulePortCompletions(
    const QString& prefix,
    const QString& moduleTypeName) const
{
    return CompletionSemanticQuery::modulePortCompletions(
        semanticIndex(), prefix, moduleTypeName);
}

QList<sym_list::SymbolInfo> CompletionService::findModuleInternalSymbolInfosByType(
    const QString& moduleName,
    sym_list::sym_type_e symbolType,
    const QString& prefix,
    bool useRelationshipFallback) const
{
    return CompletionSemanticQuery::moduleInternalSymbolInfosByType(
        semanticIndex(),
        moduleName,
        symbolType,
        prefix,
        useRelationshipFallback);
}

QList<sym_list::SymbolInfo> CompletionService::findModuleContextSymbolInfosByType(
    const QString& moduleName,
    const QString& fileName,
    sym_list::sym_type_e symbolType,
    const QString& prefix) const
{
    return CompletionSemanticQuery::moduleContextSymbolInfosByType(
        semanticIndex(),
        moduleName,
        fileName,
        symbolType,
        prefix);
}

QList<sym_list::SymbolInfo> CompletionService::findGlobalSymbolInfosByType(
    sym_list::sym_type_e symbolType,
    const QString& prefix) const
{
    return CompletionSemanticQuery::globalSymbolInfosByType(
        semanticIndex(), symbolType, prefix);
}

QString CompletionService::currentModuleAt(const QString& fileName, int cursorPosition) const
{
    return CompletionSemanticQuery::currentModuleAt(
        semanticIndex(), fileName, cursorPosition);
}

QString CompletionService::getStructTypeForVariable(const QString& variableName,
                                                    const QString& moduleName) const
{
    return CompletionSemanticQuery::structTypeForVariable(
        semanticIndex(), variableName, moduleName);
}

bool CompletionService::tryParseStructMemberContext(const QString& line,
                                                    QString& outVariableName,
                                                    QString& outMemberPrefix) const
{
    return CompletionContextHelper::tryParseStructMember(
        line,
        outVariableName,
        outMemberPrefix);
}
