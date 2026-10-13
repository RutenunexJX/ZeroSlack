#include <QtTest>
#include <QtWidgets>
#include "mainwindow.h"
#include "tabmanager.h"
#include "workspacemanager.h"
#include "workspacesessioncoordinator.h"
#include "crashrecoveryservice.h"
#include "mycodeeditor.h"
#include "applicationthememanager.h"
#include "testuistyle.h"
#include "tbinputassist.h"
#include "editorinsertpaletteservice.h"
#include "semanticindexsnapshot.h"
#include "slangmanager.h"
#include "usertemplateservice.h"
#include "../src/simulation/simdock/core/analyzer.h"
#include "../src/simulation/simdock/core/testbench.h"
#include "../src/simulation/simdock/core/questasession.h"
#include "../src/simulation/simdock/core/workspace.h"
#include "../src/integrations/xips/xipshdlcallbuilder.h"
#include "../src/integrations/xips/xipshdlcommandservice.h"
#include "../src/integrations/xips/xipscontextprovider.h"
#include "../src/integrations/native/nativecontextview.h"
#include "../src/simulation/simdock/ui/workbench.h"

namespace {
QString report() { return qEnvironmentVariable("ZEROSLACK_TB_ASSIST_REPORT"); }
bool put(const QString& file, const QString& source) {
    QDir().mkpath(QFileInfo(file).absolutePath()); QFile f(file);
    return f.open(QIODevice::WriteOnly) && f.write(source.toUtf8()) == source.toUtf8().size();
}
QString source(QString type = "logic [7:0]") {
    return "module task_tb;\nlogic timing=0;\nlogic release_n=1;\n" + type + " payload='0, alternate='0;\n"
        "always #5 timing=~timing;\n"
        "task existing(input int repetitions, input int seed=3); endtask\n"
        "initial begin\n #1;\nend\n\nendmodule\n";
}
void install(const QString& file, const QString& text) {
    SlangManager slang;
    const auto records = slang.extractOverlayWorkspaceSymbolRecords({{file,text}});
    SemanticIndex::getInstance()->setSnapshot(std::make_shared<const SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolRecords(records, {}, {}, {{file,text}})));
}
GlobalControlQueryContext query(const QString& file, const QString& text, int pos=-1) {
    GlobalControlQueryContext q;
    q.editorAvailable=true; q.fileName=file; q.moduleName="task_tb"; q.documentText=text;
    q.cursorPosition=pos<0 ? text.lastIndexOf("endmodule") : pos;
    q.cursorLine=text.left(q.cursorPosition).count('\n')+1; q.documentRevision=17; q.documentInstance=4;
    return q;
}
QListWidgetItem* row(QListWidget* list, const QString& term) {
    for (int i=0;i<list->count();++i) if (list->item(i)->text().contains(term)) return list->item(i);
    return nullptr;
}
}
class TbTaskTemplatesTest : public QObject {
    Q_OBJECT
private slots:
    void nativeCommandsInMainWindow() {
        if (qEnvironmentVariableIsEmpty("XIPS_BROWSER_LIBRARY") || qEnvironmentVariableIsEmpty("XIPS_TEST_USER_DIR"))
            QSKIP("Supply the delivered native HDL component and user fixture.");
        QTemporaryDir settings;
        const auto root=QDir(report()).filePath("native-workspace"); QVERIFY(QDir().mkpath(root));
        const auto file=root+"/tb.sv";
        auto initial=source("logic signed [64:0]");
        initial.replace("module task_tb;", "module task_tb;\ntimeunit 1ns; timeprecision 1ps;\nint task_value=0;");
        initial.replace("#1;", "#1;\n// command here\n");
        QVERIFY(put(file,initial));
        const auto unselected=root+"/unselected.sv";
        QVERIFY(put(unselected,"module unselected; missing_type invalid; endmodule\n"));
        qputenv("ZEROSLACK_SESSION_STORAGE_PATH",settings.filePath("sessions.ini").toUtf8());
        qputenv("ZEROSLACK_EDITING_TIME_STORAGE_PATH",settings.filePath("editing.json").toUtf8());
        MainWindow window; window.resize(1380,900);
        window.tabManager->setCrashRecoveryService(std::make_unique<CrashRecoveryService>(settings.filePath("recovery")));
        window.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false); window.show(); window.activateWindow();
        auto* sessions=window.findChild<WorkspaceSessionCoordinator*>();
        QVERIFY(sessions && sessions->openWorkspace(root)); QVERIFY(window.tabManager->openFileInTab(file));
        auto* editor=window.tabManager->getCurrentEditor(); QVERIFY(editor);
        auto* build=window.findChild<simdock::Workbench*>(); QVERIFY(build); QVERIFY(build->createProject("Native HDL calls"));
        QTRY_VERIFY_WITH_TIMEOUT(window.workspaceManager->projectSnapshot().designSourceFiles().contains(unselected),30000);
        QVERIFY(!build->activeCompilationInputs().contains(unselected));
        auto* commands=window.findChild<XipsHdlCommandService*>(); QVERIFY(commands);
        QString messages;
        connect(commands,&XipsHdlCommandService::message,this,[&](const QString& message) { messages+=message+'\n'; qInfo().noquote()<<message; });
        QTRY_COMPARE_WITH_TIMEOUT(SemanticIndex::getInstance()->getCachedFileContent(file),initial,30000);
        const auto openPalette=[&](int position) {
            QTextCursor cursor(editor->document()); cursor.setPosition(position); editor->setTextCursor(cursor);
            editor->setFocus(); QTest::qWait(30); QTest::keyClick(editor,Qt::Key_Space,Qt::ControlModifier);
        };
        openPalette(initial.indexOf("// command here"));
        auto* palette=window.findChild<QWidget*>("globalControlPanel"); QVERIFY(palette && palette->isVisible());
        auto* search=palette->findChild<QLineEdit*>("globalControlSearchEdit");
        auto* list=palette->findChild<QListWidget*>("globalControlResultList"); QVERIFY(search && list);
        auto* tabs=palette->findChild<QTabBar*>("globalControlCategoryTabs"); QVERIFY(tabs); QCOMPARE(tabs->currentIndex(),0);
        auto* native=window.findChild<NativeContextView*>(); QVERIFY(native);
        QVERIFY2(native->isReady(),qPrintable(native->property("nativeComponentError").toString()));
        auto* component=native->component(); QVERIFY(component);
        const auto catalog=[&] { QVariantMap value; QMetaObject::invokeMethod(component,"hdlCommandCatalog",Q_RETURN_ARG(QVariantMap,value)); return value; };
        QTRY_COMPARE_WITH_TIMEOUT(catalog().value("state").toString(),QString("ready"),30000);
        QTRY_VERIFY_WITH_TIMEOUT(commands->canClose(nullptr),30000);
        QCOMPARE(catalog().value("entries").toList().size(),4);
        QTRY_VERIFY(row(list,"tb_reset_package::reset_driver::run"));
        QVERIFY(palette->grab().save(QDir(report()).filePath("xips-default-search.png")));
        QVERIFY(QDir(qEnvironmentVariable("XIPS_LIBRARY")).entryList(QDir::Dirs|QDir::NoDotAndDotDot).isEmpty());
        QTest::keyClick(search,Qt::Key_Escape);

        QString destination=root+"/behavior-packages", target="release_n";
        int imports=0,exports=0,bindings=0,argumentForms=0,collectForms=0;
        bool collecting=false, changeInputs=false;
        QTimer dialogs; dialogs.setInterval(20);
        connect(&dialogs,&QTimer::timeout,this,[&] {
            auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget()); if(!dialog) return;
            const auto accept=[&] { auto* button=dialog->findChild<QAbstractButton*>("formAccept"); if(button && button->isEnabled()) button->click(); };
            const auto acceptHost=[&] {
                auto* box=dialog->findChild<QDialogButtonBox*>(); QVERIFY(box);
                for(auto* button:box->buttons()) if(box->buttonRole(button)==QDialogButtonBox::AcceptRole) { QVERIFY(button->isEnabled()); button->click(); return; }
                QFAIL("No host accept action");
            };
            if(dialog->objectName()=="hdlBuiltinForm") { ++imports; accept(); }
            else if(auto* path=dialog->findChild<QLineEdit*>("exportDestination")) {
                path->setText(destination); ++exports;
                dialog->grab().save(QDir(report()).filePath("xips-use-destination.png")); accept();
            } else if(dialog->objectName()=="xipsSignalBinding") {
                auto* clock=dialog->findChild<QComboBox*>("xipsClock"); auto* signal=dialog->findChild<QComboBox*>("xipsTarget");
                clock->setCurrentIndex(clock->findData("timing")); signal->setCurrentIndex(signal->findData(target));
                dialog->findChild<QComboBox*>("xipsEdge")->setCurrentIndex(2);
                auto* level=dialog->findChild<QComboBox*>("xipsAssertedLevel"); if(level && level->isVisible()) level->setCurrentIndex(2);
                ++bindings; dialog->grab().save(QDir(report()).filePath("xips-signal-binding.png"));
                if(changeInputs) {
                    QVERIFY(build->adoptHdlInputs(build->activeProjectId(),{root+"/later.sv"},{}).isEmpty());
                    changeInputs=false;
                }
                acceptHost();
            } else if(dialog->objectName()=="xipsCallArguments") {
                auto* value=dialog->findChild<QLineEdit*>("xipsArgument_value"); if(value) value->setText(target);
                ++argumentForms; acceptHost();
            } else if(collecting && (dialog->findChild<QLineEdit*>("collectName") || dialog->objectName()=="payloadReviewForm")) {
                ++collectForms; accept();
            }
        }); dialogs.start();
        const QStringList symbols{"tb_reset_package::reset_driver::run", "tb_sequence_package::ramp_driver::run", "tb_random_package::random_driver::run", "tb_random_package::random_driver::window"};
        for(int command=0;command<symbols.size();++command) {
            QTRY_VERIFY_WITH_TIMEOUT(commands->canClose(nullptr),30000);
            target=command ? "payload" : "release_n";
            openPalette(editor->toPlainText().indexOf("// command here")); search->setText(symbols[command]);
            QTRY_VERIFY_WITH_TIMEOUT(row(list,symbols[command]),30000);
            auto* chosen=row(list,symbols[command]); QVERIFY2(chosen->flags().testFlag(Qt::ItemIsEnabled),qPrintable(chosen->text())); list->setCurrentItem(chosen);
            const auto before=editor->toPlainText();
            const auto messageCount=messages.size();
            QTest::keyClick(search,Qt::Key_Return);
            QTRY_VERIFY_WITH_TIMEOUT(editor->toPlainText()!=before || messages.size()!=messageCount,30000);
            QVERIFY2(editor->toPlainText()!=before,qPrintable(messages));
            QCOMPARE(bindings,command+1); QVERIFY(editor->toPlainText().contains("typedef type("));
            const auto inserted=editor->toPlainText(); QCOMPARE(editor->cachedDocumentText(),inserted);
            QTest::keyClick(editor,Qt::Key_Escape); editor->undo(); QCOMPARE(editor->toPlainText(),before);
            editor->redo(); QCOMPARE(editor->toPlainText(),inserted);
            QTextCursor cursor(editor->document()); cursor.setPosition(inserted.indexOf("// command here")); editor->setTextCursor(cursor); editor->insertPlainText("\n");
        }
        QCOMPARE(imports,1); QCOMPARE(exports,1);
        const auto references=root+"/.zeroslack/xips-references.json";
        const auto assets=[&] { QFile input(references); if(!input.open(QIODevice::ReadOnly)) return QJsonArray{}; return QJsonDocument::fromJson(input.readAll()).object().value("assets").toArray(); };
        QCOMPARE(assets().size(),1); QVERIFY(!assets().first().toObject().value("hdlCommand").toObject().value("sourceIdentity").toString().isEmpty());
        const auto project=simdock::loadProjects(root).first();
        QCOMPARE(project.sources.mid(0,4),QStringList({"behavior-packages/tb_signal_package.sv","behavior-packages/tb_reset_package.sv","behavior-packages/tb_sequence_package.sv","behavior-packages/tb_random_package.sv"}));
        QVERIFY(project.includeDirectories.contains("behavior-packages"));
        QVERIFY(window.tabManager->saveCurrentTab());
        QTRY_VERIFY_WITH_TIMEOUT(commands->canClose(nullptr),30000);
        collecting=true;
        QVERIFY(QMetaObject::invokeMethod(component,"collectPaths",Q_ARG(QStringList,QStringList{qEnvironmentVariable("XIPS_TEST_USER_DIR")})));
        QTRY_VERIFY_WITH_TIMEOUT(commands->canClose(nullptr),30000);
        QTRY_VERIFY_WITH_TIMEOUT(catalog().value("entries").toList().size()>=6,30000);
        collecting=false; QCOMPARE(collectForms,2);
        target="task_value"; destination=root+"/user-package";
        openPalette(editor->toPlainText().indexOf("// command here")); search->setText("user_package::delay_value");
        QTRY_VERIFY(row(list,"user_package::delay_value")); list->setCurrentItem(row(list,"user_package::delay_value"));
        QTest::keyClick(search,Qt::Key_Return);
        QTRY_VERIFY2_WITH_TIMEOUT(editor->toPlainText().contains("user_package::delay_value(.value(task_value));"),qPrintable(messages),30000);
        QTest::keyClick(editor,Qt::Key_Escape); QCOMPARE(argumentForms,1); QCOMPARE(exports,2); QCOMPARE(assets().size(),2);
        // An ordinary function remains an expression, including an unsaved source revision.
        QTextCursor cursor(editor->document()); cursor.setPosition(editor->toPlainText().indexOf("// command here")); editor->setTextCursor(cursor);
        editor->insertPlainText("\nif(task_value != 42) $fatal(1, \"task output\");\npayload = ;\n");
        target="65'sd21";
        openPalette(editor->toPlainText().indexOf("payload = ;")+10); search->setText("user_package::twice");
        QTRY_VERIFY(row(list,"user_package::twice")); list->setCurrentItem(row(list,"user_package::twice"));
        QTest::keyClick(search,Qt::Key_Return);
        QTRY_VERIFY2_WITH_TIMEOUT(editor->toPlainText().contains("payload = user_package::twice(.value(65'sd21));"),qPrintable(messages),30000);
        QTest::keyClick(editor,Qt::Key_Escape); QCOMPARE(argumentForms,2); QCOMPARE(exports,2); QCOMPARE(assets().size(),2);
        cursor=QTextCursor(editor->document()); cursor.setPosition(editor->toPlainText().indexOf("// command here")); editor->setTextCursor(cursor);
        editor->insertPlainText("if(payload != 42) $fatal(1, \"function result\");\n$display(\"HOST_XIPS_NATIVE_PASS\"); $finish;\n");
        QVERIFY(window.tabManager->saveCurrentTab()); const auto saved=editor->toPlainText();
        QVERIFY(window.grab().save(QDir(report()).filePath("xips-mainwindow-calls.png")));
        QVERIFY(window.tabManager->closeAllTabs()); QVERIFY(window.tabManager->openFileInTab(file));
        editor=window.tabManager->getCurrentEditor(); QCOMPARE(editor->toPlainText(),saved);
        QVERIFY(put(root+"/later.sv","module later; endmodule\n"));
        changeInputs=true; target="payload";
        openPalette(editor->toPlainText().indexOf("// command here")); search->setText(symbols[1]);
        QTRY_VERIFY(row(list,symbols[1])); list->setCurrentItem(row(list,symbols[1]));
        QTest::keyClick(search,Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(!changeInputs,30000);
        QTRY_VERIFY_WITH_TIMEOUT(commands->canClose(nullptr),30000); dialogs.stop();
        QCOMPARE(editor->toPlainText(),saved);
        QCOMPARE(TbInputAssistService::analysisCountForTesting(),quint64(0));
    }
    void xipsPackageCalls_data() {
        QTest::addColumn<QString>("type");
        QTest::newRow("narrow") << QString("logic [7:0]");
        QTest::newRow("wide") << QString("logic [79:0]");
        QTest::newRow("signed") << QString("logic signed [64:0]");
    }
    void xipsPackageCalls() {
        const auto library = qEnvironmentVariable("XIPS_TEST_HDL_DIR");
        if (library.isEmpty()) QSKIP("Supply the actual published xIPs HDL package directory.");
        QFETCH(QString, type);
        QTemporaryDir dir; const auto file = dir.filePath("tb.sv");
        XipsHdlCallRequest request;
        for (const auto* name : {"tb_signal_package.sv", "tb_reset_package.sv", "tb_sequence_package.sv", "tb_random_package.sv"}) {
            const auto path = QDir(library).filePath(QString::fromLatin1(name)); QFile input(path); QVERIFY(input.open(QIODevice::ReadOnly));
            const auto content = QString::fromUtf8(input.readAll());
            request.contents.insert(path, content); request.orderedFiles << path;
            QVERIFY(put(QDir(report()).filePath(QString::fromLatin1(name)), content));
        }
        auto text = source(type);
        text.replace("module task_tb;", "module task_tb;\ntimeunit 1ns; timeprecision 1ps;");
        text.replace("initial begin", "  initial begin");
        text.replace("#1;", "#1;\n// command here\nif ($realtime != 200ns) $fatal(1, \"window endpoint\");\n$display(\"HOST_XIPS_CALL_PASS\"); $finish;");
        request.orderedFiles << file;
        const QStringList symbols{"tb_reset_package::reset_driver::run", "tb_sequence_package::ramp_driver::run", "tb_random_package::random_driver::run", "tb_random_package::random_driver::window"};
        for (int command = 0; command < symbols.size(); ++command) {
            request.context = query(file, text, text.indexOf("// command here"));
            request.contents[file] = text;
            request.entry = {{"symbol", symbols[command]}, {"kind", "task"}, {"typeParameters", QStringList{"T"}}};
            request.sourceFile = QDir(library).filePath(command == 0 ? "tb_reset_package.sv" : command == 1 ? "tb_sequence_package.sv" : "tb_random_package.sv");
            request.target = command == 0 ? "release_n" : "payload"; request.clock = "timing";
            request.rising = false; request.asserted = false;
            const auto inspection = XipsHdlCallBuilder::inspect(request);
            QVERIFY2(inspection.error.isEmpty(), qPrintable(symbols[command] + ": " + inspection.error));
            const QHash<QString, QString> arguments{{"target","driver"},{"status","status"},{"cycles","3"},{"count","4"},
                {"start_value","'0"},{"step","1"},{"minimum","'0"},{"maximum","1"},{"start_at_ps","150ns/1ps"},{"stop_at_ps","200ns/1ps"},
                {"cancel","null"},{"rising","1'b0"},{"asserted_value","1'b0"},{"released_value","1'b1"}};
            const auto plan = XipsHdlCallBuilder::build(request, inspection, arguments);
            QVERIFY2(plan.error.isEmpty(), qPrintable(symbols[command] + ": " + plan.error));
            QVERIFY(plan.text.contains("typedef type("));
            auto expected = text; expected.replace(plan.start, plan.end-plan.start, plan.text);
            MyCodeEditor editor; editor.setPlainText(text);
            QTextCursor cursor(editor.document()); cursor.setPosition(plan.start); cursor.setPosition(plan.end, QTextCursor::KeepAnchor); editor.setTextCursor(cursor);
            QString error; QVERIFY(editor.insertCompletionText(plan.text,-1,0,plan.placeholders,&error));
            QCOMPARE(editor.toPlainText(),expected); QCOMPARE(editor.cachedDocumentText(),expected);
            editor.undo(); QCOMPARE(editor.toPlainText(),text);
            editor.redo(); QCOMPARE(editor.toPlainText(),expected);
            text.replace(plan.start, plan.end-plan.start, plan.text + "\n");
        }
        QVERIFY(put(QDir(report()).filePath(QString::fromLatin1(QTest::currentDataTag())+"-xips-call.sv"), text));
    }
    void sourceBackedUserCalls() {
        QTemporaryDir dir;
        const auto package = dir.filePath("user_package.sv"), file = dir.filePath("tb.sv");
        const QString library = "package user_package;\n"
            "task automatic drive(input int count, output logic [7:0] result); result=count; endtask\n"
            "function automatic logic [7:0] twice(input logic [7:0] value); return value*2; endfunction\n"
            "function automatic void clear(output logic [7:0] value); value='0; endfunction\nendpackage\n";
        auto text = source();
        XipsHdlCallRequest request;
        request.context = query(file, text, text.indexOf("#1;"));
        request.sourceFile = package; request.orderedFiles = {package, file};
        request.contents = {{package, library}, {file, text}};
        request.entry = {{"symbol", "user_package::drive"}, {"kind", "task"}};
        QVERIFY(XipsHdlCallBuilder::proceduralSite(request.context));
        auto inspection = XipsHdlCallBuilder::inspect(request);
        QVERIFY2(inspection.error.isEmpty(), qPrintable(inspection.error));
        QCOMPARE(inspection.callable.presentation.callableArguments.size(), 2);
        auto plan = XipsHdlCallBuilder::build(request, inspection, {{"count", "5"}, {"result", "payload"}});
        QVERIFY2(plan.error.isEmpty(), qPrintable(plan.error));
        QCOMPARE(plan.text, QString("user_package::drive(.count(5), .result(payload));"));
        const auto taskCall = plan.text;
        QCOMPARE(plan.placeholders.size(), 2);
        QVERIFY(!XipsHdlCallBuilder::build(request, inspection, {{"count", "5"}, {"result", "7"}}).error.isEmpty());
        request.entry = {{"symbol", "user_package::clear"}, {"kind", "function"}};
        auto voidInspection = XipsHdlCallBuilder::inspect(request);
        QVERIFY2(voidInspection.error.isEmpty(), qPrintable(voidInspection.error));
        auto voidCall = XipsHdlCallBuilder::build(request, voidInspection, {{"value", "payload"}});
        QVERIFY2(voidCall.error.isEmpty(), qPrintable(voidCall.error));
        QCOMPARE(voidCall.text, QString("user_package::clear(.value(payload));"));
        request.entry = {{"symbol", "user_package::drive"}, {"kind", "task"}};
        request.context = query(file, text);
        QVERIFY(!XipsHdlCallBuilder::inspect(request).error.isEmpty());
        text.replace("#1;", "payload = ;");
        request.context = query(file, text, text.indexOf("payload = ;") + 10);
        QVERIFY(!XipsHdlCallBuilder::proceduralSite(request.context));
        request.contents[file] = text; request.entry = {{"symbol", "user_package::twice"}, {"kind", "function"}};
        inspection = XipsHdlCallBuilder::inspect(request);
        QVERIFY2(inspection.error.isEmpty(), qPrintable(inspection.error));
        QVERIFY(!inspection.callable.presentation.callableReturnsVoid);
        plan = XipsHdlCallBuilder::build(request, inspection, {{"value", "payload"}});
        QVERIFY2(plan.error.isEmpty(), qPrintable(plan.error));
        QCOMPARE(plan.text, QString("user_package::twice(.value(payload))"));
        auto output = text; output.replace(plan.start, plan.end-plan.start, plan.text);
        const auto assignment = QStringLiteral("payload = ") + plan.text + QLatin1Char(';');
        output.replace(assignment, taskCall + QLatin1Char('\n') + assignment
            + QStringLiteral("\nif(payload !== 8'd10) $fatal(1, \"function result\");\n") + voidCall.text
            + QStringLiteral("\nif(payload !== 0) $fatal(1, \"void function output\");\n$display(\"HOST_XIPS_CALL_PASS\"); $finish;"));
        QVERIFY(put(QDir(report()).filePath("user-package.sv"), library));
        QVERIFY(put(QDir(report()).filePath("user-call-tb.sv"), output));
        request.entry["kind"] = "task";
        QVERIFY(!XipsHdlCallBuilder::inspect(request).error.isEmpty());
    }
    void sourceAndIncludeRevisionGuards() {
        QTemporaryDir dir;
        const auto file=dir.filePath("tb.sv"), package=dir.filePath("user_package.sv"), header=dir.filePath("types.svh");
        const auto text=source();
        QVERIFY(put(package, "package user_package;\n`include \"types.svh\"\nfunction automatic value_t twice(input value_t value); return value*2; endfunction\nendpackage\n"));
        QVERIFY(put(header, "typedef logic [7:0] value_t;\n"));
        XipsHdlCallRequest request; request.context=query(file,text,text.indexOf("#1;"));
        request.sourceFile=package; request.orderedFiles={package,file}; request.includeDirectories={dir.path()};
        request.contents={{file,text}}; request.entry={{"symbol","user_package::twice"},{"kind","function"}};
        const auto inspection=XipsHdlCallBuilder::inspect(request);
        QVERIFY2(inspection.error.isEmpty(),qPrintable(inspection.error));
        const auto before=QFileInfo(header).lastModified();
        QVERIFY(put(header,"typedef logic [6:0] value_t;\n"));
        QFile changed(header); QVERIFY(changed.open(QIODevice::ReadWrite)); QVERIFY(changed.setFileTime(before,QFileDevice::FileModificationTime)); changed.close();
        const auto plan=XipsHdlCallBuilder::build(request,inspection,{{"value","payload"}});
        QVERIFY(plan.error.contains("source or include changed"));
    }
    void exportedInputsAndOldComponent() {
        QTemporaryDir dir; const auto root=dir.filePath("workspace"); QVERIFY(QDir().mkpath(root));
        WorkspaceManager workspaces; workspaces.setRecentWorkspacePersistenceEnabledForTesting(false); QVERIFY(workspaces.openWorkspace(root));
        QWidget owner;
        simdock::Workbench build(&owner); build.openWorkspace(root); QVERIFY(build.createProject("Calls"));
        XipsHdlCommandService commands(nullptr,&workspaces,&build,&owner);
        auto* host=commands.view(); QVERIFY(host);
        XipsContextProvider provider(nullptr,&workspaces,&commands);
        QCOMPARE(provider.createView(provider.activationResource(root),&owner),host);
        auto* bridge=host->findChild<XipsHostBridge*>(); QVERIFY(bridge);
        const auto folder=root+"/lib/exported"; QVERIFY(QDir().mkpath(folder));
        QVERIFY(put(folder+"/dep.sv","package dep; parameter int N=3; endpackage\n"));
        QVERIFY(put(folder+"/user.sv","package user_package; function int value(); return dep::N; endfunction endpackage\n"));
        const QVariantMap receipt{{"schema","xips.use/v1"},{"assetId","asset-1"},{"revision","version-1"},{"contentHash","pinned-content"},
            {"workspace",root},{"path",folder},{"files",QStringList{"dep.sv","user.sv"}},
            {"hdlCommand",QVariantMap{{"schema","xips.hdl-use/v1"},{"commandId","asset-1/mapping-1"},{"sourceFile","user.sv"},
                {"compileOrder",QStringList{"dep.sv","user.sv"}},{"includeDirectories",QStringList{"."}}}}};
        QVERIFY2(bridge->exportCompleted(receipt).isEmpty(), "HDL receipt must be adopted");
        QVERIFY(bridge->exportCompleted(receipt).isEmpty());
        const auto projects=simdock::loadProjects(root); QCOMPARE(projects.size(),1);
        QCOMPARE(projects.first().sources,QStringList({"lib/exported/dep.sv","lib/exported/user.sv"}));
        QCOMPARE(build.activeCompilationInputs(),QStringList({folder+"/dep.sv",folder+"/user.sv"}));
        QCOMPARE(projects.first().includeDirectories,QStringList({"lib/exported"}));
        QVERIFY(workspaces.workspaceConfiguration().includeDirs.contains(folder));
        QFile references(root+"/.zeroslack/xips-references.json"); QVERIFY(references.open(QIODevice::ReadOnly));
        const auto assets=QJsonDocument::fromJson(references.readAll()).object().value("assets").toArray();
        QCOMPARE(assets.size(),1); QCOMPARE(assets.first().toObject().value("revision").toString(),QString("version-1"));
        QVERIFY(!bridge->destinationError(folder).isEmpty());
        commands.prepare();
        const auto available=commands.items("",query(root+"/tb.sv",source(),source().indexOf("#1;")));
        if (!QJsonDocument::fromVariant(host->property("nativeComponentCapabilities")).toJson().contains("\"hdl-commands-v1\"")) {
            QCOMPARE(available.size(),1); QVERIFY(!available.first().disabledReason.isEmpty());
            QVERIFY(available.first().insertionText.isEmpty());
        }
    }
    void callableSemantics() {
        QTemporaryDir dir; const auto file=dir.filePath("tb.sv"); const auto text=source();
        install(file,text); EditorInsertPaletteService service; auto q=query(file,text);
        const auto calls=service.query(GlobalControlCategory::Symbols,"t existing",q);
        QCOMPARE(calls.size(),1);
        QCOMPARE(calls.first().insertionText,QString("existing(.repetitions(repetitions), .seed());"));
        QVERIFY(calls.first().disabledReason.isEmpty());
        QVERIFY(calls.first().requiresCurrentSemantic);
        q.documentText+="// unsaved\n"; ++q.documentRevision;
        QVERIFY(!service.query(GlobalControlCategory::Symbols,"t existing",q).first().disabledReason.isEmpty());
    }
    void generationUsesSlang() {
        QTemporaryDir dir; const auto file=dir.filePath("design.sv");
        const QString text="package p; parameter int W=12; typedef enum logic [1:0] {IDLE=1,GO=2} mode_t; endpackage\n"
            "module chip #(parameter int W=p::W)(input logic clk, input p::mode_t mode, input logic signed [W-1:0] value, inout wire pin); endmodule\n";
        install(file,text);
        auto module=simdock::analyzeSource(text.toUtf8(),file).modules.first();
        QVERIFY(module.ports.isEmpty()); QVERIFY(!module.limitations.isEmpty());
        QString error;
        QVERIFY2(simdock::completeTestbenchTypes(module,*SemanticIndex::getInstance(),file,text,&error),qPrintable(error));
        QCOMPARE(module.ports.size(),4); QCOMPARE(module.parameters.size(),1);
        const auto tb=simdock::generateTestbench(module,simdock::suggestedTbOptions(module),100,&error);
        QVERIFY2(error.isEmpty(),qPrintable(error)); QVERIFY(tb.contains("p::mode_t mode;"));
        QVERIFY(tb.contains("mode = p::IDLE;")); QVERIFY(tb.contains("wire "));
        QVERIFY(put(QDir(report()).filePath("generated-dut.sv"),text));
        QVERIFY(put(QDir(report()).filePath("generated-tb.sv"),tb));
        QVERIFY(!simdock::completeTestbenchTypes(module,*SemanticIndex::getInstance(),file,text+"\n",&error));
        const QString typeParam="module chip #(parameter type T=logic)(input T value); endmodule\n";
        install(file,typeParam); module.name="chip";
        QVERIFY(!simdock::completeTestbenchTypes(module,*SemanticIndex::getInstance(),file,typeParam,&error));
        QVERIFY(error.contains("Type parameters"));
    }
    void allWaveCommandsAndStorage() {
        QTemporaryDir root; simdock::Project p; p.id="legacy"; p.name="legacy"; p.tbName="task_tb";
        p.sources={"tb.sv"}; p.tbFile="tb.sv"; p.durationNs=50;
        for (const auto& scope : {"interface","selected","all"}) {
            p.waveScope=scope; p.waveSignals={"task_tb.payload"};
            const auto script=simdock::runScript(root.path(),p,root.filePath("run"),root.filePath("modelsim.ini"));
            QVERIFY(script.contains("add wave -r /*")); QVERIFY(script.contains("log -r /*"));
            QVERIFY(!script.contains("add wave sim:/task_tb/payload"));
            const auto json=simdock::projectJson(p);
            QCOMPARE(json["waveScope"].toString(),QString(scope));
        }
    }
    void pausedAssistAndCreateTb() {
        QTemporaryDir data; const auto root=data.filePath("workspace"); QDir().mkpath(root);
        const auto file=root+"/tb.sv"; const auto original=source(); QVERIFY(put(file,original));
        qputenv("ZEROSLACK_SESSION_STORAGE_PATH",data.filePath("sessions.ini").toUtf8());
        qputenv("ZEROSLACK_EDITING_TIME_STORAGE_PATH",data.filePath("editing.json").toUtf8());
        MainWindow window; window.resize(1380,900);
        window.tabManager->setCrashRecoveryService(std::make_unique<CrashRecoveryService>(data.filePath("recovery")));
        window.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false); window.show();
        const auto before=TbInputAssistService::analysisCountForTesting();
        auto* sessions=window.findChild<WorkspaceSessionCoordinator*>();
        QVERIFY(sessions && sessions->openWorkspace(root)); QVERIFY(window.tabManager->openFileInTab(file));
        auto* editor=window.tabManager->getCurrentEditor(); QVERIFY(editor);
        QTRY_COMPARE_WITH_TIMEOUT(SemanticIndex::getInstance()->getCachedFileContent(file),original,20000);
        QTextCursor c(editor->document()); c.setPosition(original.indexOf("#1;"));
        editor->setTextCursor(c); editor->setFocus(); QTest::qWait(50);
        QTest::keyClick(editor,Qt::Key_Space,Qt::ControlModifier);
        auto* panel=window.findChild<QWidget*>("globalControlPanel"); QVERIFY(panel && panel->isVisible());
        auto* search=panel->findChild<QLineEdit*>("globalControlSearchEdit");
        auto* list=panel->findChild<QListWidget*>("globalControlResultList");
        QVERIFY(search && list); search->setText("t existing");
        QVERIFY(list->count()>0); QVERIFY(list->item(0)->flags().testFlag(Qt::ItemIsEnabled));
        QDir().mkpath(report()); QVERIFY(panel->grab().save(QDir(report()).filePath("task-call-palette.png")));
        QTest::keyClick(search,Qt::Key_Return);
        QVERIFY(editor->toPlainText().contains("existing(.repetitions(repetitions), .seed());"));
        QCOMPARE(editor->textCursor().selectedText(),QString("repetitions"));
        QTest::keyClicks(editor,"5"); QTest::keyClick(editor,Qt::Key_Escape);
        const auto withCall=editor->toPlainText();
        QCOMPARE(editor->cachedDocumentText(),withCall);
        QVERIFY(window.tabManager->saveCurrentTab());
        editor->undo(); editor->redo(); QCOMPARE(editor->toPlainText(),withCall);
        for(int i=0;i<12;i++) {
            c=editor->textCursor(); c.movePosition(QTextCursor::End); editor->setTextCursor(c);
            QTest::keyClicks(editor," "); editor->undo();
        }
        editor->viewport()->repaint(); QTest::mouseMove(editor->viewport(),QPoint(100,100));
        QTest::mouseClick(editor->viewport(),Qt::LeftButton,Qt::NoModifier,QPoint(100,100));
        QTest::mouseClick(editor->viewport(),Qt::RightButton,Qt::NoModifier,QPoint(100,100));
        QTest::keyClick(editor,Qt::Key_Escape); QTest::qWait(600);
        QVERIFY(editor->tbInputAssists().isEmpty()); QVERIFY(editor->tbAssistHitRegions().isEmpty());
        QVERIFY(!editor->findChild<QFrame*>("tbInputAssistPopup"));
        QCOMPARE(TbInputAssistService::analysisCountForTesting(),before);
        QVERIFY(window.tabManager->saveCurrentTab()); QVERIFY(window.tabManager->closeAllTabs());
        QVERIFY(window.tabManager->openFileInTab(file));
        QCOMPARE(window.tabManager->getCurrentEditor()->toPlainText(),withCall);
        const auto dutFile=root+"/chip.sv";
        const QString dut="package input_types; typedef enum logic [1:0] {IDLE=1,RUN=2} mode_t; endpackage\n"
            "module chip(input logic clk, rst_n, input input_types::mode_t mode, input logic [11:0] value); endmodule\n";
        QVERIFY(put(dutFile,dut)); QVERIFY(window.tabManager->openFileInTab(dutFile));
        QTRY_COMPARE_WITH_TIMEOUT(SemanticIndex::getInstance()->getCachedFileContent(dutFile),dut,20000);
        auto* newTb=window.findChild<QAbstractButton*>("editorNewTb"); QVERIFY(newTb); newTb->click();
        auto* creator=window.findChild<QFrame*>("newTbEditorPopup"); QVERIFY(creator);
        auto* create=creator->findChild<QPushButton*>("tbCreateAndOpen"); QVERIFY(create);
        QVERIFY2(create->isEnabled(),qPrintable(creator->findChild<QLabel*>("tbNewStatus")->text()));
        QVERIFY(window.grab().save(QDir(report()).filePath("new-tb-slang-preview.png")));
        create->click();
        QTRY_VERIFY(window.tabManager->getCurrentEditor()->toPlainText().contains("module tb_chip;"));
        QVERIFY(window.tabManager->getCurrentEditor()->toPlainText().contains("mode = input_types::IDLE;"));
        QCOMPARE(TbInputAssistService::analysisCountForTesting(),before);
        QVERIFY(window.grab().save(QDir(report()).filePath("new-tb-assist-disabled.png")));
        for(auto* e:window.tabManager->openEditors()) e->document()->setModified(false);
        const auto second=data.filePath("second"); QDir().mkpath(second);
        QVERIFY(put(second+"/empty.sv","module empty; endmodule\n"));
        QVERIFY(sessions->openWorkspace(second)); QTest::qWait(250);
        QCOMPARE(TbInputAssistService::analysisCountForTesting(),before);
    }
    void liveAllWaveSession() {
        const auto executable=qEnvironmentVariable("SIMDOCK_TEST_QUESTA");
        if(executable.isEmpty()) QSKIP("Real Questa required.");
        QTemporaryDir root;
        QVERIFY(put(root.filePath("tb.sv"),QString(QChar(96))+"timescale 1ns/1ps\n"
            "module child; logic deep_signal=0; always #3 deep_signal=~deep_signal; endmodule\n"
            "module task_tb; logic timing=0; always #5 timing=~timing; child u_child(); endmodule\n"));
        simdock::QuestaSession session; QSignalSpy finished(&session,&simdock::QuestaSession::finished);
        QString log; connect(&session,&simdock::QuestaSession::logText,this,[&](const QString& s){log+=s;});
        auto p=simdock::newProject("All hierarchy"); p.sources={"tb.sv"}; p.tbFile="tb.sv"; p.tbName="task_tb"; p.durationNs=50;
        QString previous,error;
        for(const auto& scope:{"interface","selected","all"}) {
            p.waveScope=scope; p.waveSignals={"old_missing_signal"};
            QVERIFY(simdock::saveProject(root.path(),p,&error));
            const auto loaded=simdock::loadProjects(root.path()).first(); QCOMPARE(loaded.waveScope,p.waveScope);
            const auto prepared=simdock::prepareRun(executable,root.path(),loaded);
            QVERIFY2(prepared.error.isEmpty(),qPrintable(prepared.error));
            QVERIFY2(session.start(prepared,&error),qPrintable(error));
            QTRY_VERIFY2_WITH_TIMEOUT(!finished.isEmpty(),qPrintable(log),90000);
            QVERIFY2(finished.takeFirst()[0].toBool(),qPrintable(log));
            const auto destination=QDir(report()).filePath("session-"+QString(scope)); QDir().mkpath(destination);
            QVERIFY(put(destination+"/session.log",log));
            QVERIFY(QFile::copy(session.lastRunDirectory()+"/run.do",destination+"/run.do"));
            QVERIFY(QFile::copy(session.lastRunDirectory()+"/result.wlf",destination+"/result.wlf"));
            if(!previous.isEmpty()) QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(previous),5000);
            previous=session.lastRunDirectory(); log.clear();
        }
        session.stop(); QTRY_VERIFY_WITH_TIMEOUT(!session.alive(),15000);
        QVERIFY(!QFileInfo::exists(previous));
    }
};
int main(int argc,char** argv) {
    QStandardPaths::setTestModeEnabled(true); QApplication app(argc,argv); app.setQuitOnLastWindowClosed(false);
    QCoreApplication::setOrganizationName("ZeroSlackTaskTemplateTest"); QCoreApplication::setApplicationName("ZeroSlackTaskTemplateTest");
    QTemporaryDir profile; QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,profile.path());
    QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,profile.path());
    qputenv("ZEROSLACK_SIMDOCK_LEGACY_SETTINGS_PATH",profile.filePath("legacy.ini").toUtf8());
    UserTemplateService::getInstance()->setGlobalTemplateFilePath(profile.filePath("templates.json"));
    if(!initializeUiStyleForTest()) return 2;
    ApplicationThemeManager::instance().setAnimationsEnabled(false); ApplicationThemeManager::instance().applyToApplication();
    TbTaskTemplatesTest test; return QTest::qExec(&test,argc,argv);
}
#include "tb_task_templates_test.moc"
