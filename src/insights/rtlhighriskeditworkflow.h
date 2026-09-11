#ifndef RTLHIGHRISKEDITWORKFLOW_H
#define RTLHIGHRISKEDITWORKFLOW_H

#include "semanticindex.h"

#include <rtledit/workspace_edit_transaction.h>

#include <QString>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class WorkspaceEditTransactionService;

enum class RtlHighRiskEditWorkflowState {
    Idle,
    PreviewReady,
    NoChanges,
    Cancelled,
    Applied,
    DryRunComplete,
    Undone,
    Failed
};

enum class RtlHighRiskEditWorkflowFailure {
    None,
    MissingDependency,
    InvalidState,
    PlanningRejected,
    InvalidPlan,
    PreviewConflict,
    ConfirmationTokenMismatch,
    StaleSemanticGeneration,
    StaleDocumentRevision,
    ExternalModification,
    TransactionGenerationConflict,
    ApplyFailed,
    AtomicRollbackFailed,
    NothingToUndo,
    UndoConflict,
    UndoFailed
};

struct RtlHighRiskEditPreview {
    QString actionId;
    QString confirmationToken;
    bool dryRun = false;
    int fileCount = 0;
    int editCount = 0;
    rtledit::RiskLevel riskLevel =
        rtledit::RiskLevel::Low;
    rtledit::PreviewPolicy previewPolicy =
        rtledit::PreviewPolicy::Inline;
    rtledit::WorkspaceEditPreview structuredPreview;
    rtledit::WorkspaceEditSourceDiff sourceDiff;
    QString renderedDiff;

    bool ready() const;
};

struct RtlHighRiskEditWorkflowResult {
    RtlHighRiskEditWorkflowState state =
        RtlHighRiskEditWorkflowState::Idle;
    RtlHighRiskEditWorkflowFailure failure =
        RtlHighRiskEditWorkflowFailure::None;
    rtledit::TransactionStatus transactionStatus =
        rtledit::TransactionStatus::InvalidPreparation;
    QString actionId;
    QString message;
    QString conflictFile;
    RtlHighRiskEditPreview preview;

    bool succeeded() const;
};

// Owns one preview/apply/undo position. The semantic planners remain
// responsible for edit semantics; this class only enforces the high-risk
// transaction protocol around an already-built WorkspaceEditPlan.
class RtlHighRiskEditWorkflow
{
public:
    using ExternalStateProvider =
        std::function<QString(const QString& fileName)>;

    RtlHighRiskEditWorkflow(
        SemanticIndex* semanticIndex,
        rtledit::WorkspaceDocumentManager* documents,
        WorkspaceEditTransactionService* transactions = nullptr,
        ExternalStateProvider externalStateProvider = {});
    ~RtlHighRiskEditWorkflow();

    RtlHighRiskEditWorkflow(
        const RtlHighRiskEditWorkflow&) = delete;
    RtlHighRiskEditWorkflow& operator=(
        const RtlHighRiskEditWorkflow&) = delete;

    RtlHighRiskEditWorkflowResult preparePreview(
        const QString& actionId,
        rtledit::WorkspaceEditPlan plan,
        const SemanticSnapshotToken& expectedSemanticToken,
        bool dryRun);
    RtlHighRiskEditWorkflowResult confirm(
        const QString& confirmationToken);
    RtlHighRiskEditWorkflowResult cancel();
    RtlHighRiskEditWorkflowResult undo();
    RtlHighRiskEditWorkflowResult retireUndoPosition();

    // Planner facades use these two transitions when no transaction can be
    // prepared. They preserve an outstanding applied undo position.
    RtlHighRiskEditWorkflowResult reportPlanningFailure(
        const QString& actionId,
        RtlHighRiskEditWorkflowFailure failure,
        const QString& message);
    RtlHighRiskEditWorkflowResult reportNoChanges(
        const QString& actionId,
        const QString& message);

    void discardPendingPreview();

    RtlHighRiskEditWorkflowState state() const;
    const RtlHighRiskEditWorkflowResult& lastResult() const;
    const RtlHighRiskEditPreview* preview() const;
    const rtledit::PreparedWorkspaceEditTransaction*
        preparedTransaction() const;
    bool hasPendingPreview() const;
    bool canStartPreview() const;
    bool canUndoAppliedTransaction() const;

private:
    struct CapturedDocument {
        std::string filePath;
        QString normalizedFileName;
        rtledit::WorkspaceDocumentSnapshot snapshot;
        QString externalStateToken;
        bool touched = false;
    };

    struct PreflightResult {
        RtlHighRiskEditWorkflowFailure failure =
            RtlHighRiskEditWorkflowFailure::None;
        rtledit::TransactionStatus transactionStatus =
            rtledit::TransactionStatus::InvalidPreparation;
        QString message;
        QString conflictFile;

        bool ready() const {
            return failure
                == RtlHighRiskEditWorkflowFailure::None;
        }
    };

    SemanticIndex* index = nullptr;
    rtledit::WorkspaceDocumentManager* documentManager = nullptr;
    std::unique_ptr<WorkspaceEditTransactionService>
        ownedTransactionService;
    WorkspaceEditTransactionService* transactionService = nullptr;
    ExternalStateProvider readExternalState;

    RtlHighRiskEditWorkflowState currentState =
        RtlHighRiskEditWorkflowState::Idle;
    RtlHighRiskEditWorkflowResult currentResult;
    std::optional<rtledit::PreparedWorkspaceEditTransaction>
        pendingTransaction;
    std::optional<RtlHighRiskEditPreview> currentPreview;
    std::vector<CapturedDocument> capturedDocuments;
    SemanticSnapshotToken capturedSemanticToken;
    std::uint64_t previewTransactionGeneration = 0;
    std::uint64_t appliedTransactionGeneration = 0;
    std::uint64_t previewSerial = 0;
    std::uint64_t nextPreviewSerial = 1;
    bool workflowUndoAvailable = false;

    RtlHighRiskEditWorkflowResult publish(
        RtlHighRiskEditWorkflowState state,
        RtlHighRiskEditWorkflowFailure failure,
        const QString& actionId,
        const QString& message,
        rtledit::TransactionStatus transactionStatus =
            rtledit::TransactionStatus::InvalidPreparation,
        const QString& conflictFile = {});
    RtlHighRiskEditWorkflowResult fail(
        RtlHighRiskEditWorkflowFailure failure,
        const QString& actionId,
        const QString& message,
        rtledit::TransactionStatus transactionStatus =
            rtledit::TransactionStatus::InvalidPreparation,
        const QString& conflictFile = {});
    void clearPending(bool clearPreview);
    PreflightResult captureContext(
        const rtledit::WorkspaceEditPlan& plan);
    PreflightResult preflight() const;
    bool restoreTouchedDocuments(
        const std::vector<CapturedDocument>& before,
        QString* firstResidualFile);
    QString buildConfirmationToken(
        const rtledit::PreparedWorkspaceEditTransaction& prepared,
        const QString& actionId,
        std::uint64_t serial,
        std::uint64_t transactionGeneration) const;

    static QString normalizedFileName(
        const std::string& filePath);
    static QString documentIdentity(
        const QString& fileName);
    static QString defaultExternalStateToken(
        const QString& fileName);
};

#endif // RTLHIGHRISKEDITWORKFLOW_H
