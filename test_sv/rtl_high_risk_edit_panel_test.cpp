#include "rtlhighriskeditpanel.h"

#include "semanticindexsnapshot.h"
#include "workspaceedittransactionservice.h"

#include <rtledit/edit_plan.h>
#include <rtledit/text_edit.h>

#include <QApplication>
#include <QDockWidget>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QStringList>

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
        found->second.text = *after;
        ++found->second.version.value;
        return true;
    }

    bool restoreSnapshot(
        const std::string& filePath,
        const rtledit::WorkspaceDocumentSnapshot&
            snapshotValue) override
    {
        ++restoreCalls;
        documents[filePath] = {
            snapshotValue.version,
            snapshotValue.text};
        return true;
    }

    int applyCalls = 0;
    int restoreCalls = 0;

private:
    std::map<std::string, Document> documents;
};

struct ObservedRequests {
    int renameCalls = 0;
    int connectionCalls = 0;
    QString newName;
    bool renameDryRun = false;
    RtlConnectionTransformRequest connection;
};

SemanticSnapshotToken installSnapshot(
    SemanticIndex& index)
{
    index.setSnapshot(
        std::make_shared<SemanticIndexSnapshot>());
    return index.snapshotToken();
}

SymbolStableKey stableKey(
    const QString& symbol,
    SymbolTaxonomy::DeclarationKind kind)
{
    SymbolStableKey key;
    key.fileName = QStringLiteral("a.sv");
    key.symbolName = symbol;
    key.declarationKind = kind;
    key.ownerScope = QStringLiteral("top");
    key.sourcePosition = 6;
    key.sourceLength = symbol.size();
    return key;
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
            "rtl_high_risk_edit_panel_test";
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
        "a.sv", "b.sv"};
    return plan;
}

RtlRenamePanelSession renameSession(
    const SemanticSnapshotToken& token,
    bool dryRun)
{
    RtlRenamePanelSession session;
    session.baseQuery.subjectStableKey =
        stableKey(
            QStringLiteral("target"),
            SymbolTaxonomy::DeclarationKind::Port);
    session.baseQuery.semanticToken = token;
    session.baseQuery.dryRun = dryRun;
    session.subjectLabel =
        QStringLiteral("top.target (port)");
    session.oldName = QStringLiteral("target");
    session.suggestedNewName =
        QStringLiteral("renamed");
    return session;
}

RtlConnectionTransformPanelSession
connectionSession(
    const SemanticSnapshotToken& token,
    bool dryRun)
{
    RtlConnectionTransformPanelSession session;
    session.baseRequest.instanceStableKey =
        stableKey(
            QStringLiteral("u_leaf"),
            SymbolTaxonomy::DeclarationKind::Instance);
    session.baseRequest.selectedInstancePath =
        QStringLiteral("top.u_leaf");
    session.baseRequest.parentInstancePath =
        QStringLiteral("top");
    session.baseRequest.expectedSemanticGeneration =
        token.revision;
    session.baseRequest.expectedDocumentRevision = 1;
    session.instanceLabel =
        QStringLiteral("top.u_leaf (leaf)");
    session.dryRun = dryRun;
    return session;
}

bool containsFullTokenLikeText(
    const RtlHighRiskEditPanel* panel)
{
    if (!panel)
        return false;
    QStringList texts;
    for (const QLabel* label :
         panel->findChildren<QLabel*>()) {
        texts.append(label->text());
    }
    for (const QLineEdit* edit :
         panel->findChildren<QLineEdit*>()) {
        texts.append(edit->text());
    }
    for (const QPlainTextEdit* edit :
         panel->findChildren<QPlainTextEdit*>()) {
        texts.append(edit->toPlainText());
    }
    const QRegularExpression fullToken(
        QStringLiteral(
            "(?:^|[^0-9a-f])[0-9a-f]{64}(?:$|[^0-9a-f])"),
        QRegularExpression::CaseInsensitiveOption);
    for (const QString& text : texts) {
        if (fullToken.match(text).hasMatch())
            return true;
    }
    return false;
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    SemanticIndex index;
    const SemanticSnapshotToken token =
        installSnapshot(index);
    MemoryDocuments documents;
    documents.open("a.sv", "alpha target\n");
    documents.open("b.sv", "beta target\n");
    WorkspaceEditTransactionService transactions;
    ObservedRequests observed;
    QString externalState =
        QStringLiteral("disk-v1");

    auto renameWorkflow =
        std::make_unique<RtlRenameWorkflow>(
            [&observed, plan = twoFilePlan(
                 token.revision,
                 "rtl.renamePort")](
                const RtlRenamePlanQuery& query,
                const rtledit::
                    WorkspaceDocumentManager&) {
                ++observed.renameCalls;
                observed.newName = query.newName;
                observed.renameDryRun =
                    query.dryRun;
                RtlRenameProposal proposal;
                proposal.status =
                    RtlRenamePlanStatus::Ready;
                proposal.message =
                    QStringLiteral(
                        "Rename preview ready.");
                proposal.dryRun = query.dryRun;
                proposal.workspaceEdit = plan;
                return proposal;
            },
            &index,
            &documents,
            &transactions,
            [&externalState](const QString&) {
                return externalState;
            });

    auto connectionWorkflow =
        std::make_unique<
            RtlConnectionTransformWorkflow>(
            [&observed, plan = twoFilePlan(
                 token.revision,
                 "rtl.connection.transform")](
                const RtlConnectionTransformRequest&
                    request,
                const rtledit::
                    WorkspaceDocumentManager&) {
                ++observed.connectionCalls;
                observed.connection = request;
                RtlConnectionTransformReport report;
                report.status =
                    RtlConnectionTransformStatus::
                        Ready;
                report.failure =
                    RtlConnectionTransformFailure::
                        None;
                report.message =
                    QStringLiteral(
                        "Connection preview ready.");
                report.workspaceEdit = plan;
                return report;
            },
            &index,
            &documents,
            &transactions,
            [&externalState](const QString&) {
                return externalState;
            });

    QWidget dockParent;
    RtlHighRiskEditPanelCoordinator coordinator(
        &dockParent,
        std::move(renameWorkflow),
        std::move(connectionWorkflow));
    expect("hidden change panel is not constructed by its coordinator",
           !dockParent.findChild<RtlHighRiskEditPanel*>());
    coordinator.resetForWorkspaceClose();
    expect("workspace close leaves an unused change panel unconstructed",
           !dockParent.findChild<RtlHighRiskEditPanel*>());
    RtlHighRiskEditPanel* panel =
        coordinator.panel();

    expect(
        "coordinator owns one stable dock and one stable panel",
        coordinator.dock()
            && coordinator.dock()->objectName()
                == QStringLiteral(
                    "rtlHighRiskEditDock")
            && panel
            && panel->objectName()
                == QStringLiteral(
                    "rtlHighRiskEditPanel")
            && coordinator.dock()->widget()->isAncestorOf(panel));

    QString failureReason;
    expect(
        "rename session enters the strict editing state",
        coordinator.beginRename(
            renameSession(token, false),
            &failureReason)
            && failureReason.isEmpty()
            && panel->state()
                == RtlHighRiskEditPanelState::
                    Editing);
    const std::uint64_t firstRenameSession =
        coordinator.activeSessionId();

    const RtlHighRiskEditPanelOutcome
        firstPreview = coordinator.requestPreview();
    expect(
        "preview builds structured files and hunks without writes",
        firstPreview.panelState
                == RtlHighRiskEditPanelState::
                    PreviewReady
            && firstPreview.hasStructuredPreview
            && firstPreview.fileCount == 2
            && firstPreview.editCount == 2
            && panel->diffFileCount() == 2
            && panel->diffHunkCount() == 2
            && panel->displayedDiffText().contains(
                QStringLiteral("+"))
            && documents.applyCalls == 0);
    expect(
        "only the coordinator retains the full confirmation token",
        coordinator.hasPendingConfirmationToken()
            && firstPreview
                   .confirmationFingerprint.size()
                == 12
            && !containsFullTokenLikeText(panel));
    expect(
        "rename UI parameters reach the injected workflow",
        observed.renameCalls == 1
            && observed.newName
                == QStringLiteral("renamed")
            && !observed.renameDryRun);

    panel->setRenameNewName(
        QStringLiteral("renamed_again"));
    expect(
        "changing a parameter cancels the prepared transaction",
        panel->state()
                == RtlHighRiskEditPanelState::
                    Editing
            && !coordinator.hasPendingPreview()
            && !coordinator
                    .hasPendingConfirmationToken()
            && panel->diffFileCount() == 0
            && documents.applyCalls == 0);

    const RtlHighRiskEditPanelOutcome
        secondPreview = coordinator.requestPreview();
    const int writesBeforeConfirm =
        documents.applyCalls;
    const RtlHighRiskEditPanelOutcome applied =
        coordinator.confirm();
    expect(
        "confirmation atomically applies every preview file",
        secondPreview.panelState
                == RtlHighRiskEditPanelState::
                    PreviewReady
            && applied.panelState
                == RtlHighRiskEditPanelState::Applied
            && coordinator
                   .hasProtectedUndoPosition()
            && documents.applyCalls
                == writesBeforeConfirm + 2
            && documents.text("a.sv")
                == "alpha renamed\n"
            && documents.text("b.sv")
                == "beta renamed\n");

    const std::uint64_t appliedSession =
        coordinator.activeSessionId();
    expect(
        "an applied edit rejects action switching",
        !coordinator.beginConnectionTransform(
            connectionSession(token, false),
            &failureReason)
            && coordinator.activeSessionId()
                == appliedSession
            && panel->state()
                == RtlHighRiskEditPanelState::Applied);

    const RtlHighRiskEditPanelOutcome undone =
        coordinator.undo();
    const RtlHighRiskEditPanelOutcome secondUndo =
        coordinator.undo();
    expect(
        "one workflow undo restores the complete transaction",
        undone.panelState
                == RtlHighRiskEditPanelState::Undone
            && !coordinator
                    .hasProtectedUndoPosition()
            && documents.text("a.sv")
                == "alpha target\n"
            && documents.text("b.sv")
                == "beta target\n"
            && secondUndo.failure
                == RtlHighRiskEditWorkflowFailure::
                    NothingToUndo);

    expect(
        "connection dry-run session reuses the same panel",
        coordinator.beginConnectionTransform(
            connectionSession(token, true),
            &failureReason)
            && coordinator.panel() == panel
            && coordinator.dock()->widget()->isAncestorOf(panel));
    const std::uint64_t connectionDryRunSession =
        coordinator.activeSessionId();
    const int writesBeforeDryRun =
        documents.applyCalls;

    const RtlHighRiskEditPanelOutcome stale =
        coordinator.requestPreview(firstRenameSession);
    expect(
        "an old session event is ignored without changing state",
        stale.failure
                == RtlHighRiskEditWorkflowFailure::
                    InvalidState
            && coordinator.activeSessionId()
                == connectionDryRunSession
            && panel->state()
                == RtlHighRiskEditPanelState::
                    Editing
            && documents.applyCalls
                == writesBeforeDryRun);

    panel->setConnectionOptions(
        true,
        true,
        RtlMissingPortConnectionPolicy::
            ConnectSameNamedSignal,
        RtlExplicitCastPolicy::InsertWhenRequired,
        true,
        true);
    const RtlHighRiskEditPanelOutcome dryPreview =
        coordinator.requestPreview();
    const RtlHighRiskEditPanelOutcome dryRun =
        coordinator.confirm();
    expect(
        "connection parameters are captured structurally",
        observed.connectionCalls == 1
            && observed.connection
                   .convertOrderedToNamed
            && observed.connection.addMissingPorts
            && observed.connection.removeUnknownPorts
            && observed.connection.synchronizeAllInstances
            && observed.connection.missingPortPolicy
                == RtlMissingPortConnectionPolicy::
                    ConnectSameNamedSignal
            && observed.connection.castPolicy
                == RtlExplicitCastPolicy::
                    InsertWhenRequired);
    expect(
        "dry-run validates the token with zero writes and no undo",
        dryPreview.panelState
                == RtlHighRiskEditPanelState::
                    DryRunPreviewReady
            && dryRun.panelState
                == RtlHighRiskEditPanelState::
                    DryRunComplete
            && documents.applyCalls
                == writesBeforeDryRun
            && !coordinator
                    .hasProtectedUndoPosition());

    expect(
        "a new live connection session replaces dry-run state",
        coordinator.beginConnectionTransform(
            connectionSession(token, false),
            &failureReason));
    const RtlHighRiskEditPanelOutcome
        conflictPreview = coordinator.requestPreview();
    const int writesBeforeConflict =
        documents.applyCalls;
    externalState = QStringLiteral("disk-v2");
    const RtlHighRiskEditPanelOutcome conflict =
        coordinator.confirm();
    expect(
        "external modification becomes a non-applying conflict",
        conflictPreview.panelState
                == RtlHighRiskEditPanelState::
                    PreviewReady
            && conflict.panelState
                == RtlHighRiskEditPanelState::Conflict
            && conflict.failure
                == RtlHighRiskEditWorkflowFailure::
                    ExternalModification
            && !coordinator
                    .hasPendingConfirmationToken()
            && documents.applyCalls
                == writesBeforeConflict
            && documents.text("a.sv")
                == "alpha target\n"
            && documents.text("b.sv")
                == "beta target\n");

    coordinator.resetForWorkspaceClose();
    expect(
        "workspace reset clears the reusable page session",
        coordinator.activeSessionId() == 0
            && coordinator.activeKind()
                == RtlHighRiskEditKind::None
            && panel->state()
                == RtlHighRiskEditPanelState::Empty
            && coordinator.panel() == panel);

    std::printf(
        "%d checks, %d failures\n",
        checks, failures);
    return failures == 0 ? 0 : 1;
}
