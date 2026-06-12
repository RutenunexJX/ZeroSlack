#ifndef WORKSPACEMANAGER_H
#define WORKSPACEMANAGER_H

#include "projectmodel.h"

#include <QObject>
#include <QFileSystemWatcher>
#include <QStringList>
#include <memory>

class WorkspaceManager : public QObject
{
    Q_OBJECT

public:
    explicit WorkspaceManager(QObject *parent = nullptr);
    ~WorkspaceManager();

    // Workspace operations
    bool openWorkspace(const QString& folderPath);
    void closeWorkspace();
    bool isWorkspaceOpen() const;
    QString getWorkspacePath() const;
    ProjectModel* getProjectModel() const;
    ProjectSnapshot projectSnapshot() const;

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
    void fileChanged(const QString& filePath);
    void directoryChanged(const QString& dirPath);
    void filesScanned(const QStringList& svFiles);
    void projectChanged(const ProjectSnapshot& snapshot);

private slots:
    void onFileChanged(const QString& path);
    void onDirectoryChanged(const QString& path);

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
    WorkspaceFiles files;
    WorkspaceWatcher watcher;
    std::unique_ptr<ProjectModel> projectModel;

    // Helper methods
    void scanDirectory(const QString& path);
    void updateFileWatcher();
    bool isSystemVerilogFile(const QString& fileName) const;
};

#endif // WORKSPACEMANAGER_H
