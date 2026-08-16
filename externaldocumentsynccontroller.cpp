#include "externaldocumentsynccontroller.h"

#include "editorfileidentity.h"
#include "shareddocument.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QSet>
#include <QStringList>
#include <QTextDocument>
#include <QTextStream>

#include <utility>

namespace {
QByteArray textFingerprint(const QString& text)
{
    return QCryptographicHash::hash(
        text.toUtf8(),
        QCryptographicHash::Sha256);
}
}

ExternalDocumentSyncController::ExternalDocumentSyncController(
    QObject* parent)
    : QObject(parent)
    , watcher(std::make_unique<QFileSystemWatcher>(this))
{
    connect(watcher.get(),
            &QFileSystemWatcher::fileChanged,
            this,
            &ExternalDocumentSyncController::handleFileChanged);
    connect(watcher.get(),
            &QFileSystemWatcher::directoryChanged,
            this,
            &ExternalDocumentSyncController::handleDirectoryChanged);
}

ExternalDocumentSyncController::~ExternalDocumentSyncController() = default;

void ExternalDocumentSyncController::trackDocument(
    SharedDocument* document)
{
    if (!document)
        return;
    const QString fileName =
        normalizedPath(document->fileName());
    if (fileName.isEmpty()) {
        untrackDocument(document);
        return;
    }

    auto found = trackedDocuments.find(document);
    const QString nextPathKey = comparisonKey(fileName);
    if (found != trackedDocuments.end()
        && found->pathKey == nextPathKey) {
        // Identity and presentation are intentionally separate. A document
        // can move from a real path to a symlink or junction spelling
        // without becoming a different disk entity. In that case retain
        // every generation fingerprint, but make watcher subscriptions and
        // subsequent signals follow the latest lexical path.
        if (found->fileName != fileName) {
            found->fileName = fileName;
            found->directory =
                normalizedPath(
                    QFileInfo(fileName).absolutePath());
            if (QFileInfo(fileName).isFile()) {
                document->setReadOnly(
                    !QFileInfo(fileName).isWritable());
            }
        }
        refreshSubscriptions();
        return;
    }

    TrackedDocument tracked;
    if (found != trackedDocuments.end()) {
        tracked.destroyedConnection =
            found->destroyedConnection;
    } else {
        tracked.destroyedConnection = connect(
            document,
            &QObject::destroyed,
            this,
            [this, document]() {
                trackedDocuments.remove(document);
                refreshSubscriptions();
            });
    }
    tracked.fileName = fileName;
    tracked.pathKey = nextPathKey;
    tracked.directory =
        normalizedPath(QFileInfo(fileName).absolutePath());
    tracked.savedFingerprint = textFingerprint(
        document->textDocument()
            ? document->textDocument()->toPlainText()
            : QString());
    tracked.observedFingerprint =
        tracked.savedFingerprint;
    const DiskSnapshot snapshot =
        readDiskSnapshot(fileName);
    // The first deterministic probe should publish an unavailable transition
    // if the path disappeared between opening and subscription.
    tracked.diskAvailable = true;
    if (snapshot.available) {
        document->setReadOnly(!QFileInfo(fileName).isWritable());
    }
    trackedDocuments.insert(document, std::move(tracked));
    refreshSubscriptions();
    processFileChange(fileName);
}

void ExternalDocumentSyncController::untrackDocument(
    SharedDocument* document)
{
    auto found = trackedDocuments.find(document);
    if (found == trackedDocuments.end())
        return;
    QObject::disconnect(found->destroyedConnection);
    trackedDocuments.erase(found);
    refreshSubscriptions();
}

void ExternalDocumentSyncController::noteDocumentSaved(
    SharedDocument* document)
{
    if (!document)
        return;
    trackDocument(document);
    auto found = trackedDocuments.find(document);
    if (found == trackedDocuments.end())
        return;

    const DiskSnapshot snapshot =
        readDiskSnapshot(found->fileName);
    if (!snapshot.available) {
        refreshSubscriptions();
        return;
    }
    found->savedFingerprint = snapshot.fingerprint;
    found->observedFingerprint = snapshot.fingerprint;
    found->keptLocalFingerprint.clear();
    found->diskAvailable = true;
    document->setReadOnly(
        !QFileInfo(found->fileName).isWritable());
    document->setExternalState(
        SharedDocumentExternalState::Current);
    rearmFileSubscription(found->fileName);
}

void ExternalDocumentSyncController::noteDocumentSaved(
    SharedDocument* document,
    const QByteArray& fingerprint)
{
    if (!document || fingerprint.size() != 32) {
        noteDocumentSaved(document);
        return;
    }
    trackDocument(document);
    auto found = trackedDocuments.find(document);
    if (found == trackedDocuments.end())
        return;
    const QFileInfo source(found->fileName);
    if (!source.isFile()) {
        refreshSubscriptions();
        return;
    }
    found->savedFingerprint = fingerprint;
    found->observedFingerprint = fingerprint;
    found->keptLocalFingerprint.clear();
    found->diskAvailable = true;
    document->setReadOnly(!source.isWritable());
    document->setExternalState(
        SharedDocumentExternalState::Current);
    rearmFileSubscription(found->fileName);
}

ExternalDocumentSyncResult
ExternalDocumentSyncController::processFileChange(
    const QString& requestedFileName)
{
    ExternalDocumentSyncResult result;
    result.fileName = normalizedPath(requestedFileName);
    SharedDocument* document =
        documentForPath(result.fileName);
    if (!document)
        return result;

    auto found = trackedDocuments.find(document);
    if (found == trackedDocuments.end())
        return result;
    result.fileName = found->fileName;

    const DiskSnapshot snapshot =
        readDiskSnapshot(found->fileName);
    if (!snapshot.available) {
        const bool alreadyUnavailable =
            !found->diskAvailable;
        found->diskAvailable = false;
        found->keptLocalFingerprint.clear();
        result.outcome =
            ExternalDocumentSyncOutcome::Unavailable;
        result.failureReason = snapshot.failureReason;
        const bool wasConflict =
            document->externalState()
            == SharedDocumentExternalState::Conflict;
        if (document->dirty()
            || wasConflict) {
            document->setExternalState(
                SharedDocumentExternalState::Conflict);
        } else {
            document->setExternalState(
                SharedDocumentExternalState::ExternallyModified);
        }
        refreshSubscriptions();
        if (!alreadyUnavailable) {
            emit documentUnavailable(
                document,
                found->fileName,
                snapshot.failureReason);
            if (!wasConflict
                && document->externalState()
                       == SharedDocumentExternalState::Conflict) {
                emit documentConflictDetected(
                    document,
                    found->fileName);
            }
        }
        return result;
    }

    found->diskAvailable = true;
    document->setReadOnly(
        !QFileInfo(found->fileName).isWritable());
    if (snapshot.fingerprint
        == found->observedFingerprint) {
        if (document->externalState()
                == SharedDocumentExternalState::ExternallyModified
            && snapshot.fingerprint
                   == found->savedFingerprint) {
            document->setExternalState(
                SharedDocumentExternalState::Current);
            found->keptLocalFingerprint.clear();
        }
        result.outcome =
            ExternalDocumentSyncOutcome::Unchanged;
        refreshSubscriptions();
        return result;
    }

    found->observedFingerprint = snapshot.fingerprint;
    found->keptLocalFingerprint.clear();
    if (document->dirty()
        || document->externalState()
               == SharedDocumentExternalState::Conflict) {
        document->setExternalState(
            SharedDocumentExternalState::Conflict);
        result.outcome =
            ExternalDocumentSyncOutcome::Conflict;
        rearmFileSubscription(found->fileName);
        emit documentConflictDetected(
            document,
            found->fileName);
        return result;
    }

    if (!document->reloadCleanText(snapshot.text)) {
        document->setExternalState(
            SharedDocumentExternalState::Conflict);
        result.outcome =
            ExternalDocumentSyncOutcome::Conflict;
        rearmFileSubscription(found->fileName);
        emit documentConflictDetected(
            document,
            found->fileName);
        return result;
    }

    found->savedFingerprint = snapshot.fingerprint;
    result.outcome =
        ExternalDocumentSyncOutcome::Reloaded;
    rearmFileSubscription(found->fileName);
    emit documentReloaded(document, found->fileName);
    return result;
}

ExternalDocumentConflictReview
ExternalDocumentSyncController::conflictReview(
    const QString& requestedFileName)
{
    ExternalDocumentConflictReview review;
    review.fileName = normalizedPath(requestedFileName);
    SharedDocument* document =
        documentForPath(review.fileName);
    if (!document) {
        review.failureReason =
            QStringLiteral("The document is not tracked.");
        return review;
    }

    auto found = trackedDocuments.find(document);
    if (found == trackedDocuments.end()) {
        review.failureReason =
            QStringLiteral("The document is not tracked.");
        return review;
    }
    review.fileName = found->fileName;
    review.documentId = document->documentId();
    review.localText =
        document->textDocument()
        ? document->textDocument()->toPlainText()
        : QString();
    review.documentRevision = document->textRevision();
    if (document->externalState()
        != SharedDocumentExternalState::Conflict) {
        review.failureReason =
            QStringLiteral(
                "The document no longer has an unresolved external conflict.");
        return review;
    }

    const DiskSnapshot initialSnapshot =
        readDiskSnapshot(found->fileName);
    if (!initialSnapshot.available) {
        processFileChange(found->fileName);
        review.valid = true;
        review.externalAvailable = false;
        review.failureReason =
            initialSnapshot.failureReason;
        return review;
    }
    if (initialSnapshot.fingerprint
        != found->observedFingerprint) {
        processFileChange(found->fileName);
    }

    const DiskSnapshot snapshot =
        readDiskSnapshot(found->fileName);
    if (!snapshot.available) {
        processFileChange(found->fileName);
        review.valid = true;
        review.externalAvailable = false;
        review.failureReason = snapshot.failureReason;
        return review;
    }
    if (document->externalState()
        != SharedDocumentExternalState::Conflict) {
        review.failureReason =
            QStringLiteral(
                "The document no longer has an unresolved external conflict.");
        return review;
    }
    if (snapshot.fingerprint
        != found->observedFingerprint) {
        found->observedFingerprint =
            snapshot.fingerprint;
        found->keptLocalFingerprint.clear();
        rearmFileSubscription(found->fileName);
    }

    review.externalText = snapshot.text;
    review.externalFingerprint = snapshot.fingerprint;
    review.externalAvailable = true;
    review.valid = true;
    return review;
}

ExternalDocumentConflictActionResult
ExternalDocumentSyncController::validateConflictReview(
    const ExternalDocumentConflictReview& review)
{
    return validateReview(
        review,
        nullptr,
        nullptr,
        false);
}

ExternalDocumentConflictActionResult
ExternalDocumentSyncController::
    validateConflictReviewForSaveAs(
        const ExternalDocumentConflictReview& review)
{
    return validateReview(
        review,
        nullptr,
        nullptr,
        true);
}

ExternalDocumentConflictActionResult
ExternalDocumentSyncController::keepLocal(
    const ExternalDocumentConflictReview& review)
{
    SharedDocument* document = nullptr;
    DiskSnapshot snapshot;
    ExternalDocumentConflictActionResult result =
        validateReview(review, &document, &snapshot);
    if (!result.applied() || !document)
        return result;

    auto found = trackedDocuments.find(document);
    if (found == trackedDocuments.end()) {
        result.status =
            ExternalDocumentConflictActionStatus::InvalidReview;
        result.failureReason =
            QStringLiteral("The document is no longer tracked.");
        return result;
    }

    found->keptLocalFingerprint = snapshot.fingerprint;
    // The conflict generation has been explicitly acknowledged, but the
    // buffer remains dirty until a guarded save or Save As completes.
    document->setExternalState(
        SharedDocumentExternalState::ExternallyModified);
    result.currentReview = ExternalDocumentConflictReview();
    return result;
}

ExternalDocumentConflictActionResult
ExternalDocumentSyncController::reloadExternal(
    const ExternalDocumentConflictReview& review)
{
    SharedDocument* document = nullptr;
    DiskSnapshot snapshot;
    ExternalDocumentConflictActionResult result =
        validateReview(review, &document, &snapshot);
    if (!result.applied() || !document)
        return result;

    auto found = trackedDocuments.find(document);
    if (found == trackedDocuments.end()) {
        result.status =
            ExternalDocumentConflictActionStatus::InvalidReview;
        result.failureReason =
            QStringLiteral("The document is no longer tracked.");
        return result;
    }
    if (!document->acceptExternalText(snapshot.text)) {
        result.status =
            ExternalDocumentConflictActionStatus::InvalidReview;
        result.failureReason =
            QStringLiteral(
                "The external generation could not be applied.");
        return result;
    }

    found->savedFingerprint = snapshot.fingerprint;
    found->observedFingerprint = snapshot.fingerprint;
    found->keptLocalFingerprint.clear();
    found->diskAvailable = true;
    document->setReadOnly(
        !QFileInfo(found->fileName).isWritable());
    rearmFileSubscription(found->fileName);
    emit documentReloaded(document, found->fileName);
    result.currentReview = ExternalDocumentConflictReview();
    return result;
}

bool ExternalDocumentSyncController::canOverwriteDocument(
    SharedDocument* document,
    QString* failureReason)
{
    if (failureReason)
        failureReason->clear();
    if (!document)
        return false;

    auto found = trackedDocuments.find(document);
    if (found == trackedDocuments.end())
        return true;

    const DiskSnapshot snapshot =
        readDiskSnapshot(found->fileName);
    if (!snapshot.available) {
        processFileChange(found->fileName);
        if (failureReason) {
            *failureReason = snapshot.failureReason.isEmpty()
                ? QStringLiteral(
                      "The source file is unavailable; use Save As.")
                : snapshot.failureReason;
        }
        return false;
    }

    if (snapshot.fingerprint
        != found->observedFingerprint) {
        processFileChange(found->fileName);
        if (failureReason) {
            *failureReason = QStringLiteral(
                "The source changed again. Review the refreshed conflict before saving.");
        }
        return false;
    }

    switch (document->externalState()) {
    case SharedDocumentExternalState::Current:
        if (snapshot.fingerprint
            == found->savedFingerprint) {
            return true;
        }
        if (failureReason) {
            *failureReason = QStringLiteral(
                "The current disk generation does not match the saved document baseline.");
        }
        return false;
    case SharedDocumentExternalState::ExternallyModified:
        if (!found->keptLocalFingerprint.isEmpty()
            && snapshot.fingerprint
                   == found->keptLocalFingerprint) {
            return true;
        }
        if (failureReason) {
            *failureReason = QStringLiteral(
                "The external generation has not been accepted for overwrite.");
        }
        return false;
    case SharedDocumentExternalState::Conflict:
        if (failureReason) {
            *failureReason = QStringLiteral(
                "Resolve the external conflict before saving this path.");
        }
        return false;
    }
    return false;
}

bool ExternalDocumentSyncController::isFileWatchedForTesting(
    const QString& fileName) const
{
    if (!watcher)
        return false;
    const QString key = comparisonKey(fileName);
    for (const QString& watched : watcher->files()) {
        if (comparisonKey(watched) == key)
            return true;
    }
    return false;
}

QString
ExternalDocumentSyncController::watchedFilePathForTesting(
    const QString& fileName) const
{
    if (!watcher)
        return QString();
    const QString key = comparisonKey(fileName);
    for (const QString& watched : watcher->files()) {
        if (comparisonKey(watched) == key)
            return normalizedPath(watched);
    }
    return QString();
}

bool ExternalDocumentSyncController::isDirectoryWatchedForTesting(
    const QString& directory) const
{
    if (!watcher)
        return false;
    const QString key = comparisonKey(directory);
    for (const QString& watched : watcher->directories()) {
        if (comparisonKey(watched) == key)
            return true;
    }
    return false;
}

void ExternalDocumentSyncController::handleFileChanged(
    const QString& path)
{
    processFileChange(path);
}

void ExternalDocumentSyncController::handleDirectoryChanged(
    const QString& path)
{
    const QString directoryKey = comparisonKey(path);
    QStringList changedFiles;
    for (const TrackedDocument& tracked :
         std::as_const(trackedDocuments)) {
        if (comparisonKey(tracked.directory)
            == directoryKey) {
            changedFiles.append(tracked.fileName);
        }
    }
    for (const QString& fileName : std::as_const(changedFiles))
        processFileChange(fileName);
    refreshSubscriptions();
}

QString ExternalDocumentSyncController::normalizedPath(
    const QString& path)
{
    return EditorFileIdentity::normalized(path);
}

QString ExternalDocumentSyncController::comparisonKey(
    const QString& path)
{
    return EditorFileIdentity::lookupKey(path);
}

ExternalDocumentSyncController::DiskSnapshot
ExternalDocumentSyncController::readDiskSnapshot(
    const QString& fileName)
{
    DiskSnapshot result;
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.failureReason = file.errorString();
        if (result.failureReason.isEmpty()) {
            result.failureReason =
                QStringLiteral("File is unavailable.");
        }
        return result;
    }
    QTextStream stream(&file);
    result.text = stream.readAll();
    if (stream.status() != QTextStream::Ok) {
        result.failureReason =
            QStringLiteral("Failed to read the complete file.");
        return result;
    }
    result.fingerprint = textFingerprint(result.text);
    result.available = true;
    return result;
}

void ExternalDocumentSyncController::refreshSubscriptions()
{
    if (!watcher)
        return;

    QSet<QString> desiredFiles;
    QSet<QString> desiredDirectories;
    for (const TrackedDocument& tracked :
         std::as_const(trackedDocuments)) {
        if (QFileInfo(tracked.fileName).isFile())
            desiredFiles.insert(tracked.fileName);
        if (QFileInfo(tracked.directory).isDir())
            desiredDirectories.insert(tracked.directory);
    }

    const QStringList watchedFiles = watcher->files();
    QStringList removeFiles;
    for (const QString& path : watchedFiles) {
        if (!desiredFiles.contains(path))
            removeFiles.append(path);
    }
    if (!removeFiles.isEmpty())
        watcher->removePaths(removeFiles);

    const QStringList watchedDirectories =
        watcher->directories();
    QStringList removeDirectories;
    for (const QString& path : watchedDirectories) {
        if (!desiredDirectories.contains(path))
            removeDirectories.append(path);
    }
    if (!removeDirectories.isEmpty())
        watcher->removePaths(removeDirectories);

    const QStringList currentFilePaths = watcher->files();
    const QSet<QString> currentFiles(
        currentFilePaths.cbegin(),
        currentFilePaths.cend());
    QStringList addFiles;
    for (const QString& path :
         std::as_const(desiredFiles)) {
        if (!currentFiles.contains(path))
            addFiles.append(path);
    }
    if (!addFiles.isEmpty())
        watcher->addPaths(addFiles);

    const QStringList currentDirectoryPaths =
        watcher->directories();
    const QSet<QString> currentDirectories(
        currentDirectoryPaths.cbegin(),
        currentDirectoryPaths.cend());
    QStringList addDirectories;
    for (const QString& path :
         std::as_const(desiredDirectories)) {
        if (!currentDirectories.contains(path))
            addDirectories.append(path);
    }
    if (!addDirectories.isEmpty())
        watcher->addPaths(addDirectories);
}

void ExternalDocumentSyncController::rearmFileSubscription(
    const QString& fileName)
{
    if (!watcher)
        return;
    const QString key = comparisonKey(fileName);
    QStringList matches;
    for (const QString& watched : watcher->files()) {
        if (comparisonKey(watched) == key)
            matches.append(watched);
    }
    if (!matches.isEmpty())
        watcher->removePaths(matches);
    refreshSubscriptions();
}

SharedDocument*
ExternalDocumentSyncController::documentForPath(
    const QString& fileName) const
{
    const QString key = comparisonKey(fileName);
    if (key.isEmpty())
        return nullptr;
    for (auto it = trackedDocuments.cbegin();
         it != trackedDocuments.cend();
         ++it) {
        if (it->pathKey == key)
            return it.key();
    }
    return nullptr;
}

ExternalDocumentConflictActionResult
ExternalDocumentSyncController::validateReview(
    const ExternalDocumentConflictReview& review,
    SharedDocument** resolvedDocument,
    DiskSnapshot* resolvedSnapshot,
    bool allowUnavailableExternal)
{
    if (resolvedDocument)
        *resolvedDocument = nullptr;
    if (resolvedSnapshot)
        *resolvedSnapshot = DiskSnapshot();

    ExternalDocumentConflictActionResult result;
    if (!review.valid
        || review.fileName.isEmpty()
        || review.documentId.isEmpty()) {
        result.failureReason =
            QStringLiteral("The conflict review is invalid.");
        return result;
    }

    SharedDocument* document =
        documentForPath(review.fileName);
    if (!document
        || document->documentId() != review.documentId) {
        result.failureReason =
            QStringLiteral(
                "The reviewed document is no longer open at this path.");
        return result;
    }

    auto found = trackedDocuments.find(document);
    if (found == trackedDocuments.end()) {
        result.failureReason =
            QStringLiteral("The document is no longer tracked.");
        return result;
    }

    const DiskSnapshot snapshot =
        readDiskSnapshot(found->fileName);
    if (!snapshot.available) {
        processFileChange(found->fileName);
        if (allowUnavailableExternal
            && !review.externalAvailable) {
            if (document->textRevision()
                != review.documentRevision) {
                result.status =
                    ExternalDocumentConflictActionStatus::
                        StaleDocument;
                result.failureReason =
                    QStringLiteral(
                        "The local document changed after the comparison was opened.");
                result.currentReview =
                    conflictReview(found->fileName);
                return result;
            }
            if (document->externalState()
                != SharedDocumentExternalState::
                    Conflict) {
                result.status =
                    ExternalDocumentConflictActionStatus::
                        NotInConflict;
                result.failureReason =
                    QStringLiteral(
                        "The document no longer has an unresolved external conflict.");
                return result;
            }
            result.status =
                ExternalDocumentConflictActionStatus::Applied;
            if (resolvedDocument)
                *resolvedDocument = document;
            if (resolvedSnapshot)
                *resolvedSnapshot = snapshot;
            return result;
        }
        result.status =
            ExternalDocumentConflictActionStatus::Unavailable;
        result.failureReason = snapshot.failureReason;
        result.currentReview =
            conflictReview(found->fileName);
        return result;
    }
    if (!review.externalAvailable) {
        processFileChange(found->fileName);
        result.status =
            ExternalDocumentConflictActionStatus::ExternalChanged;
        result.failureReason =
            QStringLiteral(
                "The source became available after the comparison was opened.");
        result.currentReview =
            conflictReview(found->fileName);
        return result;
    }
    if (snapshot.fingerprint
        != review.externalFingerprint) {
        processFileChange(found->fileName);
        result.status =
            ExternalDocumentConflictActionStatus::ExternalChanged;
        result.failureReason =
            QStringLiteral(
                "The source changed again after the comparison was opened.");
        result.currentReview =
            conflictReview(found->fileName);
        return result;
    }
    if (document->textRevision()
        != review.documentRevision) {
        result.status =
            ExternalDocumentConflictActionStatus::StaleDocument;
        result.failureReason =
            QStringLiteral(
                "The local document changed after the comparison was opened.");
        result.currentReview =
            conflictReview(found->fileName);
        return result;
    }
    if (document->externalState()
        != SharedDocumentExternalState::Conflict) {
        result.status =
            ExternalDocumentConflictActionStatus::NotInConflict;
        result.failureReason =
            QStringLiteral(
                "The document no longer has an unresolved external conflict.");
        return result;
    }

    result.status =
        ExternalDocumentConflictActionStatus::Applied;
    if (resolvedDocument)
        *resolvedDocument = document;
    if (resolvedSnapshot)
        *resolvedSnapshot = snapshot;
    return result;
}
