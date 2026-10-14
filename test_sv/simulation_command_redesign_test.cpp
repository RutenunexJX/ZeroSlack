#include "../src/integrations/native/nativecontextview.h"
#include "../src/integrations/xips/xipsassetsync.h"
#include "../src/integrations/xips/xipscontextprovider.h"
#include "../src/simulation/simdock/core/questasession.h"
#include "../src/simulation/simdock/core/workspace.h"
#include "../src/simulation/simdock/ui/simulationeventspanel.h"
#include "../src/simulation/simdock/ui/workbench.h"
#include "applicationthememanager.h"
#include "editorinsertpaletteservice.h"
#include "mycodeeditor.h"
#include "shareddocument.h"
#include "simulationcommandplanner.h"
#include "simulationcommandprefix.h"
#include "simulationcommandservice.h"
#include "slangmanager.h"
#include "templateinputservice.h"
#include "testuistyle.h"
#include "usertemplateservice.h"
#include "xipsassetsyncdialog.h"
#include <QScopeGuard>
#include <QtTest>
#include <QtWidgets>
#include <thread>
#define private public
#include "globalcontrolcoordinator.h"
#include "globalcontrolpanel.h"
#include "mainwindow.h"
#include "templatemanagerdialog.h"
#undef private
#include "tabmanager.h"
#include "workspacemanager.h"
#include "workspacesessioncoordinator.h"

namespace
{
bool put(const QString &path, const QString &text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    const auto b = text.toUtf8();
    return f.open(QIODevice::WriteOnly) && f.write(b) == b.size();
}
QByteArray bytes(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
void evidence(const QString &name, const QByteArray &data)
{
    const auto root = qEnvironmentVariable("SIMDOCK_TEST_ARTIFACT_DIR");
    if (root.isEmpty())
        return;
    const auto path = QDir(root).filePath(name);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(data);
}
void screenshot(QWidget *view, const QString &name)
{
    const auto root = qEnvironmentVariable("SIMDOCK_TEST_ARTIFACT_DIR");
    if (!root.isEmpty())
        view->grab().save(QDir(root).filePath(name));
}
GlobalControlQueryContext contextFor(const QString &file, const QString &source, const QString &prefix,
                                     const QString &module = "tb")
{
    GlobalControlQueryContext c;
    c.fileName = file;
    c.documentText = source;
    c.editorAvailable = true;
    c.moduleName = module;
    c.cursorPosition = source.indexOf(prefix) + prefix.size();
    c.cursorLine = source.left(c.cursorPosition).count('\n') + 1;
    c.replacementStart = c.cursorPosition - prefix.size();
    c.replacementLength = prefix.size();
    return c;
}
SimulationCommandRequest command(const QString &file, const QString &source, const QString &prefix,
                                 const QVariantMap &config, const QStringList &files)
{
    SimulationCommandRequest r;
    r.context = contextFor(file, source, prefix);
    r.contents.insert(file, source);
    r.item.parameters = config;
    r.item.replacementStart = r.context.replacementStart;
    r.item.replacementLength = r.context.replacementLength;
    r.orderedFiles = files;
    if (!r.orderedFiles.contains(file))
        r.orderedFiles << file;
    return r;
}
QString apply(const QString &source, const SimulationCommandPlan &plan)
{
    auto text = source;
    text.replace(plan.start, plan.length, plan.text);
    return text;
}
} // namespace
class SimulationCommandRedesignTest : public QObject
{
    Q_OBJECT
  private slots:
    void storageScopeMigration()
    {
        QTemporaryDir root;
        UserTemplateService service(root.filePath("global.json"), root.filePath("workspace.json"));
        const auto file = root.filePath("tb.sv");
        UserTemplateRecord record;
        record.id = "global";
        record.description = "  User description with retained spaces  ";
        record.commandToken = record.label = "@q1";
        record.insertText = "@(posedge clk1)";
        record.command = {{"kind", "snippet"}};
        QVERIFY(service.saveRecord(record, false).valid);
        QCOMPARE(service.records().first().description, record.description);
        const auto capturedRevision = service.catalogRevision();
        record.id = "workspace";
        record.insertText = "@(posedge clk2)";
        QVERIFY(service.saveRecord(record, true).valid);
        QVERIFY(!service.saveRecord(record, true, capturedRevision).valid);
        record.id = "local";
        record.insertText = "@(posedge clk3)";
        record.command.insert("file", file);
        QVERIFY(service.saveRecord(record, true).valid);
        const auto bodyFor = [&](const QString &file)
        {
            for (const auto &r : service.recordsForFile(file, true))
                if (r.commandToken == "@q1")
                    return r.insertText;
            return QString();
        };
        QCOMPARE(bodyFor(file), QString("@(posedge clk3)"));
        QCOMPARE(bodyFor(root.filePath("other.sv")), QString("@(posedge clk2)"));
        record.id = "duplicate";
        QVERIFY(!service.saveRecord(record, true).valid);
        QVERIFY(service.deleteRecord("local", true).valid);
        QCOMPARE(bodyFor(file), QString("@(posedge clk2)"));
        QVERIFY(put(root.filePath("user_package.sv"),
                    "package user_package; task automatic run(input int n); endtask endpackage\n"));
        QVERIFY(put(
            root.filePath("xips-commands.json"),
            R"({"schema":"xips.hdl-commands/v1","commands":[{"id":"id-1","name":"user_run","description":"User text","file":"user_package.sv","files":["user_package.sv"],"symbol":"user_package::run","aliases":["alias"]}]})"));
        auto migrated = service.migrateXipsCommands(root.filePath("xips-commands.json"));
        QVERIFY2(migrated.valid, qPrintable(migrated.failureReason));
        const auto before = bytes(service.workspaceTemplateLocation());
        QVERIFY(service.migrateXipsCommands(root.filePath("xips-commands.json")).valid);
        QCOMPARE(bytes(service.workspaceTemplateLocation()), before);
        QVERIFY(put(service.workspaceTemplateLocation(), "{broken"));
        QVERIFY(!service.saveRecord(record, true).valid);
        QCOMPARE(bytes(service.workspaceTemplateLocation()), QByteArray("{broken"));
    }
    void prefixSyntax()
    {
        const QString file = "prefix.sv";
        SemanticIndex::getInstance()->clearSemanticState();
        for (const auto &prefix : QStringList{"@q1", "@", QString::fromUtf8("@复位")})
        {
            const auto source = "module tb; initial begin " + prefix + "\n end endmodule";
            auto c = contextFor(file, source, prefix);
            QVERIFY(SimulationCommandPrefix::capture(c));
            QCOMPARE(c.documentText.mid(c.replacementStart, c.replacementLength), prefix);
        }
        for (const auto &source :
             QStringList{"module tb; initial @(posedge clk); endmodule", "module tb; always @* ; endmodule",
                         "module tb; // @q1\nendmodule", "module tb; initial $display(\"@q1\"); endmodule"})
        {
            auto c = contextFor(file, source, source.contains("@q1") ? "@q1" : "@");
            QVERIFY2(!SimulationCommandPrefix::capture(c), qPrintable(source));
        }
        const auto source = QString("module tb; logic clk; initial @clk; endmodule");
        SlangManager slang;
        SemanticIndex::getInstance()->setSnapshot(
            std::make_shared<const SemanticIndexSnapshot>(SemanticIndexSnapshot::fromSymbolRecords(
                slang.extractSymbolRecords(file, source), {}, {}, {{file, source}})));
        auto c = contextFor(file, source, "@clk");
        QVERIFY(!SimulationCommandPrefix::capture(c));
        c = contextFor(file, "module tb; initial @q1; endmodule", "@q1");
        c.readOnly = true;
        QVERIFY(!SimulationCommandPrefix::capture(c));
    }
    void realSlangInputs()
    {
        QTemporaryDir root;
        const auto file = root.filePath("tb.sv");
        const QString source =
            "module child #(parameter W=8)(input logic clock, output logic [W-1:0] out); logic [W-1:0] data; "
            "endmodule\n"
            "module tb; logic clk; logic reset; logic [7:0] local_data; typedef enum logic [1:0] {IDLE,RUN} "
            "state_t; state_t state; parameter N=8; child #(.W(8)) a(); child #(.W(16)) b(); for(genvar "
            "i=0;i<2;i++) begin:g child #(.W(i+3)) c(); end initial begin @cmd\n end endmodule\n";
        auto r = command(file, source, "@cmd", {{"kind", "snippet"}}, {file});
        r.item.insertionText = ";";
        auto plan = SimulationCommandPlanner::prepare(r);
        QVERIFY2(plan.error.isEmpty(), qPrintable(plan.error));
        QVERIFY(plan.inputs);
        auto candidates = [&](QVariantMap input, QHash<QString, QString> binding = {})
        { return TemplateInputService::candidates(input, *plan.inputs, binding); };
        const auto instances = candidates({{"kind", "Instance"}});
        qInfo() << "instances" << instances.values;
        QVERIFY(instances.values.contains("tb.a"));
        QVERIFY(instances.values.contains("tb.b"));
        QVERIFY(instances.values.contains("tb.g[0].c"));
        QVERIFY(instances.values.contains("tb.g[1].c"));
        QVariantMap internal{{"kind", "Internal"}, {"scope", "instance"}, {"instanceSlot", "instance"},
                             {"minWidth", 16},     {"maxWidth", 16},      {"access", "write"}};
        QVERIFY(candidates(internal, {{"instance", "tb.a"}}).values.isEmpty());
        QVERIFY(candidates(internal, {{"instance", "tb.b"}}).values.contains("tb.b.data"));
        internal.insert("minWidth", 3);
        internal.insert("maxWidth", 3);
        QVERIFY(candidates(internal, {{"instance", "tb.g[0].c"}}).values.contains("tb.g[0].c.data"));
        QVERIFY(candidates(internal, {{"instance", "tb.g[1].c"}}).values.isEmpty());
        const auto ports = candidates(
            {{"kind", "Port"}, {"scope", "instance"}, {"instanceSlot", "instance"}, {"direction", "input"}},
            {{"instance", "tb.a"}});
        QVERIFY(ports.values.contains("tb.a.clock"));
        QVERIFY(!ports.values.contains("tb.a.out"));
        auto clocks = candidates({{"kind", "Signal"}, {"role", "Clock"}});
        QVERIFY(clocks.values.isEmpty());
        for (const auto &record : plan.inputs->snapshot->getSymbolRecords())
            if (record.name == "clk")
                plan.inputs->roles.insert(
                    TemplateInputService::roleKey(record,
                                                  record.presentation.qualifiedScopePath + '.' + record.name),
                    "Clock");
        clocks = candidates({{"kind", "Signal"}, {"role", "Clock"}});
        QVERIFY2(clocks.values.contains("clk"), qPrintable(clocks.reason));
        auto parameters = candidates({{"kind", "Parameter"}});
        QVERIFY(parameters.values.contains("N"));
        auto enums = candidates({{"kind", "Choice"}, {"enumOf", "signal"}}, {{"signal", "state"}});
        qInfo() << "enum values" << enums.values;
        QVERIFY(!enums.values.isEmpty());
        QVariantMap value{{"kind", "Value"}, {"minimum", "3"}, {"maximum", "N"}, {"integer", true}};
        QVERIFY(TemplateInputService::validate(value, "4", *plan.inputs, {}).isEmpty());
        QVERIFY(!TemplateInputService::validate(value, "9", *plan.inputs, {}).isEmpty());
        QVERIFY(!TemplateInputService::validate(value, "'x", *plan.inputs, {}).isEmpty());
    }
    void slotInteraction()
    {
        MyCodeEditor editor;
        editor.resize(800, 400);
        editor.show();
        auto context = std::make_shared<TemplateInputContext>();
        context->current = [] { return true; };
        CodeTemplateSlotList placeholders{
            {"choice", 0, 1, 1, true, {{"kind", "Choice"}, {"choices", QStringList{"A", "B"}}}, context},
            {"value", 2, 1, 2, true, {{"kind", "Value"}, {"minimum", "1"}, {"maximum", "3"}}, context}};
        QString error;
        QVERIFY2(editor.insertCompletionText("A 2", -1, 0, placeholders, &error), qPrintable(error));
        QTest::keyClick(&editor, Qt::Key_Down);
        QCOMPARE(editor.toPlainText(), QString("B 2"));
        QTest::keyClick(&editor, Qt::Key_Tab);
        QCOMPARE(editor.textCursor().selectedText(), QString("2"));
        QTest::keyClicks(&editor, "9");
        QTest::keyClick(&editor, Qt::Key_Tab);
        QCOMPARE(editor.textCursor().position(), 3);
        QTest::keyClick(&editor, Qt::Key_Backspace);
        QTest::keyClicks(&editor, "3");
        QTest::keyClick(&editor, Qt::Key_Tab);
        QCOMPARE(editor.textCursor().selectedText(), QString("B"));
        QTest::keyClick(&editor, Qt::Key_Escape);
        QTest::keyClick(&editor, Qt::Key_Z, Qt::ControlModifier);
        QVERIFY(editor.toPlainText() != QString("B 3"));
        editor.document()->setModified(false);
    }
    void dependentSlots()
    {
        QTemporaryDir root;
        const auto file = root.filePath("tb.sv");
        const QString source = "module child; logic data; endmodule module tb; child a(); child b(); initial "
                               "begin @cmd\n end endmodule";
        auto request = command(file, source, "@cmd", {{"kind", "snippet"}}, {file});
        request.item.insertionText = ";";
        auto prepared = SimulationCommandPlanner::prepare(request);
        QVERIFY2(prepared.error.isEmpty(), qPrintable(prepared.error));
        MyCodeEditor editor;
        editor.show();
        CodeTemplateSlotList placeholders{
            {"instance", 0, 4, 1, true, {{"kind", "Instance"}}, prepared.inputs},
            {"signal",
             5,
             9,
             2,
             true,
             {{"kind", "Internal"}, {"scope", "instance"}, {"instanceSlot", "instance"}},
             prepared.inputs}};
        QString error;
        QVERIFY2(editor.insertCompletionText("tb.a tb.a.data", -1, 0, placeholders, &error),
                 qPrintable(error));
        QTest::keyClick(&editor, Qt::Key_Down);
        QCOMPARE(editor.toPlainText(), QString("tb.b "));
        QTest::keyClick(&editor, Qt::Key_Tab);
        QTest::keyClick(&editor, Qt::Key_Down);
        QCOMPARE(editor.toPlainText(), QString("tb.b tb.b.data"));
        QTest::keyClicks(&editor, "tb.a.data");
        QTest::keyClick(&editor, Qt::Key_Tab);
        QVERIFY(editor.textCursor().position() >= 5);
        QTest::keyClick(&editor, Qt::Key_Escape);
        editor.document()->setModified(false);
    }
    void valueAndHierarchyConstraints()
    {
        QTemporaryDir root;
        const auto file = root.filePath("tb.sv");
        const QString source =
            "module child; typedef enum logic [1:0] {IDLE, RUN} state_t; state_t state; "
            "logic [7:0] dynamic_data[]; logic [3:0] queue_data[$]; logic [2:0] associative_data[int]; "
            "endmodule\n"
            "module tb; child a(); for(genvar i=0;i<2;i++) begin:g child c(); end initial begin @cmd\n end "
            "endmodule";
        auto request = command(file, source, "@cmd", {{"kind", "snippet"}}, {file});
        request.item.insertionText = ";";
        auto plan = SimulationCommandPlanner::prepare(request);
        QVERIFY2(plan.error.isEmpty(), qPrintable(plan.error));
        const auto arrays = TemplateInputService::candidates(
            {{"kind", "Signal"}, {"scope", "hierarchy"}, {"packedRank", 1}, {"unpackedRank", 1}},
            *plan.inputs, {});
        for (const auto *name : {"tb.a.dynamic_data", "tb.a.queue_data", "tb.a.associative_data"})
            QVERIFY2(arrays.values.contains(name), name);
        const auto scalars = TemplateInputService::candidates(
            {{"kind", "Signal"}, {"scope", "hierarchy"}, {"unpackedRank", 0}}, *plan.inputs, {});
        QVERIFY(!scalars.values.contains("tb.a.dynamic_data"));
        const QVariantMap choice{{"kind", "Choice"}, {"scope", "hierarchy"}, {"enumOf", "signal"}};
        const auto hierarchical =
            TemplateInputService::candidates(choice, *plan.inputs, {{"signal", "tb.a.state"}});
        qInfo() << hierarchical.values;
        QVERIFY(hierarchical.values.contains("tb.a.IDLE"));
        QVERIFY(hierarchical.values.contains("tb.a.RUN"));
        const auto generated =
            TemplateInputService::candidates(choice, *plan.inputs, {{"signal", "tb.g[1].c.state"}});
        QVERIFY(generated.values.contains("tb.g[1].c.IDLE"));
        const QVariantMap value{{"kind", "Value"}};
        QVERIFY2(TemplateInputService::validate(value, "\"message\"", *plan.inputs, {}).isEmpty(),
                 qPrintable(TemplateInputService::validate(value, "\"message\"", *plan.inputs, {})));
        QVERIFY(
            TemplateInputService::validate({{"kind", "Value"}, {"integer", true}}, "1.5", *plan.inputs, {})
                .size());
        QVERIFY(TemplateInputService::validate({{"kind", "Value"}, {"minWidth", 8}, {"maxWidth", 8}}, "8'hxz",
                                               *plan.inputs, {})
                    .size());
        QVERIFY(TemplateInputService::validate(
                    {{"kind", "Value"}, {"minWidth", 8}, {"maxWidth", 8}, {"known", false}}, "8'hxz",
                    *plan.inputs, {})
                    .isEmpty());
        const QVariantMap time{{"kind", "Value"}, {"unit", "ns"}, {"minimum", "1"}, {"maximum", "3"}};
        QVERIFY(TemplateInputService::validate(time, "2ns", *plan.inputs, {}).isEmpty());
        QVERIFY(!TemplateInputService::validate(time, "4ns", *plan.inputs, {}).isEmpty());
        auto event = command(file, QString(source).replace("@cmd", "@tb.a.state"), "@tb.a.state",
                             {{"kind", "snippet"}}, {file});
        event.item.insertionText = ";";
        QVERIFY(SimulationCommandPlanner::prepare(event).error.contains("event signal"));
    }
    void savedAssetReferences()
    {
        QTemporaryDir root;
        WorkspaceManager workspace;
        workspace.setRecentWorkspacePersistenceEnabledForTesting(false);
        QVERIFY(workspace.openWorkspace(root.path()));
        QWidget owner;
        XipsHostBridge bridge(nullptr, &workspace, &owner);
        const auto package = root.filePath("lib/user.sv");
        QVERIFY(put(package, "package p; endpackage\n"));
        const QVariantMap receipt{{"schema", "xips.use/v1"},         {"assetId", "asset"},
                                  {"revision", "revision-a"},        {"contentHash", "sha256:pinned"},
                                  {"path", root.filePath("lib")},    {"workspace", root.path()},
                                  {"files", QStringList{"user.sv"}}, {"name", "User package"}};
        QVERIFY2(bridge.exportCompleted(receipt).isEmpty(), qPrintable(bridge.exportCompleted(receipt)));
        const auto original = bytes(root.filePath(".zeroslack/xips-references.json"));
        auto state = XipsHostBridge::sourceBindings(root.path(), {package});
        QCOMPARE(state.size(), 1);
        QCOMPARE(state.first().toMap().value("localState").toString(), QString("unchanged"));
        QVERIFY(put(package, "package p; parameter int N=7; endpackage\n"));
        QCOMPARE(XipsHostBridge::sourceBindings(root.path(), {package})
                     .first()
                     .toMap()
                     .value("localState")
                     .toString(),
                 QString("modified"));
        QVERIFY(bridge.exportCompleted(receipt).isEmpty());
        QCOMPARE(bytes(root.filePath(".zeroslack/xips-references.json")), original);
        auto conflict = receipt;
        conflict.insert("contentHash", "sha256:changed");
        QVERIFY(!bridge.exportCompleted(conflict).isEmpty());
        QCOMPARE(bytes(root.filePath(".zeroslack/xips-references.json")), original);
        auto escape = receipt;
        escape.insert("revision", "other");
        escape.insert("files", QStringList{"../outside.sv"});
        QVERIFY(!bridge.exportCompleted(escape).isEmpty());
    }
    void assetDependencyGuards()
    {
        QTemporaryDir root, library;
        const auto header = root.filePath("pkg/defs.svh");
        const auto base = root.filePath("pkg/base.sv");
        const auto package = root.filePath("pkg/user_package.sv");
        const QString baseText = "package base; parameter int N=1; endpackage\n";
        const QString text =
            "`include \"defs.svh\"\npackage p; import base::*; parameter int V=`VALUE+N; endpackage\n";
        QVERIFY(put(header, "`define VALUE 2\n"));
        QVERIFY(put(base, baseText));
        QVERIFY(put(package, text));
        SimulationCommandRequest context;
        context.includeDirectories = {QFileInfo(header).absolutePath()};
        context.contents.insert(package, text);
        auto plan =
            XipsAssetSync::prepare(root.path(), {base, package}, context, {}, library.path(), "Package");
        QVERIFY2(plan.error.isEmpty(), qPrintable(plan.error));
        const auto inputs = plan.operation.value("request").toMap().value("files").toList();
        QCOMPARE(inputs.size(), 3);
        QStringList seen;
        for (const auto &entry : inputs)
        {
            const auto file = entry.toMap();
            for (const auto &dependency : file.value("dependencies").toStringList())
                QVERIFY(seen.contains(dependency));
            seen << file.value("path").toString();
        }
        QVERIFY(seen.indexOf("defs.svh") < seen.indexOf("user_package.sv"));
        QVERIFY(seen.indexOf("base.sv") < seen.indexOf("user_package.sv"));
        context.contents[package] += "// unsaved\n";
        QVERIFY(!XipsAssetSync::prepare(root.path(), {base, package}, context, {}, library.path(), "Package")
                     .error.isEmpty());
        context.contents.clear();
        QVERIFY(put(header, "`define VALUE 3\n"));
        QVERIFY(!XipsAssetSync::inputsCurrent(plan.operation).isEmpty());
        QVERIFY(!XipsAssetSync::prepare(root.path(), {package, base}, context, {}, library.path(), "Package")
                     .error.isEmpty());
        QVERIFY(QFile::remove(header));
        QVERIFY(!XipsAssetSync::prepare(root.path(), {base, package}, context, {}, library.path(), "Package")
                     .error.isEmpty());
    }
    void assetAdapterTransactions()
    {
        if (!QFileInfo(XipsAssetSync::executable()).isFile())
            QSKIP("Supply the frozen independent asset adapter.");
        QTemporaryDir root, other, state;
        const auto oldState = qgetenv("XIPS_ASSET_ADAPTER_STATE");
        qputenv("XIPS_ASSET_ADAPTER_STATE", state.path().toUtf8());
        const auto restore = qScopeGuard([&] { qputenv("XIPS_ASSET_ADAPTER_STATE", oldState); });
        const auto library = qEnvironmentVariable("XIPS_LIBRARY");
        const auto file = root.filePath("pkg/user_package.sv");
        const QString source = "package adapter_p; parameter int N=1; endpackage\n";
        QVERIFY(put(file, source));
        SimulationCommandRequest context;
        auto plan = XipsAssetSync::prepare(root.path(), {file}, context, {}, library,
                                           "ZeroSlack package integration");
        QVERIFY2(plan.error.isEmpty(), qPrintable(plan.error));
        const auto run = [&](const QVariantMap &operation, const QString &action)
        {
            const auto result = XipsAssetSync::execute(operation, action);
            evidence("assets/" + operation.value("requestId").toString() + "-" + action + ".json",
                     QJsonDocument::fromVariant(result).toJson());
            return result;
        };
        auto preview = run(plan.operation, "preview");
        QCOMPARE(preview.value("state").toString(), QString("previewed"));
        auto published = run(preview.value("operation").toMap(), "commit");
        QVERIFY2(published.value("referenceApplied").toBool(),
                 qPrintable(QString::fromUtf8(QJsonDocument::fromVariant(published).toJson())));
        auto first = XipsHostBridge::sourceBindings(root.path(), {file}).first().toMap();
        const auto firstRevision = first.value("revision").toString();
        const auto otherFile = other.filePath("pkg/user_package.sv");
        QVERIFY(put(otherFile, source));
        QVERIFY(XipsHostBridge::recordPublishedVersion(other.path(), {}, first, "pin-other").isEmpty());
        const auto otherReferences = bytes(other.filePath(".zeroslack/xips-references.json"));
        auto repeated = run(published.value("operation").toMap(), "operationStatus");
        QVERIFY(repeated.value("referenceApplied").toBool());
        QCOMPARE(repeated.value("receipt").toMap().value("revision").toString(), firstRevision);
        QVERIFY(put(file, source + "// second version\n"));
        first = XipsHostBridge::sourceBindings(root.path(), {file}).first().toMap();
        plan = XipsAssetSync::prepare(root.path(), {file}, context, first, library, "");
        QVERIFY2(plan.error.isEmpty(), qPrintable(plan.error));
        preview = run(plan.operation, "preview");
        QCOMPARE(preview.value("state").toString(), QString("previewed"));
        const auto rivalPlan = XipsAssetSync::prepare(
            other.path(), {otherFile}, context,
            XipsHostBridge::sourceBindings(other.path(), {otherFile}).first().toMap(), library, "");
        QVERIFY2(rivalPlan.error.isEmpty(), qPrintable(rivalPlan.error));
        const auto rivalPreview = run(rivalPlan.operation, "preview");
        QCOMPARE(rivalPreview.value("state").toString(), QString("previewed"));
        QLockFile referenceLock(root.filePath(".zeroslack/xips-references.json.lock"));
        QVERIFY(referenceLock.tryLock(0));
        published = run(preview.value("operation").toMap(), "commit");
        referenceLock.unlock();
        QCOMPARE(published.value("state").toString(), QString("committed"));
        QVERIFY(!published.value("referenceApplied").toBool());
        QVERIFY(!published.value("referenceError").toString().isEmpty());
        QCOMPARE(
            XipsHostBridge::sourceBindings(root.path(), {file}).first().toMap().value("revision").toString(),
            firstRevision);
        QVERIFY(put(file, source + "// edit after publication\n"));
        auto recovered = run(published.value("operation").toMap(), "operationStatus");
        QVERIFY2(recovered.value("referenceApplied").toBool(),
                 qPrintable(recovered.value("referenceError").toString()));
        const auto current = XipsHostBridge::sourceBindings(root.path(), {file}).first().toMap();
        QVERIFY(current.value("revision").toString() != firstRevision);
        QCOMPARE(current.value("localState").toString(), QString("modified"));
        QCOMPARE(bytes(other.filePath(".zeroslack/xips-references.json")), otherReferences);
        const auto conflict = run(rivalPreview.value("operation").toMap(), "commit");
        QCOMPARE(conflict.value("state").toString(), QString("conflict"));
        QCOMPARE(bytes(other.filePath(".zeroslack/xips-references.json")), otherReferences);
        plan = XipsAssetSync::prepare(root.path(), {file}, context, current, library, "");
        QVERIFY(plan.error.isEmpty());
        preview = run(plan.operation, "preview");
        const auto cancelled = run(preview.value("operation").toMap(), "cancel");
        QCOMPARE(cancelled.value("state").toString(), QString("cancelled"));
        QCOMPARE(cancelled.value("publication").toString(), QString("none"));
        QCOMPARE(XipsHostBridge::sourceBindings(root.path(), {file}).first().toMap().value("revision"),
                 current.value("revision"));
    }
    void assetSyncMainWindow()
    {
        if (!QFileInfo(XipsAssetSync::executable()).isFile())
            QSKIP("Supply the frozen independent asset adapter.");
        QTemporaryDir root, state;
        const auto oldState = qgetenv("XIPS_ASSET_ADAPTER_STATE");
        qputenv("XIPS_ASSET_ADAPTER_STATE", state.path().toUtf8());
        const auto restore = qScopeGuard([&] { qputenv("XIPS_ASSET_ADAPTER_STATE", oldState); });
        const auto package = root.filePath("gui_package.sv");
        QVERIFY(put(package, "package gui_package; task ping(); endtask endpackage\n"));
        MainWindow window;
        window.resize(1280, 860);
        window.show();
        QVERIFY(window.workspaceSessionCoordinator->openWorkspace(root.path()));
        QVERIFY(window.tabManager->openFileInTab(package));
        window.simulationCommands->manage();
        auto *manager = window.findChild<TemplateManagerDialog *>();
        QVERIFY(manager);
        manager->packages->setPlainText(package);
        manager->name->setText("GUI package");
        auto *update = manager->findChild<QPushButton *>("updateInXips");
        QVERIFY(update && update->isEnabled());
        update->click();
        auto *sync = manager->findChild<XipsAssetSyncDialog *>();
        QVERIFY(sync);
        sync->findChild<QLineEdit *>("assetLibrary")->setText(qEnvironmentVariable("XIPS_LIBRARY"));
        auto *preview = sync->findChild<QPushButton *>("assetPreviewButton");
        auto *commit = sync->findChild<QPushButton *>("assetCommitButton");
        QVERIFY(preview && commit);
        preview->click();
        QTRY_VERIFY_WITH_TIMEOUT(!sync->isBusy(), 45000);
        QVERIFY2(commit->isEnabled(), qPrintable(sync->findChild<QLabel *>("assetSyncStatus")->text()));
        screenshot(sync, "asset-preview.png");
        commit->click();
        QTRY_VERIFY_WITH_TIMEOUT(!sync->isBusy(), 45000);
        QVERIFY2(QFileInfo::exists(root.filePath(".zeroslack/xips-references.json")),
                 qPrintable(sync->findChild<QLabel *>("assetSyncStatus")->text()));
        screenshot(sync, "asset-published.png");
        evidence("assets/gui-reference.json", bytes(root.filePath(".zeroslack/xips-references.json")));
        const auto originalBinding = XipsHostBridge::sourceBindings(root.path(), {package}).first().toMap();
        QPointer<XipsAssetSyncDialog> oldDialog = sync;
        sync->reject();
        QTRY_VERIFY(oldDialog.isNull());
        auto *editor = window.tabManager->getCurrentEditor();
        editor->moveCursor(QTextCursor::End);
        editor->insertPlainText("// saved second version\n");
        QVERIFY(window.tabManager->saveCurrentTab());
        update->click();
        sync = manager->findChild<XipsAssetSyncDialog *>();
        QVERIFY(sync);
        preview = sync->findChild<QPushButton *>("assetPreviewButton");
        commit = sync->findChild<QPushButton *>("assetCommitButton");
        preview->click();
        QTRY_VERIFY_WITH_TIMEOUT(!sync->isBusy(), 45000);
        QVERIFY2(commit->isEnabled(), qPrintable(sync->findChild<QLabel *>("assetSyncStatus")->text()));
        commit->click();
        QTRY_VERIFY_WITH_TIMEOUT(!sync->isBusy(), 45000);
        auto latest = XipsHostBridge::sourceBindings(root.path(), {package}).first().toMap();
        QVERIFY2(latest.value("revision") != originalBinding.value("revision"),
                 qPrintable(sync->findChild<QLabel *>("assetSyncStatus")->text()));
        screenshot(&window, "mainwindow-asset-sync.png");
        int expectedVersions = 2;
#ifdef Q_OS_WIN
        oldDialog = sync;
        sync->reject();
        QTRY_VERIFY(oldDialog.isNull());
        editor->moveCursor(QTextCursor::End);
        editor->insertPlainText("// third version, recover its reference\n");
        QVERIFY(window.tabManager->saveCurrentTab());
        update->click();
        sync = manager->findChild<XipsAssetSyncDialog *>();
        QVERIFY(sync);
        preview = sync->findChild<QPushButton *>("assetPreviewButton");
        commit = sync->findChild<QPushButton *>("assetCommitButton");
        preview->click();
        QTRY_VERIFY_WITH_TIMEOUT(!sync->isBusy(), 45000);
        QVERIFY2(commit->isEnabled(), qPrintable(sync->findChild<QLabel *>("assetSyncStatus")->text()));
        const auto referencePath = root.filePath(".zeroslack/xips-references.json");
        const auto priorReferences = bytes(referencePath);
        const auto savedSource = bytes(package);
        // Model a noncooperating reader that denies Windows delete sharing.
        QFile externalReader(referencePath);
        QVERIFY(externalReader.open(QIODevice::ReadOnly));
        commit->click();
        QTRY_VERIFY_WITH_TIMEOUT(!sync->isBusy(), 45000);
        QCOMPARE(bytes(referencePath), priorReferences);
        QCOMPARE(bytes(package), savedSource);
        QVERIFY(!commit->isEnabled() && !preview->isEnabled());
        const auto retained = XipsAssetSync::recoverable(root.path());
        QCOMPARE(retained.size(), 1);
        const auto pending = retained.first().toMap();
        QCOMPARE(pending.value("status").toString(), QString("published_pending_reference"));
        const auto saveFailure = pending.value("lastReferenceFailure").toMap();
        QCOMPARE(saveFailure.value("stage").toString(), QString("commit"));
        QCOMPARE(saveFailure.value("qtFileError").toInt(), int(QFileDevice::RenameError));
        QVERIFY(!saveFailure.value("systemError").toString().isEmpty());
        QVERIFY(sync->findChild<QLabel *>("assetSyncStatus")
                    ->text()
                    .contains(saveFailure.value("systemError").toString()));
        const auto requestId = pending.value("requestId").toString();
        const auto receipt = pending.value("lastResponse").toMap().value("receipt").toMap();
        QVERIFY(receipt.value("revision") != latest.value("revision"));
        evidence("assets/gui-published-pending-reference.json", QJsonDocument::fromVariant(pending).toJson());
        screenshot(sync, "asset-reference-failed.png");
        oldDialog = sync;
        sync->reject();
        QTRY_VERIFY(oldDialog.isNull());
        externalReader.close();
        update->click();
        sync = manager->findChild<XipsAssetSyncDialog *>();
        QVERIFY(sync);
        auto *recover = sync->findChild<QPushButton *>("assetRecoverButton");
        QVERIFY(recover && recover->isEnabled());
        bool recoverySelected = false;
        QTimer selectRecovery;
        selectRecovery.setInterval(20);
        connect(&selectRecovery, &QTimer::timeout, this,
                [&]
                {
                    auto *picker = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
                    if (picker && picker->textValue().contains(requestId))
                    {
                        recoverySelected = true;
                        picker->accept();
                        selectRecovery.stop();
                    }
                });
        selectRecovery.start();
        QTimer::singleShot(10000, &selectRecovery,
                           [&]
                           {
                               if (selectRecovery.isActive())
                                   if (auto *picker =
                                           qobject_cast<QInputDialog *>(QApplication::activeModalWidget()))
                                       picker->reject();
                           });
        recover->click();
        selectRecovery.stop();
        QVERIFY(recoverySelected);
        QTRY_VERIFY_WITH_TIMEOUT(!sync->isBusy(), 45000);
        latest = XipsHostBridge::sourceBindings(root.path(), {package}).first().toMap();
        QCOMPARE(latest.value("revision"), receipt.value("revision"));
        QCOMPARE(latest.value("lastOperation").toString(), requestId);
        QCOMPARE(bytes(package), savedSource);
        QVERIFY(XipsAssetSync::recoverable(root.path()).isEmpty());
        const auto recovered = QJsonDocument::fromJson(
                                   bytes(root.filePath(".zeroslack/asset-operations/" + requestId + ".json")))
                                   .toVariant()
                                   .toMap();
        QCOMPARE(recovered.value("lastAction").toString(), QString("operationStatus"));
        QCOMPARE(recovered.value("status").toString(), QString("applied"));
        QCOMPARE(recovered.value("lastResponse").toMap().value("receipt").toMap(), receipt);
        QCOMPARE(recovered.value("lastReferenceFailure").toMap(), saveFailure);
        evidence("assets/gui-reference-recovered.json", QJsonDocument::fromVariant(recovered).toJson());
        screenshot(sync, "asset-reference-recovered.png");
        expectedVersions = 3;
#endif
        sync->reject();
        manager->reject();
        QCoreApplication::processEvents();
        if (!qEnvironmentVariableIsEmpty("XIPS_BROWSER_LIBRARY"))
        {
            QTemporaryDir other;
            const auto original = other.filePath("gui_package.sv");
            QVERIFY(put(original, "package gui_package; task ping(); endtask endpackage\n"));
            QVERIFY(XipsHostBridge::recordPublishedVersion(other.path(), {}, originalBinding,
                                                           "explicit-original-pin")
                        .isEmpty());
            const auto pinnedBytes = bytes(original);
            QVERIFY(window.workspaceSessionCoordinator->openWorkspace(other.path()));
            window.simulationCommands->showBrowser();
            auto *host = window.simulationCommands->view();
            QVERIFY(host && host->isReady());
            auto *browser = host->component();
            QTRY_VERIFY_WITH_TIMEOUT(host->canClose(nullptr), 15000);
            QVERIFY(QMetaObject::invokeMethod(browser, "revealAsset",
                                              Q_ARG(QString, latest.value("assetId").toString())));
            auto *versions = browser->findChild<QTableView *>("revisionTable");
            auto *pages = browser->findChild<QTabWidget *>("assetPages");
            QVERIFY(versions && pages);
            pages->setCurrentIndex(1);
            QTRY_VERIFY_WITH_TIMEOUT(versions->model()->rowCount() >= 2, 15000);
            QCOMPARE(versions->model()->rowCount(), expectedVersions);
            evidence(
                "assets/gui-version-count.json",
                QJsonDocument::fromVariant(QVariantMap{{"assetId", latest.value("assetId")},
                                                       {"versionCount", versions->model()->rowCount()},
                                                       {"expectedVersions", expectedVersions},
                                                       {"revisionAfterRecovery", latest.value("revision")}})
                    .toJson());
            QModelIndex target;
            for (int row = 0; row < versions->model()->rowCount(); ++row)
            {
                auto index = versions->model()->index(row, 0);
                if (index.data(Qt::UserRole).toString() == latest.value("revision").toString())
                    target = index;
            }
            QVERIFY(target.isValid());
            versions->setCurrentIndex(target);
            auto *take = browser->findChild<QAbstractButton *>("takeButton");
            QVERIFY(take);
            QTRY_VERIFY(take->isVisible() && take->isEnabled());
            const auto destination = other.filePath("chosen-update");
            bool seen = false;
            QTimer form;
            form.setInterval(20);
            connect(&form, &QTimer::timeout, this,
                    [&]
                    {
                        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                        if (!dialog)
                            return;
                        auto *field = dialog->findChild<QLineEdit *>("exportDestination");
                        auto *accept = dialog->findChild<QAbstractButton *>("formAccept");
                        if (field && accept && accept->isEnabled())
                        {
                            field->setText(destination);
                            seen = true;
                            accept->click();
                        }
                    });
            form.start();
            take->click();
            QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(QDir(destination).filePath("gui_package.sv")), 15000);
            form.stop();
            QVERIFY(seen);
            QTRY_VERIFY_WITH_TIMEOUT(host->canClose(nullptr), 15000);
            QCOMPARE(bytes(original), pinnedBytes);
            QCOMPARE(
                XipsHostBridge::sourceBindings(other.path(), {original}).first().toMap().value("revision"),
                originalBinding.value("revision"));
            const auto adopted =
                XipsHostBridge::sourceBindings(other.path(), {QDir(destination).filePath("gui_package.sv")});
            QCOMPARE(adopted.size(), 1);
            QCOMPARE(adopted.first().toMap().value("revision"), latest.value("revision"));
            QCOMPARE(adopted.first().toMap().value("compileOrder").toStringList(),
                     QStringList{"gui_package.sv"});
            screenshot(&window, "mainwindow-explicit-version-adoption.png");
            evidence("assets/second-project-explicit-adoption.json",
                     bytes(other.filePath(".zeroslack/xips-references.json")));
        }
    }
    void builtInPackagesAsOrdinaryAssets()
    {
        if (!QFileInfo(XipsAssetSync::executable()).isFile())
            QSKIP("Supply the frozen independent asset adapter.");
        QTemporaryDir root, state;
        const auto oldState = qgetenv("XIPS_ASSET_ADAPTER_STATE");
        qputenv("XIPS_ASSET_ADAPTER_STATE", state.path().toUtf8());
        const auto restore = qScopeGuard([&] { qputenv("XIPS_ASSET_ADAPTER_STATE", oldState); });
        for (const auto &kind : {QString("driver"), QString("print")})
        {
            QStringList sources;
            const auto supportError = SimulationCommandService::materializeSupport(root.path(), kind, &sources);
            QVERIFY2(supportError.isEmpty(), qPrintable(kind + ": " + supportError));
            if (kind == "driver")
                for (const auto *name : {"xips-commands.json", "LICENSE", "NOTICE", "THIRD-PARTY-NOTICES.md"})
                    sources << QDir(QFileInfo(sources.first()).absolutePath()).filePath(name);
            const auto plan = XipsAssetSync::prepare(root.path(), sources, {}, {},
                                                     qEnvironmentVariable("XIPS_LIBRARY"), "Builtin " + kind);
            QVERIFY2(plan.error.isEmpty(), qPrintable(plan.error));
            const auto preview = XipsAssetSync::execute(plan.operation, "preview");
            QCOMPARE(preview.value("state").toString(), QString("previewed"));
            const auto commit = XipsAssetSync::execute(preview.value("operation").toMap(), "commit");
            evidence("assets/builtin-" + kind + ".json", QJsonDocument::fromVariant(commit).toJson());
            QVERIFY2(commit.value("referenceApplied").toBool(),
                     qPrintable(QString::fromUtf8(QJsonDocument::fromVariant(commit).toJson())));
            const auto binding = XipsHostBridge::sourceBindings(root.path(), sources).first().toMap();
            QCOMPARE(binding.value("compileOrder").toStringList().size(), kind == "driver" ? 4 : 1);
            const auto unchanged = XipsAssetSync::prepare(root.path(), sources, {}, binding,
                                                          qEnvironmentVariable("XIPS_LIBRARY"), "");
            QVERIFY2(unchanged.error.isEmpty(), qPrintable(unchanged.error));
            const auto samePreview = XipsAssetSync::execute(unchanged.operation, "preview");
            const auto same = XipsAssetSync::execute(samePreview.value("operation").toMap(), "commit");
            QCOMPARE(same.value("state").toString(), QString("reused"));
            QVERIFY(same.value("referenceApplied").toBool());
        }
    }
    void assetReferenceConcurrentReaders()
    {
        QTemporaryDir root;
        const auto file = root.filePath("package.sv");
        const QString source(8 * 1024 * 1024, QLatin1Char(' '));
        QVERIFY(put(file, source));
        QVariantMap before{
            {"library", root.filePath("library")},
            {"assetId", "asset"},
            {"revision", "r0"},
            {"contentHash", "sha256:initial"},
            {"path", "."},
            {"files", QStringList{"package.sv"}},
            {"baseFiles",
             QVariantMap{
                 {"package.sv",
                  QString::fromLatin1(
                      QCryptographicHash::hash(source.toUtf8(), QCryptographicHash::Sha256).toHex())}}},
            {"singleFile", false}};
        QVERIFY(XipsHostBridge::recordPublishedVersion(root.path(), {}, before, "initial").isEmpty());
        before.insert("lastOperation", "initial");
        std::atomic_bool stop = false;
        QSemaphore started;
        int reads = 0, invalidReads = 0;
        std::thread reader(
            [&]
            {
                do
                {
                    const auto bindings = XipsHostBridge::sourceBindings(root.path(), {file});
                    if (bindings.size() != 1 || bindings.first().toMap().value("localState") != "unchanged")
                        ++invalidReads;
                    if (++reads == 1)
                        started.release();
                } while (!stop.load());
            });
        started.acquire();
        QVariantList failures;
        for (int i = 1; i <= 64; ++i)
        {
            const auto request = QString("update-%1").arg(i);
            auto next = before;
            next.insert("revision", QString("r%1").arg(i));
            next.insert("contentHash", QString("sha256:%1").arg(i));
            QVariantMap detail;
            const auto error =
                XipsHostBridge::recordPublishedVersion(root.path(), before, next, request, &detail);
            if (error.isEmpty())
            {
                before = next;
                before.insert("lastOperation", request);
            }
            else
            {
                detail.insert("message", error);
                detail.insert("requestId", request);
                failures << detail;
            }
        }
        stop.store(true);
        reader.join();
        evidence("assets/concurrent-reference-reads.json",
                 QJsonDocument::fromVariant(QVariantMap{{"writeAttempts", 64},
                                                        {"reads", reads},
                                                        {"invalidReads", invalidReads},
                                                        {"failures", failures}})
                     .toJson());
        QCOMPARE(invalidReads, 0);
        QCOMPARE(failures.size(), 0);
        QCOMPARE(XipsHostBridge::sourceBindings(root.path(), {file}).first().toMap().value("revision"),
                 before.value("revision"));
    }
    void assetReferenceRecovery()
    {
        QTemporaryDir root, other;
        const auto package = root.filePath("lib/package.sv");
        const auto otherPackage = other.filePath("lib/package.sv");
        const QString original = "package p; parameter int N=1; endpackage\n";
        QVERIFY(put(package, original));
        QVERIFY(put(otherPackage, original));
        const auto hash = [](const QByteArray &bytes)
        { return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()); };
        QVariantMap first{{"library", root.filePath("library")},
                          {"assetId", "asset"},
                          {"revision", "r1"},
                          {"contentHash", "sha256:first"},
                          {"path", "lib"},
                          {"files", QStringList{"package.sv"}},
                          {"baseFiles", QVariantMap{{"package.sv", hash(original.toUtf8())}}},
                          {"singleFile", false}};
        QVERIFY(XipsHostBridge::recordPublishedVersion(root.path(), {}, first, "register").isEmpty());
        QVERIFY(XipsHostBridge::recordPublishedVersion(other.path(), {}, first, "pin-other").isEmpty());
        const auto before = XipsHostBridge::sourceBindings(root.path(), {package}).first().toMap();
        const QString next = "package p; parameter int N=2; endpackage\n";
        QVERIFY(put(package, next));
        auto published = before;
        published.insert("revision", "r2");
        published.insert("contentHash", "sha256:second");
        published.insert("baseFiles", QVariantMap{{"package.sv", hash(next.toUtf8())}});
        const auto referenceFile = root.filePath(".zeroslack/xips-references.json");
        const auto priorBytes = bytes(referenceFile);
        QLockFile lock(referenceFile + ".lock");
        QVERIFY(lock.tryLock(0));
        QVERIFY(!XipsHostBridge::recordPublishedVersion(root.path(), before, published, "update").isEmpty());
        QCOMPARE(bytes(referenceFile), priorBytes);
        lock.unlock();
        // Publication has succeeded; a later local edit must remain modified after recovery.
        QVERIFY(put(package, next + "// local edit after publication\n"));
        QVERIFY(XipsHostBridge::recordPublishedVersion(root.path(), before, published, "update").isEmpty());
        const auto committedBytes = bytes(referenceFile);
        QVERIFY(XipsHostBridge::recordPublishedVersion(root.path(), before, published, "update").isEmpty());
        QCOMPARE(bytes(referenceFile), committedBytes);
        const auto current = XipsHostBridge::sourceBindings(root.path(), {package}).first().toMap();
        QCOMPARE(current.value("revision").toString(), QString("r2"));
        QCOMPARE(current.value("localState").toString(), QString("modified"));
        QCOMPARE(XipsHostBridge::sourceBindings(other.path(), {otherPackage})
                     .first()
                     .toMap()
                     .value("revision")
                     .toString(),
                 QString("r1"));
        auto competing = published;
        competing.insert("revision", "r3");
        QVERIFY(
            !XipsHostBridge::recordPublishedVersion(root.path(), before, competing, "competing").isEmpty());
        QVERIFY(!XipsHostBridge::recordPublishedVersion(root.path(), before, competing, "update").isEmpty());
        QCOMPARE(bytes(referenceFile), committedBytes);
        auto unsafe = published;
        unsafe.insert("path", "../escape");
        QVERIFY(!XipsHostBridge::recordPublishedVersion(root.path(), {}, unsafe, "unsafe").isEmpty());
        auto partial = published;
        partial.insert("baseFiles", QVariantMap{});
        QVERIFY(!XipsHostBridge::recordPublishedVersion(root.path(), {}, partial, "partial").isEmpty());
        QVERIFY(put(referenceFile, "broken"));
        QVERIFY(!XipsHostBridge::recordPublishedVersion(root.path(), {}, first, "recover-invalid").isEmpty());
        QCOMPARE(bytes(referenceFile), QByteArray("broken"));
    }
    void staleEditorResults()
    {
        QTemporaryDir root;
        const auto file = root.filePath("tb.sv");
        const QString source = "module tb; initial begin @q1\n end endmodule";
        QVERIFY(put(file, source));
        MainWindow window;
        window.show();
        QVERIFY(window.workspaceSessionCoordinator->openWorkspace(root.path()));
        QVERIFY(window.tabManager->openFileInTab(file));
        auto *editor = window.tabManager->getCurrentEditor();
        auto *service = UserTemplateService::getInstance();
        UserTemplateRecord record;
        record.id = "guard";
        record.commandToken = record.label = "@q1";
        record.insertText = "$display(\"guard\");";
        record.command = {{"kind", "snippet"}, {"file", file}};
        QVERIFY(service->saveRecord(record, true).valid);
        const auto select = [&]
        {
            QTextCursor cursor(editor->document());
            cursor.setPosition(source.indexOf("@q1") + 3);
            editor->setTextCursor(cursor);
            auto request = window.simulationCommands->currentRequest();
            request.context.simulationCommands = true;
            request.context.replacementStart = source.indexOf("@q1");
            request.context.replacementLength = 3;
            return EditorInsertPaletteService()
                .query(GlobalControlCategory::Templates, "q1", request.context)
                .first();
        };
        QSignalSpy messages(window.simulationCommands.get(), &SimulationCommandService::message);
        auto item = select();
        window.simulationCommands->activate(item);
        editor->setReadOnly(true);
        QTRY_VERIFY_WITH_TIMEOUT(messages.size() > 0, 10000);
        QCOMPARE(editor->toPlainText(), source);
        editor->setReadOnly(false);
        messages.clear();
        item = select();
        window.simulationCommands->activate(item);
        editor->insertPlainText(" ");
        QTRY_VERIFY_WITH_TIMEOUT(messages.size() > 0, 10000);
        QVERIFY(!editor->toPlainText().contains("$display"));
        editor->undo();
        messages.clear();
        item = select();
        window.simulationCommands->activate(item);
        const auto other = root.filePath("other.sv");
        QVERIFY(put(other, "module other; endmodule"));
        QVERIFY(window.tabManager->openFileInTab(other));
        QTRY_VERIFY_WITH_TIMEOUT(messages.size() > 0, 10000);
        QCOMPARE(editor->toPlainText(), source);
        messages.clear();
        QVERIFY(window.tabManager->openFileInTab(file));
        item = select();
        window.simulationCommands->activate(item);
        window.simulationCommands->prepare();
        QTest::qWait(200);
        QCOMPARE(editor->toPlainText(), source);
        QTemporaryDir anotherWorkspace;
        item = select();
        window.simulationCommands->activate(item);
        editor->document()->setModified(false);
        QVERIFY(window.workspaceSessionCoordinator->openWorkspace(anotherWorkspace.path()));
        QTest::qWait(200);
        QCOMPARE(bytes(file), source.toUtf8());
        QVERIFY(window.workspaceSessionCoordinator->openWorkspace(root.path()));
        QVERIFY(window.tabManager->openFileInTab(file));
        editor = window.tabManager->getCurrentEditor();
        item = select();
        window.simulationCommands->activate(item);
        editor->document()->setModified(false);
        QVERIFY(window.tabManager->closeAllTabs());
        QTRY_VERIFY_WITH_TIMEOUT(messages.size() > 0, 10000);
        window.close();
    }
    void unsavedDocumentExpansion()
    {
        MainWindow window;
        window.show();
        window.tabManager->createNewTab();
        auto *editor = window.tabManager->getCurrentEditor();
        QVERIFY(editor);
        const QString source = "module tb; initial begin @unsaved\n end endmodule";
        editor->setPlainText(source);
        auto *templates = UserTemplateService::getInstance();
        UserTemplateRecord record;
        record.id = "unsaved-guard";
        record.commandToken = record.label = "@unsaved";
        record.insertText = "#1ns;";
        record.command = {{"kind", "snippet"}};
        QVERIFY(templates->saveRecord(record, false).valid);
        QTextCursor cursor(editor->document());
        cursor.setPosition(source.indexOf("@unsaved") + 8);
        editor->setTextCursor(cursor);
        auto request = window.simulationCommands->currentRequest();
        request.context.simulationCommands = true;
        request.context.replacementStart = source.indexOf("@unsaved");
        request.context.replacementLength = 8;
        const auto items =
            EditorInsertPaletteService().query(GlobalControlCategory::Templates, "unsaved", request.context);
        QVERIFY(!items.isEmpty());
        QSignalSpy messages(window.simulationCommands.get(), &SimulationCommandService::message);
        window.simulationCommands->activate(items.first());
        QTRY_VERIFY_WITH_TIMEOUT(messages.size() > 0, 10000);
        QVERIFY2(editor->toPlainText() == QString(source).replace("@unsaved", "#1ns;"),
                 qPrintable(messages.first().first().toString() + " | " + editor->documentFileName()));
        QTest::keyClick(editor, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(editor->toPlainText(), source);
        editor->document()->setModified(false);
        window.close();
    }
    void eventFiltersAndTheme()
    {
        QSettings().remove("simulation/events");
        simdock::SimulationEventsPanel panel;
        panel.resize(1100, 360);
        panel.show();
        panel.startRun("filters");
        simdock::SimulationEvent first;
        first.runId = "filters";
        first.timePs = "1000";
        first.level = "Info";
        first.tag = "clock";
        first.instance = "tb.a";
        first.message = "edge";
        first.sourceFile = "tb.sv";
        first.sourceLine = 7;
        auto second = first;
        second.level = "Error";
        second.tag = "data";
        second.instance = "tb.b";
        second.message = "changed";
        panel.append({first, first, second});
        auto *table = panel.findChild<QTableView *>("simulationEventTable");
        QVERIFY(table);
        QCOMPARE(table->model()->rowCount(), 3);
        auto *search = panel.findChild<QLineEdit *>("simulationEventFilter");
        search->setText("tb.b");
        QCOMPARE(table->model()->rowCount(), 1);
        search->clear();
        auto *level = panel.findChild<QComboBox *>("simulationEventLevel");
        level->setCurrentText("Error");
        QCOMPARE(table->model()->rowCount(), 1);
        level->setCurrentIndex(0);
        auto *group = panel.findChild<QComboBox *>("simulationEventGroup");
        group->setCurrentIndex(3);
        QCOMPARE(table->model()->index(0, 3).data().toString(), QString("tb.a"));
        auto *merge = panel.findChild<QCheckBox *>();
        QVERIFY(merge);
        QTest::mouseClick(merge, Qt::LeftButton);
        QCOMPARE(table->model()->rowCount(), 2);
        QSignalSpy source(&panel, &simdock::SimulationEventsPanel::sourceRequested);
        QModelIndex index = table->model()->index(0, 0);
        QVERIFY(QMetaObject::invokeMethod(table, "doubleClicked", Q_ARG(QModelIndex, index)));
        QCOMPARE(source.size(), 1);
        QCOMPARE(source.first()[1].toInt(), 7);
        ApplicationThemeManager::instance().setMode(ThemeMode::Dark);
        QTest::qWait(100);
        screenshot(&panel, "events-dark.png");
        GlobalControlPanel palette;
        GlobalControlItem symbol;
        symbol.title = "a_very_long_enumerated_signal_name_in_parameterized_instance";
        symbol.typeBadge = "enum";
        GlobalControlItem templ;
        templ.title = "@ramp";
        templ.parameterSummary = "signal · range · step";
        palette.setItems({symbol, templ});
        palette.showAt(&panel, panel.mapToGlobal(QPoint(40, 40)));
        QTest::qWait(100);
        screenshot(&palette, "completion-dark.png");
        QCOMPARE(palette.resultList->sizeHintForRow(0), palette.resultList->sizeHintForRow(1));
        palette.hide();
        ApplicationThemeManager::instance().setMode(ThemeMode::Light);
        QSettings().remove("simulation/events");
    }
    void decoderAndCapacity()
    {
        simdock::SimulationEventDecoder decoder;
        decoder.reset("run");
        const QByteArray row = "ZS_EVT_V1|1.250|Info|746167|7462|74622e7376|7|68695c6e|76:787a:686578:6e73\n";
        QVERIFY(decoder.feed(row).isEmpty());
        QVERIFY(decoder.feed("# ZS_RUN_V1|run\n").isEmpty());
        QVERIFY(decoder.feed(row.left(12)).isEmpty());
        auto events = decoder.feed(row.mid(12));
        QCOMPARE(events.size(), 1);
        QCOMPARE(events[0].values[0].value("value").toString(), QString("xz"));
        simdock::SimulationEventModel model;
        model.setCapacity(2);
        model.startRun("run");
        model.setMergeRepeats(true);
        model.append({events[0], events[0], events[0]});
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.data(model.index(0, 6)).toULongLong(), qulonglong(2));
        QCOMPARE(model.overflow(), quint64(1));
        model.startRun("next");
        model.append(events);
        QCOMPARE(model.rowCount(), 0);
    }
    void eventMemoryLimitAndMalformedInput()
    {
        simdock::SimulationEventModel model;
        model.startRun("wide");
        simdock::SimulationEvent event;
        event.runId = "wide";
        event.level = "Info";
        event.message = QString(500000, 'a');
        for (int i = 0; i < 64; ++i)
            model.append({event});
        QVERIFY(model.overflow() > 0);
        QVERIFY(model.rowCount() < 64);
        simdock::SimulationEventDecoder decoder;
        decoder.reset("bad");
        decoder.feed("ZS_RUN_V1|bad\n");
        QVERIFY(decoder.feed("ZS_EVT_V1|1|Info|746167|7462|74622e7376|1|ff|\n").isEmpty());
        QCOMPARE(decoder.rejectedRecords(), quint64(1));
    }
    void simulationOnlyPaletteCatalog() {
      QTemporaryDir root;
      auto *templates = UserTemplateService::getInstance();
      const auto previousGlobal = templates->globalTemplateLocation();
      const auto previousWorkspace = templates->workspaceTemplateLocation();
      const auto restore = qScopeGuard([&] {
        templates->setGlobalTemplateFilePath(previousGlobal);
        templates->setWorkspaceTemplateFilePath(previousWorkspace);
      });
      templates->setGlobalTemplateFilePath(root.filePath("global.json"));
      templates->setWorkspaceTemplateFilePath(root.filePath("workspace.json"));
      const auto file = root.filePath("tb.sv");
      const auto package = root.filePath("user_package.sv");
      QVERIFY(put(package, "package user_package; task run(); endtask function "
                           "int value(); return 1; endfunction endpackage"));
      for (const auto &kind :
           {QString("snippet"), QString("task"), QString("function"),
            QString("print"), QString("local"), QString("legacy")}) {
        UserTemplateRecord record;
        record.id = kind;
        record.commandToken = record.label =
            kind == "legacy" ? ";;legacy" : "@user_" + kind;
        record.insertText = "$display(\"user\");";
        record.command = {
            {"kind", kind == "local" || kind == "legacy" ? "snippet" : kind}};
        if (kind == "task" || kind == "function") {
          record.insertText.clear();
          record.command.insert("symbol", kind == "task"
                                              ? "user_package::run"
                                              : "user_package::value");
          record.command.insert("sourceFile", package);
        }
        if (kind == "local")
          record.command.insert("file", file);
        QVERIFY(templates
                    ->saveRecord(record, kind == "task" || kind == "print" ||
                                             kind == "local")
                    .valid);
      }
      const auto globalBefore = bytes(templates->globalTemplateLocation());
      const auto workspaceBefore =
          bytes(templates->workspaceTemplateLocation());
      auto context = contextFor(
          file, "module tb; initial begin @user\nend endmodule", "@user");
      context.documentInstance = 23;
      context.documentTextRevision = 7;
      EditorInsertPaletteService palette;
      const auto ordinary =
          palette.query(GlobalControlCategory::Templates, "", context);
      context.simulationCommands = true;
      const auto prefixed =
          palette.query(GlobalControlCategory::Templates, "", context);
      QCOMPARE(ordinary.size(), prefixed.size());
      QStringList ids;
      for (int i = 0; i < ordinary.size(); ++i) {
        const auto &item = ordinary.at(i);
        ids << item.id;
        QVERIFY(item.id.startsWith('@'));
        QCOMPARE(item.operation,
                 GlobalControlItemOperation::InsertSimulationCommand);
        QCOMPARE(item.id, prefixed.at(i).id);
        QCOMPARE(item.parameters, prefixed.at(i).parameters);
        QCOMPARE(item.sourceDocumentTextRevision, context.documentTextRevision);
        QCOMPARE(item.sourceDocumentInstance, context.documentInstance);
      }
      for (const auto &kind : {"snippet", "task", "function", "print", "local"})
        QVERIFY(ids.contains("@user_" + QString(kind)));
      for (const auto &token : {"@reset", "@ramp", "@random", "@random_window", "@print", "@repeat"})
        QVERIFY(ids.contains(token));
      QVERIFY(!ids.contains("@definitions"));
      QVERIFY(!ids.contains("@process"));
      for (const auto &removed :
           {";;legacy", "instantiate", "import ", "include ", "new header"})
        QVERIFY(
            palette.query(GlobalControlCategory::Templates, removed, context)
                .isEmpty());
      context.fileName = root.filePath("other.sv");
      QVERIFY(
          palette
              .query(GlobalControlCategory::Templates, "@user_local", context)
              .isEmpty());
      QCOMPARE(bytes(templates->globalTemplateLocation()), globalBefore);
      QCOMPARE(bytes(templates->workspaceTemplateLocation()), workspaceBefore);
      evidence("simulation-catalog.json",
               QJsonDocument::fromVariant(
                   QVariantMap{{"ids", ids},
                               {"ordinaryAndPrefixMatch", true},
                               {"legacyStoragePreserved", true}})
                   .toJson());
    }
    void templateKeyboardInsertion_data() {
      QTest::addColumn<bool>("prefix");
      QTest::addColumn<bool>("listFocus");
      QTest::newRow("templates-search") << false << false;
      QTest::newRow("templates-list") << false << true;
      QTest::newRow("prefix-search") << true << false;
      QTest::newRow("prefix-list") << true << true;
    }
    void templateKeyboardInsertion() {
      QFETCH(bool, prefix);
      QFETCH(bool, listFocus);
      QTemporaryDir root;
      const auto file = root.filePath("tb.sv");
      const QString token = prefix ? "@key_" : "";
      const QString source = "module tb;\nlogic clk;\ninitial begin\n    " +
                             token + "\nend\nendmodule\n";
      const int start = source.indexOf("    ") + 4;
      QVERIFY(put(file, source));
      MainWindow window;
      window.resize(1100, 760);
      window.show();
      window.activateWindow();
      QVERIFY(window.workspaceSessionCoordinator->openWorkspace(root.path()));
      QVERIFY(window.tabManager->openFileInTab(file));
      auto *editor = window.tabManager->getCurrentEditor();
      QVERIFY(editor);
      auto *templates = UserTemplateService::getInstance();
      for (const auto &name : {QString("alpha"), QString("beta")}) {
        UserTemplateRecord record;
        record.id = "key_" + name;
        record.commandToken = record.label = "@key_" + name;
        record.insertText = "$display(\"" + name + "\");";
        record.command = {{"kind", "snippet"}, {"file", file}};
        QVERIFY(templates->saveRecord(record, true).valid);
      }
      QTextCursor cursor(editor->document());
      cursor.setPosition(start + token.size());
      editor->setTextCursor(cursor);
      editor->setFocus();
      cursor.insertText(" ");
      cursor.deletePreviousChar();
      editor->setTextCursor(cursor);
      QCOMPARE(editor->toPlainText(), source);
      const auto *shared = window.tabManager->sharedDocumentForEditor(editor);
      QVERIFY(shared && shared->textRevision() > 0);
      QSignalSpy messages(window.simulationCommands.get(),
                          &SimulationCommandService::message);
      auto *panel = window.globalControlCoordinator->panel.get();
      for (int repeat = 0; repeat < 2; ++repeat) {
        messages.clear();
        window.activateWindow();
        editor->setFocus();
        QTRY_VERIFY_WITH_TIMEOUT(editor->hasFocus(), 5000);
        QTest::keyClick(editor, Qt::Key_Space, Qt::ControlModifier);
        QTRY_VERIFY(panel->isVisible());
        auto *search = panel->findChild<QLineEdit *>("globalControlSearchEdit");
        auto *list = panel->findChild<QListWidget *>("globalControlResultList");
        QVERIFY(search && list);
        QTRY_VERIFY(search->hasFocus());
        const auto capturedContext =
            window.globalControlCoordinator->currentContext;
        evidence(QString("keyboard-%1-%2-open.json")
                     .arg(QTest::currentDataTag())
                     .arg(repeat),
                 QJsonDocument::fromVariant(
                     QVariantMap{{"simulationCommands",
                                  capturedContext.simulationCommands},
                                 {"cursor", capturedContext.cursorPosition},
                                 {"initialQuery", capturedContext.initialQuery},
                                 {"source", capturedContext.documentText},
                                 {"category", int(panel->category())},
                                 {"sourceTextRevision",
                                  capturedContext.documentTextRevision}})
                     .toJson());
        if (!prefix) {
          auto *categories =
              panel->findChild<QTabBar *>("globalControlCategoryTabs");
          QVERIFY(categories && categories->isVisible());
          QTest::mouseClick(categories, Qt::LeftButton, Qt::NoModifier,
                            categories->tabRect(1).center());
          QCOMPARE(panel->category(), GlobalControlCategory::Templates);
          search->setFocus();
          QTest::keyClicks(search, "key_");
        }
        QCOMPARE(list->count(), 2);
        QCOMPARE(list->currentItem()->text(), QString("@key_alpha"));
        if (listFocus) {
          QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                            list->visualItemRect(list->currentItem()).center());
          QTRY_VERIFY(list->hasFocus());
        }
        QWidget *focus = listFocus ? static_cast<QWidget *>(list)
                                   : static_cast<QWidget *>(search);
        QTest::keyClick(focus, Qt::Key_Down);
        QCOMPARE(list->currentItem()->text(), QString("@key_beta"));
        QTest::keyClick(focus, Qt::Key_Up);
        QCOMPARE(list->currentItem()->text(), QString("@key_alpha"));
        QTest::keyClick(focus, Qt::Key_Down);
        const auto selected = panel->currentItems.at(list->currentRow());
        const auto expectedRevision = shared->textRevision();
        screenshot(panel, QString("keyboard-%1-%2-before.png")
                              .arg(QTest::currentDataTag())
                              .arg(repeat));
        QTest::keyClick(focus, listFocus ? Qt::Key_Enter : Qt::Key_Return);
        QTRY_VERIFY(!panel->isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(!messages.isEmpty(), 15000);
        const QString expected =
            QString(source).replace(start, token.size(), "$display(\"beta\");");
        evidence(QString("keyboard-%1-%2.json")
                     .arg(QTest::currentDataTag())
                     .arg(repeat),
                 QJsonDocument::fromVariant(
                     QVariantMap{{"sourceTextRevision", expectedRevision},
                                 {"candidateTextRevision",
                                  selected.sourceDocumentTextRevision},
                                 {"focus", listFocus ? "list" : "search"},
                                 {"prefix", prefix},
                                 {"selected", selected.id},
                                 {"message", messages.last().first()},
                                 {"actual", editor->toPlainText()},
                                 {"expected", expected}})
                     .toJson());
        QVERIFY2(editor->toPlainText() == expected,
                 qPrintable(messages.last().first().toString()));
        QTRY_VERIFY(editor->hasFocus());
        QTest::keyClick(editor, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(editor->toPlainText(), source);
        QTest::keyClick(editor, Qt::Key_Y, Qt::ControlModifier);
        QCOMPARE(editor->toPlainText(), expected);
        QTest::keyClick(editor, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(editor->toPlainText(), source);
        cursor.setPosition(start + token.size());
        editor->setTextCursor(cursor);
      }
      editor->document()->setModified(false);
    }
    void builtinTemplateKeyboardInput_data() {
      QTest::addColumn<bool>("prefix");
      QTest::newRow("templates") << false;
      QTest::newRow("prefix") << true;
    }
    void builtinTemplateKeyboardInput() {
      QFETCH(bool, prefix);
      QTemporaryDir root;
      const auto file = root.filePath("tb.sv");
      const QString token = prefix ? "@print" : "";
      const QString source =
          "module tb; timeunit 1ns; timeprecision 1ps;\ninitial begin\n    " +
          token + "\nend\nendmodule\n";
      const int position = source.indexOf("    ") + 4 + token.size();
      QVERIFY(put(file, source));
      auto project = simdock::newProject("Keyboard print");
      project.sources = {"tb.sv"};
      project.tbFile = "tb.sv";
      project.tbName = "tb";
      QString projectError;
      QVERIFY2(simdock::saveProject(root.path(), project, &projectError),
               qPrintable(projectError));
      MainWindow window;
      window.show();
      QVERIFY(window.workspaceSessionCoordinator->openWorkspace(root.path()));
      QVERIFY2(window.openSimDockTarget(root.path(), project.id, &projectError),
               qPrintable(projectError));
      QVERIFY(window.tabManager->openFileInTab(file));
      auto *editor = window.tabManager->getCurrentEditor();
      QTextCursor cursor(editor->document());
      cursor.setPosition(position);
      editor->setTextCursor(cursor);
      window.activateWindow();
      editor->setFocus();
      QTRY_VERIFY(editor->hasFocus());
      QSignalSpy messages(window.simulationCommands.get(),
                          &SimulationCommandService::message);
      QTest::keyClick(editor, Qt::Key_Space, Qt::ControlModifier);
      auto *panel = window.globalControlCoordinator->panel.get();
      QTRY_VERIFY(panel->isVisible());
      auto *search = panel->findChild<QLineEdit *>("globalControlSearchEdit");
      auto *list = panel->findChild<QListWidget *>("globalControlResultList");
      QTRY_VERIFY(search->hasFocus());
      if (!prefix) {
        QTest::keyClick(search, Qt::Key_Tab);
        QCOMPARE(panel->category(), GlobalControlCategory::Templates);
        QTest::keyClicks(search, "print");
      }
      QVERIFY(list->currentItem());
      QCOMPARE(list->currentItem()->text(), QString("@print"));
      QTest::keyClick(search, Qt::Key_Return);
      QTRY_VERIFY_WITH_TIMEOUT(!messages.isEmpty(), 15000);
      QVERIFY2(editor->toPlainText().contains(
                   "zeroslack_events_package::emit_event"),
               qPrintable(messages.last().first().toString()));
      QVERIFY(editor->templateSlotModeActive());
      QVERIFY(editor->templateSlotModeSlotCount() >= 1);
      QCOMPARE(editor->textCursor().selectedText(), QString("\"message\""));
      auto *buildInputs = window.findChild<simdock::Workbench *>();
      QVERIFY(buildInputs);
      QCOMPARE(buildInputs->activeProjectId(), project.id);
      const auto inputs = buildInputs->activeCompilationInputs();
      QCOMPARE(inputs.size(), 2);
      QVERIFY(inputs.first().endsWith("zeroslack_events_package.sv"));
      QCOMPARE(QFileInfo(inputs.last()).canonicalFilePath(),
               QFileInfo(file).canonicalFilePath());
      const auto inserted = editor->toPlainText();
      screenshot(&window,
                 QString("keyboard-print-%1.png").arg(QTest::currentDataTag()));
      evidence(QString("keyboard-print-%1.json").arg(QTest::currentDataTag()),
               QJsonDocument::fromVariant(
                   QVariantMap{{"text", inserted},
                               {"slots", editor->templateSlotModeSlotCount()},
                               {"message", messages.last().first()}})
                   .toJson());
      QTest::keyClick(editor, Qt::Key_Tab);
      QTest::keyClick(editor, Qt::Key_Tab, Qt::ShiftModifier);
      QTest::keyClick(editor, Qt::Key_Escape);
      QTest::keyClick(editor, Qt::Key_Z, Qt::ControlModifier);
      QCOMPARE(editor->toPlainText(), source);
      QTest::keyClick(editor, Qt::Key_Y, Qt::ControlModifier);
      QCOMPARE(editor->toPlainText(), inserted);
      editor->document()->setModified(false);
    }
    void templateKeyboardGuards_data() {
      QTest::addColumn<bool>("prefix");
      QTest::addColumn<QString>("change");
      for (bool prefix : {false, true})
        for (const auto *change :
             {"document", "cursor", "catalog", "readonly", "readonly-before"})
          QTest::newRow(qPrintable(
              QString("%1-%2").arg(prefix ? "prefix" : "templates", change)))
              << prefix << QString(change);
    }
    void templateKeyboardGuards() {
      QFETCH(bool, prefix);
      QFETCH(QString, change);
      QTemporaryDir root;
      const auto file = root.filePath("tb.sv");
      const QString token = prefix ? "@guard" : "";
      const QString source =
          "module tb;\ninitial begin\n    " + token + "\nend\nendmodule\n";
      QVERIFY(put(file, source));
      MainWindow window;
      window.show();
      QVERIFY(window.workspaceSessionCoordinator->openWorkspace(root.path()));
      QVERIFY(window.tabManager->openFileInTab(file));
      auto *editor = window.tabManager->getCurrentEditor();
      UserTemplateRecord record;
      record.id = "keyboard-guard";
      record.commandToken = record.label = "@guard";
      record.insertText = "$display(\"guard\");";
      record.command = {{"kind", "snippet"}, {"file", file}};
      auto *templates = UserTemplateService::getInstance();
      QVERIFY(templates->saveRecord(record, true).valid);
      QTextCursor cursor(editor->document());
      cursor.setPosition(source.indexOf("    ") + 4 + token.size());
      editor->setTextCursor(cursor);
      if (change == "readonly-before")
        editor->setReadOnly(true);
      window.activateWindow();
      editor->setFocus();
      QTRY_VERIFY(editor->hasFocus());
      QSignalSpy messages(window.simulationCommands.get(),
                          &SimulationCommandService::message);
      QTest::keyClick(editor, Qt::Key_Space, Qt::ControlModifier);
      auto *panel = window.globalControlCoordinator->panel.get();
      QTRY_VERIFY(panel->isVisible());
      auto *search = panel->findChild<QLineEdit *>("globalControlSearchEdit");
      QTRY_VERIFY(search->hasFocus());
      if (!prefix || change == "readonly-before") {
        QTest::keyClick(search, Qt::Key_Tab);
        QCOMPARE(panel->category(), GlobalControlCategory::Templates);
        QTest::keyClick(search, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClicks(search, "guard");
      }
      if (change == "document")
        editor->insertPlainText(" ");
      if (change == "cursor") {
        cursor.movePosition(QTextCursor::NextCharacter);
        editor->setTextCursor(cursor);
      }
      if (change == "catalog")
        templates->reload();
      if (change == "readonly")
        editor->setReadOnly(true);
      const auto before = editor->toPlainText();
      const auto undo = editor->document()->availableUndoSteps();
      QTest::keyClick(search, Qt::Key_Return);
      QString reason;
      if (change == "readonly-before") {
        auto *notice = panel->findChild<QLabel *>("globalControlNotice");
        QVERIFY(notice && notice->isVisible());
        reason = notice->text();
        QVERIFY(reason.contains("read-only"));
        QVERIFY(panel->isVisible());
        screenshot(panel,
                   QString("keyboard-%1.png").arg(QTest::currentDataTag()));
        QTest::keyClick(search, Qt::Key_Escape);
      } else {
        QTRY_VERIFY_WITH_TIMEOUT(!messages.isEmpty(), 15000);
        reason = messages.last().first().toString();
        QVERIFY(
            reason.contains(change == "readonly" ? "read-only" : "changed"));
        QVERIFY(!panel->isVisible());
      }
      QCOMPARE(editor->toPlainText(), before);
      QCOMPARE(editor->document()->availableUndoSteps(), undo);
      evidence(
          QString("keyboard-guard-%1.json").arg(QTest::currentDataTag()),
          QJsonDocument::fromVariant(
              QVariantMap{{"reason", reason},
                          {"textPreserved", editor->toPlainText() == before},
                          {"undoSteps", undo}})
              .toJson());
      editor->setReadOnly(false);
      editor->document()->setModified(false);
    }
    void mainWindowAndLocalExpansion() {
      QTemporaryDir root;
      const auto file = root.filePath("tb.sv");
      const QString source =
          "module tb; logic clk1; initial begin @q1\n; end endmodule\n";
      QVERIFY(put(file, source));
      MainWindow window;
      window.resize(1100, 760);
      window.show();
      QVERIFY(window.workspaceSessionCoordinator->openWorkspace(root.path()));
      QVERIFY(window.tabManager->openFileInTab(file));
      auto *editor = window.tabManager->getCurrentEditor();
      QVERIFY(editor);
      auto *service = UserTemplateService::getInstance();
      UserTemplateRecord q;
      q.id = "q1";
      q.commandToken = q.label = "@q1";
      q.insertText = "@(posedge clk1)";
      q.command = {{"kind", "snippet"}, {"file", file}};
      QVERIFY(service->saveRecord(q, true).valid);
      QTextCursor cursor(editor->document());
      cursor.setPosition(source.indexOf("@q1") + 3);
      editor->setTextCursor(cursor);
      window.activateWindow();
      editor->setFocus();
      QTRY_VERIFY(editor->hasFocus());
      QTest::keyClick(editor, Qt::Key_Space, Qt::ControlModifier);
      QTRY_VERIFY(window.globalControlCoordinator->panel->isVisible());
      QTest::qWait(100);
      screenshot(&window, "mainwindow-prefix.png");
      screenshot(window.globalControlCoordinator->panel.get(),
                 "completion-prefix.png");
      auto *search =
          window.globalControlCoordinator->panel->findChild<QLineEdit *>(
              "globalControlSearchEdit");
      QVERIFY(search && search->hasFocus());
      QTest::keyClick(search, Qt::Key_Return);
      QTRY_VERIFY_WITH_TIMEOUT(
          editor->toPlainText().contains("@(posedge clk1)"), 15000);
      QCOMPARE(editor->toPlainText(),
               QString(source).replace("@q1", "@(posedge clk1)"));
      QTest::keyClick(editor, Qt::Key_Z, Qt::ControlModifier);
      QCOMPARE(editor->toPlainText(), source);
      TemplateManagerDialog dialog(service,
                                   window.simulationCommands->currentRequest(),
                                   root.path(), &window);
      dialog.show();
      QTest::qWait(100);
      screenshot(&dialog, "template-manager.png");
      QVERIFY(dialog.findChild<QLineEdit *>("templateName"));
      dialog.name->setText("q2");
      dialog.body->setPlainText("@(negedge clk1)");
      auto *actions = dialog.findChild<QDialogButtonBox *>();
      QVERIFY(actions);
      QAbstractButton *saveButton = nullptr;
      for (auto *button : actions->buttons())
        if (actions->buttonRole(button) == QDialogButtonBox::AcceptRole)
          saveButton = button;
      QVERIFY(saveButton);
      QTest::mouseClick(saveButton, Qt::LeftButton);
      QVERIFY2(dialog.notice->text() == "Template saved.",
               qPrintable(dialog.notice->text()));
      dialog.reject();
      auto *eventsButton = window.findChild<QAbstractButton *>(
          "bottomPanelButton_simulationEvents");
      QVERIFY(eventsButton);
      QVERIFY(eventsButton->isVisible());
      QTest::mouseClick(eventsButton, Qt::LeftButton);
      auto *eventsPanel = window.findChild<simdock::SimulationEventsPanel *>();
      QVERIFY(eventsPanel);
      QTRY_VERIFY(eventsPanel->isVisible());
      screenshot(&window, "mainwindow-events.png");
      editor->document()->setModified(false);
      window.close();
    }
    void packageCreationAndBinding()
    {
        QTemporaryDir root;
        const auto dependency = root.filePath("dep.sv"), existing = root.filePath("mixed.sv"),
                   tb = root.filePath("tb.sv");
        QVERIFY(put(dependency, "package dep; parameter int N=7; endpackage\n"));
        QVERIFY(put(existing, "package first; parameter int N=1; endpackage\npackage actual; endpackage\n"));
        QVERIFY(put(tb, "module tb; initial begin end endmodule\n"));
        UserTemplateService service(root.filePath("global.json"), root.filePath("workspace.json"));
        SimulationCommandRequest request;
        request.context = contextFor(tb, QString::fromUtf8(bytes(tb)), "begin");
        request.orderedFiles = {dependency, existing, tb};
        request.contents = {{tb, QString::fromUtf8(bytes(tb))}};
        TemplateManagerDialog manager(&service, request, root.path());
        manager.show();
        bool newPackage = false, handled = false, binding = false;
        int dialogs = 0;
        QTimer timer;
        timer.setInterval(20);
        connect(&timer, &QTimer::timeout, this,
                [&]
                {
                    auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                    if (!dialog)
                        return;
                    if (auto *picker = qobject_cast<QFileDialog *>(dialog))
                    {
                        picker->selectFile(root.filePath("extra_package.sv"));
                        QMetaObject::invokeMethod(picker, "accept");
                        return;
                    }
                    if (dialog->objectName() == "createTemplateImplementation")
                    {
                        auto *combo = dialog->findChild<QComboBox *>("implementationPackage");
                        QVERIFY(combo);
                        combo->setCurrentIndex(newPackage ? 0
                                                          : combo->findText("actual", Qt::MatchStartsWith));
                        QVERIFY(combo->currentIndex() >= 0);
                        auto *name = dialog->findChild<QLineEdit *>("implementationPackageName");
                        name->setText("extra_package");
                        dialog->findChild<QPlainTextEdit *>("implementationCode")
                            ->setPlainText(newPackage ? "task automatic ping(); $display(\"ping\"); endtask"
                                                      : "function automatic int answer(input int value); "
                                                        "return dep::N+value; endfunction");
                        handled = true;
                        ++dialogs;
                        dialog->accept();
                        return;
                    }
                    if (binding && dialog->windowTitle() == "Bind declaration")
                    {
                        auto *combo = dialog->findChild<QComboBox *>();
                        QVERIFY(combo);
                        const auto index = combo->findText("actual::answer", Qt::MatchStartsWith);
                        QVERIFY(index >= 0);
                        combo->setCurrentIndex(index);
                        dialog->accept();
                    }
                });
        timer.start();
        manager.createImplementation();
        QTRY_VERIFY_WITH_TIMEOUT(bytes(existing).contains("function automatic int answer"), 10000);
        QVERIFY(handled);
        QVERIFY(bytes(existing).startsWith("package first; parameter int N=1; endpackage\n"));
        binding = true;
        manager.bindCallable();
        QTRY_COMPARE_WITH_TIMEOUT(manager.symbol->text(), QString("actual::answer"), 10000);
        binding = false;
        QTimer::singleShot(0, &manager,
                           [&]
                           {
                               auto *constraints = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                               QVERIFY(constraints && constraints->windowTitle() == "Input constraints");
                               screenshot(constraints, "input-constraints.png");
                               constraints->accept();
                           });
        manager.configureInput(0);
        QCOMPARE(manager.kind->currentText(), QString("function"));
        QCOMPARE(manager.selected.command.value("sourceFile").toString(), existing);
        newPackage = true;
        manager.createImplementation();
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(root.filePath("extra_package.sv")), 10000);
        QVERIFY(bytes(root.filePath("extra_package.sv")).contains("package extra_package;"));
        QCOMPARE(dialogs, 2);
        timer.stop();
        manager.selected.label = "User display title";
        manager.name->setText("answer");
        manager.aliases->setText("my answer, 用户别名");
        manager.save();
        QCOMPARE(manager.notice->text(), QString("Template saved."));
        const auto saved = service.recordsForFile(tb, true);
        bool found = false;
        for (const auto &record : saved)
            if (record.commandToken == "@answer")
            {
                found = true;
                QCOMPARE(record.label, QString("User display title"));
                QVERIFY(record.command.value("aliases").toStringList().contains("用户别名"));
                QCOMPARE(record.command.value("sourceFile").toString(), existing);
            }
        QVERIFY(found);
        manager.reject();
    }
    void embedded2142OrdinaryAssets()
    {
        if (qEnvironmentVariableIsEmpty("XIPS_BROWSER_LIBRARY") ||
            qEnvironmentVariableIsEmpty("XIPS_TEST_ASSET_ID"))
            QSKIP("Supply the frozen 2.14.2 ordinary asset fixture.");
        QTemporaryDir root;
        MainWindow window;
        window.resize(1280, 860);
        window.show();
        QVERIFY(window.workspaceSessionCoordinator->openWorkspace(root.path()));
        window.simulationCommands->showBrowser();
        auto *host = window.simulationCommands->view();
        QVERIFY(host);
        QVERIFY2(host->isReady(), qPrintable(host->property("nativeComponentError").toString()));
        auto *browser = host->component();
        QVERIFY(browser);
        QVERIFY(browser->metaObject()->indexOfMethod("hdlCommandCatalog()") < 0);
        QVERIFY(browser->metaObject()->indexOfMethod("useHdlCommand(QVariantMap)") < 0);
        QTRY_VERIFY_WITH_TIMEOUT(host->canClose(nullptr), 15000);
        QVERIFY(QMetaObject::invokeMethod(browser, "revealAsset",
                                          Q_ARG(QString, qEnvironmentVariable("XIPS_TEST_ASSET_ID"))));
        auto *versions = browser->findChild<QTableView *>("revisionTable");
        auto *pages = browser->findChild<QTabWidget *>("assetPages");
        QVERIFY(versions && pages);
        pages->setCurrentIndex(1);
        QTRY_VERIFY_WITH_TIMEOUT(versions->model()->rowCount() > 0, 15000);
        QModelIndex selected;
        for (int row = 0; row < versions->model()->rowCount(); ++row)
        {
            auto index = versions->model()->index(row, 0);
            if (index.data(Qt::UserRole).toString() == qEnvironmentVariable("XIPS_TEST_REVISION"))
                selected = index;
        }
        QVERIFY(selected.isValid());
        versions->setCurrentIndex(selected);
        QTRY_VERIFY_WITH_TIMEOUT(host->canClose(nullptr), 15000);
        auto *copy = browser->findChild<QAbstractButton *>("takeButton");
        QVERIFY(copy);
        QTRY_VERIFY(copy->isVisible() && copy->isEnabled());
        const auto destination = root.filePath("pinned");
        bool dialogSeen = false;
        QTimer form;
        form.setInterval(20);
        connect(&form, &QTimer::timeout, this,
                [&]
                {
                    auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                    if (!dialog)
                        return;
                    auto *path = dialog->findChild<QLineEdit *>("exportDestination");
                    if (!path)
                        return;
                    path->setText(destination);
                    auto *accept = dialog->findChild<QAbstractButton *>("formAccept");
                    if (accept && accept->isEnabled())
                    {
                        dialogSeen = true;
                        QTest::mouseClick(accept, Qt::LeftButton);
                    }
                });
        form.start();
        QTest::mouseClick(copy, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(root.filePath(".zeroslack/xips-references.json")), 15000);
        form.stop();
        QVERIFY(dialogSeen);
        QTRY_VERIFY_WITH_TIMEOUT(host->canClose(nullptr), 15000);
        const auto receipt = QJsonDocument::fromJson(bytes(root.filePath(".zeroslack/xips-references.json")))
                                 .object()
                                 .value("assets")
                                 .toArray()
                                 .first()
                                 .toObject();
        QCOMPARE(receipt.value("revision").toString(), qEnvironmentVariable("XIPS_TEST_REVISION"));
        QVERIFY(!receipt.value("baseFiles").toObject().isEmpty());
        QVERIFY(!receipt.value("library").toString().isEmpty());
        window.simulationCommands->prepare();
        bool migrated = false;
        for (const auto &record :
             UserTemplateService::getInstance()->recordsForFile(root.filePath("tb.sv"), true))
            if (record.command.value("sourceFile").toString() ==
                QDir(destination).filePath("user_package.sv"))
                migrated = true;
        QVERIFY(migrated);
        screenshot(&window, "mainwindow-xips-2142.png");
        evidence("embedded-2142-receipt.json", QJsonDocument(receipt).toJson());
        window.close();
    }
    void builtinDriverPlans()
    {
        QTemporaryDir root;
        QStringList files;
        const auto error = SimulationCommandService::materializeSupport(root.path(), "driver", &files);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const auto file = root.filePath("tb.sv");
        for (const auto &builtin : UserTemplateService::simulationBuiltins())
        {
            if (builtin.command.value("kind") != "driver")
                continue;
            const QString source =
                "module tb; timeunit 1ns; timeprecision 1ps; logic clk=0; logic rst=0; logic [7:0] data=0; "
                "always #1ns clk=~clk; initial begin @cmd\n end endmodule\n";
            auto config = builtin.command;
            config.insert(
                "inputs",
                QVariantMap{
                    {"signal", QVariantMap{{"kind", "Signal"},
                                           {"access", "write"},
                                           {"default", builtin.commandToken == "@reset" ? "rst" : "data"}}},
                    {"clock", QVariantMap{{"kind", "Signal"}, {"role", "Clock"}, {"default", "clk"}}}});
            auto r = command(file, source, "@cmd", config, files);
            const auto result = SimulationCommandPlanner::prepare(r);
            QVERIFY2(result.error.isEmpty(), qPrintable(builtin.commandToken + ": " + result.error));
            QVERIFY(result.text.contains("class simulation_driver_"));
            QVERIFY(!result.placeholders.isEmpty());
            auto generated = apply(source, result);
            const auto beforeEnd = generated.lastIndexOf("end\n end endmodule");
            QVERIFY(beforeEnd >= 0);
            generated.insert(beforeEnd,
                             "if (status != tb_signal_package::TB_OK) $fatal(1, \"driver failed\");\n");
            QVERIFY(put(file, generated));
            evidence("drivers/" + builtin.commandToken.mid(1) + ".sv", generated.toUtf8());
            auto project = simdock::newProject(builtin.commandToken);
            for (const auto &path : files)
                project.sources << QDir(root.path()).relativeFilePath(path);
            project.sources << "tb.sv";
            project.tbFile = "tb.sv";
            project.tbName = "tb";
            project.durationNs = 200;
            simdock::QuestaSession session;
            QSignalSpy finished(&session, &simdock::QuestaSession::finished);
            QString log, runError;
            connect(&session, &simdock::QuestaSession::logText, this, [&](const auto &text) { log += text; });
            const auto executable = qEnvironmentVariable("SIMDOCK_TEST_QUESTA");
            QVERIFY(!executable.isEmpty());
            QVERIFY2(session.start(executable, root.path(), project, &runError), qPrintable(runError));
            QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 90000);
            evidence("drivers/" + builtin.commandToken.mid(1) + ".log", log.toUtf8());
            QVERIFY2(finished.first()[0].toBool(), qPrintable(log));
            session.stop();
            QTRY_VERIFY_WITH_TIMEOUT(!session.alive(), 10000);
        }
    }
    void actualQuestaEventsAndCalls()
    {
        const auto executable = qEnvironmentVariable("SIMDOCK_TEST_QUESTA");
        QVERIFY2(!executable.isEmpty(), "Real Questa is required for this test.");
        QTemporaryDir root;
        QStringList support;
        auto error = SimulationCommandService::materializeSupport(root.path(), "print", &support);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const auto package = root.filePath("user_package.sv");
        QVERIFY(put(package,
                    "package user_package; task automatic delay_value(input int n); #1ns; endtask function "
                    "automatic int plus_one(input int x); return x+1; endfunction endpackage\n"));
        const auto file = root.filePath("tb.sv");
        QString source = "module tb; timeunit 1ns; timeprecision 1ps; logic [3:0] data=4'b10xz; int answer; "
                         "initial begin\n@task\nanswer=@function;\n#print\nfor(int i=0;i<25020;i++) begin "
                         "#1ps; #repeat_print end\n#2ns; end endmodule\n";
        auto r = command(file, source, "@task",
                         {{"kind", "task"}, {"symbol", "user_package::delay_value"}, {"sourceFile", package}},
                         support + QStringList{package});
        auto task = SimulationCommandPlanner::prepare(r);
        QVERIFY2(task.error.isEmpty(), qPrintable(task.error));
        source = apply(source, task);
        r = command(file, source, "@function",
                    {{"kind", "function"}, {"symbol", "user_package::plus_one"}, {"sourceFile", package}},
                    support + QStringList{package});
        auto function = SimulationCommandPlanner::prepare(r);
        QVERIFY2(function.error.isEmpty(), qPrintable(function.error));
        source = apply(source, function);
        QVariantMap preset{
            {"level", "Error"},
            {"tag", "tag|comma,"},
            {"message", "line one\nline two \"quote\" \\ slash"},
            {"condition", "answer==1"},
            {"fields",
             QVariantList{QVariantMap{
                 {"name", "data"}, {"expression", "data"}, {"radix", "binary"}, {"unit", "bits"}}}}};
        r = command(file, source, "#print", {{"kind", "print"}, {"print", preset}},
                    support + QStringList{package});
        auto event = SimulationCommandPlanner::prepare(r);
        QVERIFY2(event.error.isEmpty(), qPrintable(event.error));
        source = apply(source, event);
        preset.insert("level", "Info");
        preset.insert("message", "repeat");
        preset.remove("condition");
        r = command(file, source, "#repeat_print", {{"kind", "print"}, {"print", preset}},
                    support + QStringList{package});
        event = SimulationCommandPlanner::prepare(r);
        QVERIFY2(event.error.isEmpty(), qPrintable(event.error));
        source = apply(source, event);
        QVERIFY(put(file, source));
        evidence("questa/generated-tb.sv", source.toUtf8());
        auto project = simdock::newProject("Structured events");
        for (const auto &path : support)
            project.sources << QDir(root.path()).relativeFilePath(path);
        project.sources << "user_package.sv" << "tb.sv";
        project.tbFile = "tb.sv";
        project.tbName = "tb";
        project.durationNs = 100;
        simdock::QuestaSession session;
        QSignalSpy finished(&session, &simdock::QuestaSession::finished);
        QString log;
        QList<simdock::SimulationEvent> events;
        simdock::SimulationEventsPanel panel;
        panel.resize(1150, 480);
        panel.show();
        connect(&session, &simdock::QuestaSession::logText, this,
                [&](const QString &value) { log += value; });
        connect(&session, &simdock::QuestaSession::eventRunStarted, &panel,
                &simdock::SimulationEventsPanel::startRun);
        connect(&session, &simdock::QuestaSession::eventsReady, this,
                [&](const auto &batch)
                {
                    events += batch;
                    panel.append(batch);
                });
        connect(&session, &simdock::QuestaSession::eventRunCleared, &panel, [&] { panel.startRun({}); });
        QVERIFY2(session.start(executable, root.path(), project, &error), qPrintable(error));
        QTRY_VERIFY2_WITH_TIMEOUT(!finished.isEmpty(), qPrintable(log), 90000);
        evidence("questa/host.log", log.toUtf8());
        QVERIFY2(finished.first()[0].toBool(), qPrintable(log));
        QCOMPARE(events.size(), 25021);
        QCOMPARE(events.first().level, QString("Error"));
        QCOMPARE(events.first().timePs, QString("1000.000"));
        QCOMPARE(events.first().instance, QString("tb"));
        QCOMPARE(events.first().values[0].value("value").toString(), QString("10xz"));
        QCOMPARE(events.first().message, QString("line one\nline two \"quote\" \\ slash"));
        QVERIFY(events.first().sourceFile.endsWith("tb.sv"));
        QVERIFY(events.first().sourceLine > 0);
        QCOMPARE(panel.eventModel()->rowCount(), 20000);
        QCOMPARE(panel.eventModel()->overflow(), quint64(5021));
        screenshot(&panel, "simulation-events.png");
        const auto oldRun = events.first().runId;
        finished.clear();
        events.clear();
        QVERIFY2(session.start(executable, root.path(), project, &error), qPrintable(error));
        QCOMPARE(panel.eventModel()->rowCount(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 90000);
        QVERIFY(!events.isEmpty());
        QVERIFY(events.first().runId != oldRun);
        session.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!session.alive(), 10000);
        QCOMPARE(panel.eventModel()->rowCount(), 0);
    }
};
int main(int argc, char **argv)
{
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QStandardPaths::setTestModeEnabled(true);
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    QCoreApplication::setOrganizationName("ZeroSlackCommandRedesignTest");
    QCoreApplication::setApplicationName("ZeroSlackCommandRedesignTest");
    QTemporaryDir profile;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, profile.path());
    qputenv("ZEROSLACK_SIMDOCK_LEGACY_SETTINGS_PATH", profile.filePath("legacy.ini").toUtf8());
    UserTemplateService::getInstance()->setGlobalTemplateFilePath(profile.filePath("templates.json"));
    if (!initializeUiStyleForTest())
        return 2;
    ApplicationThemeManager::instance().setAnimationsEnabled(false);
    ApplicationThemeManager::instance().applyToApplication();
    SimulationCommandRedesignTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "simulation_command_redesign_test.moc"
