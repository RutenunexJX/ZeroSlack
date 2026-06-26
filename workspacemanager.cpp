#include "workspacemanager.h"
#include "activitylogservice.h"
#include <QFileDialog>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QInputDialog>
#include <QLineEdit>
#include <QSettings>
#include <QTimer>
#include <algorithm>
#include <utility>

namespace {
constexpr int kMaxRecentWorkspaces = 20;
constexpr const char* kRecentWorkspaceGroup = "recentWorkspaces";
constexpr const char* kRecentWorkspaceItems = "items";
constexpr const char* kRecentWorkspaceAlias = "alias";
constexpr const char* kRecentWorkspacePath = "path";
}

WorkspaceManager::WorkspaceManager(QObject *parent)
    : QObject(parent)
    , projectModel(std::make_unique<ProjectModel>(this))
{
    qRegisterMetaType<WorkspaceManager::WorkspaceEntry>("WorkspaceManager::WorkspaceEntry");
    files.reserveDefaults();
    loadRecentWorkspaces();

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
    Q_UNUSED(files)
    if (!watcher)
        return;

    clear();

    watcher->addPath(workspacePath);
}

void WorkspaceManager::WorkspaceWatcher::updateFiles(const QStringList& files)
{
    Q_UNUSED(files)
    if (!watcher)
        return;

    const QStringList watchedFiles = watcher->files();
    if (!watchedFiles.isEmpty())
        watcher->removePaths(watchedFiles);
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
    const bool interactive = pathToOpen.isEmpty();
    if (pathToOpen.isEmpty()) {
        pathToOpen = QFileDialog::getExistingDirectory(
            qobject_cast<QWidget*>(parent()),
            "Select Workspace Directory");
        if (pathToOpen.isEmpty()) return false; // User cancelled
    }
    pathToOpen = normalizeWorkspacePath(pathToOpen);
    if (pathToOpen.isEmpty())
        return false;

    const int existingIndex = workspaceIndexForPath(pathToOpen);
    if (existingIndex >= 0) {
        return switchWorkspace(existingIndex);
    }

    const QString alias = interactive
        ? promptWorkspaceAlias(pathToOpen)
        : defaultWorkspaceAlias(pathToOpen);
    if (alias.isEmpty())
        return false;

    timer.restart();
    ActivityLogService::getInstance()->append(
        QStringLiteral("Workspace"),
        ActivityLogLevel::Info,
        QStringLiteral("Opening %1").arg(QDir::toNativeSeparators(pathToOpen)));

    WorkspaceEntry entry;
    entry.alias = alias;
    entry.path = pathToOpen;
    workspaces.append(entry);
    activeIndex = workspaces.size() - 1;
    rememberRecentWorkspace(entry);
    emit workspaceListChanged();

    activateWorkspacePath(entry.path, entry.alias, activeIndex);

    ActivityLogService::getInstance()->append(
        QStringLiteral("Workspace"),
        ActivityLogLevel::Info,
        QStringLiteral("Activated %1 as \"%2\"; file scan running")
            .arg(QDir::toNativeSeparators(workspacePath))
            .arg(workspaceAlias),
        static_cast<int>(timer.elapsed()));

    return true;
}

void WorkspaceManager::closeWorkspace()
{
    if (!isWorkspaceOpen()) return;
    const QString closingPath = workspacePath;
    cancelDirectoryScan();
    stopFileWatching();
    workspacePath.clear();
    workspaceAlias.clear();
    activeIndex = -1;
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

QString WorkspaceManager::getWorkspaceAlias() const
{
    return workspaceAlias;
}

QList<WorkspaceManager::WorkspaceEntry> WorkspaceManager::workspaceEntries() const
{
    return workspaces;
}

QList<WorkspaceManager::WorkspaceEntry> WorkspaceManager::recentWorkspaceEntries() const
{
    return recentWorkspaces;
}

int WorkspaceManager::activeWorkspaceIndex() const
{
    return activeIndex;
}

ProjectModel* WorkspaceManager::getProjectModel() const
{
    return projectModel.get();
}

ProjectSnapshot WorkspaceManager::projectSnapshot() const
{
    return projectModel ? projectModel->snapshot() : ProjectSnapshot();
}

bool WorkspaceManager::switchWorkspace(int index)
{
    if (index < 0 || index >= workspaces.size())
        return false;
    const WorkspaceEntry entry = workspaces.at(index);
    if (index == activeIndex && isWorkspaceOpen()) {
        rememberRecentWorkspace(entry);
        return true;
    }

    const bool activated = activateWorkspacePath(entry.path, entry.alias, index);
    if (activated)
        rememberRecentWorkspace(entry);
    return activated;
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
    if (normalizeWorkspacePath(path) != workspacePath) return;

    startDirectoryScan(workspacePath);
    emit directoryChanged(path);
}

void WorkspaceManager::startDirectoryScan(const QString& path)
{
    cancelDirectoryScan();
    pendingScannedFiles.clear();
    pendingScannedFiles.reserve(qMax(500, files.allFiles.size()));
    scanningPath = normalizeWorkspacePath(path);
    if (scanningPath.isEmpty())
        return;

    scanIterator =
        std::make_unique<QDirIterator>(scanningPath,
                                       QDir::Files,
                                       QDirIterator::Subdirectories);
    if (!scanTimer) {
        scanTimer = new QTimer(this);
        scanTimer->setSingleShot(false);
        connect(scanTimer,
                &QTimer::timeout,
                this,
                &WorkspaceManager::processDirectoryScanChunk);
    }

    emit workspaceScanStarted(scanningPath);
    scanTimer->start(0);
}

void WorkspaceManager::processDirectoryScanChunk()
{
    if (!scanIterator)
        return;

    QElapsedTimer chunkTimer;
    chunkTimer.start();
    int filesThisChunk = 0;
    constexpr int kMaxFilesPerChunk = 256;
    constexpr qint64 kMaxChunkMs = 8;
    while (scanIterator->hasNext()) {
        pendingScannedFiles.append(scanIterator->next());
        ++filesThisChunk;
        if (filesThisChunk >= kMaxFilesPerChunk
            || chunkTimer.elapsed() >= kMaxChunkMs) {
            break;
        }
    }

    if (filesThisChunk > 0)
        emit workspaceScanProgress(scanningPath, pendingScannedFiles.size());

    if (!scanIterator->hasNext())
        finishDirectoryScan();
}

void WorkspaceManager::finishDirectoryScan()
{
    if (scanTimer)
        scanTimer->stop();
    scanIterator.reset();

    const QString finishedPath = scanningPath;
    scanningPath.clear();
    const QStringList oldFiles = files.allFiles;
    files.setScannedFiles(projectModel.get(), pendingScannedFiles);
    pendingScannedFiles.clear();
    updateFileWatcher();

    if (files.allFiles != oldFiles)
        emit filesScanned(files.systemVerilogFiles);

    emit workspaceScanFinished(finishedPath,
                               files.allFiles.size(),
                               files.systemVerilogFiles.size());
    ActivityLogService::getInstance()->append(
        QStringLiteral("Workspace"),
        ActivityLogLevel::Info,
        QStringLiteral("Scanned %1 files, %2 SystemVerilog files")
            .arg(files.allFiles.size())
            .arg(files.systemVerilogFiles.size()));
}

void WorkspaceManager::cancelDirectoryScan()
{
    if (scanTimer)
        scanTimer->stop();
    scanIterator.reset();
    pendingScannedFiles.clear();
    scanningPath.clear();
}

void WorkspaceManager::updateFileWatcher()
{
    if (!watcher.active() || !isWorkspaceOpen()) return;

    watcher.updateFiles(files.allFiles);
}

bool WorkspaceManager::activateWorkspacePath(const QString& path,
                                             const QString& alias,
                                             int index)
{
    const QString normalizedPath = normalizeWorkspacePath(path);
    if (normalizedPath.isEmpty() || alias.trimmed().isEmpty())
        return false;

    stopFileWatching();
    files.clear();
    if (projectModel)
        projectModel->closeProject();

    workspaceAlias = alias.trimmed();
    projectModel->setWorkspaceRoot(normalizedPath);
    workspacePath = projectModel->workspaceRoot();
    activeIndex = index;
    startFileWatching();

    emit workspaceActivated(activeIndex, workspaceAlias, workspacePath);
    emit workspaceOpened(workspacePath);
    startDirectoryScan(workspacePath);
    return true;
}

void WorkspaceManager::loadRecentWorkspaces()
{
    recentWorkspaces.clear();

    QSettings settings(QStringLiteral("ZeroSlack"), QStringLiteral("ZeroSlack"));
    settings.beginGroup(QString::fromLatin1(kRecentWorkspaceGroup));
    const int count = settings.beginReadArray(
        QString::fromLatin1(kRecentWorkspaceItems));
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        WorkspaceEntry entry;
        entry.alias =
            settings.value(QString::fromLatin1(kRecentWorkspaceAlias)).toString().trimmed();
        entry.path = normalizeWorkspacePath(
            settings.value(QString::fromLatin1(kRecentWorkspacePath)).toString());
        if (entry.path.isEmpty())
            continue;
        if (entry.alias.isEmpty())
            entry.alias = defaultWorkspaceAlias(entry.path);
        if (workspaceIndexForPath(entry.path) >= 0)
            continue;

        const bool alreadyRecent =
            std::any_of(recentWorkspaces.cbegin(),
                        recentWorkspaces.cend(),
                        [&entry](const WorkspaceEntry& existing) {
                            return existing.path == entry.path;
                        });
        if (!alreadyRecent)
            recentWorkspaces.append(entry);
        if (recentWorkspaces.size() >= kMaxRecentWorkspaces)
            break;
    }
    settings.endArray();
    settings.endGroup();
}

void WorkspaceManager::saveRecentWorkspaces() const
{
    QSettings settings(QStringLiteral("ZeroSlack"), QStringLiteral("ZeroSlack"));
    settings.beginGroup(QString::fromLatin1(kRecentWorkspaceGroup));
    settings.remove(QString());
    settings.beginWriteArray(QString::fromLatin1(kRecentWorkspaceItems));
    for (int i = 0; i < recentWorkspaces.size(); ++i) {
        settings.setArrayIndex(i);
        const WorkspaceEntry& entry = recentWorkspaces.at(i);
        settings.setValue(QString::fromLatin1(kRecentWorkspaceAlias),
                          entry.alias);
        settings.setValue(QString::fromLatin1(kRecentWorkspacePath),
                          entry.path);
    }
    settings.endArray();
    settings.endGroup();
    settings.sync();
}

void WorkspaceManager::rememberRecentWorkspace(const WorkspaceEntry& entry)
{
    WorkspaceEntry normalized;
    normalized.path = normalizeWorkspacePath(entry.path);
    if (normalized.path.isEmpty())
        return;

    normalized.alias = entry.alias.trimmed();
    if (normalized.alias.isEmpty())
        normalized.alias = defaultWorkspaceAlias(normalized.path);

    for (int i = recentWorkspaces.size() - 1; i >= 0; --i) {
        if (recentWorkspaces.at(i).path == normalized.path)
            recentWorkspaces.removeAt(i);
    }
    recentWorkspaces.prepend(normalized);
    while (recentWorkspaces.size() > kMaxRecentWorkspaces)
        recentWorkspaces.removeLast();
    saveRecentWorkspaces();
}

QString WorkspaceManager::promptWorkspaceAlias(const QString& path) const
{
    const QString suggested = defaultWorkspaceAlias(path);
    bool accepted = false;
    const QString alias = QInputDialog::getText(
        qobject_cast<QWidget*>(parent()),
        QStringLiteral("Workspace Alias"),
        QStringLiteral("Alias"),
        QLineEdit::Normal,
        suggested,
        &accepted).trimmed();
    if (!accepted)
        return QString();
    return alias.isEmpty() ? suggested : alias;
}

QString WorkspaceManager::defaultWorkspaceAlias(const QString& path) const
{
    const QString name = QFileInfo(path).fileName().trimmed();
    return name.isEmpty() ? QStringLiteral("workspace") : name;
}

int WorkspaceManager::workspaceIndexForPath(const QString& path) const
{
    const QString normalizedPath = normalizeWorkspacePath(path);
    for (int i = 0; i < workspaces.size(); ++i) {
        if (workspaces.at(i).path == normalizedPath)
            return i;
    }
    return -1;
}

QString WorkspaceManager::normalizeWorkspacePath(const QString& path) const
{
    if (path.isEmpty())
        return QString();
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()));
}

bool WorkspaceManager::isSystemVerilogFile(const QString& fileName) const
{
    if (fileName.isEmpty()) return false;

    static const QStringList svExtensions = {"sv", "v", "vh", "svh", "vp", "svp"};
    const QString suffix = QFileInfo(fileName).suffix().toLower();
    return svExtensions.contains(suffix);
}
