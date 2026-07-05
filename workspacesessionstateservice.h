#ifndef WORKSPACESESSIONSTATESERVICE_H
#define WORKSPACESESSIONSTATESERVICE_H

#include "workspaceconfigurationservice.h"

#include <QByteArray>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>

struct WorkspaceSessionTabState {
    QString filePath;
    int cursorLine = 1;
    int cursorColumn = 1;
    int verticalScrollValue = 0;
    bool active = false;
};

struct WorkspaceSessionUiState {
    QByteArray mainWindowGeometry;
    QByteArray mainWindowState;
};

struct WorkspaceSessionState {
    QString workspaceRoot;
    QString originalRoot;
    QString workspaceId;
    QString savedAtUtc;
    WorkspaceConfiguration configuration;
    QList<WorkspaceSessionTabState> tabs;
    WorkspaceSessionUiState ui;
    QStringList scannedFiles;
    bool scanComplete = false;
};

struct WorkspaceSessionSaveResult {
    bool saved = false;
    QString sessionFilePath;
    QString message;
};

struct WorkspaceSessionRestoreResult {
    bool loaded = false;
    QString sessionFilePath;
    WorkspaceSessionState state;
    QStringList skippedTabs;
    QStringList skippedScannedFiles;
    QStringList externalPaths;
    QString message;
};

class WorkspaceSessionStateService
{
public:
    static constexpr int kVersion = 1;

    static QString sessionFilePath(const QString& workspaceRoot);
    static bool sessionFileExists(const QString& workspaceRoot);

    WorkspaceSessionSaveResult save(
        const WorkspaceSessionState& state) const;
    WorkspaceSessionRestoreResult load(
        const QString& workspaceRoot) const;

private:
    static QString normalizePath(const QString& path);
    static bool isInsideRoot(const QString& root, const QString& path);
    static QString relativePath(const QString& root, const QString& path);
    static QString resolveStoredPath(const QString& root,
                                     const QJsonValue& value,
                                     QStringList* externalPaths);
};

#endif // WORKSPACESESSIONSTATESERVICE_H
