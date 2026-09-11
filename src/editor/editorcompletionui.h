#ifndef EDITORCOMPLETIONUI_H
#define EDITORCOMPLETIONUI_H

#include <QList>
#include <QMetaObject>
#include <QString>

#include <functional>

#include "includeheaderworkflowtypes.h"

class CompletionModel;
class MyCodeEditor;
class QAbstractItemView;
class QCompleter;
class QKeyEvent;
class QModelIndex;
class QRect;
struct CommandModeCompletionState;
struct EditorCompletionActivationContext;
struct EditorCompletionPopupKeyContext;

class EditorCompletionUi
{
public:
    void attachToEditor(
        MyCodeEditor* editor,
        const std::function<void(const QModelIndex&)>& handleActivated);
    void detach();

    QAbstractItemView* popup() const;
    bool popupVisible() const;
    void hidePopup() const;
    int rowCount() const;
    bool hasRows() const;
    QModelIndex currentIndex() const;
    QModelIndex firstSelectableIndex() const;
    void activateIndex(const QModelIndex& index) const;

    EditorCompletionActivationContext activationContextForIndex(
        const QModelIndex& index) const;
    EditorCompletionPopupKeyContext popupKeyContextForEvent(
        QKeyEvent* event) const;
    void updateCommandModeCompletions(
        const CommandModeCompletionState& state,
        bool allowSymbolFallback = true) const;
    void updateIncludeFileCompletions(const QStringList& filePaths,
                                      const QString& prefix) const;
    void updateIncludeNewHeaderCompletions(
        const QList<IncludeNewHeaderChoice>& choices,
        const QString& title) const;
    void showForCursor(
        const QRect& cursorRectangle,
        bool selectFirstCompletion) const;

private:
    void init(MyCodeEditor* editor);

    QCompleter* completer = nullptr;
    CompletionModel* model = nullptr;
    QMetaObject::Connection activationConnection;
};

#endif // EDITORCOMPLETIONUI_H
