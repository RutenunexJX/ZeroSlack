#include "mycodeeditor.h"
#include "ordinarytemplateplan.h"
#include "usertemplateservice.h"
#include "documenttemplatemodel.h"
#include "templateinputservice.h"
#include "semanticindexsnapshot.h"
#include "slangmanager.h"
#include "tsdocument.h"
#include "formatterservice.h"
#include "mainwindow.h"
#include "tabmanager.h"
#include <QScopeGuard>
#include <QtTest>
#include <QtWidgets>

class SlashTemplateTest : public QObject {
    Q_OBJECT
    QTemporaryDir root;
    std::unique_ptr<MyCodeEditor> editor;
    QLineEdit* search() const { return editor->findChild<QLineEdit*>("editorSlashQuery"); }
    QFrame* popup() const { return editor->findChild<QFrame*>("editorSlashPopup"); }
    void open(const QString& text = "") {
        QTest::keyClicks(editor.get(), "/");
        QVERIFY(popup()->isVisible());
        search()->setText(text);
    }
    static UserTemplateRecord resource(const QString& id, const QString& body) {
        UserTemplateRecord record;
        record.id = id; record.commandToken = "/" + id; record.label = id;
        record.insertText = body; record.command = {{"kind", "snippet"}};
        return record;
    }
private slots:
    void init() {
        QVERIFY(root.isValid());
        auto* service = UserTemplateService::getInstance();
        service->setGlobalTemplateFilePath(root.filePath(QString::number(QRandomGenerator::global()->generate()) + ".json"));
        service->setWorkspaceTemplateFilePath({}); service->reload();
        editor = std::make_unique<MyCodeEditor>();
        editor->setDocumentFileName(root.filePath("design.sv"));
        editor->resize(740, 420); editor->show(); editor->setFocus();
        editor->setPlainText("module m;\nlogic clk;\nendmodule\n");
        editor->document()->setModified(false);
        QTextCursor cursor(editor->document()); cursor.setPosition(10); editor->setTextCursor(cursor);
    }
    void cleanup() { editor.reset(); }
    void transientAtEveryPosition_data() {
        QTest::addColumn<QString>("source"); QTest::addColumn<int>("position");
        QTest::newRow("expression") << QString("x = a + b;") << 6;
        QTest::newRow("string") << QString("$display(\"path\");") << 11;
        QTest::newRow("path") << QString("// C:/rtl/top.sv") << 10;
        QTest::newRow("empty") << QString() << 0;
    }
    void transientAtEveryPosition() {
        QFETCH(QString, source); QFETCH(int, position);
        editor->setPlainText(source); editor->document()->setModified(false);
        QTextCursor cursor(editor->document()); cursor.setPosition(position); editor->setTextCursor(cursor);
        const auto revision = editor->semanticDocumentRevision(); const auto undo = editor->document()->availableUndoSteps();
        open("no such resource");
        QTest::keyClick(search(), Qt::Key_Return);
        QVERIFY(popup()->isVisible()); QCOMPARE(editor->toPlainText(), source);
        QCOMPARE(editor->semanticDocumentRevision(), revision); QCOMPARE(editor->document()->availableUndoSteps(), undo);
        QVERIFY(!editor->document()->isModified());
        QTest::keyClick(search(), Qt::Key_Escape); QCOMPARE(editor->textCursor().position(), position);
        QVERIFY(!popup()->isVisible());
    }
    void literalEscape_data() {
        QTest::addColumn<QString>("next");
        QTest::newRow("space") << QString(" "); QTest::newRow("line-comment") << QString("/"); QTest::newRow("block-comment") << QString("*");
    }
    void literalEscape() {
        QFETCH(QString, next); const auto before = editor->toPlainText(); const auto position = editor->textCursor().position();
        open(); QTest::keyClicks(search(), next);
        QVERIFY(!popup()->isVisible()); QString expected = before; expected.insert(position, "/" + next); QCOMPARE(editor->toPlainText(), expected);
        editor->undo(); QCOMPARE(editor->toPlainText(), before); editor->redo(); QCOMPARE(editor->toPlainText(), expected);
    }
    void cancelSelectionAndNoImplicitAccept() {
        QTextCursor cursor(editor->document()); cursor.setPosition(0); cursor.setPosition(6, QTextCursor::KeepAnchor); editor->setTextCursor(cursor);
        const auto text = editor->toPlainText(); open("clocked process");
        QCOMPARE(editor->toPlainText(), text); QCOMPARE(editor->textCursor(), cursor);
        QTest::keyClick(search(), Qt::Key_Escape); QCOMPARE(editor->textCursor(), cursor);
        open("clocked process"); QTest::keyClick(search(), Qt::Key_Return);
        QCOMPARE(editor->toPlainText(), text); QVERIFY(popup()->isVisible());
    }
    void compositionAndAtomicUndo() {
        auto leaf = resource("leaf", "${count} + ${count}");
        leaf.command.insert("inputs", QVariantMap{{"count", QVariantMap{{"kind", "Value"}, {"default", "3"}, {"integer", true}}}});
        auto outer = resource("outer", "(${include:leaf})${cursor}");
        auto plan = OrdinaryTemplatePlanner::prepare(outer, {leaf, outer});
        QVERIFY2(plan.error.isEmpty(), qPrintable(plan.error)); QCOMPARE(plan.text, "(3 + 3)");
        const auto before = editor->toPlainText(); const auto start = editor->textCursor().position();
        QVERIFY(editor->insertOrdinaryTemplate(plan.text, plan.placeholders, plan.finalCursor));
        QCOMPARE(editor->templateModel()->instances().size(), 0);
        QTest::keyClicks(editor.get(), "12");
        QVERIFY(editor->toPlainText().contains("(12 + 12)"));
        QTest::keyClick(editor.get(), Qt::Key_Tab);
        QVERIFY(!editor->templateSlotModeActive()); QCOMPARE(editor->textCursor().position(), start + 9);
        editor->undo(); QCOMPARE(editor->toPlainText().mid(start, 7), QString("(1 + 1)"));
        editor->undo(); QVERIFY(editor->toPlainText().contains("(3 + 3)"));
        editor->undo(); QCOMPARE(editor->toPlainText(), before);
    }
    void wrapSelectionAndRejectCycles() {
        auto wrap = resource("wrap", "begin\n${body}\nend${cursor}"); wrap.command.insert("acceptsBody", true);
        auto plan = OrdinaryTemplatePlanner::prepare(wrap, {wrap}, "a = 1;");
        QVERIFY(plan.error.isEmpty()); QCOMPARE(plan.text, "begin\na = 1;\nend");
        auto a = resource("a", "${include:b}"); auto b = resource("b", "${include:a}");
        QVERIFY(!OrdinaryTemplatePlanner::prepare(a, {a, b}).error.isEmpty());
        QVERIFY(!OrdinaryTemplatePlanner::prepare(a, {a}).error.isEmpty());
        auto huge = resource("huge", QString(1024*1024+1,'x')); QVERIFY(!OrdinaryTemplatePlanner::prepare(huge, {huge}).error.isEmpty());
        auto duplicate = b; duplicate.commandToken = "/another"; duplicate.insertText = "other";
        QVERIFY(OrdinaryTemplatePlanner::prepare(a, {a,b,duplicate}).error.contains("Ambiguous"));
    }
    void hotReloadKeepsLastGoodGeneration() {
        auto* service = UserTemplateService::getInstance();
        const auto saved = service->setRecords({resource("hello", "hello")});
        QVERIFY2(saved.valid, qPrintable(saved.failureReason));
        const auto revision = service->catalogRevision();
        QFile file(service->globalTemplateLocation()); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("{half-written"); file.close();
        QVERIFY(!service->reload().valid); QCOMPARE(service->catalogRevision(), revision);
        QVERIFY(!service->records().isEmpty()); QCOMPARE(service->records().first().commandToken, "/hello");
        QVERIFY(service->setRecords({resource("hello", "new")}).valid);
        QVERIFY(service->catalogRevision() > revision);
    }
    void pasteLoadAndUndoDoNotOpen() {
        editor->insertPlainText("/process"); QVERIFY(!popup());
        editor->undo(); editor->redo(); QVERIFY(!popup());
        editor->setPlainText("/process"); QVERIFY(!popup());
        open(); QApplication::clipboard()->setText("clocked process"); QTest::keyClick(search(), Qt::Key_V, Qt::ControlModifier);
        QCOMPARE(editor->toPlainText(), "/process"); QCOMPARE(search()->text(), "clocked process");
    }
    void mainWindowQueryOwnsPaste() {
        MainWindow window; window.show(); window.activateWindow();
        window.tabManager->createNewTab(); auto* page=window.tabManager->getCurrentEditor(); QVERIFY(page);
        page->setPlainText("module own;\nendmodule\n"); page->setFocus(); QCoreApplication::processEvents();
        const auto source=page->toPlainText(); const auto undo=page->document()->availableUndoSteps();
        QVERIFY(!page->findChild<QFrame*>("editorSlashPopup"));
        QTest::keyClicks(page,"/");
        auto* query=page->findChild<QLineEdit*>("editorSlashQuery"); QVERIFY(query && query->isVisible());
        QTRY_VERIFY(query->hasFocus());
        QApplication::clipboard()->setText("clocked process"); QTest::keyClick(query,Qt::Key_V,Qt::ControlModifier);
        QCOMPARE(query->text(),QString("clocked process")); QVERIFY(query->isVisible());
        QCOMPARE(page->toPlainText(),source); QCOMPARE(page->document()->availableUndoSteps(),undo);
        QTest::keyClick(query,Qt::Key_Escape); page->document()->setModified(false);
    }
    void mainWindowLiteralInput_data() {
        QTest::addColumn<QString>("input"); QTest::addColumn<bool>("editorAddressed");
        QTest::newRow("space-focus") << QString("/ literal") << false;
        QTest::newRow("comment-focus") << QString("// comment") << false;
        QTest::newRow("block-comment-focus") << QString("/* comment") << false;
        QTest::newRow("space-queued") << QString("/ literal") << true;
        QTest::newRow("comment-queued") << QString("// comment") << true;
        QTest::newRow("block-comment-queued") << QString("/* comment") << true;
    }
    void mainWindowLiteralInput() {
        QFETCH(QString, input); QFETCH(bool, editorAddressed);
        MainWindow window; window.show(); window.activateWindow();
        window.tabManager->createNewTab(); auto* page = window.tabManager->getCurrentEditor(); QVERIFY(page);
        page->setPlainText("module input;\nendmodule\n");
        QTextCursor cursor(page->document()); cursor.setPosition(0); page->setTextCursor(cursor);
        page->setFocus(); QTRY_VERIFY(page->hasFocus());
        const auto before = page->toPlainText();
        for (const auto ch : input) {
            auto* target = editorAddressed ? page : QApplication::focusWidget();
            QVERIFY(target && target->isVisible());
            QTest::keyClicks(target, QString(ch)); QCoreApplication::processEvents();
        }
        QCOMPARE(page->toPlainText(), input + before);
        QTRY_VERIFY(page->hasFocus());
        auto* popup = page->findChild<QFrame*>("editorSlashPopup"); QVERIFY(popup && !popup->isVisible());
        page->document()->setModified(false);
    }
    void resourceDependenciesAndScopeIsolation() {
        auto* service = UserTemplateService::getInstance();
        auto leaf = resource("leaf", "original"); auto outer = resource("outer", "(${include:leaf})");
        QVERIFY(service->setRecords({leaf, outer}).valid);
        QFile file(service->globalTemplateLocation()); QVERIFY(file.open(QIODevice::ReadOnly));
        const auto before = file.readAll(); file.close();
        QVERIFY(!service->deleteRecord("leaf", false).valid);
        QVERIFY(!service->setRecords({outer}).valid);
        auto cyclic = leaf; cyclic.insertText = "${include:outer}";
        QVERIFY(!service->saveRecord(cyclic, false).valid);
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), before); file.close();
        const auto workspace = root.filePath("workspace-valid.json"); service->setWorkspaceTemplateFilePath(workspace);
        leaf.insertText = "local"; QVERIFY(service->saveRecord(leaf, true).valid);
        auto catalog = OrdinaryTemplatePlanner::catalog(service, editor->documentFileName());
        auto plan = OrdinaryTemplatePlanner::prepare(outer, catalog); QCOMPARE(plan.text, "(local)");
        QFile broken(root.filePath("workspace-broken.json")); QVERIFY(broken.open(QIODevice::WriteOnly)); broken.write("{"); broken.close();
        service->setWorkspaceTemplateFilePath(broken.fileName()); QVERIFY(!service->reload().valid);
        for (const auto& record : service->records()) QVERIFY(record.insertText != "local");
        service->setWorkspaceTemplateFilePath({}); QVERIFY(service->reload().valid);
    }
    void physicalTextInputsFrozenAndEditable() {
        auto value = resource("greeting", "${word} + ${word}${cursor}");
        value.command["inputs"] = QVariantMap{{"word", QVariantMap{{"kind","Text"},{"default","hello"}}}};
        auto* service = UserTemplateService::getInstance(); QVERIFY(service->setRecords({value}).valid);
        const auto before = editor->toPlainText(); open("greeting"); QTest::keyClick(search(), Qt::Key_Return);
        QVERIFY(!popup()->isVisible()); QVERIFY(editor->templateSlotModeActive());
        value.insertText = "changed resource"; value.command.remove("inputs"); QVERIFY(service->setRecords({value}).valid);
        QTest::keyClicks(editor.get(), "world"); QVERIFY(editor->toPlainText().contains("world + world"));
        QTest::keyClick(editor.get(), Qt::Key_Tab); QVERIFY(!editor->templateSlotModeActive());
        QTextCursor cursor(editor->document()); cursor.setPosition(0); editor->setTextCursor(cursor);
        QTest::keyClicks(editor.get(), "x"); QVERIFY(editor->toPlainText().startsWith("xmodule"));
        open("greeting"); QTest::keyClick(search(), Qt::Key_Return); QVERIFY(editor->toPlainText().contains("changed resource"));
        QVERIFY(editor->toPlainText() != before);
    }
    void physicalWrapAndCancelBoundaries() {
        editor->setPlainText("module m;\ninitial begin\na = 1;\nend\nendmodule\n");
        QTRY_VERIFY(!editor->syntaxDocument()->hasPendingEdits());
        QTextCursor cursor(editor->document()); cursor.setPosition(editor->toPlainText().indexOf("a ="));
        cursor.setPosition(cursor.position()+6,QTextCursor::KeepAnchor); editor->setTextCursor(cursor);
        const auto before = editor->toPlainText(); open("/wrap"); QTest::keyClick(search(),Qt::Key_Escape); QCOMPARE(editor->textCursor(),cursor);
        open("/wrap"); QTest::keyClick(search(),Qt::Key_Return); QVERIFY(!popup()->isVisible());
        QVERIFY(editor->toPlainText().contains("begin\n    a = 1;\nend")); editor->undo(); QCOMPARE(editor->toPlainText(),before);
        editor->redo(); QVERIFY(editor->toPlainText() != before);
        open(); QTest::keyClick(search(),Qt::Key_Backspace); QVERIFY(!popup()->isVisible());
        open(); QTest::keyClick(search(),Qt::Key_Tab); QVERIFY(!popup()->isVisible());
        open(); QTest::keyClick(search(),Qt::Key_Backtab); QVERIFY(!popup()->isVisible());
        open("/note"); QTextCursor external(editor->document()); external.insertText("// external\n");
        const auto changed = editor->toPlainText(); QVERIFY(!popup()->isVisible()); QCOMPARE(editor->toPlainText(),changed);
        open("/note"); editor->formatDocument(); QVERIFY(!popup()->isVisible());
    }
    void templateContextAndSemanticInputs() {
        const QString source = "module m;\nlogic clk;\n\nendmodule\n";
        editor->setPlainText(source); QTextCursor cursor(editor->document()); cursor.setPosition(source.indexOf("\n\n")+1); editor->setTextCursor(cursor);
        QTRY_VERIFY(!editor->syntaxDocument()->hasPendingEdits());
        SlangManager slang; const auto records = slang.extractOverlayWorkspaceSymbolRecords({{editor->documentFileName(), source}});
        auto* index = SemanticIndex::getInstance(); const auto previous = index->snapshot();
        const auto restore = qScopeGuard([&] { index->setSnapshot(previous); });
        index->setSnapshot(std::make_shared<const SemanticIndexSnapshot>(SemanticIndexSnapshot::fromSymbolRecords(records, {}, {}, {{editor->documentFileName(), source}})));
        open("clocked process"); QTest::keyClick(search(), Qt::Key_Return);
        QVERIFY(!popup()->isVisible()); QVERIFY(editor->toPlainText().contains("always_ff @(posedge clk) begin"));
        QVERIFY(editor->templateSlotModeActive()); QTest::keyClick(editor.get(),Qt::Key_Tab);
        QVERIFY(editor->textCursor().selectedText() != "clk");
        QTest::keyClick(editor.get(),Qt::Key_Escape); QVERIFY(!editor->templateSlotModeActive());
        editor->setPlainText("module m;\nstring p = \"path\";\nendmodule\n");
        cursor = QTextCursor(editor->document()); cursor.setPosition(editor->toPlainText().indexOf("path")+2); editor->setTextCursor(cursor);
        QTRY_VERIFY(!editor->syntaxDocument()->hasPendingEdits()); const auto unchanged = editor->toPlainText();
        open("clocked process"); QTest::keyClick(search(),Qt::Key_Return); QVERIFY(popup()->isVisible()); QCOMPARE(editor->toPlainText(),unchanged);
        QTest::keyClick(search(),Qt::Key_Escape);
    }
    void identityChangeCancels() {
        open("clocked process"); editor->setDocumentFileName(root.filePath("other.sv")); QVERIFY(!popup()->isVisible());
        open(); editor->setPlainText("different"); QVERIFY(!popup()->isVisible()); QCOMPARE(editor->toPlainText(), "different");
    }
    void imeCommitDoesNotInsert() {
        open(); const auto before = editor->toPlainText();
        QInputMethodEvent preedit("clock", {}); QApplication::sendEvent(search(), &preedit);
        QTest::keyClick(search(), Qt::Key_Return); QCOMPARE(editor->toPlainText(), before);
        QInputMethodEvent commit; commit.setCommitString("clocked process"); QApplication::sendEvent(search(), &commit);
        QTest::keyClick(search(), Qt::Key_Return); QCOMPARE(editor->toPlainText(), before);
        QVERIFY(popup()->isVisible());
    }
};
QTEST_MAIN(SlashTemplateTest)
#include "slash_template_test.moc"
