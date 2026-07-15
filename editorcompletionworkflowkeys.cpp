#include "editorcompletionworkflow.h"

#include "editorcompletionui.h"
#include "editormodestate.h"
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
    const QString documentText = editor->document()->toPlainText();
    inlineSession.abbreviationText = documentText.mid(
        inlineSession.replacementStartPosition,
        inlineSession.replacementEndPosition
            - inlineSession.replacementStartPosition);
    inlineSession.filterText = documentText.mid(
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
            completion->popupKeyContextForEvent(event, *modes));
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
    case CompletionPopupKeyAction::HidePopup:
        clearInlineAbbreviationSession();
        hideAutoComplete();
        return true;
    case CompletionPopupKeyAction::HidePopupAndClearCommand:
        if (inlineSession.active) {
            clearInlineAbbreviationSession();
            modes->clearCommandMode();
            hideAutoComplete();
            return true;
        }
        clearCommandInputAtCursor();
        hideAutoComplete();
        return true;
    case CompletionPopupKeyAction::Consume:
        return true;
    case CompletionPopupKeyAction::None:
        return false;
    }

    return false;
}
