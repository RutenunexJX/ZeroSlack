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
#include <QKeyEvent>
#include <QRegularExpression>
#include <QStringList>
#include <QTextBlock>
#include <QTextCursor>
#include <algorithm>
#include <utility>

namespace {
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

void EditorCompletionWorkflow::hideCompletionPopup()
{
    includeCompletionActive = false;
    includeCompletionMode = IncludeCompletionMode::None;
    completion->hidePopup();

    if (modes->commandModeActive)
        selections->clearCommand(editor);
}

void EditorCompletionWorkflow::clearInlineAbbreviationSession()
{
    inlineSession = {};
}

void EditorCompletionWorkflow::cancelInlineAbbreviationSession()
{
    if (!inlineSession.active)
        return;

    clearInlineAbbreviationSession();
    includeCompletionActive = false;
    includeCompletionMode = IncludeCompletionMode::None;
    completion->hidePopup();
    modes->clearCommandMode();
    selections->clearCommand(editor);
}

bool EditorCompletionWorkflow::inlineAbbreviationSessionValid() const
{
    if (!editor || !inlineSession.active)
        return false;
    const QTextCursor currentCursor = editor->textCursor();
    if (currentCursor.hasSelection()
        || currentCursor.position() != inlineSession.replacementEndPosition) {
        return false;
    }
    if (inlineSession.replacementStartPosition < 0
        || inlineSession.replacementEndPosition
               < inlineSession.replacementStartPosition) {
        return false;
    }

    if (inlineSession.replacementEndPosition
        > editor->cachedDocumentText().size()) {
        return false;
    }
    return editor->cachedDocumentSlice(
               inlineSession.replacementStartPosition,
               inlineSession.replacementEndPosition
                   - inlineSession.replacementStartPosition)
        == inlineSession.abbreviationText;
}

bool EditorCompletionWorkflow::refreshInlineCandidateFilter()
{
    if (!inlineAbbreviationSessionValid()
        || !inlineSession.candidateFiltering) {
        return false;
    }

    CommandModeCompletionQuery query = inlineSession.anchorQuery;
    query.hasExplicitMatch = true;
    query.explicitMatch.matched = true;
    query.explicitMatch.input = inlineSession.filterText;
    const CommandModeCompletionState state =
        CompletionService::getInstance()->commandModeCompletionState(query);
    if (!state.matched) {
        cancelInlineAbbreviationSession();
        return false;
    }

    inlineSession.completion = state;
    completion->updateCommandModeCompletions(state, false);
    if (state.prefixPosition >= 0)
        selections->highlightCommand(editor, state.prefixPosition);
    showCompletionPopup(true);

    if (inlineCandidateCount(state) == 0) {
        emit editor->editorStatusMessageRequested(
            QStringLiteral("No %1 candidates for \"%2\"")
                .arg(state.descriptor.description,
                     state.completionPrefix));
    }
    return true;
}

void EditorCompletionWorkflow::handleCursorPositionChanged()
{
    if (applyingInlineReplacement || !inlineSession.active)
        return;

    const QTextCursor cursor = editor->textCursor();
    if (cursor.hasSelection()
        || cursor.position() != inlineSession.replacementEndPosition) {
        cancelInlineAbbreviationSession();
    }
}

void EditorCompletionWorkflow::showCompletionPopup(bool selectFirstCompletion)
{
    completion->showForCursor(
        editor->cursorRect(editor->textCursor()),
        selectFirstCompletion || modes->commandModeActive);
}

void EditorCompletionWorkflow::executeEditorActionCommand(const QString& command)
{
    clearCommandInputAtCursor();
    hideCompletionPopup();
    editor->state->executeEditorActionCommand(editor, command);
}

int EditorCompletionWorkflow::inlineCandidateCount(
    const CommandModeCompletionState& state) const
{
    if (state.helpRequested)
        return state.helpCommands.size() + state.helpDescriptors.size();
    if (state.intent == InlineCommandIntent::CodeTemplate
        || state.intent == InlineCommandIntent::EditorAction) {
        return state.templateItems.size();
    }
    if (state.intent == InlineCommandIntent::SemanticCompletion
        || state.intent == InlineCommandIntent::PackageImport) {
        return state.symbolRecords.size();
    }
    return 0;
}

bool EditorCompletionWorkflow::applySingleInlineAbbreviationCandidate(
    const CommandModeCompletionState& state)
{
    CompletionActivationQuery activationQuery;
    activationQuery.selectable = true;

    if (state.intent == InlineCommandIntent::CodeTemplate
        || state.intent == InlineCommandIntent::EditorAction) {
        if (state.templateItems.size() != 1)
            return false;
        const CodeTemplateItem item = state.templateItems.constFirst();
        activationQuery.itemText = item.label;
        activationQuery.defaultValue =
            item.insertText.isEmpty() ? item.defaultValue : item.insertText;
        activationQuery.selectionStart = item.selectionStart;
        activationQuery.selectionLength = item.selectionLength;
        activationQuery.templateSlots = item.templateSlots;
    } else if (state.intent == InlineCommandIntent::SemanticCompletion
               || state.intent == InlineCommandIntent::PackageImport) {
        if (state.symbolRecords.size() != 1)
            return false;
        const CommandSymbolCompletionItem item =
            CompletionService::getInstance()->commandSymbolCompletionItem(
                state.symbolRecords.constFirst(),
                state.commandKind,
                state.completionPrefix);
        activationQuery.itemText = item.text;
        activationQuery.defaultValue = item.defaultValue;
        activationQuery.selectionStart = item.selectionStart;
        activationQuery.selectionLength = item.selectionLength;
        activationQuery.templateSlots = item.templateSlots;
    } else {
        return false;
    }

    const CompletionActivationState activationState =
        semanticService()->completionActivationState(activationQuery);
    applyCompletionActivationState(activationState);
    return true;
}

bool EditorCompletionWorkflow::showInlineAbbreviationCompletions(
    const CommandModeCompletionState& state,
    int replacementStartPosition,
    int replacementEndPosition,
    int anchorPosition,
    int commandEndPosition,
    int filterStartPosition,
    const QString& abbreviationText,
    const CommandModeCompletionQuery& anchorQuery)
{
    inlineSession.active = true;
    inlineSession.replacementStartPosition = replacementStartPosition;
    inlineSession.replacementEndPosition = replacementEndPosition;
    inlineSession.anchorPosition = anchorPosition;
    inlineSession.commandEndPosition = commandEndPosition;
    inlineSession.filterStartPosition = filterStartPosition;
    inlineSession.abbreviationText = abbreviationText;
    inlineSession.filterText = abbreviationText.mid(
        filterStartPosition - replacementStartPosition);
    inlineSession.anchorQuery = anchorQuery;
    inlineSession.completion = state;

    modes->setCommandModeActive(true);
    selections->highlightCommand(
        editor,
        state.prefixPosition);

    if (state.intent == InlineCommandIntent::HeaderInclude) {
        if (!showIncludeCommandCompletions(state)) {
            emit editor->editorStatusMessageRequested(
                QStringLiteral("No include completion provider"));
            clearInlineAbbreviationSession();
            modes->clearCommandMode();
            selections->clearCommand(editor);
        }
        return true;
    }

    const int candidateCount = inlineCandidateCount(state);
    const bool supportsCandidateFiltering =
        !state.helpRequested
        && state.intent != InlineCommandIntent::HeaderInclude;
    if (candidateCount == 0 && !supportsCandidateFiltering) {
        emit editor->editorStatusMessageRequested(
            QStringLiteral("No %1 candidates for \"%2\"")
                .arg(state.descriptor.description,
                     state.completionPrefix));
        clearInlineAbbreviationSession();
        modes->clearCommandMode();
        selections->clearCommand(editor);
        return true;
    }

    if (candidateCount == 1
        && applySingleInlineAbbreviationCandidate(state)) {
        return true;
    }

    inlineSession.candidateFiltering = supportsCandidateFiltering;

    completion->updateCommandModeCompletions(
        state,
        !inlineSession.candidateFiltering);
    showCompletionPopup(true);
    if (candidateCount == 0) {
        emit editor->editorStatusMessageRequested(
            QStringLiteral("No %1 candidates for \"%2\"")
                .arg(state.descriptor.description,
                     state.completionPrefix));
    }
    return true;
}

bool EditorCompletionWorkflow::handleInlineAbbreviationTab(QKeyEvent* event)
{
    if (!editor || !event || event->key() != Qt::Key_Tab
        || event->modifiers() != Qt::NoModifier
        || editor->textCursor().hasSelection()) {
        return false;
    }

    QTextCursor cursor = editor->textCursor();
    const QTextBlock block = cursor.block();
    if (!block.isValid())
        return false;

    const int column = cursor.position() - block.position();
    const QString textBeforeCursor = block.text().left(column);
    const CommandModeMatch match =
        CompletionService::getInstance()->matchCommandMode(textBeforeCursor);
    if (!match.matched)
        return false;

    const int replacementStartPosition = block.position() + match.prefixPosition;
    const int replacementEndPosition = cursor.position();
    const QString abbreviationText = editor->cachedDocumentSlice(
        replacementStartPosition,
        replacementEndPosition - replacementStartPosition);
    const QString commandToken = match.descriptor.label.isEmpty()
        ? match.descriptor.prefix.trimmed()
        : match.descriptor.label;
    const int commandEndPosition =
        replacementStartPosition + commandToken.size();
    const int filterStartPosition =
        commandEndPosition < replacementEndPosition
            && editor->document()->characterAt(commandEndPosition)
                   == QLatin1Char(' ')
        ? commandEndPosition + 1
        : commandEndPosition;

    EditorSemanticContext anchorContext =
        contextProvider(replacementStartPosition, true);
    anchorContext.moduleName = moduleNameProvider(replacementStartPosition);
    if (InlineCommandMode::isPositionInCommentOrString(
            anchorContext.documentText,
            replacementStartPosition)) {
        return false;
    }

    CommandModeCompletionQuery query;
    query.lineUpToCursor = textBeforeCursor;
    query.fileName = anchorContext.fileName;
    query.moduleName = anchorContext.moduleName;
    query.documentText = anchorContext.documentText;
    query.cursorLine = anchorContext.cursorLine;
    query.cursorPosition = replacementStartPosition;
    query.hasExplicitMatch = true;
    query.explicitMatch.matched = true;
    query.explicitMatch.helpRequested = match.helpRequested;
    query.explicitMatch.intent = match.intent;
    query.explicitMatch.prefixPosition = match.prefixPosition;
    query.explicitMatch.endPosition = textBeforeCursor.size();
    query.explicitMatch.commandToken = match.descriptor.label;
    query.explicitMatch.input = match.input;
    query.explicitMatch.descriptor = match.descriptor;

    const CommandModeCompletionState state =
        CompletionService::getInstance()->commandModeCompletionState(query);
    if (!state.matched)
        return false;

    event->accept();
    return showInlineAbbreviationCompletions(state,
                                             replacementStartPosition,
                                             replacementEndPosition,
                                             replacementStartPosition,
                                             commandEndPosition,
                                             filterStartPosition,
                                             abbreviationText,
                                             query);
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
    }

    if (activationState.hidePopup)
        hideCompletionPopup();
}

int EditorCompletionWorkflow::replaceCommandInputAtCursor(
    const QString& text,
    int selectionStart,
    int selectionLength)
{
    QTextCursor cursor = editor->textCursor();
    if (inlineSession.active && !inlineAbbreviationSessionValid()) {
        cancelInlineAbbreviationSession();
        return -1;
    }
    if (inlineAbbreviationSessionValid()) {
        const int commandStartPosition =
            inlineSession.replacementStartPosition;
        applyingInlineReplacement = true;
        cursor.beginEditBlock();
        cursor.setPosition(inlineSession.replacementStartPosition);
        cursor.setPosition(inlineSession.replacementEndPosition,
                           QTextCursor::KeepAnchor);
        cursor.insertText(text);
        cursor.endEditBlock();
        applyingInlineReplacement = false;
        clearInlineAbbreviationSession();

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

    return -1;
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
            completion->activationContextForIndex(index);
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
        completion->activationContextForIndex(index);
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

EditorCompletionWorkflow::IncludeCompletionContext
EditorCompletionWorkflow::includeCompletionContextAtCursor() const
{
    IncludeCompletionContext context;
    if (!editor || !includeFileProvider)
        return context;

    if (inlineAbbreviationSessionValid()
        && inlineSession.completion.intent
               == InlineCommandIntent::HeaderInclude) {
        context.active = true;
        context.prefix = inlineSession.completion.input.trimmed();
        context.replacementStartPosition =
            inlineSession.replacementStartPosition;
        context.replacementEndPosition = inlineSession.replacementEndPosition;
        return context;
    }

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
    showCompletionPopup(true);
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
        showCompletionPopup(true);
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
        showCompletionPopup(true);
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
    showCompletionPopup(true);
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
    applyingInlineReplacement = true;
    cursor.setPosition(context.replacementStartPosition);
    cursor.setPosition(context.replacementEndPosition,
                       QTextCursor::KeepAnchor);
    cursor.insertText(QStringLiteral("`include \"%1\"").arg(includePath));
    applyingInlineReplacement = false;
    editor->setTextCursor(cursor);

    includeCompletionActive = false;
    clearInlineAbbreviationSession();
    hideCompletionPopup();
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
