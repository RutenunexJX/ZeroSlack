#include "rtlrenameworkflow.h"

#include <memory>
#include <utility>

namespace {

SemanticIndex* resolvedIndex(SemanticIndex* index)
{
    return index
        ? index : SemanticIndex::getInstance();
}

RtlRenameWorkflow::PlanOperation defaultPlanOperation(
    SemanticIndex* index)
{
    auto planner =
        std::make_shared<RtlRenamePlanner>(
            resolvedIndex(index));
    return [planner](
               const RtlRenamePlanQuery& query,
               const rtledit::WorkspaceDocumentManager&
                   documents) {
        return planner->plan(query, documents);
    };
}

QString fromUtf8(const std::string& text)
{
    return QString::fromUtf8(
        text.data(), static_cast<qsizetype>(text.size()));
}

} // namespace

RtlRenameWorkflow::RtlRenameWorkflow(
    SemanticIndex* semanticIndex,
    rtledit::WorkspaceDocumentManager* documents,
    WorkspaceEditTransactionService* transactions,
    RtlHighRiskEditWorkflow::ExternalStateProvider
        externalStateProvider)
    : RtlRenameWorkflow(
          defaultPlanOperation(semanticIndex),
          semanticIndex,
          documents,
          transactions,
          std::move(externalStateProvider))
{
}

RtlRenameWorkflow::RtlRenameWorkflow(
    PlanOperation planOperation,
    SemanticIndex* semanticIndex,
    rtledit::WorkspaceDocumentManager* documents,
    WorkspaceEditTransactionService* transactions,
    RtlHighRiskEditWorkflow::ExternalStateProvider
        externalStateProvider)
    : index(resolvedIndex(semanticIndex))
    , documentManager(documents)
    , planRename(std::move(planOperation))
    , workflow(
          index,
          documents,
          transactions,
          std::move(externalStateProvider))
{
}

RtlHighRiskEditWorkflowResult
RtlRenameWorkflow::preparePreview(
    const RtlRenamePlanQuery& query)
{
    if (!workflow.canStartPreview()) {
        return workflow.reportPlanningFailure(
            actionFamilyId(),
            RtlHighRiskEditWorkflowFailure::InvalidState,
            QStringLiteral(
                "An applied RTL rename is still awaiting its "
                "single-step undo."));
    }

    activeProposal.reset();
    plannerStatus = RtlRenamePlanStatus::InvalidRequest;
    if (!planRename || !index || !documentManager) {
        return workflow.reportPlanningFailure(
            actionFamilyId(),
            RtlHighRiskEditWorkflowFailure::
                MissingDependency,
            QStringLiteral(
                "The RTL rename planner is unavailable."));
    }

    RtlRenameProposal planned =
        planRename(query, *documentManager);
    plannerStatus = planned.status;
    activeProposal = std::move(planned);

    if (plannerStatus == RtlRenamePlanStatus::NoChange) {
        return workflow.reportNoChanges(
            actionFamilyId(),
            activeProposal->message.isEmpty()
                ? QStringLiteral(
                      "The requested RTL name is unchanged.")
                : activeProposal->message);
    }
    if (plannerStatus != RtlRenamePlanStatus::Ready
        || activeProposal->workspaceEdit.edits.empty()) {
        const RtlHighRiskEditWorkflowFailure failure =
            workflowFailure(plannerStatus);
        return workflow.reportPlanningFailure(
            actionFamilyId(),
            failure,
            activeProposal->message.isEmpty()
                ? QStringLiteral(
                      "The RTL rename planner rejected the "
                      "request.")
                : activeProposal->message);
    }
    if (activeProposal->dryRun != query.dryRun) {
        return workflow.reportPlanningFailure(
            proposalActionId(*activeProposal),
            RtlHighRiskEditWorkflowFailure::
                PreviewConflict,
            QStringLiteral(
                "The rename proposal dry-run mode does not match "
                "the request."));
    }

    return workflow.preparePreview(
        proposalActionId(*activeProposal),
        activeProposal->workspaceEdit,
        query.semanticToken,
        query.dryRun);
}

RtlHighRiskEditWorkflowResult
RtlRenameWorkflow::confirm(
    const QString& confirmationToken)
{
    return workflow.confirm(confirmationToken);
}

RtlHighRiskEditWorkflowResult
RtlRenameWorkflow::cancel()
{
    return workflow.cancel();
}

RtlHighRiskEditWorkflowResult
RtlRenameWorkflow::undo()
{
    return workflow.undo();
}

RtlHighRiskEditWorkflowResult
RtlRenameWorkflow::retireUndoPosition()
{
    return workflow.retireUndoPosition();
}

const RtlRenameProposal*
RtlRenameWorkflow::proposal() const
{
    return activeProposal
        ? &*activeProposal : nullptr;
}

RtlRenamePlanStatus
RtlRenameWorkflow::lastPlanStatus() const
{
    return plannerStatus;
}

RtlHighRiskEditWorkflow&
RtlRenameWorkflow::transactionWorkflow()
{
    return workflow;
}

const RtlHighRiskEditWorkflow&
RtlRenameWorkflow::transactionWorkflow() const
{
    return workflow;
}

QString RtlRenameWorkflow::actionFamilyId()
{
    return QStringLiteral("rtl.rename");
}

RtlHighRiskEditWorkflowFailure
RtlRenameWorkflow::workflowFailure(
    RtlRenamePlanStatus status)
{
    switch (status) {
    case RtlRenamePlanStatus::MissingSemanticSnapshot:
    case RtlRenamePlanStatus::StaleSemanticGeneration:
        return RtlHighRiskEditWorkflowFailure::
            StaleSemanticGeneration;
    case RtlRenamePlanStatus::MissingDocumentSnapshot:
    case RtlRenamePlanStatus::StaleDocumentRevision:
        return RtlHighRiskEditWorkflowFailure::
            StaleDocumentRevision;
    case RtlRenamePlanStatus::
        TransactionPreparationFailed:
        return RtlHighRiskEditWorkflowFailure::InvalidPlan;
    case RtlRenamePlanStatus::Ready:
    case RtlRenamePlanStatus::InvalidRequest:
    case RtlRenamePlanStatus::InvalidNewName:
    case RtlRenamePlanStatus::NoChange:
    case RtlRenamePlanStatus::SubjectNotFound:
    case RtlRenamePlanStatus::AmbiguousSubject:
    case RtlRenamePlanStatus::UnsupportedSubject:
    case RtlRenamePlanStatus::ConflictingDefinition:
    case RtlRenamePlanStatus::InvalidTreeSnapshot:
    case RtlRenamePlanStatus::SyntaxError:
    case RtlRenamePlanStatus::OrderedConnection:
    case RtlRenamePlanStatus::
        IncompleteSemanticBinding:
    case RtlRenamePlanStatus::AmbiguousStructure:
        break;
    }
    return RtlHighRiskEditWorkflowFailure::
        PlanningRejected;
}

QString RtlRenameWorkflow::proposalActionId(
    const RtlRenameProposal& proposal)
{
    for (const rtledit::TextEditProvenance& provenance :
         proposal.workspaceEdit.provenance) {
        if (!provenance.actionId.empty())
            return fromUtf8(provenance.actionId);
    }
    return actionFamilyId();
}
