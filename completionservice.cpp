#include "completionservice.h"

#include "completioncommandmode.h"
#include "completioncontexthelper.h"
#include "completioncontextquery.h"
#include "completionmatcher.h"
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

bool CompletionService::matchesCompletionAbbreviation(
    const QString& text,
    const QString& abbreviation) const
{
    return CompletionMatcher::matchesAbbreviation(text, abbreviation);
}

int CompletionService::calculateCompletionMatchScore(
    const QString& text,
    const QString& abbreviation) const
{
    return CompletionMatcher::calculateContextMatchScore(text, abbreviation);
}

int CompletionService::completionItemScore(const QString& text, const QString& prefix) const
{
    return CompletionMatcher::completionItemScore(text, prefix);
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

QList<CommandModeCommand> CompletionService::commandModeCommands() const
{
    return CompletionCommandMode::commands();
}

CommandModeMatch CompletionService::matchCommandMode(const QString& lineUpToCursor) const
{
    return CompletionCommandMode::matchCommandMode(lineUpToCursor);
}

CommandModeInputState CompletionService::commandModeInputState(
    const QString& lineUpToCursor) const
{
    return CompletionCommandMode::inputState(lineUpToCursor);
}

CommandModeCompletionState CompletionService::commandModeCompletionState(
    const CommandModeCompletionQuery& query) const
{
    const CommandModeInputState inputState =
        commandModeInputState(query.lineUpToCursor);

    CommandModeCompletionState state;
    if (!inputState.matched)
        return state;

    state.matched = true;
    state.exitRequested = inputState.exitRequested;
    state.prefixPosition = inputState.prefixPosition;
    state.input = inputState.input;
    state.completionPrefix = inputState.input.trimmed();
    state.command = inputState.command;

    if (state.exitRequested)
        return state;

    CommandCompletionQuery completionQuery;
    completionQuery.prefix = state.completionPrefix;
    completionQuery.fileName = query.fileName;
    completionQuery.moduleName = query.moduleName;
    completionQuery.documentText = query.documentText;
    completionQuery.symbolType = state.command.symbolType;

    state.symbols = findCommandCompletionSymbols(completionQuery);
    if (state.symbols.isEmpty()
        && CompletionSymbolQuery::isModuleRangeSymbolType(state.command.symbolType)
        && completionQuery.moduleName.isEmpty()) {
        state.hidePopup = true;
        return state;
    }

    state.showCompletions = true;
    return state;
}

EditorCompletionState CompletionService::editorCompletionState(
    const EditorCompletionQuery& query) const
{
    EditorCompletionState state;

    QString variableName;
    QString memberPrefix;
    const QString lineForParse = query.lineUpToCursor.trimmed();
    if (CompletionContextHelper::tryParseStructMember(
            lineForParse, variableName, memberPrefix)) {
        const QString structTypeName =
            getStructTypeForVariable(variableName, query.moduleName);
        if (!structTypeName.isEmpty()) {
            CompletionQuery completionQuery;
            completionQuery.prefix = memberPrefix;
            completionQuery.fileName = query.fileName;
            completionQuery.moduleName = query.moduleName;
            completionQuery.structTypeNameForMember = structTypeName;
            completionQuery.cursorLine = query.cursorLine;
            completionQuery.cursorPosition = query.cursorPosition;

            state.available = true;
            state.prefix = memberPrefix;
            state.replacementStartColumn =
                query.lineUpToCursor.lastIndexOf(QLatin1Char('.')) + 1;
            state.completion = findCompletionResult(completionQuery);
            return state;
        }
    }

    if (query.wordPrefix.length() < 1)
        return state;

    CompletionQuery completionQuery;
    completionQuery.prefix = query.wordPrefix;
    completionQuery.fileName = query.fileName;
    completionQuery.moduleName = query.moduleName;
    completionQuery.cursorLine = query.cursorLine;
    completionQuery.cursorPosition = query.cursorPosition;

    state.available = true;
    state.prefix = query.wordPrefix;
    state.replacementStartColumn =
        qMax(0, query.lineUpToCursor.size() - query.wordPrefix.size());
    state.completion = findCompletionResult(completionQuery);
    return state;
}

CompletionTriggerState CompletionService::completionTriggerState(
    const CompletionTriggerQuery& query) const
{
    CompletionTriggerState state;

    if (query.lineUpToCursor.isEmpty()) {
        state.hidePopup = !query.commandModeActive;
        return state;
    }

    const QChar lastChar = query.lineUpToCursor.back();
    if (query.commandModeActive) {
        state.continueCompletion =
            lastChar.isLetterOrNumber()
            || lastChar == QLatin1Char('_')
            || lastChar == QLatin1Char(' ');
        state.hidePopup = false;
        return state;
    }

    if (lastChar.isLetterOrNumber()
        || lastChar == QLatin1Char('_')
        || lastChar == QLatin1Char('.')) {
        state.continueCompletion = true;
        return state;
    }

    if (lastChar != QLatin1Char(' ')) {
        state.hidePopup = true;
        return state;
    }

    const QString lineBeforeSpace =
        query.lineUpToCursor.left(query.lineUpToCursor.size() - 1).trimmed();
    QString variableName;
    QString memberPrefix;
    if (!CompletionContextHelper::tryParseStructMember(
            lineBeforeSpace, variableName, memberPrefix)) {
        state.hidePopup = true;
        return state;
    }

    state.continueCompletion =
        !getStructTypeForVariable(variableName, query.moduleName).isEmpty();
    state.hidePopup = !state.continueCompletion;
    return state;
}

bool CompletionService::shouldContinueCompletion(
    const CompletionTriggerQuery& query) const
{
    return completionTriggerState(query).continueCompletion;
}

CompletionActivationState CompletionService::completionActivationState(
    const CompletionActivationQuery& query) const
{
    return CompletionCommandMode::activationState(query);
}

CompletionPopupKeyState CompletionService::completionPopupKeyState(
    const CompletionPopupKeyQuery& query) const
{
    return CompletionCommandMode::popupKeyState(query);
}

CommandSymbolPresentation CompletionService::commandSymbolPresentation(
    sym_list::sym_type_e symbolType) const
{
    return CompletionCommandMode::symbolPresentation(symbolType);
}

CommandSymbolCompletionItem CompletionService::commandSymbolCompletionItem(
    const sym_list::SymbolInfo& symbol,
    sym_list::sym_type_e requestedType,
    const QString& prefix) const
{
    return CompletionCommandMode::symbolCompletionItem(symbol, requestedType, prefix);
}

QList<int> CompletionService::findCompletionAbbreviationPositions(
    const QString& text,
    const QString& abbreviation) const
{
    return CompletionMatcher::abbreviationPositions(text, abbreviation);
}

QVector<QPair<QString, int>> CompletionService::findScoredKeywordCompletions(
    const QString& prefix) const
{
    return CompletionMatcher::scoredKeywordCompletions(prefix);
}

QStringList CompletionService::findKeywordCompletions(
    const QString& prefix,
    int maxResults) const
{
    return CompletionMatcher::keywordCompletions(prefix, maxResults);
}

QStringList CompletionService::findKeywordAbbreviationMatches(
    const QStringList& candidates,
    const QString& abbreviation) const
{
    return CompletionMatcher::keywordAbbreviationMatches(candidates, abbreviation);
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

QStringList CompletionService::findScopeCompletions(const CompletionQuery& query) const
{
    return CompletionSymbolQuery::scopeCompletions(
        semanticIndex(), query.fileName, query.cursorLine, query.prefix);
}

QStringList CompletionService::findCommandCompletions(const CommandCompletionQuery& query) const
{
    return CompletionSymbolQuery::namesFromSymbols(findCommandSymbolsFromIndex(query));
}

QList<sym_list::SymbolInfo> CompletionService::findCommandCompletionSymbols(
    const CommandCompletionQuery& query) const
{
    const bool useSymbolInfoDirectly =
        query.symbolType == sym_list::sym_packed_struct_var
        || query.symbolType == sym_list::sym_unpacked_struct_var
        || query.symbolType == sym_list::sym_packed_struct
        || query.symbolType == sym_list::sym_unpacked_struct;

    if (useSymbolInfoDirectly) {
        if (query.moduleName.isEmpty())
            return {};

        semanticIndex()->refreshStructTypedefEnumForFile(query.fileName, query.documentText);

        return semanticIndex()->getModuleContextSymbolsByType(
            query.moduleName,
            query.fileName,
            query.symbolType,
            query.prefix);
    }

    return findCommandSymbolsFromIndex(query);
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
    return semanticIndex()->getEnumValueCompletionNames(prefix, enumTypeName);
}

QString CompletionService::findEnumTypeForVariable(
    const QString& variableName,
    const QString& moduleName) const
{
    return semanticIndex()->enumTypeForVariable(variableName, moduleName);
}

QStringList CompletionService::findModulePortCompletions(
    const QString& prefix,
    const QString& moduleTypeName) const
{
    return semanticIndex()->getModulePortCompletionNames(prefix, moduleTypeName);
}

QList<sym_list::SymbolInfo> CompletionService::findModuleInternalSymbolInfosByType(
    const QString& moduleName,
    sym_list::sym_type_e symbolType,
    const QString& prefix,
    bool useRelationshipFallback) const
{
    return semanticIndex()->getModuleInternalSymbolsByType(
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
    return semanticIndex()->getModuleContextSymbolsByType(
        moduleName,
        fileName,
        symbolType,
        prefix);
}

QList<sym_list::SymbolInfo> CompletionService::findGlobalSymbolInfosByType(
    sym_list::sym_type_e symbolType,
    const QString& prefix) const
{
    return semanticIndex()->getGlobalSymbolInfosByType(symbolType, prefix);
}

QString CompletionService::currentModuleAt(const QString& fileName, int cursorPosition) const
{
    return semanticIndex()->currentModuleAt(fileName, cursorPosition);
}

QString CompletionService::getStructTypeForVariable(const QString& variableName,
                                                    const QString& moduleName) const
{
    return semanticIndex()->getStructTypeForVariable(variableName, moduleName);
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

QList<sym_list::SymbolInfo> CompletionService::findCommandSymbolsFromIndex(
    const CommandCompletionQuery& query) const
{
    return semanticIndex()->getCommandCompletionSymbols(
        query.moduleName,
        query.symbolType,
        query.prefix);
}
