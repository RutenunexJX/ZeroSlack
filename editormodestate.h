#ifndef EDITORMODESTATE_H
#define EDITORMODESTATE_H

class EditorModeState
{
public:
    bool commandModeActive = false;

    void setCommandModeActive(bool active);
    void clearCommandMode();
};

#endif // EDITORMODESTATE_H
