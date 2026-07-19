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
    void clearCommandMode();
    void setComModeActive(bool active);
    void setComBuffer(const QString& input);
    void clearComBuffer();

};

#endif // EDITORMODESTATE_H
