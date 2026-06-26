#ifndef WORKSPACEMANAGER_H
#define WORKSPACEMANAGER_H

#include "projectmodel.h"

#include <QObject>
#include <QFileSystemWatcher>
#include <QList>
#include <QStringList>
#include <memory>

class QDirIterator;
class QTimer;

class WorkspaceManager : public QObject
{
    Q_OBJECT

public:
    struct WorkspaceEntry {
        QString alias;
        QString path;
    };

    explicit WorkspaceManager(QObject *parent = nullptr);
    ~WorkspaceManager();

    // Workspace operations
    bool openWorkspace(const QString& folderPath);
    void closeWorkspace();
    bool closeWorkspace(int index);
    bool isWorkspaceOpen() const;
    QString getWorkspacePath() const;
    QString getWorkspaceAlias() const;
    QList<WorkspaceEntry> workspaceEntries() const;
    QList<WorkspaceEntry> recentWorkspaceEntries() const;
    int activeWorkspaceIndex() const;
    ProjectModel* getProjectModel() const;
    ProjectSnapshot projectSnapshot() const;
    bool switchWorkspace(int index);

    // File management
    QStringList getAllFiles() const;
    QStringList getSystemVerilogFiles() const;
    QStringList getFilesByExtension(const QString& extension) const;
    QString resolveIncludePath(const QString& includePath,
                               const QString& currentFile = QString()) const;

    // File watching
    void startFileWatching();
    void stopFileWatching();

signals:
    void workspaceOpened(const QString& path);
    void workspaceClosed();
    void workspaceListChanged();
    void workspaceActivated(int index, const QString& alias, const QString& path);
    void fileChanged(const QString& filePath);
    void directoryChanged(const QString& dirPath);
    void filesScanned(const QStringList& svFiles);
    void workspaceScanStarted(const QString& path);
    void workspaceScanProgress(const QString& path, int filesFound);
    void workspaceScanFinished(const QString& path,
                               int totalFiles,
                               int systemVerilogFiles);
    void projectChanged(const ProjectSnapshot& snapshot);

private slots:
    void onFileChanged(const QString& path);
    void onDirectoryChanged(const QString& path);
    void processDirectoryScanChunk();

private:
    struct WorkspaceFiles {
        QStringList allFiles;
        QStringList systemVerilogFiles;

        void reserveDefaults();
        void clear();
        void setScannedFiles(ProjectModel* projectModel,
                             const QStringList& scannedFiles);
        QStringList filesByExtension(const QString& extension) const;
    };

    struct WorkspaceWatcher {
        std::unique_ptr<QFileSystemWatcher> watcher;

        void ensure(WorkspaceManager* owner);
        void clear();
        void watchWorkspace(const QString& workspacePath,
                            const QStringList& files);
        void updateFiles(const QStringList& files);
        bool active() const;
    };

    QString workspacePath;
    QString workspaceAlias;
    QList<WorkspaceEntry> workspaces;
    QList<WorkspaceEntry> recentWorkspaces;
    int activeIndex = -1;
    WorkspaceFiles files;
    WorkspaceWatcher watcher;
    std::unique_ptr<ProjectModel> projectModel;
    std::unique_ptr<QDirIterator> scanIterator;
    QStringList pendingScannedFiles;
    QString scanningPath;
    QTimer* scanTimer = nullptr;

    // Helper methods
    void startDirectoryScan(const QString& path);
    void finishDirectoryScan();
    void cancelDirectoryScan();
    void updateFileWatcher();
    bool activateWorkspacePath(const QString& path,
                               const QString& alias,
                               int index);
    void loadRecentWorkspaces();
    void saveRecentWorkspaces() const;
    void rememberRecentWorkspace(const WorkspaceEntry& entry);
    QString promptWorkspaceAlias(const QString& path) const;
    QString defaultWorkspaceAlias(const QString& path) const;
    int workspaceIndexForPath(const QString& path) const;
    QString normalizeWorkspacePath(const QString& path) const;
    bool isSystemVerilogFile(const QString& fileName) const;
};

Q_DECLARE_METATYPE(WorkspaceManager::WorkspaceEntry)

#endif // WORKSPACEMANAGER_H
