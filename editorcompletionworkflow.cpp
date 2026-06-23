#include "editorcompletionworkflow.h"

#include "editorcompletionui.h"
#include "editormodestate.h"
#include "editorselection.h"
#include "editorruntime.h"
#include "inlinecommandmode.h"
#include "mycodeeditor.h"

#include <QModelIndex>
#include <QTextBlock>
#include <QTextCursor>

namespace {
bool hasLongerExactEditorActionPrefix(const InlineCommandDescriptor& descriptor)
{
    if (descriptor.prefix.endsWith(QLatin1Char(' ')))
        return false;

    for (const InlineCommandDescriptor& item :
         InlineCommandMode::descriptorsForIntent(InlineCommandIntent::EditorAction)) {
        if (item.label == descriptor.label)
            continue;
        if (item.prefix.endsWith(QLatin1Char(' ')))
            continue;
        if (item.prefix.size() > descriptor.prefix.size()
            && item.prefix.startsWith(descriptor.prefix)) {
            return true;
        }
    }
    return false;
}
}

void EditorCompletionWorkflow::bind(
    MyCodeEditor* nextEditor,
    EditorCompletionUi* nextCompletion,
    EditorModeState* nextModes,
    EditorSelection* nextSelections,
    const ContextProvider& nextContextProvider,
    const ModuleNameProvider& nextModuleNameProvider,
    const SemanticServiceProvider& nextServiceProvider)
{
    editor = nextEditor;
    completion = nextCompletion;
    modes = nextModes;
    selections = nextSelections;
    contextProvider = nextContextProvider;
    moduleNameProvider = nextModuleNameProvider;
    serviceProvider = nextServiceProvider;
}

EditorSemanticContextService* EditorCompletionWorkflow::semanticService() const
{
    return serviceProvider();
}

EditorSemanticContext EditorCompletionWorkflow::semanticContextForCursor(
    const QTextCursor& cursor,
    bool includeDocumentText) const
{
    return contextProvider(cursor.position(), includeDocumentText);
}

void EditorCompletionWorkflow::hideAutoComplete()
{
    completion->hidePopup();

    if (modes->commandModeActive)
        selections->clearCommand(editor);
}

void EditorCompletionWorkflow::showAutoComplete()
{
    completion->showForCursor(
        editor->cursorRect(editor->textCursor()),
        modes->commandModeActive);
}

void EditorCompletionWorkflow::applyAlternateModeCompletionDisplayState(
    const EditorAlternateModeCompletionDisplayState& displayState)
{
    if (!displayState.updateCompletions)
        return;

    modes->setAlternateBuffer(displayState.normalizedInput);
    completion->updateAlternateModeCompletions(displayState);

    if (displayState.showPopup)
        showAutoComplete();
}

void EditorCompletionWorkflow::processAlternateModeInput(const QString& input)
{
    if (!modes->alternateModeActive)
        return;

    const EditorAlternateModeCompletionDisplayState completionState =
        semanticService()->alternateModeCompletionDisplayState(input);
    applyAlternateModeCompletionDisplayState(completionState);
}

void EditorCompletionWorkflow::executeAlternateModeCommand(
    const QString& command)
{
    if (!command.trimmed().isEmpty())
        emit editor->alternateCommandRequested(command);
    modes->clearAlternateBuffer();
    hideAutoComplete();
}

void EditorCompletionWorkflow::executeEditorActionCommand(const QString& command)
{
    clearCommandInputAtCursor();
    hideAutoComplete();
    editor->state->executeEditorActionCommand(editor, command);
}

void EditorCompletionWorkflow::updateCompletionTriggerForTextChange(
    const QTextCursor& cursor)
{
    EditorSemanticContext context = semanticContextForCursor(cursor, false);
    context.moduleName = moduleNameProvider(cursor.position() - 1);
    const EditorCompletionTextChangeState completionState =
        semanticService()->completionTextChangeState(context);
    modes->setCommandModeActive(completionState.commandModeActive);

    if (completionState.startCompletionTimer) {
        completion->startTimer();
    } else if (completionState.hidePopup) {
        hideAutoComplete();
    }
}

void EditorCompletionWorkflow::handleTextChanged()
{
    completion->stopTimer();
    updateCompletionTriggerForTextChange(editor->textCursor());
}

void EditorCompletionWorkflow::applyCompletionActivationState(
    const CompletionActivationState& activationState)
{
    if (activationState.action == CompletionActivationAction::None)
        return;

    QTextCursor cursor = editor->textCursor();

    if (activationState.action
        == CompletionActivationAction::ExecuteAlternateCommand) {
        executeAlternateModeCommand(activationState.text);
        return;
    }
    if (activationState.action
        == CompletionActivationAction::ExecuteEditorAction) {
        executeEditorActionCommand(activationState.text);
        return;
    }

    if (activationState.action == CompletionActivationAction::ReplaceLine) {
        cursor.movePosition(QTextCursor::StartOfLine, QTextCursor::MoveAnchor);
        cursor.movePosition(QTextCursor::EndOfLine, QTextCursor::KeepAnchor);
        cursor.insertText(activationState.text);

        if (activationState.clearCommandMode) {
            modes->clearCommandMode();
            selections->clearCommand(editor);
        }
    } else if (activationState.action
               == CompletionActivationAction::ReplaceCommandInput) {
        replaceCommandInputAtCursor(activationState.text,
                                    activationState.selectionStart,
                                    activationState.selectionLength);
        if (activationState.clearCommandMode) {
            modes->clearCommandMode();
            selections->clearCommand(editor);
        }
    } else if (activationState.action
               == CompletionActivationAction::ReplaceWord) {
        completion->replaceWordAtCursor(editor, activationState.text);
    }

    if (activationState.hidePopup)
        hideAutoComplete();
}

void EditorCompletionWorkflow::replaceCommandInputAtCursor(
    const QString& text,
    int selectionStart,
    int selectionLength)
{
    QTextCursor cursor = editor->textCursor();
    const EditorSemanticContext context = semanticContextForCursor(
        cursor,
        false);
    const CommandModeInputState inputState =
        semanticService()->commandModeInputState(context);
    if (!inputState.matched)
        return;

    const int commandStartPosition =
        cursor.block().position() + inputState.prefixPosition;
    cursor.setPosition(commandStartPosition);
    cursor.setPosition(editor->textCursor().position(), QTextCursor::KeepAnchor);
    cursor.insertText(text);

    if (selectionStart >= 0 && selectionLength >= 0
        && selectionStart + selectionLength <= text.size()) {
        QTextCursor selectionCursor = editor->textCursor();
        selectionCursor.setPosition(commandStartPosition + selectionStart);
        if (selectionLength > 0) {
            selectionCursor.setPosition(commandStartPosition + selectionStart
                                            + selectionLength,
                                        QTextCursor::KeepAnchor);
        }
        editor->setTextCursor(selectionCursor);
    }
}

void EditorCompletionWorkflow::clearCommandInputAtCursor()
{
    replaceCommandInputAtCursor(QString());
    modes->clearCommandMode();
    selections->clearCommand(editor);
}

void EditorCompletionWorkflow::handleCompletionActivated(
    const QModelIndex& index)
{
    const EditorCompletionActivationContext activationContext =
        completion->activationContextForIndex(index, *modes);
    const CompletionActivationState activationState =
        semanticService()->completionActivationState(activationContext);
    applyCompletionActivationState(activationState);
}

bool EditorCompletionWorkflow::refreshCommandModeCompletion(
    const EditorSemanticContext& context)
{
    const EditorCommandModeCompletionRefreshState commandState =
        semanticService()->commandModeCompletionRefreshState(
            context,
            modes->commandModeExitedByDoubleSpace);
    if (commandState.matched) {
        modes->setCommandModeActive(commandState.commandModeActive);
        if (commandState.suppressAfterExit)
            return true;

        if (commandState.completion.intent == InlineCommandIntent::EditorAction
            && !commandState.completion.helpRequested
            && !commandState.completion.descriptor.prefix.endsWith(QLatin1Char(' '))
            && !hasLongerExactEditorActionPrefix(commandState.completion.descriptor)) {
            executeEditorActionCommand(commandState.completion.descriptor.label);
            return true;
        }
        if (commandState.completion.intent == InlineCommandIntent::EditorAction
            && !commandState.completion.helpRequested
            && commandState.completion.descriptor.prefix.endsWith(QLatin1Char(' '))
            && commandState.completion.input.isEmpty()) {
            executeEditorActionCommand(commandState.completion.descriptor.label);
            return true;
        }

        if (commandState.exitRequested) {
            if (commandState.clearCommandHighlight)
                selections->clearCommand(editor);
            if (commandState.markExitedByDoubleSpace)
                modes->markCommandModeExitedByDoubleSpace();
            if (completion->popupVisible())
                completion->hidePopup();
            return true;
        }

        if (commandState.highlightCommand) {
            selections->highlightCommand(
                editor,
                commandState.completion.prefixPosition);
        }

        if (commandState.hidePopup) {
            if (completion->popupVisible())
                completion->hidePopup();
            return true;
        }

        if (commandState.showCompletions) {
            completion->updateCommandModeCompletions(commandState);
            showAutoComplete();
        }
        return true;
    }

    if (commandState.resetExitedByDoubleSpace)
        modes->resetCommandModeExit();

    selections->clearCommand(editor);
    modes->clearCommandMode();

    return false;
}

void EditorCompletionWorkflow::refreshSymbolCompletion(
    EditorSemanticContext context,
    const QTextBlock& currentBlock)
{
    context.wordPrefix = completion->wordUnderCursor(editor);
    const EditorCompletionState completionState =
        semanticService()->editorCompletionState(context);
    if (completionState.available) {
        completion->updateSymbolCompletions(completionState);
        completion->setReplacementStart(
            currentBlock.position(),
            completionState.replacementStartColumn);
        showAutoComplete();
    }
}

void EditorCompletionWorkflow::handleAutoCompleteTimer()
{
    const QTextCursor cursor = editor->textCursor();
    const QTextBlock currentBlock = cursor.block();

    modes->noteCompletionTimerLine(currentBlock.blockNumber());

    EditorSemanticContext context = semanticContextForCursor(cursor, true);

    if (refreshCommandModeCompletion(context))
        return;

    if (modes->alternateModeActive) {
        processAlternateModeInput(context.lineUpToCursor);
        return;
    }

    refreshSymbolCompletion(context, currentBlock);
}
