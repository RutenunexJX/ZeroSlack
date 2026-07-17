#ifndef EDITORCOMPLETIONUI_H
#define EDITORCOMPLETIONUI_H

#include <QList>
#include <QString>

#include <functional>

#include "includeheaderworkflowtypes.h"

class CompletionModel;
class EditorModeState;
class MyCodeEditor;
class QAbstractItemView;
class QCompleter;
class QKeyEvent;
class QModelIndex;
class QRect;
class QTimer;
struct CommandModeCompletionState;
struct EditorCompletionActivationContext;
struct EditorCompletionPopupKeyContext;
struct EditorCompletionState;

class EditorCompletionUi
{
public:
    void attachToEditor(
        MyCodeEditor* editor,
        const std::function<void()>& handleTimer,
        const std::function<void(const QModelIndex&)>& handleActivated,
        const std::function<void()>& handleTextChanged);

    QAbstractItemView* popup() const;
    bool popupVisible() const;
    void hidePopup() const;
    void startTimer() const;
    void stopTimer() const;
    int rowCount() const;
    bool hasRows() const;
    QModelIndex currentIndex() const;
    QModelIndex firstSelectableIndex() const;
    void activateIndex(const QModelIndex& index) const;

    EditorCompletionActivationContext activationContextForIndex(
        const QModelIndex& index,
        const EditorModeState& modes) const;
    EditorCompletionPopupKeyContext popupKeyContextForEvent(
        QKeyEvent* event,
        const EditorModeState& modes) const;
    void updateCommandModeCompletions(
        const CommandModeCompletionState& state,
        bool allowSymbolFallback = true) const;
    void updateIncludeFileCompletions(const QStringList& filePaths,
                                      const QString& prefix) const;
    void updateIncludeNewHeaderCompletions(
        const QList<IncludeNewHeaderChoice>& choices,
        const QString& title) const;
    void updateSymbolCompletions(
        const EditorCompletionState& completionState) const;
    void setReplacementStart(int blockPosition, int replacementStartColumn);
    QString wordUnderCursor(MyCodeEditor* editor);
    void replaceWordAtCursor(MyCodeEditor* editor, const QString& text) const;
    void showForCursor(
        const QRect& cursorRectangle,
        bool selectFirstCompletion) const;

private:
    void init(MyCodeEditor* editor);

    QCompleter* completer = nullptr;
    CompletionModel* model = nullptr;
    QTimer* timer = nullptr;
    int wordStartPos = 0;
};

#endif // EDITORCOMPLETIONUI_H
