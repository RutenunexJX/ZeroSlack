#include "uidialogs.h"
#include "workspacemanager.h"

#include "slangparseoptions.h"
#include "semanticanalysisinput.h"
#include "activitylogservice.h"
#include "workspaceignoreservice.h"
#include <QFileDialog>
#include <QDir>
#include <QDirIterator>
#include <QDateTime>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QInputDialog>
#include <QLineEdit>
#include <QPointer>
#include <QSettings>
#include <QSet>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>
#include <QThread>
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
    directoryThreadPool.setMaxThreadCount(1);
    directoryThreadPool.setThreadPriority(QThread::LowPriority);
    directoryThreadPool.setObjectName(QStringLiteral("ZeroSlackWorkspaceDirectory"));
    files.reserveDefaults();
    loadRecentWorkspaces();
    workspaceAliasSelector = [](QWidget* dialogParent,
                                const QString& suggested) {
        bool accepted = false;
        const QString alias = UiDialogs::getText(
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
    this->owner = owner;
    if (watcher)
        return;
    watcher = std::make_unique<QFileSystemWatcher>(owner);
    continuation = std::make_unique<QTimer>();
    continuation->setSingleShot(true);
    QObject::connect(continuation.get(), &QTimer::timeout, owner, [this] { advance(); });
    QObject::connect(watcher.get(), &QFileSystemWatcher::fileChanged,
                     owner, &WorkspaceManager::onFileChanged);
    QObject::connect(watcher.get(), &QFileSystemWatcher::directoryChanged,
                     owner, &WorkspaceManager::onDirectoryChanged);
}

void WorkspaceManager::WorkspaceWatcher::clear()
{
    installed = {};
    apply({});
}

void WorkspaceManager::WorkspaceWatcher::apply(const PreparedPaths& prepared,
                                               std::function<void()> completion)
{
    if (!watcher)
        return;
    if (completion)
        installed = std::move(completion);
    // Windows paths from captured input are case folded; directory enumeration
    // keeps display case. Reconcile by the same identity to avoid registering
    // the complete workspace a second time after semantic publication.
    const auto key = SemanticInputCapture::pathKey;
    QHash<QString, QString> current;
    for (const auto& path : watcher->files()) current.insert(key(path), path);
    for (const auto& path : watcher->directories()) current.insert(key(path), path);
    desiredFiles.clear();
    desiredDirectories.clear();
    for (const auto& path : prepared.directories) desiredDirectories.insert(key(path), path);
    for (const auto& path : prepared.files) desiredFiles.insert(key(path), path);
    removals.clear();
    additions.clear();
    for (auto it = current.cbegin(); it != current.cend(); ++it)
        if (!desiredFiles.contains(it.key()) && !desiredDirectories.contains(it.key()))
            removals.append(it.value());
    // Ancestor directories are watched before their files. Input publication
    // waits for this bounded reconciliation to finish before capturing sources.
    QSet<QString> queued;
    for (const auto& paths : {prepared.directories, prepared.files})
        for (const auto& path : paths) {
            const auto identity = key(path);
            if (!current.contains(identity) && !queued.contains(identity)) {
                additions.append(path);
                queued.insert(identity);
            }
        }
    continuation->start(0);
}

void WorkspaceManager::WorkspaceWatcher::advance()
{
    QElapsedTimer budget;
    budget.start();
    int operations = 0;
    while ((!removals.isEmpty() || !additions.isEmpty())
           && operations < 8 && budget.elapsed() < 4) {
        if (!removals.isEmpty()) watcher->removePath(removals.takeLast());
        else watcher->addPath(additions.takeFirst());
        ++operations;
    }
    if (!removals.isEmpty() || !additions.isEmpty()) {
        continuation->start(0);
        return;
    }
    auto completion = std::move(installed);
    installed = {};
    if (completion) completion();
}

QString WorkspaceManager::WorkspaceWatcher::rewatchFile(const QString& path)
{
    if (!watcher)
        return {};
    const QString ownedPath = desiredFiles.value(SemanticInputCapture::pathKey(path));
    if (ownedPath.isEmpty())
        return {};
    // QFileSystemWatcher removes a replaced file. Re-establish coverage using
    // current desired ownership, not the native watcher's delayed state. Use
    // its registered spelling for Windows case/separator aliases, including
    // external includes, without filesystem queries in this callback.
    if (!additions.contains(ownedPath)) additions.append(ownedPath);
    continuation->start(0);
    return ownedPath;
}

bool WorkspaceManager::WorkspaceWatcher::active() const
{
    return watcher != nullptr;
}

WorkspaceManager::~WorkspaceManager()
{
    cancelDirectoryScan();
    if (scanWatcher)
        disconnect(scanWatcher, nullptr, this, nullptr);
    directoryThreadPool.waitForDone();
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
    if (!canChangeActiveWorkspace()) return false;

    timer.restart();
    ActivityLogService::getInstance()->append(
        QStringLiteral("Workspace"),
        ActivityLogLevel::Info,
        QStringLiteral("Opening %1").arg(QDir::toNativeSeparators(pathToOpen)));

    WorkspaceEntry entry;
    entry.alias = alias;
    entry.path = pathToOpen;
    workspaces.append(entry);
    const int newIndex = workspaces.size() - 1;
    if (!activateWorkspacePath(entry.path, entry.alias, newIndex, true)) {
        // Failed preflight leaves the previous workspace and its scan intact.
        if (newIndex < workspaces.size() && workspaces.at(newIndex).path == entry.path
            && activeIndex != newIndex) workspaces.removeAt(newIndex);
        emit workspaceListChanged();
        return false;
    }
    rememberRecentWorkspace(entry);
    emit workspaceListChanged();

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

    if (!canChangeActiveWorkspace()) return;

    cancelDirectoryScan();
    stopFileWatching();
    ++workspaceActivationGeneration;
    const QString closingPath = workspacePath;
    workspacePath.clear();
    workspaceAlias.clear();
    activeIndex = -1;
    files.clear();
    scannedDirectories.clear();
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
    if (closingActive && !canChangeActiveWorkspace()) return false;
    const bool activateReplacement = closingActive && workspaces.size() > 1;
    WorkspaceConfiguration replacementConfiguration;
    if (activateReplacement) {
        const int replacementIndex = index + 1 < workspaces.size() ? index + 1 : index - 1;
        replacementConfiguration = loadConfigurationForWorkspace(workspaces.at(replacementIndex).path);
        if (!replacementConfiguration.isValid()) return false;
    }

    if (closingActive) {
        ++workspaceActivationGeneration;
        cancelDirectoryScan();
        stopFileWatching();
        if (!activateReplacement) {
            workspacePath.clear();
            workspaceAlias.clear();
            activeIndex = -1;
            files.clear();
            scannedDirectories.clear();
        }
        if (!activateReplacement && projectModel)
            projectModel->closeProject();
    }

    workspaces.removeAt(index);
    if (projectModel)
        projectModel->notifyWorkspaceClosed(closingEntry.path);
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
                                  false, &replacementConfiguration);
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

bool WorkspaceManager::removeRecentWorkspace(const QString& path)
{
    const QString normalizedPath = normalizeWorkspacePath(path);
    if (normalizedPath.isEmpty())
        return false;

    bool removed = false;
    for (int i = recentWorkspaces.size() - 1; i >= 0; --i) {
        if (recentWorkspaces.at(i).path != normalizedPath)
            continue;
        recentWorkspaces.removeAt(i);
        removed = true;
    }
    if (!removed)
        return false;

    saveRecentWorkspaces();
    return true;
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
    if (activeIndex >= 0
        && activeIndex < workspaces.size()
        && workspaces.at(activeIndex).path
               == snapshot.workspaceRoot) {
        configuration.virtualSourceGroups =
            workspaces.at(activeIndex)
                .virtualSourceGroups;
        configuration.storageRevision = workspaces.at(activeIndex).configurationRevision;
    }
    return workspaceConfigurationService
        ? workspaceConfigurationService->normalized(configuration)
        : configuration;
}

QList<WorkspaceVirtualSourceGroup>
WorkspaceManager::virtualSourceGroups() const
{
    return workspaceConfiguration()
        .virtualSourceGroups;
}

bool WorkspaceManager::setVirtualSourceGroups(
    const QList<WorkspaceVirtualSourceGroup>& groups,
    QString* errorMessage)
{
    WorkspaceConfiguration configuration =
        workspaceConfiguration();
    if (!configuration.isValid()) {
        if (errorMessage) {
            *errorMessage =
                QStringLiteral(
                    "No workspace is open.");
        }
        return false;
    }
    configuration.virtualSourceGroups = groups;
    return setWorkspaceConfiguration(
        configuration, errorMessage);
}

bool WorkspaceManager::setIgnoredDirectories(const QStringList& directories,
                                             QString* errorMessage)
{
    WorkspaceConfiguration configuration = workspaceConfiguration();
    configuration.ignoredDirs = directories;
    return setWorkspaceConfiguration(configuration, errorMessage);
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
        if (path.isEmpty())
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

    projectModel->setScannedFiles(existingFiles, false);
    files.allFiles = projectModel->allFiles();
    files.systemVerilogFiles = projectModel->systemVerilogFiles();
    if (activeIndex >= 0 && activeIndex < workspaces.size()
        && workspaces.at(activeIndex).path == workspacePath) {
        workspaces[activeIndex].scannedFiles = existingFiles;
        workspaces[activeIndex].ignoredDirectories =
            projectModel ? projectModel->ignoredPaths() : QStringList();
        workspaces[activeIndex].scanComplete = scanComplete;
    }

    emit filesScanned(files.systemVerilogFiles);
    emit workspaceListChanged();
    // A completed session scan is only a cache snapshot. Files may have been
    // added, removed, or renamed while ZeroSlack was closed, so reconcile the
    // restored list with the directory without delaying its initial display.
    startDirectoryScan(workspacePath);
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

    if (!canChangeActiveWorkspace()) return false;

    const bool activated =
        activateWorkspacePath(entry.path, entry.alias, index, false);
    if (activated)
        rememberRecentWorkspace(entry);
    return activated;
}

void WorkspaceManager::setWorkspaceTransitionGuard(std::function<QString()> guard)
{
    workspaceTransitionGuard = std::move(guard);
}

bool WorkspaceManager::canChangeActiveWorkspace() const
{
    const QString error = workspaceTransitionGuard ? workspaceTransitionGuard() : QString();
    if (error.isEmpty()) return true;
    ActivityLogService::getInstance()->append(QStringLiteral("Workspace"), ActivityLogLevel::Info, error);
    return false;
}

QStringList WorkspaceManager::getAllFiles() const
{
    return files.allFiles;
}

QStringList WorkspaceManager::getSystemVerilogFiles() const
{
    return files.systemVerilogFiles;
}

void WorkspaceManager::refreshWorkspaceFiles()
{
    if (isWorkspaceOpen())
        startDirectoryScan(workspacePath);
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
    WorkspaceWatcher::PreparedPaths rootWatch;
    rootWatch.directories = {workspacePath};
    semanticWatchPaths = {};
    preparedWatchPaths = rootWatch;
    watcher.apply(rootWatch);
}

void WorkspaceManager::stopFileWatching()
{
    directoryMembership.clear();
    semanticWatchPaths = {};
    preparedWatchPaths = {};
    watcher.clear();
}

void WorkspaceManager::applySemanticWatchPaths(const QString& root,
    const QStringList& files, const QStringList& directories)
{
    if (normalizeWorkspacePath(root) != workspacePath || !isWorkspaceOpen())
        return;
    QSet<QString> previous;
    for (const auto& paths : {preparedWatchPaths.files, preparedWatchPaths.directories,
                             semanticWatchPaths.files, semanticWatchPaths.directories})
        for (const auto& path : paths) previous.insert(SemanticInputCapture::pathKey(path));
    bool extendsCoverage = false;
    for (const auto& paths : {files, directories})
        for (const auto& path : paths)
            extendsCoverage |= !previous.contains(SemanticInputCapture::pathKey(path));
    semanticWatchPaths.files = files;
    semanticWatchPaths.directories = directories;
    applyCombinedWatchPaths();
    if (extendsCoverage) {
        // An include may have changed between capture and watch installation.
        // Revalidate once AFTER new coverage is installed. Unchanged desired
        // coverage does not schedule another request, so this cannot loop.
        auto previousCompletion = std::move(watcher.installed);
        watcher.installed = [self = QPointer<WorkspaceManager>(this), root,
                             previousCompletion = std::move(previousCompletion)] {
            if (previousCompletion) previousCompletion();
            if (self && self->isWorkspaceOpen()
                && self->normalizeWorkspacePath(root) == self->workspacePath)
                emit self->semanticInputsChanged(root);
        };
    }
}

void WorkspaceManager::applyCombinedWatchPaths()
{
    auto paths = preparedWatchPaths;
    paths.files.append(semanticWatchPaths.files);
    paths.directories.append(semanticWatchPaths.directories);
    paths.files.removeDuplicates();
    paths.directories.removeDuplicates();
    watcher.apply(paths);
}

void WorkspaceManager::onFileChanged(const QString& path)
{
    const QString ownedPath = watcher.rewatchFile(path);
    if (ownedPath.isEmpty())
        return;
    const QString identity = SemanticInputCapture::pathKey(ownedPath);
    if (std::any_of(semanticWatchPaths.files.cbegin(), semanticWatchPaths.files.cend(),
            [&](const QString& file) { return SemanticInputCapture::pathKey(file) == identity; })) {
        emit semanticInputsChanged(workspacePath);
        return;
    }
    if (!isSystemVerilogFile(ownedPath)) return;

    emit fileChanged(ownedPath);
}

void WorkspaceManager::onDirectoryChanged(const QString& path)
{
    if (!isWorkspaceOpen())
        return;
    const QString key = SemanticInputCapture::pathKey(path);
    const QString ownedPath = watcher.desiredDirectories.value(key);
    if (ownedPath.isEmpty())
        return;

    QPointer<WorkspaceManager> self(this);
    const auto activation = workspaceActivationGeneration;
    if (std::any_of(semanticWatchPaths.directories.cbegin(), semanticWatchPaths.directories.cend(),
            [&](const QString& directory) { return SemanticInputCapture::pathKey(directory) == key; }))
        emit semanticInputsChanged(workspacePath);
    if (!self || activation != workspaceActivationGeneration
        || (ownedPath.compare(workspacePath, Qt::CaseInsensitive) != 0
            && !ownedPath.startsWith(workspacePath + '/', Qt::CaseInsensitive)))
        return;
    // Coalesced into one worker plus one latest request. No directory listing
    // or membership comparison runs in the GUI notification handler.
    startDirectoryScan(workspacePath,
        (!directoryScanPending || membershipProbeDirectory == key)
                && directoryMembership.contains(key) ? ownedPath : QString());
}

void WorkspaceManager::startDirectoryScan(const QString& path, const QString& membershipDirectory)
{
    cancelDirectoryScan();
    scanningPath = normalizeWorkspacePath(path);
    if (scanningPath.isEmpty() || scanningPath != workspacePath)
        return;
    directoryScanPending = true;
    membershipProbeDirectory = SemanticInputCapture::pathKey(membershipDirectory);
    pendingScanRequest = DirectoryScanRequest{scanningPath,
        projectModel ? projectModel->snapshot() : ProjectSnapshot{}, scanGeneration,
        membershipDirectory, directoryMembership.value(SemanticInputCapture::pathKey(membershipDirectory))};
    if (!scanTimer) {
        scanTimer = new QTimer(this);
        scanTimer->setInterval(50);
        connect(scanTimer, &QTimer::timeout, this, &WorkspaceManager::processDirectoryScanChunk);
    }
    if (membershipDirectory.isEmpty())
        scanTimer->start();
    const auto generation = scanGeneration;
    const auto requestedPath = scanningPath;
    QPointer<WorkspaceManager> self(this);
    if (membershipDirectory.isEmpty())
        emit workspaceScanStarted(requestedPath);
    if (self && directoryScanIsCurrent(generation, requestedPath))
        launchPendingDirectoryScan();
}

void WorkspaceManager::launchPendingDirectoryScan()
{
    if (scanWatcher || !pendingScanRequest)
        return;
    const auto request = *pendingScanRequest;
    pendingScanRequest.reset();
    auto cancellation = std::make_shared<std::atomic_bool>(false);
    auto progress = std::make_shared<std::atomic<int>>(0);
    scanCancellation = cancellation;
    scanProgress = progress;
    auto* watcher = new QFutureWatcher<DirectoryScanResult>(this);
    scanWatcher = watcher;
    connect(watcher, &QFutureWatcher<DirectoryScanResult>::finished, this,
        [this, watcher, request, cancellation] {
            auto result = watcher->future().takeResult();
            scanWatcher = nullptr;
            watcher->deleteLater();
            if (scanCancellation == cancellation)
                scanCancellation.reset();
            if (!result.cancelled && directoryScanIsCurrent(request.generation, request.path)) {
                if (result.membershipProbe) {
                    directoryScanPending = false;
                    scanningPath.clear();
                    scanProgress.reset();
                    QPointer<WorkspaceManager> self(this);
                    if (result.membershipChanged)
                        startDirectoryScan(request.path);
                    if (self)
                        launchPendingDirectoryScan();
                    return;
                }
                pendingScannedFiles = std::move(result.files);
                pendingScannedDirectories = std::move(result.directories);
                directoryMembership = std::move(result.membership);
                preparedWatchPaths = std::move(result.watches);
                QPointer<WorkspaceManager> self(this);
                finishDirectoryScan(request.generation, request.path);
                if (!self)
                    return;
            }
            launchPendingDirectoryScan();
        });
    watcher->setFuture(QtConcurrent::run(&directoryThreadPool, [request, cancellation, progress] {
        DirectoryScanResult result;
        if (!request.membershipDirectory.isEmpty()) {
            result.membershipProbe = true;
            QStringList children;
            const auto entries = QDir(request.membershipDirectory).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot);
            for (const auto& entry : entries) {
                if (cancellation->load(std::memory_order_relaxed)) {
                    result.cancelled = true;
                    return result;
                }
                children.append(SemanticInputCapture::pathKey(entry.absoluteFilePath()));
            }
            children.sort(Qt::CaseSensitive);
            result.membershipChanged = children != request.expectedChildren;
            return result;
        }
        result.directories.append(request.path);
        result.membership.insert(SemanticInputCapture::pathKey(request.path), {});
        auto normalized = [](const QString& path) {
            return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()));
        };
        auto sourceFile = [&](const QString& path) {
            QString suffix = QFileInfo(path).suffix().toLower();
            for (QString extension : request.project.fileExtensions) {
                if (extension.startsWith('.'))
                    extension.remove(0, 1);
                if (extension.compare(suffix, Qt::CaseInsensitive) == 0)
                    return true;
            }
            return false;
        };
        auto ignored = [&](const QString& path) {
            for (const QString& prefix : request.project.ignoredPaths)
                if (path.compare(prefix, Qt::CaseInsensitive) == 0
                    || path.startsWith(prefix + '/', Qt::CaseInsensitive))
                    return true;
            return false;
        };
        QDirIterator iterator(request.path, QDir::AllEntries | QDir::NoDotAndDotDot,
                              QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            if (cancellation->load(std::memory_order_relaxed)) {
                result.cancelled = true;
                return result;
            }
            const QString path = normalized(iterator.next());
            const QFileInfo entry = iterator.fileInfo();
            result.membership[SemanticInputCapture::pathKey(entry.absolutePath())]
                .append(SemanticInputCapture::pathKey(path));
            if (entry.isDir()) {
                result.directories.append(path);
                result.membership[SemanticInputCapture::pathKey(path)];
            } else {
                result.files.append(path);
                progress->store(result.files.size(), std::memory_order_relaxed);
            }
            if (!entry.isDir() && sourceFile(path) && !ignored(path))
                result.watches.files.append(path);
        }
        result.files.sort(Qt::CaseInsensitive);
        result.directories.sort(Qt::CaseInsensitive);
        result.watches.files.sort(Qt::CaseInsensitive);
        result.watches.directories = result.directories;
        for (auto it = result.membership.begin(); it != result.membership.end(); ++it)
            it->sort(Qt::CaseSensitive);
        return result;
    }));
}

void WorkspaceManager::processDirectoryScanChunk()
{
    // Progress polling reads only an atomic counter. It never advances an IO iterator.
    if (directoryScanPending && scanProgress)
        emit workspaceScanProgress(scanningPath, scanProgress->load(std::memory_order_relaxed));
}

void WorkspaceManager::finishDirectoryScan(std::uint64_t generation,
                                           const QString& path)
{
    if (!directoryScanIsCurrent(generation, path))
        return;

    QPointer<WorkspaceManager> self(this);
    emit workspaceScanProgress(path, pendingScannedFiles.size());
    if (!self || !directoryScanIsCurrent(generation, path))
        return;

    auto paths = preparedWatchPaths;
    paths.files.append(semanticWatchPaths.files);
    paths.directories.append(semanticWatchPaths.directories);
    watcher.apply(paths, [self, generation, path] {
        if (self) self->publishDirectoryScan(generation, path);
    });
}

void WorkspaceManager::publishDirectoryScan(std::uint64_t generation,
                                            const QString& path)
{
    if (!directoryScanIsCurrent(generation, path))
        return;

    if (scanTimer)
        scanTimer->stop();
    directoryScanPending = false;
    scanningPath.clear();
    const QString finishedPath = path;
    const QStringList scannedFiles = pendingScannedFiles;
    scannedDirectories = pendingScannedDirectories;
    pendingScannedFiles.clear();
    pendingScannedDirectories.clear();
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
    if (scanCancellation)
        scanCancellation->store(true, std::memory_order_relaxed);
    pendingScanRequest.reset();
    scanProgress.reset();
    if (scanTimer)
        scanTimer->stop();
    directoryScanPending = false;
    pendingScannedFiles.clear();
    pendingScannedDirectories.clear();
    scanningPath.clear();
}

bool WorkspaceManager::directoryScanIsCurrent(
    std::uint64_t generation,
    const QString& path) const
{
    return generation == scanGeneration
        && directoryScanPending
        && !path.isEmpty()
        && scanningPath == path
        && workspacePath == path;
}

void WorkspaceManager::updateFileWatcher()
{
    if (!watcher.active() || !isWorkspaceOpen()) return;

    startDirectoryScan(workspacePath);
}

WorkspaceConfiguration WorkspaceManager::loadConfigurationForWorkspace(const QString& path)
{
    WorkspaceConfigurationService fallback;
    const auto loaded = (workspaceConfigurationService ? workspaceConfigurationService.get() : &fallback)->loadWithResult(path);
    if (!loaded.usable()) {
        ActivityLogService::getInstance()->append(QStringLiteral("Workspace"), ActivityLogLevel::Error, loaded.message);
        emit workspaceActivationFailed(path, loaded.message);
        return {};
    }
    // Validate the same ignore policy before changing active ownership.
    const auto ignored = WorkspaceIgnoreService::getInstance()->normalizeIgnoredDirectories(
        WorkspaceIgnoreQuery{loaded.configuration.workspaceRoot, loaded.configuration.ignoredDirs});
    if (!ignored.valid) {
        emit workspaceActivationFailed(path, ignored.failureReason);
        return {};
    }
    return loaded.configuration;
}

bool WorkspaceManager::applyWorkspaceConfiguration(
    const WorkspaceConfiguration& configuration,
    bool persist,
    QString* errorMessage,
    bool notify,
    const QStringList* activatingFiles,
    bool discoveryComplete)
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

    if (persist && workspaceConfigurationService) {
        const auto saved = workspaceConfigurationService->saveWithResult(clean);
        if (!saved.saved) {
            if (errorMessage) *errorMessage = saved.message;
            return false;
        }
        clean.storageRevision = saved.revision;
    }

    if (activatingFiles) {
        ProjectSnapshot workspace;
        workspace.sourceDiscoveryComplete = discoveryComplete;
        workspace.workspaceRoot = clean.workspaceRoot;
        workspace.allFiles = *activatingFiles;
        workspace.includeDirs = clean.includeDirs;
        workspace.defines = clean.defines;
        workspace.fileExtensions = clean.fileExtensions;
        workspace.topModule = clean.topModule;
        workspace.ignoredPaths = clean.ignoredDirs;
        projectModel->setWorkspaceState(workspace);
    } else {
        projectModel->setWorkspaceConfiguration(clean.includeDirs,
                                            clean.defines,
                                            clean.fileExtensions,
                                            clean.topModule,
                                            clean.ignoredDirs);
    }
    files.allFiles = projectModel->allFiles();
    files.systemVerilogFiles = projectModel->systemVerilogFiles();
    updateActiveEntryConfiguration(clean);
    if (notify) {
        // Activation applies configuration before installing the final watch
        // set in startFileWatching(). Registering here would immediately be
        // cleared and repeated, while workspacePath still names the old root.
        updateFileWatcher();
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
    entry.configurationRevision = configuration.storageRevision;
    entry.includeDirs = configuration.includeDirs;
    entry.defines = configuration.defines;
    entry.fileExtensions = configuration.fileExtensions;
    entry.topModule = configuration.topModule;
    entry.virtualSourceGroups =
        configuration.virtualSourceGroups;
}

bool WorkspaceManager::activateWorkspacePath(const QString& path,
                                             const QString& alias,
                                             int index,
                                             bool openedNewWorkspace,
                                             const WorkspaceConfiguration* prepared)
{
    const QString normalizedPath = normalizeWorkspacePath(path);
    if (normalizedPath.isEmpty() || alias.trimmed().isEmpty())
        return false;

    const auto configuration = prepared ? *prepared : loadConfigurationForWorkspace(normalizedPath);
    if (!configuration.isValid()) return false;

    const std::uint64_t requestedActivationGeneration =
        ++workspaceActivationGeneration;
    cancelDirectoryScan();
    stopFileWatching();
    scannedDirectories.clear();

    workspaceAlias = alias.trimmed();
    activeIndex = index;

    const QStringList activatingFiles = index >= 0 && index < workspaces.size()
        && workspaces.at(index).scanComplete ? workspaces.at(index).scannedFiles : QStringList{};
    if (projectModel && !applyWorkspaceConfiguration(configuration, false, nullptr,
                                                       false, &activatingFiles, false)) return false;

    workspacePath = projectModel ? projectModel->workspaceRoot() : normalizedPath;
    startFileWatching();

    const QString activatedPath = workspacePath;
    QPointer<WorkspaceManager> self(this);

    emit workspaceActivated(activeIndex, workspaceAlias, workspacePath);
    if (!self
        || workspaceActivationGeneration
               != requestedActivationGeneration
        || workspacePath != activatedPath || activeIndex != index) {
        return false;
    }
    if (openedNewWorkspace) {
        emit workspaceOpened(workspacePath);
        if (!self
            || workspaceActivationGeneration
                   != requestedActivationGeneration
            || workspacePath != activatedPath || activeIndex != index) {
            return false;
        }
    }
    // Cached file lists make activation immediate, but they cannot prove that
    // the directory remained unchanged while this workspace was inactive.
    // Reconcile every activation; finishDirectoryScan only republishes files
    // when the resulting set actually differs.
    const bool scanAlreadyStarted =
        directoryScanPending
        && scanningPath == activatedPath;
    if (!scanAlreadyStarted)
        startDirectoryScan(workspacePath);
    if (!self
        || workspaceActivationGeneration
               != requestedActivationGeneration
        || workspacePath != activatedPath
        || activeIndex != index) {
        return false;
    }
    return true;
}

void WorkspaceManager::loadRecentWorkspaces()
{
    recentWorkspaces.clear();

    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope,
                       QStringLiteral("ZeroSlack"), QStringLiteral("ZeroSlack"));
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

    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope,
                       QStringLiteral("ZeroSlack"), QStringLiteral("ZeroSlack"));
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
