#include "editormodestate.h"

void EditorModeState::setCommandModeActive(bool active)
{
    commandModeActive = active;
}

void EditorModeState::clearCommandMode()
{
    commandModeActive = false;
}
