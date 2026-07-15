#ifndef EDITORCOMPLETIONWORKFLOW_H
#define EDITORCOMPLETIONWORKFLOW_H

#include "completiontypes.h"
#include "editorsemanticcontextservice.h"
#include "includeheaderworkflowtypes.h"

#include <functional>
#include <QList>
#include <QStringList>

class EditorCompletionUi;
class EditorModeState;
class EditorSelection;
class MyCodeEditor;
class QKeyEvent;
class QModelIndex;
class QTextBlock;
class QTextCursor;

class EditorCompletionWorkflow
{
public:
    using ContextProvider =
        std::function<EditorSemanticContext(int, bool)>;
    using ModuleNameProvider = std::function<QString(int)>;
    using SemanticServiceProvider =
        std::function<EditorSemanticContextService*()>;
    using IncludeFileProvider =
        std::function<QStringList(const QString& currentFile)>;
    using IncludeNewHeaderCreator =
        std::function<IncludeNewHeaderResult(
            const IncludeNewHeaderRequest& request)>;

    void bind(
        MyCodeEditor* editor,
        EditorCompletionUi* completion,
        EditorModeState* modes,
        EditorSelection* selections,
        const ContextProvider& contextProvider,
        const ModuleNameProvider& moduleNameProvider,
        const SemanticServiceProvider& serviceProvider);

    bool handleCompletionPopupKey(QKeyEvent* event);
    void handleTextChanged();
    void handleCompletionActivated(const QModelIndex& index);
    void handleAutoCompleteTimer();
    void handleCursorPositionChanged();
    bool handleInlineAbbreviationTab(QKeyEvent* event);
    void setIncludeFileProvider(IncludeFileProvider provider);
    void setIncludeNewHeaderCreator(IncludeNewHeaderCreator creator);

private:
    enum class IncludeCompletionMode {
        None,
        File,
        NewHeader
    };

    struct IncludeCompletionContext {
        bool active = false;
        QString prefix;
        int replacementStartPosition = -1;
        int replacementEndPosition = -1;
    };

    struct InlineAbbreviationSession {
        bool active = false;
        bool candidateFiltering = false;
        int replacementStartPosition = -1;
        int replacementEndPosition = -1;
        int anchorPosition = -1;
        int commandEndPosition = -1;
        int filterStartPosition = -1;
        QString abbreviationText;
        QString filterText;
        CommandModeCompletionQuery anchorQuery;
        CommandModeCompletionState completion;
    };

    EditorSemanticContextService* semanticService() const;
    EditorSemanticContext semanticContextForCursor(
        const QTextCursor& cursor,
        bool includeDocumentText) const;
    void hideAutoComplete();
    void showAutoComplete(bool selectFirstCompletion = false);
    void executeEditorActionCommand(const QString& command);
    void clearInlineAbbreviationSession();
    void cancelInlineAbbreviationSession();
    bool inlineAbbreviationSessionValid() const;
    bool handleInlineCandidateFilterKey(QKeyEvent* event);
    bool refreshInlineCandidateFilter();
    bool showInlineAbbreviationCompletions(
        const CommandModeCompletionState& state,
        int replacementStartPosition,
        int replacementEndPosition,
        int anchorPosition,
        int commandEndPosition,
        int filterStartPosition,
        const QString& abbreviationText,
        const CommandModeCompletionQuery& anchorQuery);
    bool applySingleInlineAbbreviationCandidate(
        const CommandModeCompletionState& state);
    int inlineCandidateCount(const CommandModeCompletionState& state) const;
    void updateCompletionTriggerForTextChange(const QTextCursor& cursor);
    void applyCompletionActivationState(
        const CompletionActivationState& activationState);
    int replaceCommandInputAtCursor(const QString& text,
                                    int selectionStart = -1,
                                    int selectionLength = 0);
    void clearCommandInputAtCursor();
    bool applyCompletionPopupKeyState(
        QKeyEvent* event,
        const CompletionPopupKeyState& popupState);
    bool refreshCommandModeCompletion(
        const EditorSemanticContext& context);
    void refreshSymbolCompletion(
        EditorSemanticContext context,
        const QTextBlock& currentBlock);
    IncludeCompletionContext includeCompletionContextAtCursor() const;
    bool showIncludeCommandCompletions(
        const CommandModeCompletionState& state);
    void showIncludeFileCompletions(
        const IncludeCompletionContext& context);
    bool showIncludeNewHeaderCompletions(
        const IncludeCompletionContext& context);
    void applyIncludeCompletion(const QString& includePath);
    void applyIncludeNewHeaderChoice(const QString& choice);

    MyCodeEditor* editor = nullptr;
    EditorCompletionUi* completion = nullptr;
    EditorModeState* modes = nullptr;
    EditorSelection* selections = nullptr;
    ContextProvider contextProvider;
    ModuleNameProvider moduleNameProvider;
    SemanticServiceProvider serviceProvider;
    IncludeFileProvider includeFileProvider;
    IncludeNewHeaderCreator includeNewHeaderCreator;
    bool includeCompletionActive = false;
    IncludeCompletionMode includeCompletionMode = IncludeCompletionMode::None;
    InlineAbbreviationSession inlineSession;
    bool applyingInlineReplacement = false;
};

#endif // EDITORCOMPLETIONWORKFLOW_H
