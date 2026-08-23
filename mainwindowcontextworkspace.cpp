#include "mainwindow.h"

#include "contextworkspacecontroller.h"
#include "navigationcommandcoordinator.h"
#include "panellayoutcontroller.h"
#include "pinloomcodelinkcoordinator.h"
#include "pinloomcontextprovider.h"
#include "pinloomhostclient.h"
#include "settingscenterpanel.h"
#include "temporaryeditorcontextprovider.h"
#include "temporaryeditorsearchprovider.h"
#include "workspacemanager.h"

#include <utility>

bool MainWindow::revealSuiteSource(const QString& filePath,
                                   int lineNumber,
                                   int columnNumber)
{
    if (!navigationCommandCoordinator || filePath.trimmed().isEmpty())
        return false;
    const bool revealed =
        navigationCommandCoordinator->navigateToFileAndLineAndFlash(
            filePath,
            qMax(1, lineNumber),
            qMax(1, columnNumber));
    if (revealed) {
        show();
        raise();
        activateWindow();
    }
    return revealed;
}

void MainWindow::setupContextWorkspace()
{
    if (!editorSplitHost || contextWorkspaceController)
        return;
    contextWorkspaceController =
        std::make_unique<ContextWorkspaceController>(
            this,
            editorSplitHost,
            this);
    pinloomCodeLinkCoordinator =
        std::make_unique<PinloomCodeLinkCoordinator>(
            contextWorkspaceController.get());
    if (panelLayoutController) {
        panelLayoutController->registerSidePanel(
            QStringLiteral("contextWorkspace"),
            contextWorkspaceController->dockWidget());
    }
    if (workspaceManager) {
        const QString workspaceRoot =
            workspaceManager->isWorkspaceOpen()
            ? workspaceManager->getWorkspacePath()
            : QString();
        contextWorkspaceController->setWorkspaceRoot(workspaceRoot);
        pinloomCodeLinkCoordinator->setWorkspaceRoot(workspaceRoot);
        connect(workspaceManager.get(),
                &WorkspaceManager::workspaceActivated,
                contextWorkspaceController.get(),
                [controller = contextWorkspaceController.get(),
                 coordinator = pinloomCodeLinkCoordinator.get()](
                    int,
                    const QString&,
                    const QString& path) {
                    controller->setWorkspaceRoot(path);
                    coordinator->setWorkspaceRoot(path);
                });
        connect(workspaceManager.get(),
                &WorkspaceManager::workspaceClosed,
                contextWorkspaceController.get(),
                [controller = contextWorkspaceController.get(),
                 coordinator = pinloomCodeLinkCoordinator.get()]() {
                    controller->setWorkspaceRoot({});
                    coordinator->setWorkspaceRoot({});
                });
    }

    auto temporaryProvider =
        std::make_unique<TemporaryEditorContextProvider>(
            tabManager.get());
    temporaryProvider->setSearchProvider(
        [this](const QString& rawQuery)
            -> EditorSearchCandidates {
            return temporaryEditorSearchProvider
                ? temporaryEditorSearchProvider->query(rawQuery)
                : EditorSearchCandidates{};
        });
    contextWorkspaceController->registerProvider(
        std::move(temporaryProvider));

    pinloomHostClient = std::make_unique<PinloomHostClient>(this);
    if (settingsCenterPanel) {
        pinloomHostClient->setExecutablePath(
            settingsCenterPanel->snapshot()
                .value(QStringLiteral(
                    "integration.pinloomExecutablePath"))
                .toString());
    }
    auto pinloomProvider =
        std::make_unique<PinloomContextProvider>(
            pinloomHostClient.get());
    pinloomProvider->setLinkHandler(
        [this](const QVariantMap& sourceMap,
               const PinloomHostEntry& entry,
               QString* failureReason) {
            if (!pinloomCodeLinkCoordinator) {
                if (failureReason) {
                    *failureReason = QStringLiteral(
                        "Code-link storage is unavailable.");
                }
                return false;
            }
            return pinloomCodeLinkCoordinator->attachLink(
                sourceMap, entry, failureReason);
        });
    contextWorkspaceController->registerProvider(
        std::move(pinloomProvider));
}
