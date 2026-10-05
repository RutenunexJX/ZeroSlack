#include "tabmanager.h"
#include "workspaceeditdocumentmanager.h"
#include "documentregistry.h"
#include <zeroslack/documents/documentfileread.h>
#include <rtledit/workspace_edit_transaction.h>
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
}

class WorkspaceDocumentContractTest : public QObject {
    Q_OBJECT
private slots:
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
