#ifndef WORKSPACESESSIONSTATESERVICE_H
#define WORKSPACESESSIONSTATESERVICE_H

#include "contextworkspacestate.h"
#include "panellayoutstate.h"
#include "workspaceconfigurationservice.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include <memory>

class QSettings;

struct WorkspaceSessionTabState {
    QString filePath;
    int cursorLine = 1;
    int cursorColumn = 1;
    int verticalScrollValue = 0;
    int horizontalScrollValue = 0;
    bool active = false;
    QString viewId;
    int groupIndex = 0;
    int tabIndex = 0;
    bool locked = false;
};

struct WorkspaceSessionUiState {
    QByteArray mainWindowGeometry;
    QByteArray mainWindowState;
    QString navigationFilesQuery;
    QString navigationDesignQuery;
    QString tabGroupingMode = QStringLiteral("none");
    PanelLayoutState panelLayout;
    ContextWorkspaceState contextWorkspace;
};

struct WorkspaceSessionState {
    QString workspaceRoot;
    QString originalRoot;
    QString workspaceId;
    QString savedAtUtc;
    QList<WorkspaceSessionTabState> tabs;
    WorkspaceSessionUiState ui;
    QStringList scannedFiles;
    bool scanComplete = false;
};

struct WorkspaceSessionSaveResult {
    bool saved = false;
    QString storagePath;
    QString workspaceIdentity;
    QString message;
};

struct WorkspaceSessionRestoreResult {
    bool loaded = false;
    QString storagePath;
    WorkspaceSessionState state;
    QStringList skippedTabs;
    QStringList skippedScannedFiles;
    QString message;
};

struct WorkspaceLegacyImportResult {
    bool loaded = false;
    QString legacyFilePath;
    WorkspaceSessionState state;
    QStringList skippedTabs;
    QStringList skippedScannedFiles;
    QString message;
};

class WorkspaceSessionStateService
{
public:
    static constexpr int kVersion = 4;

    explicit WorkspaceSessionStateService(
        const QString& settingsFilePath =
            QString());

    static QString workspaceIdentity(
        const QString& workspaceRoot);
    static QString legacySessionFilePath(
        const QString& workspaceRoot);
    static bool legacySessionFileExists(
        const QString& workspaceRoot);

    QString localStoragePath() const;
    bool sessionExists(
        const QString& workspaceRoot) const;
    WorkspaceSessionSaveResult save(
        const WorkspaceSessionState& state) const;
    WorkspaceSessionRestoreResult load(
        const QString& workspaceRoot) const;
    bool clear(const QString& workspaceRoot) const;
    WorkspaceLegacyImportResult loadLegacy(
        const QString& workspaceRoot) const;

private:
    QString settingsFilePath;

    static QString normalizePath(
        const QString& path);
    static bool isInsideRoot(
        const QString& root,
        const QString& path);
    static QString relativePath(
        const QString& root,
        const QString& path);
    static QString settingsGroup(
        const QString& workspaceRoot);
    std::unique_ptr<QSettings> makeSettings() const;
};

#endif // WORKSPACESESSIONSTATESERVICE_H
