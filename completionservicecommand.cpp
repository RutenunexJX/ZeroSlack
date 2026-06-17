#include "completionservice.h"

#include "completioncommandmode.h"
#include "completionsemanticquery.h"
#include "completionsymbolquery.h"
#include "symboltaxonomy.h"

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
    state.requestedKind = inputState.command.symbolType;

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
        && SymbolTaxonomy::isDirectModuleContextCompletionRequest(state.command.symbolType)
        && completionQuery.moduleName.isEmpty()) {
        state.hidePopup = true;
        return state;
    }

    state.showCompletions = true;
    state.symbolStableKeys.reserve(state.symbols.size());
    state.symbolRecords.reserve(state.symbols.size());
    for (const sym_list::SymbolInfo& symbol : state.symbols) {
        const SemanticSymbolRecord record =
            semanticSymbolRecordForSymbol(symbol);
        state.symbolRecords.append(record);
        if (record.stableKey.isValid()) {
            state.symbolStableKeys.append(record.stableKey);
        } else {
            state.symbolStableKeys.append(symbolStableKeyForSymbol(symbol));
        }
    }
    return state;
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

QStringList CompletionService::findCommandCompletions(const CommandCompletionQuery& query) const
{
    return CompletionSymbolQuery::namesFromSymbols(
        CompletionSemanticQuery::commandSymbols(semanticIndex(), query));
}

QList<sym_list::SymbolInfo> CompletionService::findCommandCompletionSymbols(
    const CommandCompletionQuery& query) const
{
    return CompletionSemanticQuery::commandSymbols(semanticIndex(), query);
}
