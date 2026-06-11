#include "editorsemanticcontextservice.h"

#include <Qt>

std::unique_ptr<EditorSemanticContextService>
    EditorSemanticContextService::instance = nullptr;

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

EditorSemanticContextService* EditorSemanticContextService::getInstance()
{
    if (!instance)
        instance = std::make_unique<EditorSemanticContextService>();
    return instance.get();
}

EditorSemanticContextService::EditorSemanticContextService() = default;

EditorSemanticContextService::~EditorSemanticContextService() = default;

SourceSymbolActionContext EditorSemanticContextService::sourceSymbolActionContext(
    const EditorSemanticContext& context) const
{
    return SourceNavigationService::getInstance()->symbolActionContextAtColumn(
        context.lineText,
        context.column,
        context.fileName,
        context.moduleName);
}

SourceEditorNavigationTarget EditorSemanticContextService::sourceNavigationTarget(
    const EditorSemanticContext& context,
    const std::function<bool(const QString&)>& canResolveIdentifier) const
{
    return SourceNavigationService::getInstance()->editorNavigationTargetAtColumn(
        context.lineText,
        context.column,
        canResolveIdentifier);
}

SourceEditorNavigationTarget
EditorSemanticContextService::definitionSourceNavigationTarget(
    const EditorSemanticContext& context) const
{
    return sourceNavigationTarget(
        context,
        [this, context](const QString& symbolName) {
            return canResolveDefinitionTarget(symbolName, context);
        });
}

EditorSourceNavigationTarget
EditorSemanticContextService::editorSourceNavigationTarget(
    const EditorSemanticContext& context,
    int blockPosition) const
{
    EditorSourceNavigationTarget editorTarget;
    const SourceEditorNavigationTarget sourceTarget =
        definitionSourceNavigationTarget(context);
    if (!sourceTarget.matched)
        return editorTarget;

    editorTarget.matched = true;
    editorTarget.jumpable = sourceTarget.jumpable;
    editorTarget.includeTarget = sourceTarget.includeTarget;
    editorTarget.identifierTarget = sourceTarget.identifierTarget;
    editorTarget.text = sourceTarget.text;
    editorTarget.startPos = blockPosition + sourceTarget.startColumn;
    editorTarget.endPos = blockPosition + sourceTarget.endColumn;
    editorTarget.cursorPosition = blockPosition + sourceTarget.cursorColumn;
    return editorTarget;
}

EditorSourceNavigationClickState
EditorSemanticContextService::sourceNavigationClickState(
    const EditorSourceNavigationTarget& target) const
{
    EditorSourceNavigationClickState state;
    if (!target.matched || target.text.isEmpty())
        return state;

    state.text = target.text;
    state.acceptEvent = true;
    if (target.includeTarget) {
        state.action = EditorSourceNavigationClickAction::OpenInclude;
        return state;
    }

    state.action = EditorSourceNavigationClickAction::NavigateToDefinition;
    state.contextCursorPosition =
        target.identifierTarget ? target.cursorPosition : -1;
    return state;
}

SourceIdentifierTarget EditorSemanticContextService::sourceIdentifierTarget(
    const EditorSemanticContext& context) const
{
    return SourceNavigationService::getInstance()->identifierAtColumn(
        context.lineText,
        context.column);
}

DefinitionNavigationQuery EditorSemanticContextService::definitionNavigationQuery(
    const QString& symbolName,
    const EditorSemanticContext& context) const
{
    DefinitionNavigationContext navigationContext;
    navigationContext.symbolName = symbolName;
    navigationContext.fileName = context.fileName;
    navigationContext.moduleName = context.moduleName;
    navigationContext.lineText = context.lineText;
    navigationContext.column = context.column;
    return DefinitionNavigationService::getInstance()
        ->navigationQueryForContext(navigationContext);
}

DefinitionNavigationTarget EditorSemanticContextService::resolveDefinitionTarget(
    const QString& symbolName,
    const EditorSemanticContext& context) const
{
    return DefinitionNavigationService::getInstance()->resolveTarget(
        definitionNavigationQuery(symbolName, context));
}

bool EditorSemanticContextService::canResolveDefinitionTarget(
    const QString& symbolName,
    const EditorSemanticContext& context) const
{
    return DefinitionNavigationService::getInstance()->canResolveTarget(
        definitionNavigationQuery(symbolName, context));
}

QString EditorSemanticContextService::definitionTooltipText(
    const QString& symbolName,
    const EditorSemanticContext& context) const
{
    return DefinitionNavigationService::getInstance()->tooltipText(
        definitionNavigationQuery(symbolName, context));
}

CompletionTriggerQuery EditorSemanticContextService::completionTriggerQuery(
    const EditorSemanticContext& context) const
{
    CompletionTriggerQuery query;
    query.lineUpToCursor = context.lineUpToCursor;
    query.moduleName = context.moduleName;
    query.commandModeActive = context.commandModeActive;
    return query;
}

CompletionTriggerState EditorSemanticContextService::completionTriggerState(
    const EditorSemanticContext& context) const
{
    return CompletionService::getInstance()->completionTriggerState(
        completionTriggerQuery(context));
}

EditorCompletionTextChangeState
EditorSemanticContextService::completionTextChangeState(
    const EditorSemanticContext& context) const
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

CompletionQuery EditorSemanticContextService::completionQuery(
    const QString& prefix,
    const EditorSemanticContext& context) const
{
    CompletionQuery query;
    query.prefix = prefix;
    query.fileName = context.fileName;
    query.moduleName = context.moduleName;
    query.cursorLine = context.cursorLine;
    query.cursorPosition = context.cursorPosition;
    return query;
}

QStringList EditorSemanticContextService::completionNames(
    const QString& prefix,
    const EditorSemanticContext& context) const
{
    return CompletionService::getInstance()
        ->findCompletionResult(completionQuery(prefix, context))
        .names;
}

CommandModeCompletionQuery
EditorSemanticContextService::commandModeCompletionQuery(
    const EditorSemanticContext& context) const
{
    CommandModeCompletionQuery query;
    query.lineUpToCursor = context.lineUpToCursor;
    query.fileName = context.fileName;
    query.moduleName = context.moduleName;
    query.documentText = context.documentText;
    return query;
}

CommandModeCompletionState EditorSemanticContextService::commandModeCompletionState(
    const EditorSemanticContext& context) const
{
    return CompletionService::getInstance()->commandModeCompletionState(
        commandModeCompletionQuery(context));
}

EditorCommandModeCompletionRefreshState
EditorSemanticContextService::commandModeCompletionRefreshState(
    const EditorSemanticContext& context,
    bool exitedByDoubleSpace) const
{
    EditorCommandModeCompletionRefreshState state;
    state.completion = commandModeCompletionState(context);
    state.matched = state.completion.matched;

    if (!state.matched) {
        state.resetExitedByDoubleSpace = true;
        return state;
    }

    state.commandModeActive = true;

    if (exitedByDoubleSpace) {
        state.suppressAfterExit = true;
        return state;
    }

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

CommandModeInputState EditorSemanticContextService::commandModeInputState(
    const EditorSemanticContext& context) const
{
    return CompletionService::getInstance()->commandModeInputState(
        context.lineUpToCursor);
}

CommandModeMatch EditorSemanticContextService::commandModeMatch(
    const EditorSemanticContext& context) const
{
    return CompletionService::getInstance()->matchCommandMode(
        context.lineUpToCursor);
}

EditorAlternateModeKeyState EditorSemanticContextService::alternateModeKeyState(
    const EditorAlternateModeKeyContext& context) const
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
        return state;
    }

    return state;
}

AlternateCommandCompletionState
EditorSemanticContextService::alternateCommandCompletionState(
    const QString& input) const
{
    return AlternateCommandService::getInstance()->completionState(input);
}

EditorCompletionQuery EditorSemanticContextService::editorCompletionQuery(
    const EditorSemanticContext& context) const
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

EditorCompletionState EditorSemanticContextService::editorCompletionState(
    const EditorSemanticContext& context) const
{
    return CompletionService::getInstance()->editorCompletionState(
        editorCompletionQuery(context));
}

CompletionActivationState EditorSemanticContextService::completionActivationState(
    const EditorCompletionActivationContext& context) const
{
    CompletionActivationQuery query;
    query.selectable = context.selectable;
    query.mode = completionModeForEditorState(
        context.alternateModeActive,
        context.commandModeActive);
    query.itemText = context.itemText;
    query.defaultValue = context.defaultValue;
    return completionActivationState(query);
}

CompletionActivationState EditorSemanticContextService::completionActivationState(
    const CompletionActivationQuery& query) const
{
    return CompletionService::getInstance()->completionActivationState(query);
}

CompletionPopupKeyState EditorSemanticContextService::completionPopupKeyState(
    const EditorCompletionPopupKeyContext& context) const
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

CompletionPopupKeyState EditorSemanticContextService::completionPopupKeyState(
    const CompletionPopupKeyQuery& query) const
{
    return CompletionService::getInstance()->completionPopupKeyState(query);
}
