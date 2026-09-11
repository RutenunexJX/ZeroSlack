#include "editorcompletionworkflow.h"

#include "editorcompletionui.h"
#include "editormodecontroller.h"
#include "editorselection.h"
#include "editorruntime.h"
#include "inlinecommandmode.h"
#include "mycodeeditor.h"
#include "packagetoolservice.h"
#include "tsdocument.h"

#include <QAbstractItemView>
#include <QModelIndex>
#include <QDir>
#include <QElapsedTimer>
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
    EditorModeController* nextModes,
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

    if (modes) {
        modes->setExitHandler(
            EditorModeId::InlineCandidates,
            [this](EditorModeExitReason) {
                resetInlineAbbreviationSession();
            });
        modes->setExitHandler(
            EditorModeId::CompletionCandidates,
            [this](EditorModeExitReason) {
                includeCompletionActive = false;
                includeCompletionMode = IncludeCompletionMode::None;
                if (completion)
                    completion->hidePopup();
            });
    }
}

EditorSemanticContextService* EditorCompletionWorkflow::semanticService() const
{
    return serviceProvider();
}

void EditorCompletionWorkflow::hideCompletionPopup()
{
    if (modes
        && modes->isActive(EditorModeId::InlineCandidates)) {
        modes->exit(EditorModeId::InlineCandidates,
                    EditorModeExitReason::Canceled);
        return;
    }
    if (modes
        && modes->isActive(EditorModeId::CompletionCandidates)) {
        modes->exit(EditorModeId::CompletionCandidates,
                    EditorModeExitReason::Canceled);
        return;
    }

    includeCompletionActive = false;
    includeCompletionMode = IncludeCompletionMode::None;
    completion->hidePopup();
    if (selections && editor)
        selections->clearCommand(editor);
}

void EditorCompletionWorkflow::clearInlineAbbreviationSession()
{
    if (modes
        && modes->isActive(EditorModeId::InlineCandidates)) {
        modes->exit(EditorModeId::InlineCandidates,
                    EditorModeExitReason::Completed);
        return;
    }
    resetInlineAbbreviationSession();
}

void EditorCompletionWorkflow::resetInlineAbbreviationSession()
{
    if (editor)
        editor->state->finishInlineFilterTextOverlay();
    inlineSession = {};
    includeCompletionActive = false;
    includeCompletionMode = IncludeCompletionMode::None;
    if (completion)
        completion->hidePopup();
    if (selections && editor)
        selections->clearCommand(editor);
}

void EditorCompletionWorkflow::cancelInlineAbbreviationSession()
{
    if (!inlineAbbreviationSessionActive())
        return;

    if (modes) {
        modes->exit(EditorModeId::InlineCandidates,
                    EditorModeExitReason::Canceled);
    } else {
        resetInlineAbbreviationSession();
    }
}

bool EditorCompletionWorkflow::inlineAbbreviationSessionActive() const
{
    return modes
        && modes->isActive(EditorModeId::InlineCandidates);
}

bool EditorCompletionWorkflow::inlineAbbreviationSessionValid() const
{
    if (!editor || !inlineAbbreviationSessionActive())
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
        > editor->state->cachedDocumentLength()) {
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
    ++editor->state->hotPathMetrics.inlineFilterRefreshes;
    ++editor->state->hotPathMetrics.inlineFilterServiceQueries;
    QElapsedTimer serviceTimer;
    serviceTimer.start();
    CommandModeCompletionState state =
        CompletionService::getInstance()->commandModeCompletionState(query);
    editor->state->hotPathMetrics.inlineFilterServiceQueryNanoseconds +=
        static_cast<std::uint64_t>(serviceTimer.nsecsElapsed());
    if (!state.matched) {
        cancelInlineAbbreviationSession();
        return false;
    }
    if (state.intent == InlineCommandIntent::PackageImport) {
        state.symbolRecords.erase(
            std::remove_if(
                state.symbolRecords.begin(),
                state.symbolRecords.end(),
                [this](const SemanticSymbolRecord& record) {
                    return !inlineSession.packageImportSiteValid
                        || record.name.isEmpty()
                        || record.name
                               == inlineSession
                                      .packageImportEnclosingPackageName
                        || inlineSession.packageImportWildcardImports
                               .contains(record.name);
                }),
            state.symbolRecords.end());
        state.symbolStableKeys.clear();
        state.symbolStableKeys.reserve(state.symbolRecords.size());
        for (const SemanticSymbolRecord& record : state.symbolRecords)
            state.symbolStableKeys.append(record.stableKey);
    }

    inlineSession.completion = state;
    if (modes) {
        modes->updatePresentation(
            EditorModeId::InlineCandidates,
            QStringLiteral("Inline candidates: %1")
                .arg(inlineCandidateCount(state)),
            QStringLiteral("%1; filter \"%2\"; "
                           "Tab/Enter accepts; Esc cancels")
                .arg(state.descriptor.description,
                     inlineSession.filterText));
    }
    ++editor->state->hotPathMetrics.inlineFilterModelUpdates;
    QElapsedTimer modelTimer;
    modelTimer.start();
    completion->updateCommandModeCompletions(state, false);
    if (completion->popupVisible()) {
        const QModelIndex selectable =
            completion->firstSelectableIndex();
        if (selectable.isValid())
            completion->popup()->setCurrentIndex(selectable);
    } else {
        showCompletionPopup(true);
    }
    editor->state->hotPathMetrics.inlineFilterModelUpdateNanoseconds +=
        static_cast<std::uint64_t>(modelTimer.nsecsElapsed());

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
    if (applyingInlineReplacement
        || !inlineAbbreviationSessionActive()) {
        return;
    }

    const QTextCursor cursor = editor->textCursor();
    if (cursor.hasSelection()
        || cursor.position() != inlineSession.replacementEndPosition) {
        cancelInlineAbbreviationSession();
    }
}

void EditorCompletionWorkflow::showCompletionPopup(bool selectFirstCompletion)
{
    const bool inlineActive = inlineAbbreviationSessionActive();
    if (editor && inlineActive)
        ++editor->state->hotPathMetrics.inlineFilterPopupCompletes;
    if (modes && !inlineActive) {
        modes->enter(EditorModeId::CompletionCandidates,
                     EditorModeEntryReason::UserAction);
    }
    completion->showForCursor(
        editor->cursorRect(editor->textCursor()),
        selectFirstCompletion || inlineActive);
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
    if (state.intent == InlineCommandIntent::PackageImport) {
        if (state.symbolRecords.size() != 1)
            return false;
        return applyPackageImport(state.symbolRecords.constFirst().name);
    }

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
    } else if (state.intent == InlineCommandIntent::SemanticCompletion) {
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
    if (state.intent == InlineCommandIntent::PackageImport) {
        const PackageImportSite site =
            PackageToolService::analyzePackageImportSite(
                inlineSession.anchorQuery.documentText,
                replacementStartPosition,
                replacementEndPosition);
        inlineSession.packageImportSiteValid = site.valid;
        inlineSession.packageImportEnclosingPackageName =
            site.enclosingPackageName;
        inlineSession.packageImportWildcardImports =
            site.wildcardImportedPackages;
    }
    inlineSession.anchorQuery.documentText.clear();
    editor->state->hotPathMetrics
        .inlineFilterRetainedDocumentCharactersPeak =
        qMax(editor->state->hotPathMetrics
                 .inlineFilterRetainedDocumentCharactersPeak,
             static_cast<std::uint64_t>(
                 inlineSession.anchorQuery.documentText.size()));

    if (modes) {
        modes->enter(EditorModeId::InlineCandidates,
                     EditorModeEntryReason::InlineCommand);
    }
    ++editor->state->hotPathMetrics.inlineFilterHighlightUpdates;
    selections->highlightCommand(
        editor,
        state.prefixPosition);

    if (state.intent == InlineCommandIntent::HeaderInclude) {
        if (!showIncludeCommandCompletions(state)) {
            emit editor->editorStatusMessageRequested(
                QStringLiteral("No include completion provider"));
            clearInlineAbbreviationSession();
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
        return true;
    }

    if (candidateCount == 1
        && applySingleInlineAbbreviationCandidate(state)) {
        return true;
    }

    inlineSession.candidateFiltering = supportsCandidateFiltering;
    if (inlineSession.candidateFiltering) {
        editor->state->beginInlineFilterTextOverlay(
            replacementStartPosition,
            replacementEndPosition);
    }

    ++editor->state->hotPathMetrics.inlineFilterModelUpdates;
    completion->updateCommandModeCompletions(
        state,
        !inlineSession.candidateFiltering);
    if (modes) {
        modes->updatePresentation(
            EditorModeId::InlineCandidates,
            QStringLiteral("Inline candidates: %1")
                .arg(candidateCount),
            QStringLiteral("%1; type or Backspace to filter; "
                           "Tab/Enter accepts; Esc cancels")
                .arg(state.descriptor.description));
    }
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
    CommandModeMatch match =
        CompletionService::getInstance()->matchCommandMode(textBeforeCursor);
    if (!match.matched) {
        const InlineCommandMatch structuralMatch =
            InlineCommandMode::matchAbbreviationBeforeCursor(
                textBeforeCursor);
        if (structuralMatch.matched
            && (structuralMatch.intent
                    == InlineCommandIntent::HeaderInclude
                || structuralMatch.intent
                    == InlineCommandIntent::PackageImport)) {
            match.matched = true;
            match.helpRequested = structuralMatch.helpRequested;
            match.intent = structuralMatch.intent;
            match.prefixPosition =
                structuralMatch.prefixPosition;
            match.input = structuralMatch.input;
            match.descriptor = structuralMatch.descriptor;
            match.command =
                InlineCommandMode::toCommandModeCommand(
                    structuralMatch.descriptor);
        }
    }
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
    bool commandInCommentOrString = false;
    if (match.intent == InlineCommandIntent::HeaderInclude
        || match.intent == InlineCommandIntent::PackageImport) {
        const TSDocument* syntaxDocument =
            editor->state->syntax.tsDocument();
        if (syntaxDocument) {
            commandInCommentOrString =
                syntaxDocument->isCommentAt(replacementStartPosition)
                || syntaxDocument->isStringAt(replacementStartPosition);
        } else {
            TSDocument syntaxSnapshot;
            syntaxSnapshot.setText(anchorContext.documentText);
            commandInCommentOrString =
                syntaxSnapshot.isCommentAt(replacementStartPosition)
                || syntaxSnapshot.isStringAt(replacementStartPosition);
        }
    } else {
        commandInCommentOrString =
            InlineCommandMode::isPositionInCommentOrString(
                anchorContext.documentText,
                replacementStartPosition);
    }
    if (commandInCommentOrString) {
        return false;
    }

    CommandModeCompletionQuery query;
    query.lineUpToCursor = textBeforeCursor;
    query.fileName = anchorContext.fileName;
    query.moduleName = anchorContext.moduleName;
    query.packageName = anchorContext.packageName;
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
    if (!state.matched || state.hidePopup)
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
            clearInlineAbbreviationSession();
        }
    } else if (activationState.action
               == CompletionActivationAction::ReplaceCommandInput) {
        const int insertionStart =
            replaceCommandInputAtCursor(activationState.text,
                                        activationState.selectionStart,
                                        activationState.selectionLength);
        if (activationState.clearCommandMode) {
            clearInlineAbbreviationSession();
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
    if (inlineAbbreviationSessionActive()
        && !inlineAbbreviationSessionValid()) {
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
    clearInlineAbbreviationSession();
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
    if (inlineAbbreviationSessionValid()
        && inlineSession.completion.intent
               == InlineCommandIntent::PackageImport) {
        if (activationContext.selectable)
            applyPackageImport(activationContext.itemText);
        return;
    }
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

QStringList EditorCompletionWorkflow::includeFileCandidates() const
{
    if (!editor || !includeFileProvider)
        return {};
    return includeFileProvider(editor->documentFileName());
}

bool EditorCompletionWorkflow::applyStructuredInsertionPlan(
    const StructuredInlineInsertionPlan& plan,
    QString* failureReason)
{
    if (!editor) {
        if (failureReason)
            *failureReason = QStringLiteral("No editable editor tab is available.");
        return false;
    }
    if (!plan.ok()) {
        if (plan.duplicate()) {
            if (!plan.failureMessage.isEmpty())
                emit editor->editorStatusMessageRequested(plan.failureMessage);
            if (failureReason)
                failureReason->clear();
            return true;
        }
        if (failureReason)
            *failureReason = plan.failureMessage;
        return false;
    }

    QTextCursor cursor(editor->document());
    cursor.beginEditBlock();
    cursor.setPosition(plan.replacementStart);
    cursor.setPosition(plan.replacementEnd, QTextCursor::KeepAnchor);
    cursor.insertText(plan.replacementText);
    cursor.endEditBlock();
    editor->setTextCursor(cursor);
    if (failureReason)
        failureReason->clear();
    return true;
}

bool EditorCompletionWorkflow::insertPackageImportAtCursor(
    const QString& packageName,
    QString* failureReason)
{
    if (!editor)
        return applyStructuredInsertionPlan({}, failureReason);
    const int position = editor->textCursor().position();
    return applyStructuredInsertionPlan(
        PackageToolService::packageImportPlan(
            editor->cachedDocumentText(), position, position, packageName),
        failureReason);
}

bool EditorCompletionWorkflow::insertHeaderIncludeAtCursor(
    const QString& includePath,
    QString* failureReason)
{
    if (!editor)
        return applyStructuredInsertionPlan({}, failureReason);
    const int position = editor->textCursor().position();
    return applyStructuredInsertionPlan(
        PackageToolService::headerIncludePlan(
            editor->cachedDocumentText(), position, position, includePath),
        failureReason);
}

bool EditorCompletionWorkflow::createAndInsertHeaderAtCursor(
    const QString& fileName,
    QString* failureReason)
{
    if (!editor || !includeNewHeaderCreator) {
        if (failureReason)
            *failureReason = QStringLiteral("Header creation is unavailable.");
        return false;
    }

    const QString typedName = fileName.trimmed();
    const QString stem = sanitizedIncludeHeaderStem(typedName);
    QString extension = QFileInfo(QDir::fromNativeSeparators(typedName))
                            .suffix()
                            .toLower();
    if (extension.isEmpty())
        extension = QStringLiteral("svh");
    if (stem.isEmpty()
        || (extension != QStringLiteral("vh")
            && extension != QStringLiteral("svh"))) {
        if (failureReason)
            *failureReason = QStringLiteral("Use a .vh or .svh header name.");
        return false;
    }

    const QString normalizedName = stem + QLatin1Char('.') + extension;
    const int position = editor->textCursor().position();
    const StructuredInlineInsertionPlan preflight =
        PackageToolService::headerIncludePlan(
            editor->cachedDocumentText(), position, position, normalizedName);
    if (!preflight.ok())
        return applyStructuredInsertionPlan(preflight, failureReason);

    const QList<IncludeTemplateDefinition> templates =
        includeHeaderTemplates(stem, extension);
    if (templates.isEmpty()) {
        if (failureReason)
            *failureReason = QStringLiteral("No header template is available.");
        return false;
    }

    IncludeNewHeaderRequest request;
    request.fileStem = stem;
    request.extension = extension;
    request.currentFileName = editor->documentFileName();
    request.templateName = templates.constFirst().name;
    request.templateBody = templates.constFirst().body;
    request.cursorToken = QStringLiteral("__ZEROSLACK_CURSOR__");
    const IncludeNewHeaderResult result = includeNewHeaderCreator(request);
    if (!result.success) {
        if (failureReason)
            *failureReason = result.errorMessage;
        return false;
    }
    return applyStructuredInsertionPlan(
        PackageToolService::headerIncludePlan(
            editor->cachedDocumentText(),
            position,
            position,
            result.includePath),
        failureReason);
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

bool EditorCompletionWorkflow::applyPackageImport(
    const QString& packageName)
{
    if (!editor || packageName.isEmpty()
        || !inlineAbbreviationSessionValid()) {
        return false;
    }

    const StructuredInlineInsertionPlan plan =
        PackageToolService::packageImportPlan(
            editor->cachedDocumentText(),
            inlineSession.replacementStartPosition,
            inlineSession.replacementEndPosition,
            packageName);
    if (!plan.ok() && !plan.duplicate()) {
        if (!plan.failureMessage.isEmpty()) {
            emit editor->editorStatusMessageRequested(
                plan.failureMessage);
        }
        cancelInlineAbbreviationSession();
        return true;
    }

    QTextCursor cursor(editor->document());
    applyingInlineReplacement = true;
    cursor.beginEditBlock();
    cursor.setPosition(plan.replacementStart);
    cursor.setPosition(plan.replacementEnd,
                       QTextCursor::KeepAnchor);
    cursor.insertText(plan.ok() ? plan.replacementText : QString());
    cursor.endEditBlock();
    applyingInlineReplacement = false;
    editor->setTextCursor(cursor);

    if (plan.duplicate() && !plan.failureMessage.isEmpty()) {
        emit editor->editorStatusMessageRequested(
            plan.failureMessage);
    }
    clearInlineAbbreviationSession();
    hideCompletionPopup();
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

    const StructuredInlineInsertionPlan plan =
        PackageToolService::headerIncludePlan(
            editor->cachedDocumentText(),
            context.replacementStartPosition,
            context.replacementEndPosition,
            includePath);
    if (!plan.ok() && !plan.duplicate()) {
        if (!plan.failureMessage.isEmpty()) {
            emit editor->editorStatusMessageRequested(
                plan.failureMessage);
        }
        cancelInlineAbbreviationSession();
        return;
    }

    QTextCursor cursor(editor->document());
    applyingInlineReplacement = true;
    cursor.beginEditBlock();
    cursor.setPosition(plan.replacementStart);
    cursor.setPosition(plan.replacementEnd,
                       QTextCursor::KeepAnchor);
    cursor.insertText(plan.ok() ? plan.replacementText : QString());
    cursor.endEditBlock();
    applyingInlineReplacement = false;
    editor->setTextCursor(cursor);

    if (plan.duplicate() && !plan.failureMessage.isEmpty()) {
        emit editor->editorStatusMessageRequested(
            plan.failureMessage);
    }
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

    const StructuredInlineInsertionPlan preflight =
        PackageToolService::headerIncludePlan(
            editor->cachedDocumentText(),
            context.replacementStartPosition,
            context.replacementEndPosition,
            choice);
    if (!preflight.ok()) {
        if (preflight.duplicate()) {
            applyIncludeCompletion(choice);
        } else if (!preflight.failureMessage.isEmpty()) {
            emit editor->editorStatusMessageRequested(
                preflight.failureMessage);
            cancelInlineAbbreviationSession();
        }
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
