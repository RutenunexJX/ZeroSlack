#include "editorcompletionworkflow.h"

#include "editorcompletionui.h"
#include "editormodestate.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QKeyEvent>
#include <QModelIndex>

bool EditorCompletionWorkflow::handleCompletionPopupKey(QKeyEvent* event)
{
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
        hideAutoComplete();
        return true;
    case CompletionPopupKeyAction::HidePopupAndClearCommand:
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
