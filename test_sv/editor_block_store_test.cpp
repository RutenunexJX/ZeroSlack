#include "documentblockstore.h"
#include "documentreferenceservice.h"
#include "documentblocktransfer.h"
#include "blockdropadapter.h"
#include "waveblockcontent.h"
#include "editorblockpreview.h"
#include "editorblockcontent.h"
#include "uicontrols.h"
#include "blocknotestyle.h"
#include "mycodeeditor.h"
#include "tabfileio.h"
#include "tabmanager.h"
#include "shareddocument.h"
#include "pinloomreferencepicker.h"
#include "pinloomcontextview.h"
#include "associatedblockpage.h"
#include "associatedblockdragcontroller.h"
#include "tsdocument.h"
#include <QtTest>
#include <QtWidgets>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QScopeGuard>
#include <QDesktopServices>
#ifdef Q_OS_WIN
#include <windows.h>
#include <atomic>
#include <chrono>
#include <thread>
#endif

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
    QUrl originalUrl;
    Q_INVOKABLE void captureOriginalUrl(const QUrl& url) { originalUrl=url; }
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
    void semanticReferencesKeepBindingsAndIndependentRelations() {
        const QString code="module top;\nassign ready = valid;\nendmodule\n";
        editor->setPlainText(code);QVERIFY(write(source,code.toUtf8()));
        TSDocument syntax;syntax.setText(code);
        const auto binding=PinloomSourceSelection::fromSyntaxAnchor(directory->path(),source,code,syntax.bindableCodeAnchorAt(code.indexOf("assign")));
        QVERIFY(binding.isValid());
        const DocumentReferenceService::Selection chosen{"https://example.invalid/specification","Specification",true};
        const auto before=model()->serialize();
        QVERIFY(DocumentReferenceService::insertSemantic(model(),binding,code,{},&error).isEmpty());QCOMPARE(model()->serialize(),before);
        const auto first=DocumentReferenceService::insertSemantic(model(),binding,code,chosen,&error);QVERIFY2(!first.isEmpty(),qPrintable(error));
        const auto second=DocumentReferenceService::insertSemantic(model(),binding,code,chosen,&error);QVERIFY(!second.isEmpty());QVERIFY(first!=second);
        QCOMPARE(model()->items().size(),2);QCOMPARE(model()->find(first)->reference.value("source").toMap(),binding.toVariantMap());
        DocumentReferenceService::refresh(model(),source,code,&syntax,nullptr);QVERIFY(!model()->presentation(first).missing);
        const auto frozen=model()->serialize();const auto revision=model()->revision();
        DocumentReferenceService::refresh(model(),source,code,&syntax,nullptr);QCOMPARE(model()->revision(),revision);QCOMPARE(model()->serialize(),frozen);
        QTextCursor edit(editor->document());edit.setPosition(0);edit.insertText("// prefix\n");
        const auto moved=editor->toPlainText();syntax.setText(moved);
        DocumentReferenceService::refresh(model(),source,moved,&syntax,nullptr);
        QVERIFY(!model()->presentation(first).missing);QCOMPARE(model()->presentation(first).start,binding.startPosition+10);
        QVERIFY(DocumentReferenceService::setDisplay(model(),first,"embedded"));QCOMPARE(model()->find(first)->uri,chosen.uri);
        QCOMPARE(model()->find(first)->reference.value("source").toMap(),binding.toVariantMap());
        QVERIFY(model()->undoAssociation(false));QVERIFY(model()->find(first)->compactReference());
        QTextDocument target;target.setPlainText("destination\n");auto* destination=DocumentBlockModel::get(&target);
        const auto copied=model()->transferTo(destination,first,QTextCursor(&target),true);QVERIFY(!copied.isEmpty());QVERIFY(copied!=first);
        QCOMPARE(destination->find(copied)->reference.value("sourceKind").toString(),QString("position"));
        QCOMPARE(destination->find(copied)->uri,chosen.uri);QCOMPARE(model()->items().size(),2);
        editor->setPlainText("module top;\nendmodule\n");syntax.setText(editor->toPlainText());
        DocumentReferenceService::refresh(model(),source,editor->toPlainText(),&syntax,nullptr);
        QVERIFY(model()->presentation(first).missing);QVERIFY(!model()->presentation(first).referenceStatus.isEmpty());
        QVERIFY(model()->find(first));QCOMPARE(model()->find(first)->uri,chosen.uri);
    }
    void legacyReferenceMappingRoundTripsAndMovesAtomically() {
        const QString code="module top;\nassign ready = valid;\nendmodule\n";
        editor->setPlainText(code);QVERIFY(write(source,code.toUtf8()));
        TSDocument syntax;syntax.setText(code);
        const auto binding=PinloomSourceSelection::fromSyntaxAnchor(directory->path(),source,code,syntax.bindableCodeAnchorAt(code.indexOf("assign")));
        PinloomCodeLinkStore legacy;legacy.setWorkspaceRoot(directory->path());
        const QUrl uri("pinloom://entry/reference-one");
        QVERIFY2(legacy.addLink(binding,uri,"Reference one",PinloomHostIdentity::fromUri(uri).toVariantMap(),&error),qPrintable(error));
        const auto bytes=read(legacy.storagePath());const auto anchors=legacy.linksForDocument(source,code,&syntax);QCOMPARE(anchors.size(),1);
        const auto linkId=anchors.first().anchor.links.first().id;
        const auto id=DocumentReferenceService::convertLegacy(model(),anchors.first(),linkId,"embedded",&error);QVERIFY2(!id.isEmpty(),qPrintable(error));
        QVERIFY(model()->find(id)->uri.isEmpty());QVERIFY(model()->find(id)->caption.isEmpty());
        QVERIFY(DocumentReferenceService::hasLegacy(model(),anchors.first().anchor.id,linkId));
        DocumentReferenceService::refresh(model(),source,code,&syntax,&legacy);QCOMPARE(model()->presentation(id).uri,uri.toString());
        const auto display=*model()->find(id);
        QVERIFY(DocumentReferenceService::setDisplay(model(),id,"compact"));QVERIFY(DocumentReferenceService::setDisplay(model(),id,"embedded"));
        QCOMPARE(model()->items().size(),1);QCOMPARE(model()->find(id)->height,display.height);QCOMPARE(model()->find(id)->appearance,display.appearance);
        QVERIFY2(save(),qPrintable(error));const auto saved=model()->serialize();
        QTextDocument restored;restored.setPlainText(code);auto* again=DocumentBlockModel::get(&restored);
        QVERIFY(again->restore(saved,&error));DocumentReferenceService::refresh(again,source,code,&syntax,&legacy);
        QCOMPARE(again->presentation(id).uri,uri.toString());QCOMPARE(read(legacy.storagePath()),bytes);
        const auto stamp=model()->revision();QVERIFY(model()->remove(id));QVERIFY(model()->find(id)->removedReference());
        QVERIFY(model()->undoAssociation(false));QVERIFY(!model()->find(id)->removedReference());QVERIFY(model()->revision()>stamp);
        QTextDocument target;target.setPlainText("destination\n");auto* destination=DocumentBlockModel::get(&target);
        QCOMPARE(model()->transferTo(destination,id,QTextCursor(&target),false),id);
        QVERIFY(!model()->find(id));QCOMPARE(model()->items().size(),1);QVERIFY(model()->items().first().removedReference());
        QCOMPARE(destination->find(id)->uri,uri.toString());QCOMPARE(destination->find(id)->reference.value("sourceKind").toString(),QString("position"));
        QVERIFY(destination->undoAssociation(false));QVERIFY(model()->find(id));QVERIFY(!destination->find(id));
        QVERIFY(model()->undoAssociation(true));QVERIFY(destination->find(id));QVERIFY(!model()->find(id));QCOMPARE(read(legacy.storagePath()),bytes);
    }
    void referenceReplyCannotRebindIdentityOrDisplay() {
        QList<PinloomHostClient::RawReplyHandler> replies;QObject owner;
        PinloomHostClient client([&](const auto&,const auto& reply){replies.append(reply);},&owner);
        EditorAssociatedBlock block;block.type="reference";block.uri="pinloom://entry/reference-one";
        const auto id=model()->insert(block,editor->textCursor());
        QJsonObject entry{{"identity",QJsonObject{{"entryId","different"}}},{"uri","pinloom://entry/different"},{"type","note"},{"title","Wrong object"}};
        QJsonObject response{{"entry",entry},{"content","Wrong content"}};
        EditorBlockPreview preview(model(),&client,&owner);EditorBlockPreviewResult result;
        preview.request(id,[&](const auto& value){result=value;});QTRY_COMPARE(replies.size(),1);
        replies.takeFirst()(response,{});QTRY_VERIFY(!result.loading);QVERIFY(result.status.contains("different target"));QVERIFY(result.text.isEmpty());
        bool done=false;EditorBlockPreview::inspectOriginal(model(),&client,id,&owner,[&](const auto&){done=true;});QTRY_COMPARE(replies.size(),1);
        QVERIFY(DocumentReferenceService::setDisplay(model(),id,"compact"));
        replies.takeFirst()(response,{});QCoreApplication::processEvents();QVERIFY(!done);
    }
    void wavePresentationSurvivesDetachCrossFileMoveAndPairedUndo() {
        QTabWidget tabs;tabs.resize(980,700);tabs.show();TabManager manager(&tabs);
        QVERIFY(manager.openFileInTab(source));auto* left=manager.getCurrentEditor();auto* owner=manager.sharedDocumentForEditor(left);
        const auto id=left->blockModel()->insert("wave",left->textCursor(),WaveBlockContent::initialProject());QVERIFY(!id.isEmpty());
        QWidget* native=nullptr;QTRY_VERIFY((native=left->findChild<QWidget*>("WaveEditor")));const QPointer<QWidget> original=native;
        auto* page=manager.detachAssociatedBlock(owner,id);QVERIFY(page);QCOMPARE(page->findChild<QWidget*>("WaveEditor"),original.data());
        const auto second=directory->filePath("second.sv");QVERIFY(write(second,"module second; endmodule\n"));QVERIFY(manager.openFileInTab(second));
        auto* right=manager.getCurrentEditor();auto* receiving=manager.sharedDocumentForEditor(right);
        const auto captured=DocumentBlockTransfer::capture(owner,id);QString issue;
        QCOMPARE(DocumentBlockTransfer::apply(captured,receiving,right->textCursor(),DocumentBlockTransfer::Mode::Move,{},&issue),id);
        QTRY_VERIFY(right->findChild<QWidget*>("WaveEditor")==original.data());QVERIFY(original);QVERIFY(!left->blockModel()->find(id));
        QVERIFY(right->blockModel()->undoAssociation(false));QTRY_COMPARE(manager.associatedBlockPages().size(),1);
        page=manager.associatedBlockPages().first();QCOMPARE(page->ownerDocument(),owner);QCOMPARE(page->findChild<QWidget*>("WaveEditor"),original.data());
        QVERIFY(left->blockModel()->undoAssociation(true));QTRY_VERIFY(right->findChild<QWidget*>("WaveEditor")==original.data());QVERIFY(original);
        QCOMPARE(read(source),QByteArray("alpha\nbeta\n"));QCOMPARE(read(second),QByteArray("module second; endmodule\n"));
    }
    void wholeBlockClipboardAndToolsLeaveTextEditingNative() {
        QTabWidget tabs; tabs.resize(950,680); tabs.show(); tabs.raise(); tabs.activateWindow();
        TabManager manager(&tabs); QVERIFY(manager.openFileInTab(source));
        auto* code=manager.getCurrentEditor(); auto* blocks=code->blockModel();
        const auto sourceText=code->toPlainText(); const auto id=blocks->insert("text",code->textCursor(),"Native text");
        code->selectAssociatedBlock(id); QFrame* frame=nullptr;
        QTRY_VERIFY((frame=code->findChild<QFrame*>("editorBlockShell")) && frame->isVisible());
        auto* content=static_cast<EditorBlockContent*>(frame->findChild<QWidget*>("editorBlockContent"));
        auto* text=frame->findChild<QTextEdit*>("editorBlockText"); auto* tools=frame->findChild<QWidget*>("editorBlockTools");
        QVERIFY(content && text && tools); QVERIFY(!content->isEditing());
        const auto sourceCursor=code->textCursor();
        QTest::mouseClick(frame,Qt::LeftButton,Qt::NoModifier,QPoint(50,50)); QVERIFY(frame->hasFocus());
        QTest::mouseDClick(frame,Qt::LeftButton,Qt::NoModifier,QPoint(50,50)); QVERIFY(frame->hasFocus());
        QCOMPARE(code->textCursor().position(),sourceCursor.position());
        QCOMPARE(code->textCursor().anchor(),sourceCursor.anchor());
        QTest::keyClick(frame,Qt::Key_Return); QVERIFY(!content->isEditing());
        auto* edit=frame->findChild<QToolButton*>("blockEditToggle");QVERIFY(edit);
        QTest::mouseClick(edit,Qt::LeftButton); QVERIFY(content->isEditing()); QTRY_VERIFY(text->hasFocus());
        QCursor::setPos(frame->mapToGlobal(QPoint(45,frame->height()-10)));
        EditorBlockContent::refreshTools(frame,tools); QVERIFY(edit->isVisible()); QVERIFY(edit->isChecked());
        text->selectAll(); QTest::keyClick(text,Qt::Key_C,Qt::ControlModifier);
        QCOMPARE(QApplication::clipboard()->text(),QString("Native text"));
        QVERIFY(!QApplication::clipboard()->mimeData()->hasFormat("application/x-zeroslack-block+json"));
        QTest::keyClick(text,Qt::Key_X,Qt::ControlModifier); QVERIFY(blocks->find(id)); QCOMPARE(text->toPlainText(),QString());
        QTest::keyClick(text,Qt::Key_V,Qt::ControlModifier); QCOMPARE(text->toPlainText(),QString("Native text"));
        QTest::keyClick(text,Qt::Key_Escape); QVERIFY(content->isEditing());
        QTest::mouseClick(edit,Qt::LeftButton); QVERIFY(!content->isEditing());
        auto* handle=code->findChild<QToolButton*>("editorBlockDragHandle");QVERIFY(handle);
        QTest::mouseClick(handle,Qt::LeftButton);QTRY_VERIFY(frame->hasFocus());
        QTest::keyClick(frame,Qt::Key_Tab); QCOMPARE(QApplication::focusWidget(),handle);
        EditorBlockContent::refreshTools(frame,tools); QVERIFY(tools->isVisible());
        QTest::keyClick(QApplication::focusWidget(),Qt::Key_Escape); QCOMPARE(QApplication::focusWidget(),handle);
        QTest::mouseClick(handle,Qt::LeftButton);QTRY_VERIFY(frame->hasFocus());
        QTest::keyClick(frame,Qt::Key_C,Qt::ControlModifier);
        QVERIFY(QApplication::clipboard()->mimeData()->hasFormat("application/x-zeroslack-block+json"));
        auto* gutter=code->findChild<QWidget*>("editorLineNumberGutter"); QVERIFY(gutter);
        QTest::mouseClick(gutter,Qt::LeftButton,Qt::NoModifier,QPoint(5,5)); QTRY_VERIFY(code->hasFocus());
        QTest::keyClick(code,Qt::Key_V,Qt::ControlModifier); QCOMPARE(blocks->items().size(),2);
        QVERIFY(blocks->items()[0].id!=blocks->items()[1].id); QCOMPARE(code->toPlainText(),sourceText);
    }
    void namedAppearanceSubmenusRemainOperable() {
        std::unique_ptr<QTabWidget> owned(UiControls::editorTabWidget()); auto& tabs=*owned;
        tabs.resize(950,680); tabs.setWindowFlag(Qt::WindowStaysOnTopHint); tabs.show(); tabs.raise(); tabs.activateWindow();
        TabManager manager(&tabs); QVERIFY(manager.openFileInTab(source));
        auto* code=manager.getCurrentEditor(); auto* blocks=code->blockModel();
        const auto id=blocks->insert("text",code->textCursor(),"Appearance sample"); code->selectAssociatedBlock(id);
        QFrame* frame=nullptr; QTRY_VERIFY((frame=code->findChild<QFrame*>("editorBlockShell")) && frame->isVisible());
        QToolButton* button=nullptr;
        for(auto* candidate:frame->findChildren<QToolButton*>()) if(candidate->accessibleName()=="Block actions") button=candidate;
        QVERIFY(button); auto* menu=button->menu(); QVERIFY(menu);
        const auto named=[](QMenu* parent,const QString& text) -> QAction* {
            for(auto* action:parent->actions()) if(action->text()==text) return action;
            return nullptr;
        };
        auto* appearanceAction=named(menu,"Appearance"); QVERIFY(appearanceAction && appearanceAction->menu());
        auto* appearance=appearanceAction->menu();
        for(const auto& choice:QList<QPair<QString,QString>>{{"Tone","Sage"},{"Font","Engineering Mono"}}) {
            QTest::keyClick(frame,Qt::Key_Tab); QVERIFY(button->isVisible());
            menu->popup(button->mapToGlobal(QPoint(0,button->height()))); QTRY_VERIFY(menu->isVisible());
            QTest::mouseClick(menu,Qt::LeftButton,Qt::NoModifier,menu->actionGeometry(appearanceAction).center());
            QTRY_VERIFY(appearance->isVisible());
            auto* submenuAction=named(appearance,choice.first); QVERIFY(submenuAction && submenuAction->menu());
            auto* submenu=submenuAction->menu(); QVERIFY(!submenu->actions().isEmpty());
            QTest::mouseClick(appearance,Qt::LeftButton,Qt::NoModifier,appearance->actionGeometry(submenuAction).center());
            QTRY_VERIFY(submenu->isVisible());
            auto* action=named(submenu,choice.second); QVERIFY(action && action->isEnabled());
            QTest::mouseClick(submenu,Qt::LeftButton,Qt::NoModifier,submenu->actionGeometry(action).center());
            QTRY_VERIFY(!menu->isVisible());
            QCOMPARE(blocks->find(id)->appearance.value(choice.first=="Tone" ? "tone" : "fontSet").toString(),choice.second);
            code->selectAssociatedBlock(id); QTRY_VERIFY(frame->hasFocus());
        }
        QCOMPARE(blocks->find(id)->appearance.value("preset").toString(),QString("Paper"));
        named(appearance,"Reset to type default")->trigger();
        QCOMPARE(blocks->find(id)->appearance,BlockNoteStyle::defaults("text"));
    }
    void floatingPreviewOnlyHidesTheSourceAndRestoresOnFailure() {
        std::unique_ptr<QTabWidget> owned(UiControls::editorTabWidget()); auto& tabs=*owned;
        tabs.resize(850,650); tabs.show(); TabManager manager(&tabs); QVERIFY(manager.openFileInTab(source));
        auto* code=manager.getCurrentEditor(); auto* document=manager.sharedDocumentForEditor(code); auto* blocks=code->blockModel();
        const auto id=blocks->insert("text",code->textCursor(),"Floating source");
        QPointer<AssociatedBlockPage> page=manager.detachAssociatedBlock(document,id); QVERIFY(page);
        QCoreApplication::processEvents();
        const auto appearance=BlockNoteStyle::instance().appearance(blocks->find(id)->appearance);
        const auto painted=page->grab().toImage(); const auto ratio=painted.devicePixelRatio();
        QCOMPARE(painted.pixelColor(qRound(6*ratio),qRound(page->height()/2*ratio)),appearance.background);
        auto* floating=manager.editorSplitController()->floatPage(page,tabs.mapToGlobal(QPoint(100,100))); QVERIFY(floating);
        auto* window=floating->window(); QTRY_VERIFY(window->isVisible()); const auto geometry=window->geometry();
        tabs.setCurrentWidget(code); auto* gutter=code->findChild<QWidget*>("editorLineNumberGutter"); QVERIFY(gutter);
        auto* drag=manager.findChild<AssociatedBlockDragController*>(); QVERIFY(drag);
        auto mime=std::unique_ptr<QMimeData>(drag->createMimeData(document,id)); const auto before=blocks->serialize();
        QDragEnterEvent enter(QPoint(5,5),Qt::MoveAction,mime.get(),Qt::LeftButton,Qt::NoModifier); QApplication::sendEvent(gutter,&enter);
        QVERIFY(enter.isAccepted()); QVERIFY(!window->isVisible()); QCOMPARE(blocks->serialize(),before);
        QDragLeaveEvent leave; QApplication::sendEvent(gutter,&leave); QVERIFY(window->isVisible()); QCOMPARE(window->geometry(),geometry);
        QDragEnterEvent second(QPoint(5,5),Qt::MoveAction,mime.get(),Qt::LeftButton,Qt::NoModifier); QApplication::sendEvent(gutter,&second);
        QVERIFY(second.isAccepted()); document->setReadOnly(true);
        QDropEvent rejected(QPointF(5,5),Qt::MoveAction,mime.get(),Qt::LeftButton,Qt::NoModifier); QApplication::sendEvent(gutter,&rejected);
        QVERIFY(!rejected.isAccepted()); QVERIFY(window->isVisible()); QCOMPARE(window->geometry(),geometry); QCOMPARE(blocks->serialize(),before);
        document->setReadOnly(false); mime.reset(drag->createMimeData(document,id));
        QDragEnterEvent third(QPoint(5,5),Qt::MoveAction,mime.get(),Qt::LeftButton,Qt::NoModifier); QApplication::sendEvent(gutter,&third);
        QVERIFY(third.isAccepted());
        QDropEvent committed(QPointF(5,5),Qt::MoveAction,mime.get(),Qt::LeftButton,Qt::NoModifier); QApplication::sendEvent(gutter,&committed);
        QVERIFY(committed.isAccepted()); QVERIFY(!blocks->find(id)->detached); QTRY_VERIFY(manager.associatedBlockPages().isEmpty());
    }
    void crossFileAssetsSaveAsReopenAndClosedPeerGuard() {
        QTabWidget tabs; TabManager manager(&tabs); manager.setWorkspaceScope({directory->path()},directory->path());
        QVERIFY(manager.openFileInTab(source)); auto* left=manager.getCurrentEditor(); auto* origin=manager.sharedDocumentForEditor(left);
        auto* a=left->blockModel(); auto* storage=DocumentBlockStore::get(a); const QByteArray bytes("asset\0payload",13);
        EditorAssociatedBlock block; block.type="attachment"; block.asset=storage->importBytes(bytes,"dat",&error); QVERIFY(!block.asset.isEmpty());
        const auto assetId=a->insert(block,left->textCursor()); const auto waveId=a->insert("wave",left->textCursor(),WaveBlockContent::initialProject());
        QVERIFY(manager.saveEditorView(left)); manager.createNewTab(); auto* right=manager.getCurrentEditor(); auto* target=manager.sharedDocumentForEditor(right);
        auto* b=right->blockModel(); auto captured=DocumentBlockTransfer::capture(origin,assetId); const auto before=a->serialize();
        QVERIFY(write(source,"externally changed\n"));
        QVERIFY(DocumentBlockTransfer::apply(captured,target,right->textCursor(),DocumentBlockTransfer::Mode::Move,{},&error).isEmpty());
        QVERIFY(!error.isEmpty()); QCOMPARE(a->serialize(),before); QVERIFY(b->items().isEmpty()); QVERIFY(write(source,"alpha\nbeta\n"));
        QCOMPARE(DocumentBlockTransfer::apply(captured,target,right->textCursor(),DocumentBlockTransfer::Mode::Move,{},&error),assetId);
        const auto waveCopy=DocumentBlockTransfer::apply(DocumentBlockTransfer::capture(origin,waveId),target,right->textCursor(),DocumentBlockTransfer::Mode::Copy,{},&error);
        QVERIFY2(!waveCopy.isEmpty(),qPrintable(error)); QVERIFY(waveCopy!=waveId);
        const auto targetPath=directory->filePath("other/copy.sv"); QVERIFY(QDir().mkpath(QFileInfo(targetPath).absolutePath()));
        QVERIFY(manager.saveEditorView(right,true,targetPath)); QVERIFY(manager.saveEditorView(left));
        QVERIFY(b->undoAssociation(false)); QVERIFY(b->undoAssociation(false)); QVERIFY(a->find(assetId)); QVERIFY(!b->find(assetId));
        QVERIFY(a->undoAssociation(true)); QVERIFY(b->undoAssociation(true)); QVERIFY(!a->find(assetId)); QVERIFY(b->find(assetId));
        QVERIFY(manager.saveEditorView(right)); QVERIFY(manager.saveEditorView(left)); QVERIFY(manager.closeAllTabs());
        QVERIFY(manager.openFileInTab(source)); left=manager.getCurrentEditor(); QVERIFY(!left->blockModel()->find(assetId)); QVERIFY(left->blockModel()->find(waveId));
        QVERIFY(manager.openFileInTab(targetPath)); right=manager.getCurrentEditor(); b=right->blockModel();
        QVERIFY(b->find(assetId)); QVERIFY(b->find(waveCopy)); QCOMPARE(DocumentBlockStore::get(b)->assetBytes(block.asset,&error),bytes);
        QVERIFY2(WaveBlockContent::validate(b->find(waveCopy)->text.toUtf8(),&error),qPrintable(error));
        auto ephemeral=std::make_unique<SharedDocument>(QUuid::createUuid().toString(),QString(),QString("peer"));
        auto* peer=DocumentBlockModel::get(ephemeral->textDocument()); const auto transient=peer->insert("text",QTextCursor(ephemeral->textDocument()),"retained");
        QCOMPARE(DocumentBlockTransfer::apply(DocumentBlockTransfer::capture(ephemeral.get(),transient),manager.sharedDocumentForEditor(right),right->textCursor(),DocumentBlockTransfer::Mode::Move,{},&error),transient);
        ephemeral.reset(); const auto retained=b->serialize(); QVERIFY(b->undoAssociation(false)); QVERIFY(!b->error().isEmpty()); QCOMPARE(b->serialize(),retained);
    }
    void pairedMoveCopyAndFailureAtomicity() {
        SharedDocument left(QUuid::createUuid().toString(),{},"left\nline\n"),right(QUuid::createUuid().toString(),{},"right\nline\n");
        auto* a=DocumentBlockModel::get(left.textDocument()); auto* b=DocumentBlockModel::get(right.textDocument());
        const auto id=a->insert("text",QTextCursor(left.textDocument()),"independent");
        QString issue; auto captured=DocumentBlockTransfer::capture(&left,id);
        const auto copy=DocumentBlockTransfer::apply(captured,&right,QTextCursor(right.textDocument()),DocumentBlockTransfer::Mode::Copy,{},&issue);
        QVERIFY2(!copy.isEmpty(),qPrintable(issue)); QVERIFY(copy!=id); QVERIFY(a->find(id));
        b->editText(copy,"changed"); b->endTextSession(); QCOMPARE(a->find(id)->text,QString("independent"));
        const auto leftBefore=a->serialize(),rightBefore=b->serialize();
        right.setReadOnly(true); QVERIFY(DocumentBlockTransfer::apply(captured,&right,QTextCursor(right.textDocument()),DocumentBlockTransfer::Mode::Move,{},&issue).isEmpty());
        QCOMPARE(a->serialize(),leftBefore); QCOMPARE(b->serialize(),rightBefore); right.setReadOnly(false);
        QCOMPARE(DocumentBlockTransfer::apply(captured,&right,QTextCursor(right.textDocument()->findBlockByNumber(1)),DocumentBlockTransfer::Mode::Move,{},&issue),id);
        QVERIFY(!a->find(id)); QVERIFY(b->find(id));
        const auto moved=b->serialize(); a->insert("text",QTextCursor(left.textDocument()),"later");
        QVERIFY(b->undoAssociation(false)); QVERIFY(!b->error().isEmpty()); QCOMPARE(b->serialize(),moved);
        QVERIFY(a->undoAssociation(false)); QVERIFY(b->undoAssociation(false)); QVERIFY(a->find(id)); QVERIFY(!b->find(id));
        QVERIFY(a->undoAssociation(true)); QVERIFY(!a->find(id)); QVERIFY(b->find(id));
        left.setReadOnly(true); QVERIFY(b->undoAssociation(false)); QVERIFY(!a->find(id)); QVERIFY(b->find(id)); left.setReadOnly(false);
        QVERIFY(b->undoAssociation(false)); QVERIFY(a->find(id));
        captured=DocumentBlockTransfer::capture(&left,id); a->editText(id,"late edit"); a->endTextSession();
        const auto staleBefore=a->serialize(); QVERIFY(DocumentBlockTransfer::apply(captured,&right,QTextCursor(right.textDocument()),DocumentBlockTransfer::Mode::Move,{},&issue).isEmpty());
        QCOMPARE(a->serialize(),staleBefore); QCOMPARE(left.textDocument()->toPlainText(),QString("left\nline\n"));
        QCOMPARE(right.textDocument()->toPlainText(),QString("right\nline\n"));
    }
    void externalAdaptersAndCopiedAttachmentsAreLossless() {
        auto* storage=store(); QMimeData input; input.setText("ordinary note");
        auto values=BlockDropAdapter::decode(&input,storage,&error); QCOMPARE(values.size(),1); QCOMPARE(values.first().type,QString("text"));
        QMimeData code; code.setData("text/x-systemverilog","logic a;\n"); values=BlockDropAdapter::decode(&code,storage,&error); QCOMPARE(values.first().type,QString("code"));
        QMimeData link; link.setUrls({QUrl("https://example.test/design")}); values=BlockDropAdapter::decode(&link,storage,&error); QCOMPARE(values.first().type,QString("reference"));
        QMimeData wave; wave.setData("application/json",WaveBlockContent::initialProject().toUtf8()); values=BlockDropAdapter::decode(&wave,storage,&error);
        QVERIFY2(values.size()==1,qPrintable(error)); QCOMPARE(values.first().type,QString("wave"));
        const auto path=directory->filePath("binary.dat"); const QByteArray bytes("one\0two",7); QVERIFY(write(path,bytes));
        QMimeData files; files.setUrls({QUrl::fromLocalFile(path)}); values=BlockDropAdapter::decode(&files,storage,&error); QCOMPARE(values.size(),1);
        QCOMPARE(values.first().type,QString("attachment")); QCOMPARE(storage->assetBytes(values.first().asset,&error),bytes);
        const auto ids=editor->blockModel()->insertMany(values,editor->textCursor()); QCOMPARE(ids.size(),1); QVERIFY(save());
        const auto snapshot=storage->recoveryState(&error,directory->filePath("resources")); QVERIFY2(!snapshot.isEmpty(),qPrintable(error));
        QTextDocument restored; auto* other=DocumentBlockStore::get(DocumentBlockModel::get(&restored));
        QVERIFY2(other->restoreRecovery(snapshot,&error),qPrintable(error)); QCOMPARE(other->assetBytes(values.first().asset,&error),bytes);
        files.setUrls({QUrl::fromLocalFile(path),QUrl::fromLocalFile(directory->filePath("missing.bin"))});
        const auto before=editor->blockModel()->serialize(); QVERIFY(BlockDropAdapter::decode(&files,storage,&error).isEmpty()); QVERIFY(!error.isEmpty()); QCOMPARE(editor->blockModel()->serialize(),before);
    }
    void detachedCopyAndDeferredCutKeepOriginalUntilSuccess() {
        QTabWidget tabs; tabs.resize(850,550); tabs.show(); TabManager manager(&tabs); QVERIFY(manager.openFileInTab(source));
        auto* code=manager.getCurrentEditor(); auto* document=manager.sharedDocumentForEditor(code); auto* blocks=code->blockModel();
        const auto id=blocks->insert("text",code->textCursor(),"original"); const auto before=blocks->serialize();
        QVERIFY(!manager.detachAssociatedBlock(document,id,nullptr,[](auto*) { return false; },true)); QCOMPARE(blocks->serialize(),before);
        auto* page=manager.detachAssociatedBlock(document,id,nullptr,{},true); QVERIFY(page); QVERIFY(page->blockId()!=id); QVERIFY(!blocks->find(id)->detached);
        const auto duplicate=page->blockId(); QVERIFY(blocks->find(duplicate)->detached); QVERIFY(blocks->undoAssociation(false)); QVERIFY(!blocks->find(duplicate)); QVERIFY(blocks->find(id));
        auto* drag=manager.findChild<AssociatedBlockDragController*>(); QVERIFY(drag); QVERIFY2(drag->copyBlock(document,id,true),qPrintable(blocks->error())); QVERIFY(blocks->find(id));
        manager.createNewTab(); auto* target=manager.getCurrentEditor(); auto* receiving=manager.sharedDocumentForEditor(target);
        receiving->setReadOnly(true); QVERIFY(!drag->pasteBlock(target,target->textCursor())); QVERIFY(blocks->find(id));
        receiving->setReadOnly(false); QVERIFY(drag->pasteBlock(target,target->textCursor())); QVERIFY(!blocks->find(id)); QVERIFY(target->blockModel()->find(id));
        QVERIFY(target->blockModel()->undoAssociation(false)); QVERIFY(blocks->find(id)); QVERIFY(!target->blockModel()->find(id));
    }
    void detachedPageRetainsOwnerAndSavesWithoutCodeView() {
        QTabWidget tabs; tabs.resize(850,550); tabs.show(); TabManager manager(&tabs);
        manager.setCrashRecoveryService(std::make_unique<CrashRecoveryService>(directory->filePath("page-recovery")));
        manager.setWorkspaceScope({directory->path()},directory->path()); QVERIFY(manager.openFileInTab(source));
        auto* code=manager.getCurrentEditor(); QPointer<SharedDocument> document=manager.sharedDocumentForEditor(code);
        auto* blocks=code->blockModel(); const auto id=blocks->insert("text",code->textCursor(),"Shared note");
        const auto original=read(source);
        auto* page=manager.detachAssociatedBlock(document,id); QVERIFY(page); QCOMPARE(page->model(),blocks);
        QCoreApplication::processEvents(); QVERIFY(blocks->find(id)->detached);
        QCOMPARE(code->findChildren<QFrame*>("editorBlockShell").size(),0);
        manager.closeTab(tabs.indexOf(code)); QVERIFY(document); QCOMPARE(document->viewCount(),0); QCOMPARE(document->consumerCount(),1);
        auto* text=page->findChild<QTextEdit*>("editorBlockText"); QVERIFY(text);
        auto* edit=page->findChild<QToolButton*>("blockEditToggle");QVERIFY(edit);QTest::mouseClick(edit,Qt::LeftButton);QVERIFY(!text->isReadOnly());
        text->setPlainText("Still editable without a code view"); page->finishEditing();
        QCOMPARE(blocks->find(id)->text,text->toPlainText()); QVERIFY(manager.saveCurrentTab());
        QCOMPARE(read(source),original); QVERIFY(!document->dirty());
        QTest::mouseClick(edit,Qt::LeftButton);QVERIFY(!text->isReadOnly());text->setPlainText("Uncommitted note"); page->finishEditing();
        manager.unsavedDocumentManagerForTesting()->setDecisionProvider([](const auto&,QWidget*) { return UnsavedDocumentBatchDecision::Cancel; });
        manager.closeTab(tabs.indexOf(page)); QCOMPARE(manager.currentAssociatedBlock(),page); QVERIFY(document->dirty());
        const auto sidecar=DocumentBlockStore::get(blocks)->storagePath(); QVERIFY(write(sidecar,"{external change"));
        QVERIFY(!manager.saveCurrentTab()); QCOMPARE(manager.currentAssociatedBlock(),page); QCOMPARE(blocks->find(id)->text,QString("Uncommitted note"));
        QCOMPARE(read(source),original);
        manager.checkpointCrashRecovery(); manager.flushCrashRecovery();
        QVERIFY(!manager.listCrashRecoveryCandidates(directory->path()).candidates.isEmpty());
    }
    void detachedCloseReopenSaveAsAndUndoPlacement() {
        QWidget host; QVBoxLayout layout(&host); QTabWidget tabs; layout.addWidget(&tabs); host.resize(900,600); host.show();
        TabManager manager(&tabs); manager.enableSplitLayout(&host); manager.setWorkspaceScope({directory->path()},directory->path());
        QVERIFY(manager.openFileInTab(source)); auto* code=manager.getCurrentEditor(); auto* document=manager.sharedDocumentForEditor(code);
        auto* blocks=code->blockModel(); const auto id=blocks->insert("text",code->textCursor(),"One association");
        auto* page=manager.detachAssociatedBlock(document,id); QVERIFY(page);
        auto* split=manager.editorSplitController()->movePageToSplit(page,EditorSplitDirection::Right); QVERIFY(split);
        const auto groupId=manager.editorSplitController()->groupId(split);
        manager.closeTab(split->indexOf(page)); QVERIFY(blocks->find(id)->detached); QVERIFY(manager.associatedBlockPages().isEmpty());
        QVERIFY(manager.reopenClosedTab()); page=manager.currentAssociatedBlock(); QVERIFY(page);
        QCOMPARE(page->blockId(),id); QCOMPARE(page->model(),blocks);
        const auto renamed=directory->filePath("rtl/renamed.sv"); QVERIFY(manager.saveSharedDocument(document,true,renamed));
        QCOMPARE(page->ownerDocument()->fileName(),renamed); QCOMPARE(read(renamed),read(source));
        QTextCursor target(document->textDocument()->findBlockByNumber(1)); QVERIFY(blocks->move(id,target));
        QTRY_VERIFY(manager.associatedBlockPages().isEmpty()); QVERIFY(!blocks->find(id)->detached);
        code->undo(); QTRY_COMPARE(manager.associatedBlockPages().size(),1);
        page=manager.associatedBlockPages().first(); QVERIFY(blocks->find(id)->detached);
        QVERIFY(!manager.editorSplitController()->groupForPage(page)->property("pageGroupId").toString().isEmpty());
        page->replay(true); QTRY_VERIFY(manager.associatedBlockPages().isEmpty()); QVERIFY(!blocks->find(id)->detached);
        QCOMPARE(blocks->find(id)->start,target.block().position());
        QVERIFY(manager.saveSharedDocument(document));
    }
    void detachedRegistrationFailurePreservesInlineAndExistingPage() {
        QWidget host; std::unique_ptr<QTabWidget> owned(UiControls::editorTabWidget()); auto& tabs=*owned;
        TabManager manager(&tabs); manager.editorSplitController()->setHost(&host); QVERIFY(manager.openFileInTab(source));
        auto* code=manager.getCurrentEditor(); auto* document=manager.sharedDocumentForEditor(code);
        auto* blocks=code->blockModel(); const auto id=blocks->insert("text",code->textCursor(),"Keep this note");
        const auto before=blocks->serialize(); const auto revision=blocks->revision();
        QVERIFY(!manager.detachAssociatedBlock(document,id,nullptr,[](auto*) { return false; }));
        QCOMPARE(blocks->serialize(),before); QCOMPARE(blocks->revision(),revision);
        QVERIFY(manager.associatedBlockPages().isEmpty()); QCOMPARE(document->consumerCount(),0);
        auto* page=manager.detachAssociatedBlock(document,id); QVERIFY(page);
        QVERIFY(!manager.detachAssociatedBlock(document,id,nullptr,[](auto*) { return false; }));
        QCOMPARE(manager.associatedBlockPages(),QList<AssociatedBlockPage*>{page}); QVERIFY(blocks->find(id)->detached);
        const auto detached=blocks->serialize(); const int originalIndex=tabs.indexOf(page);
        QVERIFY(!manager.detachAssociatedBlock(document,id,nullptr,[&](auto* moving) {
            auto* receiving=manager.editorSplitController()->createSplit(&tabs,EditorSplitDirection::Right);
            if(!receiving || !manager.editorSplitController()->movePage(moving,receiving)) return false;
            document->setReadOnly(true); return true;
        }));
        document->setReadOnly(false); QCOMPARE(manager.editorSplitController()->groupForPage(page),&tabs);
        QCOMPARE(tabs.indexOf(page),originalIndex); QCOMPARE(blocks->serialize(),detached);
    }
    void detachedPathMutationRejectsLateBlockEditsAndClosesConsumers() {
        QTabWidget tabs; TabManager manager(&tabs); QVERIFY(manager.openFileInTab(source));
        auto* code=manager.getCurrentEditor(); auto* document=manager.sharedDocumentForEditor(code);
        auto* blocks=code->blockModel(); const auto id=blocks->insert("text",code->textCursor(),"Before rename");
        auto* page=manager.detachAssociatedBlock(document,id); QVERIFY(page); QVERIFY(manager.saveSharedDocument(document));
        manager.closeTab(tabs.indexOf(code)); QCOMPARE(document->viewCount(),0);
        WorkspacePathMutation mutation; QString issue;
        QVERIFY(manager.prepareWorkspacePathMutation(source,false,&mutation,nullptr,&issue));
        blocks->editText(id,"Late edit"); blocks->endTextSession(); QVERIFY(!manager.validateWorkspacePathMutation(mutation,&issue));
        QVERIFY(manager.saveSharedDocument(document));
        QVERIFY(manager.prepareWorkspacePathMutation(source,false,&mutation,nullptr,&issue));
        QVERIFY(manager.finalizeWorkspacePathMutation(mutation,&issue)); QVERIFY(manager.associatedBlockPages().isEmpty());
    }
    void gutterDropUsesOwnerRevisionAndOrder() {
        QTabWidget tabs; tabs.resize(850,650); tabs.show(); TabManager manager(&tabs);
        QVERIFY(manager.openFileInTab(source)); auto* code=manager.getCurrentEditor(); auto* document=manager.sharedDocumentForEditor(code);
        auto* blocks=code->blockModel(); const auto moving=blocks->insert("text",code->textCursor(),"moving");
        const auto first=blocks->insert("text",QTextCursor(code->document()->findBlockByNumber(1)),"first");
        const auto second=blocks->insert("text",QTextCursor(code->document()->findBlockByNumber(1)),"second");
        auto* page=manager.detachAssociatedBlock(document,moving); QVERIFY(page);
        tabs.setCurrentWidget(code); QCoreApplication::processEvents();
        auto* drag=manager.findChild<AssociatedBlockDragController*>(); QVERIFY(drag);
        auto* gutter=code->findChild<QWidget*>("editorLineNumberGutter"); QVERIFY(gutter && gutter->acceptDrops());
        auto send=[&](QMimeData* mime, QPoint point, bool commit) {
            QDragEnterEvent enter(point,Qt::MoveAction,mime,Qt::LeftButton,Qt::NoModifier); QApplication::sendEvent(gutter,&enter);
            if(!enter.isAccepted()) return false;
            QDragMoveEvent move(point,Qt::MoveAction,mime,Qt::LeftButton,Qt::NoModifier); QApplication::sendEvent(gutter,&move);
            if(!move.isAccepted() || !commit) return move.isAccepted();
            QDropEvent drop(QPointF(point),Qt::MoveAction,mime,Qt::LeftButton,Qt::NoModifier); QApplication::sendEvent(gutter,&drop); return drop.isAccepted();
        };
        const auto sourceBytes=code->toPlainText(); const auto before=blocks->serialize();
        auto mime=std::unique_ptr<QMimeData>(drag->createMimeData(document,moving));
        const int lineY=qRound(code->blockGeometry(1).top+4);
        QPoint point=gutter->mapFromGlobal(code->viewport()->mapToGlobal(QPoint(0,lineY))); point.setX(gutter->width()/2);
        QVERIFY(send(mime.get(),point,false)); QCOMPARE(blocks->serialize(),before);
        auto* marker=code->findChild<QLabel*>("associatedBlockDropMarker"); QVERIFY(marker); QCOMPARE(marker->property("sourceLine").toInt(),1);
        QDragLeaveEvent leave; QApplication::sendEvent(gutter,&leave); QCOMPARE(blocks->serialize(),before);
        document->setReadOnly(true); QVERIFY(!send(mime.get(),point,true)); document->setReadOnly(false);
        QTextCursor change(document->textDocument()); change.insertText("x"); QVERIFY(!send(mime.get(),point,true));
        code->undo(); mime.reset(drag->createMimeData(document,moving));
        QVERIFY(send(mime.get(),point,true)); QTRY_VERIFY(manager.associatedBlockPages().isEmpty());
        QVERIFY(!blocks->find(moving)->detached); QCOMPARE(code->toPlainText(),sourceBytes);
        QCOMPARE(blocks->find(moving)->start,code->document()->findBlockByNumber(1).position());
        QVERIFY(blocks->find(moving)->order < blocks->find(first)->order);
        QVERIFY(blocks->find(first)->order < blocks->find(second)->order);
    }
    void releasedGripUsesTheNormalDropTransaction_data() {
        QTest::addColumn<QString>("target");
        QTest::newRow("owner-gutter")<<QString("gutter");
        QTest::newRow("tab-bar")<<QString("tab");
        QTest::newRow("other-owner-gutter")<<QString("other");
        QTest::newRow("gutter-control-copy")<<QString("copy");
        QTest::newRow("page-tab-control-copy")<<QString("tab-copy");
        QTest::newRow("page-tab-return-to-same-group")<<QString("tab-return");
    }
    void sustainedBlockGripNativeDrag_data() {
        QTest::addColumn<QString>("presentation");
        QTest::addColumn<bool>("crossFile");
        QTest::addColumn<bool>("cancel");
        QTest::newRow("inline-same-file-copy") << QString("inline") << false << false;
        QTest::newRow("inline-cross-file-copy") << QString("inline") << true << false;
        QTest::newRow("inline-escape") << QString("inline") << false << true;
        QTest::newRow("floating-page-copy") << QString("floating") << false << false;
        QTest::newRow("floating-page-escape") << QString("floating") << false << true;
        QTest::newRow("docked-page-copy") << QString("docked") << false << false;
        QTest::newRow("docked-page-escape") << QString("docked") << false << true;
    }
    void sustainedBlockGripNativeDrag() {
#ifndef Q_OS_WIN
        QSKIP("Requires Windows native held-button drag");
#else
        if (QGuiApplication::platformName() != "windows") QSKIP("Requires native Windows QPA");
        QFETCH(QString, presentation); QFETCH(bool, crossFile); QFETCH(bool, cancel);
        QVERIFY2(!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)
                 && !(GetAsyncKeyState(VK_CONTROL) & 0x8000)
                 && !(GetAsyncKeyState(VK_ESCAPE) & 0x8000), "Native input must initially be idle");
        QWidget host; std::unique_ptr<QTabWidget> owned(UiControls::editorTabWidget());
        auto& tabs=*owned; TabManager manager(&tabs);
        manager.setCrashRecoveryService(std::make_unique<CrashRecoveryService>(directory->filePath("native-recovery")));
        auto* split = manager.editorSplitController(); split->setHost(&host);
        host.setWindowFlag(Qt::WindowStaysOnTopHint);
        host.setGeometry(QRect(QGuiApplication::primaryScreen()->availableGeometry().topLeft() + QPoint(120,120), QSize(1100,780)));
        host.show(); host.raise(); host.activateWindow(); QVERIFY(QTest::qWaitForWindowActive(&host));
        QVERIFY(manager.openFileInTab(source));
        auto* code = manager.getCurrentEditor(); auto* owner = manager.sharedDocumentForEditor(code);
        auto* blocks = code->blockModel(); const auto id = blocks->insert("text", code->textCursor(), "Native held block");
        auto* destination = code;
        if (crossFile) {
            const auto other = directory->filePath("destination.sv"); QVERIFY(write(other,"one\ntwo\nthree\n"));
            QVERIFY(manager.openFileInTab(other)); destination = manager.getCurrentEditor();
            auto* receiving = split->createSplit(&tabs, EditorSplitDirection::Right); QVERIFY(receiving);
            QVERIFY(split->movePage(destination, receiving)); tabs.setCurrentWidget(code);
        }
        auto* receivingBlocks = destination->blockModel();
        code->selectAssociatedBlock(id);
        QPointer<QWidget> handle;
        QTRY_VERIFY((handle = code->findChild<QToolButton*>("editorBlockDragHandle")) && handle->isVisible());
        auto* gutter = destination->findChild<QWidget*>("editorLineNumberGutter"); QVERIFY(gutter);
        QCoreApplication::processEvents();
        const int destinationPosition = destination->document()->findBlockByNumber(1).position();
        QPoint dropPoint = gutter->mapToGlobal(QPoint(gutter->width()/2, qRound(destination->blockGeometry(1).top+5)));
        QVERIFY(gutter->rect().contains(gutter->mapFromGlobal(dropPoint)));
        QPointer<AssociatedBlockPage> page;
        if (presentation != "inline") {
            page = manager.detachAssociatedBlock(owner,id); QVERIFY(page);
            tabs.setCurrentWidget(code); QCoreApplication::processEvents();
            dropPoint=gutter->mapToGlobal(QPoint(gutter->width()/2,qRound(destination->blockGeometry(1).top+5)));
            if (presentation == "floating") {
                auto* group = split->floatPage(page, dropPoint + QPoint(80,80)); QVERIFY(group);
                auto* floating = group->window();
                floating->setGeometry(QRect(dropPoint-QPoint(80,110),QSize(500,330)));
                tabs.setCurrentWidget(code); floating->show(); floating->raise(); floating->activateWindow();
                QVERIFY(QTest::qWaitForWindowActive(floating));
            } else tabs.setCurrentWidget(page);
            QTRY_VERIFY((handle=page->findChild<QToolButton*>("associatedBlockDragHandle")));
            page->setFocus(); QTRY_VERIFY(handle->isVisible());
        }
        blocks->editText(id,"History sentinel"); blocks->endTextSession();
        QVERIFY(blocks->undoAssociation(false)); QCOMPARE(blocks->find(id)->text,QString("Native held block"));
        QVERIFY(blocks->canRedo());
        const auto before = blocks->serialize(), receivingBefore = receivingBlocks->serialize();
        const auto revision = blocks->revision(), receivingRevision = receivingBlocks->revision();
        const auto sourceText = code->toPlainText(), destinationText = destination->toPlainText();
        const auto sourceTextUndo = code->document()->availableUndoSteps();
        const auto sourceTextRedo = code->document()->availableRedoSteps();
        const auto originalGroup = page ? split->groupForPage(page) : nullptr;
        const auto originalWindow = page ? page->window() : nullptr;
        const auto windowGeometry = originalWindow ? originalWindow->geometry() : QRect();
        const auto pageGeometry = page ? page->geometry() : QRect();
        const int groupCount = split->groups().size();
        auto* controller = manager.findChild<AssociatedBlockDragController*>(); QVERIFY(controller);
        QSignalSpy finished(controller,&AssociatedBlockDragController::gestureFinished);
        const QPoint previousCursor = QCursor::pos();
        std::atomic_bool leftHeld=false, controlHeld=false, escapeHeld=false;
        const auto mouseButton = [&](bool down) {
            INPUT event{}; event.type=INPUT_MOUSE; event.mi.dwFlags=down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
            const bool ok=SendInput(1,&event,sizeof(event))==1; if(ok) leftHeld=down; return ok;
        };
        const auto key = [&](WORD value,bool down) {
            INPUT event{}; event.type=INPUT_KEYBOARD; event.ki.wVk=value; event.ki.dwFlags=down ? 0 : KEYEVENTF_KEYUP;
            const bool ok=SendInput(1,&event,sizeof(event))==1;
            if(ok && value==VK_CONTROL) controlHeld=down;
            if(ok && value==VK_ESCAPE) escapeHeld=down;
            return ok;
        };
        const auto ownsForeground = [] {
            DWORD process=0; GetWindowThreadProcessId(GetForegroundWindow(),&process);
            return process==GetCurrentProcessId();
        };
        const auto nativePoint = [&](const QPoint& point) {
            POINT native{}; ClientToScreen(reinterpret_cast<HWND>(host.winId()),&native);
            const auto local=host.mapFromGlobal(point); const auto dpr=host.devicePixelRatioF();
            native.x+=qRound(local.x()*dpr); native.y+=qRound(local.y()*dpr);
            return native;
        };
        const auto moveCursor = [](POINT native) {
            DWORD process=0; GetWindowThreadProcessId(WindowFromPoint(native),&process);
            if(process!=GetCurrentProcessId()) return false;
            INPUT event{}; event.type=INPUT_MOUSE;
            event.mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK;
            event.mi.dx=MulDiv(native.x-GetSystemMetrics(SM_XVIRTUALSCREEN),65535,GetSystemMetrics(SM_CXVIRTUALSCREEN)-1);
            event.mi.dy=MulDiv(native.y-GetSystemMetrics(SM_YVIRTUALSCREEN),65535,GetSystemMetrics(SM_CYVIRTUALSCREEN)-1);
            return SendInput(1,&event,sizeof(event))==1;
        };
        const auto releaseInput = qScopeGuard([&] {
            if(controller->dragInProgress()) QDrag::cancel();
            if(leftHeld) mouseButton(false); if(controlHeld) key(VK_CONTROL,false); if(escapeHeld) key(VK_ESCAPE,false);
            QCursor::setPos(previousCursor);
        });
        handle->setFocus(); QTRY_COMPARE(QApplication::focusWidget(),handle.data());
        const QPointer<QWidget> originalFocus=QApplication::focusWidget();
        const auto capture=[&](const QString& phase) {
            const auto path=qEnvironmentVariable("ZEROSLACK_TEST_ARTIFACT_DIR");
            if(path.isEmpty()) return;
            const auto stem=path+"/"+QString::fromUtf8(QTest::currentDataTag())+"-"+phase;
            host.grab().save(stem+"-host.png");
            if(page && page->window()!=&host && page->window()->isVisible()) page->window()->grab().save(stem+"-page.png");
        };
        capture("before");
        const auto pressPoint=handle->mapToGlobal(handle->rect().center());
        const auto pressNative=nativePoint(pressPoint);
        const auto thresholdNative=nativePoint(pressPoint+QPoint(28,0));
        const auto dropNative=nativePoint(dropPoint);
        QVERIFY(moveCursor(pressNative)); QTest::qWait(80); QVERIFY(ownsForeground());
        QVERIFY(mouseButton(true)); QTRY_VERIFY(GetAsyncKeyState(VK_LBUTTON)&0x8000);
        QTest::qWait(80);
        QVERIFY(!controller->dragInProgress());
        QTimer observer; observer.setInterval(20);
        QElapsedTimer elapsed; elapsed.start();
        QPointer<QDrag> nativeDrag;
        std::atomic_bool execObserved=false, targetReady=false, copyObserved=false, gestureDone=false, done=false, stopInput=false;
        std::atomic_bool modifierWhileHeld=false;
        std::atomic_int inputFailure=0, inputStage=0;
        bool hiddenObserved=false, markerObserved=false;
        QList<int> nativeActions;
        int nativeSignalsWhileHeld=0, nativeEventsWhileHeld=0;
        struct DragEventProbe final : QObject {
            std::function<void(QDropEvent*)> capture;
            bool eventFilter(QObject*,QEvent* event) override {
                if(event->type()==QEvent::DragEnter || event->type()==QEvent::DragMove)
                    capture(static_cast<QDropEvent*>(event));
                return false;
            }
        } probe;
        probe.capture=[&](QDropEvent* event) {
            if(controller->dragInProgress() && (GetAsyncKeyState(VK_LBUTTON)&0x8000)
                && event->mimeData()->hasFormat("application/x-zeroslack-associated-block")) ++nativeEventsWhileHeld;
        };
        qApp->installEventFilter(&probe);
        connect(&observer,&QTimer::timeout,&observer,[&] {
            if(!nativeDrag && controller->dragInProgress()) {
                nativeDrag=controller->findChild<QDrag*>();
                if(nativeDrag) connect(nativeDrag,&QDrag::actionChanged,&observer,[&](Qt::DropAction action) {
                    nativeActions.append(int(action));
                    if(GetAsyncKeyState(VK_LBUTTON)&0x8000) ++nativeSignalsWhileHeld;
                    if(action==Qt::CopyAction && (GetAsyncKeyState(VK_LBUTTON)&0x8000)
                        && (GetAsyncKeyState(VK_CONTROL)&0x8000)) copyObserved=true;
                    qInfo()<<"A01_NATIVE_ACTION"<<action<<"leftHeld"<<bool(GetAsyncKeyState(VK_LBUTTON)&0x8000)
                           <<"controlHeld"<<bool(GetAsyncKeyState(VK_CONTROL)&0x8000)<<"elapsedMs"<<elapsed.elapsed();
                });
            }
            if(nativeDrag && controller->dragInProgress() && (GetAsyncKeyState(VK_LBUTTON)&0x8000)
                && nativeDrag->supportedActions()==(Qt::MoveAction|Qt::CopyAction)) {
                if(!execObserved) qInfo()<<"A01_EXEC_ENTERED"<<nativeDrag->source()<<nativeDrag->supportedActions()<<elapsed.elapsed();
                execObserved=true;
            }
            if(inputStage>=2 && controller->dragInProgress()) {
                auto* marker=destination->findChild<QLabel*>("associatedBlockDropMarker");
                const auto cursor=gutter->mapFromGlobal(QCursor::pos());
                markerObserved=markerObserved || (marker && marker->isVisible()
                    && marker->property("sourceLine").toInt()==1 && gutter->rect().contains(cursor)
                    && (QCursor::pos()-dropPoint).manhattanLength()<=2);
                if(page) hiddenObserved=hiddenObserved || !page->isVisible();
                else for(auto* content:code->findChildren<QWidget*>("editorBlockContent"))
                    hiddenObserved=hiddenObserved || (content->property("blockDragHidden").toBool() && !content->isVisible());
                if(markerObserved && hiddenObserved && !targetReady) {
                    qInfo()<<"A01_TARGET_READY"<<presentation<<"hidden"<<hiddenObserved<<"marker"<<markerObserved;
                    capture("held-target");
                    targetReady=true;
                }
            }
            gestureDone=finished.count()>0;
            if(inputFailure!=0 && controller->dragInProgress()) QDrag::cancel();
        });
        observer.start();
        // Windows OLE startup can block Qt timers while waiting for native
        // mouse input. Only the sender uses a worker; all widget reads stay here.
        std::thread sender([&] {
            const auto start=std::chrono::steady_clock::now();
            const auto milliseconds=[&] { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count(); };
            qint64 stageStarted=0; int tick=0;
            while(!stopInput && !done) {
                const auto now=milliseconds();
                if(now>8000) { inputFailure=1; break; }
                if(!ownsForeground()) { inputFailure=2; break; }
                if(inputStage==0 && now>80) {
                    inputStage=1; stageStarted=now;
                }
                POINT point=inputStage<2 ? thresholdNative : dropNative;
                point.x+=(tick++%2);
                if(inputStage>=1 && inputStage<=3 && !moveCursor(point)) { inputFailure=3; break; }
                if(inputStage==1 && execObserved && now-stageStarted>140) {
                    inputStage=2; stageStarted=now;
                } else if(inputStage==2 && targetReady && now-stageStarted>220) {
                    if(!(GetAsyncKeyState(VK_LBUTTON)&0x8000)) { inputFailure=6; break; }
                    if(!key(cancel ? VK_ESCAPE : VK_CONTROL,true)) { inputFailure=4; break; }
                    modifierWhileHeld=(GetAsyncKeyState(VK_LBUTTON)&0x8000)
                        && (GetAsyncKeyState(cancel ? VK_ESCAPE : VK_CONTROL)&0x8000);
                    inputStage=3; stageStarted=now;
                } else if(inputStage==3) {
                    if(cancel && gestureDone) done=true;
                    else if(!cancel && copyObserved && now-stageStarted>180) {
                        if(!mouseButton(false)) { inputFailure=5; break; }
                        inputStage=4;
                    }
                } else if(inputStage==4 && gestureDone) done=true;
                std::this_thread::sleep_for(std::chrono::milliseconds(40));
            }
            if(inputFailure!=0 && ownsForeground()) key(VK_ESCAPE,true);
            if(leftHeld) mouseButton(false);
            if(controlHeld) key(VK_CONTROL,false);
            if(escapeHeld) key(VK_ESCAPE,false);
            done=true;
        });
        const auto joinSender=qScopeGuard([&] { stopInput=true; if(sender.joinable()) sender.join(); observer.stop(); });
        QTRY_VERIFY_WITH_TIMEOUT(done.load(),10000);
        sender.join(); observer.stop();
        qInfo()<<"A01_NATIVE_RESULT"<<presentation<<"crossFile"<<crossFile<<"cancel"<<cancel
               <<"exec"<<execObserved.load()<<"nativeActions"<<nativeActions<<"signalsWhileHeld"<<nativeSignalsWhileHeld<<"eventsWhileHeld"<<nativeEventsWhileHeld
               <<"hidden"<<hiddenObserved<<"marker"<<markerObserved<<"modifierWhileHeld"<<modifierWhileHeld.load()
               <<"finished"<<finished.count()<<"stage"<<inputStage.load()<<"failure"<<inputFailure.load();
        if(inputFailure!=0) {
            capture("failed");
            auto* marker=destination->findChild<QLabel*>("associatedBlockDropMarker");
            qInfo()<<"A01_FAILED_TARGET"<<dropPoint<<"cursor"<<QCursor::pos()<<"gutter"<<gutter->mapToGlobal(QPoint())<<gutter->size()
                   <<"lineOne"<<destination->blockGeometry(1).top<<"markerLine"<<(marker ? marker->property("sourceLine").toInt() : -1);
        }
        QCOMPARE(inputFailure.load(),0);
        QVERIFY(!(GetAsyncKeyState(VK_LBUTTON)&0x8000));
        QVERIFY(!(GetAsyncKeyState(VK_CONTROL)&0x8000)); QVERIFY(!(GetAsyncKeyState(VK_ESCAPE)&0x8000));
        QVERIFY(execObserved); QVERIFY(nativeEventsWhileHeld>0); QVERIFY(hiddenObserved); QVERIFY(markerObserved); QVERIFY(modifierWhileHeld);
        QCOMPARE(finished.count(),1); QVERIFY(!controller->dragInProgress());
        QTRY_VERIFY(controller->findChildren<QDrag*>().isEmpty());
        QVERIFY(!GetCapture()); QVERIFY(!QWidget::mouseGrabber()); QVERIFY(!QGuiApplication::overrideCursor());
        for(auto* window:QGuiApplication::allWindows())
            if(window->objectName()=="QWindowsDragCursorWindow") QVERIFY(!window->isVisible());
        QVERIFY(!destination->findChild<QLabel*>("associatedBlockDropMarker"));
        for(auto* overlay:host.findChildren<QWidget*>("editorDropPreviewOverlay")) QVERIFY(!overlay->isVisible());
        for(auto* content:code->findChildren<QWidget*>("editorBlockContent")) QVERIFY(!content->property("blockDragHidden").toBool());
        QVERIFY(handle); QCOMPARE(handle->cursor().shape(),Qt::OpenHandCursor);
        QCOMPARE(code->toPlainText(),sourceText); QCOMPARE(destination->toPlainText(),destinationText);
        QCOMPARE(code->document()->availableUndoSteps(),sourceTextUndo);
        QCOMPARE(code->document()->availableRedoSteps(),sourceTextRedo);
        QCOMPARE(split->groups().size(),groupCount);
        if(page) {
            QCOMPARE(page->ownerDocument(),owner); QCOMPARE(page->blockId(),id);
            QCOMPARE(split->groupForPage(page),originalGroup); QCOMPARE(page->window(),originalWindow);
            if(cancel || presentation=="floating") QTRY_VERIFY(page->isVisible());
            else QCOMPARE(originalGroup->currentWidget(),code);
            QCOMPARE(page->window()->geometry(),windowGeometry); QCOMPARE(page->geometry(),pageGeometry);
        }
        capture("after");
        if(cancel) {
            QCOMPARE(blocks->serialize(),before); QCOMPARE(receivingBlocks->serialize(),receivingBefore);
            QCOMPARE(blocks->revision(),revision); QCOMPARE(receivingBlocks->revision(),receivingRevision);
            QTRY_COMPARE(QApplication::focusWidget(),originalFocus.data());
            QVERIFY(blocks->canRedo()); QVERIFY(blocks->undoAssociation(true));
            QCOMPARE(blocks->find(id)->text,QString("History sentinel"));
            QVERIFY(blocks->undoAssociation(false)); QCOMPARE(blocks->serialize(),before);
        } else {
            QVERIFY(nativeSignalsWhileHeld>0); QVERIFY(copyObserved);
            QVERIFY(nativeActions.contains(int(Qt::CopyAction))); QVERIFY(blocks->find(id));
            QCOMPARE(blocks->find(id)->detached,presentation!="inline");
            QCOMPARE(receivingBlocks->items().size(),crossFile ? 1 : 2);
            QString duplicate; for(const auto& item:receivingBlocks->items()) if(item.id!=id) duplicate=item.id;
            QVERIFY(!duplicate.isEmpty()); QCOMPARE(receivingBlocks->find(duplicate)->text,QString("Native held block"));
            QCOMPARE(receivingBlocks->find(duplicate)->start,destinationPosition); QVERIFY(!receivingBlocks->find(duplicate)->detached);
            if(crossFile) QCOMPARE(blocks->serialize(),before);
            QVERIFY(receivingBlocks->undoAssociation(false)); QCOMPARE(blocks->serialize(),before); QCOMPARE(receivingBlocks->serialize(),receivingBefore);
            QVERIFY(receivingBlocks->undoAssociation(true)); QVERIFY(receivingBlocks->find(duplicate));
        }
#endif
    }
    void releasedGripUsesTheNormalDropTransaction() {
#ifndef Q_OS_WIN
        QSKIP("Windows native drag startup regression");
#else
        if(QGuiApplication::platformName()!="windows") QSKIP("Requires native Windows hit testing");
        QFETCH(QString,target);
        QWidget host; QTabWidget tabs; TabManager manager(&tabs); manager.editorSplitController()->setHost(&host);
        host.setWindowFlag(Qt::WindowStaysOnTopHint);
        host.resize(850,650); host.move(QGuiApplication::primaryScreen()->availableGeometry().topLeft()+QPoint(60,60));
        host.show(); host.raise(); host.activateWindow(); QVERIFY(QTest::qWaitForWindowActive(&host));
        QVERIFY(manager.openFileInTab(source));
        auto* code=manager.getCurrentEditor(); auto* document=manager.sharedDocumentForEditor(code);
        auto* blocks=code->blockModel(); const auto id=blocks->insert("text",code->textCursor(),"Fast grip");
        QPointer<QWidget> handle;
        QTRY_VERIFY((handle=code->findChild<QToolButton*>("editorBlockDragHandle")));
        auto* drag=manager.findChild<AssociatedBlockDragController*>(); QVERIFY(drag);
        auto* destination=code;
        if (target=="tab-copy" || target=="tab-return") {
            QVERIFY(manager.detachAssociatedBlock(document,id));
            handle=tabs.tabBar();
            if(target=="tab-copy") {
                auto* group=manager.editorSplitController()->createSplit(&tabs,EditorSplitDirection::Right); QVERIFY(group);
                QVERIFY(manager.editorSplitController()->movePage(code,group));
            } else tabs.setCurrentWidget(code);
        }
        if(target=="other") {
            manager.createNewTab(); destination=manager.getCurrentEditor(); destination->setPlainText("other\nowner\n");
            // Hiding an inline editor removes its transient block shell.
            // Keep a source handle owned directly by the source editor.
            handle=new QToolButton(code); handle->setObjectName("editorBlockDragHandle"); handle->setProperty("blockId",id);
        }
        QCoreApplication::processEvents();
        auto* gutter=destination->findChild<QWidget*>("editorLineNumberGutter"); QVERIFY(gutter);
        const auto global=target=="tab" ? tabs.tabBar()->mapToGlobal(tabs.tabBar()->rect().center())
            : gutter->mapToGlobal(QPoint(gutter->width()/2,qRound(destination->blockGeometry(1).top+4)));
        const auto before=blocks->serialize(); const auto sourceText=code->toPlainText();
        const auto pressPoint=target=="tab-copy" || target=="tab-return" ? tabs.tabBar()->tabRect(tabs.indexOf(manager.associatedBlockPages().first())).center() : handle->rect().center();
        const auto pressedGlobal=handle->mapToGlobal(pressPoint);
        const auto modifiers=target.endsWith("copy") ? Qt::ControlModifier : Qt::NoModifier;
        QMouseEvent press(QEvent::MouseButtonPress,pressPoint,pressedGlobal,Qt::LeftButton,Qt::LeftButton,modifiers);
        QApplication::sendEvent(handle,&press);
        QCursor::setPos(global);
        QCOMPARE(QCursor::pos(),global);
        qInfo()<<"NATIVE_DROP_PRECONDITION"<<target<<global<<host.frameGeometry()<<tabs.geometry()<<destination->isVisible()
            <<QGuiApplication::topLevelAt(global)<<QApplication::widgetAt(global)<<QGuiApplication::primaryScreen()->availableGeometry();
        POINT nativeCursor{}; GetCursorPos(&nativeCursor); const auto nativeHit=WindowFromPoint(nativeCursor);
        DWORD nativeProcess=0; GetWindowThreadProcessId(nativeHit,&nativeProcess); wchar_t nativeClass[128]{}; GetClassNameW(nativeHit,nativeClass,128);
        POINT origin{}; ClientToScreen(reinterpret_cast<HWND>(host.winId()),&origin);
        qInfo()<<"NATIVE_DROP_WINDOWS"<<QPoint(nativeCursor.x,nativeCursor.y)<<QPoint(origin.x,origin.y)<<nativeProcess<<QString::fromWCharArray(nativeClass)
            <<quintptr(GetForegroundWindow())<<quintptr(host.winId());
        QTRY_VERIFY(QApplication::widgetAt(global));
        qInfo()<<"RELEASED_GRIP_TARGET"<<target<<global<<QApplication::widgetAt(global)->objectName()<<QApplication::widgetAt(global)->metaObject()->className();
        QSignalSpy finished(drag,&AssociatedBlockDragController::gestureFinished);
        // The queued move still says LeftButton, although the native button is up.
        QMouseEvent move(QEvent::MouseMove,handle->mapFromGlobal(global),global,Qt::NoButton,Qt::LeftButton,modifiers);
        QApplication::sendEvent(handle,&move);
        QCOMPARE(finished.count(),1); QVERIFY(!drag->dragInProgress());
        QCOMPARE(code->toPlainText(),sourceText);
        if(target.endsWith("copy")) {
            QCOMPARE(blocks->items().size(),2); QVERIFY(blocks->find(id));
            QString duplicate; for(const auto& block:blocks->items()) if(block.id!=id) duplicate=block.id;
            QVERIFY(!duplicate.isEmpty()); QVERIFY(!blocks->find(duplicate)->detached);
            QCOMPARE(blocks->find(id)->detached,target=="tab-copy");
            QVERIFY(blocks->undoAssociation(false)); QCOMPARE(blocks->serialize(),before);
        }
        else if(target=="other") { QVERIFY(!blocks->find(id)); QVERIFY(destination->blockModel()->find(id)); QVERIFY(destination->blockModel()->undoAssociation(false)); QCOMPARE(blocks->serialize(),before); }
        else if(target=="tab") {
            QTRY_COMPARE(manager.associatedBlockPages().size(),1); QVERIFY(blocks->find(id)->detached);
        } else {
            QCOMPARE(blocks->find(id)->start,code->document()->findBlockByNumber(1).position());
            QVERIFY(!blocks->find(id)->detached); QVERIFY(manager.associatedBlockPages().isEmpty());
            QTRY_VERIFY(QApplication::focusWidget() && QApplication::focusWidget()->property("blockId").toString()==id);
        }
        QVERIFY(!code->findChild<QLabel*>("associatedBlockDropMarker"));
#endif
    }
    void gutterDropPreservesWrappedAndFoldedAnchors_data() {
        QTest::addColumn<bool>("folded"); QTest::newRow("wrapped-range")<<false; QTest::newRow("folded-range")<<true;
    }
    void gutterDropPreservesWrappedAndFoldedAnchors() {
        QFETCH(bool,folded);
        QTabWidget tabs; tabs.resize(380,800); tabs.show(); TabManager manager(&tabs); manager.createNewTab();
        auto* code=manager.getCurrentEditor(); code->setLineWrapMode(QPlainTextEdit::WidgetWidth);
        const QString sourceText="module m;\ninitial begin\n  value = "+QString("long_name + ").repeated(18)+"0;\n  ready = 1;\nend\nendmodule\n";
        code->setPlainText(sourceText); auto* document=manager.sharedDocumentForEditor(code); auto* blocks=code->blockModel();
        const auto moving=blocks->insert("text",QTextCursor(code->document()),"moving");
        QTextCursor range(code->document()); range.setPosition(sourceText.indexOf("value")); range.setPosition(range.position()+5,QTextCursor::KeepAnchor);
        const auto target=blocks->insert("text",range,"Existing ranged note");
        QVERIFY(manager.detachAssociatedBlock(document,moving)); tabs.setCurrentWidget(code);
        QTRY_VERIFY(!code->syntaxDocument()->hasPendingEdits());
        if(folded) QVERIFY(code->toggleFoldAtLineForTest(1));
        QCoreApplication::processEvents();
        QFrame* shell=nullptr;
        QTRY_VERIFY(([&] {
            for(auto* frame:code->findChildren<QFrame*>("editorBlockShell")) if(frame->property("blockId")==target) shell=frame;
            return shell && shell->isVisible();
        })());
        if (folded) {
            auto* body=shell->findChild<QTextEdit*>("editorBlockText"); QVERIFY(body);
            QTRY_VERIFY(!body->isVisible());
        }
        auto* gutter=code->findChild<QWidget*>("editorLineNumberGutter"); QVERIFY(gutter);
        auto* drag=manager.findChild<AssociatedBlockDragController*>(); QVERIFY(drag);
        auto mime=std::unique_ptr<QMimeData>(drag->createMimeData(document,moving));
        auto point=gutter->mapFromGlobal(shell->mapToGlobal(QPoint(0,2))); point.setX(gutter->width()/2);
        qInfo()<<"DROP_PROJECTION"<<folded<<code->devicePixelRatioF()<<shell->geometry()<<point;
        const auto before=blocks->serialize();
        QDragEnterEvent enter(point,Qt::MoveAction,mime.get(),Qt::LeftButton,Qt::NoModifier); QApplication::sendEvent(gutter,&enter); QVERIFY(enter.isAccepted());
        auto* marker=code->findChild<QLabel*>("associatedBlockDropMarker"); QVERIFY(marker);
        QCOMPARE(marker->property("sourceLine").toInt(),2); QCOMPARE(marker->property("beforeBlockId").toString(),target);
        QCOMPARE(blocks->serialize(),before);
        QDropEvent drop(QPointF(point),Qt::MoveAction,mime.get(),Qt::LeftButton,Qt::NoModifier); QApplication::sendEvent(gutter,&drop); QVERIFY(drop.isAccepted());
        QTRY_VERIFY(manager.associatedBlockPages().isEmpty()); QCOMPARE(code->toPlainText(),sourceText);
        QCOMPARE(blocks->find(moving)->start,code->document()->findBlockByNumber(2).position());
        QVERIFY(blocks->find(moving)->order<blocks->find(target)->order);
        code->undo(); QTRY_COMPARE(manager.associatedBlockPages().size(),1); QVERIFY(blocks->find(moving)->detached);
        manager.createNewTab(); auto* other=manager.getCurrentEditor(); other->setPlainText("other owner\n");
        QVERIFY(other!=code); QVERIFY(manager.sharedDocumentForEditor(other)!=document);
        auto* otherGutter=other->findChild<QWidget*>("editorLineNumberGutter"); QVERIFY(otherGutter);
        QTRY_VERIFY(otherGutter->isVisible());
        mime.reset(drag->createMimeData(document,moving));
        const auto original=blocks->serialize(),receiving=other->blockModel()->serialize();
        QDragEnterEvent crossFile(QPoint(5,5),Qt::MoveAction,mime.get(),Qt::LeftButton,Qt::NoModifier); QApplication::sendEvent(otherGutter,&crossFile);
        QVERIFY(crossFile.isAccepted()); QVERIFY(blocks->find(moving)->detached);
        QCOMPARE(blocks->serialize(),original); QCOMPARE(other->blockModel()->serialize(),receiving);
        QDropEvent transferred(QPointF(5,5),Qt::MoveAction,mime.get(),Qt::LeftButton,Qt::NoModifier); QApplication::sendEvent(otherGutter,&transferred);
        QVERIFY(transferred.isAccepted()); QVERIFY(!blocks->find(moving)); QVERIFY(other->blockModel()->find(moving));
        other->undo(); QCOMPARE(blocks->serialize(),original); QCOMPARE(other->blockModel()->serialize(),receiving);
    }
    void gutterAutoScrollDoesNotCommitOnHoverOrLeave() {
        QTabWidget tabs; tabs.resize(500,350); tabs.show(); TabManager manager(&tabs); manager.createNewTab();
        auto* code=manager.getCurrentEditor(); code->setPlainText(QString("logic value;\n").repeated(200));
        auto* document=manager.sharedDocumentForEditor(code); auto* blocks=code->blockModel();
        const auto id=blocks->insert("text",code->textCursor(),"scroll me"); QVERIFY(manager.detachAssociatedBlock(document,id));
        tabs.setCurrentWidget(code); QCoreApplication::processEvents();
        auto* drag=manager.findChild<AssociatedBlockDragController*>(); auto* gutter=code->findChild<QWidget*>("editorLineNumberGutter");
        QVERIFY(drag && gutter); auto mime=std::unique_ptr<QMimeData>(drag->createMimeData(document,id));
        const auto before=blocks->serialize(); const int scroll=code->verticalScrollBar()->value();
        auto point=gutter->mapFromGlobal(code->viewport()->mapToGlobal(QPoint(0,code->viewport()->height()-5))); point.setX(gutter->width()/2);
        QDragEnterEvent enter(point,Qt::MoveAction,mime.get(),Qt::LeftButton,Qt::NoModifier); QApplication::sendEvent(gutter,&enter); QVERIFY(enter.isAccepted());
        QTRY_VERIFY(code->verticalScrollBar()->value()>scroll); QCOMPARE(blocks->serialize(),before);
        QDragLeaveEvent leave; QApplication::sendEvent(gutter,&leave); const int stopped=code->verticalScrollBar()->value();
        QTest::qWait(100); QCOMPARE(code->verticalScrollBar()->value(),stopped); QCOMPARE(blocks->serialize(),before);
    }
    void detachedSessionAndUntitledRecoveryKeepIdentity() {
        QTabWidget tabs; TabManager manager(&tabs); manager.setWorkspaceScope({directory->path()},directory->path());
        manager.setCrashRecoveryService(std::make_unique<CrashRecoveryService>(directory->filePath("session-recovery")));
        manager.createNewTab(); auto* code=manager.getCurrentEditor(); code->setPlainText("module untitled;\nendmodule\n");
        auto* document=manager.sharedDocumentForEditor(code); const auto documentId=document->documentId();
        auto* blocks=code->blockModel(); const auto id=blocks->insert("text",code->textCursor(),"Unsaved association");
        auto value=*blocks->find(id); value.appearance={{"style","Outlined"},{"textColor","#784321"}}; QVERIFY(blocks->update(value));
        QVERIFY(manager.detachAssociatedBlock(document,id)); manager.closeTab(tabs.indexOf(code)); QCOMPARE(document->viewCount(),0);
        WorkspaceSessionState state; state.workspaceRoot=directory->path(); state.tabs=manager.workspaceSessionTabs(directory->path());
        QCOMPARE(state.tabs.size(),1); QCOMPARE(state.tabs.first().kind,QString("associatedBlock")); QVERIFY(state.tabs.first().filePath.isEmpty());
        WorkspaceSessionStateService service(directory->filePath("sessions.ini")); QVERIFY(service.save(state).saved);
        const auto restored=service.load(directory->path()); QVERIFY(restored.loaded); QCOMPARE(restored.state.tabs.size(),1);
        QTabWidget otherTabs; TabManager other(&otherTabs); other.setWorkspaceScope({directory->path()},directory->path());
        QStringList skipped; other.restoreWorkspaceSessionTabs(directory->path(),restored.state.tabs,&skipped); QVERIFY(skipped.isEmpty());
        auto* page=other.currentAssociatedBlock(); QVERIFY(page); QCOMPARE(page->ownerDocument()->documentId(),documentId);
        QCOMPARE(page->ownerDocument()->viewCount(),0); QCOMPARE(page->blockId(),id);
        QCOMPARE(page->model()->find(id)->text,QString("Unsaved association")); QCOMPARE(page->model()->find(id)->appearance,value.appearance);
        QCOMPARE(page->ownerDocument()->textDocument()->toPlainText(),QString("module untitled;\nendmodule\n"));
        other.unsavedDocumentManagerForTesting()->setDecisionProvider([](const auto&,QWidget*) { return UnsavedDocumentBatchDecision::DiscardAll; });
        other.closeTab(otherTabs.indexOf(page)); QVERIFY(other.associatedBlockPages().isEmpty());
        QVERIFY(other.reopenClosedTab()); QCOMPARE(other.currentAssociatedBlock()->blockId(),id);
    }
    void missingOwnerSessionIsRetainedForRetry() {
        QTabWidget tabs; TabManager manager(&tabs); manager.setWorkspaceScope({directory->path()},directory->path());
        QVERIFY(manager.openFileInTab(source)); auto* code=manager.getCurrentEditor(); auto* document=manager.sharedDocumentForEditor(code);
        const auto id=code->blockModel()->insert("text",code->textCursor(),"Recover me"); QVERIFY(manager.detachAssociatedBlock(document,id));
        QVERIFY(manager.saveCurrentTab()); const auto states=manager.workspaceSessionTabs(directory->path());
        const auto retained=read(source); QVERIFY(QFile::remove(source));
        QTabWidget otherTabs; TabManager other(&otherTabs); other.setWorkspaceScope({directory->path()},directory->path());
        QStringList skipped; other.restoreWorkspaceSessionTabs(directory->path(),states,&skipped); QVERIFY(!skipped.isEmpty());
        QVERIFY(other.associatedBlockPages().isEmpty());
        const auto retry=other.workspaceSessionTabs(directory->path()); QCOMPARE(retry.size(),1); QCOMPARE(retry.first().viewState.value("blockId").toString(),id);
        QVERIFY(write(source,retained)); skipped.clear(); other.restoreWorkspaceSessionTabs(directory->path(),retry,&skipped);
        QVERIFY(skipped.isEmpty()); QVERIFY(other.currentAssociatedBlock()); QCOMPARE(other.currentAssociatedBlock()->blockId(),id);
    }

    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void init() {
        directory = std::make_unique<QTemporaryDir>(); QVERIFY(directory->isValid());
        source = directory->filePath("rtl/unit.sv"); QVERIFY(write(source,"alpha\nbeta\n"));
        editor = std::make_unique<MyCodeEditor>(); editor->setPlainText("alpha\nbeta\n"); editor->document()->setModified(false);
        error.clear(); QVERIFY(store()->bind(source,directory->path(),&error));
    }
    void cleanup() { editor.reset(); directory.reset(); }
    void referencePickerReportsCapabilityAndRejectsStaleReply() {
        QList<PinloomHostClient::RawReplyHandler> replies;
        PinloomHostClient client([&](const QJsonObject& request,const auto& reply) {
            if(request["method"]=="search") reply({{"entries",QJsonArray{}}},{});
            else replies.append(reply);
        });
        PinloomReferencePicker picker(&client,"pinloom://entry/delayed",editor.get());
        auto* link=picker.findChild<QLineEdit*>("referencePickerLink");
        auto* insert=picker.findChild<QPushButton*>("referencePickerInsert");
        auto* capability=picker.findChild<QLabel*>("referencePickerCapability"); QVERIFY(link && insert && capability);
        QVERIFY(!insert->isEnabled()); QCOMPARE(replies.size(),1);
        link->setText("https://example.invalid/reference"); QVERIFY(insert->isEnabled());
        QVERIFY(capability->text().contains("Link only")); QCOMPARE(picker.selectedUri(),link->text());
        replies.takeFirst()({{"entry",QJsonObject{{"identity",QJsonObject{{"entryId","delayed"}}},{"type","code"},{"title","old"},{"uri","pinloom://entry/delayed"}}},{"content","old preview"}},{});
        QCoreApplication::processEvents(); QCOMPARE(picker.selectedUri(),link->text()); QVERIFY(capability->text().contains("Link only"));
        const auto code=editor->toPlainText(); const auto revision=model()->revision(); picker.reject();
        QCOMPARE(editor->toPlainText(),code); QCOMPARE(model()->revision(),revision); QVERIFY(model()->items().isEmpty());
    }
    void referencePickerUsesExistingSearchAndStableIdentity() {
        const QJsonObject identity{{"entryId","entry"},{"anchorId","anchor"}};
        const QJsonObject entry{{"identity",identity},{"type","code"},{"title","Code location"},{"uri","pinloom://entry/entry?anchor=anchor"}};
        QStringList methods;
        PinloomHostClient client([&](const QJsonObject& request,const auto& reply) {
            methods.append(request["method"].toString());
            if(request["method"]=="search") reply({{"entries",QJsonArray{entry}}},{});
            else if(request["method"]=="resolve") reply({{"entry",entry},{"content","logic value;"},{"contentType","text/plain"}},{});
        });
        PinloomReferencePicker picker(&client,{},editor.get()); picker.show(); QCoreApplication::processEvents();
        auto* view=picker.findChild<PinloomContextView*>(); QVERIFY(view);
        QTRY_COMPARE(view->previewState(),QString("preview"));
        auto* insert=picker.findChild<QPushButton*>("referencePickerInsert"); QVERIFY(insert->isEnabled());
        QCOMPARE(PinloomHostIdentity::fromUri(QUrl(picker.selectedUri())).toJson(),PinloomHostIdentity::fromJson(identity).toJson());
        QVERIFY(methods.contains("search") && methods.contains("resolve")); QVERIFY(!methods.contains("open"));
        insert->click(); QCOMPARE(picker.result(),int(QDialog::Accepted));
    }
    void referencePickerUnavailableAndSourceChangeAreSafe() {
        PinloomHostClient offline([](const QJsonObject&,const auto& reply){reply({},"offline");});
        {
            PinloomReferencePicker picker(&offline,"pinloom://entry/missing",editor.get());
            QVERIFY(!picker.findChild<QPushButton*>("referencePickerInsert")->isEnabled());
            QVERIFY(picker.findChild<QLabel*>("referencePickerCapability")->text().contains("unavailable"));
        }
        editor->show(); bool handled=false;
        QTimer::singleShot(0,editor.get(),[&] {
            auto* dialog=editor->findChild<PinloomReferencePicker*>(); if(!dialog)return;
            dialog->findChild<QTabWidget*>("referencePickerMode")->setCurrentIndex(1);
            dialog->findChild<QLineEdit*>("referencePickerLink")->setText("https://example.invalid/reference");
            QTextCursor change(editor->document()); change.insertText("changed ");
            handled=true; dialog->findChild<QPushButton*>("referencePickerInsert")->click();
        });
        editor->editorBlockInsertRequested({{"blockType","reference"}},editor->textCursor());
        QVERIFY(handled); QVERIFY(model()->items().isEmpty()); QVERIFY(editor->toPlainText().startsWith("changed "));
    }
    void referencePickerNeverExecutesClipInsertion() {
        const QJsonObject identity{{"entryId","clip:review"},{"clipId","review"}};
        const QJsonObject entry{{"identity",identity},{"type","clip"},{"title","Review note"},{"uri","pinloom://entry/clip:review?clip=review"}};
        int opens=0;
        PinloomHostClient client([&](const QJsonObject& request,const auto& reply) {
            if(request["method"]=="search") reply({{"entries",QJsonArray{entry}}},{});
            else if(request["method"]=="resolve") reply({{"entry",entry},{"content","Reference text"}},{});
            else { ++opens; reply({{"message","inserted"}},{}); }
        });
        PinloomReferencePicker picker(&client,{},editor.get()); picker.show();
        auto* view=picker.findChild<PinloomContextView*>(); QVERIFY(view);
        QTRY_COMPARE(view->previewState(),QString("preview"));
        view->openButton()->click(); QCoreApplication::processEvents();
        QCOMPARE(opens,0);
        QVERIFY(!view->openButton()->isEnabled());
        QVERIFY(view->openButton()->toolTip().contains("no available original source"));
        QVERIFY(picker.findChild<QPushButton*>("referencePickerInsert")->isEnabled());
        QCOMPARE(editor->toPlainText(),QString("alpha\nbeta\n"));
    }
    void referencePickerRequiresVerifiedPdfLocation() {
        const QJsonObject identity{{"entryId","anchor:review"},{"resourceId","pdf"},{"anchorId","region"}};
        const QJsonObject entry{{"identity",identity},{"type","anchor"},{"title","Review PDF"},{"uri","pinloom://entry/anchor:review?resource=pdf&anchor=region"}};
        int opens=0;
        PinloomHostClient client([&](const QJsonObject& request,const auto& reply) {
            if(request["method"]=="search") reply({{"entries",QJsonArray{entry}}},{});
            else if(request["method"]=="resolve") reply({{"entry",entry},{"content","PDF region"},{"details",QJsonObject{{"locatorType","sumatrapdf.rect"}}}},{});
            else { ++opens; reply({{"state","open_requested"},{"message","accepted"}},{}); }
        });
        PinloomReferencePicker picker(&client,{},editor.get()); picker.show();
        auto* view=picker.findChild<PinloomContextView*>(); QVERIFY(view);
        QTRY_COMPARE(view->previewState(),QString("preview"));
        QVERIFY(view->openButton()->isEnabled()); view->openButton()->click();
        QTRY_COMPARE(opens,1);
        QVERIFY(view->statusText().contains("did not confirm the PDF location"));
    }
    void referencePickerShowsResultsAndReadablePreview() {
        const auto entry=[](const QString& id) {
            return QJsonObject{{"identity",QJsonObject{{"entryId",id}}},{"type","code"},{"title",id},{"uri","pinloom://entry/"+id}};
        };
        const auto first=entry("first"), second=entry("second");
        PinloomHostClient client([&](const QJsonObject& request,const auto& reply) {
            if(request["method"]=="search") reply({{"entries",QJsonArray{first,second}}},{});
            else reply({{"entry",first},{"content","Reference preview"}},{});
        });
        PinloomReferencePicker picker(&client,{},editor.get()); picker.show();
        auto* view=picker.findChild<PinloomContextView*>(); QVERIFY(view);
        QTRY_COMPARE(view->previewState(),QString("preview"));
        auto* list=view->resultList(); QTRY_COMPARE(list->count(),2);
        QTRY_VERIFY(list->viewport()->rect().contains(list->visualItemRect(list->item(1))));
        QVERIFY(view->previewEditor()->height()>=view->fontMetrics().height()*6);
    }
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
        preview.request(unsupported,[&](const auto& result){if(!result.loading){QVERIFY(result.status.isEmpty()); QVERIFY(result.text.contains(other.uri)); QVERIFY(result.text.contains("Link only")); QVERIFY(result.image.isNull()); ++completed;}}); QCOMPARE(completed,1);
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
        QString providerError;
        QObject owner; PinloomHostClient client([&](const auto&,const auto& reply){reply(response,providerError);},&owner);
        EditorAssociatedBlock block; block.type="reference"; block.uri="pinloom://entry/preview-entry"; const auto id=model()->insert(block,editor->textCursor());
        EditorBlockPreview preview(model(),&client,&owner); EditorBlockPreviewResult result; bool done=false;
        auto receive=[&](const auto& value){if(!value.loading){result=value;done=true;}};
        preview.request(id,receive); QTRY_VERIFY(done); QCOMPARE(result.text,QString("module preview; endmodule")); QVERIFY2(result.status.isEmpty(),qPrintable(result.status));
        response["preview"]=QJsonObject{{"kind","image"},{"state","ready"},{"mimeType","image/png"},{"filePath",png},{"byteSize",int(QFileInfo(png).size())},{"pixelWidth",90},{"pixelHeight",120},{"page",1}};
        entry["type"]="pdf"; response["entry"]=entry; response["content"]=""; done=false;
        QElapsedTimer timer; timer.start(); preview.request(id,receive); QTRY_VERIFY(done); const auto previewNs=timer.nsecsElapsed();
        QVERIFY2(result.status.isEmpty(),qPrintable(result.status)); QCOMPARE(result.image.size(),QSize(90,120));
        qInfo()<<"PREVIEW_PERF provider_png_ns="<<previewNs;
        response.remove("preview"); response["content"]=" \n "; done=false;
        preview.request(id,receive); QTRY_VERIFY(done); QVERIFY(result.text.isEmpty()); QVERIFY(result.image.isNull()); QVERIFY(result.status.startsWith("Link only"));
        providerError="provider offline"; done=false; preview.request(id,receive); QTRY_VERIFY(done);
        QCOMPARE(result.status,providerError); QVERIFY(!result.status.contains("Link only")); providerError.clear();
        entry["deleted"]=true; response["entry"]=entry; done=false; preview.request(id,receive); QTRY_VERIFY(done); QVERIFY(result.status.contains("deleted"));
        const auto before=read(source); const auto modified=QFileInfo(source).lastModified(); QVERIFY(save()); QCOMPARE(read(source),before); QCOMPARE(QFileInfo(source).lastModified(),modified);
    }
    void referenceOpenUsesActualLocatorAndRejectsStaleReply() {
        QJsonObject entry{{"identity",QJsonObject{{"entryId","open-entry"}}},{"uri","pinloom://entry/open-entry"},{"type","file"},{"title","Original target"}};
        QJsonObject document{{"entry",entry}};
        QJsonObject opened{{"message","Opened target"},{"state","open_requested"},{"positionVerified",false}};
        QString openError; int openCount=0; bool delay=false;
        PinloomHostClient::RawReplyHandler pending;
        QObject owner;
        PinloomHostClient client([&](const QJsonObject& request,const auto& reply) {
            if (request.value("method")=="resolve") { if(delay) pending=reply; else reply(document,{}); }
            else if (request.value("method")=="open") { ++openCount; reply(opened,openError); }
        },&owner);
        EditorAssociatedBlock block; block.type="reference"; block.uri="pinloom://entry/open-entry";
        const auto id=model()->insert(block,editor->textCursor());
        bool done=false,ok=false; QString message;
        auto request=[&] { done=false; EditorBlockPreview::openReference(model(),&client,id,&owner,[&](bool value,const QString& text) { done=true;ok=value;message=text; }); };
        request(); QTRY_VERIFY(done); QVERIFY(ok); QCOMPARE(message,QString("Opened target")); QCOMPARE(openCount,1);
        document["details"]=QJsonObject{{"locatorType","file.line"}};
        request(); QTRY_VERIFY(done); QVERIFY(ok); QCOMPARE(openCount,2);
        document["details"]=QJsonObject{{"locatorType","sumatrapdf.rect"}};
        request(); QTRY_VERIFY(done); QVERIFY(!ok); QVERIFY(message.contains("did not confirm")); QCOMPARE(openCount,3);
        opened["positionVerified"]=true; opened["state"]="located";
        request(); QTRY_VERIFY(done); QVERIFY(ok); QCOMPARE(openCount,4);
        openError="No valid foreground insertion target";
        request(); QTRY_VERIFY(done); QVERIFY(!ok); QCOMPARE(message,openError); QCOMPARE(openCount,5);
        openError.clear(); entry["deleted"]=true; document["entry"]=entry;
        request(); QTRY_VERIFY(done); QVERIFY(!ok); QVERIFY(message.contains("deleted")); QCOMPARE(openCount,5);
        entry["deleted"]=false; document["entry"]=entry; delay=true;
        request(); QTRY_VERIFY(bool(pending)); QVERIFY(model()->remove(id)); pending(document,{});
        QCoreApplication::processEvents(); QVERIFY(!done); QCOMPARE(openCount,5);
    }
    void clipOriginalNeverUsesInsertionAction() {
        QJsonObject entry{{"identity",QJsonObject{{"entryId","clip:original-clip"},{"clipId","original-clip"}}},
            {"uri","pinloom://entry/clip:original-clip?clip=original-clip"},{"type","clip"},{"title","Read-only clip"}};
        QJsonObject document{{"entry",entry},{"content","Clip preview"},{"details",QJsonObject{{"sourceUri",""}}}};
        QString providerError; int openCount=0; bool delay=false;
        PinloomHostClient::RawReplyHandler pending;
        QObject owner;
        PinloomHostClient client([&](const QJsonObject& request,const auto& reply) {
            if(request.value("method")=="resolve") { if(delay) pending=reply; else reply(document,providerError); }
            else if(request.value("method")=="open") { ++openCount; reply({},{}); }
        },&owner);
        EditorAssociatedBlock block; block.type="reference"; block.uri=entry.value("uri").toString();
        const auto id=model()->insert(block,editor->textCursor());
        bool done=false,ok=false; QString message;
        auto request=[&] { done=false; EditorBlockPreview::openReference(model(),&client,id,&owner,[&](bool value,const QString& text) { done=true;ok=value;message=text; }); };
        QDesktopServices::setUrlHandler("https",this,"captureOriginalUrl");
        QDesktopServices::setUrlHandler("file",this,"captureOriginalUrl");
        const auto restore=qScopeGuard([] { QDesktopServices::unsetUrlHandler("https"); QDesktopServices::unsetUrlHandler("file"); });
        originalUrl.clear(); request(); QTRY_VERIFY(done); QVERIFY(!ok); QVERIFY(message.contains("no available original"));
        QCOMPARE(openCount,0); QVERIFY(originalUrl.isEmpty());
        for(const auto& url:{QUrl("https://example.invalid/original#section"),QUrl::fromLocalFile(source)}) {
            document["details"]=QJsonObject{{"sourceUri",url.toString()}};
            request(); QTRY_VERIFY(done); QVERIFY2(ok,qPrintable(message)); QCOMPARE(originalUrl,url); QCOMPARE(openCount,0);
        }
        for(const auto& unavailable:{QString(),QString("pinloom://entry/clip:other?clip=other"),QString("javascript:void(0)"),QString("https:///missing-host"),QString("relative.txt")}) {
            document["details"]=QJsonObject{{"sourceUri",unavailable}}; originalUrl.clear();
            request(); QTRY_VERIFY(done); QVERIFY(!ok); QVERIFY(originalUrl.isEmpty()); QCOMPARE(openCount,0);
        }
        providerError="provider offline"; request(); QTRY_VERIFY(done); QVERIFY(!ok); QCOMPARE(message,providerError); providerError.clear();
        entry["deleted"]=true; document["entry"]=entry; request(); QTRY_VERIFY(done); QVERIFY(!ok); QVERIFY(message.contains("deleted"));
        entry["deleted"]=false; document["entry"]=entry; delay=true;
        request(); QTRY_VERIFY(bool(pending)); QVERIFY(model()->remove(id)); pending(document,{});
        QCoreApplication::processEvents(); QVERIFY(!done); QCOMPARE(openCount,0); QVERIFY(originalUrl.isEmpty());
    }
    void inlineAndDetachedOriginalRespectClipCapability() {
        originalUrl.clear(); QDesktopServices::setUrlHandler("https",this,"captureOriginalUrl");
        const auto restoreUrl=qScopeGuard([] { QDesktopServices::unsetUrlHandler("https"); });
        const auto previous=qgetenv("ZEROSLACK_PINLOOM_HOST_SERVER");
        const auto endpoint="zs-clip-original-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
        qputenv("ZEROSLACK_PINLOOM_HOST_SERVER",endpoint.toUtf8());
        const auto restore=qScopeGuard([previous] { if(previous.isNull()) qunsetenv("ZEROSLACK_PINLOOM_HOST_SERVER"); else qputenv("ZEROSLACK_PINLOOM_HOST_SERVER",previous); });
        QLocalServer server; QVERIFY(server.listen(endpoint));
        QJsonObject entry{{"identity",QJsonObject{{"entryId","clip:guarded-clip"},{"clipId","guarded-clip"}}},
            {"uri","pinloom://entry/clip:guarded-clip?clip=guarded-clip"},{"type","clip"},{"title","Guarded clip"}};
        QJsonObject document{{"entry",entry},{"content","Read-only clip content"},{"details",QJsonObject{{"sourceUri",""}}}};
        int opens=0, resolves=0;
        connect(&server,&QLocalServer::newConnection,&server,[&] {
            while(auto* socket=server.nextPendingConnection()) {
                connect(socket,&QLocalSocket::disconnected,socket,&QObject::deleteLater);
                const auto respond=[&,socket] {
                    if(!socket->canReadLine()) return;
                    const auto request=QJsonDocument::fromJson(socket->readLine()).object();
                    const auto method=request.value("method").toString();
                    if(method=="open") ++opens;
                    if(method=="resolve") ++resolves;
                    const QJsonObject response{{"protocol",PinloomHostClient::protocolName()},{"requestId",request.value("requestId")},
                        {"ok",true},{"result",method=="resolve" ? document : QJsonObject{}}};
                    socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact)+'\n'); socket->flush(); socket->disconnectFromServer();
                };
                connect(socket,&QLocalSocket::readyRead,socket,respond); respond();
            }
        });
        QTabWidget tabs; tabs.resize(850,650); tabs.show(); TabManager manager(&tabs);
        QVERIFY(manager.openFileInTab(source)); auto* code=manager.getCurrentEditor(); auto* shared=manager.sharedDocumentForEditor(code);
        EditorAssociatedBlock block; block.type="reference"; block.uri=entry.value("uri").toString();
        const auto id=code->blockModel()->insert(block,code->textCursor());
        QFrame* frame=nullptr; QTRY_VERIFY((frame=code->findChild<QFrame*>("editorBlockShell")));
        auto* original=frame->findChild<QAction*>("editorBlockOpenOriginal"); QVERIFY(original);
        auto* menu=qobject_cast<QMenu*>(original->parent()); QVERIFY(menu); QVERIFY(!original->isEnabled());
        QMetaObject::invokeMethod(menu,"aboutToShow",Qt::DirectConnection);
        QTRY_VERIFY(original->toolTip().contains("no available original")); QVERIFY(!original->isEnabled());
        auto* preview=frame->findChild<QTextEdit*>("editorBlockReferenceText"); QVERIFY(preview); QVERIFY(preview->isReadOnly());
        QTRY_COMPARE(preview->toPlainText(),QString("Read-only clip content"));
        QSignalSpy status(code,&MyCodeEditor::editorStatusMessageRequested);
        QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier); QApplication::sendEvent(frame,&enter);
        auto* content=static_cast<EditorBlockContent*>(frame->findChild<QWidget*>("editorBlockContent")); QVERIFY(content);
        QVERIFY(!content->isEditing());auto* edit=frame->findChild<QToolButton*>("blockEditToggle");QVERIFY(edit);QTest::mouseClick(edit,Qt::LeftButton);
        QVERIFY(content->isEditing()); QVERIFY(status.isEmpty()); QCOMPARE(opens,0);
        QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier); QApplication::sendEvent(preview,&escape); QVERIFY(content->isEditing());
        QTest::mouseClick(edit,Qt::LeftButton);QVERIFY(!content->isEditing());
        document["details"]=QJsonObject{{"sourceUri","https://example.invalid/original"}};
        QMetaObject::invokeMethod(menu,"aboutToShow",Qt::DirectConnection); QTRY_VERIFY(original->isEnabled());
        document["details"]=QJsonObject{{"sourceUri",""}}; const auto before=status.count(); original->trigger();
        QTRY_COMPARE(status.count(),before+1); QVERIFY(status.last().first().toString().contains("no available original")); QCOMPARE(opens,0);
        auto* page=manager.detachAssociatedBlock(shared,id); QVERIFY(page);
        original=page->findChild<QAction*>("editorBlockOpenOriginal"); QVERIFY(original);
        menu=qobject_cast<QMenu*>(original->parent()); QVERIFY(menu);
        QMetaObject::invokeMethod(menu,"aboutToShow",Qt::DirectConnection);
        QTRY_VERIFY(original->toolTip().contains("no available original")); QVERIFY(!original->isEnabled());
        preview=page->findChild<QTextEdit*>("editorBlockReferenceText"); QVERIFY(preview); QVERIFY(preview->isReadOnly());
        QTRY_COMPARE(preview->toPlainText(),QString("Read-only clip content"));
        document["details"]=QJsonObject{{"sourceUri","https://example.invalid/original"}};
        QMetaObject::invokeMethod(menu,"aboutToShow",Qt::DirectConnection); QTRY_VERIFY(original->isEnabled());
        document["details"]=QJsonObject{{"sourceUri",""}}; original->trigger();
        const auto reasonVisible=[page] { for(auto* label:page->findChildren<QLabel*>()) if(label->isVisible() && label->text().contains("no available original")) return true; return false; };
        QTRY_VERIFY(reasonVisible()); QCOMPARE(opens,0); QVERIFY(originalUrl.isEmpty()); QVERIFY(resolves>=6); QCOMPARE(read(source),QByteArray("alpha\nbeta\n"));
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
