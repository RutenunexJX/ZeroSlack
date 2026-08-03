#ifndef EXTERNALDOCUMENTSYNCCONTROLLER_H
#define EXTERNALDOCUMENTSYNCCONTROLLER_H

#include <QByteArray>
#include <QHash>
#include <QMetaObject>
#include <QObject>
#include <QString>

#include "shareddocument.h"

#include <cstdint>
#include <memory>

class QFileSystemWatcher;

enum class ExternalDocumentSyncOutcome {
    Untracked,
    Unchanged,
    Reloaded,
    Conflict,
    Unavailable
};

enum class ExternalDocumentConflictActionStatus {
    Applied,
    InvalidReview,
    StaleDocument,
    ExternalChanged,
    Unavailable,
    NotInConflict
};

struct ExternalDocumentSyncResult {
    ExternalDocumentSyncOutcome outcome =
        ExternalDocumentSyncOutcome::Untracked;
    QString fileName;
    QString failureReason;
};

struct ExternalDocumentConflictReview {
    bool valid = false;
    bool externalAvailable = false;
    QString documentId;
    QString fileName;
    QString localText;
    QString externalText;
    QByteArray externalFingerprint;
    std::uint64_t documentRevision = 0;
    QString failureReason;
};

struct ExternalDocumentConflictActionResult {
    ExternalDocumentConflictActionStatus status =
        ExternalDocumentConflictActionStatus::InvalidReview;
    ExternalDocumentConflictReview currentReview;
    QString failureReason;

    bool applied() const
    {
        return status
            == ExternalDocumentConflictActionStatus::Applied;
    }
};

class ExternalDocumentSyncController : public QObject
{
    Q_OBJECT

public:
    explicit ExternalDocumentSyncController(
        QObject* parent = nullptr);
    ~ExternalDocumentSyncController() override;

    void trackDocument(SharedDocument* document);
    void untrackDocument(SharedDocument* document);
    void noteDocumentSaved(SharedDocument* document);

    // This is the deterministic business-logic entry point. The
    // QFileSystemWatcher callbacks delegate to it, while tests can invoke it
    // immediately after changing a fixture without waiting for OS events.
    ExternalDocumentSyncResult processFileChange(
        const QString& fileName);

    // Captures the exact local revision and external generation used by a
    // conflict decision. Every mutating action re-reads the source and
    // rejects stale reviews without touching the shared document.
    ExternalDocumentConflictReview conflictReview(
        const QString& fileName);
    ExternalDocumentConflictActionResult validateConflictReview(
        const ExternalDocumentConflictReview& review);
    ExternalDocumentConflictActionResult
    validateConflictReviewForSaveAs(
        const ExternalDocumentConflictReview& review);
    ExternalDocumentConflictActionResult keepLocal(
        const ExternalDocumentConflictReview& review);
    ExternalDocumentConflictActionResult reloadExternal(
        const ExternalDocumentConflictReview& review);

    // Guards ordinary saves against a watcher event that has not yet been
    // delivered. A kept-local decision authorizes only the external
    // generation that was explicitly reviewed.
    bool canOverwriteDocument(
        SharedDocument* document,
        QString* failureReason = nullptr);

    bool isFileWatchedForTesting(
        const QString& fileName) const;
    QString watchedFilePathForTesting(
        const QString& fileName) const;
    bool isDirectoryWatchedForTesting(
        const QString& directory) const;

signals:
    void documentReloaded(SharedDocument* document,
                          const QString& fileName);
    void documentConflictDetected(
        SharedDocument* document,
        const QString& fileName);
    void documentUnavailable(SharedDocument* document,
                             const QString& fileName,
                             const QString& failureReason);

private slots:
    void handleFileChanged(const QString& path);
    void handleDirectoryChanged(const QString& path);

private:
    struct TrackedDocument {
        // Lexical path selected by the user, used for subscriptions and
        // UI-facing notifications.
        QString fileName;
        // EditorFileIdentity key used for disk-entity comparisons.
        QString pathKey;
        QString directory;
        QByteArray savedFingerprint;
        QByteArray observedFingerprint;
        QByteArray keptLocalFingerprint;
        bool diskAvailable = false;
        QMetaObject::Connection destroyedConnection;
    };

    struct DiskSnapshot {
        bool available = false;
        QString text;
        QByteArray fingerprint;
        QString failureReason;
    };

    std::unique_ptr<QFileSystemWatcher> watcher;
    QHash<SharedDocument*, TrackedDocument> trackedDocuments;

    static QString normalizedPath(const QString& path);
    static QString comparisonKey(const QString& path);
    static DiskSnapshot readDiskSnapshot(const QString& fileName);
    void refreshSubscriptions();
    void rearmFileSubscription(const QString& fileName);
    SharedDocument* documentForPath(
        const QString& fileName) const;
    ExternalDocumentConflictActionResult validateReview(
        const ExternalDocumentConflictReview& review,
        SharedDocument** document,
        DiskSnapshot* snapshot,
        bool allowUnavailableExternal = false);
};

#endif // EXTERNALDOCUMENTSYNCCONTROLLER_H
