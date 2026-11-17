#include "moduletemplateplan.h"
#include "ordinarytemplateplan.h"
#include "usertemplateservice.h"
#include "mycodeeditor.h"
#include "semanticindexsnapshot.h"
#include "slangmanager.h"
#include "templatemanagerdialog.h"
#include "automationservice.h"
#include <QtTest>
#include <QtWidgets>

class ModuleTemplatesTest : public QObject {
    Q_OBJECT
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
        return editor.findChild<QFrame*>("moduleTemplatePopup");
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
        QVERIFY(plan.text.contains("localparam int dut_WIDTH = 16;"));
        QVERIFY(plan.text.contains("localparam int dut_ADDRESS = $clog2(dut_DEPTH);"));
        QVERIFY(plan.text.contains("logic signed [dut_WIDTH-1:0] dut_data [dut_DEPTH];"));
        QVERIFY(plan.text.contains("wire logic dut_ready;"));
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
        QVERIFY(ModuleTemplatePlanner::prepare(model, options, {"data"}).error.contains("data"));
        options.prefix = "a "; QVERIFY(!ModuleTemplatePlanner::prepare(model, options).error.isEmpty());
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
        QVERIFY(plan.text.contains("[dut_WIDTH-1:0] WIDTH;"));
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
        QVERIFY(preview.value("data").toObject().value("text").toString().contains("api_WIDTH = 12"));
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
    void popupPreviewCancelInsertUndo() {
        MyCodeEditor editor; setupEditor(editor); QTest::qWait(100);
        auto* popup=open(editor,"signals"); QVERIFY(popup); QVERIFY(popup->isVisible());
        auto* search=popup->findChild<QLineEdit*>("moduleTemplateSearch");
        auto* insert=popup->findChild<QPushButton*>("moduleTemplateInsert");
        auto* preview=popup->findChild<QPlainTextEdit*>("moduleTemplatePreview");
        auto* feedback=popup->findChild<QLabel*>("moduleTemplateFeedback");
        search->setText("child"); QVERIFY(!insert->isEnabled()); QCOMPARE(editor.toPlainText(),text);
        QTest::keyClick(search,Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(insert->isEnabled() || !feedback->text().contains("Preparing"), 15000);
        QVERIFY2(insert->isEnabled(),qPrintable(feedback->text())); QVERIFY(preview->toPlainText().contains("logic clk;"));
        QCOMPARE(editor.toPlainText(),text);
        QTest::keyClick(search,Qt::Key_Escape); QTRY_VERIFY(!popup->isVisible()); QCOMPARE(editor.toPlainText(),text);
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        popup=open(editor,"signals"); QVERIFY(popup); search=popup->findChild<QLineEdit*>("moduleTemplateSearch");
        search->setText("child"); QTest::keyClick(search,Qt::Key_Return);
        insert=popup->findChild<QPushButton*>("moduleTemplateInsert"); QTRY_VERIFY_WITH_TIMEOUT(insert->isEnabled(),15000);
        auto* prefix=popup->findChild<QLineEdit*>("moduleTemplatePrefix"); prefix->setText("dut_");
        QTRY_VERIFY_WITH_TIMEOUT(insert->isEnabled(),15000); insert->click();
        const auto changed=editor.toPlainText(); QVERIFY(changed.contains("dut_data"));
        editor.undo(); QCOMPARE(editor.toPlainText(),text); editor.redo(); QCOMPARE(editor.toPlainText(),changed);
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
