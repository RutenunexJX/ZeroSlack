#include "rtlconnectiontransformworkflow.h"

#include <memory>
#include <utility>

namespace {

SemanticIndex* resolvedIndex(SemanticIndex* index)
{
    return index
        ? index : SemanticIndex::getInstance();
}

RtlConnectionTransformWorkflow::PlanOperation
defaultPlanOperation(SemanticIndex* index)
{
    auto planner =
        std::make_shared<
            RtlConnectionTransformPlanner>(
                resolvedIndex(index));
    return [planner](
               const RtlConnectionTransformRequest& request,
               const rtledit::WorkspaceDocumentManager&
                   documents) {
        return planner->plan(request, documents);
    };
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

} // namespace

RtlConnectionTransformWorkflow::
RtlConnectionTransformWorkflow(
    SemanticIndex* semanticIndex,
    rtledit::WorkspaceDocumentManager* documents,
    WorkspaceEditTransactionService* transactions,
    RtlHighRiskEditWorkflow::ExternalStateProvider
        externalStateProvider)
    : RtlConnectionTransformWorkflow(
          defaultPlanOperation(semanticIndex),
          semanticIndex,
          documents,
          transactions,
          std::move(externalStateProvider))
{
}

RtlConnectionTransformWorkflow::
RtlConnectionTransformWorkflow(
    PlanOperation planOperation,
    SemanticIndex* semanticIndex,
    rtledit::WorkspaceDocumentManager* documents,
    WorkspaceEditTransactionService* transactions,
    RtlHighRiskEditWorkflow::ExternalStateProvider
        externalStateProvider)
    : index(resolvedIndex(semanticIndex))
    , documentManager(documents)
    , planTransform(std::move(planOperation))
    , workflow(
          index,
          documents,
          transactions,
          std::move(externalStateProvider))
{
}

RtlHighRiskEditWorkflowResult
RtlConnectionTransformWorkflow::preparePreview(
    const RtlConnectionTransformRequest& request,
    bool dryRun)
{
    if (!workflow.canStartPreview()) {
        return workflow.reportPlanningFailure(
            actionId(),
            RtlHighRiskEditWorkflowFailure::InvalidState,
            QStringLiteral(
                "An applied RTL connection transform is still "
                "awaiting its single-step undo."));
    }

    activeReport.reset();
    plannerStatus =
        RtlConnectionTransformStatus::Rejected;
    plannerFailure =
        RtlConnectionTransformFailure::InvalidRequest;
    if (!planTransform || !index || !documentManager) {
        return workflow.reportPlanningFailure(
            actionId(),
            RtlHighRiskEditWorkflowFailure::
                MissingDependency,
            QStringLiteral(
                "The RTL connection transform planner is "
                "unavailable."));
    }

    const SemanticSnapshotToken planningToken =
        index->snapshotToken();
    if (!planningToken.isValid()
        || planningToken.revision == 0
        || planningToken.revision
            != request.expectedSemanticGeneration) {
        return workflow.reportPlanningFailure(
            actionId(),
            RtlHighRiskEditWorkflowFailure::
                StaleSemanticGeneration,
            QStringLiteral(
                "The requested RTL connection transform semantic "
                "generation is stale."));
    }

    RtlConnectionTransformReport planned =
        planTransform(request, *documentManager);
    plannerStatus = planned.status;
    plannerFailure = planned.failure;
    activeReport = std::move(planned);

    const SemanticSnapshotToken liveToken =
        index->snapshotToken();
    if (!sameSemanticToken(
            planningToken, liveToken)) {
        return workflow.reportPlanningFailure(
            actionId(),
            RtlHighRiskEditWorkflowFailure::
                StaleSemanticGeneration,
            QStringLiteral(
                "The semantic generation changed while the RTL "
                "connection transform was being planned."));
    }

    if (plannerStatus
            == RtlConnectionTransformStatus::NoChanges) {
        return workflow.reportNoChanges(
            actionId(),
            activeReport->message.isEmpty()
                ? QStringLiteral(
                      "The selected instance already satisfies the "
                      "requested connection form.")
                : activeReport->message);
    }
    if (plannerStatus
            != RtlConnectionTransformStatus::Ready
        || activeReport->workspaceEdit.edits.empty()) {
        return workflow.reportPlanningFailure(
            actionId(),
            workflowFailure(plannerFailure),
            activeReport->message.isEmpty()
                ? QStringLiteral(
                      "The RTL connection transform planner "
                      "rejected the request.")
                : activeReport->message);
    }

    return workflow.preparePreview(
        actionId(),
        activeReport->workspaceEdit,
        planningToken,
        dryRun);
}

RtlHighRiskEditWorkflowResult
RtlConnectionTransformWorkflow::confirm(
    const QString& confirmationToken)
{
    return workflow.confirm(confirmationToken);
}

RtlHighRiskEditWorkflowResult
RtlConnectionTransformWorkflow::cancel()
{
    return workflow.cancel();
}

RtlHighRiskEditWorkflowResult
RtlConnectionTransformWorkflow::undo()
{
    return workflow.undo();
}

RtlHighRiskEditWorkflowResult
RtlConnectionTransformWorkflow::
retireUndoPosition()
{
    return workflow.retireUndoPosition();
}

const RtlConnectionTransformReport*
RtlConnectionTransformWorkflow::report() const
{
    return activeReport
        ? &*activeReport : nullptr;
}

RtlConnectionTransformStatus
RtlConnectionTransformWorkflow::lastPlanStatus() const
{
    return plannerStatus;
}

RtlConnectionTransformFailure
RtlConnectionTransformWorkflow::lastPlanFailure() const
{
    return plannerFailure;
}

RtlHighRiskEditWorkflow&
RtlConnectionTransformWorkflow::
transactionWorkflow()
{
    return workflow;
}

const RtlHighRiskEditWorkflow&
RtlConnectionTransformWorkflow::
transactionWorkflow() const
{
    return workflow;
}

QString RtlConnectionTransformWorkflow::actionId()
{
    return QStringLiteral("rtl.connection.transform");
}

RtlHighRiskEditWorkflowFailure
RtlConnectionTransformWorkflow::workflowFailure(
    RtlConnectionTransformFailure failure)
{
    switch (failure) {
    case RtlConnectionTransformFailure::
        MissingSemanticSnapshot:
    case RtlConnectionTransformFailure::
        StaleSemanticGeneration:
    case RtlConnectionTransformFailure::
        SemanticSnapshotChanged:
        return RtlHighRiskEditWorkflowFailure::
            StaleSemanticGeneration;
    case RtlConnectionTransformFailure::MissingDocument:
    case RtlConnectionTransformFailure::
        StaleDocumentRevision:
    case RtlConnectionTransformFailure::
        StaleSemanticSource:
        return RtlHighRiskEditWorkflowFailure::
            StaleDocumentRevision;
    case RtlConnectionTransformFailure::None:
    case RtlConnectionTransformFailure::InvalidRequest:
    case RtlConnectionTransformFailure::SyntaxError:
    case RtlConnectionTransformFailure::InstanceNotFound:
    case RtlConnectionTransformFailure::
        InstanceSemanticMismatch:
    case RtlConnectionTransformFailure::
        AmbiguousModuleDefinition:
    case RtlConnectionTransformFailure::AmbiguousFormal:
    case RtlConnectionTransformFailure::MissingFormalType:
    case RtlConnectionTransformFailure::
        MultiInstanceTypeDifference:
    case RtlConnectionTransformFailure::WildcardConnection:
    case RtlConnectionTransformFailure::
        MixedConnectionStyle:
    case RtlConnectionTransformFailure::
        UnsupportedConnectionSyntax:
    case RtlConnectionTransformFailure::
        OrderedConnectionCountExceedsFormals:
    case RtlConnectionTransformFailure::UnknownNamedFormal:
    case RtlConnectionTransformFailure::AmbiguousActual:
    case RtlConnectionTransformFailure::MissingActualType:
    case RtlConnectionTransformFailure::CastNotProvable:
        break;
    }
    return RtlHighRiskEditWorkflowFailure::
        PlanningRejected;
}
