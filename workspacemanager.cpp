#include "workspacemanager.h"
#include <QFileDialog>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <utility>

WorkspaceManager::WorkspaceManager(QObject *parent)
    : QObject(parent)
{
    allFiles.reserve(500);
    svFiles.reserve(100);

}

WorkspaceManager::~WorkspaceManager()
{
}

bool WorkspaceManager::openWorkspace(const QString& folderPath)
{
    QString pathToOpen = folderPath;
    if (pathToOpen.isEmpty()) {
        pathToOpen = QFileDialog::getExistingDirectory(
            qobject_cast<QWidget*>(parent()),
            "Select Workspace Directory");
        if (pathToOpen.isEmpty()) return false; // User cancelled
    }

    if (isWorkspaceOpen()) {
        closeWorkspace();
    }

    workspacePath = pathToOpen;
    scanDirectory(workspacePath);
    startFileWatching();

    emit workspaceOpened(workspacePath);
    emit filesScanned(svFiles);

    return true;
}

void WorkspaceManager::closeWorkspace()
{
    if (!isWorkspaceOpen()) return;
    stopFileWatching();
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

    for (const QString& filePath : std::as_const(allFiles)) {
        if (QFileInfo(filePath).suffix().toLower() == lowerExt) {
            filteredFiles.append(filePath);
        }
    }
    return filteredFiles;
}

void WorkspaceManager::startFileWatching()
{
    if (!isWorkspaceOpen()) return;

    if (!fileWatcher) {
        fileWatcher = std::make_unique<QFileSystemWatcher>(this);
        connect(fileWatcher.get(), &QFileSystemWatcher::fileChanged,
                this, &WorkspaceManager::onFileChanged);
        connect(fileWatcher.get(), &QFileSystemWatcher::directoryChanged,
                this, &WorkspaceManager::onDirectoryChanged);
    }

    const QStringList watchedFiles = fileWatcher->files();
    const QStringList watchedDirs = fileWatcher->directories();

    if (!watchedFiles.isEmpty()) {
        fileWatcher->removePaths(watchedFiles);
    }
    if (!watchedDirs.isEmpty()) {
        fileWatcher->removePaths(watchedDirs);
    }

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

    QStringList oldFiles = allFiles;
    scanDirectory(workspacePath);
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

    filterSystemVerilogFiles();
}

void WorkspaceManager::updateFileWatcher()
{
    if (!fileWatcher || !isWorkspaceOpen()) return;

    const QStringList watchedFiles = fileWatcher->files();
    if (!watchedFiles.isEmpty()) {
        fileWatcher->removePaths(watchedFiles);
    }

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

    for (const QString& filePath : std::as_const(allFiles)) {
        if (isSystemVerilogFile(filePath)) {
            svFiles.append(filePath);
        }
    }
}
