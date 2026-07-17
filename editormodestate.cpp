#include "editormodestate.h"

void EditorModeState::setCommandModeActive(bool active)
{
    commandModeActive = active;
}

void EditorModeState::noteCompletionTimerLine(int lineNumber)
{
    if (completionTimerLineNumber == lineNumber)
        return;

    completionTimerLineNumber = lineNumber;
}

void EditorModeState::clearCommandMode()
{
    commandModeActive = false;
}

void EditorModeState::setComModeActive(bool active)
{
    comModeActive = active;
    if (!active)
        comBuffer.clear();
}

void EditorModeState::setComBuffer(const QString& input)
{
    comBuffer = input;
}

void EditorModeState::clearComBuffer()
{
    comBuffer.clear();
}
