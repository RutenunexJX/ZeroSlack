#ifndef WORKSPACEFILEOPERATIONSERVICE_H
#define WORKSPACEFILEOPERATIONSERVICE_H

#include <QByteArray>
#include <QDateTime>
#include <QString>

#include <functional>

enum class WorkspaceFileOperationKind {
    CreateFile,
    CreateDirectory,
    Rename,
    Delete,
    CopyFullPath,
    RevealInFileManager
};

struct WorkspaceFileSnapshot {
    QString identityKey;
    bool exists = false;
    bool directory = false;
    qint64 size = -1;
    QDateTime modifiedUtc;
    QByteArray contentDigest;

    bool operator==(const WorkspaceFileSnapshot& other) const;
    bool operator!=(const WorkspaceFileSnapshot& other) const
    {
        return !(*this == other);
    }
};

struct WorkspaceFileOperationPlan {
    WorkspaceFileOperationKind kind =
        WorkspaceFileOperationKind::CopyFullPath;
    QString workspaceRoot;
    QString sourcePath;
    QString targetPath;
    bool sourceDirectory = false;
    bool valid = false;
    bool recoverable = false;
    QString preview;
    QString failureReason;
    WorkspaceFileSnapshot sourceSnapshot;
    QString revisionToken;
};

struct WorkspaceFileOperationResult {
    bool succeeded = false;
    QString path;
    QString recoveredPath;
    QString failureReason;
};

class WorkspaceFileOperationService
{
public:
    using TrashMover = std::function<bool(
        const QString& sourcePath,
        QString* recoveredPath,
        QString* failureReason)>;

    WorkspaceFileOperationService();

    WorkspaceFileOperationPlan planCreateFile(
        const QString& workspaceRoot,
        const QString& parentDirectory,
        const QString& leafName) const;
    WorkspaceFileOperationPlan planCreateDirectory(
        const QString& workspaceRoot,
        const QString& parentDirectory,
        const QString& leafName) const;
    WorkspaceFileOperationPlan planRename(
        const QString& workspaceRoot,
        const QString& sourcePath,
        const QString& leafName) const;
    WorkspaceFileOperationPlan planDelete(
        const QString& workspaceRoot,
        const QString& sourcePath) const;
    WorkspaceFileOperationPlan planCopyFullPath(
        const QString& workspaceRoot,
        const QString& sourcePath) const;
    WorkspaceFileOperationPlan planReveal(
        const QString& workspaceRoot,
        const QString& sourcePath) const;

    WorkspaceFileOperationResult apply(
        const WorkspaceFileOperationPlan& plan) const;

    void setTrashMoverForTesting(TrashMover mover);

    static bool pathIsInsideWorkspace(
        const QString& workspaceRoot,
        const QString& candidatePath,
        bool allowWorkspaceRoot = true);
    static WorkspaceFileSnapshot snapshot(
        const QString& path,
        bool includeContentDigest = true);

private:
    TrashMover trashMover;

    WorkspaceFileOperationPlan planCreate(
        WorkspaceFileOperationKind kind,
        const QString& workspaceRoot,
        const QString& parentDirectory,
        const QString& leafName) const;
    WorkspaceFileOperationPlan planExistingPath(
        WorkspaceFileOperationKind kind,
        const QString& workspaceRoot,
        const QString& sourcePath) const;
    static bool validLeafName(
        const QString& leafName,
        QString* failureReason);
    static WorkspaceFileOperationResult failed(
        const QString& reason);
};

#endif // WORKSPACEFILEOPERATIONSERVICE_H
