#ifndef RTLCONNECTIONTRANSFORMWORKFLOW_H
#define RTLCONNECTIONTRANSFORMWORKFLOW_H

#include "rtlconnectiontransformplanner.h"
#include "rtlhighriskeditworkflow.h"

#include <functional>
#include <optional>

// Production facade for ordered-to-named conversion, safe missing-port
// completion, and provable width/signed casts planned by
// RtlConnectionTransformPlanner.
class RtlConnectionTransformWorkflow
{
public:
    using PlanOperation = std::function<
        RtlConnectionTransformReport(
            const RtlConnectionTransformRequest&,
            const rtledit::WorkspaceDocumentManager&)>;

    RtlConnectionTransformWorkflow(
        SemanticIndex* semanticIndex,
        rtledit::WorkspaceDocumentManager* documents,
        WorkspaceEditTransactionService* transactions = nullptr,
        RtlHighRiskEditWorkflow::ExternalStateProvider
            externalStateProvider = {});
    RtlConnectionTransformWorkflow(
        PlanOperation planOperation,
        SemanticIndex* semanticIndex,
        rtledit::WorkspaceDocumentManager* documents,
        WorkspaceEditTransactionService* transactions = nullptr,
        RtlHighRiskEditWorkflow::ExternalStateProvider
            externalStateProvider = {});

    RtlHighRiskEditWorkflowResult preparePreview(
        const RtlConnectionTransformRequest& request,
        bool dryRun = false);
    RtlHighRiskEditWorkflowResult confirm(
        const QString& confirmationToken);
    RtlHighRiskEditWorkflowResult cancel();
    RtlHighRiskEditWorkflowResult undo();
    RtlHighRiskEditWorkflowResult retireUndoPosition();

    const RtlConnectionTransformReport* report() const;
    RtlConnectionTransformStatus lastPlanStatus() const;
    RtlConnectionTransformFailure lastPlanFailure() const;
    RtlHighRiskEditWorkflow& transactionWorkflow();
    const RtlHighRiskEditWorkflow&
        transactionWorkflow() const;

    static QString actionId();

private:
    SemanticIndex* index = nullptr;
    rtledit::WorkspaceDocumentManager* documentManager = nullptr;
    PlanOperation planTransform;
    RtlHighRiskEditWorkflow workflow;
    std::optional<RtlConnectionTransformReport> activeReport;
    RtlConnectionTransformStatus plannerStatus =
        RtlConnectionTransformStatus::Rejected;
    RtlConnectionTransformFailure plannerFailure =
        RtlConnectionTransformFailure::InvalidRequest;

    static RtlHighRiskEditWorkflowFailure workflowFailure(
        RtlConnectionTransformFailure failure);
};

#endif // RTLCONNECTIONTRANSFORMWORKFLOW_H
