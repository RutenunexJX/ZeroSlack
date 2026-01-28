#include "workspacemanager.h"
#include <QFileDialog>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
//#include <QDebug>

WorkspaceManager::WorkspaceManager(QObject *parent)
    : QObject(parent)
{
    // Reserve space for file lists
    allFiles.reserve(500);
    svFiles.reserve(100);

}

WorkspaceManager::~WorkspaceManager()
{
}

bool WorkspaceManager::openWorkspace(const QString& folderPath)
{
    QString pathToOpen = folderPath;

    // If no path provided, show folder dialog
    if (pathToOpen.isEmpty()) {
        pathToOpen = QFileDialog::getExistingDirectory(
            qobject_cast<QWidget*>(parent()),
            "Select Workspace Directory");
        if (pathToOpen.isEmpty()) return false; // User cancelled
    }

    // Close current workspace if open
    if (isWorkspaceOpen()) {
        closeWorkspace();
    }

    workspacePath = pathToOpen;


    // Scan directory
    scanDirectory(workspacePath);

    // Setup file watching
    startFileWatching();

    emit workspaceOpened(workspacePath);
    emit filesScanned(svFiles);

    return true;
}

void WorkspaceManager::closeWorkspace()
{
    if (!isWorkspaceOpen()) return;

    // Stop file watching
    stopFileWatching();

    // Clear data
    workspacePath.clear();
    allFiles.clear();
    svFiles.clear();

    emit workspaceClosed();
}

bool WorkspaceManager::isWorkspaceOpen() const
{
    return !workspacePath.isEmpty();
}

QString WorkspaceManager::getWorkspacePath() const
{
    return workspacePath;
}

QStringList WorkspaceManager::getAllFiles() const
{
    return allFiles;
}

QStringList WorkspaceManager::getSystemVerilogFiles() const
{
    return svFiles;
}

QStringList WorkspaceManager::getFilesByExtension(const QString& extension) const
{
    QStringList filteredFiles;
    const QString lowerExt = extension.toLower();

    filteredFiles.reserve(allFiles.size() / 10);

    for (const QString& filePath : qAsConst(allFiles)) {
        if (QFileInfo(filePath).suffix().toLower() == lowerExt) {
            filteredFiles.append(filePath);
        }
    }
    return filteredFiles;
}

void WorkspaceManager::startFileWatching()
{
    if (!isWorkspaceOpen()) return;

    // Create file watcher if needed
    if (!fileWatcher) {
        fileWatcher = std::make_unique<QFileSystemWatcher>(this);
        connect(fileWatcher.get(), &QFileSystemWatcher::fileChanged,
                this, &WorkspaceManager::onFileChanged);
        connect(fileWatcher.get(), &QFileSystemWatcher::directoryChanged,
                this, &WorkspaceManager::onDirectoryChanged);
    }

    // Clear existing watches
    const QStringList watchedFiles = fileWatcher->files();
    const QStringList watchedDirs = fileWatcher->directories();

    if (!watchedFiles.isEmpty()) {
        fileWatcher->removePaths(watchedFiles);
    }
    if (!watchedDirs.isEmpty()) {
        fileWatcher->removePaths(watchedDirs);
    }

    // Add workspace files and directory to watcher
    if (!allFiles.isEmpty()) {
        fileWatcher->addPaths(allFiles);
    }
    fileWatcher->addPath(workspacePath);
}

void WorkspaceManager::stopFileWatching()
{
    if (!fileWatcher) return;

    const QStringList watchedFiles = fileWatcher->files();
    const QStringList watchedDirs = fileWatcher->directories();

    if (!watchedFiles.isEmpty()) {
        fileWatcher->removePaths(watchedFiles);
    }
    if (!watchedDirs.isEmpty()) {
        fileWatcher->removePaths(watchedDirs);
    }
}

void WorkspaceManager::onFileChanged(const QString& path)
{
    if (!isSystemVerilogFile(path)) return;

    emit fileChanged(path);
}

void WorkspaceManager::onDirectoryChanged(const QString& path)
{
    if (path != workspacePath) return;

    // Rescan directory
    QStringList oldFiles = allFiles;
    scanDirectory(workspacePath);

    // Only update if changed
    if (allFiles != oldFiles) {
        updateFileWatcher();
        emit filesScanned(svFiles);
    }

    emit directoryChanged(path);
}

void WorkspaceManager::scanDirectory(const QString& path)
{
    allFiles.clear();
    allFiles.reserve(500);

    QDirIterator iterator(path, QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        allFiles.append(iterator.next());
    }

    // Filter SystemVerilog files
    filterSystemVerilogFiles();
}

void WorkspaceManager::updateFileWatcher()
{
    if (!fileWatcher || !isWorkspaceOpen()) return;

    // Remove old file watches (keep directory watches)
    const QStringList watchedFiles = fileWatcher->files();
    if (!watchedFiles.isEmpty()) {
        fileWatcher->removePaths(watchedFiles);
    }

    // Add new files to watcher
    if (!allFiles.isEmpty()) {
        fileWatcher->addPaths(allFiles);
    }
}

bool WorkspaceManager::isSystemVerilogFile(const QString& fileName) const
{
    if (fileName.isEmpty()) return false;

    static const QStringList svExtensions = {"sv", "v", "vh", "svh", "vp", "svp"};
    const QString suffix = QFileInfo(fileName).suffix().toLower();
    return svExtensions.contains(suffix);
}

void WorkspaceManager::filterSystemVerilogFiles()
{
    svFiles.clear();
    svFiles.reserve(allFiles.size() / 10);

    for (const QString& filePath : qAsConst(allFiles)) {
        if (isSystemVerilogFile(filePath)) {
            svFiles.append(filePath);
        }
    }
}
