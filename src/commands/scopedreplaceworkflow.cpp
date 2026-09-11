#include "scopedreplaceworkflow.h"

#include "notificationcenter.h"
#include "workspaceedittransactionservice.h"

#include <rtledit/edit_plan.h>

#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <utility>

namespace {

std::string utf8String(const QString& text)
{
    const QByteArray bytes = text.toUtf8();
    return std::string(
        bytes.constData(),
        static_cast<std::size_t>(bytes.size()));
}

QString fromUtf8(const std::string& text)
{
    return QString::fromUtf8(
        text.data(), static_cast<qsizetype>(text.size()));
}

bool sameSnapshot(
    const rtledit::WorkspaceDocumentSnapshot& left,
    const rtledit::WorkspaceDocumentSnapshot& right)
{
    return left.version == right.version
        && left.text == right.text;
}

QString transactionMessage(
    const rtledit::WorkspaceEditTransactionResult& result)
{
    return result.message.empty()
        ? QStringLiteral("Workspace replace transaction failed.")
        : fromUtf8(result.message);
}

int selectedFileCount(
    const rtledit::WorkspaceEditPlan& plan)
{
    QSet<QString> files;
    for (const rtledit::WorkspaceTextEdit& edit : plan.edits)
        files.insert(fromUtf8(edit.filePath));
    return files.size();
}

ScopedReplaceWorkflowFailure prepareFailure(
    rtledit::TransactionPrepareStatus status)
{
    switch (status) {
    case rtledit::TransactionPrepareStatus::Stale:
        return ScopedReplaceWorkflowFailure::
            StaleDocumentRevision;
    case rtledit::TransactionPrepareStatus::InvalidPlan:
    case rtledit::TransactionPrepareStatus::DiffFailed:
        return ScopedReplaceWorkflowFailure::InvalidPreview;
    case rtledit::TransactionPrepareStatus::Ready:
        break;
    }
    return ScopedReplaceWorkflowFailure::InvalidPreview;
}

} // namespace

bool ScopedReplaceWorkflowResult::succeeded() const
{
    if (failure != ScopedReplaceWorkflowFailure::None)
        return false;
    switch (state) {
    case ScopedReplaceWorkflowState::PreviewReady:
    case ScopedReplaceWorkflowState::Applied:
    case ScopedReplaceWorkflowState::DryRunComplete:
    case ScopedReplaceWorkflowState::Undone:
        return true;
    case ScopedReplaceWorkflowState::Idle:
    case ScopedReplaceWorkflowState::Cancelled:
    case ScopedReplaceWorkflowState::Failed:
        return false;
    }
    return false;
}

ScopedReplaceWorkflow::ScopedReplaceWorkflow(
    rtledit::WorkspaceDocumentManager* documents,
    WorkspaceEditTransactionService* transactions,
    NotificationCenter* notifications,
    QObject* parent)
    : QObject(parent)
    , documentManager(documents)
    , notificationCenter(notifications)
{
    qRegisterMetaType<ScopedReplaceWorkflowResult>(
        "ScopedReplaceWorkflowResult");
    if (transactions) {
        transactionService = transactions;
    } else {
        ownedTransactionService =
            std::make_unique<WorkspaceEditTransactionService>();
        transactionService = ownedTransactionService.get();
    }
}

ScopedReplaceWorkflow::~ScopedReplaceWorkflow() = default;

void ScopedReplaceWorkflow::setNotificationCenter(
    NotificationCenter* notifications)
{
    notificationCenter = notifications;
}

ScopedReplaceWorkflowResult
ScopedReplaceWorkflow::preparePreview(
    const ReplacePreviewPlan& preview,
    const QList<SearchDocumentSnapshot>& searchedDocuments,
    bool dryRun)
{
    clearPending();
    if (!documentManager || !transactionService) {
        return fail(
            ScopedReplaceWorkflowFailure::MissingDependency,
            QStringLiteral(
                "The scoped replace transaction service is unavailable."));
    }
    if (!preview.ready()
        || preview.transactionPlan.riskLevel
            != rtledit::RiskLevel::High
        || preview.transactionPlan.previewPolicy
            != rtledit::PreviewPolicy::Diff
        || preview.transactionPlan.edits.empty()) {
        return fail(
            ScopedReplaceWorkflowFailure::InvalidPreview,
            QStringLiteral(
                "Scoped replace requires a non-empty High-risk Diff preview."));
    }

    QHash<QString, SearchDocumentSnapshot> searchedByIdentity;
    for (const SearchDocumentSnapshot& document :
         searchedDocuments) {
        const QString identity =
            documentIdentity(document.fileName);
        if (!identity.isEmpty()
            && !searchedByIdentity.contains(identity)) {
            searchedByIdentity.insert(identity, document);
        }
    }

    QHash<QString, quint64> previewRevisionByIdentity;
    for (const ReplaceFilePreview& file : preview.files) {
        if (!file.selected)
            continue;
        const QString identity =
            documentIdentity(file.fileName);
        if (!identity.isEmpty()) {
            previewRevisionByIdentity.insert(
                identity, file.documentRevision);
        }
    }

    rtledit::WorkspaceEditPlan reboundPlan =
        preview.transactionPlan;
    QHash<QString, rtledit::DocumentVersion>
        liveVersionByIdentity;
    QSet<QString> capturedIdentities;
    capturedDocuments.clear();

    for (const rtledit::WorkspaceTextEdit& edit :
         reboundPlan.edits) {
        const QString fileName =
            normalizedFileName(edit.filePath);
        const QString identity =
            documentIdentity(fileName);
        const auto searchedIt =
            searchedByIdentity.constFind(identity);
        if (identity.isEmpty()
            || searchedIt == searchedByIdentity.constEnd()) {
            return fail(
                ScopedReplaceWorkflowFailure::
                    StaleSearchRevision,
                QStringLiteral(
                    "The searched document is no longer available: %1")
                    .arg(fileName));
        }

        const SearchDocumentSnapshot& searched =
            searchedIt.value();
        const quint64 previewRevision =
            previewRevisionByIdentity.value(
                identity, searched.revision);
        if (previewRevision != searched.revision) {
            return fail(
                ScopedReplaceWorkflowFailure::
                    StaleSearchRevision,
                QStringLiteral(
                    "The search revision changed before preview: %1")
                    .arg(fileName),
                rtledit::TransactionStatus::Stale);
        }

        const auto live =
            documentManager->snapshot(edit.filePath);
        if (!live
            || live->text != utf8String(searched.text)) {
            return fail(
                ScopedReplaceWorkflowFailure::
                    StaleDocumentRevision,
                QStringLiteral(
                    "The document changed before preview: %1")
                    .arg(fileName),
                rtledit::TransactionStatus::Stale);
        }

        liveVersionByIdentity.insert(
            identity, live->version);
        if (!capturedIdentities.contains(identity)) {
            capturedIdentities.insert(identity);
            capturedDocuments.push_back(
                CapturedDocument{
                    edit.filePath,
                    *live,
                    searched.revision,
                    externalStateToken(fileName)});
        }
    }

    for (rtledit::WorkspaceTextEdit& edit :
         reboundPlan.edits) {
        const QString identity =
            documentIdentity(normalizedFileName(edit.filePath));
        edit.expectedDocumentVersion =
            liveVersionByIdentity.value(identity);
    }
    reboundPlan.baselines =
        rtledit::collectDocumentBaselines(reboundPlan.edits);
    reboundPlan.hasMixedDocumentVersions = false;

    const rtledit::SemanticIndexSnapshot semanticSnapshot =
        reboundPlan.semanticSnapshot;
    rtledit::PreparedWorkspaceEditTransaction prepared =
        transactionService->prepare(
            std::move(reboundPlan),
            semanticSnapshot,
            *documentManager,
            dryRun);
    if (!prepared.ready()) {
        const ScopedReplaceWorkflowFailure failure =
            prepareFailure(prepared.status);
        clearPending();
        return fail(
            failure,
            QStringLiteral(
                "The scoped replace Diff preview could not be prepared."),
            prepared.status
                    == rtledit::TransactionPrepareStatus::Stale
                ? rtledit::TransactionStatus::Stale
                : rtledit::TransactionStatus::
                      InvalidPreparation);
    }

    const QString renderedDiff =
        fromUtf8(rtledit::renderWorkspaceEditSourceDiffHunks(
            prepared.sourceDiff));
    const int fileCount =
        selectedFileCount(prepared.plan);
    const int editCount =
        static_cast<int>(prepared.plan.edits.size());
    pendingTransaction = std::move(prepared);
    return publish(
        ScopedReplaceWorkflowState::PreviewReady,
        ScopedReplaceWorkflowFailure::None,
        dryRun
            ? QStringLiteral(
                  "Dry-run Diff ready. Confirm to validate the plan "
                  "without changing files.")
            : QStringLiteral(
                  "Diff ready. Confirm to apply one atomic workspace "
                  "transaction."),
        rtledit::TransactionStatus::InvalidPreparation,
        renderedDiff,
        fileCount,
        editCount);
}

ScopedReplaceWorkflowResult ScopedReplaceWorkflow::confirm(
    const QList<SearchDocumentSnapshot>& currentDocuments)
{
    if (!documentManager || !transactionService) {
        return fail(
            ScopedReplaceWorkflowFailure::MissingDependency,
            QStringLiteral(
                "The scoped replace transaction service is unavailable."));
    }
    if (!pendingTransaction
        || currentState
            != ScopedReplaceWorkflowState::PreviewReady) {
        return fail(
            ScopedReplaceWorkflowFailure::InvalidPreview,
            QStringLiteral(
                "Build and inspect a Diff preview before confirming."));
    }

    QHash<QString, SearchDocumentSnapshot>
        currentByIdentity;
    for (const SearchDocumentSnapshot& document :
         currentDocuments) {
        const QString identity =
            documentIdentity(document.fileName);
        if (!identity.isEmpty()
            && !currentByIdentity.contains(identity)) {
            currentByIdentity.insert(identity, document);
        }
    }

    for (const CapturedDocument& captured :
         capturedDocuments) {
        const QString fileName =
            normalizedFileName(captured.filePath);
        if (externalStateToken(fileName)
            != captured.externalStateToken) {
            clearPending();
            return fail(
                ScopedReplaceWorkflowFailure::
                    ExternalModification,
                QStringLiteral(
                    "The file changed externally after preview: %1")
                    .arg(fileName),
                rtledit::TransactionStatus::Conflict);
        }

        if (!currentByIdentity.isEmpty()) {
            const auto searched =
                currentByIdentity.constFind(
                    documentIdentity(fileName));
            if (searched == currentByIdentity.constEnd()
                || searched->revision
                    != captured.searchRevision
                || utf8String(searched->text)
                    != captured.snapshot.text) {
                clearPending();
                return fail(
                    ScopedReplaceWorkflowFailure::
                        StaleSearchRevision,
                    QStringLiteral(
                        "The search revision changed after preview: %1")
                        .arg(fileName),
                    rtledit::TransactionStatus::Stale);
            }
        }

        const auto live =
            documentManager->snapshot(captured.filePath);
        if (!live || !sameSnapshot(*live, captured.snapshot)) {
            clearPending();
            return fail(
                ScopedReplaceWorkflowFailure::
                    StaleDocumentRevision,
                QStringLiteral(
                    "The document revision changed after preview: %1")
                    .arg(fileName),
                rtledit::TransactionStatus::Stale);
        }
    }

    rtledit::PreparedWorkspaceEditTransaction prepared =
        *pendingTransaction;
    const rtledit::SemanticIndexSnapshot semanticSnapshot =
        pendingTransaction->plan.semanticSnapshot;
    const QString diff = currentResult.renderedDiff;
    const int fileCount = currentResult.fileCount;
    const int editCount = currentResult.editCount;
    const rtledit::WorkspaceEditTransactionResult result =
        transactionService->applyConfirmed(
            std::move(prepared),
            semanticSnapshot,
            *documentManager);
    clearPending();

    if (result.status == rtledit::TransactionStatus::Applied) {
        workflowUndoAvailable = true;
        return publish(
            ScopedReplaceWorkflowState::Applied,
            ScopedReplaceWorkflowFailure::None,
            transactionMessage(result),
            result.status,
            diff,
            fileCount,
            editCount);
    }
    if (result.status
        == rtledit::TransactionStatus::DryRunOnly) {
        return publish(
            ScopedReplaceWorkflowState::DryRunComplete,
            ScopedReplaceWorkflowFailure::None,
            transactionMessage(result),
            result.status,
            diff,
            fileCount,
            editCount);
    }

    const bool rollbackFailed =
        !result.residualFiles.empty()
        || (result.status
                == rtledit::TransactionStatus::ApplyFailed
            && !result.applyResult.patchResult
                    .changedFiles.empty());
    ScopedReplaceWorkflowFailure failure =
        rollbackFailed
        ? ScopedReplaceWorkflowFailure::
              AtomicRollbackFailed
        : ScopedReplaceWorkflowFailure::ApplyFailed;
    if (result.status == rtledit::TransactionStatus::Stale)
        failure = ScopedReplaceWorkflowFailure::
            StaleDocumentRevision;
    else if (result.status
             == rtledit::TransactionStatus::Conflict)
        failure = ScopedReplaceWorkflowFailure::Conflict;

    return fail(
        failure,
        transactionMessage(result),
        result.status);
}

ScopedReplaceWorkflowResult ScopedReplaceWorkflow::cancel()
{
    clearPending();
    return publish(
        ScopedReplaceWorkflowState::Cancelled,
        ScopedReplaceWorkflowFailure::None,
        QStringLiteral(
            "The pending scoped replace preview was cancelled."));
}

ScopedReplaceWorkflowResult ScopedReplaceWorkflow::undo()
{
    if (!documentManager || !transactionService) {
        return fail(
            ScopedReplaceWorkflowFailure::MissingDependency,
            QStringLiteral(
                "The scoped replace transaction service is unavailable."));
    }
    if (!workflowUndoAvailable
        || !transactionService->canUndo()) {
        workflowUndoAvailable = false;
        return fail(
            ScopedReplaceWorkflowFailure::NothingToUndo,
            QStringLiteral(
                "No applied scoped replace transaction is available "
                "to undo."),
            rtledit::TransactionStatus::NothingToUndo);
    }

    const rtledit::WorkspaceEditTransactionResult result =
        transactionService->undo(*documentManager);
    if (result.status == rtledit::TransactionStatus::Undone) {
        workflowUndoAvailable =
            transactionService->canUndo();
        return publish(
            ScopedReplaceWorkflowState::Undone,
            ScopedReplaceWorkflowFailure::None,
            transactionMessage(result),
            result.status);
    }

    return fail(
        result.residualFiles.empty()
            ? ScopedReplaceWorkflowFailure::UndoFailed
            : ScopedReplaceWorkflowFailure::
                  AtomicRollbackFailed,
        transactionMessage(result),
        result.status);
}

void ScopedReplaceWorkflow::discardPendingPreview()
{
    clearPending();
    if (currentState
        == ScopedReplaceWorkflowState::PreviewReady) {
        currentState = ScopedReplaceWorkflowState::Idle;
        currentResult = ScopedReplaceWorkflowResult{};
    }
}

ScopedReplaceWorkflowState ScopedReplaceWorkflow::state() const
{
    return currentState;
}

const ScopedReplaceWorkflowResult&
ScopedReplaceWorkflow::lastResult() const
{
    return currentResult;
}

const rtledit::PreparedWorkspaceEditTransaction*
ScopedReplaceWorkflow::preparedTransaction() const
{
    return pendingTransaction
        ? &*pendingTransaction : nullptr;
}

bool ScopedReplaceWorkflow::hasPendingPreview() const
{
    return pendingTransaction.has_value()
        && currentState
            == ScopedReplaceWorkflowState::PreviewReady;
}

bool ScopedReplaceWorkflow::canUndoAppliedTransaction() const
{
    return workflowUndoAvailable
        && transactionService
        && transactionService->canUndo();
}

ScopedReplaceWorkflowResult ScopedReplaceWorkflow::publish(
    ScopedReplaceWorkflowState state,
    ScopedReplaceWorkflowFailure failure,
    const QString& message,
    rtledit::TransactionStatus transactionStatus,
    const QString& renderedDiff,
    int fileCount,
    int editCount)
{
    currentState = state;
    currentResult.state = state;
    currentResult.failure = failure;
    currentResult.transactionStatus = transactionStatus;
    currentResult.message = message;
    currentResult.renderedDiff = renderedDiff;
    currentResult.fileCount = fileCount;
    currentResult.editCount = editCount;
    emit stateChanged(currentResult);
    if (failure != ScopedReplaceWorkflowFailure::None)
        emit failureRaised(currentResult);
    return currentResult;
}

ScopedReplaceWorkflowResult ScopedReplaceWorkflow::fail(
    ScopedReplaceWorkflowFailure failure,
    const QString& message,
    rtledit::TransactionStatus transactionStatus)
{
    const ScopedReplaceWorkflowResult result =
        publish(
            ScopedReplaceWorkflowState::Failed,
            failure,
            message,
            transactionStatus);
    postFailureNotification(result);
    return result;
}

void ScopedReplaceWorkflow::postFailureNotification(
    const ScopedReplaceWorkflowResult& result)
{
    if (!notificationCenter)
        return;

    NotificationDraft draft;
    draft.key = QStringLiteral(
        "transaction.scopedReplace");
    draft.topic =
        NotificationTopic::TransactionConflict;
    draft.severity =
        result.failure
                == ScopedReplaceWorkflowFailure::
                    AtomicRollbackFailed
        ? NotificationSeverity::Critical
        : NotificationSeverity::Error;
    draft.source = QStringLiteral("Scoped Replace");
    draft.message = result.message;
    notificationCenter->post(draft);
}

void ScopedReplaceWorkflow::clearPending()
{
    pendingTransaction.reset();
    capturedDocuments.clear();
}

QString ScopedReplaceWorkflow::normalizedFileName(
    const std::string& filePath)
{
    const QString fileName = fromUtf8(filePath);
    if (fileName.isEmpty())
        return {};
    return QDir::cleanPath(
        QDir::fromNativeSeparators(
            QFileInfo(fileName).absoluteFilePath()));
}

QString ScopedReplaceWorkflow::documentIdentity(
    const QString& fileName)
{
    if (fileName.isEmpty())
        return {};
    QString identity =
        QDir::cleanPath(
            QDir::fromNativeSeparators(
                QFileInfo(fileName).absoluteFilePath()));
#ifdef Q_OS_WIN
    identity = identity.toCaseFolded();
#endif
    return identity;
}

QString ScopedReplaceWorkflow::externalStateToken(
    const QString& fileName)
{
    const QFileInfo info(fileName);
    if (!info.exists())
        return QStringLiteral("missing");
    if (!info.isFile())
        return QStringLiteral("not-file");

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        return QStringLiteral("unreadable|%1|%2|%3")
            .arg(info.size())
            .arg(info.lastModified().toMSecsSinceEpoch())
            .arg(static_cast<int>(info.permissions()));
    }
    const QByteArray digest =
        QCryptographicHash::hash(
            file.readAll(),
            QCryptographicHash::Sha256).toHex();
    return QStringLiteral("%1|%2|%3|%4")
        .arg(info.size())
        .arg(info.lastModified().toMSecsSinceEpoch())
        .arg(static_cast<int>(info.permissions()))
        .arg(QString::fromLatin1(digest));
}
