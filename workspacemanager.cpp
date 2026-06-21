#include "workspacemanager.h"
#include "activitylogservice.h"
#include <QFileDialog>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QElapsedTimer>
#include <utility>

WorkspaceManager::WorkspaceManager(QObject *parent)
    : QObject(parent)
    , projectModel(std::make_unique<ProjectModel>(this))
{
    files.reserveDefaults();

    connect(projectModel.get(), &ProjectModel::projectChanged,
            this, &WorkspaceManager::projectChanged);
}

void WorkspaceManager::WorkspaceFiles::reserveDefaults()
{
    allFiles.reserve(500);
    systemVerilogFiles.reserve(100);
}

void WorkspaceManager::WorkspaceFiles::clear()
{
    allFiles.clear();
    systemVerilogFiles.clear();
}

void WorkspaceManager::WorkspaceFiles::setScannedFiles(
    ProjectModel* projectModel,
    const QStringList& scannedFiles)
{
    if (!projectModel) {
        clear();
        return;
    }

    projectModel->setScannedFiles(scannedFiles);
    allFiles = projectModel->allFiles();
    systemVerilogFiles = projectModel->systemVerilogFiles();
}

QStringList WorkspaceManager::WorkspaceFiles::filesByExtension(
    const QString& extension) const
{
    QStringList filteredFiles;
    const QString lowerExt = extension.toLower();

    filteredFiles.reserve(allFiles.size() / 10);

    for (const QString& filePath : std::as_const(allFiles)) {
        if (QFileInfo(filePath).suffix().toLower() == lowerExt)
            filteredFiles.append(filePath);
    }
    return filteredFiles;
}

void WorkspaceManager::WorkspaceWatcher::ensure(WorkspaceManager* owner)
{
    if (watcher)
        return;

    watcher = std::make_unique<QFileSystemWatcher>(owner);
    QObject::connect(watcher.get(), &QFileSystemWatcher::fileChanged,
                     owner, &WorkspaceManager::onFileChanged);
    QObject::connect(watcher.get(), &QFileSystemWatcher::directoryChanged,
                     owner, &WorkspaceManager::onDirectoryChanged);
}

void WorkspaceManager::WorkspaceWatcher::clear()
{
    if (!watcher)
        return;

    const QStringList watchedFiles = watcher->files();
    const QStringList watchedDirs = watcher->directories();

    if (!watchedFiles.isEmpty())
        watcher->removePaths(watchedFiles);
    if (!watchedDirs.isEmpty())
        watcher->removePaths(watchedDirs);
}

void WorkspaceManager::WorkspaceWatcher::watchWorkspace(
    const QString& workspacePath,
    const QStringList& files)
{
    if (!watcher)
        return;

    clear();

    if (!files.isEmpty())
        watcher->addPaths(files);
    watcher->addPath(workspacePath);
}

void WorkspaceManager::WorkspaceWatcher::updateFiles(const QStringList& files)
{
    if (!watcher)
        return;

    const QStringList watchedFiles = watcher->files();
    if (!watchedFiles.isEmpty())
        watcher->removePaths(watchedFiles);

    if (!files.isEmpty())
        watcher->addPaths(files);
}

bool WorkspaceManager::WorkspaceWatcher::active() const
{
    return watcher != nullptr;
}

WorkspaceManager::~WorkspaceManager()
{
}

bool WorkspaceManager::openWorkspace(const QString& folderPath)
{
    QElapsedTimer timer;
    timer.start();

    QString pathToOpen = folderPath;
    if (pathToOpen.isEmpty()) {
        pathToOpen = QFileDialog::getExistingDirectory(
            qobject_cast<QWidget*>(parent()),
            "Select Workspace Directory");
        if (pathToOpen.isEmpty()) return false; // User cancelled
    }

    ActivityLogService::getInstance()->append(
        QStringLiteral("Workspace"),
        ActivityLogLevel::Info,
        QStringLiteral("Opening %1").arg(QDir::toNativeSeparators(pathToOpen)));

    if (isWorkspaceOpen()) {
        closeWorkspace();
    }

    projectModel->setWorkspaceRoot(pathToOpen);
    workspacePath = projectModel->workspaceRoot();
    scanDirectory(workspacePath);
    startFileWatching();

    emit workspaceOpened(workspacePath);
    emit filesScanned(files.systemVerilogFiles);

    ActivityLogService::getInstance()->append(
        QStringLiteral("Workspace"),
        ActivityLogLevel::Info,
        QStringLiteral("Opened %1 (%2 SystemVerilog files)")
            .arg(QDir::toNativeSeparators(workspacePath))
            .arg(files.systemVerilogFiles.size()),
        static_cast<int>(timer.elapsed()));

    return true;
}

void WorkspaceManager::closeWorkspace()
{
    if (!isWorkspaceOpen()) return;
    const QString closingPath = workspacePath;
    stopFileWatching();
    workspacePath.clear();
    files.clear();
    projectModel->closeProject();

    emit workspaceClosed();
    ActivityLogService::getInstance()->append(
        QStringLiteral("Workspace"),
        ActivityLogLevel::Info,
        QStringLiteral("Closed %1").arg(QDir::toNativeSeparators(closingPath)));
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
    return files.allFiles;
}

QStringList WorkspaceManager::getSystemVerilogFiles() const
{
    return files.systemVerilogFiles;
}

QStringList WorkspaceManager::getFilesByExtension(const QString& extension) const
{
    return files.filesByExtension(extension);
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
    for (const QString& filePath : files.allFiles) {
        if (QFileInfo(filePath).fileName() == includeFileName)
            return filePath;
    }
    return QString();
}

void WorkspaceManager::startFileWatching()
{
    if (!isWorkspaceOpen()) return;

    watcher.ensure(this);
    watcher.watchWorkspace(workspacePath, files.allFiles);
}

void WorkspaceManager::stopFileWatching()
{
    watcher.clear();
}

void WorkspaceManager::onFileChanged(const QString& path)
{
    if (!isSystemVerilogFile(path)) return;

    emit fileChanged(path);
}

void WorkspaceManager::onDirectoryChanged(const QString& path)
{
    if (path != workspacePath) return;

    QStringList oldFiles = files.allFiles;
    scanDirectory(workspacePath);
    if (files.allFiles != oldFiles) {
        updateFileWatcher();
        emit filesScanned(files.systemVerilogFiles);
    }

    emit directoryChanged(path);
}

void WorkspaceManager::scanDirectory(const QString& path)
{
    QStringList scannedFiles;
    scannedFiles.reserve(500);

    QDirIterator iterator(path, QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        scannedFiles.append(iterator.next());
    }

    files.setScannedFiles(projectModel.get(), scannedFiles);
}

void WorkspaceManager::updateFileWatcher()
{
    if (!watcher.active() || !isWorkspaceOpen()) return;

    watcher.updateFiles(files.allFiles);
}

bool WorkspaceManager::isSystemVerilogFile(const QString& fileName) const
{
    if (fileName.isEmpty()) return false;

    static const QStringList svExtensions = {"sv", "v", "vh", "svh", "vp", "svp"};
    const QString suffix = QFileInfo(fileName).suffix().toLower();
    return svExtensions.contains(suffix);
}
