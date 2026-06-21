#include "editorcompletionworkflow.h"

#include "editorcompletionui.h"
#include "editormodestate.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QKeyEvent>
#include <QModelIndex>

void EditorCompletionWorkflow::applyAlternateModeKeyState(
    const EditorAlternateModeKeyState& keyState)
{
    switch (keyState.action) {
    case EditorAlternateModeKeyAction::UpdateInput:
    case EditorAlternateModeKeyAction::RefreshCompletions:
        applyAlternateModeCompletionDisplayState(keyState.completion);
        break;
    case EditorAlternateModeKeyAction::ExecuteCommand:
        executeAlternateModeCommand(keyState.command);
        break;
    case EditorAlternateModeKeyAction::ClearAndHide:
        if (keyState.hidePopup)
            hideAutoComplete();
        if (keyState.clearBuffer)
            modes->clearAlternateBuffer();
        break;
    case EditorAlternateModeKeyAction::Consume:
        break;
    }
}

bool EditorCompletionWorkflow::handleAlternateModeKey(QKeyEvent* event)
{
    if (handleCompletionPopupKey(event))
        return true;

    EditorAlternateModeKeyContext alternateKeyContext;
    alternateKeyContext.key = event->key();
    alternateKeyContext.text = event->text();
    alternateKeyContext.buffer = modes->alternateBuffer;
    const EditorAlternateModeKeyState alternateKeyState =
        semanticService()->alternateModeKeyState(alternateKeyContext);
    applyAlternateModeKeyState(alternateKeyState);
    return true;
}

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
    case CompletionPopupKeyAction::HidePopupAndClearAlternate:
        hideAutoComplete();
        modes->clearAlternateBuffer();
        return true;
    case CompletionPopupKeyAction::BackspaceAlternateInput:
        if (!modes->alternateBuffer.isEmpty()) {
            const EditorAlternateModeCompletionDisplayState completionState =
                semanticService()->alternateModeCompletionDisplayState(
                    modes->alternateBufferWithoutLastChar());
            applyAlternateModeCompletionDisplayState(completionState);
        } else {
            hideAutoComplete();
        }
        return true;
    case CompletionPopupKeyAction::Consume:
        return true;
    case CompletionPopupKeyAction::None:
        return false;
    }

    return false;
}
