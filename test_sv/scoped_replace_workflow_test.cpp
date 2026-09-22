#include "notificationcenter.h"
#include "testuistyle.h"
#include "scopedreplaceworkflow.h"
#include "scopedsearchpanel.h"
#include "searchservice.h"
#include "workspaceedittransactionservice.h"

#include <rtledit/edit_plan.h>
#include <rtledit/text_edit.h>

#include <QApplication>
#include <QByteArray>
#include <QDockWidget>
#include <QFile>
#include <QLineEdit>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTreeWidget>

#include <cstdio>
#include <cstdint>
#include <map>
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

bool writeUtf8File(
    const QString& fileName,
    const QString& text)
{
    QFile file(fileName);
    const QByteArray bytes = text.toUtf8();
    return file.open(
               QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(bytes) == bytes.size();
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
        std::uint64_t version,
        const QString& text)
    {
        documents[utf8String(fileName)] =
            Document{{version}, utf8String(text)};
    }

    void replace(
        const QString& fileName,
        std::uint64_t version,
        const QString& text)
    {
        open(fileName, version, text);
    }

    QString text(const QString& fileName) const
    {
        const auto found =
            documents.find(utf8String(fileName));
        return found == documents.end()
            ? QString()
            : fromUtf8(found->second.text);
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

        if (failApplyCall > 0
            && applyCalls == failApplyCall) {
            if (mutateBeforeFailure) {
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
    bool mutateBeforeFailure = false;
    bool failRestore = false;

private:
    std::map<std::string, Document> documents;
};

struct PreviewFixture {
    QString firstFile;
    QString secondFile;
    QString firstBefore =
        QStringLiteral("logic target;\n");
    QString secondBefore =
        QStringLiteral("wire target;\n");
    quint64 firstSearchRevision = 7;
    quint64 secondSearchRevision = 11;
    QList<SearchDocumentSnapshot> searchedDocuments;
    ReplacePreviewPlan preview;

    PreviewFixture(
        QString first,
        QString second = QString())
        : firstFile(std::move(first))
        , secondFile(std::move(second))
    {
        searchedDocuments.append(
            SearchDocumentSnapshot{
                firstFile,
                firstBefore,
                nullptr,
                firstSearchRevision});
        if (!secondFile.isEmpty()) {
            searchedDocuments.append(
                SearchDocumentSnapshot{
                    secondFile,
                    secondBefore,
                    nullptr,
                    secondSearchRevision});
        }

        std::vector<rtledit::WorkspaceTextEdit> edits;
        edits.push_back(makeEdit(
            firstFile,
            firstSearchRevision,
            firstBefore,
            QStringLiteral("renamed")));
        if (!secondFile.isEmpty()) {
            edits.push_back(makeEdit(
                secondFile,
                secondSearchRevision,
                secondBefore,
                QStringLiteral("renamed")));
        }

        rtledit::SemanticEditIntent intent;
        intent.kind =
            rtledit::SemanticEditKind::ReplaceText;
        preview.transactionPlan =
            rtledit::makeWorkspaceEditPlan(
                std::move(intent),
                rtledit::RiskLevel::High,
                rtledit::PreviewPolicy::Diff,
                std::move(edits));
        preview.status = ReplacePreviewStatus::Ready;
        preview.files.append(
            filePreview(
                firstFile, firstSearchRevision));
        if (!secondFile.isEmpty()) {
            preview.files.append(
                filePreview(
                    secondFile, secondSearchRevision));
        }
    }

    static rtledit::WorkspaceTextEdit makeEdit(
        const QString& fileName,
        quint64 revision,
        const QString& text,
        const QString& replacement)
    {
        const int start =
            text.indexOf(QStringLiteral("target"));
        rtledit::WorkspaceTextEdit edit;
        edit.filePath = utf8String(fileName);
        edit.expectedDocumentVersion = {revision};
        edit.range = {
            {0, static_cast<std::size_t>(start)},
            {0, static_cast<std::size_t>(
                    start
                    + QStringLiteral("target").size())}};
        edit.expectedText = "target";
        edit.newText = utf8String(replacement);
        return edit;
    }

    static ReplaceFilePreview filePreview(
        const QString& fileName,
        quint64 revision)
    {
        ReplaceFilePreview file;
        file.fileName = fileName;
        file.fileIdentity = fileName;
        file.documentRevision = revision;
        file.selected = true;
        return file;
    }
};

void checkApplyAndSingleUndo()
{
    PreviewFixture fixture(
        QStringLiteral("memory/a.sv"),
        QStringLiteral("memory/b.sv"));
    MemoryDocuments documents;
    documents.open(
        fixture.firstFile, 1001, fixture.firstBefore);
    documents.open(
        fixture.secondFile, 2001, fixture.secondBefore);
    WorkspaceEditTransactionService transactions;
    NotificationCenter notifications;
    ScopedReplaceWorkflow workflow(
        &documents, &transactions, &notifications);

    const ScopedReplaceWorkflowResult prepared =
        workflow.preparePreview(
            fixture.preview,
            fixture.searchedDocuments);
    expect(
        "preview rebinds search revisions and renders High+Diff",
        prepared.state
                == ScopedReplaceWorkflowState::PreviewReady
            && prepared.failure
                == ScopedReplaceWorkflowFailure::None
            && prepared.fileCount == 2
            && prepared.editCount == 2
            && !prepared.renderedDiff.isEmpty()
            && workflow.hasPendingPreview()
            && workflow.preparedTransaction()
            && workflow.preparedTransaction()
                   ->plan.edits.front()
                   .expectedDocumentVersion.value
                == 1001);

    const ScopedReplaceWorkflowResult applied =
        workflow.confirm();
    expect(
        "explicit confirmation applies one atomic transaction",
        applied.state
                == ScopedReplaceWorkflowState::Applied
            && applied.transactionStatus
                == rtledit::TransactionStatus::Applied
            && documents.text(fixture.firstFile)
                == QStringLiteral("logic renamed;\n")
            && documents.text(fixture.secondFile)
                == QStringLiteral("wire renamed;\n")
            && workflow.canUndoAppliedTransaction());

    const ScopedReplaceWorkflowResult undone =
        workflow.undo();
    expect(
        "one workflow undo restores every changed file",
        undone.state
                == ScopedReplaceWorkflowState::Undone
            && documents.text(fixture.firstFile)
                == fixture.firstBefore
            && documents.text(fixture.secondFile)
                == fixture.secondBefore
            && !workflow.canUndoAppliedTransaction());
    expect(
        "undo is not silently repeated",
        workflow.undo().failure
                == ScopedReplaceWorkflowFailure::
                    NothingToUndo
            && notifications.size() == 1);
}

void checkStaleRevision()
{
    PreviewFixture fixture(
        QStringLiteral("memory/stale.sv"));
    MemoryDocuments documents;
    documents.open(
        fixture.firstFile, 301, fixture.firstBefore);
    NotificationCenter notifications;
    ScopedReplaceWorkflow workflow(
        &documents, nullptr, &notifications);

    expect(
        "stale fixture preview is initially valid",
        workflow.preparePreview(
                    fixture.preview,
                    fixture.searchedDocuments)
                .state
            == ScopedReplaceWorkflowState::PreviewReady);
    QList<SearchDocumentSnapshot> staleDocuments =
        fixture.searchedDocuments;
    staleDocuments[0].revision += 1;
    const ScopedReplaceWorkflowResult result =
        workflow.confirm(staleDocuments);
    expect(
        "confirm-time search revision change is rejected",
        result.failure
                == ScopedReplaceWorkflowFailure::
                    StaleSearchRevision
            && result.transactionStatus
                == rtledit::TransactionStatus::Stale
            && documents.applyCalls == 0
            && notifications.size() == 1);

    workflow.preparePreview(
        fixture.preview,
        fixture.searchedDocuments);
    documents.replace(
        fixture.firstFile, 302, fixture.firstBefore);
    const ScopedReplaceWorkflowResult documentResult =
        workflow.confirm(fixture.searchedDocuments);
    expect(
        "confirm-time document-manager revision change is rejected",
        documentResult.failure
                == ScopedReplaceWorkflowFailure::
                    StaleDocumentRevision
            && documentResult.transactionStatus
                == rtledit::TransactionStatus::Stale
            && documents.applyCalls == 0);
}

void checkExternalModification()
{
    QTemporaryDir directory;
    expect(
        "external-modification temporary directory exists",
        directory.isValid());
    if (!directory.isValid())
        return;

    const QString fileName =
        directory.filePath(QStringLiteral("external.sv"));
    PreviewFixture fixture(fileName);
    writeUtf8File(fileName, fixture.firstBefore);
    MemoryDocuments documents;
    documents.open(
        fileName, 401, fixture.firstBefore);
    NotificationCenter notifications;
    ScopedReplaceWorkflow workflow(
        &documents, nullptr, &notifications);
    workflow.preparePreview(
        fixture.preview,
        fixture.searchedDocuments);

    const QString externalText =
        QStringLiteral("// external\n")
        + fixture.firstBefore;
    expect(
        "external fixture mutation is written",
        writeUtf8File(fileName, externalText));
    const ScopedReplaceWorkflowResult result =
        workflow.confirm();
    expect(
        "external file state change conflicts before document mutation",
        result.failure
                == ScopedReplaceWorkflowFailure::
                    ExternalModification
            && result.transactionStatus
                == rtledit::TransactionStatus::Conflict
            && documents.applyCalls == 0
            && documents.text(fileName)
                == fixture.firstBefore
            && notifications.size() == 1);
}

void checkSecondFileFailureRollsBack()
{
    PreviewFixture fixture(
        QStringLiteral("memory/rollback_a.sv"),
        QStringLiteral("memory/rollback_b.sv"));
    MemoryDocuments documents;
    documents.open(
        fixture.firstFile, 501, fixture.firstBefore);
    documents.open(
        fixture.secondFile, 601, fixture.secondBefore);
    documents.failApplyCall = 2;
    documents.mutateBeforeFailure = true;
    NotificationCenter notifications;
    ScopedReplaceWorkflow workflow(
        &documents, nullptr, &notifications);
    workflow.preparePreview(
        fixture.preview,
        fixture.searchedDocuments);

    const ScopedReplaceWorkflowResult result =
        workflow.confirm();
    expect(
        "second-file failure rolls back current and prior files",
        result.failure
                == ScopedReplaceWorkflowFailure::ApplyFailed
            && result.transactionStatus
                == rtledit::TransactionStatus::ApplyFailed
            && documents.applyCalls == 2
            && documents.restoreCalls >= 2
            && documents.text(fixture.firstFile)
                == fixture.firstBefore
            && documents.text(fixture.secondFile)
                == fixture.secondBefore
            && !workflow.canUndoAppliedTransaction()
            && notifications.size() == 1);
}

void checkRollbackFailureIsVisible()
{
    PreviewFixture fixture(
        QStringLiteral("memory/residual_a.sv"),
        QStringLiteral("memory/residual_b.sv"));
    MemoryDocuments documents;
    documents.open(
        fixture.firstFile, 611, fixture.firstBefore);
    documents.open(
        fixture.secondFile, 612, fixture.secondBefore);
    documents.failApplyCall = 2;
    documents.mutateBeforeFailure = true;
    documents.failRestore = true;
    NotificationCenter notifications;
    ScopedReplaceWorkflow workflow(
        &documents, nullptr, &notifications);
    workflow.preparePreview(
        fixture.preview,
        fixture.searchedDocuments);

    const ScopedReplaceWorkflowResult result =
        workflow.confirm();
    const QList<NotificationItem> posted =
        notifications.notifications();
    expect(
        "rollback residuals are critical and never reported as success",
        result.failure
                == ScopedReplaceWorkflowFailure::
                    AtomicRollbackFailed
            && result.state
                == ScopedReplaceWorkflowState::Failed
            && !posted.isEmpty()
            && posted.constLast().severity
                == NotificationSeverity::Critical
            && documents.text(fixture.firstFile)
                != fixture.firstBefore);
}

void checkCancelAndDryRun()
{
    PreviewFixture fixture(
        QStringLiteral("memory/cancel_dry.sv"));
    MemoryDocuments documents;
    documents.open(
        fixture.firstFile, 701, fixture.firstBefore);
    ScopedReplaceWorkflow workflow(&documents);

    workflow.preparePreview(
        fixture.preview,
        fixture.searchedDocuments);
    const ScopedReplaceWorkflowResult cancelled =
        workflow.cancel();
    expect(
        "cancel discards the pending preview without mutation",
        cancelled.state
                == ScopedReplaceWorkflowState::Cancelled
            && !workflow.hasPendingPreview()
            && documents.applyCalls == 0
            && documents.text(fixture.firstFile)
                == fixture.firstBefore);

    workflow.preparePreview(
        fixture.preview,
        fixture.searchedDocuments,
        true);
    const ScopedReplaceWorkflowResult dryRun =
        workflow.confirm();
    expect(
        "dry-run confirmation validates only the plan",
        dryRun.state
                == ScopedReplaceWorkflowState::DryRunComplete
            && dryRun.transactionStatus
                == rtledit::TransactionStatus::DryRunOnly
            && documents.applyCalls == 0
            && documents.text(fixture.firstFile)
                == fixture.firstBefore
            && !workflow.canUndoAppliedTransaction());
}

void checkHiddenDockDoesNotExpandOrStealFocus()
{
    QTemporaryDir directory;
    expect(
        "hidden-dock temporary directory exists",
        directory.isValid());
    if (!directory.isValid())
        return;

    const QString fileName =
        directory.filePath(QStringLiteral("panel.sv"));
    const QString text =
        QStringLiteral(
            "module panel;\n"
            "  logic target;\n"
            "endmodule\n");
    writeUtf8File(fileName, text);

    MemoryDocuments documents;
    documents.open(fileName, 17, text);
    ScopedReplaceWorkflow workflow(&documents);
    SearchService searchService(nullptr);

    QMainWindow window;
    auto* focusSentinel = new QLineEdit(&window);
    window.setCentralWidget(focusSentinel);
    ScopedSearchPanelCoordinator coordinator(
        &window, &searchService);
    coordinator.setReplaceWorkflow(&workflow);
    ScopedSearchPanelContext context;
    context.documents = {
        SearchDocumentSnapshot{
            fileName, text, nullptr, 17}};
    context.workspaceDocuments = context.documents;
    context.workspaceDocumentsSpecified = true;
    context.activeFileName = fileName;
    coordinator.setSearchContext(context);
    window.addDockWidget(
        Qt::BottomDockWidgetArea,
        coordinator.dock());
    window.resize(900, 620);
    window.show();
    QApplication::processEvents();
    coordinator.dock()->hide();
    focusSentinel->setFocus();
    QApplication::processEvents();

    const int hiddenHeight =
        coordinator.dock()->height();
    QWidget* const focusBefore =
        QApplication::focusWidget();
    coordinator.panel()->setQueryText(
        QStringLiteral("target"));
    coordinator.panel()->setScope(
        ScopedSearchScope::Workspace);
    coordinator.panel()->setReplacementText(
        QStringLiteral("renamed"));
    coordinator.refresh();
    coordinator.buildReplacePreview();
    QApplication::processEvents();

    expect(
        "hidden result and Diff updates preserve dock geometry and focus",
        coordinator.dock()->isHidden()
            && coordinator.dock()->height()
                == hiddenHeight
            && QApplication::focusWidget()
                == focusBefore
            && workflow.state()
                == ScopedReplaceWorkflowState::PreviewReady
            && coordinator.panel()->applyButton()
                   ->isEnabled()
            && coordinator.panel()->cancelButton()
                   ->isEnabled()
            && !coordinator.panel()->undoButton()
                    ->isEnabled()
            && !coordinator.panel()->diffView()
                    ->toPlainText().isEmpty());
    window.hide();
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    if (!initializeUiStyleForTest()) return 3;

    checkApplyAndSingleUndo();
    checkStaleRevision();
    checkExternalModification();
    checkSecondFileFailureRollsBack();
    checkRollbackFailureIsVisible();
    checkCancelAndDryRun();
    checkHiddenDockDoesNotExpandOrStealFocus();

    std::printf(
        "scoped_replace_workflow_test: %d/%d checks passed\n",
        checks - failures,
        checks);
    return failures == 0 ? 0 : 1;
}
