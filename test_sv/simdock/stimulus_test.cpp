#include "../../src/simulation/simdock/core/analyzer.h"
#include "../../src/simulation/simdock/core/stimulus.h"
#include "../../src/simulation/simdock/core/preparation.h"
#include "../../src/simulation/simdock/core/testbench.h"
#include "../../src/simulation/simdock/core/workspace.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>
using namespace simdock;
namespace
{
bool put(const QString &path, const QByteArray &data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}
const QByteArray package =
    "package types; localparam N=4; typedef logic [N*2-1:0] word_t; typedef word_t alias_t; "
    "typedef enum logic [1:0] { IDLE, ACTIVE, DONE } mode_e; "
    "typedef struct packed { logic valid; logic [6:0] data; } payload_t; endpackage";
const QByteArray dut =
    "module graphic_dut import types::*; #(parameter W=3)"
    "(input logic clk, rst, enable, input mode_e mode, input logic [7:0] data, input logic [W-1:0] a, "
    "input alias_t b, input payload_t payload); endmodule";
QJsonObject interval(const char *id, qint64 begin, qint64 end, const QString &value)
{
    return {{"id", QString::fromLatin1(id)},
            {"startTick", QString::number(begin)},
            {"endTick", QString::number(end)},
            {"value", value}};
}
QJsonObject laneSegments(QJsonObject drawing, const QString &name, const QJsonArray &segments)
{
    auto wave = drawing["wave"].toObject();
    auto scenario = wave["scenarios"].toArray().first().toObject();
    auto lanes = scenario["lanes"].toArray();
    for (int i = 0; i < lanes.size(); ++i)
    {
        auto lane = lanes[i].toObject();
        if (lane["id"].toString() == name)
        {
            lane["segments"] = segments;
            lanes[i] = lane;
        }
    }
    scenario["lanes"] = lanes;
    wave["scenarios"] = QJsonArray{scenario};
    drawing["wave"] = wave;
    return drawing;
}
Scan sources()
{
    Scan scan;
    scan.files << analyzeSource(package, "types.sv") << analyzeSource(dut, "dut.sv");
    return scan;
}
} // namespace
class StimulusTest : public QObject
{
    Q_OBJECT
  private slots:
    void semanticCancellationAndRecovery()
    {
        QByteArray code("module many(input logic a);\n");
        for (int i = 0; i < 2000; ++i) code += "logic signal_" + QByteArray::number(i) + ";\n";
        code += "endmodule\n";
        Scan scan; scan.files << analyzeSource(code, "many.sv");
        int checkpoints = 0;
        const auto cancelled = resolveStimulus(scan.files.first().modules.first(), scan, [&] { return ++checkpoints > 2; });
        QVERIFY(checkpoints > 2); QVERIFY(cancelled.error.contains("cancelled"));
        const auto next = resolveStimulus(scan.files.first().modules.first(), scan);
        QVERIFY2(next.error.isEmpty(), qPrintable(next.error)); QCOMPARE(next.inputs.size(), 1);
    }
    void preparationReuseAndHeaderInvalidation()
    {
        QTemporaryDir workspace;
        QVERIFY(QDir(workspace.path()).mkpath("rtl"));
        QVERIFY(put(workspace.filePath("width.svh"), "package widths; typedef logic [7:0] word_t; endpackage\n"));
        QVERIFY(put(workspace.filePath("rtl/dut.sv"),
            "`include \"width.svh\"\nmodule dut import widths::*; (input word_t value, output word_t result); assign result=value; endmodule"));
        auto p = newProject("Reuse");
        p.sources = {"rtl/dut.sv"}; p.dutFile = "rtl/dut.sv"; p.dutName = "dut";
        // Include search uses all selected source directories, as the simulator does.
        QVERIFY(put(workspace.filePath("base.sv"), "module base; endmodule"));
        p.sources.prepend("base.sv");
        SourceCache cache;
        QString error;
        auto a = prepareStimulus(workspace.path(), p, {}, &cache, nullptr, &error);
        QVERIFY2(a, qPrintable(error));
        QCOMPARE(a->semantics.inputs.first().width, 8);
        QCOMPARE(a->semantics.ports.size(), 2);
        auto b = prepareStimulus(workspace.path(), p, a, &cache, nullptr, &error);
        QCOMPARE(b, a);
        // A formerly missing local include now shadows the root include.
        QVERIFY(put(workspace.filePath("rtl/width.svh"), "package widths; typedef logic [3:0] word_t; endpackage\n"));
        auto c = prepareStimulus(workspace.path(), p, b, &cache, nullptr, &error);
        QVERIFY2(c, qPrintable(error)); QVERIFY(c != b);
        QCOMPARE(c->semantics.inputs.first().width, 4);
        QFile header(workspace.filePath("rtl/width.svh"));
        const auto timestamp = QFileInfo(header).lastModified();
        QVERIFY(put(header.fileName(), "package widths; typedef logic [5:0] word_t; endpackage\n"));
        QVERIFY(header.open(QIODevice::ReadWrite));
        QVERIFY(header.setFileTime(timestamp, QFileDevice::FileModificationTime)); header.close();
        auto d = prepareStimulus(workspace.path(), p, c, &cache, nullptr, &error);
        QVERIFY2(d, qPrintable(error)); QVERIFY(d != c);
        QCOMPARE(d->semantics.inputs.first().width, 6);
        std::atomic_bool cancelled{true};
        QVERIFY(!prepareStimulus(workspace.path(), p, d, &cache, &cancelled, &error));
        QVERIFY(error.contains("cancelled"));
        QVERIFY(QFile::remove(header.fileName()));
        auto e = prepareStimulus(workspace.path(), p, d, &cache, nullptr, &error);
        QVERIFY2(e, qPrintable(error)); QCOMPARE(e->semantics.inputs.first().width, 8);
        p.sources.move(0, 1);
        QVERIFY(prepareStimulus(workspace.path(), p, e, &cache, nullptr, &error) != e);
    }
    void adjacentTypedefNames()
    {
        const auto bytes = QByteArray(
            "package adjacent_types;\n"
            "typedef enum logic [1:0]{\n IDLE, ACTIVE, DONE\n}mode_e;\n"
            "typedef struct packed{logic flag;logic [6:0] value;}payload_t;\n"
            "typedef enum logic [1:0]{OFF=0, ON=1}/* comment */switch_e;\n"
            "typedef mode_e alias_e;\n"
            "endpackage\n"
            "module adjacent_dut import adjacent_types::*; (input mode_e mode, input payload_t payload, "
            "input switch_e setting, input alias_e copied); endmodule");
        const auto file = analyzeSource(bytes, "adjacent.sv");
        QVERIFY2(!file.syntaxError, qPrintable(syntaxTree(bytes)));
        const auto module = file.modules.first();
        Scan scan;
        scan.files << file;
        QString error;
        const auto inputs = stimulusSignals(module, scan, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(inputs.size(), 4);
        QCOMPARE(inputs[0].enumValues.value("ACTIVE"), QString("1"));
        QCOMPARE(inputs[1].width, 8);
        QCOMPARE(inputs[2].enumValues.value("ON"), QString("1"));
        QCOMPARE(inputs[3].enumValues, inputs[0].enumValues);
        const auto drawing = newStimulus(module, scan, suggestedTbOptions(module), 100, &error);
        QVERIFY2(!drawing.isEmpty(), qPrintable(error));
        QVERIFY2(!stimulusTestbench(module, scan, drawing, &error).isEmpty(), qPrintable(error));
        const auto array =
            analyzeSource("package arrays; typedef logic [7:0] unpacked_t [4]; endpackage "
                          "module array_dut import arrays::*; (input unpacked_t values); endmodule",
                          "array.sv");
        Scan arrayScan;
        arrayScan.files << array;
        QVERIFY(stimulusSignals(array.modules.first(), arrayScan, &error).isEmpty());
        QVERIFY(error.contains("packed integral"));
    }
    void typesAndPersistence()
    {
        const auto scan = sources();
        const auto module = scan.files.last().modules.first();
        QString error;
        const auto inputs = stimulusSignals(module, scan, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error + "\n" + syntaxTree(package)));
        QCOMPARE(inputs.size(), 8);
        QCOMPARE(inputs[3].enumValues.value("ACTIVE"), QString("1"));
        QCOMPARE(inputs[5].width, 3);
        QCOMPARE(inputs[6].width, 8);
        QCOMPARE(inputs[7].width, 8);
        auto drawing = newStimulus(module, scan, suggestedTbOptions(module), 100, &error);
        QVERIFY2(!drawing.isEmpty(), qPrintable(error));
        drawing = laneSegments(drawing, "mode",
                               {interval("a", 0, 25000, "IDLE"), interval("b", 25000, 100000, "ACTIVE")});
        QTemporaryDir workspace;
        QVERIFY(put(workspace.filePath("types.sv"), package));
        QVERIFY(put(workspace.filePath("dut.sv"), dut));
        auto project = newProject("Drawn inputs");
        project.sources = {"types.sv", "dut.sv"};
        project.dutFile = module.file;
        project.dutName = module.name;
        QVERIFY(put(workspace.filePath("manual.sv"), "// user TB"));
        project.tbFile = "manual.sv";
        QVERIFY2(saveStimulus(workspace.path(), project, module, scan, drawing, &error), qPrintable(error));
        const auto saved = project.tbFile;
        auto loaded = loadProjects(workspace.path());
        QCOMPARE(loaded.size(), 1);
        QCOMPARE(loaded.first().stimulus, drawing);
        QCOMPARE(loaded.first().tbFile, saved);
        QFile tb(workspace.filePath(saved));
        QVERIFY(tb.open(QIODevice::ReadOnly));
        const auto content = tb.readAll();
        tb.close();
        QVERIFY(content.contains("mode = mode_e'(2'h1)"));
        QVERIFY(content.contains("#(25000 * 1ps)"));
        QVERIFY2(saveStimulus(workspace.path(), project, module, scan, drawing, &error), qPrintable(error));
        QCOMPARE(project.tbFile, saved);
        QFile manual(workspace.filePath("manual.sv"));
        QVERIFY(manual.open(QIODevice::ReadOnly));
        QCOMPARE(manual.readAll(), QByteArray("// user TB"));
        QVERIFY(put(workspace.filePath(saved), "// edited by user"));
        QVERIFY(!saveStimulus(workspace.path(), project, module, scan, drawing, &error));
        QVERIFY(error.contains("preserved"));
    }
    void validationAndImplicitValues()
    {
        const auto scan = sources();
        const auto module = scan.files.last().modules.first();
        QString error;
        auto drawing = newStimulus(module, scan, suggestedTbOptions(module), 100, &error);
        QVERIFY2(!drawing.isEmpty(), qPrintable(error));
        for (const auto &value : {QString("0x8"), QString("0; $finish;")})
        {
            auto invalid = laneSegments(drawing, "a", {interval("bad", 0, 100000, value)});
            QVERIFY(stimulusTestbench(module, scan, invalid, &error).isEmpty());
        }
        auto overlap =
            laneSegments(drawing, "a", {interval("a", 0, 60000, "0"), interval("b", 50000, 100000, "1")});
        QVERIFY(stimulusTestbench(module, scan, overlap, &error).isEmpty());
        auto invalidEnum = laneSegments(drawing, "mode", {interval("bad", 0, 100000, "BAD_ENUM")});
        QVERIFY(stimulusTestbench(module, scan, invalidEnum, &error).isEmpty());
        auto changed = module;
        changed.parameters.first().value = "4";
        QVERIFY(stimulusTestbench(changed, scan, drawing, &error).isEmpty());
        QVERIFY(error.contains("changed"));
        auto holes = laneSegments(drawing, "b", {interval("value", 20000, 40000, "10")});
        auto tb = stimulusTestbench(module, scan, holes, &error);
        QVERIFY2(!tb.isEmpty(), qPrintable(error));
        QVERIFY(tb.contains("alias_t'(8'h10)"));
        QVERIFY(tb.contains("alias_t'(8'bxxxxxxxx)"));
        holes = laneSegments(drawing, "data", {interval("value", 0, 100000, "0bx10z")});
        tb = stimulusTestbench(module, scan, holes, &error);
        QVERIFY2(!tb.isEmpty(), qPrintable(error));
        QVERIFY(tb.contains("8'b0000x10z"));
        const auto wide = analyzeSource("module wide(input logic [64:0] data); endmodule", "wide.sv");
        Scan wideScan;
        wideScan.files << wide;
        const auto wideModule = wide.modules.first();
        QVERIFY(newStimulus(wideModule, wideScan, suggestedTbOptions(wideModule), 100, &error).isEmpty());
        QVERIFY(error.contains("64 bits"));
    }
    void signedTypesMatchWaveValues()
    {
        const auto bytes = QByteArray(
            "package signed_types; typedef enum logic signed [7:0] { NEG=8'shff, ZERO=0 } e_t; endpackage "
            "module signed_dut import signed_types::*; (input logic signed [7:0] data, input e_t mode); "
            "endmodule");
        const auto file = analyzeSource(bytes, "signed.sv");
        Scan scan;
        scan.files << file;
        const auto module = file.modules.first();
        QString error;
        const auto inputs = stimulusSignals(module, scan, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(inputs[1].enumValues.value("NEG"), QString("-1"));
        auto drawing = newStimulus(module, scan, suggestedTbOptions(module), 100, &error);
        drawing = laneSegments(drawing, "data", {interval("value", 0, 100000, "0xf")});
        auto tb = stimulusTestbench(module, scan, drawing, &error);
        QVERIFY2(!tb.isEmpty(), qPrintable(error));
        QVERIFY(tb.contains("data = 8'hff"));
        drawing = laneSegments(drawing, "data", {interval("value", 0, 100000, "0bx10z")});
        tb = stimulusTestbench(module, scan, drawing, &error);
        QVERIFY2(!tb.isEmpty(), qPrintable(error));
        QVERIFY(tb.contains("data = 8'bxxxxx10z"));
    }
    void semanticExpressionsAndPackages()
    {
        QTemporaryDir workspace;
        QVERIFY(put(workspace.filePath("width.svh"), "`define INPUT_COUNT 5\n"));
        const QByteArray definitions =
            "`include \"width.svh\"\n"
            "package calc; localparam N=(2**3==8) ? `INPUT_COUNT : 1; "
            "function automatic int width(input int n); return $clog2(2**n); endfunction "
            "typedef logic signed [width(N)-1:0] word_t; "
            "typedef enum logic [3:0] { FIRST=4'(N-1), SECOND=FIRST+2 }mode_e; endpackage\n"
            "package aliases; import calc::*; typedef word_t alias_t; typedef mode_e state_t; endpackage\n";
        const QByteArray design =
            "`timescale 1ns/1ps\nmodule computed import aliases::*; #(parameter W=(1 ? 7 : 2))"
            "(input alias_t data, input state_t state, input logic [W-1:0] param_data); endmodule";
        Scan scan;
        scan.root = workspace.path();
        scan.files << analyzeSource(definitions, "types.sv") << analyzeSource(design, "computed.sv");
        const auto module = scan.files.last().modules.first();
        QString error;
        const auto ports = stimulusSignals(module, scan, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(ports.size(), 3);
        QCOMPARE(ports[0].width, 5);
        QVERIFY(ports[0].isSigned);
        QCOMPARE(ports[1].enumValues.value("FIRST"), QString("4"));
        QCOMPARE(ports[1].enumValues.value("SECOND"), QString("6"));
        QCOMPARE(ports[2].width, 7);
        // A package edit must invalidate existing lane widths, even when the
        // DUT header is unchanged. No stale or guessed semantic fallback.
        const auto drawing = newStimulus(module, scan, suggestedTbOptions(module), 100, &error);
        QVERIFY2(!drawing.isEmpty(), qPrintable(error));
        QVERIFY(put(workspace.filePath("width.svh"), "`define INPUT_COUNT 6\n"));
        QVERIFY(stimulusTestbench(module, scan, drawing, &error).isEmpty());
        QVERIFY(error.contains("changed"));
    }
    void semanticErrorsAreReported()
    {
        const auto source =
            analyzeSource("module bad(input missing_pkg::data_t data); endmodule", "broken.sv");
        Scan scan;
        scan.files << source;
        QString error;
        QVERIFY(stimulusSignals(source.modules.first(), scan, &error).isEmpty());
        QVERIFY2(error.contains("SystemVerilog semantic analysis failed"), qPrintable(error));
        QVERIFY2(error.contains("broken.sv:1:") && error.contains("missing_pkg"), qPrintable(error));
    }
    void retime()
    {
        const auto scan = sources();
        const auto m = scan.files.last().modules.first();
        QString error;
        auto options = suggestedTbOptions(m);
        auto drawing = newStimulus(m, scan, options, 100, &error);
        drawing = laneSegments(drawing, "enable",
                               {interval("a", 0, 25000, "0"), interval("b", 25000, 100000, "1")});
        options.clockPeriodNs = 20;
        auto longer = retimeStimulus(m, scan, drawing, options, 150, &error);
        auto tb = stimulusTestbench(m, scan, longer, &error);
        QVERIFY2(!tb.isEmpty(), qPrintable(error));
        QVERIFY(tb.contains("#(25000 * 1ps)"));
        QVERIFY(tb.contains("always #(10000 * 1ps)"));
        auto shorter = retimeStimulus(m, scan, longer, options, 20, &error);
        tb = stimulusTestbench(m, scan, shorter, &error);
        QVERIFY2(!tb.isEmpty(), qPrintable(error));
        QVERIFY(!tb.contains("#(25000 * 1ps)"));
    }
    void liveQuesta()
    {
        const auto executable = qEnvironmentVariable("SIMDOCK_TEST_QUESTA");
        if (executable.isEmpty())
            QSKIP("Set SIMDOCK_TEST_QUESTA for live generated-stimulus verification.");
        QTemporaryDir workspace;
        QVERIFY(put(workspace.filePath("types.sv"), package));
        auto checkedDut = dut;
        checkedDut.replace("endmodule", "initial begin #1; if(clk!==0 || rst!==1 || enable!==0 || "
                                        "mode!==IDLE || data!==8'h10) $fatal(1,\"Initial values differ\"); "
                                        "#5; if(clk!==1) $fatal(1,\"Clock differs\"); #20; if(enable!==1 || "
                                        "mode!==ACTIVE) $fatal(1,\"Transitions differ\"); "
                                        "#25; if(rst!==0 || data!==8'ha5) $fatal(1,\"Reset/data differ\"); "
                                        "$display(\"GRAPHICAL_STIMULUS_PASS\"); end endmodule");
        checkedDut.prepend("`timescale 1ns/1ps\n");
        QVERIFY(put(workspace.filePath("dut.sv"), checkedDut));
        const auto scan = scanWorkspace(workspace.path());
        const auto m = analyzeSource(checkedDut, "dut.sv").modules.first();
        QString error;
        auto drawing = newStimulus(m, scan, suggestedTbOptions(m), 100, &error);
        QVERIFY2(!drawing.isEmpty(), qPrintable(error));
        drawing = laneSegments(drawing, "enable",
                               {interval("a", 0, 25000, "0"), interval("b", 25000, 100000, "1")});
        drawing = laneSegments(drawing, "mode",
                               {interval("a", 0, 25000, "IDLE"), interval("b", 25000, 100000, "ACTIVE")});
        drawing = laneSegments(drawing, "data",
                               {interval("a", 0, 50000, "10"), interval("b", 50000, 100000, "0xa5")});
        const auto tb = stimulusTestbench(m, scan, drawing, &error);
        QVERIFY2(!tb.isEmpty(), qPrintable(error));
        QVERIFY(put(workspace.filePath("tb.sv"), tb.toUtf8()));
        const QDir tools = QFileInfo(executable).dir();
        const auto execute = [&](const QString &tool, const QStringList &args)
        {
            QProcess p;
            p.setWorkingDirectory(workspace.path());
            p.setProcessChannelMode(QProcess::MergedChannels);
            p.start(tools.filePath(tool), args);
            const bool done = p.waitForFinished(60000);
            if (!done)
            {
                p.kill();
                p.waitForFinished(5000);
            }
            error = QString::fromLocal8Bit(p.readAll());
            return done && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
        };
        QVERIFY2(execute("vlib", {"work"}), qPrintable(error));
        QVERIFY2(execute("vlog", {"-sv", "-work", "work", "types.sv", "dut.sv", "tb.sv"}), qPrintable(error));
        QVERIFY2(execute("vsim", {"-c", "-onfinish", "exit", "work.tb_graphic_dut", "-do",
                                  "onerror {quit -code 1}; run -all; quit -code 0"}),
                 qPrintable(error));
        QVERIFY2(error.contains("GRAPHICAL_STIMULUS_PASS"), qPrintable(error));
    }
};
QTEST_GUILESS_MAIN(StimulusTest)
#include "stimulus_test.moc"
