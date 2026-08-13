#include "workspacesessioncoordinator.h"

#include "panellayoutcontroller.h"
#include "tabmanager.h"
#include "workspacemanager.h"

#include <QDir>
#include <QFileInfo>
#include <QTimer>

#include <utility>

namespace {
QString workspaceRootKey(const QString& path)
{
    if (path.isEmpty())
        return QString();
    QString normalized = QDir::cleanPath(
        QDir::fromNativeSeparators(
            QFileInfo(path).absoluteFilePath()));
#ifdef Q_OS_WIN
    normalized = normalized.toCaseFolded();
#endif
    return normalized;
}
}

WorkspaceSessionCoordinator::WorkspaceSessionCoordinator(
    WorkspaceManager* workspaceManager,
    TabManager* tabManager,
    PanelLayoutController* panelLayoutController,
    WorkspaceSessionUiBridge uiBridge,
    const QString& settingsFilePath,
    int saveDelayMs,
    QObject* parent)
    : QObject(parent)
    , workspaceManager(workspaceManager)
    , tabManager(tabManager)
    , panelLayoutController(panelLayoutController)
    , uiBridge(std::move(uiBridge))
    , stateService(settingsFilePath)
{
    saveTimer = new QTimer(this);
    saveTimer->setSingleShot(true);
    saveTimer->setInterval(qMax(1, saveDelayMs));
    connect(saveTimer,
            &QTimer::timeout,
            this,
            &WorkspaceSessionCoordinator::flushScheduledSave,
            Qt::UniqueConnection);

    if (this->tabManager) {
        connect(this->tabManager,
                &TabManager::workspaceSessionStateChanged,
                this,
                &WorkspaceSessionCoordinator::scheduleSessionSave,
                Qt::UniqueConnection);
    }
    if (this->workspaceManager) {
        connect(this->workspaceManager,
                &WorkspaceManager::workspaceActivated,
                this,
                &WorkspaceSessionCoordinator::handleWorkspaceActivated,
                Qt::UniqueConnection);
        connect(this->workspaceManager,
                &WorkspaceManager::workspaceClosed,
                this,
                &WorkspaceSessionCoordinator::handleWorkspaceClosed,
                Qt::UniqueConnection);
    }
    if (this->panelLayoutController) {
        QPointer<WorkspaceSessionCoordinator> self(this);
        this->panelLayoutController->setStateChangedHandler(
            [self]() {
                if (self)
                    self->scheduleSessionSave();
            });
    }
}

WorkspaceSessionCoordinator::~WorkspaceSessionCoordinator()
{
    cancelScheduledSave();
    if (panelLayoutController)
        panelLayoutController->setStateChangedHandler({});
}

void WorkspaceSessionCoordinator::setRestoreOnActivation(bool enabled)
{
    restoreOnActivation = enabled;
}

void WorkspaceSessionCoordinator::setRememberPanelState(bool enabled)
{
    rememberPanelState = enabled;
}

WorkspaceSessionState
WorkspaceSessionCoordinator::captureSessionState() const
{
    WorkspaceSessionState state;
    if (!workspaceManager || !workspaceManager->isWorkspaceOpen())
        return state;

    const QString workspaceRoot =
        workspaceManager->getWorkspacePath();
    state.workspaceRoot = workspaceRoot;
    if (tabManager) {
        state.tabs =
            tabManager->workspaceSessionTabs(workspaceRoot);
    }
    if (uiBridge.captureUiState) {
        state.ui =
            uiBridge.captureUiState(rememberPanelState);
    }
    if (tabManager) {
        state.ui.tabGroupingMode =
            tabGroupingModeStableId(
                tabManager->tabGroupingMode());
    }

    const QList<WorkspaceManager::WorkspaceEntry> entries =
        workspaceManager->workspaceEntries();
    const int activeIndex =
        workspaceManager->activeWorkspaceIndex();
    if (activeIndex >= 0 && activeIndex < entries.size()) {
        const WorkspaceManager::WorkspaceEntry& entry =
            entries.at(activeIndex);
        if (workspaceRootKey(entry.path)
            == workspaceRootKey(workspaceRoot)) {
            state.scannedFiles = entry.scannedFiles;
            state.scanComplete = entry.scanComplete;
        }
    }
    if (state.scannedFiles.isEmpty()) {
        state.scannedFiles =
            workspaceManager->projectSnapshot().allFiles;
    }
    return state;
}

bool WorkspaceSessionCoordinator::saveSession(bool showStatusMessage)
{
    if (!workspaceManager || !workspaceManager->isWorkspaceOpen()) {
        if (showStatusMessage) {
            showStatus(
                QStringLiteral(
                    "Open a workspace before saving a session"),
                3000);
        }
        emit sessionSaveFinished(QString(), false);
        return false;
    }

    const QString workspaceRoot =
        workspaceManager->getWorkspacePath();
    if (workspaceRootKey(scheduledWorkspaceRoot)
        == workspaceRootKey(workspaceRoot)) {
        cancelScheduledSave();
    }

    const QString rootKey = workspaceRootKey(workspaceRoot);
    if (cleanWorkspaceRoots.contains(rootKey)) {
        if (!showStatusMessage)
            return false;
        cleanWorkspaceRoots.remove(rootKey);
    }

    const WorkspaceSessionSaveResult result =
        stateService.save(captureSessionState());
    if (showStatusMessage) {
        showStatus(
            result.saved
                ? QStringLiteral(
                      "Local workspace session saved to %1; "
                      ".zeroslack/project.json is unchanged")
                      .arg(QDir::toNativeSeparators(
                          result.storagePath))
                : result.message,
            result.saved ? 3000 : 5000);
    }
    emit sessionSaveFinished(workspaceRoot, result.saved);
    return result.saved;
}

bool WorkspaceSessionCoordinator::restoreSession()
{
    if (!workspaceManager || !workspaceManager->isWorkspaceOpen()) {
        showStatus(
            QStringLiteral(
                "Open a workspace before restoring a session"),
            3000);
        emit sessionRestoreFinished(QString(), false);
        return false;
    }

    cancelScheduledSave();
    const QString workspaceRoot =
        workspaceManager->getWorkspacePath();
    const QString rootKey = workspaceRootKey(workspaceRoot);
    if (cleanWorkspaceRoots.contains(rootKey)) {
        showStatus(
            QStringLiteral(
                "Workspace session ignored for this activation"),
            3000);
        emit sessionRestoreFinished(workspaceRoot, false);
        return false;
    }

    WorkspaceSessionRestoreResult result =
        stateService.load(workspaceRoot);
    bool importedLegacy = false;
    if (!result.loaded
        && WorkspaceSessionStateService::legacySessionFileExists(
            workspaceRoot)) {
        const WorkspaceLegacyImportResult legacy =
            stateService.loadLegacy(workspaceRoot);
        if (legacy.loaded) {
            result.loaded = true;
            result.state = legacy.state;
            result.skippedTabs = legacy.skippedTabs;
            result.skippedScannedFiles =
                legacy.skippedScannedFiles;
            result.message = legacy.message;
            importedLegacy = true;
            stateService.save(legacy.state);
        } else {
            result.message = legacy.message;
        }
    }
    if (!result.loaded) {
        showStatus(result.message, 5000);
        emit sessionRestoreFinished(workspaceRoot, false);
        return false;
    }

    const bool scanRestored =
        workspaceManager->restoreSessionScanState(
            result.state.scannedFiles,
            result.state.scanComplete);

    QStringList skippedTabs = result.skippedTabs;
    QStringList tabRestoreSkips;
    const QStringList restoredTabs =
        tabManager
        ? tabManager->restoreWorkspaceSessionTabs(
              workspaceRoot,
              result.state.tabs,
              &tabRestoreSkips)
        : QStringList();
    if (tabManager) {
        tabManager->setTabGroupingMode(
            tabGroupingModeFromStableId(
                result.state.ui.tabGroupingMode));
    }
    skippedTabs.append(tabRestoreSkips);
    skippedTabs.removeDuplicates();

    WorkspaceSessionUiRestoreResult uiResult;
    if (uiBridge.restoreUiState) {
        uiResult = uiBridge.restoreUiState(
            result.state.ui,
            rememberPanelState);
    }

    QStringList notes;
    if (!scanRestored)
        notes.append(QStringLiteral("scan list skipped"));
    if (!uiResult.geometryRestored
        || !uiResult.dockStateRestored) {
        notes.append(QStringLiteral("layout fallback used"));
    }
    if (!skippedTabs.isEmpty()) {
        notes.append(
            QStringLiteral("%1 tab(s) skipped")
                .arg(skippedTabs.size()));
    }
    if (!result.skippedScannedFiles.isEmpty()) {
        notes.append(
            QStringLiteral("%1 scanned file(s) skipped")
                .arg(result.skippedScannedFiles.size()));
    }
    if (importedLegacy) {
        notes.append(
            QStringLiteral("legacy .zs imported read-only"));
    }

    QString message =
        QStringLiteral(
            "Local workspace session restored: "
            "%1 tab(s), %2 scanned file(s); "
            "portable project configuration unchanged")
            .arg(restoredTabs.size())
            .arg(result.state.scannedFiles.size());
    if (!notes.isEmpty()) {
        message += QStringLiteral(" (%1)")
                       .arg(notes.join(QStringLiteral("; ")));
    }
    showStatus(message, notes.isEmpty() ? 4000 : 7000);
    scheduleSessionSave();

    const bool restored =
        scanRestored && uiResult.dockStateRestored;
    emit sessionRestoreFinished(workspaceRoot, restored);
    return restored;
}

void WorkspaceSessionCoordinator::clearSession()
{
    if (!workspaceManager || !workspaceManager->isWorkspaceOpen()) {
        showStatus(
            QStringLiteral(
                "Open a workspace before ignoring a session"),
            3000);
        emit sessionClearFinished(QString(), false);
        return;
    }

    const QString workspaceRoot =
        workspaceManager->getWorkspacePath();
    cancelScheduledSave();
    const bool cleared = stateService.clear(workspaceRoot);
    cleanWorkspaceRoots.insert(workspaceRootKey(workspaceRoot));
    showStatus(
        cleared
            ? QStringLiteral(
                  "Local workspace session cleared; "
                  "portable project configuration and "
                  "legacy .zs are unchanged")
            : QStringLiteral(
                  "Failed to clear local workspace session; "
                  "portable project configuration is unchanged"),
        cleared ? 4000 : 5000);
    emit sessionClearFinished(workspaceRoot, cleared);
}

void WorkspaceSessionCoordinator::scheduleSessionSave()
{
    if (!workspaceManager || !workspaceManager->isWorkspaceOpen())
        return;
    const QString workspaceRoot =
        workspaceManager->getWorkspacePath();
    if (workspaceRootKey(activationGuardWorkspaceRoot)
        == workspaceRootKey(workspaceRoot)) {
        return;
    }
    if (cleanWorkspaceRoots.contains(
            workspaceRootKey(workspaceRoot))) {
        return;
    }

    scheduledWorkspaceRoot = workspaceRoot;
    saveTimer->start();
}

void WorkspaceSessionCoordinator::noteSessionAvailability()
{
    if (!workspaceManager || !workspaceManager->isWorkspaceOpen())
        return;

    const QString workspaceRoot =
        workspaceManager->getWorkspacePath();
    if (cleanWorkspaceRoots.contains(
            workspaceRootKey(workspaceRoot))) {
        return;
    }
    if (stateService.sessionExists(workspaceRoot)) {
        showStatus(
            QStringLiteral(
                "Local workspace session available: "
                "use ow s restore; project configuration "
                "loads separately"),
            5000);
    } else if (WorkspaceSessionStateService::legacySessionFileExists(
                   workspaceRoot)) {
        showStatus(
            QStringLiteral(
                "Legacy .zs detected: ow s restore imports "
                "local state read-only; project settings "
                "load separately"),
            6000);
    }
}

bool WorkspaceSessionCoordinator::saveBeforeWorkspaceTransition()
{
    cancelScheduledSave();
    return saveSession(false);
}

bool WorkspaceSessionCoordinator::openWorkspace(
    const QString& folderPath)
{
    if (!workspaceManager || folderPath.isEmpty())
        return false;
    if (workspaceManager->isWorkspaceOpen())
        saveBeforeWorkspaceTransition();
    return workspaceManager->openWorkspace(folderPath);
}

bool WorkspaceSessionCoordinator::openWorkspaceFromUserSelection(
    const QString& folderPath)
{
    if (!workspaceManager || folderPath.isEmpty())
        return false;
    if (workspaceManager->isWorkspaceOpen())
        saveBeforeWorkspaceTransition();
    return workspaceManager->openWorkspaceFromUserSelection(folderPath);
}

bool WorkspaceSessionCoordinator::switchWorkspace(int index)
{
    if (!workspaceManager
        || index < 0
        || index >= workspaceManager->workspaceEntries().size()) {
        return false;
    }
    if (index == workspaceManager->activeWorkspaceIndex())
        return true;
    saveBeforeWorkspaceTransition();
    return workspaceManager->switchWorkspace(index);
}

bool WorkspaceSessionCoordinator::closeWorkspace(int index)
{
    if (!workspaceManager || !tabManager)
        return false;
    const QList<WorkspaceManager::WorkspaceEntry> entries =
        workspaceManager->workspaceEntries();
    if (index < 0 || index >= entries.size())
        return false;

    if (index == workspaceManager->activeWorkspaceIndex())
        saveBeforeWorkspaceTransition();
    if (!tabManager->closeTabsInWorkspace(entries.at(index).path))
        return false;
    return workspaceManager->closeWorkspace(index);
}

void WorkspaceSessionCoordinator::handleWorkspaceActivated(
    int,
    const QString&,
    const QString& path)
{
    cancelScheduledSave();
    activationGuardWorkspaceRoot = path;
    const std::uint64_t guardGeneration =
        ++activationGuardGeneration;
    QTimer::singleShot(
        0,
        this,
        [this, guardGeneration]() {
            if (activationGuardGeneration
                == guardGeneration) {
                activationGuardWorkspaceRoot.clear();
            }
        });
    if (restoreOnActivation)
        restoreSession();
    else
        noteSessionAvailability();
}

void WorkspaceSessionCoordinator::handleWorkspaceClosed()
{
    cancelScheduledSave();
    ++activationGuardGeneration;
    activationGuardWorkspaceRoot.clear();
}

void WorkspaceSessionCoordinator::flushScheduledSave()
{
    const QString scheduledRoot = scheduledWorkspaceRoot;
    scheduledWorkspaceRoot.clear();
    if (!workspaceManager || !workspaceManager->isWorkspaceOpen())
        return;
    if (workspaceRootKey(workspaceManager->getWorkspacePath())
        != workspaceRootKey(scheduledRoot)) {
        return;
    }
    saveSession(false);
}

void WorkspaceSessionCoordinator::cancelScheduledSave()
{
    if (saveTimer)
        saveTimer->stop();
    scheduledWorkspaceRoot.clear();
}

void WorkspaceSessionCoordinator::showStatus(
    const QString& message,
    int timeoutMs) const
{
    if (uiBridge.showStatus && !message.isEmpty())
        uiBridge.showStatus(message, timeoutMs);
}
