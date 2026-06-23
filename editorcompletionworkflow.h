#ifndef EDITORCOMPLETIONWORKFLOW_H
#define EDITORCOMPLETIONWORKFLOW_H

#include "editorsemanticcontextservice.h"

#include <functional>

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

    void bind(
        MyCodeEditor* editor,
        EditorCompletionUi* completion,
        EditorModeState* modes,
        EditorSelection* selections,
        const ContextProvider& contextProvider,
        const ModuleNameProvider& moduleNameProvider,
        const SemanticServiceProvider& serviceProvider);

    bool handleAlternateModeKey(QKeyEvent* event);
    bool handleCompletionPopupKey(QKeyEvent* event);
    void handleTextChanged();
    void handleCompletionActivated(const QModelIndex& index);
    void handleAutoCompleteTimer();
    void executeAlternateModeCommand(const QString& command);

private:
    EditorSemanticContextService* semanticService() const;
    EditorSemanticContext semanticContextForCursor(
        const QTextCursor& cursor,
        bool includeDocumentText) const;
    void hideAutoComplete();
    void showAutoComplete();
    void applyAlternateModeCompletionDisplayState(
        const EditorAlternateModeCompletionDisplayState& displayState);
    void processAlternateModeInput(const QString& input);
    void executeEditorActionCommand(const QString& command);
    void updateCompletionTriggerForTextChange(const QTextCursor& cursor);
    void applyCompletionActivationState(
        const CompletionActivationState& activationState);
    void replaceCommandInputAtCursor(const QString& text,
                                     int selectionStart = -1,
                                     int selectionLength = 0);
    void clearCommandInputAtCursor();
    void applyAlternateModeKeyState(
        const EditorAlternateModeKeyState& keyState);
    bool applyCompletionPopupKeyState(
        QKeyEvent* event,
        const CompletionPopupKeyState& popupState);
    bool refreshCommandModeCompletion(
        const EditorSemanticContext& context);
    void refreshSymbolCompletion(
        EditorSemanticContext context,
        const QTextBlock& currentBlock);

    MyCodeEditor* editor = nullptr;
    EditorCompletionUi* completion = nullptr;
    EditorModeState* modes = nullptr;
    EditorSelection* selections = nullptr;
    ContextProvider contextProvider;
    ModuleNameProvider moduleNameProvider;
    SemanticServiceProvider serviceProvider;
};

#endif // EDITORCOMPLETIONWORKFLOW_H
