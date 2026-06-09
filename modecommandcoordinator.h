#ifndef MODECOMMANDCOORDINATOR_H
#define MODECOMMANDCOORDINATOR_H

#include <QObject>

class ModeManager;
class NavigationPaneCoordinator;
class QKeyEvent;

class ModeCommandCoordinator : public QObject
{
    Q_OBJECT

public:
    explicit ModeCommandCoordinator(ModeManager* modeManager,
                                    NavigationPaneCoordinator* navigationPane,
                                    QObject* parent = nullptr);

    void connectSignals();
    bool handleKeyPress(QKeyEvent* event);
    bool handleKeyRelease(QKeyEvent* event);

private:
    ModeManager* modeManager = nullptr;
    NavigationPaneCoordinator* navigationPane = nullptr;
    bool signalsConnected = false;
};

#endif // MODECOMMANDCOORDINATOR_H
