#include "completionservice.h"

#include "completioncontexthelper.h"
#include "completionsemanticquery.h"
#include "completionsymbolquery.h"
#include "semanticindex.h"

#include <Qt>

QStringList CompletionService::findModuleInternalVariableCompletions(
    const QString& moduleName,
    const QString& prefix) const
{
    return CompletionSymbolQuery::namesFromRecords(
        semanticIndex()->getModuleCompletionSymbolRecords(moduleName, prefix));
}

QStringList CompletionService::findModuleSymbolsByKind(
    const QString& moduleName,
    CompletionCommandKind commandKind,
    const QString& prefix) const
{
    CommandCompletionQuery query;
    query.prefix = prefix;
    query.moduleName = moduleName;
    query.commandKind = commandKind;
    return CompletionSymbolQuery::namesFromRecords(
        CompletionSemanticQuery::commandSymbolRecords(semanticIndex(), query));
}

QStringList CompletionService::findGlobalSymbolCompletions(const QString& prefix) const
{
    return CompletionSymbolQuery::namesFromRecords(
        semanticIndex()->getGlobalCompletionSymbolRecords(prefix));
}

QStringList CompletionService::findGlobalSymbolsByKind(
    CompletionCommandKind commandKind,
    const QString& prefix) const
{
    CommandCompletionQuery query;
    query.prefix = prefix;
    query.commandKind = commandKind;
    return CompletionSymbolQuery::namesFromRecords(
        CompletionSemanticQuery::commandSymbolRecords(semanticIndex(), query));
}

QStringList CompletionService::findVariableCompletionsInScope(
    const QString& moduleName,
    CompletionCommandKind commandKind,
    const QString& prefix) const
{
    return moduleName.isEmpty()
        ? CompletionSymbolQuery::namesFromRecords(
              CompletionSemanticQuery::typedSymbolRecords(
                  semanticIndex(),
                  commandKind,
                  prefix))
        : findModuleSymbolsByKind(moduleName, commandKind, prefix);
}

QStringList CompletionService::findTaskFunctionCompletions(const QString& prefix) const
{
    QStringList result = CompletionSymbolQuery::namesFromRecords(
        CompletionSemanticQuery::typedSymbolRecords(
            semanticIndex(),
            CompletionCommandKind::Task,
            prefix));
    result.append(CompletionSymbolQuery::namesFromRecords(
        CompletionSemanticQuery::typedSymbolRecords(
            semanticIndex(),
            CompletionCommandKind::Function,
            prefix)));
    result.removeDuplicates();
    result.sort(Qt::CaseInsensitive);
    return result;
}

QStringList CompletionService::findInstantiableModuleCompletions(const QString& prefix) const
{
    return findGlobalSymbolsByKind(CompletionCommandKind::Module, prefix);
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
