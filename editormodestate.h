#ifndef EDITORMODESTATE_H
#define EDITORMODESTATE_H

#include <QString>

class EditorModeState
{
public:
    bool commandModeActive = false;
    bool comModeActive = false;
    QString comBuffer;

    void setCommandModeActive(bool active);
    void noteCompletionTimerLine(int lineNumber);
    void clearCommandMode();
    void setComModeActive(bool active);
    void setComBuffer(const QString& input);
    void clearComBuffer();

private:
    int completionTimerLineNumber = -1;
};

#endif // EDITORMODESTATE_H
