#include "tabmanager.h"
#include "workspaceeditdocumentmanager.h"
#include "documentregistry.h"
#include "editorfileidentity.h"
#include "workspaceconfigurationdialog.h"
#include "workspacemanager.h"
#include "rtlhighriskeditpanel.h"
#include "workspaceedittransactionservice.h"
#include "semanticindexsnapshot.h"
#include <QPushButton>
#include <zeroslack/documents/documentfileread.h>
#include <rtledit/workspace_edit_transaction.h>
#include <rtledit/patch_engine.h>
#include <rtledit/text_edit.h>
#include <QApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QDir>
#include <QProcess>
#include <QPointer>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

namespace {
bool writeBytes(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
struct AliasFixture {
    QTemporaryDir root;
    QString original, replacement, alias, moved;
    AliasFixture() {
        // Preserve these small fixtures; never recursively delete a junction.
        root.setAutoRemove(false);
        original = root.filePath("original");
        replacement = root.filePath("replacement");
        alias = root.filePath("alias");
        moved = root.filePath("moved-alias");
    }
    bool link(const QString& target) {
#ifdef Q_OS_WIN
        QProcess process;
        process.start("cmd.exe", {"/d", "/s", "/c", "mklink", "/J",
            QDir::toNativeSeparators(alias), QDir::toNativeSeparators(target)});
        return process.waitForFinished(10000) && process.exitCode() == 0 && QFileInfo(alias).isDir();
#else
        return QFile::link(target, alias);
#endif
    }
    bool prepare() {
        qInfo().noquote() << "retained alias fixture" << root.path();
        return root.isValid() && QDir().mkpath(original) && QDir().mkpath(replacement)
            && writeBytes(original + "/unit.sv", "module original; endmodule\n")
            && writeBytes(replacement + "/unit.sv", "module replacement; endmodule\n") && link(original);
    }
    bool moveAlias() {
        // Both absolute endpoints name immediate children of this fixture.
        return QFileInfo(alias).absolutePath() == root.path()
            && QFileInfo(moved).absolutePath() == root.path()
            && QDir(root.path()).rename("alias", "moved-alias");
    }
    QString source() const { return original + "/unit.sv"; }
    QString viaAlias() const { return alias + "/unit.sv"; }
};
rtledit::PreparedWorkspaceEditTransaction preparePrefix(
    rtledit::WorkspaceEditTransactionCoordinator& coordinator,
    WorkspaceEditDocumentManager& documents, const QString& path, const std::string& prefix) {
    const auto file = path.toUtf8().toStdString();
    const auto before = documents.snapshot(file);
    if (!before) return {};
    auto plan = rtledit::makeWorkspaceEditPlan({}, rtledit::RiskLevel::High, rtledit::PreviewPolicy::Diff,
        {{file, before->version, {{0, 0}, {0, 0}}, "", prefix}});
    auto result = coordinator.prepare(std::move(plan), documents);
    coordinator.confirmPreview(&result);
    return result;
}

class InterleavingDocuments final : public rtledit::WorkspaceDocumentManager {
public:
    WorkspaceEditDocumentManager& real;
    std::function<void(const std::string&)> afterApply, beforeRestore, afterRestore;
    std::string failAfterModification;
    std::vector<std::string> restoreCalls;
    explicit InterleavingDocuments(WorkspaceEditDocumentManager& real) : real(real) {}
    std::optional<rtledit::WorkspaceDocumentSnapshot> snapshot(const std::string& path) const override {
        return real.snapshot(path);
    }
    rtledit::DocumentMutationResult applyTextEdits(const std::string& path, rtledit::DocumentVersion version,
        const std::vector<rtledit::WorkspaceTextEdit>& edits) override {
        auto result = real.applyTextEdits(path, version, edits);
        if (result && afterApply) afterApply(path);
        if (result && path == failAfterModification) result.applied = false;
        return result;
    }
    rtledit::DocumentMutationResult applyPreparedTextEdits(const std::string& path,
        const rtledit::WorkspaceDocumentSnapshot& expected, const std::vector<rtledit::IndexedWorkspaceTextEdit>& edits) override {
        auto result = real.applyPreparedTextEdits(path, expected, edits);
        if (result && afterApply) afterApply(path);
        if (result && path == failAfterModification) result.applied = false;
        return result;
    }
    rtledit::DocumentMutationResult restoreSnapshot(const std::string& path,
        const rtledit::WorkspaceDocumentSnapshot& expected, const rtledit::WorkspaceDocumentSnapshot& target) override {
        restoreCalls.push_back(path);
        if (beforeRestore) beforeRestore(path);
        auto result = real.restoreSnapshot(path, expected, target);
        if (result && afterRestore) afterRestore(path);
        return result;
    }
};

rtledit::PreparedWorkspaceEditTransaction preparePair(rtledit::WorkspaceEditTransactionCoordinator& coordinator,
    rtledit::WorkspaceDocumentManager& documents, const std::string& a, const std::string& b) {
    std::vector<rtledit::WorkspaceTextEdit> edits;
    for (const auto& path : {a, b}) {
        const auto state = documents.snapshot(path);
        if (!state) return {};
        edits.push_back({path, state->version, {{0,0},{0,0}}, "", "// transaction\n"});
    }
    auto result = coordinator.prepare(rtledit::makeWorkspaceEditPlan({}, rtledit::RiskLevel::High,
        rtledit::PreviewPolicy::Diff, edits), documents);
    coordinator.confirmPreview(&result); return result;
}
}

class WorkspaceDocumentContractTest : public QObject {
    Q_OBJECT
private slots:
    void reentrantEditIsNotClaimedForRollback() {
        QTemporaryDir temp; const auto path = temp.filePath("reentrant.sv");
        QVERIFY(writeBytes(path, "module m; endmodule\n"));
        QTabWidget widget; TabManager tabs(&widget);
        tabs.setCrashRecoveryService(std::make_unique<CrashRecoveryService>(temp.filePath("recovery")));
        QVERIFY(tabs.openFileInTab(path));
        auto* document = tabs.getCurrentEditor()->document();
        bool injected = false;
        const auto connection = QObject::connect(document, &QTextDocument::contentsChanged, document, [&] {
            if (injected) return;
            injected = true; QTextCursor cursor(document); cursor.insertText("// external observer\n");
        });
        WorkspaceEditDocumentManager adapter(&tabs);
        rtledit::WorkspaceEditTransactionCoordinator coordinator;
        const auto result = coordinator.apply(preparePrefix(coordinator, adapter, path, "// transaction\n"), adapter);
        QObject::disconnect(connection);
        QVERIFY(injected);
        QVERIFY(!result.succeeded());
        QCOMPARE(result.residualFiles, std::vector<std::string>{path.toUtf8().toStdString()});
        QVERIFY(document->toPlainText().contains("// external observer"));
        QVERIFY(!coordinator.canUndo());
        document->setModified(false);
    }

    void realPanelRetainsRetryAndRetirement() {
        QTemporaryDir temp;
        const auto path = temp.filePath("panel.sv");
        QVERIFY(writeBytes(path, "module target; endmodule\n"));
        QTabWidget widget;
        TabManager tabs(&widget);
        tabs.setCrashRecoveryService(std::make_unique<CrashRecoveryService>(temp.filePath("recovery")));
        QVERIFY(tabs.openFileInTab(path));
        WorkspaceEditDocumentManager documents(&tabs);
        WorkspaceEditTransactionService service;
        SemanticIndex index;
        index.setSnapshot(std::make_shared<SemanticIndexSnapshot>());
        const auto token = index.snapshotToken();
        auto rename = std::make_unique<RtlRenameWorkflow>(
            [path, token](const RtlRenamePlanQuery& query, const rtledit::WorkspaceDocumentManager& docs) {
                const auto file = path.toUtf8().toStdString();
                const auto snapshot = docs.snapshot(file);
                RtlRenameProposal proposal;
                if (!snapshot) return proposal;
                proposal.status = RtlRenamePlanStatus::Ready;
                proposal.dryRun = query.dryRun;
                proposal.workspaceEdit = rtledit::makeWorkspaceEditPlan({}, rtledit::RiskLevel::High,
                    rtledit::PreviewPolicy::Diff, {{file, snapshot->version, {{0, 0}, {0, 0}}, "", "// plan\n"}});
                proposal.workspaceEdit.semanticSnapshot.id = std::to_string(token.revision);
                proposal.workspaceEdit.semanticIndexFilePaths = {file};
                return proposal;
            }, &index, &documents, &service);
        QWidget parent;
        RtlHighRiskEditPanelCoordinator panel(&parent, std::move(rename), nullptr);
        RtlRenamePanelSession session;
        session.baseQuery.dryRun = false;
        session.baseQuery.semanticToken = token;
        session.baseQuery.subjectStableKey.fileName = path;
        session.baseQuery.subjectStableKey.symbolName = "target";
        session.baseQuery.subjectStableKey.declarationKind = SymbolTaxonomy::DeclarationKind::Module;
        session.oldName = "target";
        session.suggestedNewName = "renamed";
        QVERIFY(panel.beginRename(session));
        auto* undo = panel.panel()->findChild<QPushButton*>("rtlHighRiskUndoButton");
        auto* retire = panel.panel()->findChild<QPushButton*>("rtlHighRiskRetireButton");
        auto* preview = panel.panel()->findChild<QPushButton*>("rtlHighRiskPreviewButton");
        auto* confirm = panel.panel()->findChild<QPushButton*>("rtlHighRiskConfirmButton");
        QVERIFY(undo && retire && preview && confirm);
        preview->click();
        QVERIFY2(panel.lastOutcome().canConfirm, qPrintable(panel.lastOutcome().message));
        confirm->click();
        QVERIFY2(panel.lastOutcome().canUndo, qPrintable(panel.lastOutcome().message));
        auto* doc = tabs.sharedDocumentForEditor(tabs.getCurrentEditor());
        doc->setReadOnly(true);
        undo->click();
        QCOMPARE(panel.lastOutcome().failure, RtlHighRiskEditWorkflowFailure::UndoFailed);
        QVERIFY(panel.hasProtectedUndoPosition());
        QVERIFY(undo->isEnabled() && retire->isEnabled() && !preview->isEnabled());
        QVERIFY(!panel.beginRename(session));
        doc->setReadOnly(false);
        undo->click();
        QCOMPARE(panel.lastOutcome().workflowState, RtlHighRiskEditWorkflowState::Undone);
        QVERIFY(!panel.hasProtectedUndoPosition());
        QCOMPARE(doc->textDocument()->toPlainText(), QString("module target; endmodule\n"));

        // A same-buffer external edit invalidates retry but keeps the release action.
        QVERIFY(panel.beginRename(session));
        preview->click(); confirm->click();
        QTextCursor cursor(doc->textDocument()); cursor.insertText("// external\n");
        undo->click();
        QCOMPARE(panel.lastOutcome().failure, RtlHighRiskEditWorkflowFailure::UndoConflict);
        QVERIFY(panel.hasProtectedUndoPosition() && !undo->isEnabled() && retire->isEnabled());
        const auto externalText = doc->textDocument()->toPlainText();
        retire->click();
        QVERIFY(!panel.hasProtectedUndoPosition() && preview->isEnabled());
        QCOMPARE(doc->textDocument()->toPlainText(), externalText);
        QVERIFY(panel.beginRename(session));
        preview->click(); confirm->click();
        const auto oldSession = panel.activeSessionId();
        panel.resetForWorkspaceClose();
        QVERIFY(!panel.hasProtectedUndoPosition());
        doc->textDocument()->setModified(false);
        QVERIFY(tabs.closeAllTabs());
        QVERIFY(tabs.openFileInTab(path));
        QVERIFY(panel.beginRename(session));
        QCOMPARE(panel.undo(oldSession).failure, RtlHighRiskEditWorkflowFailure::InvalidState);
        preview->click(); confirm->click();
        QVERIFY(panel.lastOutcome().canUndo);
        // Another successful shared transaction cannot be undone by this owner.
        const auto file = path.toUtf8().toStdString();
        const auto current = documents.snapshot(file);
        auto plan = rtledit::makeWorkspaceEditPlan({}, rtledit::RiskLevel::High, rtledit::PreviewPolicy::Diff,
            {{file, current->version, {{0, 0}, {0, 0}}, "", "// newer\n"}});
        auto prepared = service.prepare(plan, {}, documents);
        QVERIFY(service.applyConfirmed(prepared, {}, documents).succeeded());
        const auto newest = documents.snapshot(file)->text;
        undo->click();
        QCOMPARE(panel.lastOutcome().failure, RtlHighRiskEditWorkflowFailure::TransactionGenerationConflict);
        QCOMPARE(documents.snapshot(file)->text, newest);
        QVERIFY(!panel.hasProtectedUndoPosition());
        tabs.sharedDocumentForEditor(tabs.getCurrentEditor())->textDocument()->setModified(false);
    }

    void rejectedUnopenedDocumentIsNeverRestored() {
        QTemporaryDir fixture;
        const auto a = fixture.filePath("a.sv"), b = fixture.filePath("b.sv");
        QVERIFY(writeBytes(a, "module a; endmodule\n")); QVERIFY(writeBytes(b, "module b; endmodule\n"));
        QTabWidget widget; TabManager tabs(&widget);
        tabs.setCrashRecoveryService(std::make_unique<CrashRecoveryService>(fixture.filePath("recovery")));
        WorkspaceEditDocumentManager real(&tabs); InterleavingDocuments documents(real);
        rtledit::WorkspaceEditTransactionCoordinator coordinator;
        const auto prepared = preparePair(coordinator, documents, a.toStdString(), b.toStdString());
        QVERIFY(prepared.ready()); QCOMPARE(tabs.editorCount(), 0);
        const QByteArray external("module external_b; endmodule\n");
        documents.afterApply = [&](const std::string& path) { if (path == a.toStdString()) QVERIFY(writeBytes(b, external)); };
        const auto result = coordinator.apply(prepared, documents);
        QCOMPARE(result.status, rtledit::TransactionStatus::ApplyFailed);
        QVERIFY(result.residualFiles.empty());
        QCOMPARE(documents.restoreCalls, std::vector<std::string>{a.toStdString()});
        QCOMPARE(readDocumentFile(b).rawBytes, external);
        QVERIFY(!tabs.getDocumentModel()->editorForFile(b));
        QCOMPARE(real.snapshot(a.toStdString())->text, std::string("module a; endmodule\n"));
        QVERIFY(tabs.openFileInTab(b));
        const auto* bDocument = tabs.sharedDocumentForEditor(tabs.getCurrentEditor());
        QCOMPARE(bDocument->textDocument()->toPlainText(), QString::fromUtf8(external));
        QCOMPARE(bDocument->savedBaselineSha256(), QCryptographicHash::hash(external, QCryptographicHash::Sha256));
        QVERIFY(!bDocument->dirty()); QVERIFY(!coordinator.canUndo());
        tabs.clearCrashRecoveryAfterNormalClose();
    }
    void partialFailureRollsBackOnlyOwnedVersions_data() {
        QTest::addColumn<bool>("externalDuringRollback");
        QTest::newRow("owned-rollback") << false;
        QTest::newRow("external-during-rollback") << true;
    }
    void partialFailureRollsBackOnlyOwnedVersions() {
        QFETCH(bool, externalDuringRollback);
        QTemporaryDir fixture; const auto a = fixture.filePath("a.sv"), b = fixture.filePath("b.sv");
        QVERIFY(writeBytes(a, "module a; endmodule\n")); QVERIFY(writeBytes(b, "module b; endmodule\n"));
        QTabWidget widget; TabManager tabs(&widget);
        tabs.setCrashRecoveryService(std::make_unique<CrashRecoveryService>(fixture.filePath("recovery")));
        WorkspaceEditDocumentManager real(&tabs); InterleavingDocuments documents(real);
        rtledit::WorkspaceEditTransactionCoordinator coordinator;
        const auto prepared = preparePair(coordinator, documents, a.toStdString(), b.toStdString());
        documents.failAfterModification = b.toStdString();
        documents.beforeRestore = [&](const std::string& path) {
            if (externalDuringRollback && path == a.toStdString()) {
                auto* editor = tabs.getDocumentModel()->editorForFile(a);
                QVERIFY(editor); QTextCursor cursor(editor->document()); cursor.insertText("// external edit\n");
            }
        };
        const auto result = coordinator.apply(prepared, documents);
        QCOMPARE(result.status, rtledit::TransactionStatus::ApplyFailed);
        QCOMPARE(documents.restoreCalls, (std::vector<std::string>{b.toStdString(), a.toStdString()}));
        QCOMPARE(real.snapshot(b.toStdString())->text, std::string("module b; endmodule\n"));
        if (externalDuringRollback) {
            QCOMPARE(result.residualFiles, std::vector<std::string>{a.toStdString()});
            QVERIFY(real.snapshot(a.toStdString())->text.find("external edit") != std::string::npos);
        } else {
            QVERIFY(result.residualFiles.empty());
            QCOMPARE(real.snapshot(a.toStdString())->text, std::string("module a; endmodule\n"));
        }
        tabs.clearCrashRecoveryAfterNormalClose();
    }
    void undoRevalidatesEachFileAtWrite() {
        QTemporaryDir fixture; const auto a = fixture.filePath("a.sv"), b = fixture.filePath("b.sv");
        QVERIFY(writeBytes(a, "module a; endmodule\n")); QVERIFY(writeBytes(b, "module b; endmodule\n"));
        QTabWidget widget; TabManager tabs(&widget);
        tabs.setCrashRecoveryService(std::make_unique<CrashRecoveryService>(fixture.filePath("recovery")));
        WorkspaceEditDocumentManager real(&tabs); InterleavingDocuments documents(real);
        rtledit::WorkspaceEditTransactionCoordinator coordinator;
        QVERIFY(coordinator.apply(preparePair(coordinator, documents, a.toStdString(), b.toStdString()), documents).succeeded());
        const auto appliedA = real.snapshot(a.toStdString())->text, appliedB = real.snapshot(b.toStdString())->text;
        bool changed = false;
        const QByteArray external("module external_b; endmodule\n");
        documents.afterRestore = [&](const std::string& path) {
            if (!changed && path == a.toStdString()) { changed = true; QVERIFY(writeBytes(b, external)); }
        };
        const auto result = coordinator.undo(documents);
        QCOMPARE(result.status, rtledit::TransactionStatus::Conflict);
        QCOMPARE(real.snapshot(a.toStdString())->text, appliedA);
        QCOMPARE(real.snapshot(b.toStdString())->text, appliedB);
        QCOMPARE(readDocumentFile(b).rawBytes, external);
        QCOMPARE(documents.restoreCalls, (std::vector<std::string>{a.toStdString(), b.toStdString(), a.toStdString()}));
        QVERIFY(coordinator.canUndo());
        tabs.clearCrashRecoveryAfterNormalClose();
    }
    void restoreRejectsSameTextNewRevisionAndReopen() {
        QTemporaryDir fixture; const auto path = fixture.filePath("a.sv");
        QVERIFY(writeBytes(path, "module a; endmodule\n"));
        QTabWidget widget; TabManager tabs(&widget);
        tabs.setCrashRecoveryService(std::make_unique<CrashRecoveryService>(fixture.filePath("recovery")));
        QVERIFY(tabs.openFileInTab(path)); WorkspaceEditDocumentManager real(&tabs);
        const auto captured = *real.snapshot(path.toStdString());
        QTextCursor cursor(tabs.getCurrentEditor()->document()); cursor.insertText("x"); cursor.deletePreviousChar();
        QCOMPARE(real.snapshot(path.toStdString())->text, captured.text);
        QVERIFY(!real.restoreSnapshot(path.toStdString(), captured, captured));
        tabs.getCurrentEditor()->document()->setModified(false); QVERIFY(tabs.closeAllTabs());
        QVERIFY(tabs.openFileInTab(path));
        QVERIFY(!real.restoreSnapshot(path.toStdString(), captured, captured));
        tabs.clearCrashRecoveryAfterNormalClose();
    }
    void indexedUnicodeBatchUsesLogicalCoordinates() {
        QTemporaryDir fixture; const auto path = fixture.filePath("unicode.sv");
        const QString text = QStringLiteral("// 中文😀\n") + QString(50000, ' ') + QStringLiteral("\nlogic 变量;\nlogic other;\n");
        QVERIFY(writeBytes(path, encodeDocumentText(text, {QStringConverter::Utf8, true, "\r\n"})));
        QTabWidget widget; TabManager tabs(&widget);
        tabs.setCrashRecoveryService(std::make_unique<CrashRecoveryService>(fixture.filePath("recovery")));
        WorkspaceEditDocumentManager real(&tabs);
        const auto before = *real.snapshot(path.toStdString());
        const auto file = path.toStdString();
        rtledit::PatchEngine engine(real);
        const std::vector<rtledit::WorkspaceTextEdit> edits{
            {file, before.version, {{2,6},{2,12}}, "变量", "信号😀"},
            {file, before.version, {{3,6},{3,11}}, "other", "changed"},
            {file, before.version, {{3,0},{3,0}}, "", "// first\n"},
            {file, before.version, {{3,0},{3,0}}, "", "// second\n"}};
        QVERIFY(engine.apply(edits).applied());
        const auto after = real.snapshot(file);
        QVERIFY(after); QVERIFY(QString::fromStdString(after->text).contains("logic 信号😀;\n// first\n// second\nlogic changed;"));
        QVERIFY(real.restoreSnapshot(file, *after, before));
        QCOMPARE(real.snapshot(file)->text, before.text);
        const auto current = *real.snapshot(file);
        const auto splitUtf8 = current.text.substr(3, 1);
        QVERIFY(!engine.apply({{file, current.version, {{0,3},{0,4}}, splitUtf8, "x"}}).applied());
        QCOMPARE(real.snapshot(file)->text, before.text);
        tabs.clearCrashRecoveryAfterNormalClose();
    }
    void configurationDialogRetainsStaleDraft() {
        QTemporaryDir fixture;
        QVERIFY(writeBytes(fixture.filePath("unit.sv"), "module unit; endmodule\n"));
        WorkspaceConfigurationService service;
        auto initial = service.defaultConfiguration(fixture.path());
        QVERIFY(service.save(initial));
        WorkspaceManager manager; manager.setRecentWorkspacePersistenceEnabledForTesting(false);
        QVERIFY(manager.openWorkspace(fixture.path()));
        WorkspaceConfigurationDialog dialog;
        auto draft = manager.workspaceConfiguration();
        draft.topModule = "local_draft";
        dialog.setConfiguration(draft);
        auto external = service.load(fixture.path()); external.defines.insert("EXTERNAL", "1");
        QVERIFY(service.save(external));
        const auto project = WorkspaceConfigurationService::projectFilePath(fixture.path());
        QFile saved(project); QVERIFY(saved.open(QIODevice::ReadOnly)); const auto bytes = saved.readAll(); saved.close();
        QString reason;
        QVERIFY(!manager.setWorkspaceConfiguration(dialog.configuration(), &reason));
        QVERIFY(reason.contains("changed"));
        QCOMPARE(dialog.configuration().topModule, QString("local_draft"));
        QCOMPARE(dialog.configuration().storageRevision, draft.storageRevision);
        QVERIFY(saved.open(QIODevice::ReadOnly)); QCOMPARE(saved.readAll(), bytes); saved.close();
        // Explicit Reload pairs the external fields with their new baseline.
        dialog.setConfiguration(service.load(fixture.path()));
        auto reloaded = dialog.configuration(); reloaded.topModule = "after_reload";
        dialog.setConfiguration(reloaded);
        QVERIFY(manager.setWorkspaceConfiguration(dialog.configuration(), &reason));
        QCOMPARE(service.load(fixture.path()).defines.value("EXTERNAL"), QString("1"));
        QCOMPARE(manager.workspaceConfiguration().storageRevision, service.load(fixture.path()).storageRevision);
    }
    void ordinarySaveRejectsReboundAlias_data() {
        QTest::addColumn<bool>("sameContent"); QTest::addColumn<bool>("otherOpen");
        QTest::newRow("same-unopened") << true << false;
        QTest::newRow("same-open") << true << true;
        QTest::newRow("different-unopened") << false << false;
        QTest::newRow("different-open") << false << true;
    }
    void ordinarySaveRejectsReboundAlias() {
        QFETCH(bool, sameContent); QFETCH(bool, otherOpen);
        AliasFixture fixture; QVERIFY(fixture.prepare());
        if (sameContent) QVERIFY(writeBytes(fixture.replacement + "/unit.sv", "module original; endmodule\n"));
        const auto originalBytes = readDocumentFile(fixture.source()).rawBytes;
        const auto replacementBytes = readDocumentFile(fixture.replacement + "/unit.sv").rawBytes;
        QTabWidget widget; TabManager tabs(&widget);
        tabs.setCrashRecoveryService(std::make_unique<CrashRecoveryService>(fixture.root.filePath("recovery")));
        tabs.setWorkspaceScope({fixture.root.path()}, fixture.root.path());
        QVERIFY(tabs.openFileInTab(fixture.viaAlias()));
        auto* view = tabs.getCurrentEditor(); auto* document = tabs.sharedDocumentForEditor(view);
        QTextCursor cursor(view->document()); cursor.insertText("// owned dirty\n");
        tabs.flushCrashRecovery();
        const auto before = tabs.listCrashRecoveryCandidates(fixture.root.path());
        QCOMPARE(before.candidates.size(), 1);
        const auto recoveryId = before.candidates.first().recoveryId;
        QVERIFY(fixture.moveAlias()); QVERIFY(fixture.link(fixture.replacement));
        if (otherOpen) QVERIFY(tabs.openFileInTab(fixture.viaAlias()));
        QSignalSpy failures(&tabs, &TabManager::fileSaveFailed);
        QVERIFY(!tabs.saveEditorView(view));
        QCOMPARE(failures.size(), 1); QVERIFY(document->dirty());
        QCOMPARE(readDocumentFile(fixture.source()).rawBytes, originalBytes);
        QCOMPARE(readDocumentFile(fixture.replacement + "/unit.sv").rawBytes, replacementBytes);
        tabs.flushCrashRecovery();
        const auto after = tabs.listCrashRecoveryCandidates(fixture.root.path());
        QCOMPARE(after.candidates.size(), 1);
        QCOMPARE(after.candidates.first().recoveryId, recoveryId);
        QCOMPARE(EditorFileIdentity::lookupKey(after.candidates.first().canonicalFileIdentity),
                 EditorFileIdentity::lookupKey(fixture.source()));
        const auto saveAs = fixture.root.filePath("safe-save-as.sv");
        QVERIFY(tabs.saveEditorView(view, true, saveAs));
        QVERIFY(!document->dirty());
        QVERIFY(tabs.listCrashRecoveryCandidates(fixture.root.path()).candidates.isEmpty());
        QCOMPARE(readDocumentFile(saveAs).text, view->toPlainText());
        QCOMPARE(readDocumentFile(fixture.replacement + "/unit.sv").rawBytes, replacementBytes);
        QVERIFY(tabs.closeAllTabs());
    }
    void pathMutationUsesPreparedAliasViews() {
        AliasFixture fixture; QVERIFY(fixture.prepare());
        QTabWidget widget; QWidget auxiliaryHost; TabManager tabs(&widget);
        tabs.setCrashRecoveryService(std::make_unique<CrashRecoveryService>(fixture.root.filePath("recovery")));
        QVERIFY(tabs.openFileInTab(fixture.viaAlias()));
        auto* document = tabs.sharedDocumentForEditor(tabs.getCurrentEditor());
        QVERIFY(tabs.createAuxiliaryView(document->documentId(), fixture.viaAlias(), &auxiliaryHost));
        WorkspacePathMutation prepared;
        QString reason;
        QVERIFY(tabs.prepareWorkspacePathMutation(fixture.original, true, &prepared, nullptr, &reason));
        QCOMPARE(prepared.documents.size(), 1);
        QCOMPARE(prepared.documents.first().views.size(), 2);
        // The rename is bounded to immediate children of the retained fixture.
        QVERIFY(QDir(fixture.root.path()).rename("original", "renamed-original"));
        QVERIFY(!QFileInfo(fixture.viaAlias()).isFile());
        QPointer<SharedDocument> alive(document);
        QVERIFY2(tabs.finalizeWorkspacePathMutation(prepared, &reason), qPrintable(reason));
        QCOMPARE(tabs.editorCount(), 0); QVERIFY(tabs.auxiliaryViews().isEmpty()); QVERIFY(!alive);
    }
    void pathMutationRejectsChangesAfterPreparation() {
        QTemporaryDir root; const auto path = root.filePath("unit.sv");
        QVERIFY(writeBytes(path, "module unit; endmodule\n"));
        QTabWidget widget; TabManager tabs(&widget);
        tabs.setCrashRecoveryService(std::make_unique<CrashRecoveryService>(root.filePath("recovery")));
        QVERIFY(tabs.openFileInTab(path)); WorkspacePathMutation prepared;
        QVERIFY(tabs.prepareWorkspacePathMutation(path, false, &prepared));
        QTextCursor cursor(tabs.getCurrentEditor()->document()); cursor.insertText("// changed\n");
        QVERIFY(!tabs.validateWorkspacePathMutation(prepared));
        QVERIFY(!tabs.finalizeWorkspacePathMutation(prepared));
        QCOMPARE(tabs.editorCount(), 1);
        tabs.getCurrentEditor()->document()->setModified(false);
        QVERIFY(tabs.closeAllTabs());
    }
    void opaqueSharedIdRetainsItsFileLookup() {
        QTemporaryDir fixture;
        const auto path = fixture.filePath("unit.sv");
        QVERIFY(writeBytes(path, "module unit; endmodule\n"));
        SharedDocument document("explicit-owner-id", path, "module unit; endmodule\n");
        MyCodeEditor editor;
        document.attachView(&editor);
        DocumentModel model;
        model.registerEditor(&editor, path);
        QCOMPARE(model.documentMetadataForEditor(&editor).documentId, document.documentId());
        QCOMPARE(model.editorForFile(path), &editor);
        QTextCursor cursor(editor.document()); cursor.insertText("// update\n");
        model.markSaved(&editor);
        QVERIFY(!model.documentMetadataForEditor(&editor).dirty);
        model.unregisterEditor(&editor);
        QVERIFY(!model.editorForFile(path));
    }
    void movedAliasTakeRemovesRegisteredView() {
        AliasFixture fixture; QVERIFY(fixture.prepare());
        MyCodeEditor editor;
        DocumentRegistry registry;
        TrackedDocument tracked; tracked.editor = &editor;
        tracked.snapshot.documentId = fixture.viaAlias();
        tracked.snapshot.fileName = fixture.viaAlias();
        registry.add(&editor, tracked);
        QCOMPARE(registry.editorsForDocumentId(fixture.source()).size(), 1);
        QVERIFY(fixture.moveAlias());
        registry.take(&editor);
        QVERIFY(!registry.contains(&editor));
        QVERIFY(registry.editorsForDocumentId(fixture.source()).isEmpty());
        QVERIFY(!registry.editorForFile(fixture.source()));
    }
    void movedAliasPreservesRepresentativeUntilLastViewLeaves() {
        AliasFixture fixture; QVERIFY(fixture.prepare());
        MyCodeEditor first, second;
        DocumentRegistry registry;
        TrackedDocument a; a.editor = &first;
        a.snapshot.documentId = fixture.viaAlias(); a.snapshot.fileName = fixture.viaAlias();
        auto b = a; b.editor = &second;
        registry.add(&first, a); registry.add(&second, b);
        QVERIFY(fixture.moveAlias());
        registry.take(&second);
        QCOMPARE(registry.editorsForDocumentId(fixture.source()), QList<MyCodeEditor*>({&first}));
        QCOMPARE(registry.editorForFile(fixture.source()), &first);
        QCOMPARE(registry.indexes.editorForDocumentId(fixture.source()), &first);
        auto renamed = a;
        renamed.snapshot.fileName = fixture.root.filePath("saved-as.sv");
        renamed.snapshot.documentId = renamed.snapshot.fileName;
        registry.replace(&first, renamed, a.snapshot);
        QVERIFY(registry.editorsForDocumentId(fixture.source()).isEmpty());
        QVERIFY(!registry.editorForFile(fixture.source()));
        QCOMPARE(registry.editorForFile(renamed.snapshot.fileName), &first);
        registry.take(&first);
        QVERIFY(!registry.editorForFile(renamed.snapshot.fileName));
    }
    void replacingRepresentativeKeepsOtherViewsIndexed() {
        AliasFixture fixture; QVERIFY(fixture.prepare());
        MyCodeEditor first, second;
        DocumentRegistry registry;
        TrackedDocument a; a.editor = &first;
        a.snapshot.documentId = fixture.viaAlias(); a.snapshot.fileName = fixture.viaAlias();
        auto b = a; b.editor = &second;
        registry.add(&first, a); registry.add(&second, b);
        QVERIFY(fixture.moveAlias());
        auto renamed = b;
        renamed.snapshot.documentId = fixture.root.filePath("other.sv");
        renamed.snapshot.fileName = renamed.snapshot.documentId;
        registry.replace(&second, renamed, b.snapshot);
        QCOMPARE(registry.editorsForDocumentId(fixture.source()), QList<MyCodeEditor*>({&first}));
        QCOMPARE(registry.editorForFile(fixture.source()), &first);
        registry.take(&second); registry.take(&first);
        QVERIFY(registry.editorsForDocumentId(fixture.source()).isEmpty());
    }
    void sharedRegistryReleasesOriginalKeyAfterAliasMoves() {
        AliasFixture fixture; QVERIFY(fixture.prepare());
        SharedDocumentRegistry registry;
        MyCodeEditor first, second;
        auto* document = registry.acquire(fixture.viaAlias(), "module original; endmodule\n");
        const auto id = document->documentId();
        document->attachView(&first); document->attachView(&second);
        QVERIFY(fixture.moveAlias());
        QVERIFY(document->detachView(&first));
        QVERIFY(!registry.documentForView(&first));
        QVERIFY(!registry.releaseIfUnused(document));
        QCOMPARE(registry.documentForView(&second), document);
        QVERIFY(document->detachView(&second));
        QPointer<SharedDocument> alive(document);
        QVERIFY(registry.releaseIfUnused(document));
        QVERIFY(!alive);
        QVERIFY(!registry.documentById(id));
        QVERIFY(!registry.documentForFile(fixture.source()));
        QVERIFY(!registry.documentForView(&second));
    }
    void aliasRebindKeepsBoundViewsAndSaveAsOwnership_data() {
        QTest::addColumn<bool>("rebind");
        QTest::newRow("alias-disappeared") << false;
        QTest::newRow("alias-rebound") << true;
    }
    void aliasRebindKeepsBoundViewsAndSaveAsOwnership() {
        QFETCH(bool, rebind);
        AliasFixture fixture; QVERIFY(fixture.prepare());
        QTabWidget widget;
        TabManager tabs(&widget);
        tabs.setCrashRecoveryService(std::make_unique<CrashRecoveryService>(fixture.root.filePath("recovery")));
        QVERIFY(tabs.openFileInTab(fixture.viaAlias()));
        auto* first = tabs.getCurrentEditor();
        auto* original = tabs.sharedDocumentForEditor(first);
        const auto originalId = original->documentId();
        QVERIFY(tabs.duplicateCurrentView());
        auto* second = tabs.getCurrentEditor();
        QVERIFY(fixture.moveAlias());
        MyCodeEditor* rebound = nullptr;
        SharedDocument* replacement = nullptr;
        if (rebind) {
            QVERIFY(fixture.link(fixture.replacement));
            QVERIFY(tabs.openFileInTab(fixture.viaAlias()));
            rebound = tabs.getCurrentEditor();
            replacement = tabs.sharedDocumentForEditor(rebound);
            QVERIFY(replacement != original);
            QVERIFY(replacement->documentId() != originalId);
            QCOMPARE(tabs.getDocumentModel()->openDocuments().size(), 2);
            QTextCursor cursor(rebound->document()); cursor.insertText("// replacement edit\n");
        }
        // A metadata refresh must retain the binding even though its lexical
        // file name now resolves to a different physical target (or none).
        tabs.getDocumentModel()->refreshEditorState(first);
        tabs.getDocumentModel()->refreshEditorState(second);
        QCOMPARE(tabs.getDocumentModel()->documentMetadataForEditor(first).documentId, originalId);
        widget.setCurrentWidget(first);
        QVERIFY(tabs.duplicateCurrentView());
        auto* third = tabs.getCurrentEditor();
        QCOMPARE(tabs.sharedDocumentForEditor(third), original);
        QCOMPARE(tabs.getDocumentModel()->documentMetadataForEditor(third).documentId, originalId);
        QTextCursor cursor(first->document()); cursor.insertText("// original edit\n");
        QVERIFY(tabs.getDocumentModel()->documentMetadataForEditor(second).dirty);
        const auto destination = fixture.root.filePath("saved-as.sv");
        QVERIFY(tabs.saveEditorView(first, true, destination));
        QCOMPARE(tabs.sharedDocumentForEditor(first), original);
        QCOMPARE(tabs.sharedDocumentForEditor(second), original);
        QCOMPARE(original->viewCount(), 3);
        QCOMPARE(tabs.getDocumentModel()->documentMetadataForEditor(first).documentId, original->documentId());
        QCOMPARE(tabs.getDocumentModel()->documentMetadataForEditor(second).documentId, original->documentId());
        QVERIFY(!tabs.getDocumentModel()->editorForFile(fixture.source()));
        QVERIFY(tabs.getDocumentModel()->editorForFile(destination));
        if (rebound) {
            QCOMPARE(tabs.sharedDocumentForEditor(rebound), replacement);
            QCOMPARE(tabs.getDocumentModel()->editorForFile(fixture.viaAlias()), rebound);
            QVERIFY(tabs.getDocumentModel()->documentMetadataForEditor(rebound).dirty);
            QVERIFY(rebound->toPlainText().contains("replacement"));
        }
        QPointer<SharedDocument> previous(original), other(replacement);
        original->textDocument()->setModified(false);
        if (replacement) replacement->textDocument()->setModified(false);
        QVERIFY(tabs.closeAllTabs());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!previous && !other);
        QVERIFY(tabs.getDocumentModel()->openDocuments().isEmpty());
    }
    void workspaceSwitchKeepsSurvivingBufferHistory() {
        QTemporaryDir first, second;
        const auto path = first.filePath("survives.sv");
        QVERIFY(writeBytes(path, "module survives; endmodule\n"));
        QTabWidget widget;
        TabManager tabs(&widget);
        tabs.setCrashRecoveryService(std::make_unique<CrashRecoveryService>(first.filePath("recovery")));
        tabs.setWorkspaceScope({first.path(), second.path()}, first.path());
        QVERIFY(tabs.openFileInTab(path));
        auto* document = tabs.sharedDocumentForEditor(tabs.getCurrentEditor());
        const auto serial = document->instanceSerial();
        WorkspaceEditDocumentManager documents(&tabs);
        rtledit::WorkspaceEditTransactionCoordinator coordinator;
        QVERIFY(coordinator.apply(preparePrefix(coordinator, documents, path, "// transaction\n"), documents).succeeded());
        tabs.setWorkspaceScope({first.path(), second.path()}, second.path());
        QCOMPARE(document->instanceSerial(), serial);
        QVERIFY(coordinator.undo(documents).succeeded());
        QCOMPARE(document->textDocument()->toPlainText(), QString("module survives; endmodule\n"));
        QVERIFY(coordinator.redo(documents).succeeded());
        tabs.setWorkspaceScope({first.path(), second.path()}, first.path());
        QCOMPARE(tabs.sharedDocumentForEditor(tabs.getCurrentEditor()), document);
        QVERIFY(document->textDocument()->toPlainText().startsWith("// transaction\n"));
        tabs.clearCrashRecoveryAfterNormalClose();
    }
    void logicalTextContract_data() {
        QTest::addColumn<int>("encoding");
        QTest::addColumn<bool>("bom");
        QTest::addColumn<QString>("eol");
        QTest::newRow("utf8-lf") << int(QStringConverter::Utf8) << false << QStringLiteral("\n");
        QTest::newRow("utf8-bom-crlf") << int(QStringConverter::Utf8) << true << QStringLiteral("\r\n");
        QTest::newRow("utf16le-crlf") << int(QStringConverter::Utf16LE) << true << QStringLiteral("\r\n");
        QTest::newRow("utf16be-cr") << int(QStringConverter::Utf16BE) << true << QStringLiteral("\r");
        QTest::newRow("utf32le-lf") << int(QStringConverter::Utf32LE) << true << QStringLiteral("\n");
    }
    void logicalTextContract() {
        QFETCH(int, encoding); QFETCH(bool, bom); QFETCH(QString, eol);
        QTemporaryDir temp;
        const auto path = temp.filePath(QStringLiteral("design.sv"));
        const QString text = QStringLiteral("// 中文\nmodule top;\nlogic signal_a;\nendmodule\n");
        const DocumentFileFormat format{QStringConverter::Encoding(encoding), bom, eol};
        const auto bytes = encodeDocumentText(text, format);
        QVERIFY(writeBytes(path, bytes));
        QTabWidget widget;
        TabManager tabs(&widget);
        tabs.setCrashRecoveryService(std::make_unique<CrashRecoveryService>(temp.filePath("recovery")));
        WorkspaceEditDocumentManager documents(&tabs);
        const auto file = path.toUtf8().toStdString();
        const auto unopened = documents.snapshot(file);
        QVERIFY(unopened);
        QCOMPARE(QString::fromStdString(unopened->text), text);
        QCOMPARE(tabs.editorCount(), 0);
        rtledit::WorkspaceEditTransactionCoordinator coordinator;
        const auto first = preparePrefix(coordinator, documents, path, "// first\n");
        QVERIFY(first.ready());
        QCOMPARE(tabs.editorCount(), 0);
        QVERIFY(coordinator.apply(first, documents).succeeded());
        QCOMPARE(tabs.editorCount(), 1);
        auto* doc = tabs.sharedDocumentForEditor(tabs.getCurrentEditor());
        QVERIFY(doc);
        QCOMPARE(doc->savedBaselineSha256(), QCryptographicHash::hash(bytes, QCryptographicHash::Sha256));
        QCOMPARE(doc->fileFormat().encoding, QStringConverter::Encoding(encoding));
        auto revision = doc->textRevision();
        QVERIFY(coordinator.apply(preparePrefix(coordinator, documents, path, "// second\n"), documents).succeeded());
        QVERIFY(doc->textRevision() > revision);
        revision = doc->textRevision();
        for (int i = 0; i < 2; ++i) {
            QVERIFY(coordinator.undo(documents).succeeded());
            QVERIFY(doc->textRevision() > revision);
            revision = doc->textRevision();
        }
        QCOMPARE(doc->textDocument()->toPlainText(), text);
        for (int i = 0; i < 2; ++i) {
            QVERIFY(coordinator.redo(documents).succeeded());
            QVERIFY(doc->textRevision() > revision);
            revision = doc->textRevision();
        }
        const auto after = doc->textDocument()->toPlainText();
        QVERIFY(tabs.saveEditorView(tabs.getCurrentEditor(), false, path));
        QFile disk(path); QVERIFY(disk.open(QIODevice::ReadOnly));
        QCOMPARE(disk.readAll(), encodeDocumentText(after, format));
        // Saving changes the raw disk generation and invalidates old history.
        QCOMPARE(coordinator.undo(documents).status, rtledit::TransactionStatus::Conflict);
        disk.close();
        QVERIFY(coordinator.apply(preparePrefix(coordinator, documents, path, "// unsaved\n"), documents).succeeded());
        QTextCursor cursor(doc->textDocument()); cursor.insertText("// user\n");
        QCOMPARE(coordinator.undo(documents).status, rtledit::TransactionStatus::Conflict);
        doc->textDocument()->setModified(false);
    }
    void openReusesOneReadAndSaveAsUpdatesViews() {
        QTemporaryDir temp;
        const auto path = temp.filePath("original.sv");
        const QByteArray bytes("module original;\r\nendmodule\r\n");
        QVERIFY(writeBytes(path, bytes));
        QTabWidget widget;
        TabManager tabs(&widget);
        tabs.setCrashRecoveryService(std::make_unique<CrashRecoveryService>(temp.filePath("recovery")));
        resetDocumentFileReadMetricsForTest();
        QVERIFY(tabs.openFileInTab(path));
        const auto metrics = documentFileReadMetricsForTest();
        QCOMPARE(metrics.reads, std::uint64_t(1));
        QCOMPARE(metrics.bytes, std::uint64_t(bytes.size()));
        auto* first = tabs.getCurrentEditor();
        auto* doc = tabs.sharedDocumentForEditor(first);
        QVERIFY(tabs.duplicateCurrentView());
        auto* second = tabs.getCurrentEditor();
        QCOMPARE(tabs.sharedDocumentForEditor(second), doc);
        QCOMPARE(doc->viewCount(), 2);
        const auto renamed = temp.filePath("renamed.sv");
        QVERIFY(tabs.saveEditorView(second, true, renamed));
        QCOMPARE(tabs.sharedDocumentForEditor(first), doc);
        QCOMPARE(tabs.sharedDocumentForEditor(second), doc);
        QTextCursor cursor(first->document()); cursor.insertText("// edited\n");
        QCOMPARE(tabs.getDocumentModel()->documentForEditor(first).text,
            tabs.getDocumentModel()->documentForEditor(second).text);
        QCOMPARE(tabs.getDocumentModel()->documentForFile(renamed).text, first->toPlainText());
        QVERIFY(tabs.saveEditorView(second, false, renamed));
        WorkspaceEditDocumentManager documents(&tabs);
        rtledit::WorkspaceEditTransactionCoordinator coordinator;
        QVERIFY(coordinator.apply(preparePrefix(coordinator, documents, renamed, "// change\n"), documents).succeeded());
        QVERIFY(coordinator.undo(documents).succeeded());
        doc->textDocument()->setModified(false);
        QPointer<SharedDocument> previous(doc);
        QVERIFY(tabs.closeAllTabs());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(previous.isNull());
        QVERIFY(tabs.openFileInTab(renamed));
        QCOMPARE(coordinator.redo(documents).status, rtledit::TransactionStatus::Conflict);
    }
};

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    application.setApplicationName("ZeroSlackDocumentContractTest");
    WorkspaceDocumentContractTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "workspace_document_contract_test.moc"
