#ifndef WORKSPACESESSIONCOORDINATOR_H
#define WORKSPACESESSIONCOORDINATOR_H

#include "workspacesessionstateservice.h"
#include "zeroslackexport.h"

#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>

#include <functional>
#include <cstdint>

class PanelLayoutController;
class QTimer;
class TabManager;
class WorkspaceManager;

struct WorkspaceSessionUiRestoreResult {
    bool geometryRestored = true;
    bool dockStateRestored = true;
};

struct WorkspaceSessionUiBridge {
    std::function<WorkspaceSessionUiState(bool rememberPanelState)>
        captureUiState;
    std::function<WorkspaceSessionUiRestoreResult(
        const WorkspaceSessionUiState& state,
        bool rememberPanelState)>
        restoreUiState;
    std::function<void(const QString& message, int timeoutMs)>
        showStatus;
};

class ZEROSLACK_API WorkspaceSessionCoordinator : public QObject
{
    Q_OBJECT

public:
    static constexpr int kDefaultSaveDelayMs = 900;

    explicit WorkspaceSessionCoordinator(
        WorkspaceManager* workspaceManager,
        TabManager* tabManager,
        PanelLayoutController* panelLayoutController,
        WorkspaceSessionUiBridge uiBridge,
        const QString& settingsFilePath = QString(),
        int saveDelayMs = kDefaultSaveDelayMs,
        QObject* parent = nullptr);
    ~WorkspaceSessionCoordinator() override;

    void setRestoreOnActivation(bool enabled);
    void setRememberPanelState(bool enabled);

    bool saveSession(bool showStatus = true);
    bool restoreSession();
    void clearSession();
    void scheduleSessionSave();
    void noteSessionAvailability();
    bool saveBeforeWorkspaceTransition();
    bool openWorkspace(const QString& folderPath);
    bool openWorkspaceFromUserSelection(const QString& folderPath);
    bool switchWorkspace(int index);
    bool closeWorkspace(int index);

signals:
    void sessionSaveFinished(
        const QString& workspaceRoot,
        bool saved);
    void sessionRestoreFinished(
        const QString& workspaceRoot,
        bool restored);
    void sessionClearFinished(
        const QString& workspaceRoot,
        bool cleared);

private slots:
    void handleWorkspaceActivated(
        int index,
        const QString& alias,
        const QString& path);
    void handleWorkspaceClosed();
    void flushScheduledSave();

private:
    QPointer<WorkspaceManager> workspaceManager;
    QPointer<TabManager> tabManager;
    QPointer<PanelLayoutController> panelLayoutController;
    WorkspaceSessionUiBridge uiBridge;
    WorkspaceSessionStateService stateService;
    QTimer* saveTimer = nullptr;
    QSet<QString> cleanWorkspaceRoots;
    QString scheduledWorkspaceRoot;
    QString activationGuardWorkspaceRoot;
    std::uint64_t activationGuardGeneration = 0;
    bool restoreOnActivation = true;
    bool rememberPanelState = true;

    WorkspaceSessionState captureSessionState() const;
    void cancelScheduledSave();
    void showStatus(const QString& message, int timeoutMs) const;
};

#endif // WORKSPACESESSIONCOORDINATOR_H
