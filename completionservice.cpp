#include "completionservice.h"

#include "completioncontexthelper.h"
#include "completioncontextquery.h"
#include "completionsemanticquery.h"
#include "completionsymbolquery.h"

#include <QVector>

std::unique_ptr<CompletionService> CompletionService::instance = nullptr;

CompletionService* CompletionService::getInstance()
{
    if (!instance)
        instance = std::make_unique<CompletionService>();
    return instance.get();
}

CompletionService::CompletionService(SemanticIndex* semanticIndex)
    : index(semanticIndex ? semanticIndex : SemanticIndex::getInstance())
{
}

CompletionService::~CompletionService() = default;

void CompletionService::setSemanticIndex(SemanticIndex* semanticIndex)
{
    index = semanticIndex ? semanticIndex : SemanticIndex::getInstance();
}

QStringList CompletionService::findCompletions(const CompletionQuery& query) const
{
    return findCompletionResult(query).names;
}

CompletionResult CompletionService::findCompletionResult(
    const CompletionQuery& query) const
{
    CompletionResult result;
    result.symbols = findCompletionSymbols(query);
    result.names = CompletionSymbolQuery::namesFromSymbols(result.symbols);
    return result;
}

QList<sym_list::SymbolInfo> CompletionService::findCompletionSymbols(
    const CompletionQuery& query) const
{
    if (!query.structTypeNameForMember.isEmpty()) {
        return CompletionSymbolQuery::structMemberSymbols(
            semanticIndex(), query.structTypeNameForMember, query.prefix);
    }

    if (query.prefix.isEmpty())
        return {};

    if (!query.moduleName.isEmpty())
        return semanticIndex()->getModuleCompletionSymbols(
            query.moduleName, query.prefix);

    return semanticIndex()->getGlobalCompletionSymbols(query.prefix);
}

QVector<QPair<QString, int>> CompletionService::findScoredAllSymbolCompletions(
    const QString& prefix,
    int maxResults) const
{
    return CompletionSymbolQuery::scoredNames(
        semanticIndex()->getCompletionSymbolNames(), prefix, maxResults);
}

QStringList CompletionService::findAllSymbolCompletions(
    const QString& prefix,
    int maxResults) const
{
    const QVector<QPair<QString, int>> scored =
        findScoredAllSymbolCompletions(prefix, maxResults);
    return CompletionSymbolQuery::namesFromScored(scored, maxResults);
}

QVector<QPair<sym_list::SymbolInfo, int>>
CompletionService::findScoredSymbolCompletionsByType(
    sym_list::sym_type_e symbolType,
    const QString& prefix,
    int maxResults) const
{
    return CompletionSymbolQuery::scoredTypedSymbols(
        semanticIndex(), symbolType, prefix, maxResults);
}

QStringList CompletionService::findSymbolCompletionsByType(
    sym_list::sym_type_e symbolType,
    const QString& prefix,
    int maxResults) const
{
    const QVector<QPair<sym_list::SymbolInfo, int>> scored =
        findScoredSymbolCompletionsByType(symbolType, prefix, maxResults);
    return CompletionSymbolQuery::symbolNamesFromScored(scored, maxResults);
}

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

bool CompletionService::relationshipCompletionsAvailable() const
{
    return CompletionContextQuery::relationshipCompletionsAvailable(
        semanticIndex());
}

QVector<QPair<QString, int>> CompletionService::findSmartCompletions(
    const QString& prefix,
    const QString& fileName,
    int cursorPosition,
    bool relationshipCompletionsEnabled) const
{
    return CompletionContextQuery::smartCompletions(
        semanticIndex(),
        prefix,
        fileName,
        cursorPosition,
        relationshipCompletionsEnabled);
}

QStringList CompletionService::findModuleChildCompletions(
    const QString& moduleName,
    const QString& prefix) const
{
    return CompletionContextQuery::moduleChildCompletions(
        semanticIndex(), moduleName, prefix);
}

QStringList CompletionService::findRelatedSymbolCompletions(
    const QString& symbolName,
    const QString& prefix) const
{
    return CompletionContextQuery::relatedSymbolCompletions(
        semanticIndex(), symbolName, prefix);
}

QStringList CompletionService::findSymbolReferenceCompletions(
    const QString& symbolName,
    const QString& prefix) const
{
    return CompletionContextQuery::symbolReferenceCompletions(
        semanticIndex(), symbolName, prefix);
}

QStringList CompletionService::findClockDomainCompletions(const QString& prefix) const
{
    return CompletionContextQuery::clockDomainCompletions(
        semanticIndex(), prefix);
}

QStringList CompletionService::findResetSignalCompletions(const QString& prefix) const
{
    return CompletionContextQuery::resetSignalCompletions(
        semanticIndex(), prefix);
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

QStringList CompletionService::findContextAwareCompletions(
    const ContextCompletionQuery& query) const
{
    return CompletionContextQuery::contextAwareCompletions(
        semanticIndex(),
        query.prefix,
        query.currentModule,
        query.context,
        query.relationshipCompletionsEnabled);
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

SemanticIndex* CompletionService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}
