#ifndef RTLRENAMEWORKFLOW_H
#define RTLRENAMEWORKFLOW_H

#include "rtlhighriskeditworkflow.h"
#include "rtlrenameplanner.h"

#include <functional>
#include <optional>

// Production facade for RtlRenamePlanner. It leaves declaration/reference and
// named-association semantics in the planner, then delegates the complete
// preview/apply/undo protocol to RtlHighRiskEditWorkflow.
class RtlRenameWorkflow
{
public:
    using PlanOperation = std::function<
        RtlRenameProposal(
            const RtlRenamePlanQuery&,
            const rtledit::WorkspaceDocumentManager&)>;

    RtlRenameWorkflow(
        SemanticIndex* semanticIndex,
        rtledit::WorkspaceDocumentManager* documents,
        WorkspaceEditTransactionService* transactions = nullptr,
        RtlHighRiskEditWorkflow::ExternalStateProvider
            externalStateProvider = {});
    RtlRenameWorkflow(
        PlanOperation planOperation,
        SemanticIndex* semanticIndex,
        rtledit::WorkspaceDocumentManager* documents,
        WorkspaceEditTransactionService* transactions = nullptr,
        RtlHighRiskEditWorkflow::ExternalStateProvider
            externalStateProvider = {});

    RtlHighRiskEditWorkflowResult preparePreview(
        const RtlRenamePlanQuery& query);
    RtlHighRiskEditWorkflowResult confirm(
        const QString& confirmationToken);
    RtlHighRiskEditWorkflowResult cancel();
    RtlHighRiskEditWorkflowResult undo();
    RtlHighRiskEditWorkflowResult retireUndoPosition();

    const RtlRenameProposal* proposal() const;
    RtlRenamePlanStatus lastPlanStatus() const;
    RtlHighRiskEditWorkflow& transactionWorkflow();
    const RtlHighRiskEditWorkflow&
        transactionWorkflow() const;

    static QString actionFamilyId();

private:
    SemanticIndex* index = nullptr;
    rtledit::WorkspaceDocumentManager* documentManager = nullptr;
    PlanOperation planRename;
    RtlHighRiskEditWorkflow workflow;
    std::optional<RtlRenameProposal> activeProposal;
    RtlRenamePlanStatus plannerStatus =
        RtlRenamePlanStatus::InvalidRequest;

    static RtlHighRiskEditWorkflowFailure workflowFailure(
        RtlRenamePlanStatus status);
    static QString proposalActionId(
        const RtlRenameProposal& proposal);
};

#endif // RTLRENAMEWORKFLOW_H
