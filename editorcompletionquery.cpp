#include "editorcompletionquery.h"

#include <Qt>

namespace {
CompletionActivationMode completionModeForEditorState(
    bool alternateModeActive,
    bool commandModeActive)
{
    if (alternateModeActive)
        return CompletionActivationMode::AlternateMode;
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
    state.commandInput = commandModeInputState(context);
    state.commandModeActive = state.commandInput.matched;

    EditorSemanticContext triggerContext = context;
    triggerContext.commandModeActive = state.commandModeActive;
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

EditorAlternateModeCompletionDisplayState
EditorCompletionQueryHelper::alternateModeCompletionDisplayState(
    const QString& input)
{
    const AlternateCommandCompletionState completion =
        alternateCommandCompletionState(input);

    EditorAlternateModeCompletionDisplayState state;
    state.updateCompletions = true;
    state.showPopup = completion.showCompletions;
    state.normalizedInput = completion.normalizedInput;
    state.matches = completion.matches;
    return state;
}

EditorAlternateModeKeyState EditorCompletionQueryHelper::alternateModeKeyState(
    const EditorAlternateModeKeyContext& context)
{
    EditorAlternateModeKeyState state;

    if (context.key == Qt::Key_Backspace) {
        if (context.buffer.isEmpty()) {
            state.action = EditorAlternateModeKeyAction::RefreshCompletions;
            state.nextInput = QString();
        } else {
            state.action = EditorAlternateModeKeyAction::UpdateInput;
            state.nextInput = context.buffer.left(context.buffer.size() - 1);
        }
        state.completion = alternateModeCompletionDisplayState(state.nextInput);
        return state;
    }

    if (context.key == Qt::Key_Escape) {
        state.action = EditorAlternateModeKeyAction::ClearAndHide;
        state.hidePopup = true;
        state.clearBuffer = true;
        return state;
    }

    if (context.key == Qt::Key_Return || context.key == Qt::Key_Enter) {
        if (!context.buffer.isEmpty()) {
            state.action = EditorAlternateModeKeyAction::ExecuteCommand;
            state.command = context.buffer;
        }
        return state;
    }

    if (!context.text.isEmpty() && context.text.at(0).isPrint()) {
        state.action = EditorAlternateModeKeyAction::UpdateInput;
        state.nextInput = context.buffer + context.text;
        state.completion = alternateModeCompletionDisplayState(state.nextInput);
        return state;
    }

    return state;
}

AlternateCommandCompletionState
EditorCompletionQueryHelper::alternateCommandCompletionState(
    const QString& input)
{
    return AlternateCommandService::getInstance()->completionState(input);
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
        context.alternateModeActive,
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
        context.alternateModeActive,
        context.commandModeActive);
    query.currentIndexValid = context.currentIndexValid;
    query.hasRows = context.hasRows;
    query.alternateBufferEmpty = context.alternateBufferEmpty;
    return completionPopupKeyState(query);
}

CompletionPopupKeyState EditorCompletionQueryHelper::completionPopupKeyState(
    const CompletionPopupKeyQuery& query)
{
    return CompletionService::getInstance()->completionPopupKeyState(query);
}
