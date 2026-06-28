#include "editorcompletionworkflow.h"

#include "editorcompletionui.h"
#include "editormodestate.h"
#include "editorselection.h"
#include "editorruntime.h"
#include "inlinecommandmode.h"
#include "mycodeeditor.h"

#include <QModelIndex>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>
#include <QTextBlock>
#include <QTextCursor>
#include <algorithm>
#include <utility>

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

QString sanitizedIncludeHeaderStem(QString text)
{
    text = text.trimmed();
    if (text.startsWith(QLatin1Char('"')) && text.endsWith(QLatin1Char('"'))
        && text.size() >= 2) {
        text = text.mid(1, text.size() - 2);
    }
    text = QFileInfo(QDir::fromNativeSeparators(text)).completeBaseName();

    QString sanitized;
    sanitized.reserve(text.size());
    for (QChar ch : text) {
        if (ch.isLetterOrNumber() || ch == QLatin1Char('_')
            || ch == QLatin1Char('-')) {
            sanitized.append(ch);
        } else if (ch == QLatin1Char('.') || ch.isSpace()) {
            sanitized.append(QLatin1Char('_'));
        }
    }

    while (sanitized.contains(QStringLiteral("__")))
        sanitized.replace(QStringLiteral("__"), QStringLiteral("_"));
    sanitized = sanitized.trimmed();
    while (sanitized.startsWith(QLatin1Char('_')))
        sanitized.remove(0, 1);
    while (sanitized.endsWith(QLatin1Char('_')))
        sanitized.chop(1);
    return sanitized;
}

QString includeHeaderGuardName(const QString& stem, const QString& extension)
{
    QString guard = stem + QLatin1Char('_') + extension;
    for (QChar& ch : guard) {
        if (ch.isLetterOrNumber())
            ch = ch.toUpper();
        else
            ch = QLatin1Char('_');
    }
    if (!guard.endsWith(QStringLiteral("_")))
        guard.append(QLatin1Char('_'));
    return guard;
}

struct IncludeTemplateDefinition {
    QString name;
    QString description;
    QString body;
};

QList<IncludeTemplateDefinition> includeHeaderTemplates(
    const QString& stem,
    const QString& extension)
{
    const QString cursorToken = QStringLiteral("__ZEROSLACK_CURSOR__");
    const QString fileName = stem + QLatin1Char('.') + extension;
    const QString guard = includeHeaderGuardName(stem, extension);

    QList<IncludeTemplateDefinition> templates;
    templates.append({
        QStringLiteral("empty"),
        QStringLiteral("blank header"),
        QStringLiteral("// %1\n\n%2\n").arg(fileName, cursorToken)
    });
    templates.append({
        QStringLiteral("guard"),
        QStringLiteral("include guard"),
        QStringLiteral("`ifndef %1\n`define %1\n\n%2\n\n`endif // %1\n")
            .arg(guard, cursorToken)
    });
    if (extension == QStringLiteral("svh")) {
        const QString packageName = stem + QStringLiteral("_pkg");
        templates.append({
            QStringLiteral("package"),
            QStringLiteral("package skeleton"),
            QStringLiteral("package %1;\n\n  %2\n\nendpackage : %1\n")
                .arg(packageName, cursorToken)
        });
    } else {
        templates.append({
            QStringLiteral("defines"),
            QStringLiteral("macro definitions"),
            QStringLiteral("`ifndef %1\n`define %1\n\n`define %2_VALUE %3\n\n`endif // %1\n")
                .arg(guard, stem.toUpper(), cursorToken)
        });
    }
    return templates;
}

QStringList splitIncludeNewCommand(const QString& prefix)
{
    return prefix.trimmed().split(QRegularExpression(QStringLiteral("\\s+")),
                                  Qt::SkipEmptyParts);
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
    includeCompletionActive = false;
    includeCompletionMode = IncludeCompletionMode::None;
    completion->hidePopup();

    if (modes->commandModeActive)
        selections->clearCommand(editor);
}

void EditorCompletionWorkflow::showAutoComplete(bool selectFirstCompletion)
{
    completion->showForCursor(
        editor->cursorRect(editor->textCursor()),
        selectFirstCompletion || modes->commandModeActive);
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
        const int insertionStart =
            replaceCommandInputAtCursor(activationState.text,
                                        activationState.selectionStart,
                                        activationState.selectionLength);
        if (activationState.clearCommandMode) {
            modes->clearCommandMode();
            selections->clearCommand(editor);
        }
        if (insertionStart >= 0 && !activationState.templateSlots.isEmpty()) {
            editor->startTemplateSlotMode(insertionStart,
                                          activationState.text.size(),
                                          activationState.templateSlots);
        }
    } else if (activationState.action
               == CompletionActivationAction::ReplaceWord) {
        completion->replaceWordAtCursor(editor, activationState.text);
    }

    if (activationState.hidePopup)
        hideAutoComplete();
}

int EditorCompletionWorkflow::replaceCommandInputAtCursor(
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
        return -1;

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
    return commandStartPosition;
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
    if (includeCompletionActive) {
        const EditorCompletionActivationContext activationContext =
            completion->activationContextForIndex(index, *modes);
        if (activationContext.selectable) {
            if (includeCompletionMode == IncludeCompletionMode::NewHeader) {
                applyIncludeNewHeaderChoice(activationContext.itemText);
            } else {
                applyIncludeCompletion(activationContext.itemText);
            }
        }
        return;
    }

    const EditorCompletionActivationContext activationContext =
        completion->activationContextForIndex(index, *modes);
    const CompletionActivationState activationState =
        semanticService()->completionActivationState(activationContext);
    applyCompletionActivationState(activationState);
}

void EditorCompletionWorkflow::setIncludeFileProvider(
    IncludeFileProvider provider)
{
    includeFileProvider = std::move(provider);
}

void EditorCompletionWorkflow::setIncludeNewHeaderCreator(
    IncludeNewHeaderCreator creator)
{
    includeNewHeaderCreator = std::move(creator);
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

        if (commandState.completion.intent == InlineCommandIntent::HeaderInclude) {
            if (commandState.showCompletions)
                showIncludeCommandCompletions(commandState.completion);
            return true;
        }

        includeCompletionActive = false;
        includeCompletionMode = IncludeCompletionMode::None;
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
    includeCompletionActive = false;
    includeCompletionMode = IncludeCompletionMode::None;

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

    refreshSymbolCompletion(context, currentBlock);
}

EditorCompletionWorkflow::IncludeCompletionContext
EditorCompletionWorkflow::includeCompletionContextAtCursor() const
{
    IncludeCompletionContext context;
    if (!editor || !includeFileProvider)
        return context;

    const QTextCursor cursor = editor->textCursor();
    const EditorSemanticContext semanticContext =
        semanticContextForCursor(cursor, false);
    const CommandModeInputState inputState =
        semanticService()->commandModeInputState(semanticContext);
    if (!inputState.matched
        || inputState.intent != InlineCommandIntent::HeaderInclude
        || inputState.prefixPosition < 0) {
        return context;
    }

    context.active = true;
    context.prefix = inputState.input.trimmed();
    context.replacementStartPosition =
        cursor.block().position() + inputState.prefixPosition;
    context.replacementEndPosition = cursor.position();
    return context;
}

bool EditorCompletionWorkflow::showIncludeCommandCompletions(
    const CommandModeCompletionState& state)
{
    if (!editor || !includeFileProvider)
        return false;

    IncludeCompletionContext context;
    context.active = true;
    context.prefix = state.input.trimmed();
    context.replacementStartPosition =
        editor->textCursor().block().position() + state.prefixPosition;
    context.replacementEndPosition = editor->textCursor().position();
    if (context.replacementStartPosition < 0
        || context.replacementEndPosition < context.replacementStartPosition) {
        return false;
    }

    showIncludeFileCompletions(context);
    return true;
}

void EditorCompletionWorkflow::showIncludeFileCompletions(
    const IncludeCompletionContext& context)
{
    if (!context.active || !includeFileProvider)
        return;

    if (showIncludeNewHeaderCompletions(context))
        return;

    includeCompletionActive = true;
    includeCompletionMode = IncludeCompletionMode::File;
    const QStringList candidates =
        includeFileProvider(editor ? editor->documentFileName() : QString());
    completion->updateIncludeFileCompletions(candidates, context.prefix);
    showAutoComplete(true);
}

bool EditorCompletionWorkflow::showIncludeNewHeaderCompletions(
    const IncludeCompletionContext& context)
{
    const QStringList parts = splitIncludeNewCommand(context.prefix);
    if (parts.isEmpty() || parts.first() != QStringLiteral("-n"))
        return false;

    includeCompletionActive = true;
    includeCompletionMode = IncludeCompletionMode::NewHeader;

    QList<IncludeNewHeaderChoice> choices;
    const QString typedName = parts.size() >= 2
        ? parts.at(1).trimmed()
        : QString();
    const QString stem = !typedName.isEmpty()
        ? sanitizedIncludeHeaderStem(typedName)
        : QString();

    if (stem.isEmpty()) {
        completion->updateIncludeNewHeaderCompletions(
            choices,
            QStringLiteral("type -n name"));
        showAutoComplete(true);
        return true;
    }

    QString extension = QFileInfo(QDir::fromNativeSeparators(typedName))
        .suffix()
        .toLower();
    if (extension.isEmpty())
        extension = QStringLiteral("svh");
    if (extension != QStringLiteral("vh")
        && extension != QStringLiteral("svh")) {
        completion->updateIncludeNewHeaderCompletions(
            choices,
            QStringLiteral("use .vh or .svh"));
        showAutoComplete(true);
        return true;
    }

    const QString fileName = stem + QLatin1Char('.') + extension;
    choices.append({
        fileName,
        QStringLiteral("create header"),
        QStringLiteral("Create %1 and insert include").arg(fileName)
    });
    completion->updateIncludeNewHeaderCompletions(
        choices,
        QStringLiteral("create %1").arg(fileName));
    showAutoComplete(true);
    return true;
}

void EditorCompletionWorkflow::applyIncludeCompletion(
    const QString& includePath)
{
    if (!editor || includePath.isEmpty())
        return;

    const IncludeCompletionContext context = includeCompletionContextAtCursor();
    if (!context.active)
        return;

    QTextCursor cursor = editor->textCursor();
    cursor.setPosition(context.replacementStartPosition);
    cursor.setPosition(context.replacementEndPosition,
                       QTextCursor::KeepAnchor);
    cursor.insertText(QStringLiteral("`include \"%1\"").arg(includePath));
    editor->setTextCursor(cursor);

    includeCompletionActive = false;
    hideAutoComplete();
}

void EditorCompletionWorkflow::applyIncludeNewHeaderChoice(const QString& choice)
{
    if (!editor || choice.isEmpty())
        return;

    const IncludeCompletionContext context = includeCompletionContextAtCursor();
    if (!context.active)
        return;

    if (includeCompletionMode != IncludeCompletionMode::NewHeader
        || !includeNewHeaderCreator) {
        return;
    }

    const QStringList parts = splitIncludeNewCommand(context.prefix);
    if (parts.size() < 2)
        return;
    const QString typedName = parts.at(1).trimmed();
    const QString stem = sanitizedIncludeHeaderStem(typedName);
    QString extension = QFileInfo(QDir::fromNativeSeparators(typedName))
        .suffix()
        .toLower();
    if (extension.isEmpty())
        extension = QStringLiteral("svh");
    if (stem.isEmpty()
        || (extension != QStringLiteral("vh")
            && extension != QStringLiteral("svh"))) {
        return;
    }

    const QList<IncludeTemplateDefinition> templates =
        includeHeaderTemplates(stem, extension);
    if (templates.isEmpty())
        return;
    const IncludeTemplateDefinition defaultTemplate = templates.constFirst();

    IncludeNewHeaderRequest request;
    request.fileStem = stem;
    request.extension = extension;
    request.templateName = defaultTemplate.name;
    request.templateBody = defaultTemplate.body;
    request.cursorToken = QStringLiteral("__ZEROSLACK_CURSOR__");
    request.currentFileName = editor->documentFileName();

    const IncludeNewHeaderResult result = includeNewHeaderCreator(request);
    if (!result.success) {
        if (!result.errorMessage.isEmpty())
            emit editor->editorStatusMessageRequested(result.errorMessage);
        return;
    }

    applyIncludeCompletion(result.includePath);
}
