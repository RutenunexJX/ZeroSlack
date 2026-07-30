#include "workspacemanager.h"

#include "slangparseoptions.h"
#include "activitylogservice.h"
#include "workspaceignoreservice.h"
#include <QFileDialog>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QInputDialog>
#include <QLineEdit>
#include <QPointer>
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
    , workspaceConfigurationService(
          std::make_unique<WorkspaceConfigurationService>())
{
    qRegisterMetaType<WorkspaceManager::WorkspaceEntry>("WorkspaceManager::WorkspaceEntry");
    files.reserveDefaults();
    loadRecentWorkspaces();
    workspaceAliasSelector = [](QWidget* dialogParent,
                                const QString& suggested) {
        bool accepted = false;
        const QString alias = QInputDialog::getText(
            dialogParent,
            QStringLiteral("Workspace Alias"),
            QStringLiteral("Alias"),
            QLineEdit::Normal,
            suggested,
            &accepted).trimmed();
        if (!accepted)
            return QString();
        return alias.isEmpty() ? suggested : alias;
    };

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
    return openWorkspaceInternal(folderPath, false);
}

bool WorkspaceManager::openWorkspaceFromUserSelection(
    const QString& folderPath)
{
    if (folderPath.isEmpty())
        return false;
    return openWorkspaceInternal(folderPath, true);
}

void WorkspaceManager::setWorkspaceAliasSelector(
    WorkspaceAliasSelector selector)
{
    workspaceAliasSelector = std::move(selector);
}

void WorkspaceManager::setRecentWorkspacePersistenceEnabledForTesting(
    bool enabled)
{
    recentWorkspacePersistenceEnabled = enabled;
}

bool WorkspaceManager::openWorkspaceInternal(
    const QString& folderPath,
    bool promptForAlias)
{
    QElapsedTimer timer;
    timer.start();

    QString pathToOpen = folderPath;
    const bool selectDirectoryHere = pathToOpen.isEmpty();
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

    const QString alias = (selectDirectoryHere || promptForAlias)
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

    if (!activateWorkspacePath(entry.path,
                               entry.alias,
                               activeIndex,
                               true)) {
        return false;
    }

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
    if (!isWorkspaceOpen())
        return;

    const int indexToClose = activeIndex >= 0
        ? activeIndex
        : workspaceIndexForPath(workspacePath);
    if (indexToClose >= 0) {
        closeWorkspace(indexToClose);
        return;
    }

    cancelDirectoryScan();
    stopFileWatching();
    const QString closingPath = workspacePath;
    workspacePath.clear();
    workspaceAlias.clear();
    activeIndex = -1;
    files.clear();
    if (projectModel)
        projectModel->closeProject();

    emit workspaceClosed();
    ActivityLogService::getInstance()->append(
        QStringLiteral("Workspace"),
        ActivityLogLevel::Info,
        QStringLiteral("Closed %1").arg(QDir::toNativeSeparators(closingPath)));
}

bool WorkspaceManager::closeWorkspace(int index)
{
    if (index < 0 || index >= workspaces.size())
        return false;

    const WorkspaceEntry closingEntry = workspaces.at(index);
    const bool closingActive = index == activeIndex;
    const bool activateReplacement = closingActive && workspaces.size() > 1;

    if (closingActive) {
        cancelDirectoryScan();
        stopFileWatching();
        if (!activateReplacement) {
            workspacePath.clear();
            workspaceAlias.clear();
            activeIndex = -1;
            files.clear();
        }
        if (!activateReplacement && projectModel)
            projectModel->closeProject();
    }

    workspaces.removeAt(index);
    if (!closingActive && index < activeIndex)
        --activeIndex;

    ActivityLogService::getInstance()->append(
        QStringLiteral("Workspace"),
        ActivityLogLevel::Info,
        QStringLiteral("Closed %1")
            .arg(QDir::toNativeSeparators(closingEntry.path)));

    if (closingActive)
        emit workspaceClosed();

    bool activatedNext = true;
    if (closingActive && !workspaces.isEmpty()) {
        int nextIndex = index;
        if (nextIndex >= workspaces.size())
            nextIndex = workspaces.size() - 1;

        const WorkspaceEntry nextEntry = workspaces.at(nextIndex);
        activatedNext =
            activateWorkspacePath(nextEntry.path,
                                  nextEntry.alias,
                                  nextIndex,
                                  false);
        if (activatedNext)
            rememberRecentWorkspace(nextEntry);
    }

    emit workspaceListChanged();
    return activatedNext;
}

bool WorkspaceManager::renameWorkspaceAlias(int index,
                                            const QString& alias,
                                            QString* errorMessage)
{
    if (errorMessage)
        errorMessage->clear();

    auto fail = [errorMessage](const QString& message) {
        if (errorMessage)
            *errorMessage = message;
        return false;
    };

    if (index < 0 || index >= workspaces.size())
        return fail(QStringLiteral("Workspace does not exist."));

    const QString trimmedAlias = alias.trimmed();
    if (trimmedAlias.isEmpty())
        return fail(QStringLiteral("Workspace alias cannot be empty."));

    for (int i = 0; i < workspaces.size(); ++i) {
        if (i == index)
            continue;
        if (QString::compare(workspaces.at(i).alias.trimmed(),
                             trimmedAlias,
                             Qt::CaseInsensitive) == 0) {
            return fail(QStringLiteral("Workspace alias already exists."));
        }
    }

    WorkspaceEntry& entry = workspaces[index];
    if (entry.alias == trimmedAlias)
        return true;

    entry.alias = trimmedAlias;
    if (index == activeIndex)
        workspaceAlias = trimmedAlias;

    bool updatedRecent = false;
    for (WorkspaceEntry& recent : recentWorkspaces) {
        if (recent.path == entry.path) {
            recent.alias = trimmedAlias;
            updatedRecent = true;
        }
    }
    if (updatedRecent)
        saveRecentWorkspaces();
    else
        rememberRecentWorkspace(entry);

    emit workspaceListChanged();
    ActivityLogService::getInstance()->append(
        QStringLiteral("Workspace"),
        ActivityLogLevel::Info,
        QStringLiteral("Renamed %1 to \"%2\"")
            .arg(QDir::toNativeSeparators(entry.path), trimmedAlias));
    return true;
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

QStringList WorkspaceManager::ignoredDirectories() const
{
    return projectModel ? projectModel->ignoredPaths() : QStringList();
}

WorkspaceConfiguration WorkspaceManager::workspaceConfiguration() const
{
    WorkspaceConfiguration configuration;
    if (!projectModel)
        return configuration;
    const ProjectSnapshot snapshot = projectModel->snapshot();
    configuration.workspaceRoot = snapshot.workspaceRoot;
    configuration.includeDirs = snapshot.includeDirs;
    configuration.defines = snapshot.defines;
    configuration.ignoredDirs = snapshot.ignoredPaths;
    configuration.fileExtensions = snapshot.fileExtensions;
    configuration.topModule = snapshot.topModule;
    return workspaceConfigurationService
        ? workspaceConfigurationService->normalized(configuration)
        : configuration;
}

bool WorkspaceManager::setIgnoredDirectories(const QStringList& directories,
                                             QString* errorMessage)
{
    if (errorMessage)
        errorMessage->clear();

    const WorkspaceIgnoreReport report =
        WorkspaceIgnoreService::getInstance()->normalizeIgnoredDirectories(
            WorkspaceIgnoreQuery{workspacePath, directories});
    if (!report.valid) {
        if (errorMessage)
            *errorMessage = report.failureReason;
        return false;
    }

    if (!projectModel)
        return false;

    projectModel->setIgnoredPaths(report.ignoredDirectories);
    files.allFiles = projectModel->allFiles();
    files.systemVerilogFiles = projectModel->systemVerilogFiles();

    if (activeIndex >= 0 && activeIndex < workspaces.size()
        && workspaces.at(activeIndex).path == workspacePath) {
        workspaces[activeIndex].ignoredDirectories = report.ignoredDirectories;
    }
    WorkspaceConfiguration configuration = workspaceConfiguration();
    configuration.ignoredDirs = report.ignoredDirectories;
    if (workspaceConfigurationService)
        workspaceConfigurationService->save(configuration);
    updateActiveEntryConfiguration(configuration);

    updateFileWatcher();
    emit filesScanned(files.systemVerilogFiles);
    emit workspaceListChanged();
    ActivityLogService::getInstance()->append(
        QStringLiteral("Workspace"),
        ActivityLogLevel::Info,
        QStringLiteral("Ignored %1 workspace director%2")
            .arg(report.ignoredDirectories.size())
            .arg(report.ignoredDirectories.size() == 1
                     ? QStringLiteral("y")
                     : QStringLiteral("ies")));
    return true;
}

bool WorkspaceManager::setWorkspaceConfiguration(
    const WorkspaceConfiguration& configuration,
    QString* errorMessage)
{
    return applyWorkspaceConfiguration(configuration, true, errorMessage);
}

bool WorkspaceManager::restoreSessionScanState(const QStringList& scannedFiles,
                                               bool scanComplete)
{
    if (!isWorkspaceOpen() || !projectModel)
        return false;

    cancelDirectoryScan();

    QStringList existingFiles;
    existingFiles.reserve(scannedFiles.size());
    for (const QString& file : scannedFiles) {
        const QString path = normalizeWorkspacePath(file);
        if (path.isEmpty() || !QFileInfo(path).isFile())
            continue;
        const QString rootPrefix = workspacePath.endsWith(QLatin1Char('/'))
            ? workspacePath
            : workspacePath + QLatin1Char('/');
#ifdef Q_OS_WIN
        const QString comparePath = path.toCaseFolded();
        const QString compareRoot = workspacePath.toCaseFolded();
        const QString comparePrefix = rootPrefix.toCaseFolded();
#else
        const QString comparePath = path;
        const QString compareRoot = workspacePath;
        const QString comparePrefix = rootPrefix;
#endif
        if (comparePath == compareRoot
            || comparePath.startsWith(comparePrefix)) {
            existingFiles.append(path);
        }
    }
    existingFiles.removeDuplicates();
    existingFiles.sort(Qt::CaseInsensitive);

    files.setScannedFiles(projectModel.get(), existingFiles);
    if (activeIndex >= 0 && activeIndex < workspaces.size()
        && workspaces.at(activeIndex).path == workspacePath) {
        workspaces[activeIndex].scannedFiles = existingFiles;
        workspaces[activeIndex].ignoredDirectories =
            projectModel ? projectModel->ignoredPaths() : QStringList();
        workspaces[activeIndex].scanComplete = scanComplete;
    }

    updateFileWatcher();
    emit filesScanned(files.systemVerilogFiles);
    emit workspaceListChanged();
    if (scanComplete) {
        emit workspaceScanFinished(workspacePath,
                                   files.allFiles.size(),
                                   files.systemVerilogFiles.size());
    } else {
        startDirectoryScan(workspacePath);
    }
    return true;
}

ProjectModel* WorkspaceManager::getProjectModel() const
{
    return projectModel.get();
}

ProjectSnapshot WorkspaceManager::projectSnapshot() const
{
    ++projectSnapshotMaterializationCount;
    return projectModel ? projectModel->snapshot() : ProjectSnapshot();
}

std::uint64_t
WorkspaceManager::projectSnapshotMaterializationCountForTesting() const
{
    return projectSnapshotMaterializationCount;
}

void WorkspaceManager::resetProjectSnapshotMaterializationCountForTesting()
{
    projectSnapshotMaterializationCount = 0;
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

    const bool activated =
        activateWorkspacePath(entry.path, entry.alias, index, false);
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

QString WorkspaceManager::resolveIncludePath(const QString& includePath,
                                             const QString& currentFile) const
{
    if (includePath.isEmpty())
        return QString();

    const QFileInfo direct(includePath);
    if (direct.isAbsolute() && direct.isFile())
        return normalizeWorkspacePath(direct.absoluteFilePath());

    const QStringList configuredIncludeDirs =
        projectModel ? projectModel->snapshot().includeDirs : QStringList();
    const QStringList effectiveIncludeDirs =
        slang_parse_options::effectiveIncludeDirsForFile(
            currentFile, configuredIncludeDirs);
    for (const QString& includeDir : effectiveIncludeDirs) {
        const QString candidate =
            QDir(includeDir).absoluteFilePath(includePath);
        if (QFileInfo(candidate).isFile())
            return normalizeWorkspacePath(candidate);
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
}

void WorkspaceManager::startDirectoryScan(const QString& path)
{
    cancelDirectoryScan();
    pendingScannedFiles.clear();
    pendingScannedFiles.reserve(qMax(500, files.allFiles.size()));
    scanningPath = normalizeWorkspacePath(path);
    if (scanningPath.isEmpty() || scanningPath != workspacePath)
        return;

    const std::uint64_t generation = scanGeneration;
    const QString requestedPath = scanningPath;

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

    QPointer<WorkspaceManager> self(this);
    emit workspaceScanStarted(requestedPath);
    if (!self || !directoryScanIsCurrent(generation, requestedPath))
        return;
    scanTimer->start(0);
}

void WorkspaceManager::processDirectoryScanChunk()
{
    const std::uint64_t generation = scanGeneration;
    const QString requestedPath = scanningPath;
    if (!directoryScanIsCurrent(generation, requestedPath))
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

    if (filesThisChunk > 0) {
        QPointer<WorkspaceManager> self(this);
        emit workspaceScanProgress(requestedPath,
                                   pendingScannedFiles.size());
        if (!self || !directoryScanIsCurrent(generation, requestedPath))
            return;
    }

    if (!scanIterator->hasNext())
        finishDirectoryScan(generation, requestedPath);
}

void WorkspaceManager::finishDirectoryScan(std::uint64_t generation,
                                           const QString& path)
{
    if (!directoryScanIsCurrent(generation, path))
        return;

    if (scanTimer)
        scanTimer->stop();
    scanIterator.reset();
    scanningPath.clear();
    const QString finishedPath = path;
    const QStringList scannedFiles = pendingScannedFiles;
    pendingScannedFiles.clear();
    const std::uint64_t completionGeneration = ++scanGeneration;
    const QStringList oldFiles = files.allFiles;

    QPointer<WorkspaceManager> self(this);
    QPointer<ProjectModel> expectedProject(projectModel.get());
    if (!expectedProject)
        return;
    expectedProject->setScannedFiles(scannedFiles);
    if (!self || !expectedProject
        || scanGeneration != completionGeneration
        || workspacePath != finishedPath) {
        return;
    }
    files.allFiles = expectedProject->allFiles();
    files.systemVerilogFiles = expectedProject->systemVerilogFiles();
    if (activeIndex >= 0 && activeIndex < workspaces.size()
        && workspaces.at(activeIndex).path == finishedPath) {
        workspaces[activeIndex].scannedFiles = scannedFiles;
        workspaces[activeIndex].ignoredDirectories =
            projectModel ? projectModel->ignoredPaths() : QStringList();
        workspaces[activeIndex].scanComplete = true;
    }
    updateFileWatcher();

    if (files.allFiles != oldFiles) {
        emit filesScanned(files.systemVerilogFiles);
        if (!self || scanGeneration != completionGeneration
            || workspacePath != finishedPath) {
            return;
        }
    }

    const int totalFiles = files.allFiles.size();
    const int systemVerilogFiles = files.systemVerilogFiles.size();
    emit workspaceScanFinished(finishedPath,
                               totalFiles,
                               systemVerilogFiles);
    if (!self || scanGeneration != completionGeneration
        || workspacePath != finishedPath) {
        return;
    }
    ActivityLogService::getInstance()->append(
        QStringLiteral("Workspace"),
        ActivityLogLevel::Info,
        QStringLiteral("Scanned %1 files, %2 SystemVerilog files")
            .arg(totalFiles)
            .arg(systemVerilogFiles));
}

void WorkspaceManager::cancelDirectoryScan()
{
    ++scanGeneration;
    if (scanTimer)
        scanTimer->stop();
    scanIterator.reset();
    pendingScannedFiles.clear();
    scanningPath.clear();
}

bool WorkspaceManager::directoryScanIsCurrent(
    std::uint64_t generation,
    const QString& path) const
{
    return generation == scanGeneration
        && scanIterator
        && !path.isEmpty()
        && scanningPath == path
        && workspacePath == path;
}

void WorkspaceManager::updateFileWatcher()
{
    if (!watcher.active() || !isWorkspaceOpen()) return;

    watcher.updateFiles(files.allFiles);
}

bool WorkspaceManager::restoreWorkspaceFilesFromEntry(int index)
{
    if (index < 0 || index >= workspaces.size() || !projectModel)
        return false;

    const WorkspaceEntry entry = workspaces.at(index);
    projectModel->setWorkspaceState(entry.path, entry.scannedFiles);
    WorkspaceConfiguration configuration = loadConfigurationForWorkspace(entry.path);
    if (!entry.includeDirs.isEmpty())
        configuration.includeDirs = entry.includeDirs;
    if (!entry.fileExtensions.isEmpty())
        configuration.fileExtensions = entry.fileExtensions;
    if (!entry.defines.isEmpty())
        configuration.defines = entry.defines;
    if (!entry.topModule.isEmpty())
        configuration.topModule = entry.topModule;
    if (!entry.ignoredDirectories.isEmpty())
        configuration.ignoredDirs = entry.ignoredDirectories;
    applyWorkspaceConfiguration(configuration, false, nullptr, false);
    files.allFiles = projectModel->allFiles();
    files.systemVerilogFiles = projectModel->systemVerilogFiles();
    return true;
}

WorkspaceConfiguration WorkspaceManager::loadConfigurationForWorkspace(
    const QString& path) const
{
    if (workspaceConfigurationService)
        return workspaceConfigurationService->load(path);
    WorkspaceConfigurationService fallback;
    return fallback.defaultConfiguration(path);
}

bool WorkspaceManager::applyWorkspaceConfiguration(
    const WorkspaceConfiguration& configuration,
    bool persist,
    QString* errorMessage,
    bool notify)
{
    if (errorMessage)
        errorMessage->clear();
    if (!projectModel)
        return false;

    WorkspaceConfiguration clean =
        workspaceConfigurationService
            ? workspaceConfigurationService->normalized(configuration)
            : configuration;
    if (clean.workspaceRoot.isEmpty())
        clean.workspaceRoot = workspacePath;
    if (clean.workspaceRoot.isEmpty())
        return false;

    const WorkspaceIgnoreReport ignoreReport =
        WorkspaceIgnoreService::getInstance()->normalizeIgnoredDirectories(
            WorkspaceIgnoreQuery{clean.workspaceRoot, clean.ignoredDirs});
    if (!ignoreReport.valid) {
        if (errorMessage)
            *errorMessage = ignoreReport.failureReason;
        return false;
    }
    clean.ignoredDirs = ignoreReport.ignoredDirectories;

    if (persist && workspaceConfigurationService
        && !workspaceConfigurationService->save(clean)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Failed to save workspace configuration.");
        return false;
    }

    projectModel->setWorkspaceConfiguration(clean.includeDirs,
                                            clean.defines,
                                            clean.fileExtensions,
                                            clean.topModule,
                                            clean.ignoredDirs);
    files.allFiles = projectModel->allFiles();
    files.systemVerilogFiles = projectModel->systemVerilogFiles();
    updateActiveEntryConfiguration(clean);
    updateFileWatcher();
    if (notify) {
        emit filesScanned(files.systemVerilogFiles);
        emit workspaceListChanged();
        ActivityLogService::getInstance()->append(
            QStringLiteral("Workspace"),
            ActivityLogLevel::Info,
            QStringLiteral("Applied workspace configuration: %1 include dirs, %2 defines, %3 ignored dirs")
                .arg(clean.includeDirs.size())
                .arg(clean.defines.size())
                .arg(clean.ignoredDirs.size()));
    }
    return true;
}

void WorkspaceManager::updateActiveEntryConfiguration(
    const WorkspaceConfiguration& configuration)
{
    if (activeIndex < 0 || activeIndex >= workspaces.size())
        return;
    WorkspaceEntry& entry = workspaces[activeIndex];
    if (entry.path != configuration.workspaceRoot)
        return;
    entry.ignoredDirectories = configuration.ignoredDirs;
    entry.includeDirs = configuration.includeDirs;
    entry.defines = configuration.defines;
    entry.fileExtensions = configuration.fileExtensions;
    entry.topModule = configuration.topModule;
}

bool WorkspaceManager::activateWorkspacePath(const QString& path,
                                             const QString& alias,
                                             int index,
                                             bool openedNewWorkspace)
{
    const QString normalizedPath = normalizeWorkspacePath(path);
    if (normalizedPath.isEmpty() || alias.trimmed().isEmpty())
        return false;

    cancelDirectoryScan();
    stopFileWatching();

    workspaceAlias = alias.trimmed();
    activeIndex = index;

    if (index >= 0 && index < workspaces.size()
        && workspaces.at(index).scanComplete) {
        restoreWorkspaceFilesFromEntry(index);
    } else {
        files.clear();
        if (projectModel) {
            projectModel->setWorkspaceState(normalizedPath, {});
            WorkspaceConfiguration configuration =
                loadConfigurationForWorkspace(normalizedPath);
            if (index >= 0 && index < workspaces.size()
                && !workspaces.at(index).ignoredDirectories.isEmpty()) {
                configuration.ignoredDirs =
                    workspaces.at(index).ignoredDirectories;
            }
            applyWorkspaceConfiguration(configuration, false, nullptr, false);
        }
    }

    workspacePath = projectModel ? projectModel->workspaceRoot() : normalizedPath;
    startFileWatching();

    const QString activatedPath = workspacePath;
    const std::uint64_t activationGeneration = scanGeneration;
    const bool requiresScan = openedNewWorkspace
        || index < 0
        || index >= workspaces.size()
        || !workspaces.at(index).scanComplete;
    QPointer<WorkspaceManager> self(this);

    emit workspaceActivated(activeIndex, workspaceAlias, workspacePath);
    if (!self || scanGeneration != activationGeneration
        || workspacePath != activatedPath || activeIndex != index) {
        return false;
    }
    if (openedNewWorkspace) {
        emit workspaceOpened(workspacePath);
        if (!self || scanGeneration != activationGeneration
            || workspacePath != activatedPath || activeIndex != index) {
            return false;
        }
    }
    if (requiresScan) {
        startDirectoryScan(workspacePath);
        if (!self || workspacePath != activatedPath || activeIndex != index)
            return false;
    }
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
    if (!recentWorkspacePersistenceEnabled)
        return;

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
    if (!workspaceAliasSelector)
        return QString();
    return workspaceAliasSelector(qobject_cast<QWidget*>(parent()),
                                  suggested).trimmed();
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

    const QString suffix =
        QStringLiteral(".%1").arg(QFileInfo(fileName).suffix().toLower());
    const QStringList extensions =
        projectModel ? projectModel->fileExtensions()
                     : WorkspaceConfigurationService::defaultFileExtensions();
    return extensions.contains(suffix, Qt::CaseInsensitive);
}
