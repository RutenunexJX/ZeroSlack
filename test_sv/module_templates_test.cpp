#include "moduletemplateplan.h"
#include "ordinarytemplateplan.h"
#include "usertemplateservice.h"
#include "mycodeeditor.h"
#include "semanticindexsnapshot.h"
#include "slangmanager.h"
#include "templatemanagerdialog.h"
#include "automationservice.h"
#include "tsdocument.h"
#include <QScopeGuard>
#include <QSemaphore>
#include <QThreadPool>
#include <QtTest>
#include <QtWidgets>

class ModuleTemplatesTest : public QObject {
    Q_OBJECT
    enum class AfterB { Nothing, RepeatB, ConfirmC, HighlightC, FilterC, Escape, MoveCursor, Edit, Rename, Catalog, Workspace };
    QTemporaryDir root;
    QString file, text;
    SimulationCommandRequest source;
    QList<SemanticSymbolRecord> records;
    SemanticSymbolRecord module(const QString& name) const {
        for (const auto& record : records)
            if (record.name == name && record.declarationKind == SymbolTaxonomy::DeclarationKind::Module) return record;
        return {};
    }
    void setupSource(const QString& declarations) {
        text = declarations + "\nmodule target;\n    \nendmodule\n";
        source = {}; source.context.fileName = file; source.context.documentText = text;
        source.context.moduleName = "target"; source.context.cursorPosition = text.indexOf("    \n") + 4;
        source.contents = {{file, text}}; source.orderedFiles = {file};
        SlangManager slang; records = slang.extractSymbolRecords(file, text);
        SemanticIndex::getInstance()->setSnapshot(std::make_shared<const SemanticIndexSnapshot>(
            SemanticIndexSnapshot::fromSymbolRecords(records, {}, {}, {{file, text}})));
    }
    static UserTemplateRecord resource(const QString& mode) {
        UserTemplateRecord record;
        record.id = "test:" + mode; record.commandToken = '/' + mode; record.label = mode;
        record.command = {{"kind", "moduleDeclarations"}, {"mode", mode}, {"context", "module"}};
        return record;
    }
    QFrame* open(MyCodeEditor& editor, const QString& mode) {
        editor.activateWindow(); editor.setFocus();
        QTest::keyClicks(&editor, "/");
        auto* query = editor.findChild<QLineEdit*>("editorSlashQuery");
        if (!query) return nullptr;
        query->setText(mode); QTest::keyClick(query, Qt::Key_Return);
        return editor.findChild<QFrame*>(mode == "signals" ? "editorSlashPopup" : "moduleTemplatePopup");
    }
    void setupEditor(MyCodeEditor& editor) {
        editor.setDocumentFileName(file); editor.resize(850, 640); editor.setPlainText(text);
        QTextCursor cursor(editor.document()); cursor.setPosition(source.context.cursorPosition); editor.setTextCursor(cursor);
        editor.show(); editor.setFocus();
    }
private slots:
    void init() {
        QVERIFY(root.isValid()); file = root.filePath("design.sv");
        auto* service = UserTemplateService::getInstance(); service->setBundledTemplateFilePath({});
        service->setGlobalTemplateFilePath(root.filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + ".json"));
        service->setWorkspaceTemplateFilePath({}); QVERIFY(service->setRecords({resource("signals"), resource("parameters")}).valid);
        setupSource("module child #(parameter int WIDTH=8, DEPTH=4, localparam int ADDRESS=$clog2(DEPTH))\n"
            "(input logic clk, input logic signed [WIDTH-1:0] data [DEPTH], output logic [ADDRESS-1:0] addr, inout wire ready); endmodule\n");
    }
    void sourceTypesDimensionsAndDependencies() {
        const auto model = ModuleTemplatePlanner::inspect(source, module("child"));
        QVERIFY2(model.error.isEmpty(), qPrintable(model.error)); QCOMPARE(model.ports.size(), 4); QCOMPARE(model.parameters.size(), 3);
        ModuleTemplateOptions options;
        options.prefix = "dut_"; options.values.insert("WIDTH", "16");
        const auto plan = ModuleTemplatePlanner::prepare(model, options);
        QVERIFY2(plan.error.isEmpty(), qPrintable(plan.error));
        QVERIFY(plan.text.contains("localparam int WIDTH = 16;"));
        QVERIFY(plan.text.contains("localparam int ADDRESS = $clog2(DEPTH);"));
        QVERIFY(plan.text.contains("logic signed [WIDTH-1:0] data [DEPTH];"));
        QVERIFY(plan.text.contains("wire logic ready;")); QVERIFY(!plan.text.contains("dut_"));
        QCOMPARE(plan.requiredParameters, QStringList({"WIDTH", "DEPTH", "ADDRESS"}));
        const auto error = ModuleTemplatePlanner::validateInsertion(source, model, plan.text);
        QVERIFY2(error.isEmpty(), qPrintable(error));
    }
    void selectedParametersIncludeDependenciesAndKeepLocalDerived() {
        const auto model = ModuleTemplatePlanner::inspect(source, module("child"));
        ModuleTemplateOptions options; options.mode = "parameters"; options.selectedParameters = {"ADDRESS"}; options.parameterKeyword = "parameter";
        const auto plan = ModuleTemplatePlanner::prepare(model, options);
        QVERIFY2(plan.error.isEmpty(), qPrintable(plan.error));
        QCOMPARE(plan.text, QString("parameter int DEPTH = 4;\nlocalparam int ADDRESS = $clog2(DEPTH);\n"));
        options.values.insert("ADDRESS", "5"); QVERIFY(ModuleTemplatePlanner::prepare(model, options).error.contains("overridable"));
        options.values.clear(); options.selectedParameters.clear(); QVERIFY(!ModuleTemplatePlanner::prepare(model, options).error.isEmpty());
    }
    void nonAnsiAndTypeParameter() {
        setupSource("module child #(parameter type T=logic [7:0], parameter N=3)(a,b); input T a [N]; output signed [N:0] b; endmodule");
        const auto model = ModuleTemplatePlanner::inspect(source, module("child"));
        QVERIFY2(model.error.isEmpty(), qPrintable(model.error));
        ModuleTemplateOptions options; const auto plan = ModuleTemplatePlanner::prepare(model, options);
        QVERIFY2(plan.error.isEmpty(), qPrintable(plan.error));
        QVERIFY(plan.text.contains("localparam type T = logic [7:0];"));
        QVERIFY(plan.text.contains("T a [N];")); QVERIFY(plan.text.contains("logic signed [N:0] b;"));
        const auto error = ModuleTemplatePlanner::validateInsertion(source, model, plan.text);
        QVERIFY2(error.isEmpty(), qPrintable(error));
    }
    void conflictsInvalidValuesAndMissingDependencies() {
        const auto model = ModuleTemplatePlanner::inspect(source, module("child")); ModuleTemplateOptions options;
        QVERIFY(ModuleTemplatePlanner::prepare(model, options, {"data"}).error.isEmpty());
        options.mode="parameters"; options.selectedParameters={"WIDTH"};
        QVERIFY(ModuleTemplatePlanner::prepare(model, options, {"WIDTH"}).error.contains("WIDTH"));
        options.prefix = "a "; QVERIFY(!ModuleTemplatePlanner::prepare(model, options).error.isEmpty());
        options.mode="signals";
        options.prefix.clear(); options.values.insert("WIDTH", "missing_width");
        auto plan = ModuleTemplatePlanner::prepare(model, options);
        QVERIFY(!ModuleTemplatePlanner::validateInsertion(source, model, plan.text).isEmpty());
        options.values.clear(); options.includeParameters = false; plan = ModuleTemplatePlanner::prepare(model, options);
        QVERIFY(!ModuleTemplatePlanner::validateInsertion(source, model, plan.text).isEmpty());
        options.includeParameters = true; options.values.insert("DEPTH", "ADDRESS");
        QVERIFY(ModuleTemplatePlanner::prepare(model, options).error.contains("Cyclic"));
    }
    void identityDoesNotFallBackByName() {
        auto selected = module("child"); selected.location.fileName = root.filePath("other.sv");
        QVERIFY(!ModuleTemplatePlanner::inspect(source, selected).error.isEmpty());
    }
    void stringsAndPackageNamesAreNotRenamed() {
        setupSource("package p; parameter N=9; endpackage\nmodule child #(parameter N=4, parameter S=\"N\", parameter X=p::N)(); endmodule");
        const auto model = ModuleTemplatePlanner::inspect(source, module("child"));
        ModuleTemplateOptions options; options.mode="parameters"; options.prefix="tb_"; options.selectedParameters={"S","X"};
        const auto plan=ModuleTemplatePlanner::prepare(model,options);
        QVERIFY2(plan.error.isEmpty(),qPrintable(plan.error));
        QVERIFY(plan.text.contains("tb_S = \"N\"")); QVERIFY(plan.text.contains("tb_X = p::N")); QVERIFY(!plan.text.contains("tb_N"));
    }
    void structMembersAreNotRenamedWithParameters() {
        setupSource("module child #(parameter WIDTH=8)(input struct packed {logic [WIDTH-1:0] WIDTH;} packet); endmodule");
        const auto model=ModuleTemplatePlanner::inspect(source,module("child"));
        QVERIFY2(model.error.isEmpty(),qPrintable(model.error));
        ModuleTemplateOptions options; options.prefix="dut_";
        const auto plan=ModuleTemplatePlanner::prepare(model,options);
        QVERIFY2(plan.error.isEmpty(),qPrintable(plan.error));
        QVERIFY(plan.text.contains("[WIDTH-1:0] WIDTH;"));
        const auto error=ModuleTemplatePlanner::validateInsertion(source,model,plan.text);
        QVERIFY2(error.isEmpty(),qPrintable(error));
    }
    void interfacePortsDoNotBlockParameters() {
        setupSource("interface bus; logic data; endinterface\nmodule child #(parameter WIDTH=8)(bus b); endmodule");
        const auto model=ModuleTemplatePlanner::inspect(source,module("child"));
        ModuleTemplateOptions options; QVERIFY(!ModuleTemplatePlanner::prepare(model,options).error.isEmpty());
        options.mode="parameters"; options.selectedParameters={"WIDTH"};
        const auto plan=ModuleTemplatePlanner::prepare(model,options); QVERIFY2(plan.error.isEmpty(),qPrintable(plan.error));
        QVERIFY(plan.text.contains("WIDTH = 8"));
    }
    void resourcesValidateWithoutEmptyBodyOrComposition() {
        QVERIFY(OrdinaryTemplatePlanner::validate({resource("signals"), resource("parameters")}).isEmpty());
        auto invalid = resource("other"); QVERIFY(!OrdinaryTemplatePlanner::validate({invalid}).isEmpty());
        auto outer = resource("outer"); outer.command={{"kind","snippet"}}; outer.insertText="${include:test:signals}";
        QVERIFY(!OrdinaryTemplatePlanner::prepare(outer,{outer,resource("signals")}).error.isEmpty());
    }
    void cancellationAndExpressionBoundary() {
        auto request=source; request.cancelled=std::make_shared<std::atomic_bool>(true);
        const auto cancelled=ModuleTemplatePlanner::inspect(request,module("child"));
        QVERIFY(!cancelled.error.isEmpty());
        const auto model=ModuleTemplatePlanner::inspect(source,module("child"));
        ModuleTemplateOptions options; options.values.insert("WIDTH","16; logic unrelated; localparam y = 1");
        QVERIFY(!ModuleTemplatePlanner::prepare(model,options).error.isEmpty());
        options.values["WIDTH"]="16, unrelated = 2";
        QVERIFY(!ModuleTemplatePlanner::prepare(model,options).error.isEmpty());
    }
    void automationUsesSameGeneratorAndRequiresModule() {
        QFile saved(file); QVERIFY(saved.open(QIODevice::WriteOnly)); saved.write(text.toUtf8()); saved.close();
        ZeroSlackAutomationService api;
        QJsonObject request{{"command","document.inspect"},{"workspace",root.path()},{"file","design.sv"}};
        const auto inspected=api.execute(request); QVERIFY2(inspected.value("ok").toBool(),qPrintable(QJsonDocument(inspected).toJson()));
        request.insert("command","document.insert"); request.insert("expectedRevision",inspected.value("data").toObject().value("revision"));
        request.insert("start",source.context.cursorPosition); request.insert("dryRun",true); request.insert("templateId","user:signals");
        const auto missing=api.execute(request); QVERIFY(!missing.value("ok").toBool());
        request.insert("module","child"); request.insert("moduleFile","design.sv");
        request.insert("declarations",QJsonObject{{"prefix","api_"},{"values",QJsonObject{{"WIDTH","12"}}}});
        const auto preview=api.execute(request); QVERIFY2(preview.value("ok").toBool(),qPrintable(QJsonDocument(preview).toJson()));
        QVERIFY(preview.value("data").toObject().value("text").toString().contains("WIDTH = 12"));
        QVERIFY(!preview.value("data").toObject().value("text").toString().contains("api_"));
        QVERIFY(preview.value("data").toObject().value("instances").toArray().isEmpty());
        QVERIFY(saved.open(QIODevice::ReadOnly)); QCOMPARE(QString::fromUtf8(saved.readAll()),text);
    }
    void managerPreservesGenerationKind() {
        TemplateManagerDialog dialog(UserTemplateService::getInstance(),source,{},nullptr);
        auto* list=dialog.findChild<QListWidget*>("templateList"); auto* kind=dialog.findChild<QComboBox*>("templateKind");
        auto* scope=dialog.findChild<QComboBox*>("templateScope"); auto* buttons=dialog.findChild<QDialogButtonBox*>();
        QVERIFY(list && kind && scope && buttons); list->setCurrentRow(0); scope->setCurrentText("User global");
        QVERIFY(kind->currentText()=="Module signals" || kind->currentText()=="Module parameters");
        const auto mode=kind->currentText()=="Module signals" ? "signals" : "parameters";
        for (auto* button:buttons->buttons()) if (buttons->buttonRole(button)==QDialogButtonBox::AcceptRole) { button->click(); break; }
        bool found=false;
        for (const auto& record:UserTemplateService::getInstance()->records()) if (record.command.value("mode")==mode) {
            QVERIFY(ModuleTemplatePlanner::handles(record.command)); QVERIFY(record.insertText.isEmpty()); found=true;
        }
        QVERIFY(found);
    }
    void signalsChooseOncePreserveNamesAndUndo() {
        text.insert(source.context.cursorPosition-4,"    logic clk;\n");
        source.context.documentText=text; source.contents[file]=text;
        source.context.cursorPosition=text.indexOf("    \n")+4;
        MyCodeEditor editor; setupEditor(editor); QTest::qWait(100);
        auto* popup=open(editor,"signals"); QVERIFY(popup); QVERIFY(popup->isVisible());
        QVERIFY(!editor.findChild<QFrame*>("moduleTemplatePopup"));
        auto* search=popup->findChild<QLineEdit*>("editorSlashQuery");
        auto* list=popup->findChild<QListWidget*>("editorSlashCandidates");
        search->setText("child"); QCOMPARE(list->count(),1); QTest::qWait(180); QCOMPARE(editor.toPlainText(),text);
        QTest::keyClick(search,Qt::Key_Down); QTest::qWait(180); QCOMPARE(editor.toPlainText(),text);
        QTest::keyClick(search,Qt::Key_Escape); QTRY_VERIFY(!popup->isVisible()); QCOMPARE(editor.toPlainText(),text);
        popup=open(editor,"signals"); QVERIFY(popup);
        search=popup->findChild<QLineEdit*>("editorSlashQuery");
        search->setText("child"); QTest::keyClick(search,Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(!popup->isVisible(),15000);
        const auto changed=editor.toPlainText(); QVERIFY(changed.contains("logic signed [WIDTH-1:0] data [DEPTH];"));
        QCOMPARE(changed.count("logic clk;"),2); QVERIFY(editor.hasFocus());
        editor.undo(); QCOMPARE(editor.toPlainText(),text); editor.redo(); QCOMPARE(editor.toPlainText(),changed);
    }
    void signalsMouseSelectionAndStaleResult() {
        MyCodeEditor editor; setupEditor(editor); QTest::qWait(100);
        auto* popup=open(editor,"signals"); QVERIFY(popup);
        auto* search=popup->findChild<QLineEdit*>("editorSlashQuery"); search->setText("child");
        auto* list=popup->findChild<QListWidget*>("editorSlashCandidates");
        QTest::mouseClick(list->viewport(),Qt::LeftButton,Qt::NoModifier,list->visualItemRect(list->item(0)).center());
        QTRY_VERIFY_WITH_TIMEOUT(!popup->isVisible(),15000); QVERIFY(editor.toPlainText().contains("logic clk;"));
        editor.undo(); QCOMPARE(editor.toPlainText(),text);
        popup=open(editor,"signals"); search=popup->findChild<QLineEdit*>("editorSlashQuery");
        search->setText("child"); QTest::keyClick(search,Qt::Key_Return);
        QTest::keyClick(search,Qt::Key_Escape); QTest::qWait(300); QCOMPARE(editor.toPlainText(),text);
        popup=open(editor,"signals"); search=popup->findChild<QLineEdit*>("editorSlashQuery");
        search->setText("child"); QTest::keyClick(search,Qt::Key_Return);
        editor.setPlainText(text+"// external\n"); QTest::qWait(300); QCOMPARE(editor.toPlainText(),text+"// external\n");
    }
    void signalsConfirmationWhilePreparing_data() {
        QTest::addColumn<bool>("reopen");
        QTest::addColumn<bool>("click");
        QTest::addColumn<AfterB>("afterB");
        QTest::newRow("enter-B") << false << false << AfterB::Nothing;
        QTest::newRow("click-B") << false << true << AfterB::Nothing;
        QTest::newRow("reopen-at-new-cursor") << true << false << AfterB::Nothing;
        QTest::newRow("repeat-B-once") << false << false << AfterB::RepeatB;
        QTest::newRow("latest-of-three") << false << false << AfterB::ConfirmC;
        QTest::newRow("highlight-without-confirming") << false << false << AfterB::HighlightC;
        QTest::newRow("filter-without-confirming") << false << false << AfterB::FilterC;
        QTest::newRow("escape") << false << false << AfterB::Escape;
        QTest::newRow("cursor-moved") << false << false << AfterB::MoveCursor;
        QTest::newRow("document-edited") << false << false << AfterB::Edit;
        QTest::newRow("file-changed") << false << false << AfterB::Rename;
        QTest::newRow("catalog-changed") << false << false << AfterB::Catalog;
        QTest::newRow("workspace-sources-changed") << false << false << AfterB::Workspace;
    }
    void signalsConfirmationWhilePreparing() {
        QFETCH(bool, reopen);
        QFETCH(bool, click);
        QFETCH(AfterB, afterB);
        setupSource("module source_a(input logic from_a); endmodule\n"
                    "module source_b(input logic from_b); endmodule\n"
                    "module source_c(input logic from_c); endmodule");
        MyCodeEditor editor;
        setupEditor(editor);
        QTRY_VERIFY(!editor.syntaxDocument()->hasPendingEdits());
        auto* popup = open(editor, "signals");
        QVERIFY(popup && popup->isVisible());
        auto* search = popup->findChild<QLineEdit*>("editorSlashQuery");
        auto* list = popup->findChild<QListWidget*>("editorSlashCandidates");
        auto* feedback = popup->findChild<QLabel*>(QString(), Qt::FindDirectChildrenOnly);
        QVERIFY(search && list && feedback);
        QCOMPARE(search->placeholderText(), QString("Search modules"));
        search->setText("source_");
        QCOMPARE(list->count(), 3);
        QCOMPARE(list->currentItem()->text(), QString("source_a"));

        int anchor = editor.textCursor().position();
        QString expected = text;
        auto* pool = QThreadPool::globalInstance();
        const int oldLimit = pool->maxThreadCount();
        QSemaphore workerReady, releaseWorker;
        const auto restorePool = qScopeGuard([&] {
            pool->waitForDone();
            pool->setMaxThreadCount(oldLimit);
        });
        pool->setMaxThreadCount(1);
        pool->start([&] { workerReady.release(); releaseWorker.acquire(); });
        {
            const auto unblock = qScopeGuard([&] { releaseWorker.release(); });
            QVERIFY(workerReady.tryAcquire(1, 5000));
            QTest::keyClick(search, Qt::Key_Return);
            QCOMPARE(feedback->text(), QStringLiteral("Preparing signals…"));
            if (reopen) {
                QTest::keyClick(search, Qt::Key_Escape);
                QVERIFY(!popup->isVisible());
                auto cursor = editor.textCursor();
                cursor.movePosition(QTextCursor::StartOfBlock);
                QVERIFY(cursor.position() != anchor);
                editor.setTextCursor(cursor);
                anchor = cursor.position();
                QCOMPARE(open(editor, "signals"), popup);
                QCOMPARE(search->placeholderText(), QString("Search modules"));
                search->setText("source_");
            }
            QCOMPARE(list->item(1)->text(), QString("source_b"));
            if (click) {
                QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualItemRect(list->item(1)).center());
            } else {
                QTest::keyClick(search, Qt::Key_Down);
                QCOMPARE(list->currentItem()->text(), QString("source_b"));
                QTest::keyClick(search, Qt::Key_Return);
            }
            QCOMPARE(list->currentItem()->text(), QString("source_b"));
            QCOMPARE(feedback->text(), QStringLiteral("Preparing signals…"));
            QCOMPARE(editor.toPlainText(), text);

            switch (afterB) {
            case AfterB::Nothing: break;
            case AfterB::RepeatB: QTest::keyClick(search, Qt::Key_Return); break;
            case AfterB::ConfirmC:
            case AfterB::HighlightC:
                QTest::keyClick(search, Qt::Key_Down);
                QCOMPARE(list->currentItem()->text(), QString("source_c"));
                if (afterB == AfterB::ConfirmC) QTest::keyClick(search, Qt::Key_Return);
                break;
            case AfterB::FilterC: search->setText("source_c"); QCOMPARE(list->count(), 1); break;
            case AfterB::Escape: QTest::keyClick(search, Qt::Key_Escape); break;
            case AfterB::MoveCursor: editor.moveCursor(QTextCursor::End); break;
            case AfterB::Edit: expected += "// external edit\n"; editor.setPlainText(expected); break;
            case AfterB::Rename: editor.setDocumentFileName(root.filePath("renamed.sv")); break;
            case AfterB::Catalog:
                QVERIFY(UserTemplateService::getInstance()->setRecords({resource("parameters")}).valid);
                break;
            case AfterB::Workspace: {
                auto contents = source.contents;
                contents.insert(root.filePath("late.sv"), "module late; endmodule\n");
                SemanticIndex::getInstance()->setSnapshot(std::make_shared<const SemanticIndexSnapshot>(
                    SemanticIndexSnapshot::fromSymbolRecords(records, {}, {}, contents)));
                break;
            }
            }
            QCOMPARE(editor.toPlainText(), expected);
        }
        // Deliver A's completion, then any worker started by that completion.
        for (int pass = 0; pass < 2; ++pass) {
            QVERIFY(pool->waitForDone(10000));
            QCoreApplication::sendPostedEvents(nullptr, QEvent::FutureCallOut);
        }
        if (afterB == AfterB::Nothing || afterB == AfterB::RepeatB || afterB == AfterB::ConfirmC) {
            const QString failure = QString("After the explicit confirmation: feedback='%1', document unchanged=%2")
                .arg(feedback->text()).arg(editor.toPlainText() == text);
            QVERIFY2(!popup->isVisible(), qPrintable(failure));
            const QString line = afterB == AfterB::ConfirmC ? "logic from_c;\n" : "logic from_b;\n";
            expected.insert(anchor, line);
            QCOMPARE(editor.toPlainText(), expected);
            QCOMPARE(editor.textCursor().position(), anchor + line.size());
            editor.undo();
            QCOMPARE(editor.toPlainText(), text);
            QVERIFY(!editor.document()->isUndoAvailable());
            editor.redo();
        } else if (afterB == AfterB::HighlightC || afterB == AfterB::FilterC) {
            QVERIFY(popup->isVisible());
            QVERIFY(!feedback->text().startsWith("Preparing"));
        } else {
            QTRY_VERIFY(!popup->isVisible());
        }
        QCOMPARE(editor.toPlainText(), expected);
    }
    void parametersUseSamePopupAndSourceChangesCancel() {
        MyCodeEditor editor; setupEditor(editor); QTest::qWait(100);
        auto* popup=open(editor,"parameters"); QVERIFY(popup);
        auto* search=popup->findChild<QLineEdit*>("moduleTemplateSearch"); search->setText("child"); QTest::keyClick(search,Qt::Key_Return);
        auto* insert=popup->findChild<QPushButton*>("moduleTemplateInsert"); QTRY_VERIFY_WITH_TIMEOUT(insert->isEnabled(),15000);
        auto* table=popup->findChild<QTableWidget*>("moduleTemplateParameters"); QCOMPARE(table->rowCount(),3);
        QVERIFY(!(table->item(2,1)->flags() & Qt::ItemIsEditable)); table->item(0,1)->setText("32");
        QTRY_VERIFY_WITH_TIMEOUT(insert->isEnabled(),15000);
        QVERIFY(popup->findChild<QPlainTextEdit*>("moduleTemplatePreview")->toPlainText().contains("WIDTH = 32"));
        QPointer<QFrame> guard(popup); editor.setPlainText(text+"// external edit\n");
        QTRY_VERIFY(!guard || !guard->isVisible()); QCOMPARE(editor.toPlainText(),text+"// external edit\n");
    }
};
QTEST_MAIN(ModuleTemplatesTest)
#include "module_templates_test.moc"
