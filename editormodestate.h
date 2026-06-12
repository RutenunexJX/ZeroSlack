#ifndef EDITORMODESTATE_H
#define EDITORMODESTATE_H

#include <QString>

class EditorModeState
{
public:
    bool commandModeActive = false;
    bool alternateModeActive = false;
    QString alternateBuffer;
    bool commandModeExitedByDoubleSpace = false;

    void setAlternateModeEnabled(bool enabled);
    void setCommandModeActive(bool active);
    void noteCompletionTimerLine(int lineNumber);
    void markCommandModeExitedByDoubleSpace();
    void resetCommandModeExit();
    void clearCommandMode();
    void setAlternateBuffer(const QString& input);
    QString alternateBufferWithoutLastChar() const;
    void clearAlternateBuffer();

private:
    int completionTimerLineNumber = -1;
};

#endif // EDITORMODESTATE_H
