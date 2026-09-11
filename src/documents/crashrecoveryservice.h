#ifndef CRASHRECOVERYSERVICE_H
#define CRASHRECOVERYSERVICE_H

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>

enum class CrashRecoveryStatus {
    Success,
    AlreadyClean,
    InvalidArgument,
    StorageUnavailable,
    NotFound,
    IoError,
    CorruptRecord,
    StaleRecord,
    WorkspaceMismatch,
    IdentityMismatch
};

enum class CrashRecoverySourceState {
    Untitled,
    Missing,
    Unreadable,
    BaselineUnavailable,
    UnchangedSinceBaseline,
    ExternallyModified
};

struct CrashRecoveryDocumentKey {
    QString workspacePath;
    QString originalFilePath;
    QString untitledDocumentId;
};

struct CrashRecoverySnapshotRequest {
    CrashRecoveryDocumentKey document;
    QString text;
    quint64 documentRevision = 0;
    QByteArray savedBaselineSha256;
    QDateTime savedBaselineModifiedUtc;
};

struct CrashRecoveryOperationResult {
    CrashRecoveryStatus status =
        CrashRecoveryStatus::InvalidArgument;
    QString reason;

    bool succeeded() const;
};

struct CrashRecoveryWriteResult :
    CrashRecoveryOperationResult {
    QString recoveryId;
    QString storagePath;
};

struct CrashRecoveryCandidate {
    QString recoveryId;
    QString storagePath;
    QString workspacePath;
    QString workspaceIdentity;
    QString originalFilePath;
    QString canonicalFileIdentity;
    QString untitledDocumentId;
    quint64 documentRevision = 0;
    qsizetype recoveredTextLength = 0;
    QByteArray recoveredTextSha256;
    QByteArray savedBaselineSha256;
    QDateTime savedBaselineModifiedUtc;
    QDateTime snapshotCreatedUtc;
    CrashRecoverySourceState sourceState =
        CrashRecoverySourceState::BaselineUnavailable;
    QByteArray currentSourceSha256;
    QDateTime currentSourceModifiedUtc;
    bool sourceExists = false;
    bool sourceReadable = false;
    bool sourceChangedSinceBaseline = false;
};

struct CrashRecoveryIsolatedRecord {
    QString originalStoragePath;
    QString quarantinePath;
    CrashRecoveryStatus status =
        CrashRecoveryStatus::CorruptRecord;
    QString reason;
};

struct CrashRecoveryListResult :
    CrashRecoveryOperationResult {
    QList<CrashRecoveryCandidate> candidates;
    QList<CrashRecoveryIsolatedRecord> isolatedRecords;
};

struct CrashRecoveryReadResult :
    CrashRecoveryOperationResult {
    CrashRecoveryCandidate candidate;
    QString recoveredText;
    QByteArray currentSourceBytes;
};

struct CrashRecoveryRecoverResult :
    CrashRecoveryOperationResult {
    CrashRecoveryCandidate candidate;
    QString text;
};

class CrashRecoveryService
{
public:
    static constexpr int kSchemaVersion = 1;

    explicit CrashRecoveryService(
        const QString& recoveryRoot = QString());

    QString recoveryRootPath() const;

    CrashRecoveryWriteResult writeSnapshot(
        const CrashRecoverySnapshotRequest& request) const;
    CrashRecoveryListResult listCandidates(
        const QString& workspacePath) const;
    CrashRecoveryReadResult readComparison(
        const QString& workspacePath,
        const QString& recoveryId) const;

    // Returns recovered text to the caller. It never opens the original file
    // for writing; applying the text to a document remains a UI-layer action.
    CrashRecoveryRecoverResult recoverText(
        const QString& workspacePath,
        const QString& recoveryId) const;

    CrashRecoveryOperationResult discard(
        const QString& workspacePath,
        const QString& recoveryId) const;
    CrashRecoveryOperationResult clearAfterNormalSave(
        const CrashRecoveryDocumentKey& document) const;
    CrashRecoveryOperationResult clearAfterNormalClose(
        const CrashRecoveryDocumentKey& document) const;

    static QByteArray sha256(const QByteArray& bytes);

private:
    struct ResolvedDocumentKey {
        QString workspacePath;
        QString workspaceIdentity;
        QString originalFilePath;
        QString canonicalFileIdentity;
        QString untitledDocumentId;
        QString recoveryId;
        QString failureReason;

        bool valid() const;
        bool untitled() const;
    };

    QString configuredRecoveryRoot;

    static QString normalizePath(
        const QString& path,
        bool canonicalizeExisting);
    static QString comparisonKey(
        const QString& path);
    static QString identityDigest(
        const QString& value);
    static ResolvedDocumentKey resolveDocumentKey(
        const CrashRecoveryDocumentKey& document);
    QString workspaceDirectory(
        const QString& workspaceIdentity) const;
    QString recordPath(
        const QString& workspaceIdentity,
        const QString& recoveryId) const;

    CrashRecoveryOperationResult clearDocument(
        const CrashRecoveryDocumentKey& document,
        const QString& successReason) const;
};

#endif // CRASHRECOVERYSERVICE_H
