#include "workspacefileoperationservice.h"

#include "editorfileidentity.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringList>

#include <utility>

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

QString revisionTokenForPlan(
    const WorkspaceFileOperationPlan& plan)
{
    QByteArray serialized;
    QDataStream stream(&serialized, QIODevice::WriteOnly);
    const auto writeSnapshot = [&stream](
                                   const WorkspaceFileSnapshot& snapshot) {
        stream << snapshot.identityKey
               << snapshot.exists
               << snapshot.directory
               << snapshot.size
               << snapshot.modifiedUtc
               << snapshot.contentDigest;
    };

    stream << static_cast<qint32>(plan.kind)
           << EditorFileIdentity::lookupKey(plan.workspaceRoot)
           << EditorFileIdentity::lookupKey(plan.sourcePath)
           << EditorFileIdentity::lookupKey(plan.targetPath)
           << plan.sourceDirectory;
    writeSnapshot(plan.sourceSnapshot);
    writeSnapshot(
        WorkspaceFileOperationService::snapshot(
            plan.targetPath,
            false));

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

WorkspaceFileOperationResult
WorkspaceFileOperationService::apply(
    const WorkspaceFileOperationPlan& plan) const
{
    if (!plan.valid)
        return failed(plan.failureReason.isEmpty()
                          ? QStringLiteral("The operation plan is invalid.")
                          : plan.failureReason);

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
        QSaveFile file(plan.targetPath);
        if (!file.open(QIODevice::WriteOnly)) {
            return failed(
                QStringLiteral("Cannot create file: %1")
                    .arg(file.errorString()));
        }
        if (!file.commit()) {
            return failed(
                QStringLiteral("Cannot atomically create file: %1")
                    .arg(file.errorString()));
        }
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
    result.exists = info.exists();
    result.directory = info.isDir();
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
