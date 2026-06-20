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
    state.commandKind = inputState.command.kind;

    if (state.exitRequested)
        return state;

    CommandCompletionQuery completionQuery;
    completionQuery.prefix = state.completionPrefix;
    completionQuery.fileName = query.fileName;
    completionQuery.moduleName = query.moduleName;
    completionQuery.documentText = query.documentText;
    completionQuery.commandKind = state.command.kind;

    state.symbolRecords = findCommandCompletionSymbolRecords(completionQuery);
    if (state.symbolRecords.isEmpty()
        && CompletionCommandMode::requiresModuleContext(state.command.kind)
        && completionQuery.moduleName.isEmpty()) {
        state.hidePopup = true;
        return state;
    }

    state.showCompletions = true;
    state.symbolStableKeys.reserve(state.symbolRecords.size());
    for (const SemanticSymbolRecord& record : state.symbolRecords) {
        state.symbolStableKeys.append(record.stableKey);
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
    CompletionCommandKind kind) const
{
    return CompletionCommandMode::symbolPresentation(kind);
}

CommandSymbolCompletionItem CompletionService::commandSymbolCompletionItem(
    const SemanticSymbolRecord& record,
    CompletionCommandKind requestedKind,
    const QString& prefix) const
{
    return CompletionCommandMode::symbolCompletionItem(record, requestedKind, prefix);
}

QStringList CompletionService::findCommandCompletions(const CommandCompletionQuery& query) const
{
    return CompletionSymbolQuery::namesFromRecords(
        findCommandCompletionSymbolRecords(query));
}

QList<SemanticSymbolRecord> CompletionService::findCommandCompletionSymbolRecords(
    const CommandCompletionQuery& query) const
{
    return CompletionSemanticQuery::commandSymbolRecords(semanticIndex(), query);
}
