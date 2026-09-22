#include "multisignalpropagationpanel.h"
#include "testuistyle.h"

#include "semanticindexsnapshot.h"
#include "workspaceedittransactionservice.h"

#include <rtledit/text_edit.h>

#include <QApplication>
#include <QByteArray>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableWidget>

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

class DummyDocuments final
    : public rtledit::WorkspaceDocumentManager
{
public:
    std::optional<rtledit::WorkspaceDocumentSnapshot>
    snapshot(const std::string&) const override
    {
        return std::nullopt;
    }

    bool applyTextEdits(
        const std::string&,
        rtledit::DocumentVersion,
        const std::vector<
            rtledit::WorkspaceTextEdit>&) override
    {
        ++applyCalls;
        return false;
    }

    bool restoreSnapshot(
        const std::string&,
        const rtledit::WorkspaceDocumentSnapshot&)
        override
    {
        ++restoreCalls;
        return false;
    }

    int applyCalls = 0;
    int restoreCalls = 0;
};

class WorkflowDocuments final
    : public rtledit::WorkspaceDocumentManager
{
public:
    void open(
        const std::string& filePath,
        std::uint64_t version,
        std::string text)
    {
        files[filePath] = {
            {version}, std::move(text)};
    }

    std::optional<rtledit::WorkspaceDocumentSnapshot>
    snapshot(
        const std::string& filePath) const override
    {
        const auto found = files.find(filePath);
        return found == files.end()
            ? std::nullopt
            : std::optional<
                  rtledit::WorkspaceDocumentSnapshot>(
                  found->second);
    }

    bool applyTextEdits(
        const std::string& filePath,
        rtledit::DocumentVersion expectedVersion,
        const std::vector<
            rtledit::WorkspaceTextEdit>& edits) override
    {
        auto found = files.find(filePath);
        if (found == files.end()
            || found->second.version
                   != expectedVersion) {
            return false;
        }
        const auto after =
            rtledit::applyTextEditsToString(
                found->second.text, edits);
        if (!after)
            return false;
        found->second.text = *after;
        ++found->second.version.value;
        return true;
    }

    bool restoreSnapshot(
        const std::string& filePath,
        const rtledit::WorkspaceDocumentSnapshot&
            snapshotValue) override
    {
        files[filePath] = snapshotValue;
        return true;
    }

    std::string text(
        const std::string& filePath) const
    {
        const auto found = files.find(filePath);
        return found == files.end()
            ? std::string() : found->second.text;
    }

private:
    std::map<
        std::string,
        rtledit::WorkspaceDocumentSnapshot> files;
};

MultiSignalPropagationSignalChoice signalChoice(
    const QString& name,
    int position)
{
    MultiSignalPropagationSignalChoice choice;
    choice.label =
        QStringLiteral("leaf.%1").arg(name);
    choice.member.context.fileName =
        QStringLiteral("rtl/leaf.sv");
    choice.member.context.moduleName =
        QStringLiteral("leaf");
    choice.member.context.documentText =
        QStringLiteral(
            "module leaf; logic payload; logic flags; "
            "logic valid; endmodule");
    choice.member.context.cursorPosition = position;
    choice.member.context.documentRevision = 9;
    choice.member.exportedPortName =
        name + QStringLiteral("_out");
    choice.member.groupMemberName = name;
    return choice;
}

MultiSignalPropagationPanelInput panelInput()
{
    MultiSignalPropagationPanelInput input;
    input.signalChoices = {
        signalChoice(
            QStringLiteral("payload"), 19),
        signalChoice(
            QStringLiteral("valid"), 52),
        signalChoice(
            QStringLiteral("flags"), 34),
    };
    input.ancestors = {
        {QStringLiteral("Active design top"),
         QString()},
        {QStringLiteral("Middle instance"),
         QStringLiteral("top.u_middle")},
    };
    input.workspaceFiles = {
        QStringLiteral("rtl/leaf.sv"),
        QStringLiteral("rtl/middle.sv"),
    };
    input.semanticToken.revision = 41;
    input.semanticToken.snapshot =
        std::make_shared<
            const SemanticIndexSnapshot>();

    MultiSignalPropagationDocumentSnapshot leaf;
    leaf.fileName =
        QStringLiteral("rtl/leaf.sv");
    leaf.revision = 9;
    leaf.text =
        input.signalChoices.constFirst()
            .member.context.documentText;
    input.capturedDocuments.insert(
        leaf.fileName, leaf);
    return input;
}

MultiSignalPropagationProposal readyProposal(
    const MultiSignalPropagationQuery& query)
{
    MultiSignalPropagationProposal proposal;
    proposal.status =
        MultiSignalPropagationStatus::Ready;
    proposal.failure =
        MultiSignalPropagationFailure::None;
    proposal.message =
        QStringLiteral("Atomic preview ready.");
    proposal.dryRun = query.dryRun;
    proposal.targetAncestorInstancePath =
        query.targetAncestorInstancePath;
    proposal.workspaceEdit.riskLevel =
        rtledit::RiskLevel::High;
    proposal.workspaceEdit.previewPolicy =
        rtledit::PreviewPolicy::Diff;

    rtledit::SourceDiffFile file;
    file.filePath = "rtl/leaf.sv";
    file.version = {9};
    file.editCount = 2;
    file.beforeText =
        "module leaf;\nendmodule\n";
    file.afterText =
        "module leaf;\n"
        "output logic debug_payload;\n"
        "output logic debug_flags;\n"
        "endmodule\n";
    rtledit::SourceDiffHunk hunk;
    hunk.oldStartLine = 1;
    hunk.oldLineCount = 2;
    hunk.newStartLine = 1;
    hunk.newLineCount = 4;
    hunk.lines = {
        {rtledit::SourceDiffLineKind::Context,
         1, 1, "module leaf;"},
        {rtledit::SourceDiffLineKind::Added,
         0, 2, "output logic debug_payload;"},
        {rtledit::SourceDiffLineKind::Added,
         0, 3, "output logic debug_flags;"},
        {rtledit::SourceDiffLineKind::Context,
         2, 4, "endmodule"},
    };
    file.hunks.push_back(std::move(hunk));
    proposal.sourceDiff.status =
        rtledit::SourceDiffStatus::Built;
    proposal.sourceDiff.files.push_back(
        std::move(file));

    proposal.transaction.status =
        rtledit::TransactionPrepareStatus::Ready;
    proposal.transaction.plan =
        proposal.workspaceEdit;
    proposal.transaction.preview.status =
        rtledit::PreviewStatus::Built;
    proposal.transaction.preview.riskLevel =
        rtledit::RiskLevel::High;
    proposal.transaction.preview.previewPolicy =
        rtledit::PreviewPolicy::Diff;
    proposal.transaction.preview.fileCount = 1;
    proposal.transaction.preview.editCount = 2;
    proposal.transaction.sourceDiff =
        proposal.sourceDiff;
    proposal.transaction.dryRun = query.dryRun;
    proposal.transaction.previewConfirmed = false;
    const std::string rendered =
        rtledit::renderWorkspaceEditSourceDiffHunks(
            proposal.sourceDiff);
    proposal.renderedDiff =
        QString::fromUtf8(
            rendered.data(),
            static_cast<qsizetype>(
                rendered.size()));
    return proposal;
}

MultiSignalPropagationProposal conflictProposal()
{
    MultiSignalPropagationProposal proposal;
    proposal.status =
        MultiSignalPropagationStatus::Rejected;
    proposal.failure =
        MultiSignalPropagationFailure::
            DuplicatePortName;
    proposal.message =
        QStringLiteral(
            "Multiple members resolve to port "
            "\"debug_payload\".");
    proposal.blockers.append(
        QStringLiteral(
            "Existing ancestor port debug_payload "
            "has an incompatible direction."));
    proposal.dryRun = true;
    return proposal;
}

MultiSignalPropagationProposal workflowProposal(
    const MultiSignalPropagationQuery& query,
    rtledit::WorkspaceDocumentManager& documents,
    WorkspaceEditTransactionService& transactions,
    const std::string& filePath)
{
    const auto live = documents.snapshot(filePath);
    MultiSignalPropagationProposal proposal;
    proposal.dryRun = query.dryRun;
    if (!live) {
        proposal.status =
            MultiSignalPropagationStatus::Rejected;
        proposal.failure =
            MultiSignalPropagationFailure::
                MissingDocumentSnapshot;
        return proposal;
    }

    rtledit::WorkspaceTextEdit edit;
    edit.filePath = filePath;
    edit.expectedDocumentVersion =
        live->version;
    edit.range = {{0, 0}, {0, 0}};
    edit.newText = "// propagated\n";
    proposal.workspaceEdit =
        rtledit::makeWorkspaceEditPlan(
            {},
            rtledit::RiskLevel::High,
            rtledit::PreviewPolicy::Diff,
            {edit});
    proposal.workspaceEdit.semanticSnapshot = {
        std::to_string(
            query.semanticToken.revision)};
    proposal.workspaceEdit
        .semanticIndexFilePaths = {filePath};
    proposal.transaction =
        transactions.prepare(
            proposal.workspaceEdit,
            rtledit::SemanticIndexSnapshot{
                std::to_string(
                    query.semanticToken.revision)},
            documents,
            query.dryRun);
    proposal.sourceDiff =
        proposal.transaction.sourceDiff;
    const std::string rendered =
        rtledit::renderWorkspaceEditSourceDiffHunks(
            proposal.sourceDiff);
    proposal.renderedDiff =
        QString::fromUtf8(
            rendered.data(),
            static_cast<qsizetype>(
                rendered.size()));
    if (!proposal.transaction.ready()) {
        proposal.status =
            MultiSignalPropagationStatus::Rejected;
        proposal.failure =
            MultiSignalPropagationFailure::
                TransactionPreparationFailed;
        return proposal;
    }
    proposal.status =
        MultiSignalPropagationStatus::Ready;
    proposal.failure =
        MultiSignalPropagationFailure::None;
    proposal.message =
        QStringLiteral("Workflow preview ready.");
    return proposal;
}

} // namespace

int main(int argc, char** argv)
{
    qputenv(
        "QT_QPA_PLATFORM",
        QByteArrayLiteral("offscreen"));
    QApplication app(argc, argv);
    if (!initializeUiStyleForTest()) return 3;

    DummyDocuments documents;
    int planCalls = 0;
    std::optional<MultiSignalPropagationQuery>
        capturedQuery;
    MultiSignalPropagationPanel panel(
        [&](
            const MultiSignalPropagationQuery& query,
            rtledit::WorkspaceDocumentManager& manager) {
            ++planCalls;
            capturedQuery = query;
            expect(
                "preview callback receives the configured document manager",
                &manager == &documents);
            return readyProposal(query);
        },
        documents);
    panel.setInput(panelInput());
    panel.resize(860, 680);
    panel.show();
    QApplication::processEvents();

    QTableWidget* signalTable =
        panel.findChild<QTableWidget*>(
            QStringLiteral(
                "multiSignalPropagationSignals"));
    QComboBox* ancestors =
        panel.findChild<QComboBox*>(
            QStringLiteral(
                "multiSignalPropagationAncestor"));
    QComboBox* mode =
        panel.findChild<QComboBox*>(
            QStringLiteral(
                "multiSignalPropagationMode"));
    QLineEdit* groupName =
        panel.findChild<QLineEdit*>(
            QStringLiteral(
                "multiSignalPropagationGroupName"));
    QPushButton* preview =
        panel.findChild<QPushButton*>(
            QStringLiteral(
                "multiSignalPropagationPreviewButton"));
    QPushButton* confirm =
        panel.findChild<QPushButton*>(
            QStringLiteral(
                "multiSignalPropagationConfirmButton"));
    QPushButton* undo =
        panel.findChild<QPushButton*>(
            QStringLiteral(
                "multiSignalPropagationUndoButton"));
    QLabel* summary =
        panel.findChild<QLabel*>(
            QStringLiteral(
                "multiSignalPropagationTransactionSummary"));
    QPlainTextEdit* diff =
        panel.findChild<QPlainTextEdit*>(
            QStringLiteral(
                "multiSignalPropagationDiff"));
    QListWidget* blockers =
        panel.findChild<QListWidget*>(
            QStringLiteral(
                "multiSignalPropagationBlockers"));

    expect(
        "panel exposes table and fixed-choice structured inputs",
        signalTable
            && signalTable->rowCount() == 3
            && signalTable->columnCount() == 3
            && ancestors
            && ancestors->count() == 2
            && !ancestors->isEditable()
            && mode
            && mode->count() == 2
            && !mode->isEditable()
            && groupName
            && preview
            && confirm
            && undo
            && !preview->isEnabled());

    panel.setSignalSelected(0, true);
    panel.setSignalSelected(2, true);
    panel.setSelectedAncestorIndex(1);
    panel.setMode(
        MultiSignalPropagationMode::PortGroup);
    panel.setGroupName(
        QStringLiteral("debug"));
    panel.setGroupMemberName(
        0, QStringLiteral("payload"));
    panel.setGroupMemberName(
        2, QStringLiteral("flags"));
    expect(
        "two checked semantic rows enable explicit preview",
        panel.selectedSignalCount() == 2
            && preview->isEnabled()
            && planCalls == 0);

    preview->click();
    expect(
        "group preview builds one dry-run structured planner query",
        planCalls == 1
            && capturedQuery
            && capturedQuery->dryRun
            && capturedQuery->members.size() == 2
            && capturedQuery->mode
                   == MultiSignalPropagationMode::
                       PortGroup
            && capturedQuery->groupName
                   == QStringLiteral("debug")
            && capturedQuery
                   ->targetAncestorInstancePath
                   == QStringLiteral(
                       "top.u_middle")
            && capturedQuery->semanticToken.revision
                   == 41
            && capturedQuery->workspaceFiles.size()
                   == 2
            && capturedQuery->members.at(0)
                   .groupMemberName
                   == QStringLiteral("payload")
            && capturedQuery->members.at(1)
                   .groupMemberName
                   == QStringLiteral("flags")
            && capturedQuery->members.at(0)
                   .exportedPortName.isEmpty());
    expect(
        "prepared result renders a non-applying High+Diff preview",
        panel.hasHighDiffPreview()
            && panel.proposal()
            && panel.proposal()->ready()
            && panel.proposal()->transaction.dryRun
            && !panel.proposal()
                    ->transaction.previewConfirmed
            && summary
            && summary->text().contains(
                QStringLiteral("Risk: High"))
            && summary->text().contains(
                QStringLiteral("Preview: Diff"))
            && summary->text().contains(
                QStringLiteral("Dry run: yes"))
            && diff
            && diff->isReadOnly()
            && diff->toPlainText().contains(
                QStringLiteral(
                    "+output logic debug_payload;"))
            && blockers
            && blockers->count() == 0);
    expect(
        "dry-run preview cannot emit an applying confirmation",
        panel.findChild<QPushButton*>(
            QStringLiteral(
                "multiSignalPropagationApplyButton"))
                == nullptr
            && confirm
            && !confirm->isEnabled()
            && undo
            && !undo->isEnabled()
            && documents.applyCalls == 0
            && documents.restoreCalls == 0);

    panel.setMode(
        MultiSignalPropagationMode::
            IndependentPorts);
    panel.setExportedPortName(
        0, QStringLiteral("payload_trace"));
    panel.setExportedPortName(
        2, QStringLiteral("flags_trace"));
    preview->click();
    expect(
        "optional group can be disabled for exact independent ports",
        planCalls == 2
            && capturedQuery
            && capturedQuery->mode
                   == MultiSignalPropagationMode::
                       IndependentPorts
            && capturedQuery->groupName.isEmpty()
            && capturedQuery->members.at(0)
                   .exportedPortName
                   == QStringLiteral(
                       "payload_trace")
            && capturedQuery->members.at(1)
                   .exportedPortName
                   == QStringLiteral(
                       "flags_trace")
            && capturedQuery->members.at(0)
                   .groupMemberName.isEmpty());

    MultiSignalPropagationPanelInput applyingInput =
        panelInput();
    applyingInput.dryRun = false;
    applyingInput.signalChoices[0].selected = true;
    applyingInput.signalChoices[1].selected = true;
    panel.setInput(applyingInput);
    QSignalSpy confirmSpy(
        &panel,
        &MultiSignalPropagationPanel::confirmRequested);
    expect(
        "non-dry-run planning still renders before explicit confirmation",
        panel.requestPreview()
            && capturedQuery
            && !capturedQuery->dryRun
            && panel.proposal()
            && !panel.proposal()->dryRun
            && !panel.proposal()->transaction.dryRun
            && confirm->isEnabled()
            && documents.applyCalls == 0);
    confirm->click();
    expect(
        "confirmation is a typed outbound request and never applies in the panel",
        confirmSpy.count() == 1
            && documents.applyCalls == 0
            && documents.restoreCalls == 0);

    int conflictCalls = 0;
    MultiSignalPropagationPanel conflictPanel(
        [&](
            const MultiSignalPropagationQuery&,
            rtledit::WorkspaceDocumentManager&) {
            ++conflictCalls;
            return conflictProposal();
        },
        documents);
    MultiSignalPropagationPanelInput conflictInput =
        panelInput();
    conflictInput.signalChoices[0].selected = true;
    conflictInput.signalChoices[1].selected = true;
    conflictInput.mode =
        MultiSignalPropagationMode::PortGroup;
    conflictInput.groupName =
        QStringLiteral("debug");
    conflictInput.selectedAncestorIndex = 1;
    conflictPanel.setInput(conflictInput);
    const bool conflictReady =
        conflictPanel.requestPreview();
    QListWidget* conflictBlockers =
        conflictPanel.findChild<QListWidget*>(
            QStringLiteral(
                "multiSignalPropagationBlockers"));
    expect(
        "planner conflicts remain visible without a partial diff",
        !conflictReady
            && conflictCalls == 1
            && conflictPanel.proposal()
            && conflictPanel.proposal()->failure
                   == MultiSignalPropagationFailure::
                       DuplicatePortName
            && conflictPanel.statusText().contains(
                QStringLiteral(
                    "Multiple members resolve"))
            && conflictBlockers
            && conflictBlockers->count() == 1
            && conflictBlockers->item(0)
                   ->text().contains(
                       QStringLiteral(
                           "incompatible direction"))
            && conflictPanel.renderedDiff()
                   .isEmpty()
            && !conflictPanel
                    .hasHighDiffPreview());

    conflictPanel.setSignalSelected(1, false);
    const int callsBeforeInvalidSelection =
        conflictCalls;
    expect(
        "fewer than two selected signals never invokes the planner",
        !conflictPanel.requestPreview()
            && conflictCalls
                   == callsBeforeInvalidSelection
            && conflictPanel.statusText().contains(
                QStringLiteral(
                    "at least two signals"),
                Qt::CaseInsensitive));

    MultiSignalPropagationPanel unsafePanel(
        [](
            const MultiSignalPropagationQuery& query,
            rtledit::WorkspaceDocumentManager&) {
            MultiSignalPropagationProposal proposal =
                readyProposal(query);
            proposal.transaction.previewConfirmed =
                true;
            return proposal;
        },
        documents);
    MultiSignalPropagationPanelInput unsafeInput =
        panelInput();
    unsafeInput.signalChoices[0].selected = true;
    unsafeInput.signalChoices[1].selected = true;
    unsafePanel.setInput(unsafeInput);
    expect(
        "confirmed or non-preview planner output is refused",
        !unsafePanel.requestPreview()
            && !unsafePanel.hasHighDiffPreview()
            && unsafePanel.renderedDiff().isEmpty()
            && unsafePanel.statusText().contains(
                QStringLiteral("refused")));

    MultiSignalPropagationPanel mismatchedDiffPanel(
        [](
            const MultiSignalPropagationQuery& query,
            rtledit::WorkspaceDocumentManager&) {
            MultiSignalPropagationProposal proposal =
                readyProposal(query);
            proposal.renderedDiff =
                QStringLiteral(
                    "misleading diff unrelated to the prepared transaction");
            return proposal;
        },
        documents);
    MultiSignalPropagationPanelInput mismatchInput =
        panelInput();
    mismatchInput.signalChoices[0].selected = true;
    mismatchInput.signalChoices[1].selected = true;
    mismatchedDiffPanel.setInput(mismatchInput);
    expect(
        "displayed Diff must exactly match the prepared transaction",
        !mismatchedDiffPanel.requestPreview()
            && !mismatchedDiffPanel.hasHighDiffPreview()
            && mismatchedDiffPanel.renderedDiff().isEmpty()
            && mismatchedDiffPanel.statusText().contains(
                QStringLiteral("internally consistent")));

    const std::string workflowFile =
        "rtl/workflow_leaf.sv";
    const std::string workflowBefore =
        "module workflow_leaf;\nendmodule\n";
    WorkflowDocuments workflowDocuments;
    workflowDocuments.open(
        workflowFile, 17, workflowBefore);
    SemanticIndex workflowSemanticIndex;
    workflowSemanticIndex.setSnapshot(
        std::make_shared<
            const SemanticIndexSnapshot>());
    WorkspaceEditTransactionService
        workflowTransactions;
    MultiSignalPropagationPanel workflowPanel(
        [&](
            const MultiSignalPropagationQuery& query,
            rtledit::WorkspaceDocumentManager& manager) {
            return workflowProposal(
                query,
                manager,
                workflowTransactions,
                workflowFile);
        },
        workflowDocuments);
    MultiSignalPropagationWorkflow workflow(
        &workflowPanel,
        &workflowSemanticIndex,
        &workflowDocuments,
        &workflowTransactions);
    MultiSignalPropagationPanelInput workflowInput =
        panelInput();
    workflowInput.dryRun = false;
    workflowInput.semanticToken =
        workflowSemanticIndex.snapshotToken();
    workflowInput.signalChoices[0].selected = true;
    workflowInput.signalChoices[1].selected = true;
    workflowPanel.setInput(workflowInput);
    const std::uint64_t generationBefore =
        workflowTransactions.historyGeneration();
    const bool workflowPreviewReady =
        workflowPanel.requestPreview();
    const MultiSignalPropagationWorkflowResult
        applied = workflow.confirm();
    const std::uint64_t appliedGeneration =
        workflowTransactions.historyGeneration();
    const std::string appliedText =
        workflowDocuments.text(workflowFile);
    const MultiSignalPropagationWorkflowResult
        undone = workflow.undo();
    const std::uint64_t undoneGeneration =
        workflowTransactions.historyGeneration();
    const MultiSignalPropagationWorkflowResult
        duplicateUndo = workflow.undo();
    expect(
        "workflow confirms atomically and exposes exactly one owned undo",
        workflowPreviewReady
            && applied.succeeded()
            && applied.state
                   == MultiSignalPropagationWorkflowState::
                       Applied
            && appliedGeneration
                   == generationBefore + 1
            && appliedText.rfind(
                   "// propagated\n", 0) == 0
            && workflowDocuments.text(workflowFile)
                   == workflowBefore
            && undone.succeeded()
            && undone.state
                   == MultiSignalPropagationWorkflowState::
                       Undone
            && undoneGeneration
                   == appliedGeneration + 1
            && !duplicateUndo.succeeded());

    workflowPanel.setInput(workflowInput);
    const bool secondPreviewReady =
        workflowPanel.requestPreview();
    const MultiSignalPropagationWorkflowResult
        secondApplied = workflow.confirm();
    const auto currentBeforeInterleave =
        workflowDocuments.snapshot(workflowFile);
    rtledit::WorkspaceTextEdit newerEdit;
    if (currentBeforeInterleave) {
        newerEdit.filePath = workflowFile;
        newerEdit.expectedDocumentVersion =
            currentBeforeInterleave->version;
        newerEdit.range = {{0, 0}, {0, 0}};
        newerEdit.newText = "// newer action\n";
    }
    rtledit::WorkspaceEditPlan newerPlan =
        rtledit::makeWorkspaceEditPlan(
            {},
            rtledit::RiskLevel::High,
            rtledit::PreviewPolicy::Diff,
            {newerEdit});
    newerPlan.semanticSnapshot = {
        std::to_string(
            workflowInput.semanticToken.revision)};
    newerPlan.semanticIndexFilePaths = {
        workflowFile};
    const auto newerPrepared =
        workflowTransactions.prepare(
            newerPlan,
            rtledit::SemanticIndexSnapshot{
                std::to_string(
                    workflowInput.semanticToken.revision)},
            workflowDocuments,
            false);
    const auto newerApplied =
        workflowTransactions.applyConfirmed(
            newerPrepared,
            rtledit::SemanticIndexSnapshot{
                std::to_string(
                    workflowInput.semanticToken.revision)},
            workflowDocuments);
    const std::string afterNewerAction =
        workflowDocuments.text(workflowFile);
    const MultiSignalPropagationWorkflowResult
        interleavedUndo = workflow.undo();
    expect(
        "workflow undo refuses to consume a newer Action transaction",
        secondPreviewReady
            && secondApplied.succeeded()
            && currentBeforeInterleave
            && newerApplied.status
                   == rtledit::TransactionStatus::Applied
            && !interleavedUndo.succeeded()
            && interleavedUndo.transactionStatus
                   == rtledit::TransactionStatus::Conflict
            && workflowDocuments.text(workflowFile)
                   == afterNewerAction
            && afterNewerAction.starts_with(
                   "// newer action\n")
            && !workflow.canUndoAppliedTransaction());

    std::printf(
        "%d checks, %d failures\n",
        checks, failures);
    return failures == 0 ? 0 : 1;
}
