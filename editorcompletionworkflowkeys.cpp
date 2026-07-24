#include "editorcompletionworkflow.h"

#include "editorcompletionui.h"
#include "editormodestate.h"
#include "editorruntime.h"
#include "mycodeeditor.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QKeyEvent>
#include <QModelIndex>
#include <QTextCursor>

namespace {
bool isInlineFilterInput(const QKeyEvent* event)
{
    if (!event
        || event->modifiers().testFlag(Qt::ControlModifier)
        || event->modifiers().testFlag(Qt::AltModifier)
        || event->modifiers().testFlag(Qt::MetaModifier)
        || event->text().isEmpty()) {
        return false;
    }

    for (const QChar ch : event->text()) {
        if (!ch.isLetterOrNumber()
            && ch != QLatin1Char('_')
            && ch != QLatin1Char('$')) {
            return false;
        }
    }
    return true;
}

QModelIndex previousSelectableIndex(EditorCompletionUi* completion)
{
    if (!completion || !completion->popup()
        || !completion->popup()->model()) {
        return {};
    }

    const int rowCount = completion->popup()->model()->rowCount();
    if (rowCount <= 0)
        return {};

    const QModelIndex current = completion->currentIndex();
    int row = current.isValid() ? current.row() - 1 : rowCount - 1;
    if (row < 0)
        row = rowCount - 1;

    for (int visited = 0; visited < rowCount; ++visited) {
        const QModelIndex candidate =
            completion->popup()->model()->index(row, 0);
        if (completion->activationContextForIndex(candidate).selectable)
            return candidate;
        row = row > 0 ? row - 1 : rowCount - 1;
    }
    return {};
}
}

bool EditorCompletionWorkflow::handleInlineCandidateFilterKey(
    QKeyEvent* event)
{
    if (!inlineSession.active || !inlineSession.candidateFiltering)
        return false;
    if (!inlineAbbreviationSessionValid()) {
        cancelInlineAbbreviationSession();
        return false;
    }

    const bool plainBackspace = event->key() == Qt::Key_Backspace
        && !event->modifiers().testFlag(Qt::ControlModifier)
        && !event->modifiers().testFlag(Qt::AltModifier)
        && !event->modifiers().testFlag(Qt::MetaModifier);
    if (plainBackspace && inlineSession.filterText.isEmpty()) {
        cancelInlineAbbreviationSession();
        return false;
    }
    if (!plainBackspace && !isInlineFilterInput(event))
        return false;

    editor->state->beginInlineFilterTextOverlay(
        inlineSession.replacementStartPosition,
        inlineSession.replacementEndPosition);
    ++editor->state->hotPathMetrics.inlineFilterKeyEvents;
    QTextCursor cursor = editor->textCursor();
    applyingInlineReplacement = true;
    cursor.beginEditBlock();
    if (plainBackspace) {
        cursor.movePosition(QTextCursor::PreviousCharacter,
                            QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
    } else {
        QString insertion = event->text();
        if (inlineSession.filterText.isEmpty()
            && inlineSession.replacementEndPosition
                   == inlineSession.commandEndPosition) {
            insertion.prepend(QLatin1Char(' '));
            inlineSession.filterStartPosition =
                inlineSession.commandEndPosition + 1;
        }
        cursor.insertText(insertion);
    }
    cursor.endEditBlock();
    editor->setTextCursor(cursor);

    inlineSession.replacementEndPosition = cursor.position();
    inlineSession.abbreviationText = editor->cachedDocumentSlice(
        inlineSession.replacementStartPosition,
        inlineSession.replacementEndPosition
            - inlineSession.replacementStartPosition);
    inlineSession.filterText = editor->cachedDocumentSlice(
        inlineSession.filterStartPosition,
        inlineSession.replacementEndPosition
            - inlineSession.filterStartPosition);
    applyingInlineReplacement = false;

    event->accept();
    refreshInlineCandidateFilter();
    return true;
}

bool EditorCompletionWorkflow::handleCompletionPopupKey(QKeyEvent* event)
{
    if (handleInlineCandidateFilterKey(event))
        return true;
    if (!completion->popupVisible())
        return false;

    const CompletionPopupKeyState popupState =
        semanticService()->completionPopupKeyState(
            completion->popupKeyContextForEvent(event));
    return applyCompletionPopupKeyState(event, popupState);
}

bool EditorCompletionWorkflow::applyCompletionPopupKeyState(
    QKeyEvent* event,
    const CompletionPopupKeyState& popupState)
{
    switch (popupState.action) {
    case CompletionPopupKeyAction::ForwardToPopup:
        QApplication::sendEvent(completion->popup(), event);
        return true;
    case CompletionPopupKeyAction::SelectPreviousSelectable:
        {
            const QModelIndex previous = previousSelectableIndex(completion);
            if (previous.isValid())
                completion->popup()->setCurrentIndex(previous);
        }
        return true;
    case CompletionPopupKeyAction::ActivateCurrent:
        if (completion->currentIndex().isValid())
            completion->activateIndex(completion->currentIndex());
        return true;
    case CompletionPopupKeyAction::ActivateCurrentOrFirstSelectable:
        {
            QModelIndex currentIndex = completion->currentIndex();
            if (!currentIndex.isValid() && completion->hasRows())
                currentIndex = completion->firstSelectableIndex();
            if (currentIndex.isValid())
                completion->activateIndex(currentIndex);
        }
        return true;
    case CompletionPopupKeyAction::HidePopupAndClearCommand:
        if (inlineSession.active) {
            clearInlineAbbreviationSession();
            modes->clearCommandMode();
            hideCompletionPopup();
            return true;
        }
        clearCommandInputAtCursor();
        hideCompletionPopup();
        return true;
    case CompletionPopupKeyAction::Consume:
        return true;
    case CompletionPopupKeyAction::None:
        return false;
    }

    return false;
}
