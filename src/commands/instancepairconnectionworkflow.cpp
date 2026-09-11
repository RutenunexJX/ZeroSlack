#include "instancepairconnectionworkflow.h"

#include "semanticindex.h"
#include "workspaceedittransactionservice.h"

#include <rtledit/edit_plan.h>

#include <QByteArray>
#include <QDir>
#include <QFileInfo>

#include <string>
#include <utility>

namespace {

QString normalizedFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return {};
    return QDir::cleanPath(
        QDir::fromNativeSeparators(
            QFileInfo(fileName).absoluteFilePath()));
}

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

bool sameSemanticToken(
    const SemanticSnapshotToken& left,
    const SemanticSnapshotToken& right)
{
    return left.isValid()
        && right.isValid()
        && left.revision == right.revision
        && left.snapshot == right.snapshot;
}

bool sameBaselines(
    const std::vector<rtledit::DocumentBaseline>& left,
    const std::vector<rtledit::DocumentBaseline>& right)
{
    if (left.size() != right.size())
        return false;
    for (std::size_t index = 0;
         index < left.size();
         ++index) {
        if (left[index].filePath
                != right[index].filePath
            || left[index].version
                != right[index].version) {
            return false;
        }
    }
    return true;
}

bool sameEdit(
    const rtledit::WorkspaceTextEdit& left,
    const rtledit::WorkspaceTextEdit& right)
{
    return left.filePath == right.filePath
        && left.expectedDocumentVersion
            == right.expectedDocumentVersion
        && left.range.start == right.range.start
        && left.range.end == right.range.end
        && left.expectedText == right.expectedText
        && left.newText == right.newText;
}

bool sameSemanticObject(
    const rtledit::SemanticObjectId& left,
    const rtledit::SemanticObjectId& right)
{
    return left.kind == right.kind
        && left.qualifiedName == right.qualifiedName
        && left.ownerScope == right.ownerScope
        && left.filePath == right.filePath
        && left.range.start == right.range.start
        && left.range.end == right.range.end
        && left.signatureHash == right.signatureHash;
}

bool sameProvenance(
    const rtledit::TextEditProvenance& left,
    const rtledit::TextEditProvenance& right)
{
    return left.editIndex == right.editIndex
        && left.actionId == right.actionId
        && left.anchorName == right.anchorName
        && left.description == right.description
        && left.anchor.source == right.anchor.source
        && left.anchor.resolver == right.anchor.resolver
        && left.anchor.semanticSnapshotId
            == right.anchor.semanticSnapshotId
        && left.signalQualifiedName
            == right.signalQualifiedName
        && left.sourceInstancePath
            == right.sourceInstancePath
        && left.hierarchyStepIndex
            == right.hierarchyStepIndex
        && left.sourceFilePath == right.sourceFilePath
        && left.sourceRange.start
            == right.sourceRange.start
        && left.sourceRange.end
            == right.sourceRange.end;
}

bool samePlan(
    const rtledit::WorkspaceEditPlan& left,
    const rtledit::WorkspaceEditPlan& right)
{
    if (left.intent.kind != right.intent.kind
        || !sameSemanticObject(
            left.intent.target, right.intent.target)
        || left.semanticSnapshot.id
            != right.semanticSnapshot.id
        || left.semanticIndexFilePaths
            != right.semanticIndexFilePaths
        || left.riskLevel != right.riskLevel
        || left.previewPolicy != right.previewPolicy
        || left.hasMixedDocumentVersions
            != right.hasMixedDocumentVersions
        || !sameBaselines(
            left.baselines, right.baselines)
        || left.edits.size() != right.edits.size()
        || left.provenance.size()
            != right.provenance.size()) {
        return false;
    }
    for (std::size_t index = 0;
         index < left.edits.size();
         ++index) {
        if (!sameEdit(
                left.edits[index],
                right.edits[index])) {
            return false;
        }
    }
    for (std::size_t index = 0;
         index < left.provenance.size();
         ++index) {
        if (!sameProvenance(
                left.provenance[index],
                right.provenance[index])) {
            return false;
        }
    }
    return true;
}

bool sameStaleStatus(
    const rtledit::EditPlanStaleStatus& left,
    const rtledit::EditPlanStaleStatus& right)
{
    return left.reason == right.reason
        && left.filePath == right.filePath
        && left.expectedVersion == right.expectedVersion
        && left.actualVersion == right.actualVersion
        && left.expectedSemanticSnapshotId
            == right.expectedSemanticSnapshotId
        && left.actualSemanticSnapshotId
            == right.actualSemanticSnapshotId;
}

bool sameValidation(
    const rtledit::EditPlanValidationResult& left,
    const rtledit::EditPlanValidationResult& right)
{
    return left.issue == right.issue
        && left.filePath == right.filePath
        && left.editIndex == right.editIndex
        && left.message == right.message;
}

bool sameDiffLine(
    const rtledit::SourceDiffLine& left,
    const rtledit::SourceDiffLine& right)
{
    return left.kind == right.kind
        && left.oldLine == right.oldLine
        && left.newLine == right.newLine
        && left.text == right.text;
}

bool sameDiffHunk(
    const rtledit::SourceDiffHunk& left,
    const rtledit::SourceDiffHunk& right)
{
    if (left.oldStartLine != right.oldStartLine
        || left.oldLineCount != right.oldLineCount
        || left.newStartLine != right.newStartLine
        || left.newLineCount != right.newLineCount
        || left.lines.size() != right.lines.size()) {
        return false;
    }
    for (std::size_t index = 0;
         index < left.lines.size();
         ++index) {
        if (!sameDiffLine(
                left.lines[index],
                right.lines[index])) {
            return false;
        }
    }
    return true;
}

bool sameDiffFile(
    const rtledit::SourceDiffFile& left,
    const rtledit::SourceDiffFile& right)
{
    if (left.filePath != right.filePath
        || left.version != right.version
        || left.editCount != right.editCount
        || left.beforeText != right.beforeText
        || left.afterText != right.afterText
        || left.hunks.size() != right.hunks.size()) {
        return false;
    }
    for (std::size_t index = 0;
         index < left.hunks.size();
         ++index) {
        if (!sameDiffHunk(
                left.hunks[index],
                right.hunks[index])) {
            return false;
        }
    }
    return true;
}

bool sameSourceDiff(
    const rtledit::WorkspaceEditSourceDiff& left,
    const rtledit::WorkspaceEditSourceDiff& right)
{
    if (left.status != right.status
        || !sameStaleStatus(
            left.staleStatus, right.staleStatus)
        || !sameValidation(
            left.validationResult,
            right.validationResult)
        || left.filePath != right.filePath
        || left.message != right.message
        || left.semanticSnapshot.id
            != right.semanticSnapshot.id
        || left.semanticIndexFilePaths
            != right.semanticIndexFilePaths
        || left.files.size() != right.files.size()) {
        return false;
    }
    for (std::size_t index = 0;
         index < left.files.size();
         ++index) {
        if (!sameDiffFile(
                left.files[index],
                right.files[index])) {
            return false;
        }
    }
    return true;
}

bool sameProposalIdentity(
    const InstancePairConnectionProposal& left,
    const InstancePairConnectionProposal& right)
{
    return left.ready()
        && right.ready()
        && left.blockView.semanticGeneration
            == right.blockView.semanticGeneration
        && left.blockView.left.instancePath
            == right.blockView.left.instancePath
        && left.blockView.left.moduleName
            == right.blockView.left.moduleName
        && left.blockView.right.instancePath
            == right.blockView.right.instancePath
        && left.blockView.right.moduleName
            == right.blockView.right.moduleName
        && left.leftSignal.stableKey
            == right.leftSignal.stableKey
        && left.connectionName
            == right.connectionName
        && left.renderedSignalType
            == right.renderedSignalType
        && left.dryRun == right.dryRun
        && left.renderedDiff == right.renderedDiff
        && left.transaction.status
            == right.transaction.status
        && left.transaction.dryRun
            == right.transaction.dryRun
        && !left.transaction.previewConfirmed
        && !right.transaction.previewConfirmed
        && left.transaction.preview.status
            == right.transaction.preview.status
        && left.transaction.preview.fileCount
            == right.transaction.preview.fileCount
        && left.transaction.preview.editCount
            == right.transaction.preview.editCount
        && left.transaction.preview.semanticSnapshot.id
            == right.transaction.preview.semanticSnapshot.id
        && left.transaction.preview.semanticIndexFilePaths
            == right.transaction.preview.semanticIndexFilePaths
        && samePlan(
            left.workspaceEdit,
            right.workspaceEdit)
        && samePlan(
            left.transaction.plan,
            right.transaction.plan)
        && sameSourceDiff(
            left.sourceDiff,
            right.sourceDiff)
        && sameSourceDiff(
            left.transaction.sourceDiff,
            right.transaction.sourceDiff);
}

QString transactionMessage(
    const rtledit::WorkspaceEditTransactionResult& result)
{
    return result.message.empty()
        ? QStringLiteral(
              "Workspace edit transaction failed.")
        : fromUtf8(result.message);
}

rtledit::TransactionStatus transactionStatusForFailure(
    InstancePairConnectionWorkflowFailure failure)
{
    if (failure
            == InstancePairConnectionWorkflowFailure::
                StaleSemanticGeneration
        || failure
            == InstancePairConnectionWorkflowFailure::
                StaleDocumentRevision) {
        return rtledit::TransactionStatus::Stale;
    }
    if (failure
            == InstancePairConnectionWorkflowFailure::Conflict) {
        return rtledit::TransactionStatus::Conflict;
    }
    return rtledit::TransactionStatus::
        InvalidPreparation;
}

} // namespace

bool InstancePairConnectionWorkflowResult::succeeded() const
{
    if (failure
            != InstancePairConnectionWorkflowFailure::None) {
        return false;
    }
    switch (state) {
    case InstancePairConnectionWorkflowState::Analyzed:
    case InstancePairConnectionWorkflowState::PreviewReady:
    case InstancePairConnectionWorkflowState::NoChanges:
    case InstancePairConnectionWorkflowState::Applied:
    case InstancePairConnectionWorkflowState::DryRunComplete:
    case InstancePairConnectionWorkflowState::Undone:
        return true;
    case InstancePairConnectionWorkflowState::Idle:
    case InstancePairConnectionWorkflowState::Cancelled:
    case InstancePairConnectionWorkflowState::Failed:
        return false;
    }
    return false;
}

bool InstancePairConnectionWorkflow::Operations::isValid() const
{
    return static_cast<bool>(analyze)
        && static_cast<bool>(plan);
}

InstancePairConnectionWorkflow::
InstancePairConnectionWorkflow(
    InstancePairConnectionFacade* facade,
    InstancePairConnectionCoordinator* coordinator,
    SemanticIndex* semanticIndex,
    rtledit::WorkspaceDocumentManager* documents,
    WorkspaceEditTransactionService* transactions,
    QObject* parent)
    : InstancePairConnectionWorkflow(
          Operations{
              facade
                  ? AnalyzeOperation{
                        [facade](
                            const InstancePairConnectionQuery& query,
                            rtledit::WorkspaceDocumentManager&
                                manager) {
                            return facade->analyze(
                                query, manager);
                        }}
                  : AnalyzeOperation{},
              facade
                  ? PlanOperation{
                        [facade](
                            const InstancePairConnectionAnalysis&
                                analysis,
                            rtledit::WorkspaceDocumentManager&
                                manager) {
                            return facade->plan(
                                analysis, manager);
                        }}
                  : PlanOperation{}},
          coordinator,
          semanticIndex,
          documents,
          transactions,
          parent)
{
}

InstancePairConnectionWorkflow::
InstancePairConnectionWorkflow(
    Operations operations,
    InstancePairConnectionCoordinator* coordinator,
    SemanticIndex* semanticIndex,
    rtledit::WorkspaceDocumentManager* documents,
    WorkspaceEditTransactionService* transactions,
    QObject* parent)
    : QObject(parent),
      workflowOperations(std::move(operations)),
      panelCoordinator(coordinator),
      index(
          semanticIndex
              ? semanticIndex
              : SemanticIndex::getInstance()),
      documentManager(documents),
      ownedTransactionService(
          transactions
              ? nullptr
              : std::make_unique<
                    WorkspaceEditTransactionService>()),
      transactionService(
          transactions
              ? transactions
              : ownedTransactionService.get())
{
    qRegisterMetaType<
        InstancePairConnectionWorkflowState>();
    qRegisterMetaType<
        InstancePairConnectionWorkflowFailure>();
    qRegisterMetaType<
        InstancePairConnectionWorkflowResult>();
    connectPanelSignals();
}

InstancePairConnectionWorkflow::
~InstancePairConnectionWorkflow() = default;

InstancePairConnectionWorkflowResult
InstancePairConnectionWorkflow::analyzeAndPresent(
    const InstancePairConnectionQuery& query)
{
    if (!workflowOperations.isValid()
        || !panelCoordinator
        || !index
        || !documentManager
        || !transactionService) {
        return fail(
            InstancePairConnectionWorkflowFailure::
                MissingDependency,
            QStringLiteral(
                "Instance-pair workflow dependencies are incomplete."));
    }
    if (currentState
            == InstancePairConnectionWorkflowState::Applied
        && workflowUndoAvailable) {
        if (canUndoAppliedTransaction()) {
            return publish(
                InstancePairConnectionWorkflowState::Applied,
                InstancePairConnectionWorkflowFailure::InvalidState,
                QStringLiteral(
                    "Undo or retire the applied transaction before "
                    "starting another instance-pair workflow."));
        }
        workflowUndoAvailable = false;
    }

    discardPendingProposal();
    workflowUndoAvailable = false;

    InstancePairConnectionAnalysis analysis =
        workflowOperations.analyze(
            query, *documentManager);
    activeAnalysis = analysis;
    panelCoordinator->presentAnalysis(analysis);
    if (!panelCoordinator->panel()) {
        return fail(
            InstancePairConnectionWorkflowFailure::
                MissingDependency,
            QStringLiteral(
                "The instance-pair panel could not be created."));
    }
    if (!analysis.ready()) {
        const InstancePairConnectionWorkflowFailure
            analysisFailure =
                workflowFailure(
                    analysis.failure,
                    InstancePairConnectionWorkflowFailure::
                        AnalysisRejected);
        return fail(
            analysisFailure,
            analysis.message.isEmpty()
                ? QStringLiteral(
                      "Instance-pair analysis was rejected.")
                : analysis.message,
            transactionStatusForFailure(
                analysisFailure));
    }

    return publish(
        InstancePairConnectionWorkflowState::Analyzed,
        InstancePairConnectionWorkflowFailure::None,
        QStringLiteral(
            "Instance-pair analysis is ready."));
}

InstancePairConnectionWorkflowResult
InstancePairConnectionWorkflow::requestPreview(
    const InstancePairConnectionPlanRequest& request)
{
    if (currentState
            == InstancePairConnectionWorkflowState::Applied
        && workflowUndoAvailable) {
        return publish(
            InstancePairConnectionWorkflowState::Applied,
            InstancePairConnectionWorkflowFailure::InvalidState,
            QStringLiteral(
                "The confirmed transaction is already applied; "
                "preview cannot be rebuilt before undo."));
    }
    if (currentState
            != InstancePairConnectionWorkflowState::Analyzed
        && currentState
            != InstancePairConnectionWorkflowState::PreviewReady) {
        if (currentState
                == InstancePairConnectionWorkflowState::Cancelled) {
            return publish(
                InstancePairConnectionWorkflowState::Cancelled,
                InstancePairConnectionWorkflowFailure::Cancelled,
                QStringLiteral(
                    "Instance-pair connection remains cancelled."));
        }
        return fail(
            InstancePairConnectionWorkflowFailure::InvalidState,
            QStringLiteral(
                "No analyzed instance-pair request is available "
                "for preview."));
    }
    if (!activeAnalysis
        || !requestMatchesCurrentContext(request)) {
        return publish(
            currentState,
            InstancePairConnectionWorkflowFailure::
                RequestMismatch,
            QStringLiteral(
                "The preview request does not match the current "
                "semantic instance-pair context."));
    }

    const PreflightResult checked =
        preflight(request, nullptr);
    if (!checked.ready()) {
        discardPendingProposal();
        return fail(
            checked.failure,
            checked.message,
            transactionStatusForFailure(
                checked.failure));
    }

    InstancePairConnectionProposal proposal =
        workflowOperations.plan(
            *activeAnalysis, *documentManager);
    if (!proposal.ready()) {
        panelCoordinator->presentProposal(proposal);
        activeProposal = proposal;
        activeRequest = request;
        if (proposal.status
                == InstancePairConnectionStatus::NoChanges
            && proposal.failure
                == InstancePairConnectionFailure::None) {
            return publish(
                InstancePairConnectionWorkflowState::NoChanges,
                InstancePairConnectionWorkflowFailure::None,
                proposal.message.isEmpty()
                    ? QStringLiteral(
                          "The instances are already connected.")
                    : proposal.message);
        }
        const InstancePairConnectionWorkflowFailure
            planningFailure =
                workflowFailure(
                    proposal.failure,
                    InstancePairConnectionWorkflowFailure::
                        PlanningRejected);
        return fail(
            planningFailure,
            proposal.message.isEmpty()
                ? QStringLiteral(
                      "Instance-pair planning was rejected.")
                : proposal.message,
            transactionStatusForFailure(
                planningFailure));
    }
    const PreflightResult planned =
        preflight(request, &proposal);
    if (!planned.ready()) {
        discardPendingProposal();
        return fail(
            planned.failure,
            planned.message,
            transactionStatusForFailure(
                planned.failure));
    }

    const bool displayed =
        panelCoordinator->presentProposal(proposal);
    if (!displayed) {
        discardPendingProposal();
        return fail(
            InstancePairConnectionWorkflowFailure::
                RequestMismatch,
            QStringLiteral(
                "The prepared proposal no longer matches the "
                "displayed revision context."));
    }

    activeProposal = std::move(proposal);
    activeRequest = request;
    return publish(
        InstancePairConnectionWorkflowState::PreviewReady,
        InstancePairConnectionWorkflowFailure::None,
        activeProposal->dryRun
            ? QStringLiteral(
                  "Dry-run High+Diff plan is ready; no source "
                  "mutation is permitted.")
            : QStringLiteral(
                  "High+Diff plan is ready for explicit "
                  "confirmation."));
}

InstancePairConnectionWorkflowResult
InstancePairConnectionWorkflow::confirm(
    const InstancePairConnectionPlanRequest& request)
{
    if (currentState
            == InstancePairConnectionWorkflowState::Applied
        && workflowUndoAvailable) {
        return publish(
            InstancePairConnectionWorkflowState::Applied,
            InstancePairConnectionWorkflowFailure::InvalidState,
            QStringLiteral(
                "The instance-pair transaction is already applied."));
    }
    if (currentState
            != InstancePairConnectionWorkflowState::PreviewReady
        || !activeAnalysis
        || !activeProposal) {
        if (currentState
                == InstancePairConnectionWorkflowState::Cancelled) {
            return publish(
                InstancePairConnectionWorkflowState::Cancelled,
                InstancePairConnectionWorkflowFailure::Cancelled,
                QStringLiteral(
                    "Instance-pair connection remains cancelled."));
        }
        return fail(
            InstancePairConnectionWorkflowFailure::InvalidState,
            QStringLiteral(
                "No pending High+Diff preview is available for "
                "confirmation."));
    }
    if (!request.sameContext(activeRequest)
        || !requestMatchesCurrentContext(request)) {
        return publish(
            InstancePairConnectionWorkflowState::PreviewReady,
            InstancePairConnectionWorkflowFailure::
                RequestMismatch,
            QStringLiteral(
                "The confirmation request does not match the "
                "pending proposal context."));
    }
    if (!displayedProposalMatchesPending()) {
        discardPendingProposal();
        return fail(
            InstancePairConnectionWorkflowFailure::Conflict,
            QStringLiteral(
                "The displayed High+Diff preview no longer "
                "matches the pending transaction."),
            rtledit::TransactionStatus::Conflict);
    }

    const PreflightResult checked =
        preflight(request, &*activeProposal);
    if (!checked.ready()) {
        discardPendingProposal();
        return fail(
            checked.failure,
            checked.message,
            transactionStatusForFailure(
                checked.failure));
    }

    if (activeProposal->dryRun
        || activeProposal->transaction.dryRun) {
        return publish(
            InstancePairConnectionWorkflowState::DryRunComplete,
            InstancePairConnectionWorkflowFailure::None,
            QStringLiteral(
                "Dry-run completed with a High+Diff plan and no "
                "workspace mutation."),
            rtledit::TransactionStatus::DryRunOnly);
    }

    const auto before =
        capturePlanDocuments(*activeProposal);
    if (!before) {
        discardPendingProposal();
        return fail(
            InstancePairConnectionWorkflowFailure::
                StaleDocumentRevision,
            QStringLiteral(
                "A plan document disappeared before confirmation."),
            rtledit::TransactionStatus::Stale);
    }

    const SemanticSnapshotToken token =
        index->snapshotToken();
    const rtledit::WorkspaceEditTransactionResult applied =
        transactionService->applyConfirmed(
            activeProposal->transaction,
            rtledit::SemanticIndexSnapshot{
                std::to_string(token.revision)},
            *documentManager);
    if (applied.status
            == rtledit::TransactionStatus::Applied) {
        workflowUndoAvailable = true;
        appliedTransactionGeneration =
            transactionService->historyGeneration();
        return publish(
            InstancePairConnectionWorkflowState::Applied,
            InstancePairConnectionWorkflowFailure::None,
            transactionMessage(applied),
            applied.status);
    }
    if (applied.status
            == rtledit::TransactionStatus::DryRunOnly) {
        return publish(
            InstancePairConnectionWorkflowState::DryRunComplete,
            InstancePairConnectionWorkflowFailure::None,
            transactionMessage(applied),
            applied.status);
    }
    if (applied.status
            == rtledit::TransactionStatus::Stale) {
        discardPendingProposal();
        return fail(
            applied.applyResult.staleStatus.reason
                    == rtledit::EditPlanStaleReason::
                        SemanticSnapshotChanged
                ? InstancePairConnectionWorkflowFailure::
                      StaleSemanticGeneration
                : InstancePairConnectionWorkflowFailure::
                      StaleDocumentRevision,
            transactionMessage(applied),
            applied.status);
    }
    if (applied.status
            == rtledit::TransactionStatus::
                InvalidPreparation
        || applied.status
            == rtledit::TransactionStatus::
                PreviewRequired) {
        discardPendingProposal();
        return fail(
            InstancePairConnectionWorkflowFailure::ApplyFailed,
            transactionMessage(applied),
            applied.status);
    }

    const bool restored =
        restoreAfterFailedApply(*before);
    discardPendingProposal();
    if (!restored) {
        return fail(
            InstancePairConnectionWorkflowFailure::
                AtomicRollbackFailed,
            QStringLiteral(
                "Atomic apply failed and the pre-confirmation "
                "workspace state could not be fully restored."),
            applied.status);
    }
    return fail(
        InstancePairConnectionWorkflowFailure::ApplyFailed,
        transactionMessage(applied),
        applied.status);
}

InstancePairConnectionWorkflowResult
InstancePairConnectionWorkflow::cancel()
{
    if (currentState
            == InstancePairConnectionWorkflowState::Applied
        && workflowUndoAvailable) {
        return publish(
            InstancePairConnectionWorkflowState::Applied,
            InstancePairConnectionWorkflowFailure::InvalidState,
            QStringLiteral(
                "An applied transaction cannot be cancelled; use "
                "the workflow undo operation."));
    }
    if (currentState
            != InstancePairConnectionWorkflowState::Analyzed
        && currentState
            != InstancePairConnectionWorkflowState::PreviewReady) {
        return fail(
            InstancePairConnectionWorkflowFailure::InvalidState,
            QStringLiteral(
                "The instance-pair workflow has no cancellable "
                "request."));
    }

    discardPendingProposal();
    return publish(
        InstancePairConnectionWorkflowState::Cancelled,
        InstancePairConnectionWorkflowFailure::Cancelled,
        QStringLiteral(
            "Instance-pair connection was cancelled without "
            "workspace changes."));
}

InstancePairConnectionWorkflowResult
InstancePairConnectionWorkflow::undo()
{
    if (currentState
            != InstancePairConnectionWorkflowState::Applied
        || !workflowUndoAvailable
        || !transactionService
        || !documentManager) {
        return fail(
            InstancePairConnectionWorkflowFailure::InvalidState,
            QStringLiteral(
                "No applied instance-pair transaction is available "
                "for workflow undo."));
    }
    if (transactionService->historyGeneration()
            != appliedTransactionGeneration) {
        workflowUndoAvailable = false;
        return fail(
            InstancePairConnectionWorkflowFailure::Conflict,
            QStringLiteral(
                "A newer workspace transaction replaced this "
                "instance-pair undo position."),
            rtledit::TransactionStatus::Conflict);
    }

    const rtledit::WorkspaceEditTransactionResult undone =
        transactionService->undo(*documentManager);
    if (undone.status
            != rtledit::TransactionStatus::Undone) {
        return publish(
            InstancePairConnectionWorkflowState::Applied,
            InstancePairConnectionWorkflowFailure::UndoFailed,
            transactionMessage(undone),
            undone.status);
    }
    workflowUndoAvailable = false;
    appliedTransactionGeneration =
        transactionService->historyGeneration();
    return publish(
        InstancePairConnectionWorkflowState::Undone,
        InstancePairConnectionWorkflowFailure::None,
        transactionMessage(undone),
        undone.status);
}

InstancePairConnectionWorkflowState
InstancePairConnectionWorkflow::state() const
{
    return currentState;
}

const InstancePairConnectionWorkflowResult&
InstancePairConnectionWorkflow::lastResult() const
{
    return currentResult;
}

const InstancePairConnectionAnalysis*
InstancePairConnectionWorkflow::currentAnalysis() const
{
    return activeAnalysis ? &*activeAnalysis : nullptr;
}

const InstancePairConnectionProposal*
InstancePairConnectionWorkflow::currentProposal() const
{
    return activeProposal ? &*activeProposal : nullptr;
}

bool InstancePairConnectionWorkflow::hasPendingPreview() const
{
    return currentState
            == InstancePairConnectionWorkflowState::PreviewReady
        && activeProposal
        && activeProposal->ready();
}

bool InstancePairConnectionWorkflow::
canUndoAppliedTransaction() const
{
    return currentState
            == InstancePairConnectionWorkflowState::Applied
        && workflowUndoAvailable
        && transactionService
        && transactionService->canUndo()
        && transactionService->historyGeneration()
               == appliedTransactionGeneration;
}

void InstancePairConnectionWorkflow::connectPanelSignals()
{
    if (!panelCoordinator)
        return;
    connect(
        panelCoordinator,
        &InstancePairConnectionCoordinator::planRequested,
        this,
        [this](
            const InstancePairConnectionPlanRequest& request) {
            requestPreview(request);
        });
    connect(
        panelCoordinator,
        &InstancePairConnectionCoordinator::previewRequested,
        this,
        [this](
            const InstancePairConnectionPlanRequest& request) {
            requestPreview(request);
        });
    connect(
        panelCoordinator,
        &InstancePairConnectionCoordinator::confirmRequested,
        this,
        [this](
            const InstancePairConnectionPlanRequest& request) {
            confirm(request);
        });
    connect(
        panelCoordinator,
        &InstancePairConnectionCoordinator::undoRequested,
        this,
        [this]() { undo(); });
}

void InstancePairConnectionWorkflow::discardPendingProposal()
{
    if (panelCoordinator) {
        if (panelCoordinator->panel())
            panelCoordinator->panel()->cancelDragState();
        panelCoordinator->clearProposal();
    }
    activeProposal.reset();
    activeRequest = {};
}

InstancePairConnectionWorkflowResult
InstancePairConnectionWorkflow::publish(
    InstancePairConnectionWorkflowState state,
    InstancePairConnectionWorkflowFailure failure,
    const QString& message,
    rtledit::TransactionStatus transactionStatus)
{
    currentState = state;
    currentResult.state = state;
    currentResult.failure = failure;
    currentResult.transactionStatus =
        transactionStatus;
    currentResult.message = message;
    const bool terminalUiState =
        state == InstancePairConnectionWorkflowState::Applied
        || state
               == InstancePairConnectionWorkflowState::Undone
        || state
               == InstancePairConnectionWorkflowState::DryRunComplete
        || state
               == InstancePairConnectionWorkflowState::Cancelled
        || state
               == InstancePairConnectionWorkflowState::Failed;
    if (terminalUiState
        && panelCoordinator
        && panelCoordinator->panel()) {
        panelCoordinator->panel()->setWorkflowOutcome(
            message,
            state
                == InstancePairConnectionWorkflowState::Applied,
            state
                    == InstancePairConnectionWorkflowState::Applied
                && workflowUndoAvailable);
    }
    emit stateChanged(currentResult);
    return currentResult;
}

InstancePairConnectionWorkflowResult
InstancePairConnectionWorkflow::fail(
    InstancePairConnectionWorkflowFailure failure,
    const QString& message,
    rtledit::TransactionStatus transactionStatus)
{
    return publish(
        InstancePairConnectionWorkflowState::Failed,
        failure,
        message,
        transactionStatus);
}

InstancePairConnectionWorkflow::PreflightResult
InstancePairConnectionWorkflow::preflight(
    const InstancePairConnectionPlanRequest& request,
    const InstancePairConnectionProposal* proposal) const
{
    auto rejected =
        [](InstancePairConnectionWorkflowFailure failure,
           const QString& message) {
            PreflightResult result;
            result.failure = failure;
            result.message = message;
            return result;
        };

    if (!activeAnalysis
        || !index
        || !documentManager
        || !request.isValid()) {
        return rejected(
            InstancePairConnectionWorkflowFailure::
                InvalidRequest,
            QStringLiteral(
                "The workflow revision context is incomplete."));
    }

    const SemanticSnapshotToken liveToken =
        index->snapshotToken();
    if (!sameSemanticToken(
            activeAnalysis->query.semanticToken,
            liveToken)
        || liveToken.revision
            != request.semanticGeneration) {
        return rejected(
            InstancePairConnectionWorkflowFailure::
                StaleSemanticGeneration,
            QStringLiteral(
                "The semantic generation changed after analysis."));
    }

    for (auto it =
             request.documentRevisions.constBegin();
         it != request.documentRevisions.constEnd();
         ++it) {
        const auto live =
            documentManager->snapshot(
                utf8String(it.key()));
        if (!live
            || live->version.value
                != it.value()) {
            return rejected(
                InstancePairConnectionWorkflowFailure::
                    StaleDocumentRevision,
                QStringLiteral(
                    "The document revision changed after analysis: %1")
                    .arg(it.key()));
        }
    }

    for (auto it =
             activeAnalysis->capturedDocuments.constBegin();
         it != activeAnalysis->capturedDocuments.constEnd();
         ++it) {
        const InstancePairDocumentSnapshot& captured =
            it.value();
        const QString fileName = normalizedFileName(
            captured.fileName.isEmpty()
                ? it.key() : captured.fileName);
        const auto live =
            documentManager->snapshot(
                utf8String(fileName));
        if (!live
            || live->version.value
                != captured.revision
            || live->text
                != utf8String(captured.text)) {
            return rejected(
                InstancePairConnectionWorkflowFailure::
                    StaleDocumentRevision,
                QStringLiteral(
                    "The document content changed after analysis: %1")
                    .arg(fileName));
        }
    }

    if (!proposal)
        return {};
    if (!proposal->ready()
        || proposal->blockView.semanticGeneration
            != request.semanticGeneration
        || !(proposal->leftSignal.stableKey
             == request.leftSignalStableKey)
        || proposal->connectionName
            != request.connectionName
        || proposal->dryRun
            != activeAnalysis->query.dryRun
        || proposal->transaction.dryRun
            != activeAnalysis->query.dryRun
        || proposal->workspaceEdit.riskLevel
            != rtledit::RiskLevel::High
        || proposal->workspaceEdit.previewPolicy
            != rtledit::PreviewPolicy::Diff
        || proposal->transaction.plan.riskLevel
            != rtledit::RiskLevel::High
        || proposal->transaction.plan.previewPolicy
            != rtledit::PreviewPolicy::Diff
        || proposal->transaction.previewConfirmed
        || !samePlan(
            proposal->workspaceEdit,
            proposal->transaction.plan)
        || !sameSourceDiff(
            proposal->sourceDiff,
            proposal->transaction.sourceDiff)) {
        return rejected(
            InstancePairConnectionWorkflowFailure::Conflict,
            QStringLiteral(
                "The pending proposal no longer matches its "
                "revision context."));
    }

    const rtledit::EditPlanStaleStatus stale =
        rtledit::staleStatus(
            proposal->transaction.plan,
            rtledit::SemanticIndexSnapshot{
                std::to_string(liveToken.revision)},
            *documentManager);
    if (stale.stale()) {
        return rejected(
            stale.reason
                    == rtledit::EditPlanStaleReason::
                        SemanticSnapshotChanged
                ? InstancePairConnectionWorkflowFailure::
                      StaleSemanticGeneration
                : InstancePairConnectionWorkflowFailure::
                      StaleDocumentRevision,
            QStringLiteral(
                "The High+Diff transaction became stale before "
                "confirmation."));
    }
    return {};
}

bool InstancePairConnectionWorkflow::
requestMatchesCurrentContext(
    const InstancePairConnectionPlanRequest& request) const
{
    if (!panelCoordinator
        || !panelCoordinator->panel()
        || !request.isValid()) {
        return false;
    }
    return request.sameContext(
        panelCoordinator->panel()
            ->currentPlanRequest());
}

bool InstancePairConnectionWorkflow::
displayedProposalMatchesPending() const
{
    if (!activeProposal
        || !panelCoordinator
        || !panelCoordinator->panel()) {
        return false;
    }
    const InstancePairConnectionProposal* displayed =
        panelCoordinator->panel()
            ->proposalForConfirmation();
    return displayed
        && sameProposalIdentity(
            *displayed, *activeProposal);
}

std::optional<
    std::vector<
        InstancePairConnectionWorkflow::CapturedDocument>>
InstancePairConnectionWorkflow::capturePlanDocuments(
    const InstancePairConnectionProposal& proposal) const
{
    if (!documentManager)
        return std::nullopt;
    std::vector<CapturedDocument> result;
    result.reserve(
        proposal.transaction.plan.baselines.size());
    for (const rtledit::DocumentBaseline& baseline :
         proposal.transaction.plan.baselines) {
        const auto snapshot =
            documentManager->snapshot(
                baseline.filePath);
        if (!snapshot
            || snapshot->version
                != baseline.version) {
            return std::nullopt;
        }
        result.push_back(
            CapturedDocument{
                baseline.filePath, *snapshot});
    }
    return result;
}

bool InstancePairConnectionWorkflow::
restoreAfterFailedApply(
    const std::vector<CapturedDocument>& before)
{
    if (!documentManager)
        return false;

    bool restoredEveryDocument = true;
    for (const CapturedDocument& captured :
         before) {
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
            restoredEveryDocument = false;
        }
    }
    for (const CapturedDocument& captured :
         before) {
        const auto current =
            documentManager->snapshot(
                captured.filePath);
        if (!current
            || current->text
                != captured.snapshot.text) {
            restoredEveryDocument = false;
        }
    }
    return restoredEveryDocument;
}

InstancePairConnectionWorkflowFailure
InstancePairConnectionWorkflow::workflowFailure(
    InstancePairConnectionFailure failure,
    InstancePairConnectionWorkflowFailure fallback)
{
    switch (failure) {
    case InstancePairConnectionFailure::
        StaleSemanticGeneration:
    case InstancePairConnectionFailure::
        MissingSemanticSnapshot:
        return InstancePairConnectionWorkflowFailure::
            StaleSemanticGeneration;
    case InstancePairConnectionFailure::
        StaleDocumentRevision:
    case InstancePairConnectionFailure::
        MissingDocumentSnapshot:
    case InstancePairConnectionFailure::
        StaleSemanticSource:
        return InstancePairConnectionWorkflowFailure::
            StaleDocumentRevision;
    case InstancePairConnectionFailure::InvalidRequest:
        return InstancePairConnectionWorkflowFailure::
            InvalidRequest;
    case InstancePairConnectionFailure::NameConflict:
    case InstancePairConnectionFailure::DirectionConflict:
    case InstancePairConnectionFailure::TypeMismatch:
    case InstancePairConnectionFailure::DriverConflict:
        return InstancePairConnectionWorkflowFailure::Conflict;
    default:
        return fallback;
    }
}
