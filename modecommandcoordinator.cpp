#include "modecommandcoordinator.h"

#include "modemanager.h"
#include "navigationpanecoordinator.h"

ModeCommandCoordinator::ModeCommandCoordinator(
    ModeManager* modeManager,
    NavigationPaneCoordinator* navigationPane,
    QObject* parent)
    : QObject(parent)
    , modeManager(modeManager)
    , navigationPane(navigationPane)
{
}

void ModeCommandCoordinator::connectSignals()
{
    if (signalsConnected || !modeManager)
        return;

    connect(modeManager,
            &ModeManager::navigationToggleRequested,
            this,
            [this]() {
                if (navigationPane)
                    navigationPane->toggleVisible();
            });

    signalsConnected = true;
}

bool ModeCommandCoordinator::handleKeyPress(QKeyEvent* event)
{
    return modeManager && modeManager->handleKeyPress(event);
}

bool ModeCommandCoordinator::handleKeyRelease(QKeyEvent* event)
{
    return modeManager && modeManager->handleKeyRelease(event);
}
