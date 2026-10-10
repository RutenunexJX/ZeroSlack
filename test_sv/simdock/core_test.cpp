#include "../../src/simulation/simdock/core/analyzer.h"
#include "../../src/simulation/simdock/core/dependencies.h"
#include "../../src/simulation/simdock/core/questasession.h"
#include "../../src/simulation/simdock/core/testbench.h"
#include "../../src/simulation/simdock/core/stimulus.h"
#include "../../src/simulation/simdock/core/workspace.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
using namespace simdock;

static bool put(const QString& path, const QByteArray& bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
static QByteArray read(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
static QStringList sessionDirectories()
{
    return QDir(QDir::tempPath()).entryList({QStringLiteral("simdock-session-*")}, QDir::Dirs | QDir::NoDotAndDotDot);
}
static QString newSessionDirectory(const QStringList& previous)
{
    auto directories = sessionDirectories();
    for (const auto& entry : previous) directories.removeAll(entry);
    return directories.size() == 1 ? QDir(QDir::tempPath()).filePath(directories.first()) : QString();
}
static bool captureSession(const QString& phase, const QString& directory, const QuestaSession& session, const QString& log)
{
    const auto output = qEnvironmentVariable("SIMDOCK_TEST_ARTIFACT_DIR");
    if (output.isEmpty()) return true;
    const QDir destination(QDir(output).filePath(phase));
    if (!put(destination.filePath("host.log"), log.toUtf8())) return false;
    for (const auto& name : {"bootstrap.do", "state", "state.tmp", "transcript.log", "active.do", "command.do"}) {
        const auto path = QDir(directory).filePath(QLatin1String(name));
        if (QFileInfo::exists(path) && !put(destination.filePath(QLatin1String(name)), read(path))) return false;
    }
    for (const auto& name : {"run.do", "modelsim.ini", "transcript.log"}) {
        const auto path = QDir(session.lastRunDirectory()).filePath(QLatin1String(name));
        if (QFileInfo::exists(path) && !put(destination.filePath("run/" + QLatin1String(name)), read(path))) return false;
    }
    return put(destination.filePath("identity.json"), QJsonDocument(QJsonObject{
        {"sessionDirectory", directory}, {"runDirectory", session.lastRunDirectory()},
        {"processId", session.processId()}, {"alive", session.alive()}, {"busy", session.busy()}}).toJson());
}
#ifdef Q_OS_WIN
static HANDLE openState(const QString& path, DWORD access, DWORD sharing)
{
    const auto native = QDir::toNativeSeparators(path);
    return CreateFileW(reinterpret_cast<const wchar_t*>(native.utf16()), access, sharing,
                       nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
}
#endif
static QByteArray counter()
{
    return QByteArrayLiteral("module counter #(parameter int WIDTH=8)(input logic clk, rst_n, enable, output logic [WIDTH-1:0] count);\n"
        "always_ff @(posedge clk or negedge rst_n) if (!rst_n) count <= '0; else if(enable) count <= count+1'b1;\nendmodule\n");
}

static QByteArray typedPackage()
{
    return QByteArrayLiteral("package demo_types;\n"
        "typedef enum logic [1:0] { IDLE, ACTIVE, DONE } mode_e;\n"
        "typedef struct packed { logic valid; logic [6:0] value; } payload_t;\n"
        "typedef logic [7:0] word_t;\nendpackage\n");
}

static QByteArray typedModule()
{
    return QByteArrayLiteral("module typed_dut import demo_types::*; #(parameter int WIDTH=8)\n"
        "(input logic clk, rst, input mode_e mode, mode_copy, input demo_types::payload_t payload,\n"
        "input word_t data, output mode_e result); assign result = mode; endmodule\n");
}

class CoreTest : public QObject {
    Q_OBJECT
private slots:
    void validationUsesCompilerIncludeOrder()
    {
        QTemporaryDir workspace;
        QVERIFY(put(workspace.filePath("tb/top.sv"), "module tb; endmodule"));
        QVERIFY(put(workspace.filePath("first/helper.sv"), "module helper; endmodule"));
        QVERIFY(put(workspace.filePath("rtl/dut.sv"), "`include \"defs.svh\"\nmodule dut; endmodule"));
        QVERIFY(put(workspace.filePath("first/defs.svh"), "`define N 1\n"));
        QVERIFY(put(workspace.filePath("tb/defs.svh"), "`define N 2\n"));
        auto p = newProject("Include order");
        p.sources = {"tb/top.sv", "first/helper.sv", "rtl/dut.sv"};
        p.tbFile = "tb/top.sv"; p.tbName = "tb";
        QMap<QString, QByteArray> fingerprints;
        QVERIFY(validateInputs(workspace.path(), p, &fingerprints).isEmpty());
        QVERIFY(fingerprints.contains(workspace.filePath("first/defs.svh")));
        QVERIFY(!fingerprints.contains(workspace.filePath("tb/defs.svh")));
    }
    void dependencyIndexOwnsSnapshot()
    {
        Scan scan;
        scan.files << analyzeSource("module top; child c(); endmodule", "top.sv")
                   << analyzeSource("module child; endmodule", "first.sv")
                   << analyzeSource("module child; endmodule", "second.sv");
        const DependencyIndex index(scan);
        scan.files.clear();
        const auto a = selectDependencies(index, "top.sv", {"first.sv"});
        QCOMPARE(a.sources, QStringList({"first.sv", "top.sv"}));
        const auto b = selectDependencies(index, "top.sv", {"second.sv"});
        QCOMPARE(b.sources, QStringList({"second.sv", "top.sv"}));
        const auto ambiguous = selectDependencies(index, "top.sv", {});
        QVERIFY(!ambiguous.messages.isEmpty());
    }
    void waveformScopeAndIncrementalScript()
    {
        auto p = newProject("Scope"); p.sources = {"design.sv"}; p.tbFile = "tb.sv"; p.tbName = "tb";
        auto script = runScript("root", p, "run", "modelsim.ini", WaveTheme::Mocha);
        QVERIFY(!script.contains("log -r /*")); QVERIFY(!script.contains("find signals -r /*"));
        QVERIFY(script.contains("add wave \"/tb/*\""));
        p.waveScope = "all";
        QVERIFY(runScript("root", p, "run", "modelsim.ini").contains("log -r /*"));
        p.waveScope = "selected"; p.waveSignals = {"clk", "data"};
        script = runScript("root", p, "run", "modelsim.ini", WaveTheme::Nord, true);
        QVERIFY(script.contains("add wave \"/tb/data\""));
        QVERIFY(!script.contains("design.sv")); QVERIFY(script.contains("tb.sv"));
        QVERIFY(!script.contains("vlib "));
        p.stimulus = QJsonObject{{"scoreboard", QJsonObject{{"kind", "delay"}}}};
        script = runScript("root", p, "run", "modelsim.ini");
        QVERIFY(script.contains("log -r \"/tb/__simdock_scoreboard/*\""));
        QVERIFY(script.contains("Scoreboard checks failed"));
        QTemporaryDir workspace; QString error;
        QVERIFY(saveProject(workspace.path(), p, &error));
        const auto loaded = loadProjects(workspace.path()).first();
        QCOMPARE(loaded.waveScope, p.waveScope); QCOMPARE(loaded.waveSignals, p.waveSignals);
    }

    void cancelledWorkspaceScan()
    {
        QTemporaryDir workspace;
        QVERIFY(put(workspace.filePath(QStringLiteral("counter.sv")), counter()));
        std::atomic_bool cancelled{true};
        QVERIFY(scanWorkspace(workspace.path(), &cancelled).files.isEmpty());
        cancelled.store(false);
        QCOMPARE(scanWorkspace(workspace.path(), &cancelled).files.size(), 1);
    }
    void dependencyClosureAndOrder()
    {
        QTemporaryDir workspace;
        QVERIFY(put(workspace.filePath(QStringLiteral("a_top.sv")), QByteArrayLiteral(
            "`include \"defs.svh\"\nmodule top #(parameter N=2)(input logic clk);\n"
            "import outer_pkg::*; inner_pkg::word_t data;\n"
            "for(genvar i=0;i<N;i++) begin : g child #(.WIDTH(8)) u(.clk(clk)); end\n"
            "// unrelated fake();\nstring text=\"unrelated fake();\"; endmodule\n")));
        QVERIFY(put(workspace.filePath(QStringLiteral("rtl/child.sv")), QByteArrayLiteral(
            "module child #(parameter WIDTH=8)(input logic clk); leaf a(), b(); endmodule")));
        QVERIFY(put(workspace.filePath(QStringLiteral("rtl/leaf.sv")), QByteArrayLiteral("module leaf; endmodule")));
        QVERIFY(put(workspace.filePath(QStringLiteral("pkg/outer.sv")), QByteArrayLiteral("package outer_pkg; import inner_pkg::*; endpackage")));
        QVERIFY(put(workspace.filePath(QStringLiteral("pkg/inner.sv")), QByteArrayLiteral("package inner_pkg; typedef logic [7:0] word_t; endpackage")));
        QVERIFY(put(workspace.filePath(QStringLiteral("include/defs.svh")), QByteArrayLiteral("`include \"more.svh\"\n`define FLAG 1\n")));
        QVERIFY(put(workspace.filePath(QStringLiteral("include/more.svh")), QByteArrayLiteral("`define MORE 1\n")));
        QVERIFY(put(workspace.filePath(QStringLiteral("unrelated.sv")), QByteArrayLiteral("module unrelated; endmodule")));
        const auto scan = scanWorkspace(workspace.path());
        const auto selection = selectDependencies(scan, QStringLiteral("a_top.sv"), {});
        QVERIFY2(selection.messages.isEmpty(), qPrintable(selection.messages.join(QLatin1Char('\n'))));
        QCOMPARE(selection.sources.size(), 7);
        QVERIFY(!selection.sources.contains(QStringLiteral("unrelated.sv")));
        QCOMPARE(selection.sources.last(), QStringLiteral("a_top.sv"));
        QVERIFY(selection.sources.indexOf(QStringLiteral("pkg/inner.sv")) < selection.sources.indexOf(QStringLiteral("pkg/outer.sv")));
        QVERIFY(selection.sources.indexOf(QStringLiteral("rtl/leaf.sv")) < selection.sources.indexOf(QStringLiteral("rtl/child.sv")));
        QVERIFY(selection.sources.indexOf(QStringLiteral("include/more.svh")) < selection.sources.indexOf(QStringLiteral("include/defs.svh")));
    }
    void dependencyAmbiguityCyclesAndScope()
    {
        QTemporaryDir temp;
        const auto root = temp.filePath(QStringLiteral("workspace"));
        QVERIFY(put(QDir(root).filePath(QStringLiteral("top.sv")), QByteArrayLiteral(
            "`include \"../outside.svh\"\nmodule top; child u(); missing v(); endmodule")));
        QVERIFY(put(QDir(root).filePath(QStringLiteral("one.sv")), QByteArrayLiteral("module child; top back(); endmodule")));
        QVERIFY(put(QDir(root).filePath(QStringLiteral("two.sv")), QByteArrayLiteral("module child; endmodule")));
        QVERIFY(put(temp.filePath(QStringLiteral("outside.svh")), QByteArrayLiteral("module missing; endmodule")));
        const auto scan = scanWorkspace(root);
        const auto ambiguous = selectDependencies(scan, QStringLiteral("top.sv"), {});
        QCOMPARE(ambiguous.sources, QStringList{QStringLiteral("top.sv")});
        const auto messages = ambiguous.messages.join(QLatin1Char('\n'));
        QVERIFY(messages.contains(QStringLiteral("multiple definitions")));
        QVERIFY(messages.contains(QStringLiteral("outside the analyzed workspace")));
        QVERIFY(messages.contains(QStringLiteral("'missing' was not found")));
        const auto chosen = selectDependencies(scan, QStringLiteral("top.sv"), {QStringLiteral("one.sv")});
        QCOMPARE(chosen.sources.size(), 2);
        QVERIFY(chosen.sources.contains(QStringLiteral("one.sv")));
        QVERIFY(!chosen.sources.contains(QStringLiteral("two.sv")));
        QVERIFY(chosen.messages.join(QLatin1Char('\n')).contains(QStringLiteral("cycle")));
    }
    void interfaceAndEscapedDependencies()
    {
        Scan scan;
        scan.files << analyzeSource(QByteArrayLiteral("interface bus_if; logic clk; endinterface"), QStringLiteral("bus.sv"));
        scan.files << analyzeSource(QByteArrayLiteral("module \\escaped.leaf ; endmodule"), QStringLiteral("leaf.v"));
        scan.files << analyzeSource(QByteArrayLiteral("module top; bus_if bus(); \\escaped.leaf u(); endmodule"), QStringLiteral("top.sv"));
        const auto selection = selectDependencies(scan, QStringLiteral("top.sv"), {});
        QVERIFY2(selection.messages.isEmpty(), qPrintable(selection.messages.join(QLatin1Char('\n'))));
        QCOMPARE(selection.sources, (QStringList{QStringLiteral("bus.sv"), QStringLiteral("leaf.v"), QStringLiteral("top.sv")}));
    }
    void qualifiedPackageConstants()
    {
        const auto bytes = QByteArrayLiteral("module child #(parameter int W=cfg::WIDTH)(input logic clk, output logic [W-1:0] data); endmodule");
        const auto source = analyzeSource(bytes, QStringLiteral("child.sv"));
        QVERIFY2(source.referencedPackages.contains(QStringLiteral("cfg")), qPrintable(syntaxTree(bytes)));
    }
    void ansiParametersAndInheritedPorts()
    {
        const auto source = analyzeSource(counter(), QStringLiteral("rtl/counter.sv"));
        QVERIFY2(!source.syntaxError, qPrintable(syntaxTree(counter())));
        QCOMPARE(source.modules.size(), 1);
        const auto& m = source.modules.first();
        QCOMPARE(m.name, QStringLiteral("counter"));
        QCOMPARE(m.parameters.size(), 1);
        QCOMPARE(m.parameters.first().name, QStringLiteral("WIDTH"));
        QCOMPARE(m.parameters.first().value, QStringLiteral("8"));
        QCOMPARE(m.ports.size(), 4);
        QCOMPARE(m.ports[1].direction, QStringLiteral("input"));
        QCOMPARE(m.ports.last().type, QStringLiteral("logic [WIDTH-1:0]"));
        QVERIFY2(m.limitations.isEmpty(), qPrintable(m.limitations.join(QLatin1Char('\n'))));
    }
    void nonAnsiPorts()
    {
        const auto bytes = QByteArrayLiteral("module legacy(clk, data, result); input clk; input [7:0] data; output [7:0] result; assign result=data; endmodule");
        const auto source = analyzeSource(bytes, QStringLiteral("legacy.v"));
        QCOMPARE(source.modules.size(), 1);
        QVERIFY2(source.modules.first().ports.size() == 3, qPrintable(syntaxTree(bytes)));
        QCOMPARE(source.modules.first().ports.last().direction, QStringLiteral("output"));
    }
    void unsupportedPortIsExplicit()
    {
        const auto source = analyzeSource(QByteArrayLiteral("module m(input logic [7:0] x [4]); endmodule"), QStringLiteral("m.sv"));
        QCOMPARE(source.modules.size(), 1);
        QVERIFY(!source.modules.first().limitations.isEmpty());
        QString error;
        QVERIFY(generateTestbench(source.modules.first(), suggestedTbOptions(source.modules.first()), 1000, &error).isEmpty());
        QVERIFY(error.contains(QStringLiteral("unpacked")));
    }
    void generateDemo()
    {
        const auto module = analyzeSource(counter(), QStringLiteral("counter.sv")).modules.first();
        const auto options = suggestedTbOptions(module);
        QCOMPARE(options.clock, QStringLiteral("clk"));
        QCOMPARE(options.reset, QStringLiteral("rst_n"));
        QVERIFY(options.resetActiveLow);
        QString error;
        const auto tb = generateTestbench(module, options, 1000, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(tb.contains(QStringLiteral(".WIDTH(WIDTH)")));
        QVERIFY(tb.contains(QStringLiteral("enable = '0")));
        const auto parsed = analyzeSource(tb.toUtf8(), QStringLiteral("tb_counter.sv"));
        QVERIFY2(!parsed.syntaxError, qPrintable(syntaxTree(tb.toUtf8())));
        QCOMPARE(parsed.modules.first().name, QStringLiteral("tb_counter"));
    }
    void namedPackageTypes()
    {
        const auto source = analyzeSource(typedModule(), QStringLiteral("typed.sv"));
        QVERIFY2(!source.syntaxError, qPrintable(syntaxTree(typedModule())));
        QCOMPARE(source.modules.size(), 1);
        const auto& module = source.modules.first();
        QVERIFY2(module.limitations.isEmpty(), qPrintable(module.limitations.join(QLatin1Char('\n'))));
        QCOMPARE(module.imports, QStringList{QStringLiteral("import demo_types::*;")});
        QCOMPARE(module.ports.size(), 7);
        QVERIFY(module.ports[2].namedType);
        QVERIFY(module.ports[3].namedType);
        QCOMPARE(module.ports[3].type, QStringLiteral("mode_e"));
        QVERIFY(!isTimingInput(module.ports[2]));
        auto options = suggestedTbOptions(module);
        QCOMPARE(options.clock, QStringLiteral("clk"));
        QCOMPARE(options.reset, QStringLiteral("rst"));
        QString error;
        const auto tb = generateTestbench(module, options, 1000, &error);
        QVERIFY2(!tb.isEmpty(), qPrintable(error));
        QVERIFY(tb.indexOf(QStringLiteral("import demo_types::*;")) < tb.indexOf(QStringLiteral("localparam int WIDTH")));
        QVERIFY(tb.contains(QStringLiteral("mode_e mode_copy;")));
        QVERIFY(tb.contains(QStringLiteral("mode_copy = mode_e'('0);")));
        QVERIFY(tb.contains(QStringLiteral("payload = demo_types::payload_t'('0);")));
        QVERIFY(tb.contains(QStringLiteral("data = word_t'('0);")));
        QVERIFY(!tb.contains(QStringLiteral("result =")));
        QVERIFY2(!analyzeSource(tb.toUtf8(), QStringLiteral("tb.sv")).syntaxError, qPrintable(syntaxTree(tb.toUtf8())));
        options.clock = QStringLiteral("mode");
        QVERIFY(generateTestbench(module, options, 1000, &error).isEmpty());
        QVERIFY(error.contains(QStringLiteral("scalar input")));
    }
    void importVisibility()
    {
        const auto input = QByteArrayLiteral("module first(input logic clk); import hidden::*; endmodule\n"
            "import demo_types::mode_e, demo_types::word_t;\n"
            "module selected(input mode_e mode, input word_t data); endmodule\n"
            "module qualified(input demo_types::payload_t payload); endmodule\n"
            "import later::*;\n");
        const auto source = analyzeSource(input, QStringLiteral("imports.sv"));
        QVERIFY2(!source.syntaxError, qPrintable(syntaxTree(input)));
        QCOMPARE(source.modules.size(), 3);
        QVERIFY(source.modules.first().imports.isEmpty());
        for (int i = 1; i < source.modules.size(); ++i) {
            const auto& module = source.modules[i];
            QVERIFY2(module.limitations.isEmpty(), qPrintable(module.limitations.join(QLatin1Char('\n'))));
            QCOMPARE(module.imports, QStringList{QStringLiteral("import demo_types::mode_e, demo_types::word_t;")});
            QString error;
            QVERIFY2(!generateTestbench(module, suggestedTbOptions(module), 1000, &error).isEmpty(), qPrintable(error));
        }
        const auto qualified = analyzeSource(QByteArrayLiteral("module qualified(input demo_types::mode_e mode); endmodule"), QStringLiteral("qualified.sv")).modules.first();
        QVERIFY(qualified.limitations.isEmpty());
        QVERIFY(qualified.ports.first().namedType);
    }
    void unsupportedNamedPorts_data()
    {
        QTest::addColumn<QByteArray>("input");
        QTest::newRow("unresolved") << QByteArrayLiteral("module m(input missing_type value); endmodule");
        QTest::newRow("unimported") << QByteArrayLiteral("module m import demo_types::word_t; (input mode_e value); endmodule");
        QTest::newRow("interface") << QByteArrayLiteral("module m import demo_types::*; (bus_if bus); endmodule");
        QTest::newRow("modport") << QByteArrayLiteral("module m import demo_types::*; (bus_if.master bus); endmodule");
        QTest::newRow("inout") << QByteArrayLiteral("module m(inout demo_types::word_t data); endmodule");
        QTest::newRow("ref") << QByteArrayLiteral("module m(ref demo_types::word_t data); endmodule");
        QTest::newRow("unpacked") << QByteArrayLiteral("module m(input demo_types::word_t data [4]); endmodule");
        QTest::newRow("macro") << QByteArrayLiteral("module m(input `CUSTOM_TYPE data); endmodule");
    }
    void unsupportedNamedPorts()
    {
        QFETCH(QByteArray, input);
        const auto source = analyzeSource(input, QStringLiteral("unsupported.sv"));
        QCOMPARE(source.modules.size(), 1);
        const auto& module = source.modules.first();
        QVERIFY2(!module.limitations.isEmpty(), qPrintable(syntaxTree(input)));
        QString error;
        QVERIFY(generateTestbench(module, suggestedTbOptions(module), 1000, &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }
    void liveNamedTypeCompilation()
    {
        const QString executable = qEnvironmentVariable("SIMDOCK_TEST_QUESTA");
        if (executable.isEmpty()) QSKIP("Set SIMDOCK_TEST_QUESTA to compile generated named-type ports in Questa.");
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        QVERIFY(put(workspace.filePath(QStringLiteral("types.sv")), typedPackage()));
        QVERIFY(put(workspace.filePath(QStringLiteral("dut.sv")), typedModule()));
        const auto module = analyzeSource(typedModule(), QStringLiteral("dut.sv")).modules.first();
        QString error;
        const auto tb = generateTestbench(module, suggestedTbOptions(module), 1000, &error);
        QVERIFY2(!tb.isEmpty(), qPrintable(error));
        QVERIFY(put(workspace.filePath(QStringLiteral("tb.sv")), tb.toUtf8()));
        const auto selected = selectDependencies(scanWorkspace(workspace.path()), QStringLiteral("dut.sv"), {});
        QCOMPARE(selected.sources, (QStringList{QStringLiteral("types.sv"), QStringLiteral("dut.sv")}));
        const QDir tools = QFileInfo(executable).dir();
        const auto compile = [&](const QString& tool, const QStringList& arguments) {
            QProcess process;
            process.setWorkingDirectory(workspace.path());
            process.setProcessChannelMode(QProcess::MergedChannels);
            process.start(tools.filePath(tool), arguments);
            const bool finished = process.waitForFinished(60000);
            if (!finished) { process.kill(); process.waitForFinished(5000); }
            error = QString::fromLocal8Bit(process.readAll()) + process.errorString();
            return finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
        };
        QVERIFY2(compile(QStringLiteral("vlib"), {QStringLiteral("work")}), qPrintable(error));
        QVERIFY2(compile(QStringLiteral("vlog"), {QStringLiteral("-sv"), QStringLiteral("-work"), QStringLiteral("work"),
            QStringLiteral("types.sv"), QStringLiteral("dut.sv"), QStringLiteral("tb.sv")}), qPrintable(error));
    }
    void workspaceScopeAndPersistence()
    {
        QTemporaryDir temp;
        const QString root = temp.filePath(QStringLiteral("workspace"));
        QVERIFY(put(QDir(root).filePath(QStringLiteral("rtl/counter.sv")), counter()));
        QVERIFY(put(temp.filePath(QStringLiteral("outside.sv")), QByteArrayLiteral("module outside; endmodule")));
        QVERIFY(put(QDir(root).filePath(QStringLiteral("build/ignored.sv")), counter()));
        const auto scan = scanWorkspace(root);
        QCOMPARE(scan.files.size(), 1);
        QVERIFY(!insideWorkspace(root, temp.filePath(QStringLiteral("outside.sv"))));
        auto p = newProject(QStringLiteral("项目 A"));
        p.sources << QStringLiteral("rtl/counter.sv");
        p.dutName = QStringLiteral("counter");
        p.dutFile = p.sources.first();
        QString error;
        QVERIFY2(saveProject(root, p, &error), qPrintable(error));
        const auto loaded = loadProjects(root);
        QCOMPARE(loaded.size(), 1);
        QCOMPARE(loaded.first().sources, p.sources);
        p.sources << QStringLiteral("../outside.sv");
        QVERIFY(!saveProject(root, p, &error));
        QCOMPARE(loadProjects(root).first().sources.size(), 1);
    }
    void includeCannotEscape()
    {
        QTemporaryDir temp;
        const QString root = temp.filePath(QStringLiteral("workspace"));
        QVERIFY(put(temp.filePath(QStringLiteral("outside.svh")), QByteArrayLiteral("")));
        const auto source = QByteArray(1, char(96)) + QByteArrayLiteral("include \"../outside.svh\"\nmodule top; endmodule\n");
        QVERIFY(put(QDir(root).filePath(QStringLiteral("top.sv")), source));
        QVERIFY(put(QDir(root).filePath(QStringLiteral("tb.sv")), QByteArrayLiteral("module tb; endmodule")));
        auto p = newProject(QStringLiteral("escape"));
        p.sources << QStringLiteral("top.sv");
        p.tbFile = QStringLiteral("tb.sv"); p.tbName = QStringLiteral("tb");
        QVERIFY2(validateInputs(root, p).contains(QStringLiteral("outside the workspace")), qPrintable(syntaxTree(source)));
    }
    void neverOverwriteTb()
    {
        QTemporaryDir temp;
        QString error;
        const auto path = QStringLiteral("sim/tb.sv");
        QVERIFY(writeNewTb(temp.path(), path, QStringLiteral("original"), &error));
        QVERIFY(!writeNewTb(temp.path(), path, QStringLiteral("replacement"), &error));
        QFile file(temp.filePath(path));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArrayLiteral("original"));
    }
    void tclArgumentsAreData()
    {
        QCOMPARE(tclWord(QStringLiteral("a $x [exec wrong] \\ \" b")), QStringLiteral("\"a \\$x \\[exec wrong\\] \\\\ \\\" b\""));
    }
    void liveDependencyCompilation()
    {
        const QString executable = qEnvironmentVariable("SIMDOCK_TEST_QUESTA");
        if (executable.isEmpty()) QSKIP("Set SIMDOCK_TEST_QUESTA to verify dependency compilation in Questa.");
        QTemporaryDir workspace;
        const auto top = QByteArrayLiteral("`include \"defs.svh\"\nmodule top(input logic clk, output logic [7:0] data); child #(.W(`WIDTH)) u(.clk(clk), .data(data)); endmodule\n");
        QVERIFY(put(workspace.filePath(QStringLiteral("a_top.sv")), top));
        QVERIFY(put(workspace.filePath(QStringLiteral("child.sv")), QByteArrayLiteral("module child #(parameter int W=cfg::WIDTH)(input logic clk, output logic [W-1:0] data); initial data='0; always @(posedge clk) data<=data+1'b1; endmodule\n")));
        QVERIFY(put(workspace.filePath(QStringLiteral("z_pkg.sv")), QByteArrayLiteral("package cfg; parameter int WIDTH=8; endpackage\n")));
        QVERIFY(put(workspace.filePath(QStringLiteral("include/defs.svh")), QByteArrayLiteral("`define WIDTH 8\n")));
        const auto selection = selectDependencies(scanWorkspace(workspace.path()), QStringLiteral("a_top.sv"), {});
        QVERIFY2(selection.messages.isEmpty(), qPrintable(selection.messages.join(QLatin1Char('\n'))));
        auto project = newProject(QStringLiteral("Dependency compilation"));
        project.sources = selection.sources;
        project.tbFile = QStringLiteral("tb_top.sv");
        project.tbName = QStringLiteral("tb_top");
        const auto module = analyzeSource(top, QStringLiteral("a_top.sv")).modules.first();
        QString error;
        const auto tb = generateTestbench(module, suggestedTbOptions(module), 1000, &error);
        QVERIFY2(!tb.isEmpty(), qPrintable(error));
        QVERIFY(writeNewTb(workspace.path(), project.tbFile, tb, &error));
        QuestaSession session;
        QSignalSpy finished(&session, &QuestaSession::finished);
        QString log;
        connect(&session, &QuestaSession::logText, this, [&log](const QString& text) { log += text; });
        QVERIFY2(session.start(executable, workspace.path(), project, &error), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 90000);
        QVERIFY2(finished.first()[0].toBool(), qPrintable(log));
        QVERIFY(log.indexOf(QStringLiteral("Compiling package cfg")) < log.indexOf(QStringLiteral("Compiling module child")));
        session.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!session.alive(), 5000);
    }
    void liveStateReaderReleasesHandle()
    {
#ifndef Q_OS_WIN
        QSKIP("Windows sharing semantics are required.");
#else
        const auto executable = qEnvironmentVariable("SIMDOCK_TEST_QUESTA");
        if (executable.isEmpty()) QSKIP("Set SIMDOCK_TEST_QUESTA to verify the real Questa state handoff.");
        QTemporaryDir workspace;
        QVERIFY(put(workspace.filePath("tb.sv"), "module tb; reg clk=0; always #5 clk=~clk; endmodule\n"));
        auto project = newProject("State reader");
        project.sources = {"tb.sv"}; project.tbFile = "tb.sv"; project.tbName = "tb";
        QString log, directory, error;
        bool probed = false;
        DWORD sharingError = ERROR_SUCCESS;
        QuestaSession session;
        QSignalSpy finished(&session, &QuestaSession::finished);
        connect(&session, &QuestaSession::logText, this, [&log](const QString& text) { log += text; });
        connect(&session, &QuestaSession::stateChanged, this, [&](const QString& state) {
            if (state != QStringLiteral("Completed")) return;
            // DELETE access detects a read handle that denies atomic replacement,
            // without modifying the file or racing the simulator's contents.
            const auto handle = openState(QDir(directory).filePath("state"), DELETE,
                                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
            sharingError = handle == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
            if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
            probed = true;
        });
        const auto previous = sessionDirectories();
        QVERIFY2(session.start(executable, workspace.path(), project, &error), qPrintable(error));
        directory = newSessionDirectory(previous); QVERIFY(!directory.isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 90000);
        QVERIFY2(finished.first()[0].toBool(), qPrintable(log));
        QVERIFY(captureSession("reader", directory, session, log));
        qInfo() << "State replacement access during Completed callback: Win32 error" << sharingError;
        QVERIFY(probed);
        QCOMPARE(sharingError, DWORD(ERROR_SUCCESS));
        session.stop(); QTRY_VERIFY_WITH_TIMEOUT(!session.alive(), 5000);
#endif
    }
    void liveStatePublicationContention()
    {
#ifndef Q_OS_WIN
        QSKIP("Windows sharing semantics are required.");
#else
        const auto executable = qEnvironmentVariable("SIMDOCK_TEST_QUESTA");
        if (executable.isEmpty()) QSKIP("Set SIMDOCK_TEST_QUESTA to verify real Questa state publication.");
        QTemporaryDir workspace;
        QVERIFY(put(workspace.filePath("tb.sv"), "module tb; reg clk=0; always #5 clk=~clk; endmodule\n"));
        auto project = newProject("State contention");
        project.sources = {"tb.sv"}; project.tbFile = "tb.sv"; project.tbName = "tb";
        QString log, error;
        QuestaSession session;
        QSignalSpy finished(&session, &QuestaSession::finished);
        connect(&session, &QuestaSession::logText, this, [&log](const QString& text) { log += text; });
        const auto previous = sessionDirectories();
        QVERIFY2(session.start(executable, workspace.path(), project, &error), qPrintable(error));
        auto directory = newSessionDirectory(previous); QVERIFY(!directory.isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 90000);
        QVERIFY2(finished.takeFirst()[0].toBool(), qPrintable(log));
        const auto pid = session.processId();
        const auto statePath = QDir(directory).filePath("state");
        const auto oldRecord = read(statePath);
        QVERIFY(QByteArray(oldRecord).replace("\r\n", "\n").endsWith("|completed\n"));
        HANDLE lock = openState(statePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE);
        const auto unlock = qScopeGuard([&] { if (lock != INVALID_HANDLE_VALUE) CloseHandle(lock); });
        QVERIFY(lock != INVALID_HANDLE_VALUE);
        QVERIFY2(session.start(executable, workspace.path(), project, &error), qPrintable(error));
        const auto runId = QFileInfo(session.lastRunDirectory()).fileName().toUtf8();
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(QDir(directory).filePath("state.tmp")), 5000);
        QVERIFY(captureSession("contention-locked", directory, session, log));
        QCOMPARE(read(QDir(directory).filePath("state.tmp")).replace("\r\n", "\n"), runId + "|compiling\n");
        QCOMPARE(read(statePath), oldRecord);
        QTest::qWait(180);
        QCOMPARE(finished.size(), 0); QVERIFY(session.busy());
        QVERIFY(put(statePath, runId + "|completed"));
        QTest::qWait(180);
        QCOMPARE(finished.size(), 0); QVERIFY(session.busy());
        QVERIFY(put(statePath, runId + "|completed|unexpected\n"));
        QTest::qWait(180);
        QCOMPARE(finished.size(), 0); QVERIFY(session.busy());
        QVERIFY(put(statePath, oldRecord));
        CloseHandle(lock); lock = INVALID_HANDLE_VALUE;
        QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 45000);
        QVERIFY2(finished.takeFirst()[0].toBool(), qPrintable(log));
        QCOMPARE(session.processId(), pid);
        QVERIFY2(!log.contains("SimDock error:"), qPrintable(log));
        QVERIFY(captureSession("contention-recovered", directory, session, log));
        qInfo() << "Transient lock recovered; stale, partial and malformed completion records were ignored; pid" << pid;

        lock = openState(statePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE);
        QVERIFY(lock != INVALID_HANDLE_VALUE);
        QElapsedTimer elapsed; elapsed.start();
        QVERIFY2(session.start(executable, workspace.path(), project, &error), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(!session.alive(), 10000);
        QCOMPARE(finished.size(), 1);
        QVERIFY(!finished.takeFirst()[0].toBool());
        QVERIFY(!session.busy());
        QVERIFY2(log.contains("SimDock session error:"), qPrintable(log));
        QVERIFY2(log.contains("Could not publish Questa state"), qPrintable(log));
        QVERIFY(!log.contains("wall-clock timeout"));
        qInfo() << "Persistent state lock reported failure and closed the session after" << elapsed.elapsed() << "ms";
        QVERIFY(captureSession("contention-exhausted", directory, session, log));
        CloseHandle(lock); lock = INVALID_HANDLE_VALUE;
        const auto beforeRestart = sessionDirectories();
        QVERIFY2(session.start(executable, workspace.path(), project, &error), qPrintable(error));
        directory = newSessionDirectory(beforeRestart); QVERIFY(!directory.isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 90000);
        QVERIFY2(finished.takeFirst()[0].toBool(), qPrintable(log));
        QVERIFY(QFileInfo(QDir(session.lastRunDirectory()).filePath("result.wlf")).size() > 0);
        QVERIFY(captureSession("contention-restarted", directory, session, log));

        lock = openState(QDir(directory).filePath("state"), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE);
        QVERIFY(lock != INVALID_HANDLE_VALUE);
        QVERIFY2(session.start(executable, workspace.path(), project, &error), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(QDir(directory).filePath("state.tmp")), 5000);
        session.stop(); QTRY_VERIFY_WITH_TIMEOUT(!session.alive(), 5000);
        QCOMPARE(finished.size(), 1); QVERIFY(!finished.takeFirst()[0].toBool());
        QVERIFY(!session.busy());
        QVERIFY(captureSession("contention-cancelled", directory, session, log));
        CloseHandle(lock); lock = INVALID_HANDLE_VALUE;

        const auto beforeTimeout = sessionDirectories();
        session.setWallTimeout(1);
        QVERIFY2(session.start(executable, workspace.path(), project, &error), qPrintable(error));
        directory = newSessionDirectory(beforeTimeout); QVERIFY(!directory.isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!session.alive(), 5000);
        QCOMPARE(finished.size(), 1); QVERIFY(!finished.takeFirst()[0].toBool());
        QVERIFY(!session.busy()); QVERIFY(log.contains("wall-clock timeout"));
        QVERIFY(captureSession("deadline", directory, session, log));
        qInfo() << "Stop during publication retry and the wall-clock deadline both closed their managed sessions";
#endif
    }
    void liveLoadFailureAndRecovery()
    {
        const QString executable = qEnvironmentVariable("SIMDOCK_TEST_QUESTA");
        if (executable.isEmpty()) QSKIP("Set SIMDOCK_TEST_QUESTA to verify Questa load failure and recovery.");
        QTemporaryDir workspace;
        const auto broken = QByteArrayLiteral("module load_error(input logic clk, output logic value);\n"
            "initial value=0; always_ff @(posedge clk) value <= ~value; endmodule\n");
        QVERIFY(put(workspace.filePath(QStringLiteral("dut.sv")), broken));
        auto project = newProject(QStringLiteral("Load failure"));
        project.sources << QStringLiteral("dut.sv");
        project.tbFile = QStringLiteral("tb.sv");
        project.tbName = QStringLiteral("tb_load_error");
        const auto module = analyzeSource(broken, QStringLiteral("dut.sv")).modules.first();
        QString error;
        QVERIFY(put(workspace.filePath(project.tbFile), generateTestbench(module, suggestedTbOptions(module), project.durationNs, &error).toUtf8()));
        QuestaSession session;
        QSignalSpy finished(&session, &QuestaSession::finished);
        QSignalSpy states(&session, &QuestaSession::stateChanged);
        QString log;
        const auto artifact = qEnvironmentVariable("SIMDOCK_TEST_LOG");
        connect(&session, &QuestaSession::logText, this, [&log, artifact](const QString& text) {
            log += text;
            if (!artifact.isEmpty()) put(artifact, log.toUtf8());
        });
        QVERIFY2(session.start(executable, workspace.path(), project, &error), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 90000);
        QVERIFY2(!finished.takeFirst()[0].toBool(), qPrintable(log));
        QVERIFY2(log.contains(QStringLiteral("7061")), qPrintable(log));
        QVERIFY2(log.contains(QStringLiteral("Questa could not load the testbench")), qPrintable(log));
        QVERIFY(!log.contains(QStringLiteral("Active logfile is not in simulation mode")));
        for (const auto& state : states) QVERIFY(state[0].toString() != QStringLiteral("Running"));
        QVERIFY(session.alive());
        const auto pid = session.processId();
        const auto fixed = QByteArrayLiteral("module load_error(input logic clk, output logic value=0);\n"
            "always_ff @(posedge clk) value <= ~value; endmodule\n");
        QVERIFY(put(workspace.filePath(QStringLiteral("dut.sv")), fixed));
        states.clear();
        QVERIFY2(session.start(executable, workspace.path(), project, &error), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 45000);
        QVERIFY2(finished.takeFirst()[0].toBool(), qPrintable(log));
        QCOMPARE(session.processId(), pid);
        QVERIFY(QFileInfo(QDir(session.lastRunDirectory()).filePath(QStringLiteral("result.wlf"))).size() > 0);
        session.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!session.alive(), 5000);
    }
    void liveWaveThemes()
    {
        const QString executable = qEnvironmentVariable("SIMDOCK_TEST_QUESTA");
        if (executable.isEmpty()) QSKIP("Set SIMDOCK_TEST_QUESTA to verify waveform colors in Questa.");
        // Questa 2024.1's preference writer misparses AppData in Windows TEMP.
        QTemporaryDir workspace(QDir::currentPath() + QStringLiteral("/simdock-wave-colors-XXXXXX"));
        QVERIFY(workspace.isValid());
        QVERIFY(put(workspace.filePath(QStringLiteral("tb_colors.sv")), QByteArrayLiteral(
            "`timescale 1ns/1ps\nmodule tb_colors;\n"
            "logic clk=0, rst_n=0, valid=0; logic [7:0] data=0;\n"
            "typedef enum logic [1:0] { IDLE, TX } state_t; state_t state=IDLE;\n"
            "wire valid_x=1'bx, ready_z=1'bz; wire [1:0] data_x=2'bxx, data_z=2'bzz;\n"
            "for (genvar i=0; i<2; i++) begin : lanes wire ready=1'b1; end\n"
            "always #5 clk=~clk; initial begin #20; rst_n=1; #10; valid=1; data=8'ha5; state=TX; end\n"
            "endmodule\n")));
        auto project = newProject(QStringLiteral("Wave colors"));
        project.sources = {QStringLiteral("tb_colors.sv")};
        project.tbFile = QStringLiteral("tb_colors.sv");
        project.tbName = QStringLiteral("tb_colors");
        project.durationNs = 100;
        QString script = QStringLiteral("if {[catch {\nnamespace eval ::simdock {}\nproc ::simdock::state {value} {}\n");
        const QList<WaveTheme> themes{WaveTheme::QuestaDefault, WaveTheme::Mocha, WaveTheme::Nord, WaveTheme::SolarizedLight, WaveTheme::QuestaDefault};
        QStringList formats;
        for (int index = 0; index < themes.size(); ++index) {
            const auto theme = themes[index];
            const auto& palette = wavePalette(theme);
            const auto runDir = workspace.filePath(QString::number(index));
            QVERIFY(QDir().mkpath(runDir));
            const auto ini = QDir(runDir).filePath(QStringLiteral("modelsim.ini"));
            const auto installed = QDir(QFileInfo(executable).absolutePath()).absoluteFilePath(QStringLiteral("../modelsim.ini"));
            QVERIFY(put(ini, QStringLiteral("[Library]\nwork = %1/work\nothers = %2\n").arg(runDir, installed).toUtf8()));
            project.waveScope = QStringLiteral("all");
            script += runScript(workspace.path(), project, runDir, ini, theme);
            if (index == 0) {
                script += QStringLiteral("set nativeBackground $::PrefWave(waveBackground)\nset nativeLogic [array get ::LogicStyleTable]\n");
            } else if (theme == WaveTheme::QuestaDefault) {
                script += QStringLiteral("if {[lindex [configure wave -wavebackground] end] ne $nativeBackground} {error \"Default background was not restored: [lindex [configure wave -wavebackground] end], expected $nativeBackground\"}\n"
                    "foreach {key value} $nativeLogic {if {$::LogicStyleTable($key) ne $value} {error \"Default logic style was not restored\"}}\n");
            } else {
                script += QStringLiteral("if {[lindex [configure wave -wavebackground] end] ne %1} {error \"Wrong waveform background\"}\n"
                    "if {[lindex [configure wave -vectorcolor] end] ne %2} {error \"Wrong vector color\"}\n"
                    "if {[lindex $::LogicStyleTable(LOGIC_X) 1] ne %3} {error \"Wrong X color\"}\n"
                    "if {[lindex $::LogicStyleTable(LOGIC_Z) 1] ne %4} {error \"Wrong Z color\"}\n")
                    .arg(tclWord(palette.background), tclWord(palette.data), tclWord(palette.unknown), tclWord(palette.highZ));
            }
            const auto format = QDir(runDir).filePath(QStringLiteral("wave.do"));
            formats << format;
            script += QStringLiteral("write format wave %1\nputs \"WAVE_THEME_OK %2\"\n")
                .arg(tclWord(format), palette.id);
        }
        script += QStringLiteral("} message details]} {puts \"WAVE_THEME_FAILURE $message\"; puts [dict get $details -errorinfo]; quit -force -code 1}\nquit -force -code 0\n");
        const auto scriptPath = workspace.filePath(QStringLiteral("verify.do"));
        QVERIFY(put(scriptPath, script.toUtf8()));
        QProcess process;
        process.setProcessChannelMode(QProcess::MergedChannels);
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("MODELSIM_PREFERENCES"), workspace.filePath(QStringLiteral("preferences")));
        process.setProcessEnvironment(environment);
        process.setWorkingDirectory(workspace.path());
#ifdef Q_OS_WIN
        process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
            args->flags |= CREATE_NO_WINDOW;
            args->startupInfo->dwFlags |= STARTF_USESHOWWINDOW;
            args->startupInfo->wShowWindow = SW_HIDE;
        });
#endif
        process.start(executable, {QStringLiteral("-gui"), QStringLiteral("-do"), QStringLiteral("source -encoding utf-8 ") + tclWord(scriptPath)});
        QVERIFY(process.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(process.state() == QProcess::NotRunning, 90000);
        QFile transcript(workspace.filePath(QStringLiteral("transcript")));
        QVERIFY(transcript.open(QIODevice::ReadOnly));
        const auto log = transcript.readAll() + process.readAllStandardOutput();
        const auto artifacts = qEnvironmentVariable("SIMDOCK_TEST_WAVE_DIR");
        if (!artifacts.isEmpty()) {
            QVERIFY(put(QDir(artifacts).filePath(QStringLiteral("wave-themes.log")), log));
            for (int index = 0; index < formats.size(); ++index) {
                QFile source(formats[index]);
                if (source.open(QIODevice::ReadOnly))
                    QVERIFY(put(QDir(artifacts).filePath(QStringLiteral("%1-%2-wave.do").arg(index).arg(wavePalette(themes[index]).id)), source.readAll()));
            }
        }
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QVERIFY2(process.exitCode() == 0, log.constData());
        QVERIFY2(!log.contains("SimDock warning:"), log.constData());
        QCOMPARE(log.count("# WAVE_THEME_OK "), 5);
        for (int index = 1; index < 4; ++index) {
            QFile format(formats[index]);
            QVERIFY(format.open(QIODevice::ReadOnly));
            const auto lines = QString::fromUtf8(format.readAll()).split(QLatin1Char('\n'));
            const auto& palette = wavePalette(themes[index]);
            for (const auto& nameAndColor : {qMakePair(QStringLiteral("/clk"), palette.clock), qMakePair(QStringLiteral("/rst_n"), palette.reset),
                 qMakePair(QStringLiteral("/valid"), palette.handshake), qMakePair(QStringLiteral("/state"), palette.state), qMakePair(QStringLiteral("/lanes[0]/ready"), palette.handshake)}) {
                bool found = false;
                for (const auto& line : lines) if (line.contains(nameAndColor.first) && line.contains(nameAndColor.second)) found = true;
                QVERIFY2(found, qPrintable(QStringLiteral("Missing colored signal %1 in %2").arg(nameAndColor.first, formats[index])));
            }
        }
        QFile defaults(formats.last());
        QVERIFY(defaults.open(QIODevice::ReadOnly));
        QVERIFY(!defaults.readAll().contains(wavePalette(WaveTheme::SolarizedLight).clock.toUtf8()));
    }
    void liveCompilationReuse()
    {
        const auto executable = qEnvironmentVariable("SIMDOCK_TEST_QUESTA");
        if (executable.isEmpty()) QSKIP("Set SIMDOCK_TEST_QUESTA for compilation reuse integration.");
        QTemporaryDir workspace;
        auto rtl = counter();
        rtl.replace("always_ff", "`include \"step.svh\"\nalways_ff");
        rtl.replace("count+1'b1", "count+STEP");
        QVERIFY(put(workspace.filePath("counter.sv"), rtl));
        QVERIFY(put(workspace.filePath("step.svh"), "localparam STEP=1;\n"));
        auto p = newProject("Compile reuse");
        p.sources = {"counter.sv"}; p.dutFile = "counter.sv"; p.dutName = "counter";
        const auto scan = scanWorkspace(workspace.path());
        const auto module = analyzeSource(rtl, "counter.sv").modules.first();
        QString error;
        auto drawing = newStimulus(module, scan, suggestedTbOptions(module), 100, &error);
        QVERIFY2(!drawing.isEmpty(), qPrintable(error));
        QVERIFY2(saveStimulus(workspace.path(), p, module, scan, drawing, &error), qPrintable(error));
        QuestaSession session; QSignalSpy finished(&session, &QuestaSession::finished);
        QString log; connect(&session, &QuestaSession::logText, this, [&log](const QString &text) { log += text; });
        auto prepared = prepareRun(executable, workspace.path(), p);
        QVERIFY2(prepared.error.isEmpty(), qPrintable(prepared.error));
        QVERIFY(!prepared.designFingerprint.isEmpty());
        const auto firstFingerprint = prepared.designFingerprint;
        QVERIFY2(session.start(prepared, &error), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 90000);
        QVERIFY2(finished.takeFirst()[0].toBool(), qPrintable(log));
        const auto pid = session.processId();
        drawing = retimeStimulus(module, scan, drawing, stimulusOptions(drawing), 200, &error);
        QVERIFY2(saveStimulus(workspace.path(), p, module, scan, drawing, &error), qPrintable(error));
        p.waveScope = "selected"; p.waveSignals = {"clk", "count"};
        prepared = prepareRun(executable, workspace.path(), p);
        QCOMPARE(prepared.designFingerprint, firstFingerprint);
        QVERIFY2(session.start(prepared, &error, WaveTheme::Nord), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 45000);
        QVERIFY2(finished.takeFirst()[0].toBool(), qPrintable(log));
        QCOMPARE(session.processId(), pid);
        QFile script(QDir(session.lastRunDirectory()).filePath("run.do"));
        QVERIFY(script.open(QIODevice::ReadOnly));
        const auto reuseScript = script.readAll();
        QVERIFY(reuseScript.contains("reusing compiled design"));
        QVERIFY(!reuseScript.contains("counter.sv"));
        QVERIFY(log.contains("reusing compiled design"));
        QVERIFY2(!log.contains("SimDock warning:"), qPrintable(log));
        QVERIFY(put(workspace.filePath("step.svh"), "localparam STEP=2;\n"));
        prepared = prepareRun(executable, workspace.path(), p);
        QVERIFY(prepared.designFingerprint != firstFingerprint);
        QVERIFY2(session.start(prepared, &error), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 45000);
        QVERIFY2(finished.takeFirst()[0].toBool(), qPrintable(log));
        QFile changed(QDir(session.lastRunDirectory()).filePath("run.do"));
        QVERIFY(changed.open(QIODevice::ReadOnly)); QVERIFY(changed.readAll().contains("counter.sv"));
        session.stop(); QTRY_VERIFY_WITH_TIMEOUT(!session.alive(), 5000);
    }
    void liveQuesta()
    {
        const QString executable = qEnvironmentVariable("SIMDOCK_TEST_QUESTA");
        if (executable.isEmpty()) QSKIP("Set SIMDOCK_TEST_QUESTA to run the actual Questa GUI integration test.");
        QTemporaryDir temp(QDir::tempPath() + QStringLiteral("/simdock 中文 [demo] $-XXXXXX"));
        QVERIFY(temp.isValid());
        QVERIFY(put(temp.filePath(QStringLiteral("counter.sv")), counter()));
        auto p = newProject(QStringLiteral("Questa smoke"));
        p.sources << QStringLiteral("counter.sv");
        p.tbFile = QStringLiteral("tb_counter.sv"); p.tbName = QStringLiteral("tb_counter");
        const auto module = analyzeSource(counter(), p.sources.first()).modules.first();
        QString error;
        QVERIFY(writeNewTb(temp.path(), p.tbFile, generateTestbench(module, suggestedTbOptions(module), p.durationNs, &error), &error));
        QuestaSession session;
        QSignalSpy finished(&session, &QuestaSession::finished);
        QSignalSpy states(&session, &QuestaSession::stateChanged);
        QString log;
        const auto artifact = qEnvironmentVariable("SIMDOCK_TEST_LOG");
        connect(&session, &QuestaSession::logText, this, [&log, artifact](const QString& text) {
            log += text;
            if (!artifact.isEmpty()) put(artifact, log.toUtf8());
        });
        QVERIFY2(session.start(executable, temp.path(), p, &error), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 90000);
        QVERIFY2(finished.takeFirst()[0].toBool(), qPrintable(log));
        QVERIFY(session.alive());
        const auto pid = session.processId();
        const auto firstRun = session.lastRunDirectory();
        p.durationNs = 2000;
        QVERIFY2(session.start(executable, temp.path(), p, &error), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 45000);
        QVERIFY2(finished.takeFirst()[0].toBool(), qPrintable(log));
        QCOMPARE(session.processId(), pid);
        QVERIFY(log.contains(QStringLiteral("Time: 2 us")));
        QVERIFY(QFileInfo(QDir(firstRun).filePath(QStringLiteral("result.wlf"))).size() > 0);
        QVERIFY(put(temp.filePath(QStringLiteral("counter.sv")), counter() + QByteArrayLiteral("\nnot valid HDL;\n")));
        QVERIFY(session.start(executable, temp.path(), p, &error));
        QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 45000);
        QVERIFY2(!finished.takeFirst()[0].toBool(), qPrintable(log));
        QVERIFY(session.alive());
        QVERIFY(put(temp.filePath(QStringLiteral("counter.sv")), counter()));
        p.tbFile = QStringLiteral("tb_long.sv");
        p.tbName = QStringLiteral("tb_long");
        QVERIFY(put(temp.filePath(p.tbFile), QByteArray(1, char(96)) + QByteArrayLiteral("timescale 1ns/1ps\nmodule tb_long; reg clk=0; always #5 clk=~clk; endmodule")));
        p.durationNs = 3600000000000LL;
        session.setWallTimeout(30000);
        states.clear();
        QVERIFY(session.start(executable, temp.path(), p, &error));
        const auto reachedRunning = [&states] {
            for (const auto& state : states) if (state[0].toString() == QStringLiteral("Running")) return true;
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(reachedRunning(), 15000);
        QVERIFY(session.busy());
        session.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!session.alive(), 5000);
        QVERIFY(!session.busy());
        session.setWallTimeout(1);
        finished.clear();
        QVERIFY(session.start(executable, temp.path(), p, &error));
        QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 5000);
        QVERIFY(!finished.first()[0].toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!session.alive(), 5000);
        if (!artifact.isEmpty()) QVERIFY(put(artifact, log.toUtf8()));
    }
};
QTEST_GUILESS_MAIN(CoreTest)
#include "core_test.moc"
