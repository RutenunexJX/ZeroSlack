#include "rtlhighriskeditworkflow.h"

#include "workspaceedittransactionservice.h"

#include <rtledit/edit_plan.h>

#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>

#include <limits>
#include <utility>

namespace {

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

bool sameSemanticToken(
    const SemanticSnapshotToken& left,
    const SemanticSnapshotToken& right)
{
    return left.isValid()
        && right.isValid()
        && left.revision == right.revision
        && left.snapshot.get() == right.snapshot.get();
}

QString transactionMessage(
    const rtledit::WorkspaceEditTransactionResult& result)
{
    return result.message.empty()
        ? QStringLiteral("The RTL workspace transaction failed.")
        : fromUtf8(result.message);
}

rtledit::TransactionStatus transactionStatusForFailure(
    RtlHighRiskEditWorkflowFailure failure)
{
    switch (failure) {
    case RtlHighRiskEditWorkflowFailure::
        StaleSemanticGeneration:
    case RtlHighRiskEditWorkflowFailure::
        StaleDocumentRevision:
        return rtledit::TransactionStatus::Stale;
    case RtlHighRiskEditWorkflowFailure::
        ExternalModification:
    case RtlHighRiskEditWorkflowFailure::
        TransactionGenerationConflict:
    case RtlHighRiskEditWorkflowFailure::
        PreviewConflict:
    case RtlHighRiskEditWorkflowFailure::
        ConfirmationTokenMismatch:
    case RtlHighRiskEditWorkflowFailure::
        UndoConflict:
        return rtledit::TransactionStatus::Conflict;
    case RtlHighRiskEditWorkflowFailure::None:
    case RtlHighRiskEditWorkflowFailure::MissingDependency:
    case RtlHighRiskEditWorkflowFailure::InvalidState:
    case RtlHighRiskEditWorkflowFailure::PlanningRejected:
    case RtlHighRiskEditWorkflowFailure::InvalidPlan:
    case RtlHighRiskEditWorkflowFailure::ApplyFailed:
    case RtlHighRiskEditWorkflowFailure::AtomicRollbackFailed:
    case RtlHighRiskEditWorkflowFailure::NothingToUndo:
    case RtlHighRiskEditWorkflowFailure::UndoFailed:
        break;
    }
    return rtledit::TransactionStatus::InvalidPreparation;
}

void addHashField(
    QCryptographicHash& hash,
    const QByteArray& value)
{
    hash.addData(QByteArray::number(value.size()));
    hash.addData(QByteArrayLiteral(":"));
    hash.addData(value);
    hash.addData(QByteArrayLiteral("|"));
}

void addHashField(
    QCryptographicHash& hash,
    const std::string& value)
{
    addHashField(
        hash,
        QByteArray(
            value.data(),
            static_cast<qsizetype>(value.size())));
}

void addHashNumber(
    QCryptographicHash& hash,
    std::uint64_t value)
{
    addHashField(hash, QByteArray::number(value));
}

} // namespace

bool RtlHighRiskEditPreview::ready() const
{
    return !actionId.isEmpty()
        && !confirmationToken.isEmpty()
        && riskLevel == rtledit::RiskLevel::High
        && previewPolicy == rtledit::PreviewPolicy::Diff
        && structuredPreview.built()
        && sourceDiff.built()
        && !renderedDiff.isEmpty()
        && fileCount > 0
        && editCount > 0;
}

bool RtlHighRiskEditWorkflowResult::succeeded() const
{
    if (failure != RtlHighRiskEditWorkflowFailure::None)
        return false;
    switch (state) {
    case RtlHighRiskEditWorkflowState::PreviewReady:
    case RtlHighRiskEditWorkflowState::NoChanges:
    case RtlHighRiskEditWorkflowState::Applied:
    case RtlHighRiskEditWorkflowState::DryRunComplete:
    case RtlHighRiskEditWorkflowState::Undone:
        return true;
    case RtlHighRiskEditWorkflowState::Idle:
    case RtlHighRiskEditWorkflowState::Cancelled:
    case RtlHighRiskEditWorkflowState::Failed:
        return false;
    }
    return false;
}

RtlHighRiskEditWorkflow::RtlHighRiskEditWorkflow(
    SemanticIndex* semanticIndex,
    rtledit::WorkspaceDocumentManager* documents,
    WorkspaceEditTransactionService* transactions,
    ExternalStateProvider externalStateProvider)
    : index(
          semanticIndex
              ? semanticIndex
              : SemanticIndex::getInstance())
    , documentManager(documents)
    , ownedTransactionService(
          transactions
              ? nullptr
              : std::make_unique<
                    WorkspaceEditTransactionService>())
    , transactionService(
          transactions
              ? transactions
              : ownedTransactionService.get())
    , readExternalState(
          externalStateProvider
              ? std::move(externalStateProvider)
              : ExternalStateProvider{
                    [](const QString& fileName) {
                        return RtlHighRiskEditWorkflow::
                            defaultExternalStateToken(
                                fileName);
                    }})
{
}

RtlHighRiskEditWorkflow::~RtlHighRiskEditWorkflow() = default;

RtlHighRiskEditWorkflowResult
RtlHighRiskEditWorkflow::preparePreview(
    const QString& actionId,
    rtledit::WorkspaceEditPlan plan,
    const SemanticSnapshotToken& expectedSemanticToken,
    bool dryRun)
{
    if (!canStartPreview()) {
        return publish(
            RtlHighRiskEditWorkflowState::Applied,
            RtlHighRiskEditWorkflowFailure::InvalidState,
            currentResult.actionId,
            QStringLiteral(
                "Undo or retire the applied RTL transaction before "
                "preparing another preview."));
    }

    clearPending(true);
    if (!index
        || !documentManager
        || !transactionService
        || !readExternalState) {
        return fail(
            RtlHighRiskEditWorkflowFailure::MissingDependency,
            actionId,
            QStringLiteral(
                "The RTL high-risk edit workflow dependencies are "
                "incomplete."));
    }
    if (actionId.isEmpty()
        || plan.riskLevel != rtledit::RiskLevel::High
        || plan.previewPolicy != rtledit::PreviewPolicy::Diff
        || plan.edits.empty()) {
        return fail(
            RtlHighRiskEditWorkflowFailure::InvalidPlan,
            actionId,
            QStringLiteral(
                "RTL transformations require a non-empty High-risk "
                "Diff plan."));
    }

    const SemanticSnapshotToken liveToken =
        index->snapshotToken();
    if (!sameSemanticToken(
            expectedSemanticToken, liveToken)) {
        return fail(
            RtlHighRiskEditWorkflowFailure::
                StaleSemanticGeneration,
            actionId,
            QStringLiteral(
                "The semantic generation changed before preview "
                "preparation."),
            rtledit::TransactionStatus::Stale);
    }
    if (plan.semanticSnapshot.id
            != std::to_string(liveToken.revision)) {
        return fail(
            RtlHighRiskEditWorkflowFailure::
                StaleSemanticGeneration,
            actionId,
            QStringLiteral(
                "The edit plan is not bound to the current semantic "
                "generation."),
            rtledit::TransactionStatus::Stale);
    }

    const rtledit::EditPlanValidationResult validation =
        rtledit::validateWorkspaceEditPlan(plan);
    if (!validation.valid()) {
        return fail(
            RtlHighRiskEditWorkflowFailure::InvalidPlan,
            actionId,
            validation.message.empty()
                ? QStringLiteral(
                      "The RTL workspace edit plan is invalid.")
                : fromUtf8(validation.message));
    }
    const QByteArray actionBytes = actionId.toUtf8();
    const std::string actionText(
        actionBytes.constData(),
        static_cast<std::size_t>(
            actionBytes.size()));
    for (const rtledit::TextEditProvenance& provenance :
         plan.provenance) {
        if (provenance.actionId != actionText) {
            return fail(
                RtlHighRiskEditWorkflowFailure::
                    InvalidPlan,
                actionId,
                QStringLiteral(
                    "Every high-risk edit provenance entry must "
                    "match the workflow action identifier."));
        }
    }

    capturedSemanticToken = liveToken;
    const PreflightResult captured =
        captureContext(plan);
    if (!captured.ready()) {
        clearPending(true);
        return fail(
            captured.failure,
            actionId,
            captured.message,
            captured.transactionStatus,
            captured.conflictFile);
    }

    previewTransactionGeneration =
        transactionService->historyGeneration();
    rtledit::PreparedWorkspaceEditTransaction prepared =
        transactionService->prepare(
            std::move(plan),
            rtledit::SemanticIndexSnapshot{
                std::to_string(liveToken.revision)},
            *documentManager,
            dryRun);
    if (transactionService->historyGeneration()
            != previewTransactionGeneration) {
        clearPending(true);
        return fail(
            RtlHighRiskEditWorkflowFailure::
                TransactionGenerationConflict,
            actionId,
            QStringLiteral(
                "The shared workspace transaction generation changed "
                "while the preview was being prepared."),
            rtledit::TransactionStatus::Conflict);
    }
    if (!sameSemanticToken(
            capturedSemanticToken,
            index->snapshotToken())) {
        clearPending(true);
        return fail(
            RtlHighRiskEditWorkflowFailure::
                StaleSemanticGeneration,
            actionId,
            QStringLiteral(
                "The semantic generation changed while the "
                "High+Diff preview was being prepared."),
            rtledit::TransactionStatus::Stale);
    }
    if (!prepared.ready()) {
        const bool stale =
            prepared.status
            == rtledit::TransactionPrepareStatus::Stale;
        RtlHighRiskEditWorkflowFailure failure =
            stale
            ? RtlHighRiskEditWorkflowFailure::
                  StaleDocumentRevision
            : RtlHighRiskEditWorkflowFailure::InvalidPlan;
        if (stale
            && prepared.preview.staleStatus.reason
                == rtledit::EditPlanStaleReason::
                    SemanticSnapshotChanged) {
            failure =
                RtlHighRiskEditWorkflowFailure::
                    StaleSemanticGeneration;
        }
        clearPending(true);
        return fail(
            failure,
            actionId,
            QStringLiteral(
                "The structured High+Diff transaction preview could "
                "not be prepared."),
            stale
                ? rtledit::TransactionStatus::Stale
                : rtledit::TransactionStatus::
                      InvalidPreparation,
            fromUtf8(
                prepared.preview.staleStatus.filePath));
    }
    if (prepared.previewConfirmed
        || prepared.plan.riskLevel
            != rtledit::RiskLevel::High
        || prepared.plan.previewPolicy
            != rtledit::PreviewPolicy::Diff
        || prepared.preview.riskLevel
            != rtledit::RiskLevel::High
        || prepared.preview.previewPolicy
            != rtledit::PreviewPolicy::Diff
        || !prepared.preview.built()
        || !prepared.sourceDiff.built()) {
        clearPending(true);
        return fail(
            RtlHighRiskEditWorkflowFailure::PreviewConflict,
            actionId,
            QStringLiteral(
                "The prepared transaction does not match the required "
                "High+Diff preview contract."),
            rtledit::TransactionStatus::Conflict);
    }

    previewSerial = nextPreviewSerial++;
    RtlHighRiskEditPreview preview;
    preview.actionId = actionId;
    preview.dryRun = dryRun;
    preview.fileCount =
        static_cast<int>(prepared.preview.fileCount);
    preview.editCount =
        static_cast<int>(prepared.preview.editCount);
    preview.riskLevel = prepared.preview.riskLevel;
    preview.previewPolicy =
        prepared.preview.previewPolicy;
    preview.structuredPreview = prepared.preview;
    preview.sourceDiff = prepared.sourceDiff;
    preview.renderedDiff =
        fromUtf8(
            rtledit::renderWorkspaceEditSourceDiffHunks(
                prepared.sourceDiff));
    preview.confirmationToken =
        buildConfirmationToken(
            prepared,
            actionId,
            previewSerial,
            previewTransactionGeneration);
    if (!preview.ready()) {
        clearPending(true);
        return fail(
            RtlHighRiskEditWorkflowFailure::PreviewConflict,
            actionId,
            QStringLiteral(
                "The structured RTL edit preview is incomplete."),
            rtledit::TransactionStatus::Conflict);
    }

    pendingTransaction = std::move(prepared);
    currentPreview = std::move(preview);
    const PreflightResult finalCheck = preflight();
    if (!finalCheck.ready()) {
        clearPending(true);
        return fail(
            finalCheck.failure,
            actionId,
            finalCheck.message,
            finalCheck.transactionStatus,
            finalCheck.conflictFile);
    }
    return publish(
        RtlHighRiskEditWorkflowState::PreviewReady,
        RtlHighRiskEditWorkflowFailure::None,
        actionId,
        dryRun
            ? QStringLiteral(
                  "Dry-run High+Diff preview is ready for validation.")
            : QStringLiteral(
                  "High+Diff preview is ready for explicit "
                  "confirmation."));
}

RtlHighRiskEditWorkflowResult
RtlHighRiskEditWorkflow::confirm(
    const QString& confirmationToken)
{
    const QString actionId =
        currentPreview
        ? currentPreview->actionId
        : currentResult.actionId;
    if (!pendingTransaction
        || !currentPreview
        || currentState
            != RtlHighRiskEditWorkflowState::PreviewReady) {
        return fail(
            RtlHighRiskEditWorkflowFailure::InvalidState,
            actionId,
            QStringLiteral(
                "No pending High+Diff preview is available for "
                "confirmation."));
    }
    if (confirmationToken.isEmpty()
        || confirmationToken
            != currentPreview->confirmationToken) {
        return publish(
            RtlHighRiskEditWorkflowState::PreviewReady,
            RtlHighRiskEditWorkflowFailure::
                ConfirmationTokenMismatch,
            actionId,
            QStringLiteral(
                "The confirmation does not identify the pending "
                "High+Diff preview."),
            rtledit::TransactionStatus::Conflict);
    }

    const PreflightResult checked = preflight();
    if (!checked.ready()) {
        clearPending(false);
        return fail(
            checked.failure,
            actionId,
            checked.message,
            checked.transactionStatus,
            checked.conflictFile);
    }
    const SemanticSnapshotToken applyToken =
        index->snapshotToken();
    if (!sameSemanticToken(
            capturedSemanticToken, applyToken)) {
        clearPending(false);
        return fail(
            RtlHighRiskEditWorkflowFailure::
                StaleSemanticGeneration,
            actionId,
            QStringLiteral(
                "The semantic generation changed immediately before "
                "transaction apply."),
            rtledit::TransactionStatus::Stale);
    }

    const std::vector<CapturedDocument> before =
        capturedDocuments;
    const RtlHighRiskEditPreview acceptedPreview =
        *currentPreview;
    rtledit::PreparedWorkspaceEditTransaction prepared =
        std::move(*pendingTransaction);
    const std::uint64_t semanticRevision =
        applyToken.revision;
    clearPending(false);

    const rtledit::WorkspaceEditTransactionResult applied =
        transactionService->applyConfirmed(
            std::move(prepared),
            rtledit::SemanticIndexSnapshot{
                std::to_string(semanticRevision)},
            *documentManager);
    currentPreview = acceptedPreview;

    if (applied.status
            == rtledit::TransactionStatus::Applied) {
        workflowUndoAvailable = true;
        appliedTransactionGeneration =
            transactionService->historyGeneration();
        return publish(
            RtlHighRiskEditWorkflowState::Applied,
            RtlHighRiskEditWorkflowFailure::None,
            actionId,
            transactionMessage(applied),
            applied.status);
    }
    if (applied.status
            == rtledit::TransactionStatus::DryRunOnly) {
        workflowUndoAvailable = false;
        return publish(
            RtlHighRiskEditWorkflowState::DryRunComplete,
            RtlHighRiskEditWorkflowFailure::None,
            actionId,
            transactionMessage(applied),
            applied.status);
    }
    if (applied.status
            == rtledit::TransactionStatus::Stale) {
        const bool semanticStale =
            applied.applyResult.staleStatus.reason
            == rtledit::EditPlanStaleReason::
                SemanticSnapshotChanged;
        return fail(
            semanticStale
                ? RtlHighRiskEditWorkflowFailure::
                      StaleSemanticGeneration
                : RtlHighRiskEditWorkflowFailure::
                      StaleDocumentRevision,
            actionId,
            transactionMessage(applied),
            applied.status,
            fromUtf8(
                applied.applyResult.staleStatus.filePath));
    }
    if (applied.status
            == rtledit::TransactionStatus::
                InvalidPreparation
        || applied.status
            == rtledit::TransactionStatus::
                PreviewRequired) {
        return fail(
            RtlHighRiskEditWorkflowFailure::ApplyFailed,
            actionId,
            transactionMessage(applied),
            applied.status);
    }

    QString residualFile;
    if (!restoreTouchedDocuments(
            before, &residualFile)) {
        return fail(
            RtlHighRiskEditWorkflowFailure::
                AtomicRollbackFailed,
            actionId,
            QStringLiteral(
                "Atomic apply failed and at least one document could "
                "not be restored."),
            applied.status,
            residualFile);
    }
    return fail(
        RtlHighRiskEditWorkflowFailure::ApplyFailed,
        actionId,
        transactionMessage(applied),
        applied.status);
}

RtlHighRiskEditWorkflowResult
RtlHighRiskEditWorkflow::cancel()
{
    if (!pendingTransaction
        || !currentPreview
        || currentState
            != RtlHighRiskEditWorkflowState::PreviewReady) {
        return publish(
            currentState,
            RtlHighRiskEditWorkflowFailure::InvalidState,
            currentResult.actionId,
            QStringLiteral(
                "No pending RTL transformation preview is available "
                "to cancel."));
    }
    const QString actionId = currentPreview->actionId;
    clearPending(true);
    return publish(
        RtlHighRiskEditWorkflowState::Cancelled,
        RtlHighRiskEditWorkflowFailure::None,
        actionId,
        QStringLiteral(
            "The pending RTL transformation was cancelled without "
            "workspace changes."));
}

RtlHighRiskEditWorkflowResult
RtlHighRiskEditWorkflow::undo()
{
    if (!documentManager || !transactionService) {
        return fail(
            RtlHighRiskEditWorkflowFailure::MissingDependency,
            currentResult.actionId,
            QStringLiteral(
                "The RTL workspace transaction service is "
                "unavailable."));
    }
    if (currentState
            != RtlHighRiskEditWorkflowState::Applied
        || !workflowUndoAvailable) {
        workflowUndoAvailable = false;
        return fail(
            RtlHighRiskEditWorkflowFailure::NothingToUndo,
            currentResult.actionId,
            QStringLiteral(
                "No applied RTL transformation is available for "
                "single-step undo."),
            rtledit::TransactionStatus::NothingToUndo);
    }
    if (transactionService->historyGeneration()
            != appliedTransactionGeneration) {
        workflowUndoAvailable = false;
        return publish(
            RtlHighRiskEditWorkflowState::Applied,
            RtlHighRiskEditWorkflowFailure::
                TransactionGenerationConflict,
            currentResult.actionId,
            QStringLiteral(
                "A newer workspace transaction replaced this RTL "
                "workflow undo position."),
            rtledit::TransactionStatus::Conflict);
    }
    if (!transactionService->canUndo()) {
        workflowUndoAvailable = false;
        return publish(
            RtlHighRiskEditWorkflowState::Applied,
            RtlHighRiskEditWorkflowFailure::
                TransactionGenerationConflict,
            currentResult.actionId,
            QStringLiteral(
                "The shared transaction history no longer contains "
                "this RTL workflow undo position."),
            rtledit::TransactionStatus::Conflict);
    }

    const rtledit::WorkspaceEditTransactionResult undone =
        transactionService->undo(*documentManager);
    if (undone.status
            == rtledit::TransactionStatus::Undone) {
        workflowUndoAvailable = false;
        appliedTransactionGeneration =
            transactionService->historyGeneration();
        return publish(
            RtlHighRiskEditWorkflowState::Undone,
            RtlHighRiskEditWorkflowFailure::None,
            currentResult.actionId,
            transactionMessage(undone),
            undone.status);
    }

    const bool conflict =
        undone.status
        == rtledit::TransactionStatus::Conflict;
    return publish(
        conflict
            ? RtlHighRiskEditWorkflowState::Applied
            : RtlHighRiskEditWorkflowState::Failed,
        !undone.residualFiles.empty()
            ? RtlHighRiskEditWorkflowFailure::
                  AtomicRollbackFailed
            : conflict
                ? RtlHighRiskEditWorkflowFailure::
                      UndoConflict
                : RtlHighRiskEditWorkflowFailure::
                      UndoFailed,
        currentResult.actionId,
        transactionMessage(undone),
        undone.status,
        undone.residualFiles.empty()
            ? QString()
            : fromUtf8(
                  undone.residualFiles.front()));
}

RtlHighRiskEditWorkflowResult
RtlHighRiskEditWorkflow::retireUndoPosition()
{
    if (currentState
            != RtlHighRiskEditWorkflowState::Applied
        || !workflowUndoAvailable) {
        return publish(
            currentState,
            RtlHighRiskEditWorkflowFailure::InvalidState,
            currentResult.actionId,
            QStringLiteral(
                "No applied RTL workflow undo position is "
                "available to retire."));
    }
    workflowUndoAvailable = false;
    appliedTransactionGeneration = 0;
    return publish(
        RtlHighRiskEditWorkflowState::Applied,
        RtlHighRiskEditWorkflowFailure::None,
        currentResult.actionId,
        QStringLiteral(
            "The applied RTL transformation remains in the "
            "workspace; its workflow undo position was retired."),
        rtledit::TransactionStatus::Applied);
}

RtlHighRiskEditWorkflowResult
RtlHighRiskEditWorkflow::reportPlanningFailure(
    const QString& actionId,
    RtlHighRiskEditWorkflowFailure failure,
    const QString& message)
{
    if (!canStartPreview()) {
        return publish(
            RtlHighRiskEditWorkflowState::Applied,
            RtlHighRiskEditWorkflowFailure::InvalidState,
            currentResult.actionId,
            QStringLiteral(
                "Undo or retire the applied RTL transaction before "
                "starting another plan."));
    }
    clearPending(true);
    return fail(
        failure == RtlHighRiskEditWorkflowFailure::None
            ? RtlHighRiskEditWorkflowFailure::
                  PlanningRejected
            : failure,
        actionId,
        message,
        transactionStatusForFailure(failure));
}

RtlHighRiskEditWorkflowResult
RtlHighRiskEditWorkflow::reportNoChanges(
    const QString& actionId,
    const QString& message)
{
    if (!canStartPreview()) {
        return publish(
            RtlHighRiskEditWorkflowState::Applied,
            RtlHighRiskEditWorkflowFailure::InvalidState,
            currentResult.actionId,
            QStringLiteral(
                "Undo or retire the applied RTL transaction before "
                "starting another plan."));
    }
    clearPending(true);
    return publish(
        RtlHighRiskEditWorkflowState::NoChanges,
        RtlHighRiskEditWorkflowFailure::None,
        actionId,
        message);
}

void RtlHighRiskEditWorkflow::discardPendingPreview()
{
    if (!pendingTransaction)
        return;
    clearPending(true);
    currentState = RtlHighRiskEditWorkflowState::Idle;
    currentResult = RtlHighRiskEditWorkflowResult{};
}

RtlHighRiskEditWorkflowState
RtlHighRiskEditWorkflow::state() const
{
    return currentState;
}

const RtlHighRiskEditWorkflowResult&
RtlHighRiskEditWorkflow::lastResult() const
{
    return currentResult;
}

const RtlHighRiskEditPreview*
RtlHighRiskEditWorkflow::preview() const
{
    return currentPreview
        ? &*currentPreview : nullptr;
}

const rtledit::PreparedWorkspaceEditTransaction*
RtlHighRiskEditWorkflow::preparedTransaction() const
{
    return pendingTransaction
        ? &*pendingTransaction : nullptr;
}

bool RtlHighRiskEditWorkflow::hasPendingPreview() const
{
    return pendingTransaction.has_value()
        && currentPreview.has_value()
        && currentState
            == RtlHighRiskEditWorkflowState::PreviewReady;
}

bool RtlHighRiskEditWorkflow::canStartPreview() const
{
    return !workflowUndoAvailable;
}

bool RtlHighRiskEditWorkflow::
canUndoAppliedTransaction() const
{
    return workflowUndoAvailable
        && currentState
            == RtlHighRiskEditWorkflowState::Applied
        && transactionService
        && transactionService->canUndo()
        && transactionService->historyGeneration()
            == appliedTransactionGeneration;
}

RtlHighRiskEditWorkflowResult
RtlHighRiskEditWorkflow::publish(
    RtlHighRiskEditWorkflowState state,
    RtlHighRiskEditWorkflowFailure failure,
    const QString& actionId,
    const QString& message,
    rtledit::TransactionStatus transactionStatus,
    const QString& conflictFile)
{
    currentState = state;
    currentResult.state = state;
    currentResult.failure = failure;
    currentResult.transactionStatus = transactionStatus;
    currentResult.actionId = actionId;
    currentResult.message = message;
    currentResult.conflictFile = conflictFile;
    currentResult.preview =
        currentPreview
        ? *currentPreview
        : RtlHighRiskEditPreview{};
    return currentResult;
}

RtlHighRiskEditWorkflowResult
RtlHighRiskEditWorkflow::fail(
    RtlHighRiskEditWorkflowFailure failure,
    const QString& actionId,
    const QString& message,
    rtledit::TransactionStatus transactionStatus,
    const QString& conflictFile)
{
    return publish(
        RtlHighRiskEditWorkflowState::Failed,
        failure,
        actionId,
        message,
        transactionStatus,
        conflictFile);
}

void RtlHighRiskEditWorkflow::clearPending(
    bool clearPreview)
{
    pendingTransaction.reset();
    capturedDocuments.clear();
    capturedSemanticToken = {};
    previewTransactionGeneration = 0;
    previewSerial = 0;
    if (clearPreview)
        currentPreview.reset();
}

RtlHighRiskEditWorkflow::PreflightResult
RtlHighRiskEditWorkflow::captureContext(
    const rtledit::WorkspaceEditPlan& plan)
{
    capturedDocuments.clear();
    QHash<QString, int> capturedByIdentity;

    auto reject =
        [](RtlHighRiskEditWorkflowFailure failure,
           rtledit::TransactionStatus status,
           const QString& message,
           const QString& fileName) {
            PreflightResult result;
            result.failure = failure;
            result.transactionStatus = status;
            result.message = message;
            result.conflictFile = fileName;
            return result;
        };

    auto capture =
        [&](const std::string& filePath,
            bool touched,
            std::optional<rtledit::DocumentVersion>
                expectedVersion) -> PreflightResult {
            const QString fileName =
                normalizedFileName(filePath);
            const QString identity =
                documentIdentity(fileName);
            if (identity.isEmpty()) {
                return reject(
                    RtlHighRiskEditWorkflowFailure::
                        InvalidPlan,
                    rtledit::TransactionStatus::
                        InvalidPreparation,
                    QStringLiteral(
                        "The RTL edit plan contains an invalid file "
                        "identity."),
                    fileName);
            }

            const auto live =
                documentManager->snapshot(filePath);
            if (!live) {
                return reject(
                    RtlHighRiskEditWorkflowFailure::
                        StaleDocumentRevision,
                    rtledit::TransactionStatus::Stale,
                    QStringLiteral(
                        "A plan document is unavailable: %1")
                        .arg(fileName),
                    fileName);
            }
            if (expectedVersion
                && live->version != *expectedVersion) {
                return reject(
                    RtlHighRiskEditWorkflowFailure::
                        StaleDocumentRevision,
                    rtledit::TransactionStatus::Stale,
                    QStringLiteral(
                        "A plan document revision changed before "
                        "preview: %1")
                        .arg(fileName),
                    fileName);
            }

            const auto found =
                capturedByIdentity.constFind(identity);
            if (found
                != capturedByIdentity.constEnd()) {
                CapturedDocument& existing =
                    capturedDocuments[
                        static_cast<std::size_t>(
                            found.value())];
                if (touched
                    && existing.touched
                    && existing.filePath != filePath) {
                    return reject(
                        RtlHighRiskEditWorkflowFailure::
                            InvalidPlan,
                        rtledit::TransactionStatus::
                            InvalidPreparation,
                        QStringLiteral(
                            "One document is addressed by multiple "
                            "edit paths: %1")
                            .arg(fileName),
                        fileName);
                }
                if (!sameSnapshot(
                        existing.snapshot, *live)) {
                    return reject(
                        RtlHighRiskEditWorkflowFailure::
                            PreviewConflict,
                        rtledit::TransactionStatus::Conflict,
                        QStringLiteral(
                            "The same document resolved to "
                            "inconsistent snapshots: %1")
                            .arg(fileName),
                        fileName);
                }
                existing.touched =
                    existing.touched || touched;
                return {};
            }

            const int index =
                static_cast<int>(
                    capturedDocuments.size());
            capturedByIdentity.insert(identity, index);
            capturedDocuments.push_back(
                CapturedDocument{
                    filePath,
                    fileName,
                    *live,
                    readExternalState(fileName),
                    touched});
            return {};
        };

    for (const rtledit::DocumentBaseline& baseline :
         plan.baselines) {
        const PreflightResult result =
            capture(
                baseline.filePath,
                true,
                baseline.version);
        if (!result.ready())
            return result;
    }
    for (const std::string& filePath :
         plan.semanticIndexFilePaths) {
        const PreflightResult result =
            capture(filePath, false, std::nullopt);
        if (!result.ready())
            return result;
    }
    return {};
}

RtlHighRiskEditWorkflow::PreflightResult
RtlHighRiskEditWorkflow::preflight() const
{
    auto reject =
        [](RtlHighRiskEditWorkflowFailure failure,
           rtledit::TransactionStatus status,
           const QString& message,
           const QString& fileName = QString()) {
            PreflightResult result;
            result.failure = failure;
            result.transactionStatus = status;
            result.message = message;
            result.conflictFile = fileName;
            return result;
        };

    if (!index
        || !documentManager
        || !transactionService
        || !pendingTransaction
        || !currentPreview
        || !capturedSemanticToken.isValid()) {
        return reject(
            RtlHighRiskEditWorkflowFailure::InvalidState,
            rtledit::TransactionStatus::
                InvalidPreparation,
            QStringLiteral(
                "The pending RTL edit revision context is "
                "incomplete."));
    }

    const SemanticSnapshotToken liveToken =
        index->snapshotToken();
    if (!sameSemanticToken(
            capturedSemanticToken, liveToken)
        || pendingTransaction->plan.semanticSnapshot.id
            != std::to_string(liveToken.revision)) {
        return reject(
            RtlHighRiskEditWorkflowFailure::
                StaleSemanticGeneration,
            rtledit::TransactionStatus::Stale,
            QStringLiteral(
                "The semantic generation changed after preview."));
    }
    if (transactionService->historyGeneration()
            != previewTransactionGeneration) {
        return reject(
            RtlHighRiskEditWorkflowFailure::
                TransactionGenerationConflict,
            rtledit::TransactionStatus::Conflict,
            QStringLiteral(
                "The shared workspace transaction generation changed "
                "after preview."));
    }
    if (!pendingTransaction->ready()
        || pendingTransaction->previewConfirmed
        || currentPreview->confirmationToken
            != buildConfirmationToken(
                *pendingTransaction,
                currentPreview->actionId,
                previewSerial,
                previewTransactionGeneration)) {
        return reject(
            RtlHighRiskEditWorkflowFailure::PreviewConflict,
            rtledit::TransactionStatus::Conflict,
            QStringLiteral(
                "The pending transaction no longer matches the "
                "displayed High+Diff preview."));
    }

    for (const CapturedDocument& captured :
         capturedDocuments) {
        if (readExternalState(
                captured.normalizedFileName)
                != captured.externalStateToken) {
            return reject(
                RtlHighRiskEditWorkflowFailure::
                    ExternalModification,
                rtledit::TransactionStatus::Conflict,
                QStringLiteral(
                    "A file changed externally after preview: %1")
                    .arg(
                        captured.normalizedFileName),
                captured.normalizedFileName);
        }

        const auto live =
            documentManager->snapshot(
                captured.filePath);
        if (!live
            || !sameSnapshot(
                *live, captured.snapshot)) {
            return reject(
                RtlHighRiskEditWorkflowFailure::
                    StaleDocumentRevision,
                rtledit::TransactionStatus::Stale,
                QStringLiteral(
                    "A document revision or text changed after "
                    "preview: %1")
                    .arg(
                        captured.normalizedFileName),
                captured.normalizedFileName);
        }
    }

    const rtledit::EditPlanStaleStatus stale =
        rtledit::staleStatus(
            pendingTransaction->plan,
            rtledit::SemanticIndexSnapshot{
                std::to_string(liveToken.revision)},
            *documentManager);
    if (stale.stale()) {
        const bool semanticStale =
            stale.reason
            == rtledit::EditPlanStaleReason::
                SemanticSnapshotChanged;
        return reject(
            semanticStale
                ? RtlHighRiskEditWorkflowFailure::
                      StaleSemanticGeneration
                : RtlHighRiskEditWorkflowFailure::
                      StaleDocumentRevision,
            rtledit::TransactionStatus::Stale,
            QStringLiteral(
                "The High+Diff transaction became stale before "
                "confirmation."),
            fromUtf8(stale.filePath));
    }
    return {};
}

bool RtlHighRiskEditWorkflow::
restoreTouchedDocuments(
    const std::vector<CapturedDocument>& before,
    QString* firstResidualFile)
{
    bool restored = true;
    for (const CapturedDocument& captured : before) {
        if (!captured.touched)
            continue;
        const auto current =
            documentManager->snapshot(
                captured.filePath);
        if (current
            && current->text
                == captured.snapshot.text) {
            continue;
        }
        if (!documentManager->restoreSnapshot(
                captured.filePath,
                captured.snapshot)) {
            restored = false;
            if (firstResidualFile
                && firstResidualFile->isEmpty()) {
                *firstResidualFile =
                    captured.normalizedFileName;
            }
        }
    }
    for (const CapturedDocument& captured : before) {
        if (!captured.touched)
            continue;
        const auto current =
            documentManager->snapshot(
                captured.filePath);
        if (!current
            || current->text
                != captured.snapshot.text) {
            restored = false;
            if (firstResidualFile
                && firstResidualFile->isEmpty()) {
                *firstResidualFile =
                    captured.normalizedFileName;
            }
        }
    }
    return restored;
}

QString RtlHighRiskEditWorkflow::
buildConfirmationToken(
    const rtledit::PreparedWorkspaceEditTransaction& prepared,
    const QString& actionId,
    std::uint64_t serial,
    std::uint64_t transactionGeneration) const
{
    QCryptographicHash hash(
        QCryptographicHash::Sha256);
    addHashNumber(hash, serial);
    addHashNumber(hash, transactionGeneration);
    addHashField(hash, actionId.toUtf8());
    addHashNumber(
        hash, capturedSemanticToken.revision);
    addHashField(
        hash, prepared.plan.semanticSnapshot.id);
    addHashNumber(
        hash,
        static_cast<std::uint64_t>(
            prepared.plan.intent.kind));
    addHashNumber(
        hash,
        static_cast<std::uint64_t>(
            prepared.plan.intent.target.kind));
    addHashField(
        hash,
        prepared.plan.intent.target.qualifiedName);
    addHashField(
        hash,
        prepared.plan.intent.target.ownerScope);
    addHashField(
        hash,
        prepared.plan.intent.target.filePath);
    addHashNumber(
        hash,
        prepared.plan.intent.target.range.start.line);
    addHashNumber(
        hash,
        prepared.plan.intent.target.range.start.column);
    addHashNumber(
        hash,
        prepared.plan.intent.target.range.end.line);
    addHashNumber(
        hash,
        prepared.plan.intent.target.range.end.column);
    addHashField(
        hash,
        prepared.plan.intent.target.signatureHash);
    addHashNumber(
        hash,
        static_cast<std::uint64_t>(
            prepared.plan.riskLevel));
    addHashNumber(
        hash,
        static_cast<std::uint64_t>(
            prepared.plan.previewPolicy));
    addHashNumber(hash, prepared.dryRun ? 1 : 0);
    for (const std::string& filePath :
         prepared.plan.semanticIndexFilePaths) {
        addHashField(hash, filePath);
    }
    for (const rtledit::DocumentBaseline& baseline :
         prepared.plan.baselines) {
        addHashField(hash, baseline.filePath);
        addHashNumber(hash, baseline.version.value);
    }
    for (const rtledit::WorkspaceTextEdit& edit :
         prepared.plan.edits) {
        addHashField(hash, edit.filePath);
        addHashNumber(
            hash, edit.expectedDocumentVersion.value);
        addHashNumber(hash, edit.range.start.line);
        addHashNumber(hash, edit.range.start.column);
        addHashNumber(hash, edit.range.end.line);
        addHashNumber(hash, edit.range.end.column);
        addHashField(hash, edit.expectedText);
        addHashField(hash, edit.newText);
    }
    for (const rtledit::TextEditProvenance& provenance :
         prepared.plan.provenance) {
        addHashNumber(hash, provenance.editIndex);
        addHashField(hash, provenance.actionId);
        addHashField(hash, provenance.anchorName);
        addHashField(hash, provenance.description);
        addHashNumber(
            hash,
            static_cast<std::uint64_t>(
                provenance.anchor.source));
        addHashField(hash, provenance.anchor.resolver);
        addHashField(
            hash,
            provenance.anchor.semanticSnapshotId);
        addHashField(
            hash, provenance.signalQualifiedName);
        addHashField(
            hash, provenance.sourceInstancePath);
        addHashNumber(
            hash,
            provenance.hierarchyStepIndex
                ? static_cast<std::uint64_t>(
                      *provenance.hierarchyStepIndex)
                : std::numeric_limits<
                      std::uint64_t>::max());
        addHashField(hash, provenance.sourceFilePath);
        addHashNumber(
            hash, provenance.sourceRange.start.line);
        addHashNumber(
            hash, provenance.sourceRange.start.column);
        addHashNumber(
            hash, provenance.sourceRange.end.line);
        addHashNumber(
            hash, provenance.sourceRange.end.column);
    }
    addHashField(
        hash,
        rtledit::renderWorkspaceEditSourceDiffHunks(
            prepared.sourceDiff));
    return QString::fromLatin1(
        hash.result().toHex());
}

QString RtlHighRiskEditWorkflow::normalizedFileName(
    const std::string& filePath)
{
    const QString fileName = fromUtf8(filePath);
    if (fileName.isEmpty())
        return {};
    return QDir::cleanPath(
        QDir::fromNativeSeparators(
            QFileInfo(fileName).absoluteFilePath()));
}

QString RtlHighRiskEditWorkflow::documentIdentity(
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

QString RtlHighRiskEditWorkflow::
defaultExternalStateToken(
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
            .arg(
                info.lastModified()
                    .toMSecsSinceEpoch())
            .arg(
                static_cast<int>(
                    info.permissions()));
    }
    const QByteArray digest =
        QCryptographicHash::hash(
            file.readAll(),
            QCryptographicHash::Sha256)
            .toHex();
    return QStringLiteral("%1|%2|%3|%4")
        .arg(info.size())
        .arg(
            info.lastModified()
                .toMSecsSinceEpoch())
        .arg(
            static_cast<int>(
                info.permissions()))
        .arg(QString::fromLatin1(digest));
}
