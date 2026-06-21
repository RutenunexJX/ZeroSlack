#include "editorcompletionui.h"

#include "completionmodel.h"
#include "editorsemanticcontextservice.h"
#include "editormodestate.h"
#include "mycodeeditor.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QKeyEvent>
#include <QModelIndex>
#include <QPlainTextEdit>
#include <QRect>
#include <QTextCursor>
#include <QTimer>
#include <algorithm>

void EditorCompletionUi::init(MyCodeEditor* editor)
{
    model = new CompletionModel(editor);
    completer = new QCompleter(editor);
    completer->setModel(model);
    completer->setWidget(editor);
    completer->setCompletionMode(QCompleter::PopupCompletion);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setMaxVisibleItems(15);
    popup()->setStyleSheet(QStringLiteral(
        "QListView::item { padding: 1px 4px; min-height: 18px; }"));
    timer = new QTimer(editor);
    timer->setSingleShot(true);
    timer->setInterval(0);
}

void EditorCompletionUi::attachToEditor(
    MyCodeEditor* editor,
    const std::function<void()>& handleTimer,
    const std::function<void(const QModelIndex&)>& handleActivated,
    const std::function<void()>& handleTextChanged)
{
    init(editor);
    QObject::connect(timer, &QTimer::timeout, editor, handleTimer);
    QObject::connect(
        completer,
        QOverload<const QModelIndex&>::of(&QCompleter::activated),
        editor,
        handleActivated);
    QObject::connect(
        editor,
        &QPlainTextEdit::textChanged,
        editor,
        handleTextChanged);
}

QAbstractItemView* EditorCompletionUi::popup() const
{
    return completer->popup();
}

bool EditorCompletionUi::popupVisible() const
{
    return popup()->isVisible();
}

void EditorCompletionUi::hidePopup() const
{
    popup()->hide();
}

void EditorCompletionUi::startTimer() const
{
    timer->start();
}

void EditorCompletionUi::stopTimer() const
{
    timer->stop();
}

int EditorCompletionUi::rowCount() const
{
    return model->rowCount();
}

bool EditorCompletionUi::hasRows() const
{
    return rowCount() > 0;
}

QModelIndex EditorCompletionUi::currentIndex() const
{
    return popup()->currentIndex();
}

QModelIndex EditorCompletionUi::firstSelectableIndex() const
{
    return model->firstSelectableIndex();
}

void EditorCompletionUi::activateIndex(const QModelIndex& index) const
{
    emit completer->activated(index);
}

EditorCompletionActivationContext
EditorCompletionUi::activationContextForIndex(
    const QModelIndex& index,
    const EditorModeState& modes) const
{
    const CompletionModel::CompletionItem item = model->getItem(index);
    EditorCompletionActivationContext context;
    context.selectable = model->isSelectableIndex(index);
    context.alternateModeActive = modes.alternateModeActive;
    context.commandModeActive = modes.commandModeActive;
    context.itemText = item.text;
    context.defaultValue = item.defaultValue;
    return context;
}

EditorCompletionPopupKeyContext EditorCompletionUi::popupKeyContextForEvent(
    QKeyEvent* event,
    const EditorModeState& modes) const
{
    EditorCompletionPopupKeyContext context;
    context.key = event->key();
    context.alternateModeActive = modes.alternateModeActive;
    context.commandModeActive = modes.commandModeActive;
    context.currentIndexValid = currentIndex().isValid();
    context.hasRows = hasRows();
    context.alternateBufferEmpty = modes.alternateBuffer.isEmpty();
    return context;
}

void EditorCompletionUi::updateCommandModeCompletions(
    const EditorCommandModeCompletionRefreshState& commandState) const
{
    if (commandState.completion.intent != InlineCommandIntent::SemanticCompletion
        || commandState.completion.helpRequested) {
        model->updateInlineCommandCompletions(commandState.completion);
        return;
    }

    model->updateSymbolRecordCompletions(
        commandState.completion.symbolRecords,
        commandState.completion.completionPrefix,
        commandState.completion.commandKind);
}

void EditorCompletionUi::updateSymbolCompletions(
    const EditorCompletionState& completionState) const
{
    model->updateCompletions(completionState.completion, completionState.prefix);
}

void EditorCompletionUi::updateAlternateModeCompletions(
    const EditorAlternateModeCompletionDisplayState& displayState) const
{
    model->updateCommandCompletions(
        displayState.matches,
        displayState.normalizedInput);
}

void EditorCompletionUi::setReplacementStart(
    int blockPosition,
    int replacementStartColumn)
{
    wordStartPos = blockPosition + replacementStartColumn;
}

QString EditorCompletionUi::wordUnderCursor(MyCodeEditor* editor)
{
    QTextCursor cursor = editor->textCursor();
    const int currentPosition = cursor.position();
    cursor.movePosition(QTextCursor::StartOfWord);
    wordStartPos = cursor.position();
    cursor.setPosition(currentPosition);
    cursor.movePosition(QTextCursor::EndOfWord);
    cursor.setPosition(wordStartPos);
    cursor.setPosition(currentPosition, QTextCursor::KeepAnchor);
    return cursor.selectedText();
}

void EditorCompletionUi::replaceWordAtCursor(
    MyCodeEditor* editor,
    const QString& text) const
{
    QTextCursor cursor = editor->textCursor();
    cursor.setPosition(wordStartPos);
    cursor.setPosition(
        editor->textCursor().position(),
        QTextCursor::KeepAnchor);
    cursor.insertText(text);
}

void EditorCompletionUi::showForCursor(
    const QRect& cursorRectangle,
    bool selectFirstCompletion) const
{
    if (!hasRows())
        return;

    QRect popupRectangle = cursorRectangle;
    const int hintedWidth = popup()->sizeHintForColumn(0);
    const int popupWidth = std::clamp(hintedWidth + 24, 240, 720);
    popupRectangle.setWidth(popupWidth);
    if (selectFirstCompletion) {
        const QModelIndex selectableIndex = firstSelectableIndex();
        if (selectableIndex.isValid())
            popup()->setCurrentIndex(selectableIndex);
    }

    completer->complete(popupRectangle);
}
