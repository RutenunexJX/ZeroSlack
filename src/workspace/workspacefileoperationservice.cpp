#include "workspacefileoperationservice.h"
#include "filecreation.h"

#include "editorfileidentity.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include <filesystem>
#include <utility>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#undef CreateFile
#undef CreateDirectory
#else
#include <sys/stat.h>
#endif

namespace {
QString ensureTrailingSeparator(QString path)
{
    if (!path.endsWith(QLatin1Char('/')))
        path.append(QLatin1Char('/'));
    return path;
}

QString operationName(WorkspaceFileOperationKind kind)
{
    switch (kind) {
    case WorkspaceFileOperationKind::CreateFile:
        return QStringLiteral("Create file");
    case WorkspaceFileOperationKind::CreateDirectory:
        return QStringLiteral("Create directory");
    case WorkspaceFileOperationKind::Rename:
        return QStringLiteral("Rename");
    case WorkspaceFileOperationKind::Delete:
        return QStringLiteral("Move to Trash");
    case WorkspaceFileOperationKind::CopyFullPath:
        return QStringLiteral("Copy full path");
    case WorkspaceFileOperationKind::RevealInFileManager:
        return QStringLiteral("Reveal in file manager");
    }
    return QStringLiteral("File operation");
}

QString nativePath(const QString& path)
{
    return QDir::toNativeSeparators(path);
}

bool defaultTrashMover(const QString& sourcePath,
                       QString* recoveredPath,
                       QString* failureReason)
{
    QString trashPath;
    if (!QFile::moveToTrash(sourcePath, &trashPath)) {
        if (failureReason) {
            *failureReason = QStringLiteral(
                "The operating system did not move the selected path "
                "to its recoverable Trash/Recycle Bin. No permanent "
                "delete was attempted.");
        }
        return false;
    }
    if (recoveredPath)
        *recoveredPath = trashPath;
    if (failureReason)
        failureReason->clear();
    return true;
}

QByteArray objectId(const QString& path)
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
#ifdef Q_OS_WIN
    const HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return {};
    BY_HANDLE_FILE_INFORMATION info{};
    const bool available = GetFileInformationByHandle(handle, &info);
    CloseHandle(handle);
    if (!available) return {};
    stream << quint32(info.dwVolumeSerialNumber) << quint32(info.nFileIndexHigh) << quint32(info.nFileIndexLow);
#else
    struct stat info{};
    if (::stat(QFile::encodeName(path).constData(), &info) != 0) return {};
    stream << quint64(info.st_dev) << quint64(info.st_ino);
#endif
    return bytes;
}

bool sameObject(const WorkspaceFileSnapshot& a, const WorkspaceFileSnapshot& b)
{
    return a.identityKey == b.identityKey && a.objectId == b.objectId
        && a.exists == b.exists && a.directory == b.directory;
}

QString revisionTokenForPlan(WorkspaceFileOperationPlan& plan)
{
    plan.workspaceSnapshot = WorkspaceFileOperationService::snapshot(plan.workspaceRoot, false);
    plan.parentSnapshot = WorkspaceFileOperationService::snapshot(
        QFileInfo(plan.sourcePath.isEmpty() ? plan.targetPath : plan.sourcePath).absolutePath(), false);
    plan.targetSnapshot = WorkspaceFileOperationService::snapshot(plan.targetPath, false);
    QByteArray serialized;
    QDataStream stream(&serialized, QIODevice::WriteOnly);
    const auto writeSnapshot = [&stream](
                                   const WorkspaceFileSnapshot& snapshot) {
        stream << snapshot.identityKey
               << snapshot.objectId
               << snapshot.exists
               << snapshot.directory
               << snapshot.size
               << snapshot.modifiedUtc
               << snapshot.contentDigest;
    };

    stream << static_cast<qint32>(plan.kind)
           << plan.workspaceRoot << plan.sourcePath << plan.targetPath
           << plan.workspaceSnapshot.identityKey << plan.workspaceSnapshot.objectId
           << plan.parentSnapshot.identityKey << plan.parentSnapshot.objectId
           << plan.sourceDirectory;
    writeSnapshot(plan.sourceSnapshot);
    writeSnapshot(plan.targetSnapshot);
    for (auto it = plan.sourceEntries.cbegin(); it != plan.sourceEntries.cend(); ++it) {
        stream << it.key();
        writeSnapshot(it.value());
    }

    return QString::fromLatin1(
        QCryptographicHash::hash(
            serialized,
            QCryptographicHash::Sha256)
            .toHex());
}
}

bool WorkspaceFileSnapshot::operator==(
    const WorkspaceFileSnapshot& other) const
{
    return identityKey == other.identityKey
        && objectId == other.objectId
        && exists == other.exists
        && directory == other.directory
        && size == other.size
        && modifiedUtc == other.modifiedUtc
        && contentDigest == other.contentDigest;
}

WorkspaceFileOperationService::WorkspaceFileOperationService()
    : trashMover(defaultTrashMover)
{
}

WorkspaceFileOperationPlan
WorkspaceFileOperationService::planCreateFile(
    const QString& workspaceRoot,
    const QString& parentDirectory,
    const QString& leafName) const
{
    return planCreate(WorkspaceFileOperationKind::CreateFile,
                      workspaceRoot,
                      parentDirectory,
                      leafName);
}

WorkspaceFileOperationPlan
WorkspaceFileOperationService::planCreateDirectory(
    const QString& workspaceRoot,
    const QString& parentDirectory,
    const QString& leafName) const
{
    return planCreate(WorkspaceFileOperationKind::CreateDirectory,
                      workspaceRoot,
                      parentDirectory,
                      leafName);
}

WorkspaceFileOperationPlan
WorkspaceFileOperationService::planRename(
    const QString& workspaceRoot,
    const QString& sourcePath,
    const QString& leafName) const
{
    WorkspaceFileOperationPlan plan =
        planExistingPath(WorkspaceFileOperationKind::Rename,
                         workspaceRoot,
                         sourcePath);
    if (!plan.valid)
        return plan;

    QString nameFailure;
    if (!validLeafName(leafName, &nameFailure)) {
        plan.valid = false;
        plan.failureReason = nameFailure;
        return plan;
    }

    const QString targetPath =
        EditorFileIdentity::normalized(
            QFileInfo(plan.sourcePath)
                .dir()
                .absoluteFilePath(leafName));
    if (!pathIsInsideWorkspace(
            plan.workspaceRoot, targetPath, false)) {
        plan.valid = false;
        plan.failureReason = QStringLiteral(
            "The rename target resolves outside the active workspace.");
        return plan;
    }

    const bool sameIdentity =
        EditorFileIdentity::same(plan.sourcePath, targetPath);
    if (sameIdentity
        && plan.sourcePath == targetPath) {
        plan.valid = false;
        plan.failureReason =
            QStringLiteral("The source and target paths are identical.");
        return plan;
    }
    if (QFileInfo::exists(targetPath) && !sameIdentity) {
        plan.valid = false;
        plan.failureReason =
            QStringLiteral("The rename target already exists.");
        return plan;
    }

    plan.targetPath = targetPath;
    plan.preview =
        QStringLiteral("%1\n\nFrom:\n%2\n\nTo:\n%3")
            .arg(operationName(plan.kind),
                 nativePath(plan.sourcePath),
                 nativePath(plan.targetPath));
    plan.revisionToken = revisionTokenForPlan(plan);
    return plan;
}

WorkspaceFileOperationPlan
WorkspaceFileOperationService::planDelete(
    const QString& workspaceRoot,
    const QString& sourcePath) const
{
    WorkspaceFileOperationPlan plan =
        planExistingPath(WorkspaceFileOperationKind::Delete,
                         workspaceRoot,
                         sourcePath);
    if (!plan.valid)
        return plan;
    plan.recoverable = true;
    plan.preview =
        QStringLiteral(
            "%1\n\nPath:\n%2\n\n"
            "This operation requires operating-system Trash/Recycle Bin "
            "support. ZeroSlack will not fall back to permanent deletion.")
            .arg(operationName(plan.kind),
                 nativePath(plan.sourcePath));
    plan.revisionToken = revisionTokenForPlan(plan);
    return plan;
}

WorkspaceFileOperationPlan
WorkspaceFileOperationService::planCopyFullPath(
    const QString& workspaceRoot,
    const QString& sourcePath) const
{
    WorkspaceFileOperationPlan plan =
        planExistingPath(
            WorkspaceFileOperationKind::CopyFullPath,
            workspaceRoot,
            sourcePath);
    if (plan.valid) {
        plan.preview =
            QStringLiteral("%1\n\n%2")
                .arg(operationName(plan.kind),
                     nativePath(plan.sourcePath));
        plan.revisionToken = revisionTokenForPlan(plan);
    }
    return plan;
}

WorkspaceFileOperationPlan
WorkspaceFileOperationService::planReveal(
    const QString& workspaceRoot,
    const QString& sourcePath) const
{
    WorkspaceFileOperationPlan plan =
        planExistingPath(
            WorkspaceFileOperationKind::RevealInFileManager,
            workspaceRoot,
            sourcePath);
    if (plan.valid) {
        plan.preview =
            QStringLiteral("%1\n\n%2")
                .arg(operationName(plan.kind),
                     nativePath(plan.sourcePath));
        plan.revisionToken = revisionTokenForPlan(plan);
    }
    return plan;
}

WorkspaceFileOperationPlan
WorkspaceFileOperationService::replan(
    const WorkspaceFileOperationPlan& plan) const
{
    WorkspaceFileOperationPlan current;
    switch (plan.kind) {
    case WorkspaceFileOperationKind::CreateFile:
        current = planCreateFile(
            plan.workspaceRoot,
            QFileInfo(plan.targetPath).dir().absolutePath(),
            QFileInfo(plan.targetPath).fileName());
        break;
    case WorkspaceFileOperationKind::CreateDirectory:
        current = planCreateDirectory(
            plan.workspaceRoot,
            QFileInfo(plan.targetPath).dir().absolutePath(),
            QFileInfo(plan.targetPath).fileName());
        break;
    case WorkspaceFileOperationKind::Rename:
        current = planRename(
            plan.workspaceRoot,
            plan.sourcePath,
            QFileInfo(plan.targetPath).fileName());
        break;
    case WorkspaceFileOperationKind::Delete:
        current = planDelete(plan.workspaceRoot,
                             plan.sourcePath);
        break;
    case WorkspaceFileOperationKind::CopyFullPath:
        current = planCopyFullPath(plan.workspaceRoot,
                                   plan.sourcePath);
        break;
    case WorkspaceFileOperationKind::RevealInFileManager:
        current = planReveal(plan.workspaceRoot,
                             plan.sourcePath);
        break;
    }
    return current;
}

bool WorkspaceFileOperationService::revalidate(WorkspaceFileOperationPlan* plan,
    const WorkspaceFileSnapshot* saved, QString* failureReason) const
{
    const auto fail = [&](const QString& reason) {
        if (failureReason) *failureReason = reason;
        return false;
    };
    if (!plan || !plan->valid) return fail(QStringLiteral("The operation plan is invalid."));
    auto current = replan(*plan);
    if (!current.valid) return fail(current.failureReason);
    if (current.revisionToken == plan->revisionToken) return true;
    const QString stale = QStringLiteral("The selected path changed after confirmation. Review a new plan before applying it.");
    if (!saved || saved->contentDigest.isEmpty()
        || !sameObject(plan->workspaceSnapshot, current.workspaceSnapshot)
        || !sameObject(plan->parentSnapshot, current.parentSnapshot)
        || plan->sourceEntries.keys() != current.sourceEntries.keys()) return fail(stale);
    const auto allowed = [&](const WorkspaceFileSnapshot& before, const WorkspaceFileSnapshot& after) {
        if (before == after) return true;
        if (before.identityKey == saved->identityKey && after == *saved) return true;
        // Atomic replacement can change ancestor directory timestamps, but not
        // their identity, membership, or any other file's generation.
        return before.directory && sameObject(before, after)
            && saved->identityKey.startsWith(ensureTrailingSeparator(before.identityKey));
    };
    if (!allowed(plan->sourceSnapshot, current.sourceSnapshot)) return fail(stale);
    for (auto it = plan->sourceEntries.cbegin(); it != plan->sourceEntries.cend(); ++it)
        if (!allowed(it.value(), current.sourceEntries.value(it.key()))) return fail(stale);
    if (plan->targetSnapshot != current.targetSnapshot) {
        // Case-only rename: the target is the same reviewed source object.
        auto target = current.targetSnapshot;
        target.contentDigest = saved->contentDigest;
        if (plan->targetSnapshot.identityKey != saved->identityKey || target != *saved) return fail(stale);
    }
    *plan = std::move(current);
    return true;
}

WorkspaceFileOperationResult
WorkspaceFileOperationService::apply(const WorkspaceFileOperationPlan& plan) const
{
    if (!plan.valid)
        return failed(plan.failureReason.isEmpty()
                          ? QStringLiteral("The operation plan is invalid.") : plan.failureReason);
    const auto current = replan(plan);
    if (!current.valid)
        return failed(current.failureReason);
    if ((!plan.revisionToken.isEmpty()
         && current.revisionToken
                != plan.revisionToken)
        || (plan.revisionToken.isEmpty()
            && !plan.sourcePath.isEmpty()
            && plan.sourceSnapshot
                   != snapshot(plan.sourcePath))) {
        return failed(QStringLiteral(
            "The selected path changed after the operation was planned."));
    }

    WorkspaceFileOperationResult result;
    switch (plan.kind) {
    case WorkspaceFileOperationKind::CreateFile: {
        const auto created = FileCreation::create(plan.targetPath, {});
        if (!created.created())
            return failed(created.message);
        result.path = plan.targetPath;
        break;
    }
    case WorkspaceFileOperationKind::CreateDirectory:
        if (!QDir().mkdir(plan.targetPath)) {
            return failed(QStringLiteral(
                "Cannot create the directory."));
        }
        result.path = plan.targetPath;
        break;
    case WorkspaceFileOperationKind::Rename: {
        const QFileInfo sourceInfo(plan.sourcePath);
        QDir parent(sourceInfo.dir());
        if (!parent.rename(
                sourceInfo.fileName(),
                QFileInfo(plan.targetPath).fileName())) {
            return failed(QStringLiteral(
                "The path could not be renamed atomically."));
        }
        result.path = plan.targetPath;
        break;
    }
    case WorkspaceFileOperationKind::Delete: {
        if (!trashMover) {
            return failed(QStringLiteral(
                "Recoverable deletion is unavailable. "
                "No permanent delete was attempted."));
        }
        QString failure;
        if (!trashMover(plan.sourcePath,
                        &result.recoveredPath,
                        &failure)) {
            return failed(
                failure.isEmpty()
                    ? QStringLiteral(
                          "The selected path was not moved to Trash. "
                          "No permanent delete was attempted.")
                    : failure);
        }
        result.path = plan.sourcePath;
        break;
    }
    case WorkspaceFileOperationKind::CopyFullPath:
    case WorkspaceFileOperationKind::RevealInFileManager:
        result.path = plan.sourcePath;
        break;
    }
    result.succeeded = true;
    return result;
}

void WorkspaceFileOperationService::setTrashMoverForTesting(
    TrashMover mover)
{
    trashMover = std::move(mover);
}

bool WorkspaceFileOperationService::pathIsInsideWorkspace(
    const QString& workspaceRoot,
    const QString& candidatePath,
    bool allowWorkspaceRoot)
{
    const QString rootKey =
        EditorFileIdentity::lookupKey(workspaceRoot);
    const QString candidateKey =
        EditorFileIdentity::lookupKey(candidatePath);
    if (rootKey.isEmpty() || candidateKey.isEmpty())
        return false;
    if (candidateKey == rootKey)
        return allowWorkspaceRoot;
    return candidateKey.startsWith(
        ensureTrailingSeparator(rootKey));
}

WorkspaceFileSnapshot
WorkspaceFileOperationService::snapshot(
    const QString& path,
    bool includeContentDigest)
{
    WorkspaceFileSnapshot result;
    const QFileInfo info(path);
    result.identityKey =
        EditorFileIdentity::lookupKey(path);
    if (path.isEmpty()) return result;
    result.exists = info.exists();
    result.directory = info.isDir();
    if (result.exists) result.objectId = objectId(info.absoluteFilePath());
    result.size = info.isFile() ? info.size() : -1;
    result.modifiedUtc =
        info.lastModified().toUTC();
    if (includeContentDigest && info.isFile()) {
        QFile file(info.absoluteFilePath());
        if (file.open(QIODevice::ReadOnly)) {
            QCryptographicHash digest(
                QCryptographicHash::Sha256);
            bool readSucceeded = true;
            while (!file.atEnd()) {
                const QByteArray block =
                    file.read(256 * 1024);
                if (block.isEmpty()
                    && file.error()
                           != QFileDevice::NoError) {
                    readSucceeded = false;
                    break;
                }
                digest.addData(block);
            }
            if (readSucceeded)
                result.contentDigest = digest.result();
        }
    }
    return result;
}

WorkspaceFileOperationPlan
WorkspaceFileOperationService::planCreate(
    WorkspaceFileOperationKind kind,
    const QString& workspaceRoot,
    const QString& parentDirectory,
    const QString& leafName) const
{
    WorkspaceFileOperationPlan plan;
    plan.kind = kind;
    plan.workspaceRoot =
        EditorFileIdentity::normalized(workspaceRoot);

    const QFileInfo rootInfo(plan.workspaceRoot);
    if (!rootInfo.exists() || !rootInfo.isDir()) {
        plan.failureReason =
            QStringLiteral("The active workspace root is unavailable.");
        return plan;
    }

    const QString parent =
        EditorFileIdentity::normalized(parentDirectory);
    const QFileInfo parentInfo(parent);
    if (!parentInfo.exists() || !parentInfo.isDir()
        || !pathIsInsideWorkspace(
               plan.workspaceRoot, parent, true)) {
        plan.failureReason = QStringLiteral(
            "The selected parent directory is outside or unavailable "
            "in the active workspace.");
        return plan;
    }

    QString nameFailure;
    if (!validLeafName(leafName, &nameFailure)) {
        plan.failureReason = nameFailure;
        return plan;
    }

    plan.targetPath =
        EditorFileIdentity::normalized(
            QDir(parent).absoluteFilePath(leafName));
    if (!pathIsInsideWorkspace(
            plan.workspaceRoot,
            plan.targetPath,
            false)) {
        plan.failureReason = QStringLiteral(
            "The target path resolves outside the active workspace.");
        return plan;
    }
    if (QFileInfo::exists(plan.targetPath)) {
        plan.failureReason =
            QStringLiteral("The target path already exists.");
        return plan;
    }

    plan.valid = true;
    plan.preview =
        QStringLiteral("%1\n\nPath:\n%2")
            .arg(operationName(kind),
                 nativePath(plan.targetPath));
    plan.revisionToken = revisionTokenForPlan(plan);
    return plan;
}

WorkspaceFileOperationPlan
WorkspaceFileOperationService::planExistingPath(
    WorkspaceFileOperationKind kind,
    const QString& workspaceRoot,
    const QString& sourcePath) const
{
    WorkspaceFileOperationPlan plan;
    plan.kind = kind;
    plan.workspaceRoot =
        EditorFileIdentity::normalized(workspaceRoot);
    plan.sourcePath =
        EditorFileIdentity::normalized(sourcePath);

    const QFileInfo rootInfo(plan.workspaceRoot);
    if (!rootInfo.exists() || !rootInfo.isDir()) {
        plan.failureReason =
            QStringLiteral("The active workspace root is unavailable.");
        return plan;
    }
    if (!pathIsInsideWorkspace(
            plan.workspaceRoot,
            plan.sourcePath,
            true)) {
        plan.failureReason = QStringLiteral(
            "The selected path resolves outside the active workspace.");
        return plan;
    }
    if (EditorFileIdentity::same(
            plan.workspaceRoot,
            plan.sourcePath)
        && (kind == WorkspaceFileOperationKind::Rename
            || kind == WorkspaceFileOperationKind::Delete)) {
        plan.failureReason = QStringLiteral(
            "The workspace root cannot be renamed or deleted "
            "from the file tree.");
        return plan;
    }

    const QFileInfo sourceInfo(plan.sourcePath);
    if (!sourceInfo.exists()) {
        plan.failureReason =
            QStringLiteral("The selected path no longer exists.");
        return plan;
    }

    plan.sourceDirectory = sourceInfo.isDir();
    const bool mutationRequiresContentRevision =
        kind == WorkspaceFileOperationKind::Rename
        || kind == WorkspaceFileOperationKind::Delete;
    plan.sourceSnapshot = snapshot(
        plan.sourcePath,
        mutationRequiresContentRevision);
    if (mutationRequiresContentRevision) {
        plan.sourceEntries.insert(QString(), plan.sourceSnapshot);
        if (plan.sourceDirectory) {
            // QDirIterator treats an enumeration error like end-of-directory.
            // Confirmation requires a complete tree, including every descent.
#ifdef Q_OS_WIN
            const std::filesystem::path source(plan.sourcePath.toStdWString());
#else
            const std::filesystem::path source(QFile::encodeName(plan.sourcePath).constData());
#endif
            std::error_code error;
            std::filesystem::recursive_directory_iterator entries(source, error), end;
            while (!error && entries != end) {
#ifdef Q_OS_WIN
                const auto child = QString::fromStdWString(entries->path().native());
#else
                const auto child = QFile::decodeName(entries->path().native().c_str());
#endif
                plan.sourceEntries.insert(QDir(plan.sourcePath).relativeFilePath(child), snapshot(child));
                entries.increment(error);
            }
            if (error) {
                plan.failureReason = QStringLiteral("The selected directory could not be enumerated completely: %1")
                    .arg(QString::fromStdString(error.message()));
                return plan;
            }
        }
        for (const auto& entry : std::as_const(plan.sourceEntries)) {
            if (!entry.exists || entry.objectId.isEmpty() || (!entry.directory && entry.contentDigest.isEmpty())) {
                plan.failureReason = QStringLiteral("The selected path could not be read completely. Review it again.");
                return plan;
            }
        }
    }
    plan.valid = true;
    return plan;
}

bool WorkspaceFileOperationService::validLeafName(
    const QString& leafName,
    QString* failureReason)
{
    const QString name = leafName.trimmed();
    if (name.isEmpty()
        || name != leafName
        || name == QStringLiteral(".")
        || name == QStringLiteral("..")
        || name.contains(QLatin1Char('/'))
        || name.contains(QLatin1Char('\\'))
        || name.contains(QChar::Null)) {
        if (failureReason) {
            *failureReason = QStringLiteral(
                "Enter one non-empty file or directory name, "
                "without path separators or leading/trailing spaces.");
        }
        return false;
    }
#ifdef Q_OS_WIN
    static const QString invalid =
        QStringLiteral("<>:\"|?*");
    for (const QChar character : invalid) {
        if (name.contains(character)) {
            if (failureReason) {
                *failureReason = QStringLiteral(
                    "The name contains a character that Windows "
                    "does not permit.");
            }
            return false;
        }
    }
    const QString stem =
        name.section(QLatin1Char('.'), 0, 0)
            .toCaseFolded();
    static const QStringList reserved = {
        QStringLiteral("con"),
        QStringLiteral("prn"),
        QStringLiteral("aux"),
        QStringLiteral("nul"),
        QStringLiteral("com1"),
        QStringLiteral("com2"),
        QStringLiteral("com3"),
        QStringLiteral("com4"),
        QStringLiteral("com5"),
        QStringLiteral("com6"),
        QStringLiteral("com7"),
        QStringLiteral("com8"),
        QStringLiteral("com9"),
        QStringLiteral("lpt1"),
        QStringLiteral("lpt2"),
        QStringLiteral("lpt3"),
        QStringLiteral("lpt4"),
        QStringLiteral("lpt5"),
        QStringLiteral("lpt6"),
        QStringLiteral("lpt7"),
        QStringLiteral("lpt8"),
        QStringLiteral("lpt9"),
    };
    if (reserved.contains(stem)
        || name.endsWith(QLatin1Char('.'))) {
        if (failureReason) {
            *failureReason =
                QStringLiteral("The name is reserved by Windows.");
        }
        return false;
    }
#endif
    if (failureReason)
        failureReason->clear();
    return true;
}

WorkspaceFileOperationResult
WorkspaceFileOperationService::failed(
    const QString& reason)
{
    WorkspaceFileOperationResult result;
    result.failureReason = reason;
    return result;
}
