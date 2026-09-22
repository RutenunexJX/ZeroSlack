#include "instancepairconnectionpanel.h"
#include "testuistyle.h"

#include "semanticindexsnapshot.h"

#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdio>
#include <memory>
#include <string>
#include <utility>

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

SemanticSymbolRecord signalRecord(
    const QString& fileName,
    const QString& owner,
    const QString& name,
    int position)
{
    SemanticSymbolRecord record;
    record.name = name;
    record.stableKey.fileName = fileName;
    record.stableKey.symbolName = name;
    record.stableKey.ownerScope = owner;
    record.stableKey.sourcePosition = position;
    record.stableKey.sourceLength = name.size();
    record.stableKey.declarationKind =
        SymbolTaxonomy::DeclarationKind::Signal;
    record.location.fileName = fileName;
    record.location.startLine = 4;
    record.location.startColumn = 3;
    record.location.position = position;
    record.location.length = name.size();
    record.owner.name = owner;
    record.collectorKind =
        SymbolTaxonomy::CollectorKind::Logic;
    return record;
}

InstancePairBlockPortView portView(
    const QString& fileName,
    const QString& owner,
    const QString& name,
    SymbolTaxonomy::CollectorKind direction,
    int position)
{
    InstancePairBlockPortView port;
    port.name = name;
    port.direction = direction;
    port.record =
        signalRecord(fileName, owner, name, position);
    port.record.collectorKind = direction;
    port.effectiveType.available = true;
    port.effectiveType.integral = true;
    port.effectiveType.fixedSize = true;
    port.effectiveType.bitWidth = 8;
    port.effectiveType.resolvedTypeText =
        QStringLiteral("logic [7:0]");
    return port;
}

InstancePairConnectionAnalysis readyAnalysis()
{
    const QString leftFile =
        QStringLiteral("rtl/source_leaf.sv");
    const QString rightFile =
        QStringLiteral("rtl/sink_leaf.sv");

    InstancePairConnectionAnalysis analysis;
    analysis.status = InstancePairConnectionStatus::Ready;
    analysis.failure = InstancePairConnectionFailure::None;
    analysis.message = QStringLiteral("ready");
    analysis.leftSignal = signalRecord(
        leftFile,
        QStringLiteral("source_leaf"),
        QStringLiteral("payload"),
        42);
    analysis.signalType.available = true;
    analysis.signalType.integral = true;
    analysis.signalType.fixedSize = true;
    analysis.signalType.bitWidth = 8;
    analysis.renderedSignalType =
        QStringLiteral("logic [7:0]");

    analysis.query.connectionName =
        QStringLiteral("link");
    analysis.query.leftInstancePath =
        QStringLiteral("top.u_left.u_source");
    analysis.query.rightInstancePath =
        QStringLiteral("top.u_right.u_sink");
    analysis.query.semanticToken.snapshot =
        std::make_shared<SemanticIndexSnapshot>();
    analysis.query.semanticToken.revision = 77;
    analysis.query.leftSignalContext.documentRevision = 11;

    analysis.blockView.semanticGeneration = 77;
    analysis.blockView.activeTopModule =
        QStringLiteral("top");
    analysis.blockView.left.side =
        InstancePairSide::Left;
    analysis.blockView.left.instancePath =
        analysis.query.leftInstancePath;
    analysis.blockView.left.instanceName =
        QStringLiteral("u_source");
    analysis.blockView.left.moduleName =
        QStringLiteral("source_leaf");
    analysis.blockView.left.definitionFile = leftFile;
    analysis.blockView.left.instanceFile =
        QStringLiteral("rtl/left_mid.sv");
    analysis.blockView.left.ports.append(
        portView(
            leftFile,
            QStringLiteral("source_leaf"),
            QStringLiteral("data_o"),
            SymbolTaxonomy::CollectorKind::PortOutput,
            18));

    analysis.blockView.right.side =
        InstancePairSide::Right;
    analysis.blockView.right.instancePath =
        analysis.query.rightInstancePath;
    analysis.blockView.right.instanceName =
        QStringLiteral("u_sink");
    analysis.blockView.right.moduleName =
        QStringLiteral("sink_leaf");
    analysis.blockView.right.definitionFile = rightFile;
    analysis.blockView.right.instanceFile =
        QStringLiteral("rtl/right_mid.sv");
    analysis.blockView.right.ports.append(
        portView(
            rightFile,
            QStringLiteral("sink_leaf"),
            QStringLiteral("data_i"),
            SymbolTaxonomy::CollectorKind::PortInput,
            21));

    InstancePairDocumentSnapshot leftDocument;
    leftDocument.fileName = leftFile;
    leftDocument.revision = 11;
    InstancePairDocumentSnapshot rightDocument;
    rightDocument.fileName = rightFile;
    rightDocument.revision = 19;
    analysis.capturedDocuments.insert(
        leftFile, leftDocument);
    analysis.capturedDocuments.insert(
        rightFile, rightDocument);
    analysis.query.documents =
        analysis.capturedDocuments;
    return analysis;
}

rtledit::SourceDiffFile diffFile(
    const QString& fileName,
    std::uint64_t revision,
    const std::string& addedLine)
{
    rtledit::SourceDiffFile file;
    file.filePath = fileName.toStdString();
    file.version = {revision};
    file.editCount = 1;
    rtledit::SourceDiffHunk hunk;
    hunk.oldStartLine = 1;
    hunk.oldLineCount = 1;
    hunk.newStartLine = 1;
    hunk.newLineCount = 2;
    hunk.lines.push_back(
        {rtledit::SourceDiffLineKind::Context,
         1,
         1,
         "module leaf;"});
    hunk.lines.push_back(
        {rtledit::SourceDiffLineKind::Added,
         0,
         2,
         addedLine});
    file.hunks.push_back(std::move(hunk));
    return file;
}

InstancePairConnectionProposal readyProposal(
    const InstancePairConnectionAnalysis& analysis)
{
    InstancePairConnectionProposal proposal;
    proposal.status = InstancePairConnectionStatus::Ready;
    proposal.failure = InstancePairConnectionFailure::None;
    proposal.message = QStringLiteral("ready");
    proposal.blockView = analysis.blockView;
    proposal.leftSignal = analysis.leftSignal;
    proposal.connectionName =
        analysis.query.connectionName;
    proposal.renderedSignalType =
        analysis.renderedSignalType;
    proposal.dryRun = false;

    proposal.workspaceEdit.riskLevel =
        rtledit::RiskLevel::High;
    proposal.workspaceEdit.previewPolicy =
        rtledit::PreviewPolicy::Diff;
    proposal.workspaceEdit.semanticSnapshot = {"77"};
    proposal.workspaceEdit.baselines = {
        {"rtl/source_leaf.sv", {11}},
        {"rtl/sink_leaf.sv", {19}}};

    proposal.sourceDiff.status =
        rtledit::SourceDiffStatus::Built;
    proposal.sourceDiff.semanticSnapshot = {"77"};
    proposal.sourceDiff.files.push_back(
        diffFile(
            QStringLiteral("rtl/source_leaf.sv"),
            11,
            "output logic [7:0] link;"));
    proposal.sourceDiff.files.push_back(
        diffFile(
            QStringLiteral("rtl/sink_leaf.sv"),
            19,
            "input logic [7:0] link;"));

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
    proposal.transaction.preview.fileCount = 2;
    proposal.transaction.preview.editCount = 2;
    proposal.transaction.preview.semanticSnapshot = {"77"};
    proposal.transaction.sourceDiff =
        proposal.sourceDiff;
    proposal.transaction.previewConfirmed = false;
    const std::string renderedDiff =
        rtledit::renderWorkspaceEditSourceDiffHunks(
            proposal.transaction.sourceDiff);
    proposal.renderedDiff = QString::fromUtf8(
        renderedDiff.data(),
        static_cast<qsizetype>(renderedDiff.size()));
    return proposal;
}

bool sendDrop(
    QWidget* target,
    const InstancePairConnectionPlanRequest& request)
{
    InstancePairConnectionDragMimeData mime(request);
    QDragEnterEvent enter(
        QPoint(8, 8),
        Qt::CopyAction,
        &mime,
        Qt::LeftButton,
        Qt::NoModifier);
    QApplication::sendEvent(target, &enter);
    QDropEvent drop(
        QPointF(8, 8),
        Qt::CopyAction,
        &mime,
        Qt::LeftButton,
        Qt::NoModifier);
    QApplication::sendEvent(target, &drop);
    return enter.isAccepted() && drop.isAccepted();
}

} // namespace

int main(int argc, char** argv)
{
    qputenv(
        "QT_QPA_PLATFORM",
        QByteArrayLiteral("offscreen"));
    QApplication app(argc, argv);
    if (!initializeUiStyleForTest()) return 3;

    QWidget host;
    auto* layout = new QVBoxLayout(&host);
    auto* stack = new QStackedWidget(&host);
    auto* editorPage = new QWidget(stack);
    auto* editorLayout = new QVBoxLayout(editorPage);
    auto* editorFocus = new QLineEdit(editorPage);
    editorFocus->setObjectName(
        QStringLiteral("editorFocusSentinel"));
    editorLayout->addWidget(editorFocus);
    stack->addWidget(editorPage);
    stack->setCurrentWidget(editorPage);
    layout->addWidget(stack);
    host.resize(1000, 760);
    host.show();
    editorFocus->setFocus(Qt::OtherFocusReason);
    QApplication::processEvents();

    InstancePairConnectionCoordinator coordinator(stack);
    const InstancePairConnectionAnalysis analysis =
        readyAnalysis();
    coordinator.presentAnalysis(analysis);
    coordinator.presentAnalysis(analysis);
    QApplication::processEvents();

    InstancePairConnectionPanel* panel =
        coordinator.panel();
    expect(
        "repeated presentation reuses one panel page",
        panel
            && stack->count() == 2
            && stack->findChildren<
                   InstancePairConnectionPanel*>()
                   .size()
                == 1);
    expect(
        "analysis update does not select, show, or focus the panel",
        stack->currentWidget() == editorPage
            && !panel->isVisible()
            && editorFocus->hasFocus());

    QLabel* leftInstance =
        panel->findChild<QLabel*>(
            QStringLiteral(
                "instancePairLeftInstancePath"));
    QLabel* leftModule =
        panel->findChild<QLabel*>(
            QStringLiteral("instancePairLeftModule"));
    QLabel* rightInstance =
        panel->findChild<QLabel*>(
            QStringLiteral(
                "instancePairRightInstancePath"));
    expect(
        "side-by-side blocks expose instance path and module context",
        leftInstance
            && leftInstance->text().contains(
                analysis.blockView.left.instancePath)
            && leftModule
            && leftModule->text().contains(
                analysis.blockView.left.moduleName)
            && rightInstance
            && rightInstance->text().contains(
                analysis.blockView.right.instancePath));
    QTreeWidget* leftItems =
        panel->findChild<QTreeWidget*>(
            QStringLiteral("instancePairLeftItems"));
    QTreeWidget* rightItems =
        panel->findChild<QTreeWidget*>(
            QStringLiteral("instancePairRightItems"));
    expect(
        "both blocks render structured signal and port rows",
        leftItems
            && rightItems
            && leftItems->topLevelItemCount() == 2
            && rightItems->topLevelItemCount() == 2
            && leftItems->topLevelItem(0)->childCount() == 1
            && leftItems->topLevelItem(1)->childCount() == 1
            && rightItems->topLevelItem(1)->childCount() == 1);

    stack->setCurrentWidget(panel);
    QApplication::processEvents();
    QWidget* dropTarget =
        panel->findChild<QWidget*>(
            QStringLiteral(
                "instancePairRightDropTarget"));
    QSignalSpy planSpy(
        &coordinator,
        &InstancePairConnectionCoordinator::planRequested);

    InstancePairConnectionPlanRequest stale =
        panel->currentPlanRequest();
    --stale.semanticGeneration;
    expect(
        "stale drag token is rejected without requesting a plan",
        dropTarget
            && !sendDrop(dropTarget, stale)
            && planSpy.count() == 0);

    const InstancePairConnectionPlanRequest request =
        panel->currentPlanRequest();
    InstancePairConnectionDragMimeData activeMime(request);
    QDragEnterEvent activeEnter(
        QPoint(8, 8),
        Qt::CopyAction,
        &activeMime,
        Qt::LeftButton,
        Qt::NoModifier);
    QApplication::sendEvent(dropTarget, &activeEnter);
    expect(
        "valid drag enters a typed active state",
        activeEnter.isAccepted()
            && panel->dragActive());
    QKeyEvent escape(
        QEvent::KeyPress,
        Qt::Key_Escape,
        Qt::NoModifier);
    QApplication::sendEvent(dropTarget, &escape);
    expect(
        "Escape clears drag state without a plan request",
        !panel->dragActive()
            && planSpy.count() == 0);

    expect(
        "legal typed drag requests one structured plan",
        sendDrop(dropTarget, request)
            && planSpy.count() == 1);
    const InstancePairConnectionPlanRequest emitted =
        qvariant_cast<InstancePairConnectionPlanRequest>(
            planSpy.at(0).at(0));
    expect(
        "plan request preserves semantic identity and both revisions",
        emitted.leftSignalStableKey
                == analysis.leftSignal.stableKey
            && emitted.leftInstancePath
                == analysis.blockView.left.instancePath
            && emitted.rightInstancePath
                == analysis.blockView.right.instancePath
            && emitted.documentRevision == 11
            && emitted.semanticGeneration == 77
            && emitted.documentRevisions.size() == 2);

    stack->setCurrentWidget(editorPage);
    editorFocus->setFocus(Qt::OtherFocusReason);
    QApplication::processEvents();
    InstancePairConnectionProposal staleProposal =
        readyProposal(analysis);
    ++staleProposal.blockView.semanticGeneration;
    expect(
        "stale proposal is refused and never displayed",
        !coordinator.presentProposal(staleProposal)
            && !panel->hasDisplayableProposal()
            && panel->diffFileCount() == 0);

    const InstancePairConnectionProposal proposal =
        readyProposal(analysis);
    InstancePairConnectionProposal mismatchedDiff =
        proposal;
    mismatchedDiff.sourceDiff.files.front()
        .hunks.front().lines.back().text =
        "misleading display";
    expect(
        "displayed Diff must exactly match the prepared transaction",
        !coordinator.presentProposal(mismatchedDiff)
            && !panel->hasDisplayableProposal()
            && panel->diffFileCount() == 0);
    expect(
        "matching High+Diff proposal renders one tab per file",
        coordinator.presentProposal(proposal)
            && panel->hasDisplayableProposal()
            && panel->diffFileCount() == 2
            && stack->currentWidget() == editorPage
            && editorFocus->hasFocus());
    QLabel* summary =
        panel->findChild<QLabel*>(
            QStringLiteral(
                "instancePairTransactionSummary"));
    auto* firstDiff =
        panel->findChild<QPlainTextEdit*>(
            QStringLiteral("instancePairDiffFile_0"));
    expect(
        "preview exposes High+Diff transaction metadata and hunks",
        summary
            && summary->text().contains(
                QStringLiteral("Risk: High"))
            && summary->text().contains(
                QStringLiteral("Preview: Diff"))
            && firstDiff
            && firstDiff->toPlainText().contains(
                QStringLiteral(
                    "+output logic [7:0] link;")));

    QSignalSpy previewSpy(
        &coordinator,
        &InstancePairConnectionCoordinator::previewRequested);
    QSignalSpy confirmSpy(
        &coordinator,
        &InstancePairConnectionCoordinator::confirmRequested);
    QSignalSpy undoSpy(
        &coordinator,
        &InstancePairConnectionCoordinator::undoRequested);
    stack->setCurrentWidget(panel);
    QApplication::processEvents();
    QPushButton* previewButton =
        panel->findChild<QPushButton*>(
            QStringLiteral(
                "instancePairPreviewButton"));
    QPushButton* confirmButton =
        panel->findChild<QPushButton*>(
            QStringLiteral(
                "instancePairConfirmButton"));
    QPushButton* undoButton =
        panel->findChild<QPushButton*>(
            QStringLiteral(
                "instancePairUndoButton"));
    if (previewButton)
        previewButton->click();
    if (confirmButton)
        confirmButton->click();
    const InstancePairConnectionProposal*
        proposalForConfirmation =
            panel->proposalForConfirmation();
    expect(
        "preview and confirmation remain explicit outbound signals",
        previewButton
            && confirmButton
            && previewSpy.count() == 1
            && confirmSpy.count() == 1
            && proposalForConfirmation
            && !proposalForConfirmation
                    ->transaction.previewConfirmed);
    panel->setWorkflowOutcome(
        QStringLiteral("Applied"), true, true);
    if (undoButton)
        undoButton->click();
    expect(
        "applied workflow exposes one explicit unified undo request",
        undoButton
            && undoButton->isEnabled()
            && undoSpy.count() == 1);

    coordinator.presentAnalysis(analysis);
    InstancePairConnectionProposal dryRunProposal =
        proposal;
    dryRunProposal.dryRun = true;
    dryRunProposal.transaction.dryRun = true;
    expect(
        "dry-run preview never enables applying confirmation",
        coordinator.presentProposal(dryRunProposal)
            && confirmButton
            && !confirmButton->isEnabled()
            && panel->hasDisplayableProposal());

    std::printf(
        "%d checks, %d failures\n",
        checks, failures);
    return failures == 0 ? 0 : 1;
}
