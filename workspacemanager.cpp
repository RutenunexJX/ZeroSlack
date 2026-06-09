#include "workspacemanager.h"
#include <QFileDialog>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <utility>

WorkspaceManager::WorkspaceManager(QObject *parent)
    : QObject(parent)
    , projectModel(std::make_unique<ProjectModel>(this))
{
    allFiles.reserve(500);
    svFiles.reserve(100);

    connect(projectModel.get(), &ProjectModel::projectChanged,
            this, &WorkspaceManager::projectChanged);
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

    projectModel->setWorkspaceRoot(pathToOpen);
    workspacePath = projectModel->workspaceRoot();
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
    projectModel->closeProject();

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

ProjectModel* WorkspaceManager::getProjectModel() const
{
    return projectModel.get();
}

ProjectSnapshot WorkspaceManager::projectSnapshot() const
{
    return projectModel ? projectModel->snapshot() : ProjectSnapshot();
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

QString WorkspaceManager::resolveIncludePath(const QString& includePath,
                                             const QString& currentFile) const
{
    if (includePath.isEmpty())
        return QString();

    if (!currentFile.isEmpty()) {
        const QString currentFileCandidate =
            QFileInfo(currentFile).dir().absoluteFilePath(includePath);
        if (QFileInfo::exists(currentFileCandidate))
            return currentFileCandidate;
    }

    if (!isWorkspaceOpen())
        return QString();

    const QString candidate = QDir(workspacePath).absoluteFilePath(includePath);
    if (QFileInfo::exists(candidate))
        return candidate;

    const QString includeFileName = QFileInfo(includePath).fileName();
    for (const QString& filePath : allFiles) {
        if (QFileInfo(filePath).fileName() == includeFileName)
            return filePath;
    }
    return QString();
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

    projectModel->setScannedFiles(allFiles);
    allFiles = projectModel->allFiles();
    svFiles = projectModel->systemVerilogFiles();
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
