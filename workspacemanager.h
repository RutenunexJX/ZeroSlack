#ifndef WORKSPACEMANAGER_H
#define WORKSPACEMANAGER_H

#include "zeroslackexport.h"

#include "projectmodel.h"
#include "workspaceconfigurationservice.h"

#include <QObject>
#include <QFileSystemWatcher>
#include <QHash>
#include <QList>
#include <QStringList>
#include <cstdint>
#include <functional>
#include <memory>

class QDirIterator;
class QTimer;
class QWidget;

class ZEROSLACK_API WorkspaceManager : public QObject
{
    Q_OBJECT

public:
    using WorkspaceAliasSelector =
        std::function<QString(QWidget* dialogParent,
                              const QString& suggestedAlias)>;

    struct WorkspaceEntry {
        QString alias;
        QString path;
        QStringList scannedFiles;
        QStringList ignoredDirectories;
        QStringList includeDirs;
        QHash<QString, QString> defines;
        QStringList fileExtensions;
        QString topModule;
        QList<WorkspaceVirtualSourceGroup>
            virtualSourceGroups;
        bool scanComplete = false;
    };

    explicit WorkspaceManager(QObject *parent = nullptr);
    ~WorkspaceManager();

    // Workspace operations
    bool openWorkspace(const QString& folderPath);
    bool openWorkspaceFromUserSelection(const QString& folderPath);
    void setWorkspaceAliasSelector(WorkspaceAliasSelector selector);
    // Prevent integration tests from mutating the user's global recent list.
    void setRecentWorkspacePersistenceEnabledForTesting(bool enabled);
    void closeWorkspace();
    bool closeWorkspace(int index);
    bool renameWorkspaceAlias(int index,
                              const QString& alias,
                              QString* errorMessage = nullptr);
    bool isWorkspaceOpen() const;
    QString getWorkspacePath() const;
    QString getWorkspaceAlias() const;
    QList<WorkspaceEntry> workspaceEntries() const;
    QList<WorkspaceEntry> recentWorkspaceEntries() const;
    int activeWorkspaceIndex() const;
    QStringList ignoredDirectories() const;
    WorkspaceConfiguration workspaceConfiguration() const;
    QList<WorkspaceVirtualSourceGroup>
    virtualSourceGroups() const;
    bool setVirtualSourceGroups(
        const QList<WorkspaceVirtualSourceGroup>& groups,
        QString* errorMessage = nullptr);
    bool setIgnoredDirectories(const QStringList& directories,
                               QString* errorMessage = nullptr);
    bool setWorkspaceConfiguration(
        const WorkspaceConfiguration& configuration,
        QString* errorMessage = nullptr);
    bool restoreSessionScanState(const QStringList& scannedFiles,
                                 bool scanComplete);
    ProjectModel* getProjectModel() const;
    ProjectSnapshot projectSnapshot() const;
    std::uint64_t projectSnapshotMaterializationCountForTesting() const;
    void resetProjectSnapshotMaterializationCountForTesting();
    bool switchWorkspace(int index);

    // File management
    QStringList getAllFiles() const;
    QStringList getSystemVerilogFiles() const;
    void refreshWorkspaceFiles();
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
    };

    struct WorkspaceWatcher {
        struct EntryStamp {
            bool directory = false;
        };

        struct DirectoryDelta {
            bool membershipChanged = false;
        };

        std::unique_ptr<QFileSystemWatcher> watcher;
        WorkspaceManager* owner = nullptr;
        QHash<QString, QHash<QString, EntryStamp>> directorySnapshots;

        void ensure(WorkspaceManager* owner);
        void clear();
        void watchWorkspace(const QString& workspacePath,
                            const QStringList& files,
                            const QStringList& directories);
        void updatePaths(const QString& workspacePath,
                         const QStringList& files,
                         const QStringList& directories);
        DirectoryDelta refreshDirectory(const QString& path);
        void rewatchFile(const QString& path);
        bool active() const;

    private:
        QHash<QString, EntryStamp> snapshotDirectory(
            const QString& path) const;
    };

    QString workspacePath;
    QString workspaceAlias;
    QList<WorkspaceEntry> workspaces;
    QList<WorkspaceEntry> recentWorkspaces;
    int activeIndex = -1;
    WorkspaceFiles files;
    WorkspaceWatcher watcher;
    std::unique_ptr<ProjectModel> projectModel;
    std::unique_ptr<WorkspaceConfigurationService> workspaceConfigurationService;
    std::unique_ptr<QDirIterator> scanIterator;
    QStringList pendingScannedFiles;
    QStringList pendingScannedDirectories;
    QStringList scannedDirectories;
    QString scanningPath;
    std::uint64_t workspaceActivationGeneration = 0;
    std::uint64_t scanGeneration = 0;
    mutable std::uint64_t projectSnapshotMaterializationCount = 0;
    QTimer* scanTimer = nullptr;
    WorkspaceAliasSelector workspaceAliasSelector;
    bool recentWorkspacePersistenceEnabled = true;

    // Helper methods
    void startDirectoryScan(const QString& path);
    void finishDirectoryScan(std::uint64_t generation,
                             const QString& path);
    void cancelDirectoryScan();
    bool directoryScanIsCurrent(std::uint64_t generation,
                                const QString& path) const;
    void updateFileWatcher();
    bool activateWorkspacePath(const QString& path,
                               const QString& alias,
                               int index,
                               bool openedNewWorkspace);
    bool restoreWorkspaceFilesFromEntry(int index);
    WorkspaceConfiguration loadConfigurationForWorkspace(
        const QString& path) const;
    bool applyWorkspaceConfiguration(
        const WorkspaceConfiguration& configuration,
        bool persist,
        QString* errorMessage = nullptr,
        bool notify = true);
    void updateActiveEntryConfiguration(
        const WorkspaceConfiguration& configuration);
    void loadRecentWorkspaces();
    void saveRecentWorkspaces() const;
    void rememberRecentWorkspace(const WorkspaceEntry& entry);
    QString promptWorkspaceAlias(const QString& path) const;
    bool openWorkspaceInternal(const QString& folderPath,
                               bool promptForAlias);
    QString defaultWorkspaceAlias(const QString& path) const;
    int workspaceIndexForPath(const QString& path) const;
    QString normalizeWorkspacePath(const QString& path) const;
    bool isSystemVerilogFile(const QString& fileName) const;
};

Q_DECLARE_METATYPE(WorkspaceManager::WorkspaceEntry)

#endif // WORKSPACEMANAGER_H
