#include "rtlconnectiontransformworkflow.h"
#include "rtlrenameworkflow.h"
#include "semanticindexsnapshot.h"
#include "workspaceedittransactionservice.h"

#include <rtledit/edit_plan.h>
#include <rtledit/text_edit.h>

#include <QCoreApplication>

#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

int checks = 0;
int failures = 0;

void expect(const char* label, bool condition)
{
    ++checks;
    if (!condition)
        ++failures;
    std::printf(
        "[%s] %s\n",
        condition ? "PASS" : "FAIL",
        label);
}

class MemoryDocuments final
    : public rtledit::WorkspaceDocumentManager
{
public:
    struct Document {
        rtledit::DocumentVersion version;
        std::string text;
    };

    void open(
        std::string filePath,
        std::string text,
        std::uint64_t version = 1)
    {
        documents[std::move(filePath)] =
            Document{{version}, std::move(text)};
    }

    void replace(
        const std::string& filePath,
        std::string text,
        std::uint64_t version)
    {
        documents[filePath] =
            Document{{version}, std::move(text)};
    }

    std::string text(
        const std::string& filePath) const
    {
        const auto found = documents.find(filePath);
        return found == documents.end()
            ? std::string() : found->second.text;
    }

    std::optional<rtledit::WorkspaceDocumentSnapshot>
    snapshot(
        const std::string& filePath) const override
    {
        const auto found = documents.find(filePath);
        if (found == documents.end())
            return std::nullopt;
        return rtledit::WorkspaceDocumentSnapshot{
            found->second.version,
            found->second.text};
    }

    bool applyTextEdits(
        const std::string& filePath,
        rtledit::DocumentVersion expectedVersion,
        const std::vector<
            rtledit::WorkspaceTextEdit>& edits) override
    {
        ++applyCalls;
        auto found = documents.find(filePath);
        if (found == documents.end()
            || found->second.version
                != expectedVersion) {
            return false;
        }
        const auto after =
            rtledit::applyTextEditsToString(
                found->second.text, edits);
        if (!after)
            return false;
        if (applyCalls == failApplyCall) {
            if (mutateBeforeApplyFailure) {
                found->second.text = *after;
                ++found->second.version.value;
            }
            return false;
        }
        found->second.text = *after;
        ++found->second.version.value;
        return true;
    }

    bool restoreSnapshot(
        const std::string& filePath,
        const rtledit::WorkspaceDocumentSnapshot&
            snapshot) override
    {
        ++restoreCalls;
        if (failRestore)
            return false;
        documents[filePath] =
            Document{snapshot.version, snapshot.text};
        return true;
    }

    int applyCalls = 0;
    int restoreCalls = 0;
    int failApplyCall = -1;
    bool mutateBeforeApplyFailure = false;
    bool failRestore = false;

private:
    std::map<std::string, Document> documents;
};

SemanticSnapshotToken installSnapshot(
    SemanticIndex& index)
{
    index.setSnapshot(
        std::make_shared<SemanticIndexSnapshot>());
    return index.snapshotToken();
}

rtledit::WorkspaceTextEdit replacement(
    const std::string& filePath,
    std::uint64_t version,
    std::size_t startColumn)
{
    return rtledit::WorkspaceTextEdit{
        filePath,
        rtledit::DocumentVersion{version},
        {{0, startColumn},
         {0, startColumn + 6}},
        "target",
        "renamed"};
}

rtledit::WorkspaceEditPlan twoFilePlan(
    std::uint64_t semanticRevision,
    const std::string& actionId)
{
    std::vector<rtledit::WorkspaceTextEdit> edits{
        replacement("a.sv", 1, 6),
        replacement("b.sv", 1, 5)};
    std::vector<rtledit::TextEditProvenance>
        provenance;
    for (std::size_t index = 0;
         index < edits.size();
         ++index) {
        rtledit::TextEditProvenance item;
        item.editIndex = index;
        item.actionId = actionId;
        item.anchor.source =
            rtledit::AnchorResolutionSource::
                TreeSitter;
        item.anchor.resolver =
            "rtl_transform_workflow_test";
        item.anchor.semanticSnapshotId =
            std::to_string(semanticRevision);
        item.sourceFilePath =
            edits[index].filePath;
        item.sourceRange = edits[index].range;
        provenance.push_back(std::move(item));
    }

    rtledit::SemanticEditIntent intent;
    intent.kind =
        rtledit::SemanticEditKind::ReplaceText;
    rtledit::WorkspaceEditPlan plan =
        rtledit::makeWorkspaceEditPlan(
            std::move(intent),
            rtledit::RiskLevel::High,
            rtledit::PreviewPolicy::Diff,
            std::move(edits),
            std::move(provenance));
    plan.semanticSnapshot.id =
        std::to_string(semanticRevision);
    plan.semanticIndexFilePaths = {
        "a.sv", "b.sv", "dep.sv"};
    return plan;
}

rtledit::WorkspaceEditPlan olderHistoryPlan(
    std::uint64_t semanticRevision)
{
    rtledit::WorkspaceTextEdit edit{
        "older.sv",
        rtledit::DocumentVersion{1},
        {{0, 3}, {0, 3}},
        "",
        ";"};
    rtledit::TextEditProvenance provenance;
    provenance.editIndex = 0;
    provenance.actionId = "test.older";
    provenance.anchor.source =
        rtledit::AnchorResolutionSource::TreeSitter;
    provenance.anchor.resolver =
        "rtl_transform_workflow_test";
    provenance.anchor.semanticSnapshotId =
        std::to_string(semanticRevision);
    provenance.sourceFilePath = edit.filePath;
    provenance.sourceRange = edit.range;
    rtledit::SemanticEditIntent intent;
    intent.kind =
        rtledit::SemanticEditKind::ReplaceText;
    rtledit::WorkspaceEditPlan plan =
        rtledit::makeWorkspaceEditPlan(
            std::move(intent),
            rtledit::RiskLevel::High,
            rtledit::PreviewPolicy::Diff,
            {std::move(edit)},
            {std::move(provenance)});
    plan.semanticSnapshot.id =
        std::to_string(semanticRevision);
    plan.semanticIndexFilePaths = {"older.sv"};
    return plan;
}

RtlRenameWorkflow::PlanOperation renameOperation(
    const rtledit::WorkspaceEditPlan& plan)
{
    return [plan](
               const RtlRenamePlanQuery& query,
               const rtledit::WorkspaceDocumentManager&) {
        RtlRenameProposal proposal;
        proposal.status = RtlRenamePlanStatus::Ready;
        proposal.message =
            QStringLiteral("Rename preview ready.");
        proposal.dryRun = query.dryRun;
        proposal.workspaceEdit = plan;
        return proposal;
    };
}

RtlConnectionTransformWorkflow::PlanOperation
connectionOperation(
    const rtledit::WorkspaceEditPlan& plan)
{
    return [plan](
               const RtlConnectionTransformRequest&,
               const rtledit::WorkspaceDocumentManager&) {
        RtlConnectionTransformReport report;
        report.status =
            RtlConnectionTransformStatus::Ready;
        report.failure =
            RtlConnectionTransformFailure::None;
        report.message =
            QStringLiteral(
                "Connection transform preview ready.");
        report.workspaceEdit = plan;
        return report;
    };
}

void openFixture(MemoryDocuments& documents)
{
    documents.open("a.sv", "alpha target\n");
    documents.open("b.sv", "beta target\n");
    documents.open("dep.sv", "module dependency;\n");
}

void checkRenameApplyAndSingleUndo()
{
    SemanticIndex index;
    const SemanticSnapshotToken token =
        installSnapshot(index);
    MemoryDocuments documents;
    openFixture(documents);
    documents.open("older.sv", "old\n");
    WorkspaceEditTransactionService transactions;
    QString externalState =
        QStringLiteral("disk-v1");
    RtlRenameWorkflow workflow(
        renameOperation(
            twoFilePlan(
                token.revision,
                "rtl.renamePort")),
        &index,
        &documents,
        &transactions,
        [&externalState](const QString&) {
            return externalState;
        });

    auto olderPrepared = transactions.prepare(
        olderHistoryPlan(token.revision),
        rtledit::SemanticIndexSnapshot{
            std::to_string(token.revision)},
        documents,
        false);
    const auto olderApplied =
        transactions.applyConfirmed(
            std::move(olderPrepared),
            rtledit::SemanticIndexSnapshot{
                std::to_string(token.revision)},
            documents);
    expect(
        "shared transaction fixture has an older undo entry",
        olderApplied.status
            == rtledit::TransactionStatus::Applied);
    const int applyCallsBeforePreview =
        documents.applyCalls;

    RtlRenamePlanQuery query;
    query.semanticToken = token;
    query.dryRun = false;
    const RtlHighRiskEditWorkflowResult preview =
        workflow.preparePreview(query);
    expect(
        "rename facade exposes a structured High+Diff preview",
        preview.state
                == RtlHighRiskEditWorkflowState::
                    PreviewReady
            && preview.preview.ready()
            && preview.preview.actionId
                == QStringLiteral("rtl.renamePort")
            && preview.preview.structuredPreview
                   .riskLevel
                == rtledit::RiskLevel::High
            && preview.preview.structuredPreview
                   .previewPolicy
                == rtledit::PreviewPolicy::Diff
            && !preview.preview.renderedDiff.isEmpty());

    const RtlHighRiskEditWorkflowResult wrong =
        workflow.confirm(QStringLiteral("wrong-token"));
    expect(
        "confirmation is bound to the displayed preview token",
        wrong.state
                == RtlHighRiskEditWorkflowState::
                    PreviewReady
            && wrong.failure
                == RtlHighRiskEditWorkflowFailure::
                    ConfirmationTokenMismatch
            && documents.applyCalls
                == applyCallsBeforePreview);

    const RtlHighRiskEditWorkflowResult applied =
        workflow.confirm(
            preview.preview.confirmationToken);
    expect(
        "one confirmed rename transaction applies every file",
        applied.state
                == RtlHighRiskEditWorkflowState::Applied
            && applied.transactionStatus
                == rtledit::TransactionStatus::Applied
            && documents.text("a.sv")
                == "alpha renamed\n"
            && documents.text("b.sv")
                == "beta renamed\n"
            && workflow.transactionWorkflow()
                   .canUndoAppliedTransaction());

    const RtlHighRiskEditWorkflowResult undone =
        workflow.undo();
    expect(
        "one workflow undo restores the entire rename",
        undone.state
                == RtlHighRiskEditWorkflowState::Undone
            && documents.text("a.sv")
                == "alpha target\n"
            && documents.text("b.sv")
                == "beta target\n"
            && !workflow.transactionWorkflow()
                    .canUndoAppliedTransaction()
            && transactions.canUndo());
    expect(
        "the rename workflow does not consume an older undo entry",
        workflow.undo().failure
                == RtlHighRiskEditWorkflowFailure::
                    NothingToUndo
            && transactions.canUndo());
}

void checkConnectionDryRun()
{
    SemanticIndex index;
    const SemanticSnapshotToken token =
        installSnapshot(index);
    MemoryDocuments documents;
    openFixture(documents);
    WorkspaceEditTransactionService transactions;
    RtlConnectionTransformWorkflow workflow(
        connectionOperation(
            twoFilePlan(
                token.revision,
                "rtl.connection.transform")),
        &index,
        &documents,
        &transactions,
        [](const QString&) {
            return QStringLiteral("disk-v1");
        });

    RtlConnectionTransformRequest request;
    request.expectedSemanticGeneration =
        token.revision;
    const RtlHighRiskEditWorkflowResult preview =
        workflow.preparePreview(request, true);
    const RtlHighRiskEditWorkflowResult dryRun =
        workflow.confirm(
            preview.preview.confirmationToken);
    expect(
        "connection facade dry-run validates without mutation or undo",
        preview.preview.dryRun
            && dryRun.state
                == RtlHighRiskEditWorkflowState::
                    DryRunComplete
            && dryRun.transactionStatus
                == rtledit::TransactionStatus::DryRunOnly
            && documents.applyCalls == 0
            && documents.text("a.sv")
                == "alpha target\n"
            && !transactions.canUndo());
}

void checkConfirmTimeConflicts()
{
    {
        SemanticIndex index;
        const SemanticSnapshotToken token =
            installSnapshot(index);
        MemoryDocuments documents;
        openFixture(documents);
        RtlConnectionTransformWorkflow workflow(
            connectionOperation(
                twoFilePlan(
                    token.revision,
                    "rtl.connection.transform")),
            &index,
            &documents,
            nullptr,
            [](const QString&) {
                return QStringLiteral("disk-v1");
            });
        RtlConnectionTransformRequest request;
        request.expectedSemanticGeneration =
            token.revision;
        const auto preview =
            workflow.preparePreview(request);
        index.setSnapshot(token.snapshot);
        const auto stale = workflow.confirm(
            preview.preview.confirmationToken);
        expect(
            "semantic generation changes reject confirmation",
            stale.failure
                    == RtlHighRiskEditWorkflowFailure::
                        StaleSemanticGeneration
                && stale.transactionStatus
                    == rtledit::TransactionStatus::Stale
                && documents.applyCalls == 0);
    }

    {
        SemanticIndex index;
        const SemanticSnapshotToken token =
            installSnapshot(index);
        MemoryDocuments documents;
        openFixture(documents);
        RtlConnectionTransformWorkflow workflow(
            connectionOperation(
                twoFilePlan(
                    token.revision,
                    "rtl.connection.transform")),
            &index,
            &documents,
            nullptr,
            [](const QString&) {
                return QStringLiteral("disk-v1");
            });
        RtlConnectionTransformRequest request;
        request.expectedSemanticGeneration =
            token.revision;
        const auto preview =
            workflow.preparePreview(request);
        documents.replace(
            "dep.sv", "module dependency;\n", 77);
        const auto stale = workflow.confirm(
            preview.preview.confirmationToken);
        expect(
            "document revision changes reject confirmation",
            stale.failure
                == RtlHighRiskEditWorkflowFailure::
                        StaleDocumentRevision
                && stale.conflictFile.endsWith(
                    QStringLiteral("dep.sv"))
                && documents.applyCalls == 0);
    }

    {
        SemanticIndex index;
        const SemanticSnapshotToken token =
            installSnapshot(index);
        MemoryDocuments documents;
        openFixture(documents);
        QString externalState =
            QStringLiteral("disk-v1");
        RtlConnectionTransformWorkflow workflow(
            connectionOperation(
                twoFilePlan(
                    token.revision,
                    "rtl.connection.transform")),
            &index,
            &documents,
            nullptr,
            [&externalState](const QString&) {
                return externalState;
            });
        RtlConnectionTransformRequest request;
        request.expectedSemanticGeneration =
            token.revision;
        const auto preview =
            workflow.preparePreview(request);
        externalState = QStringLiteral("disk-v2");
        const auto conflict = workflow.confirm(
            preview.preview.confirmationToken);
        expect(
            "external file state changes are distinct conflicts",
            conflict.failure
                    == RtlHighRiskEditWorkflowFailure::
                        ExternalModification
                && conflict.transactionStatus
                    == rtledit::TransactionStatus::Conflict
                && documents.applyCalls == 0);
    }

    {
        SemanticIndex index;
        const SemanticSnapshotToken token =
            installSnapshot(index);
        MemoryDocuments documents;
        openFixture(documents);
        WorkspaceEditTransactionService transactions;
        RtlConnectionTransformWorkflow workflow(
            connectionOperation(
                twoFilePlan(
                    token.revision,
                    "rtl.connection.transform")),
            &index,
            &documents,
            &transactions,
            [](const QString&) {
                return QStringLiteral("disk-v1");
            });
        RtlConnectionTransformRequest request;
        request.expectedSemanticGeneration =
            token.revision;
        const auto preview =
            workflow.preparePreview(request);
        transactions.clearHistory();
        const auto conflict = workflow.confirm(
            preview.preview.confirmationToken);
        expect(
            "shared transaction history generation changes conflict",
            conflict.failure
                    == RtlHighRiskEditWorkflowFailure::
                        TransactionGenerationConflict
                && documents.applyCalls == 0);
    }
}

void checkFailedApplyRollsBack()
{
    SemanticIndex index;
    const SemanticSnapshotToken token =
        installSnapshot(index);
    MemoryDocuments documents;
    openFixture(documents);
    documents.failApplyCall = 2;
    documents.mutateBeforeApplyFailure = true;
    RtlRenameWorkflow workflow(
        renameOperation(
            twoFilePlan(
                token.revision,
                "rtl.renameParameter")),
        &index,
        &documents,
        nullptr,
        [](const QString&) {
            return QStringLiteral("disk-v1");
        });
    RtlRenamePlanQuery query;
    query.semanticToken = token;
    query.dryRun = false;
    const auto preview = workflow.preparePreview(query);
    const auto failed = workflow.confirm(
        preview.preview.confirmationToken);
    expect(
        "second-file apply failure restores the full workspace",
        failed.failure
                == RtlHighRiskEditWorkflowFailure::
                    ApplyFailed
            && documents.text("a.sv")
                == "alpha target\n"
            && documents.text("b.sv")
                == "beta target\n"
            && documents.restoreCalls >= 2
            && !workflow.transactionWorkflow()
                    .canUndoAppliedTransaction());
}

void checkRollbackFailureIsVisible()
{
    SemanticIndex index;
    const SemanticSnapshotToken token =
        installSnapshot(index);
    MemoryDocuments documents;
    openFixture(documents);
    documents.failApplyCall = 2;
    documents.mutateBeforeApplyFailure = true;
    documents.failRestore = true;
    RtlRenameWorkflow workflow(
        renameOperation(
            twoFilePlan(
                token.revision,
                "rtl.renameParameter")),
        &index,
        &documents,
        nullptr,
        [](const QString&) {
            return QStringLiteral("disk-v1");
        });
    RtlRenamePlanQuery query;
    query.semanticToken = token;
    query.dryRun = false;
    const auto preview = workflow.preparePreview(query);
    const auto failed = workflow.confirm(
        preview.preview.confirmationToken);
    expect(
        "rollback residuals are reported as atomic failure",
        failed.failure
                == RtlHighRiskEditWorkflowFailure::
                    AtomicRollbackFailed
            && !failed.conflictFile.isEmpty()
            && !workflow.transactionWorkflow()
                    .canUndoAppliedTransaction());
}

void checkNoChangesAndPlannerRejection()
{
    SemanticIndex index;
    const SemanticSnapshotToken token =
        installSnapshot(index);
    MemoryDocuments documents;
    openFixture(documents);
    RtlConnectionTransformWorkflow noChanges(
        [](const RtlConnectionTransformRequest&,
           const rtledit::WorkspaceDocumentManager&) {
            RtlConnectionTransformReport report;
            report.status =
                RtlConnectionTransformStatus::NoChanges;
            report.failure =
                RtlConnectionTransformFailure::None;
            report.message =
                QStringLiteral("No connection changes.");
            return report;
        },
        &index,
        &documents);
    RtlConnectionTransformRequest request;
    request.expectedSemanticGeneration =
        token.revision;
    expect(
        "planner no-change is a successful terminal state",
        noChanges.preparePreview(request).state
            == RtlHighRiskEditWorkflowState::NoChanges);

    RtlRenameWorkflow rejected(
        [](const RtlRenamePlanQuery&,
           const rtledit::WorkspaceDocumentManager&) {
            RtlRenameProposal proposal;
            proposal.status =
                RtlRenamePlanStatus::OrderedConnection;
            proposal.message =
                QStringLiteral(
                    "Ordered connection cannot be renamed.");
            return proposal;
        },
        &index,
        &documents);
    RtlRenamePlanQuery query;
    query.semanticToken = token;
    expect(
        "planner rejection remains structured in the facade",
        rejected.preparePreview(query).failure
            == RtlHighRiskEditWorkflowFailure::
                PlanningRejected);
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);

    checkRenameApplyAndSingleUndo();
    checkConnectionDryRun();
    checkConfirmTimeConflicts();
    checkFailedApplyRollsBack();
    checkRollbackFailureIsVisible();
    checkNoChangesAndPlannerRejection();

    std::printf(
        "rtl_transform_workflow_test: %d checks, %d failure(s)\n",
        checks,
        failures);
    return failures == 0 ? 0 : 1;
}
