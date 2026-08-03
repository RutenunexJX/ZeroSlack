#ifndef INSTANCEPAIRCONNECTIONWORKFLOW_H
#define INSTANCEPAIRCONNECTIONWORKFLOW_H

#include "instancepairconnectionpanel.h"

#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class SemanticIndex;
class WorkspaceEditTransactionService;

enum class InstancePairConnectionWorkflowState {
    Idle,
    Analyzed,
    PreviewReady,
    NoChanges,
    Cancelled,
    Applied,
    DryRunComplete,
    Undone,
    Failed
};

enum class InstancePairConnectionWorkflowFailure {
    None,
    MissingDependency,
    InvalidState,
    InvalidRequest,
    RequestMismatch,
    AnalysisRejected,
    PlanningRejected,
    StaleSemanticGeneration,
    StaleDocumentRevision,
    Conflict,
    Cancelled,
    ApplyFailed,
    AtomicRollbackFailed,
    UndoFailed
};

struct InstancePairConnectionWorkflowResult {
    InstancePairConnectionWorkflowState state =
        InstancePairConnectionWorkflowState::Idle;
    InstancePairConnectionWorkflowFailure failure =
        InstancePairConnectionWorkflowFailure::None;
    rtledit::TransactionStatus transactionStatus =
        rtledit::TransactionStatus::InvalidPreparation;
    QString message;

    bool succeeded() const;
};

Q_DECLARE_METATYPE(InstancePairConnectionWorkflowState)
Q_DECLARE_METATYPE(InstancePairConnectionWorkflowFailure)
Q_DECLARE_METATYPE(InstancePairConnectionWorkflowResult)

class InstancePairConnectionWorkflow final : public QObject
{
    Q_OBJECT

public:
    using AnalyzeOperation = std::function<
        InstancePairConnectionAnalysis(
            const InstancePairConnectionQuery&,
            rtledit::WorkspaceDocumentManager&)>;
    using PlanOperation = std::function<
        InstancePairConnectionProposal(
            const InstancePairConnectionAnalysis&,
            rtledit::WorkspaceDocumentManager&)>;

    struct Operations {
        AnalyzeOperation analyze;
        PlanOperation plan;

        bool isValid() const;
    };

    // Omitting transactions gives this workflow an isolated undo history.
    // With an injected shared service, generation ownership prevents this
    // workflow from undoing a newer action's transaction.
    InstancePairConnectionWorkflow(
        InstancePairConnectionFacade* facade,
        InstancePairConnectionCoordinator* coordinator,
        SemanticIndex* semanticIndex,
        rtledit::WorkspaceDocumentManager* documents,
        WorkspaceEditTransactionService* transactions = nullptr,
        QObject* parent = nullptr);
    InstancePairConnectionWorkflow(
        Operations operations,
        InstancePairConnectionCoordinator* coordinator,
        SemanticIndex* semanticIndex,
        rtledit::WorkspaceDocumentManager* documents,
        WorkspaceEditTransactionService* transactions = nullptr,
        QObject* parent = nullptr);
    ~InstancePairConnectionWorkflow() override;

    InstancePairConnectionWorkflowResult analyzeAndPresent(
        const InstancePairConnectionQuery& query);
    InstancePairConnectionWorkflowResult requestPreview(
        const InstancePairConnectionPlanRequest& request);
    InstancePairConnectionWorkflowResult confirm(
        const InstancePairConnectionPlanRequest& request);
    InstancePairConnectionWorkflowResult cancel();
    InstancePairConnectionWorkflowResult undo();

    InstancePairConnectionWorkflowState state() const;
    const InstancePairConnectionWorkflowResult& lastResult() const;
    const InstancePairConnectionAnalysis* currentAnalysis() const;
    const InstancePairConnectionProposal* currentProposal() const;
    bool hasPendingPreview() const;
    bool canUndoAppliedTransaction() const;

signals:
    void stateChanged(
        InstancePairConnectionWorkflowResult result);

private:
    struct CapturedDocument {
        std::string filePath;
        rtledit::WorkspaceDocumentSnapshot snapshot;
    };

    struct PreflightResult {
        InstancePairConnectionWorkflowFailure failure =
            InstancePairConnectionWorkflowFailure::None;
        QString message;

        bool ready() const {
            return failure
                == InstancePairConnectionWorkflowFailure::None;
        }
    };

    Operations workflowOperations;
    InstancePairConnectionCoordinator* panelCoordinator = nullptr;
    SemanticIndex* index = nullptr;
    rtledit::WorkspaceDocumentManager* documentManager = nullptr;
    std::unique_ptr<WorkspaceEditTransactionService>
        ownedTransactionService;
    WorkspaceEditTransactionService* transactionService = nullptr;

    InstancePairConnectionWorkflowState currentState =
        InstancePairConnectionWorkflowState::Idle;
    InstancePairConnectionWorkflowResult currentResult;
    std::optional<InstancePairConnectionAnalysis> activeAnalysis;
    std::optional<InstancePairConnectionProposal> activeProposal;
    InstancePairConnectionPlanRequest activeRequest;
    bool workflowUndoAvailable = false;
    std::uint64_t appliedTransactionGeneration = 0;

    void connectPanelSignals();
    void discardPendingProposal();
    InstancePairConnectionWorkflowResult publish(
        InstancePairConnectionWorkflowState state,
        InstancePairConnectionWorkflowFailure failure,
        const QString& message,
        rtledit::TransactionStatus transactionStatus =
            rtledit::TransactionStatus::InvalidPreparation);
    InstancePairConnectionWorkflowResult fail(
        InstancePairConnectionWorkflowFailure failure,
        const QString& message,
        rtledit::TransactionStatus transactionStatus =
            rtledit::TransactionStatus::InvalidPreparation);
    PreflightResult preflight(
        const InstancePairConnectionPlanRequest& request,
        const InstancePairConnectionProposal* proposal) const;
    bool requestMatchesCurrentContext(
        const InstancePairConnectionPlanRequest& request) const;
    bool displayedProposalMatchesPending() const;
    std::optional<std::vector<CapturedDocument>>
        capturePlanDocuments(
            const InstancePairConnectionProposal& proposal) const;
    bool restoreAfterFailedApply(
        const std::vector<CapturedDocument>& before);
    static InstancePairConnectionWorkflowFailure
        workflowFailure(
            InstancePairConnectionFailure failure,
            InstancePairConnectionWorkflowFailure fallback);
};

#endif // INSTANCEPAIRCONNECTIONWORKFLOW_H
