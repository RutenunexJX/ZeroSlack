#include "editorcompletionquery.h"

CommandModeCompletionQuery
EditorCompletionQueryHelper::commandModeCompletionQuery(
    const EditorSemanticContext& context)
{
    CommandModeCompletionQuery query;
    query.lineUpToCursor = context.lineUpToCursor;
    query.fileName = context.fileName;
    query.moduleName = context.moduleName;
    query.packageName = context.packageName;
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

CompletionActivationState EditorCompletionQueryHelper::completionActivationState(
    const EditorCompletionActivationContext& context)
{
    CompletionActivationQuery query;
    query.selectable = context.selectable;
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
    query.modifiers = context.modifiers;
    query.currentIndexValid = context.currentIndexValid;
    query.hasRows = context.hasRows;
    return completionPopupKeyState(query);
}

CompletionPopupKeyState EditorCompletionQueryHelper::completionPopupKeyState(
    const CompletionPopupKeyQuery& query)
{
    return CompletionService::getInstance()->completionPopupKeyState(query);
}
