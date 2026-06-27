#include "editormodestate.h"

void EditorModeState::setAlternateModeEnabled(bool enabled)
{
    alternateModeActive = enabled;
}

void EditorModeState::setCommandModeActive(bool active)
{
    commandModeActive = active;
}

void EditorModeState::noteCompletionTimerLine(int lineNumber)
{
    if (completionTimerLineNumber == lineNumber)
        return;

    commandModeExitedByDoubleSpace = false;
    completionTimerLineNumber = lineNumber;
}

void EditorModeState::markCommandModeExitedByDoubleSpace()
{
    commandModeExitedByDoubleSpace = true;
}

void EditorModeState::resetCommandModeExit()
{
    commandModeExitedByDoubleSpace = false;
}

void EditorModeState::clearCommandMode()
{
    commandModeActive = false;
}

void EditorModeState::setAlternateBuffer(const QString& input)
{
    alternateBuffer = input;
}

QString EditorModeState::alternateBufferWithoutLastChar() const
{
    return alternateBuffer.left(alternateBuffer.size() - 1);
}

void EditorModeState::clearAlternateBuffer()
{
    alternateBuffer.clear();
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
