#include "editorcompletionquery.h"

#include <Qt>

namespace {
CompletionActivationMode completionModeForEditorState(
    bool commandModeActive)
{
    if (commandModeActive)
        return CompletionActivationMode::CommandMode;
    return CompletionActivationMode::EditorWord;
}
}

CompletionTriggerQuery EditorCompletionQueryHelper::completionTriggerQuery(
    const EditorSemanticContext& context)
{
    CompletionTriggerQuery query;
    query.lineUpToCursor = context.lineUpToCursor;
    query.moduleName = context.moduleName;
    query.commandModeActive = context.commandModeActive;
    return query;
}

CompletionTriggerState EditorCompletionQueryHelper::completionTriggerState(
    const EditorSemanticContext& context)
{
    return CompletionService::getInstance()->completionTriggerState(
        completionTriggerQuery(context));
}

EditorCompletionTextChangeState
EditorCompletionQueryHelper::completionTextChangeState(
    const EditorSemanticContext& context)
{
    EditorCompletionTextChangeState state;
    if (CompletionService::getInstance()
            ->matchCommandMode(context.lineUpToCursor)
            .matched) {
        state.hidePopup = true;
        return state;
    }

    EditorSemanticContext triggerContext = context;
    triggerContext.commandModeActive = false;
    state.trigger = completionTriggerState(triggerContext);
    state.startCompletionTimer = state.trigger.continueCompletion;
    state.hidePopup = state.trigger.hidePopup;
    return state;
}

CompletionQuery EditorCompletionQueryHelper::completionQuery(
    const QString& prefix,
    const EditorSemanticContext& context)
{
    CompletionQuery query;
    query.prefix = prefix;
    query.fileName = context.fileName;
    query.moduleName = context.moduleName;
    query.cursorLine = context.cursorLine;
    query.cursorPosition = context.cursorPosition;
    return query;
}

QStringList EditorCompletionQueryHelper::completionNames(
    const QString& prefix,
    const EditorSemanticContext& context)
{
    return CompletionService::getInstance()
        ->findCompletionResult(completionQuery(prefix, context))
        .names;
}

CommandModeCompletionQuery
EditorCompletionQueryHelper::commandModeCompletionQuery(
    const EditorSemanticContext& context)
{
    CommandModeCompletionQuery query;
    query.lineUpToCursor = context.lineUpToCursor;
    query.fileName = context.fileName;
    query.moduleName = context.moduleName;
    query.documentText = context.documentText;
    query.cursorLine = context.cursorLine;
    query.cursorPosition = context.cursorPosition;
    return query;
}

CommandModeCompletionState
EditorCompletionQueryHelper::commandModeCompletionState(
    const EditorSemanticContext& context)
{
    return CompletionService::getInstance()->commandModeCompletionState(
        commandModeCompletionQuery(context));
}

EditorCommandModeCompletionRefreshState
EditorCompletionQueryHelper::commandModeCompletionRefreshState(
    const EditorSemanticContext& context,
    bool exitedByDoubleSpace)
{
    Q_UNUSED(exitedByDoubleSpace)

    EditorCommandModeCompletionRefreshState state;
    state.completion = commandModeCompletionState(context);
    state.matched = state.completion.matched;

    if (!state.matched) {
        state.resetExitedByDoubleSpace = true;
        return state;
    }

    state.commandModeActive = true;

    if (state.completion.exitRequested) {
        state.commandModeActive = false;
        state.exitRequested = true;
        state.markExitedByDoubleSpace = true;
        state.clearCommandHighlight = true;
        return state;
    }

    state.highlightCommand = state.completion.prefixPosition >= 0;
    state.hidePopup = state.completion.hidePopup;
    state.showCompletions = state.completion.showCompletions;
    return state;
}

CommandModeInputState EditorCompletionQueryHelper::commandModeInputState(
    const EditorSemanticContext& context)
{
    return CompletionService::getInstance()->commandModeInputState(
        context.lineUpToCursor);
}

CommandModeMatch EditorCompletionQueryHelper::commandModeMatch(
    const EditorSemanticContext& context)
{
    return CompletionService::getInstance()->matchCommandMode(
        context.lineUpToCursor);
}

EditorCompletionQuery EditorCompletionQueryHelper::editorCompletionQuery(
    const EditorSemanticContext& context)
{
    EditorCompletionQuery query;
    query.lineUpToCursor = context.lineUpToCursor;
    query.wordPrefix = context.wordPrefix;
    query.fileName = context.fileName;
    query.moduleName = context.moduleName;
    query.cursorLine = context.cursorLine;
    query.cursorPosition = context.cursorPosition;
    return query;
}

EditorCompletionState EditorCompletionQueryHelper::editorCompletionState(
    const EditorSemanticContext& context)
{
    return CompletionService::getInstance()->editorCompletionState(
        editorCompletionQuery(context));
}

CompletionActivationState EditorCompletionQueryHelper::completionActivationState(
    const EditorCompletionActivationContext& context)
{
    CompletionActivationQuery query;
    query.selectable = context.selectable;
    query.mode = completionModeForEditorState(
        context.commandModeActive);
    query.itemText = context.itemText;
    query.defaultValue = context.defaultValue;
    query.selectionStart = context.selectionStart;
    query.selectionLength = context.selectionLength;
    query.templateSlots = context.templateSlots;
    return completionActivationState(query);
}

CompletionActivationState EditorCompletionQueryHelper::completionActivationState(
    const CompletionActivationQuery& query)
{
    return CompletionService::getInstance()->completionActivationState(query);
}

CompletionPopupKeyState EditorCompletionQueryHelper::completionPopupKeyState(
    const EditorCompletionPopupKeyContext& context)
{
    CompletionPopupKeyQuery query;
    query.key = context.key;
    query.mode = completionModeForEditorState(
        context.commandModeActive);
    query.currentIndexValid = context.currentIndexValid;
    query.hasRows = context.hasRows;
    return completionPopupKeyState(query);
}

CompletionPopupKeyState EditorCompletionQueryHelper::completionPopupKeyState(
    const CompletionPopupKeyQuery& query)
{
    return CompletionService::getInstance()->completionPopupKeyState(query);
}
