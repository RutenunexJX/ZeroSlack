#include "documentblockstore.h"
#include "editorblockpreview.h"
#include "mycodeeditor.h"
#include "tabfileio.h"
#include "tabmanager.h"
#include "shareddocument.h"
#include <QtTest>
#include <QtWidgets>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>

namespace {
bool write(const QString& path, const QByteArray& bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath()); QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
QByteArray read(const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); }
QByteArray sha(const QByteArray& bytes) { return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256); }
}
class EditorBlockStoreTest : public QObject {
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> directory;
    std::unique_ptr<MyCodeEditor> editor;
    QString source, error;
    DocumentBlockModel* model() { return editor->blockModel(); }
    DocumentBlockStore* store() { return DocumentBlockStore::get(model()); }
    QString note() { return model()->insert("text", editor->textCursor(), "note"); }
    bool save(const QString& target = {}, bool copy = false, const DocumentBlockStore::SourceWriter& writer = {}) {
        const auto path = target.isEmpty() ? source : target;
        const auto old = DocumentBlockStore::fileRevision(path);
        const auto next = writer ? sha(encodeDocumentText(editor->toPlainText(),{})) : old;
        return store()->save(path, directory->path(), old, next, copy, writer, {}, &error);
    }
    DocumentBlockStore::SourceWriter writer(const QString& path) {
        return [this,path](const auto& publish, QString* issue) {
            return TabFileIo().writeTextFile(nullptr,path,editor->toPlainText(),issue,nullptr,nullptr,{}, {},nullptr,publish);
        };
    }
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void init() {
        directory = std::make_unique<QTemporaryDir>(); QVERIFY(directory->isValid());
        source = directory->filePath("rtl/unit.sv"); QVERIFY(write(source,"alpha\nbeta\n"));
        editor = std::make_unique<MyCodeEditor>(); editor->setPlainText("alpha\nbeta\n"); editor->document()->setModified(false);
        error.clear(); QVERIFY(store()->bind(source,directory->path(),&error));
    }
    void cleanup() { editor.reset(); directory.reset(); }
    void blockOnlyPreservesSource() {
        const auto before = read(source); const auto modified = QFileInfo(source).lastModified(); const auto id = note();
        QVERIFY2(save(),qPrintable(error)); QCOMPARE(read(source),before); QCOMPARE(QFileInfo(source).lastModified(),modified); QVERIFY(!model()->dirty());
        MyCodeEditor reopened; reopened.setPlainText(editor->toPlainText());
        QVERIFY(DocumentBlockStore::get(reopened.blockModel())->bind(source,directory->path(),&error));
        QCOMPARE(reopened.blockModel()->find(id)->text,"note"); QCOMPARE(reopened.blockModel()->documentId(),model()->documentId());
    }
    void combinedCommitAndFailureRollback() {
        const auto id = note(); QVERIFY(save()); const auto originalMetadata = read(store()->storagePath()); const auto originalSource = read(source);
        model()->editText(id,"new note"); editor->insertPlainText("new ");
        QVERIFY(!save({},false,[&](const auto& publish, QString* issue) { if (!publish(issue)) return false; *issue = "simulated source commit failure"; return false; }));
        QCOMPARE(read(source),originalSource); QCOMPARE(read(store()->storagePath()),originalMetadata); QVERIFY(model()->dirty());
        QVERIFY2(save({},false,writer(source)),qPrintable(error)); QCOMPARE(read(source),encodeDocumentText(editor->toPlainText(),{})); QVERIFY(!model()->dirty());
        const auto rows = QJsonDocument::fromJson(read(store()->storagePath())).object()["generations"].toArray(); QCOMPARE(rows.size(),2);
    }
    void sourcePreparationFailureDoesNotPublish() {
        note(); const auto before = read(source);
        QVERIFY(!save({},false,[](const auto&, QString* issue) { *issue = "source preparation denied"; return false; }));
        QCOMPARE(read(source),before); QVERIFY(!QFileInfo::exists(store()->storagePath())); QVERIFY(model()->dirty());
    }
    void associationConflictPreservesBoth() {
        const auto id = note(); QVERIFY(save()); model()->editText(id,"memory");
        const auto external = read(store()->storagePath()) + "\n"; QVERIFY(write(store()->storagePath(),external));
        QVERIFY(!save()); QCOMPARE(read(store()->storagePath()),external); QCOMPARE(model()->find(id)->text,"memory"); QVERIFY(model()->dirty());
    }
    void sourceChangesDuringCommit() {
        note(); editor->insertPlainText("new ");
        QVERIFY(!save({},false,[&](const auto& publish, QString* issue) { if (!write(source,"external\n")) return false; return publish(issue); }));
        QCOMPARE(read(source),QByteArray("external\n")); QVERIFY(model()->dirty()); QVERIFY(!QFileInfo::exists(store()->storagePath()));
    }
    void removedSidecarCannotSilentlyResave() {
        const auto id=note(); QVERIFY(save()); QVERIFY(QFile::remove(store()->storagePath()));
        QVERIFY(!store()->reload(&error)); QVERIFY(store()->blocked()); QVERIFY(model()->find(id));
        model()->editText(id,"retained memory"); QVERIFY(!save()); QVERIFY(!QFileInfo::exists(store()->storagePath()));
        QCOMPARE(model()->find(id)->text,QString("retained memory"));
    }
    void busyAndAssociationFailure() {
        note(); const auto path = store()->storagePath(); QDir().mkpath(QFileInfo(path).absolutePath());
        { QLockFile lock(path+".lock"); QVERIFY(lock.tryLock()); QVERIFY(!save()); }
        QVERIFY(QDir().mkpath(path)); QVERIFY(!save()); QVERIFY(model()->dirty()); QCOMPARE(read(source),QByteArray("alpha\nbeta\n"));
    }
    void corruptAndFuturePreserved_data() { QTest::addColumn<QByteArray>("bytes"); QTest::newRow("corrupt") << QByteArray("{"); QTest::newRow("future") << QByteArray("{\"format\":\"zeroslack.editor-blocks\",\"version\":99,\"generations\":[]}"); }
    void corruptAndFuturePreserved() {
        QFETCH(QByteArray,bytes); QVERIFY(write(store()->storagePath(),bytes)); QVERIFY(!store()->reload(&error)); note(); QVERIFY(!save()); QCOMPARE(read(store()->storagePath()),bytes);
    }
    void interruptedGenerationSelectsActualSource() {
        const auto id = note(); QVERIFY(save()); const auto path = store()->storagePath(); const QJsonValue previous = QJsonDocument::fromJson(read(path)).object()["generations"].toArray()[0];
        auto pending = model()->serialize(); auto items = pending["blocks"].toArray(); auto item = items[0].toObject(); item["text"] = "uncommitted"; items[0]=item; pending["blocks"]=items;
        const QJsonObject envelope{{"format","zeroslack.editor-blocks"},{"version",1},{"generations",QJsonArray{
            QJsonObject{{"sourceSha256",QString::fromLatin1(sha("future source").toHex())},{"metadata",pending}},previous}}};
        QVERIFY(write(path,QJsonDocument(envelope).toJson())); QVERIFY(!store()->reload(&error)); QCOMPARE(model()->find(id)->text,"note"); QVERIFY(store()->blocked());
        QVERIFY(store()->acceptMatchingGeneration(&error)); QVERIFY2(save(),qPrintable(error));
    }
    void changedSourceRetainsMissingAnchors() {
        const auto id = note(); QVERIFY(save()); QVERIFY(write(source,"other\n")); editor->setPlainText("other\n");
        QVERIFY(!store()->reload(&error)); QVERIFY(model()->find(id)->missing); QCOMPARE(model()->find(id)->text,"note"); QVERIFY(!save());
        QVERIFY(store()->attach(store()->storagePath(),&error)); QVERIFY(model()->find(id)->missing); QVERIFY(model()->reattach(id,editor->textCursor())); QVERIFY(save());
    }
    void saveAsCopiesIdentityAndAssets() {
        QImage image(20,10,QImage::Format_RGB32); image.fill(Qt::green); const auto input = directory->filePath("input.png"); QVERIFY(image.save(input));
        const auto asset = store()->importImage(input,&error); QVERIFY(!asset.isEmpty());
        EditorAssociatedBlock block; block.type="image"; block.asset=asset; const auto id=model()->insert(block,editor->textCursor());
        QVERIFY(save()); const auto oldId=model()->documentId(), oldPath=store()->storagePath(); const auto oldBytes=read(oldPath);
        const auto target=directory->filePath("copy.sv"); QVERIFY2(save(target,true,writer(target)),qPrintable(error));
        QVERIFY(model()->documentId()!=oldId); QVERIFY(model()->find(id)); QCOMPARE(read(oldPath),oldBytes); QVERIFY(!store()->assetBytes(asset,&error).isEmpty());
        MyCodeEditor original; original.setPlainText("alpha\nbeta\n"); QVERIFY(DocumentBlockStore::get(original.blockModel())->bind(source,directory->path(),&error)); QCOMPARE(original.blockModel()->documentId(),oldId);
    }
    void renameAndProjectMove() {
        const auto id=note(); QVERIFY(save()); const auto documentId=model()->documentId(),oldPath=store()->storagePath();
        const auto renamed=directory->filePath("rtl/renamed.sv"); QVERIFY(QFile::rename(source,renamed)); QVERIFY2(store()->relocate(renamed,directory->path(),&error),qPrintable(error));
        QCOMPARE(model()->documentId(),documentId); QVERIFY(!QFileInfo::exists(oldPath)); QVERIFY(QFileInfo::exists(store()->storagePath()));
        const auto moved=directory->filePath("project"); QDir().mkpath(moved);
        QVERIFY(QDir().rename(directory->filePath("rtl"),moved+"/rtl")); QVERIFY(QDir().rename(directory->filePath(".zeroslack"),moved+"/.zeroslack"));
        MyCodeEditor reopened; reopened.setPlainText("alpha\nbeta\n"); QVERIFY(DocumentBlockStore::get(reopened.blockModel())->bind(moved+"/rtl/renamed.sv",moved,&error));
        QCOMPARE(reopened.blockModel()->documentId(),documentId); QVERIFY(reopened.blockModel()->find(id));
    }
    void imageDecodeAndLateReply() {
        QImage image(20,10,QImage::Format_RGB32); image.fill(Qt::blue); const auto input=directory->filePath("image.jpg"); QVERIFY(image.save(input));
        EditorAssociatedBlock block; block.type="image"; block.asset=store()->importImage(input,&error); QVERIFY(!block.asset.isEmpty()); const auto id=model()->insert(block,editor->textCursor());
        QObject owner; PinloomHostClient client([](const auto&,const auto&){},&owner); EditorBlockPreview preview(model(),&client,&owner);
        bool decoded=false; preview.request(id,[&](const auto& result) { if (!result.loading) { QVERIFY2(result.status.isEmpty(),qPrintable(result.status)); QCOMPARE(result.image.size(),QSize(20,10)); decoded=true; } });
        QTRY_VERIFY(decoded);
        int late=0; preview.request(id,[&](const auto& result){if(!result.loading)++late;}); QVERIFY(model()->remove(id)); QTest::qWait(30); QCOMPARE(late,0);
    }
    void referenceProviderAndLateReply() {
        QList<PinloomHostClient::RawReplyHandler> replies;
        QObject owner; PinloomHostClient client([&](const auto&,const auto& reply){replies.append(reply);},&owner);
        EditorAssociatedBlock block; block.type="reference"; block.uri="pinloom://entry/test-entry";
        const auto identity=PinloomHostIdentity::fromUri(QUrl(block.uri));
        if (!identity.isValid()) { block.uri="pinloom://entry/test-entry?resource=test-resource"; }
        QVERIFY(PinloomHostIdentity::fromUri(QUrl(block.uri)).isValid());
        const auto id=model()->insert(block,editor->textCursor()); EditorBlockPreview preview(model(),&client,&owner); int completed=0;
        preview.request(id,[&](const auto& result){if(!result.loading)++completed;}); QTRY_VERIFY(!replies.isEmpty());
        QVERIFY(model()->remove(id)); replies.takeFirst()({},"missing reference"); QTest::qWait(10); QCOMPARE(completed,0);
        EditorAssociatedBlock other; other.type="reference"; other.uri="https://example.invalid/object";
        const auto unsupported=model()->insert(other,editor->textCursor());
        preview.request(unsupported,[&](const auto& result){if(!result.loading){QVERIFY(result.status.contains("not supported"));++completed;}}); QCOMPARE(completed,1);
    }
    void pageSaveBlockOnlyAndSaveAs() {
        QTabWidget tabs; TabManager manager(&tabs); manager.setWorkspaceScope({directory->path()},directory->path()); QVERIFY(manager.openFileInTab(source));
        auto* page=manager.getCurrentEditor(); QVERIFY(page); const auto id=page->blockModel()->insert("text",page->textCursor(),"page note");
        QVERIFY(manager.hasUnsavedChanges()); const auto before=read(source); const auto modified=QFileInfo(source).lastModified();
        QVERIFY(manager.saveCurrentTab()); QCOMPARE(read(source),before); QCOMPARE(QFileInfo(source).lastModified(),modified); QVERIFY(!manager.hasUnsavedChanges());
        const auto identity=page->blockModel()->documentId(); const auto target=directory->filePath("page-copy.sv");
        QVERIFY(manager.saveEditorView(page,true,target)); QVERIFY(page->blockModel()->documentId()!=identity); QVERIFY(page->blockModel()->find(id)); QVERIFY(manager.closeAllTabs());
        QVERIFY(manager.openFileInTab(target)); QVERIFY(manager.getCurrentEditor()->blockModel()->find(id));
    }
    void resolvedCodeAndPdfPreview() {
        const auto png=directory->filePath("page.png"); QImage page(90,120,QImage::Format_RGB32); page.fill(Qt::white); QVERIFY(page.save(png));
        QJsonObject entry{{"identity",QJsonObject{{"entryId","preview-entry"}}},{"uri","pinloom://entry/preview-entry"},{"type","code"},{"title","isolated fixture"}};
        QJsonObject response{{"entry",entry},{"content","module preview; endmodule"},{"contentType","text/x-systemverilog"}};
        QObject owner; PinloomHostClient client([&](const auto&,const auto& reply){reply(response,{});},&owner);
        EditorAssociatedBlock block; block.type="reference"; block.uri="pinloom://entry/preview-entry"; const auto id=model()->insert(block,editor->textCursor());
        EditorBlockPreview preview(model(),&client,&owner); EditorBlockPreviewResult result; bool done=false;
        auto receive=[&](const auto& value){if(!value.loading){result=value;done=true;}};
        preview.request(id,receive); QTRY_VERIFY(done); QCOMPARE(result.text,QString("module preview; endmodule")); QVERIFY2(result.status.isEmpty(),qPrintable(result.status));
        response["preview"]=QJsonObject{{"kind","image"},{"state","ready"},{"mimeType","image/png"},{"filePath",png},{"byteSize",int(QFileInfo(png).size())},{"pixelWidth",90},{"pixelHeight",120},{"page",1}};
        entry["type"]="pdf"; response["entry"]=entry; response["content"]=""; done=false;
        QElapsedTimer timer; timer.start(); preview.request(id,receive); QTRY_VERIFY(done); const auto previewNs=timer.nsecsElapsed();
        QVERIFY2(result.status.isEmpty(),qPrintable(result.status)); QCOMPARE(result.image.size(),QSize(90,120));
        qInfo()<<"PREVIEW_PERF provider_png_ns="<<previewNs;
        entry["deleted"]=true; response["entry"]=entry; done=false; preview.request(id,receive); QTRY_VERIFY(done); QVERIFY(result.status.contains("deleted"));
        const auto before=read(source); const auto modified=QFileInfo(source).lastModified(); QVERIFY(save()); QCOMPARE(read(source),before); QCOMPARE(QFileInfo(source).lastModified(),modified);
    }
    void pageCloseDecisions() {
        QTabWidget tabs; TabManager manager(&tabs); manager.setWorkspaceScope({directory->path()},directory->path()); QVERIFY(manager.openFileInTab(source));
        auto* page=manager.getCurrentEditor(); const auto id=page->blockModel()->insert("text",page->textCursor(),"unsaved");
        manager.unsavedDocumentManagerForTesting()->setDecisionProvider([](const auto&,QWidget*) { return UnsavedDocumentBatchDecision::Cancel; });
        QVERIFY(!manager.closeAllTabs()); QVERIFY(manager.getCurrentEditor()->blockModel()->find(id));
        manager.unsavedDocumentManagerForTesting()->setDecisionProvider([](const auto&,QWidget*) { return UnsavedDocumentBatchDecision::DiscardAll; });
        QVERIFY(manager.closeAllTabs()); QVERIFY(manager.openFileInTab(source)); QVERIFY(manager.getCurrentEditor()->blockModel()->items().isEmpty());
        page=manager.getCurrentEditor(); const auto saved=page->blockModel()->insert("text",page->textCursor(),"saved");
        manager.unsavedDocumentManagerForTesting()->setDecisionProvider([](const auto&,QWidget*) { return UnsavedDocumentBatchDecision::SaveAll; });
        QVERIFY(manager.closeAllTabs()); QVERIFY(manager.openFileInTab(source)); QVERIFY(manager.getCurrentEditor()->blockModel()->find(saved));
    }
    void actualRenameService() {
        const auto id=note(); QVERIFY(save()); const auto identity=model()->documentId(); const auto old=store()->storagePath();
        WorkspaceFileOperationService service; const auto plan=service.planRename(directory->path(),directory->filePath("rtl"),"logic"); QVERIFY2(plan.valid,qPrintable(plan.failureReason));
        const auto result=service.apply(plan); QVERIFY2(result.succeeded,qPrintable(result.failureReason)); QVERIFY(!QFileInfo::exists(old));
        MyCodeEditor reopened; reopened.setPlainText("alpha\nbeta\n"); QVERIFY(DocumentBlockStore::get(reopened.blockModel())->bind(directory->filePath("logic/unit.sv"),directory->path(),&error));
        QCOMPARE(reopened.blockModel()->documentId(),identity); QVERIFY(reopened.blockModel()->find(id));
    }
    void renameConflictStopsBeforeSource() {
        note(); QVERIFY(save()); const auto target=directory->filePath("rtl/other.sv"); const auto other=DocumentBlockStore::pathFor(target,directory->path()); QVERIFY(write(other,"external metadata"));
        WorkspaceFileOperationService service; const auto plan=service.planRename(directory->path(),source,"other.sv"); QVERIFY(plan.valid);
        const auto result=service.apply(plan); QVERIFY(!result.succeeded); QVERIFY(QFileInfo::exists(source)); QVERIFY(!QFileInfo::exists(target)); QCOMPARE(read(other),QByteArray("external metadata"));
    }
    void untitledRecoveryWithImage() {
        const auto recovery=directory->filePath("recovery"); QTabWidget tabs; TabManager manager(&tabs);
        manager.setCrashRecoveryService(std::make_unique<CrashRecoveryService>(recovery)); manager.setWorkspaceScope({directory->path()},directory->path()); manager.createNewTab();
        auto* page=manager.getCurrentEditor(); QVERIFY(page); auto* data=page->blockModel();
        const auto noteId=data->insert("text",page->textCursor(),"untitled note");
        QImage image(16,12,QImage::Format_RGB32); image.fill(Qt::yellow); const auto input=directory->filePath("recovered.png"); QVERIFY(image.save(input));
        EditorAssociatedBlock block; block.type="image"; block.asset=DocumentBlockStore::get(data)->importImage(input,&error); QVERIFY(!block.asset.isEmpty()); const auto imageId=data->insert(block,page->textCursor());
        manager.checkpointCrashRecovery(); manager.flushCrashRecovery();
        const auto candidates=manager.listCrashRecoveryCandidates(recovery+"/standalone"); QVERIFY2(candidates.succeeded(),qPrintable(candidates.reason)); QCOMPARE(candidates.candidates.size(),1);
        const auto comparison=manager.compareCrashRecoveryCandidate(candidates.candidates[0].recoveryId,recovery+"/standalone"); QVERIFY(comparison.succeeded()); QCOMPARE(comparison.recoveredText,QString());
        manager.setCrashRecoveryService(std::make_unique<CrashRecoveryService>(directory->filePath("continued")));
        QTabWidget restoredTabs; TabManager restored(&restoredTabs); restored.setCrashRecoveryService(std::make_unique<CrashRecoveryService>(recovery)); restored.setWorkspaceScope({directory->path()},directory->path());
        const auto applied=restored.applyCrashRecoveryCandidate(comparison.candidate); QVERIFY2(applied.succeeded(),qPrintable(applied.reason));
        auto* recovered=restored.getCurrentEditor(); QVERIFY(recovered); QVERIFY(recovered->blockModel()->find(noteId)); QVERIFY(recovered->blockModel()->find(imageId)); QCOMPARE(recovered->toPlainText(),QString()); QVERIFY(restored.hasUnsavedChanges());
        const auto target=directory->filePath("recovered.sv"); QVERIFY(restored.saveEditorView(recovered,true,target)); QCOMPARE(read(target),QByteArray()); QVERIFY(!restored.hasUnsavedChanges());
        QVERIFY(!DocumentBlockStore::get(recovered->blockModel())->assetBytes(block.asset,&error).isEmpty());
        manager.clearCrashRecoveryAfterNormalClose();
    }
    void namedBlockOnlyRecoveryAndExternalReload() {
        const auto recovery=directory->filePath("recovery"); QTabWidget tabs; TabManager manager(&tabs);
        manager.setCrashRecoveryService(std::make_unique<CrashRecoveryService>(recovery)); manager.setWorkspaceScope({directory->path()},directory->path()); QVERIFY(manager.openFileInTab(source));
        auto* page=manager.getCurrentEditor(); const auto id=page->blockModel()->insert("text",page->textCursor(),"disk note"); QVERIFY(manager.saveCurrentTab());
        page->blockModel()->editText(id,"unsaved block only"); manager.checkpointCrashRecovery(); manager.flushCrashRecovery();
        const auto candidates=manager.listCrashRecoveryCandidates(directory->path()); QCOMPARE(candidates.candidates.size(),1);
        const auto comparison=manager.compareCrashRecoveryCandidate(candidates.candidates[0].recoveryId,directory->path()); QVERIFY(comparison.succeeded()); QCOMPARE(comparison.recoveredText,QString("alpha\nbeta\n"));
        auto* shared=manager.sharedDocumentForEditor(page); QVERIFY(shared->acceptExternalText("alpha\nbeta\n")); QCOMPARE(page->blockModel()->find(id)->text,"disk note");
        QVERIFY(!page->blockModel()->canUndo()); QVERIFY(!manager.hasUnsavedChanges());
    }
};
QTEST_MAIN(EditorBlockStoreTest)
#include "editor_block_store_test.moc"
