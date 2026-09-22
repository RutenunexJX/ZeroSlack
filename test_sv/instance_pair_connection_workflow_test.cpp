#include "instancepairconnectionworkflow.h"
#include "testuistyle.h"

#include "semanticindexsnapshot.h"
#include "workspaceedittransactionservice.h"

#include <rtledit/edit_plan.h>
#include <rtledit/text_edit.h>

#include <QApplication>
#include <QByteArray>
#include <QDir>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdio>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

int checks = 0;
int failures = 0;

void expect(const char* name, bool condition)
{
    ++checks;
    if (!condition)
        ++failures;
    std::printf(
        "[%s] %s\n",
        condition ? "PASS" : "FAIL",
        name);
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

class MemoryDocuments final
    : public rtledit::WorkspaceDocumentManager
{
public:
    struct Document {
        rtledit::DocumentVersion version;
        std::string text;
    };

    void open(
        const QString& fileName,
        std::uint64_t revision,
        const QString& text)
    {
        docs[utf8String(fileName)] = {
            {revision}, utf8String(text)};
    }

    void replace(
        const QString& fileName,
        std::uint64_t revision,
        const QString& text)
    {
        open(fileName, revision, text);
    }

    QString text(const QString& fileName) const
    {
        const auto found =
            docs.find(utf8String(fileName));
        return found == docs.end()
            ? QString()
            : fromUtf8(found->second.text);
    }

    std::optional<rtledit::WorkspaceDocumentSnapshot>
    snapshot(
        const std::string& filePath) const override
    {
        const auto found = docs.find(filePath);
        if (found == docs.end())
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
        auto found = docs.find(filePath);
        if (found == docs.end()
            || found->second.version
                != expectedVersion) {
            return false;
        }
        const auto after =
            rtledit::applyTextEditsToString(
                found->second.text, edits);
        if (!after)
            return false;

        if (failApplyCall > 0
            && applyCalls == failApplyCall) {
            if (mutateBeforeFailure) {
                found->second.text = *after;
                found->second.version.value += 100;
            }
            return false;
        }

        found->second.text = *after;
        found->second.version.value += 100;
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
        docs[filePath] = {
            snapshot.version, snapshot.text};
        return true;
    }

    int applyCalls = 0;
    int restoreCalls = 0;
    int failApplyCall = -1;
    bool mutateBeforeFailure = false;
    bool failRestore = false;

private:
    std::map<std::string, Document> docs;
};

SemanticSymbolRecord signalRecord(
    const QString& fileName)
{
    SemanticSymbolRecord record;
    record.name = QStringLiteral("payload");
    record.stableKey.fileName = fileName;
    record.stableKey.symbolName = record.name;
    record.stableKey.ownerScope =
        QStringLiteral("source_leaf");
    record.stableKey.sourcePosition = 7;
    record.stableKey.sourceLength =
        record.name.size();
    record.stableKey.declarationKind =
        SymbolTaxonomy::DeclarationKind::Signal;
    record.location.fileName = fileName;
    record.location.startLine = 1;
    record.location.startColumn = 1;
    record.location.position = 7;
    record.location.length = record.name.size();
    record.owner.name = QStringLiteral("source_leaf");
    record.collectorKind =
        SymbolTaxonomy::CollectorKind::Logic;
    return record;
}

struct WorkflowFixture {
    QString leftFile =
        QDir::current().absoluteFilePath(
            QStringLiteral(
                "workflow_fixture/source_leaf.sv"));
    QString rightFile =
        QDir::current().absoluteFilePath(
            QStringLiteral(
                "workflow_fixture/sink_leaf.sv"));
    QString leftBefore =
        QStringLiteral("module source_leaf;\n");
    QString rightBefore =
        QStringLiteral("module sink_leaf;\n");

    std::shared_ptr<const SemanticIndexSnapshot>
        semanticSnapshot =
            std::make_shared<SemanticIndexSnapshot>();
    SemanticIndex semanticIndex;
    MemoryDocuments documents;
    WorkspaceEditTransactionService transactions;
    QStackedWidget stack;
    QWidget* editorPage = nullptr;
    QLineEdit* editorFocus = nullptr;
    std::unique_ptr<
        InstancePairConnectionCoordinator> coordinator;
    std::unique_ptr<
        InstancePairConnectionWorkflow> workflow;
    InstancePairConnectionQuery query;
    int analyzeCalls = 0;
    int planCalls = 0;
    std::optional<InstancePairConnectionFailure>
        forcedPlanningFailure;

    explicit WorkflowFixture(bool dryRun = false)
    {
        documents.open(leftFile, 11, leftBefore);
        documents.open(rightFile, 19, rightBefore);

        semanticIndex.setSnapshot(semanticSnapshot);
        query.leftInstancePath =
            QStringLiteral("top.u_left.u_source");
        query.rightInstancePath =
            QStringLiteral("top.u_right.u_sink");
        query.connectionName =
            QStringLiteral("link");
        query.semanticToken =
            semanticIndex.snapshotToken();
        query.workspaceFiles = {
            leftFile, rightFile};
        query.dryRun = dryRun;
        query.leftSignalContext.fileName = leftFile;
        query.leftSignalContext.moduleName =
            QStringLiteral("source_leaf");
        query.leftSignalContext.documentRevision = 11;

        InstancePairDocumentSnapshot left;
        left.fileName = leftFile;
        left.revision = 11;
        left.text = leftBefore;
        InstancePairDocumentSnapshot right;
        right.fileName = rightFile;
        right.revision = 19;
        right.text = rightBefore;
        query.documents.insert(leftFile, left);
        query.documents.insert(rightFile, right);

        editorPage = new QWidget(&stack);
        auto* editorLayout =
            new QVBoxLayout(editorPage);
        editorFocus =
            new QLineEdit(editorPage);
        editorFocus->setObjectName(
            QStringLiteral(
                "workflowEditorFocusSentinel"));
        editorLayout->addWidget(editorFocus);
        stack.addWidget(editorPage);
        stack.setCurrentWidget(editorPage);
        stack.resize(1000, 720);
        stack.show();
        editorFocus->setFocus(
            Qt::OtherFocusReason);
        QApplication::processEvents();

        coordinator = std::make_unique<
            InstancePairConnectionCoordinator>(&stack);

        InstancePairConnectionWorkflow::Operations
            operations;
        operations.analyze =
            [this](
                const InstancePairConnectionQuery& request,
                rtledit::WorkspaceDocumentManager&) {
                ++analyzeCalls;
                return analysisFor(request);
            };
        operations.plan =
            [this](
                const InstancePairConnectionAnalysis& analysis,
                rtledit::WorkspaceDocumentManager& manager) {
                ++planCalls;
                return proposalFor(
                    analysis, manager);
            };
        workflow = std::make_unique<
            InstancePairConnectionWorkflow>(
                std::move(operations),
                coordinator.get(),
                &semanticIndex,
                &documents,
                &transactions);
    }

    InstancePairConnectionAnalysis analysisFor(
        const InstancePairConnectionQuery& request) const
    {
        InstancePairConnectionAnalysis analysis;
        analysis.status =
            InstancePairConnectionStatus::Ready;
        analysis.failure =
            InstancePairConnectionFailure::None;
        analysis.message =
            QStringLiteral("analysis ready");
        analysis.query = request;
        analysis.capturedDocuments =
            request.documents;
        analysis.leftSignal =
            signalRecord(leftFile);
        analysis.signalType.available = true;
        analysis.signalType.integral = true;
        analysis.signalType.fixedSize = true;
        analysis.signalType.bitWidth = 8;
        analysis.renderedSignalType =
            QStringLiteral("logic [7:0]");

        analysis.blockView.semanticGeneration =
            request.semanticToken.revision;
        analysis.blockView.activeTopModule =
            QStringLiteral("top");
        analysis.blockView.left.side =
            InstancePairSide::Left;
        analysis.blockView.left.instancePath =
            request.leftInstancePath;
        analysis.blockView.left.instanceName =
            QStringLiteral("u_source");
        analysis.blockView.left.moduleName =
            QStringLiteral("source_leaf");
        analysis.blockView.left.definitionFile =
            leftFile;
        analysis.blockView.right.side =
            InstancePairSide::Right;
        analysis.blockView.right.instancePath =
            request.rightInstancePath;
        analysis.blockView.right.instanceName =
            QStringLiteral("u_sink");
        analysis.blockView.right.moduleName =
            QStringLiteral("sink_leaf");
        analysis.blockView.right.definitionFile =
            rightFile;
        analysis.blockView.lcaInstancePath =
            QStringLiteral("top");
        analysis.blockView.lcaModuleName =
            QStringLiteral("top");
        return analysis;
    }

    InstancePairConnectionProposal proposalFor(
        const InstancePairConnectionAnalysis& analysis,
        rtledit::WorkspaceDocumentManager& manager)
    {
        if (forcedPlanningFailure) {
            InstancePairConnectionProposal rejected;
            rejected.status =
                InstancePairConnectionStatus::Rejected;
            rejected.failure =
                *forcedPlanningFailure;
            rejected.message =
                QStringLiteral("structured conflict");
            rejected.blockView = analysis.blockView;
            rejected.leftSignal = analysis.leftSignal;
            rejected.connectionName =
                analysis.query.connectionName;
            rejected.dryRun = analysis.query.dryRun;
            return rejected;
        }

        rtledit::SemanticEditIntent intent;
        std::vector<rtledit::WorkspaceTextEdit>
            edits;
        edits.push_back(
            {utf8String(leftFile),
             {11},
             {{0, 0}, {0, 0}},
             "",
             "// connected-left\n"});
        edits.push_back(
            {utf8String(rightFile),
             {19},
             {{0, 0}, {0, 0}},
             "",
             "// connected-right\n"});

        InstancePairConnectionProposal proposal;
        proposal.blockView =
            analysis.blockView;
        proposal.leftSignal =
            analysis.leftSignal;
        proposal.connectionName =
            analysis.query.connectionName;
        proposal.renderedSignalType =
            analysis.renderedSignalType;
        proposal.dryRun =
            analysis.query.dryRun;
        proposal.workspaceEdit =
            rtledit::makeWorkspaceEditPlan(
                std::move(intent),
                rtledit::RiskLevel::High,
                rtledit::PreviewPolicy::Diff,
                std::move(edits));
        proposal.workspaceEdit.semanticSnapshot = {
            std::to_string(
                analysis.query.semanticToken
                    .revision)};
        proposal.workspaceEdit.semanticIndexFilePaths = {
            utf8String(leftFile),
            utf8String(rightFile)};
        proposal.transaction =
            transactions.prepare(
                proposal.workspaceEdit,
                rtledit::SemanticIndexSnapshot{
                    std::to_string(
                        analysis.query.semanticToken
                            .revision)},
                manager,
                analysis.query.dryRun);
        proposal.sourceDiff =
            proposal.transaction.sourceDiff;
        proposal.renderedDiff = fromUtf8(
            rtledit::
                renderWorkspaceEditSourceDiffHunks(
                    proposal.sourceDiff));
        if (!proposal.transaction.ready()) {
            proposal.status =
                InstancePairConnectionStatus::Rejected;
            proposal.failure =
                InstancePairConnectionFailure::
                    TransactionPreparationFailed;
            proposal.message =
                QStringLiteral("prepare failed");
            return proposal;
        }
        proposal.status =
            InstancePairConnectionStatus::Ready;
        proposal.failure =
            InstancePairConnectionFailure::None;
        proposal.message =
            QStringLiteral("preview ready");
        return proposal;
    }

    InstancePairConnectionWorkflowResult start()
    {
        return workflow->analyzeAndPresent(query);
    }

    InstancePairConnectionPlanRequest request() const
    {
        return coordinator->panel()
            ->currentPlanRequest();
    }

    InstancePairConnectionWorkflowResult preview()
    {
        return workflow->requestPreview(request());
    }

    InstancePairConnectionWorkflowResult confirm()
    {
        return workflow->confirm(request());
    }

    bool sourcesUnchanged() const
    {
        return documents.text(leftFile) == leftBefore
            && documents.text(rightFile) == rightBefore;
    }
};

void successfulUiWorkflowAndSingleUndo()
{
    WorkflowFixture fixture;
    const auto analyzed = fixture.start();
    expect(
        "success: analysis is presented without selecting or focusing the panel",
        analyzed.state
                == InstancePairConnectionWorkflowState::Analyzed
            && fixture.stack.currentWidget()
                == fixture.editorPage
            && fixture.editorFocus->hasFocus()
            && fixture.analyzeCalls == 1);

    QPushButton* previewButton =
        fixture.coordinator->panel()
            ->findChild<QPushButton*>(
                QStringLiteral(
                    "instancePairPreviewButton"));
    if (previewButton)
        previewButton->click();
    expect(
        "success: explicit Preview signal plans and renders without mutation",
        fixture.workflow->state()
                == InstancePairConnectionWorkflowState::
                    PreviewReady
            && fixture.workflow->hasPendingPreview()
            && fixture.planCalls == 1
            && fixture.sourcesUnchanged()
            && fixture.stack.currentWidget()
                == fixture.editorPage
            && fixture.editorFocus->hasFocus());

    QPushButton* confirmButton =
        fixture.coordinator->panel()
            ->findChild<QPushButton*>(
                QStringLiteral(
                    "instancePairConfirmButton"));
    const std::uint64_t generationBeforeApply =
        fixture.transactions.historyGeneration();
    if (confirmButton)
        confirmButton->click();
    const std::uint64_t generationAfterApply =
        fixture.transactions.historyGeneration();
    expect(
        "success: explicit Confirm applies both files atomically",
        fixture.workflow->state()
                == InstancePairConnectionWorkflowState::Applied
            && fixture.documents.text(
                   fixture.leftFile)
                   .startsWith(
                       QStringLiteral(
                           "// connected-left"))
            && fixture.documents.text(
                   fixture.rightFile)
                   .startsWith(
                       QStringLiteral(
                           "// connected-right"))
            && fixture.documents.applyCalls == 2
            && generationAfterApply
                   == generationBeforeApply + 1
            && fixture.workflow
                   ->canUndoAppliedTransaction());

    const auto undone = fixture.workflow->undo();
    const std::uint64_t generationAfterUndo =
        fixture.transactions.historyGeneration();
    const auto secondUndo = fixture.workflow->undo();
    expect(
        "success: one workflow undo restores every file and cannot repeat",
        undone.state
                == InstancePairConnectionWorkflowState::Undone
            && undone.transactionStatus
                == rtledit::TransactionStatus::Undone
            && generationAfterUndo
                   == generationAfterApply + 1
            && fixture.sourcesUnchanged()
            && secondUndo.failure
                == InstancePairConnectionWorkflowFailure::
                    InvalidState);
}

void interleavedTransactionCannotBeUndoneByWorkflow()
{
    WorkflowFixture fixture;
    fixture.start();
    fixture.preview();
    const auto applied = fixture.confirm();
    const auto live =
        fixture.documents.snapshot(
            utf8String(fixture.leftFile));

    rtledit::WorkspaceTextEdit newerEdit;
    if (live) {
        newerEdit.filePath =
            utf8String(fixture.leftFile);
        newerEdit.expectedDocumentVersion =
            live->version;
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
            fixture.query.semanticToken.revision)};
    newerPlan.semanticIndexFilePaths = {
        utf8String(fixture.leftFile)};
    const auto prepared =
        fixture.transactions.prepare(
            newerPlan,
            rtledit::SemanticIndexSnapshot{
                std::to_string(
                    fixture.query.semanticToken.revision)},
            fixture.documents,
            false);
    const auto newerApplied =
        fixture.transactions.applyConfirmed(
            prepared,
            rtledit::SemanticIndexSnapshot{
                std::to_string(
                    fixture.query.semanticToken.revision)},
            fixture.documents);
    const QString afterNewerAction =
        fixture.documents.text(
            fixture.leftFile);
    const auto rejectedUndo =
        fixture.workflow->undo();

    expect(
        "interleaved Action prevents workflow-specific undo from consuming it",
        applied.succeeded()
            && live
            && newerApplied.status
                   == rtledit::TransactionStatus::Applied
            && rejectedUndo.failure
                   == InstancePairConnectionWorkflowFailure::
                       Conflict
            && rejectedUndo.transactionStatus
                   == rtledit::TransactionStatus::Conflict
            && fixture.documents.text(
                   fixture.leftFile)
                   == afterNewerAction
            && afterNewerAction.startsWith(
                   QStringLiteral("// newer action\n"))
            && !fixture.workflow
                    ->canUndoAppliedTransaction());
}

void undoConflictPreservesRetry()
{
    WorkflowFixture fixture;
    fixture.start();
    fixture.preview();
    fixture.confirm();

    fixture.documents.replace(
        fixture.leftFile,
        999,
        QStringLiteral("external change\n"));
    const auto conflicted =
        fixture.workflow->undo();
    const bool remainedRetryable =
        conflicted.state
                == InstancePairConnectionWorkflowState::Applied
            && conflicted.failure
                == InstancePairConnectionWorkflowFailure::UndoFailed
            && conflicted.transactionStatus
                == rtledit::TransactionStatus::Conflict
            && fixture.workflow
                   ->canUndoAppliedTransaction();

    fixture.documents.replace(
        fixture.leftFile,
        111,
        QStringLiteral("// connected-left\n")
            + fixture.leftBefore);
    const auto retried = fixture.workflow->undo();
    expect(
        "undo conflict preserves the applied transaction for one retry",
        remainedRetryable
            && retried.state
                == InstancePairConnectionWorkflowState::Undone
            && fixture.sourcesUnchanged()
            && !fixture.workflow
                    ->canUndoAppliedTransaction());
}

void staleSemanticGenerationRejectsConfirm()
{
    WorkflowFixture fixture;
    fixture.start();
    fixture.preview();
    fixture.semanticIndex.setSnapshot(
        fixture.semanticSnapshot);

    const auto result = fixture.confirm();
    expect(
        "stale semantic generation rejects before apply",
        result.failure
                == InstancePairConnectionWorkflowFailure::
                    StaleSemanticGeneration
            && fixture.sourcesUnchanged()
            && fixture.documents.applyCalls == 0
            && !fixture.transactions.canUndo());
}

void displayedPreviewConflictRejectsConfirm()
{
    WorkflowFixture fixture;
    fixture.start();
    fixture.preview();

    InstancePairConnectionProposal displayed =
        *fixture.workflow->currentProposal();
    bool changedDisplayedHunk = false;
    if (!displayed.sourceDiff.files.empty()
        && !displayed.sourceDiff.files.front()
                .hunks.empty()
        && !displayed.sourceDiff.files.front()
                .hunks.front().lines.empty()) {
        displayed.sourceDiff.files.front()
            .hunks.front().lines.front().text =
            "misleading displayed diff";
        changedDisplayedHunk = true;
    }
    fixture.coordinator->presentProposal(
        displayed);

    const auto result = fixture.confirm();
    expect(
        "confirm rejects a displayed diff that differs from the pending plan",
        changedDisplayedHunk
            && result.failure
                == InstancePairConnectionWorkflowFailure::Conflict
            && result.transactionStatus
                == rtledit::TransactionStatus::Conflict
            && fixture.sourcesUnchanged()
            && fixture.documents.applyCalls == 0
            && !fixture.transactions.canUndo());
}

void structuredPlanningConflictIsClassified()
{
    WorkflowFixture fixture;
    fixture.forcedPlanningFailure =
        InstancePairConnectionFailure::NameConflict;
    fixture.start();

    const auto result = fixture.preview();
    expect(
        "structured planning conflicts remain machine-readable",
        result.failure
                == InstancePairConnectionWorkflowFailure::Conflict
            && result.transactionStatus
                == rtledit::TransactionStatus::Conflict
            && fixture.planCalls == 1
            && fixture.sourcesUnchanged()
            && fixture.documents.applyCalls == 0);
}

void staleDocumentRevisionRejectsConfirm()
{
    WorkflowFixture fixture;
    fixture.start();
    fixture.preview();
    fixture.documents.replace(
        fixture.leftFile,
        12,
        QStringLiteral("external change\n"));

    const auto result = fixture.confirm();
    expect(
        "stale document revision preserves the external edit and other file",
        result.failure
                == InstancePairConnectionWorkflowFailure::
                    StaleDocumentRevision
            && fixture.documents.text(
                   fixture.leftFile)
                   == QStringLiteral(
                       "external change\n")
            && fixture.documents.text(
                   fixture.rightFile)
                   == fixture.rightBefore
            && fixture.documents.applyCalls == 0
            && !fixture.transactions.canUndo());
}

void cancellationDiscardsPendingTransaction()
{
    WorkflowFixture fixture;
    fixture.start();
    fixture.preview();

    const auto cancelled =
        fixture.workflow->cancel();
    const auto confirmAfterCancel =
        fixture.confirm();
    expect(
        "cancel clears preview and permanently rejects its confirmation",
        cancelled.state
                == InstancePairConnectionWorkflowState::Cancelled
            && confirmAfterCancel.state
                == InstancePairConnectionWorkflowState::Cancelled
            && confirmAfterCancel.failure
                == InstancePairConnectionWorkflowFailure::Cancelled
            && fixture.coordinator->panel()
                   ->diffFileCount()
                == 0
            && fixture.sourcesUnchanged()
            && fixture.documents.applyCalls == 0);
}

void crossFileFailureRollsBackEveryDocument()
{
    WorkflowFixture fixture;
    fixture.documents.failApplyCall = 2;
    fixture.documents.mutateBeforeFailure = true;
    fixture.start();
    fixture.preview();

    const auto result = fixture.confirm();
    expect(
        "second-file failure restores current and prior files atomically",
        result.failure
                == InstancePairConnectionWorkflowFailure::ApplyFailed
            && result.transactionStatus
                == rtledit::TransactionStatus::ApplyFailed
            && fixture.sourcesUnchanged()
            && fixture.documents.applyCalls == 2
            && fixture.documents.restoreCalls >= 2
            && !fixture.transactions.canUndo());
}

void dryRunOnlyBuildsPlan()
{
    WorkflowFixture fixture(true);
    fixture.start();
    const auto preview = fixture.preview();
    const auto completed = fixture.confirm();
    expect(
        "dry-run produces High+Diff plan without entering apply",
        preview.state
                == InstancePairConnectionWorkflowState::
                    PreviewReady
            && completed.state
                == InstancePairConnectionWorkflowState::
                    DryRunComplete
            && completed.transactionStatus
                == rtledit::TransactionStatus::DryRunOnly
            && fixture.workflow->currentProposal()
            && fixture.workflow->currentProposal()
                   ->transaction.ready()
            && fixture.documents.applyCalls == 0
            && fixture.sourcesUnchanged()
            && !fixture.transactions.canUndo());
}

} // namespace

int main(int argc, char** argv)
{
    qputenv(
        "QT_QPA_PLATFORM",
        QByteArrayLiteral("offscreen"));
    QApplication app(argc, argv);
    if (!initializeUiStyleForTest()) return 3;

    successfulUiWorkflowAndSingleUndo();
    interleavedTransactionCannotBeUndoneByWorkflow();
    undoConflictPreservesRetry();
    staleSemanticGenerationRejectsConfirm();
    displayedPreviewConflictRejectsConfirm();
    structuredPlanningConflictIsClassified();
    staleDocumentRevisionRejectsConfirm();
    cancellationDiscardsPendingTransaction();
    crossFileFailureRollsBackEveryDocument();
    dryRunOnlyBuildsPlan();

    std::printf(
        "%d checks, %d failures\n",
        checks, failures);
    return failures == 0 ? 0 : 1;
}
