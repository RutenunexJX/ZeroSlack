#include "../../src/simulation/simdock/core/analyzer.h"
#include "../../src/simulation/simdock/core/scoreboard.h"
#include "../../src/simulation/simdock/core/questasession.h"
#include "../../src/simulation/simdock/core/testbench.h"
#include "../../src/simulation/simdock/core/workspace.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>
using namespace simdock;
namespace {
bool put(const QString &path, const QByteArray &bytes) {
    QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QJsonObject plan(const QString &kind = "uart_tx") {
    auto p = defaultScoreboard(); p["kind"] = kind;
    p["data"] = "data"; p["valid"] = "valid"; p["ready"] = "ready";
    p["output"] = kind == "uart_tx" ? "tx" : "out_data";
    p["outputValid"] = "out_valid"; p["outputReady"] = "out_ready";
    p["baud"] = 10000000; p["timeoutNs"] = 3000;
    return p;
}
QByteArray ports() {
    return "module device(input logic clk, rst, valid, out_ready, input logic [8:0] data, "
           "output logic ready, tx, out_valid, output logic [8:0] out_data); endmodule";
}
QJsonObject withSegments(QJsonObject drawing, const QString &id, const QList<QPair<qint64, QString>> &points,
                         qint64 durationNs = 4000) {
    auto wave = drawing["wave"].toObject(); auto scenario = wave["scenarios"].toArray().first().toObject();
    auto lanes = scenario["lanes"].toArray();
    for (int row = 0; row < lanes.size(); ++row) {
        auto lane = lanes[row].toObject(); if (lane["id"].toString() != id) continue;
        QJsonArray segments;
        for (int i = 0; i < points.size(); ++i) {
            const auto end = i + 1 < points.size() ? points[i + 1].first : durationNs;
            segments << QJsonObject{{"id", QString::number(i)}, {"startTick", QString::number(points[i].first * 1000)},
                                    {"endTick", QString::number(end * 1000)}, {"value", points[i].second}};
        }
        lane["segments"] = segments; lanes[row] = lane;
    }
    scenario["lanes"] = lanes; wave["scenarios"] = QJsonArray{scenario}; drawing["wave"] = wave; return drawing;
}
}
class ScoreboardTest : public QObject {
    Q_OBJECT
private slots:
    void semanticBindingsAndValidation() {
        Scan scan; scan.files << analyzeSource(ports(), "device.sv"); const auto m = scan.files.first().modules.first();
        QString error; const auto resolved = scoreboardSignals(m, scan, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error)); QCOMPARE(resolved.size(), 9);
        const auto timing = suggestedTbOptions(m);
        QVERIFY(!scoreboardCode(resolved, timing, plan(), &error).isEmpty());
        auto bad = plan(); bad["output"] = "data";
        QVERIFY(scoreboardCode(resolved, timing, bad, &error).isEmpty()); QVERIFY(!error.isEmpty());
        bad = plan(); bad["valid"] = "clk";
        QVERIFY(scoreboardCode(resolved, timing, bad, &error).isEmpty());
        bad = plan(); bad["kind"] = "arbitrary_hdl";
        QVERIFY(scoreboardCode(resolved, timing, bad, &error).isEmpty());
        bad = plan(); for (const auto &key : {"checkData", "checkCount", "checkFormat", "checkTimeout"}) bad[key] = false;
        QVERIFY(scoreboardCode(resolved, timing, bad, &error).isEmpty());
        bad = plan("values"); bad["values"] = QJsonArray{"1ff", "00"};
        QVERIFY2(!scoreboardCode(resolved, timing, bad, &error).isEmpty(), qPrintable(error));
        for (const auto &value : {"200", "-1", "X", "0); $finish;"}) {
            bad["values"] = QJsonArray{QString::fromLatin1(value)};
            QVERIFY(scoreboardCode(resolved, timing, bad, &error).isEmpty());
        }
        QVERIFY(scoreboardCode(resolved, timing, defaultScoreboard(), &error).isEmpty()); QVERIFY(error.isEmpty());
        const QByteArray typedefRtl = "package p; typedef logic [63:0] word_t; endpackage "
            "module typed(input logic clk, rst, valid, input p::word_t data, output logic ready, out_valid, "
            "output p::word_t out_data); endmodule";
        Scan typed; typed.files << analyzeSource(typedefRtl, "typed.sv");
        const auto tp = scoreboardSignals(typed.files.first().modules.first(), typed, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        auto expected = plan("values"); expected["outputReady"] = ""; expected["values"] = QJsonArray{"ffffffffffffffff"};
        QVERIFY2(!scoreboardCode(tp, suggestedTbOptions(typed.files.first().modules.first()), expected, &error).isEmpty(), qPrintable(error));
    }
    void persistenceAndScript() {
        QTemporaryDir work; QVERIFY(put(work.filePath("device.sv"), ports()));
        const auto scan = scanWorkspace(work.path()); const auto module = scan.files.first().modules.first();
        QString error; auto drawing = newStimulus(module, scan, suggestedTbOptions(module), 4000, &error);
        drawing["scoreboard"] = plan();
        auto project = newProject("Checked drawing"); project.sources = {"device.sv"};
        project.dutFile = "device.sv"; project.dutName = "device";
        QVERIFY2(saveStimulus(work.path(), project, module, scan, drawing, &error), qPrintable(error));
        QCOMPARE(loadProjects(work.path()).first().stimulus["scoreboard"], drawing["scoreboard"]);
        const auto retimed = retimeStimulus(module, scan, drawing, suggestedTbOptions(module), 5000, &error);
        QCOMPARE(retimed["scoreboard"], drawing["scoreboard"]);
        const auto code = stimulusTestbench(module, scan, retimed, &error);
        QVERIFY2(!code.isEmpty(), qPrintable(error)); QVERIFY(code.contains("__simdock_scoreboard.finish_check();"));
        QVERIFY(!code.contains("No functional assertions")); QVERIFY(!code.contains("@WIDTH@"));
        const auto script = runScript(work.path(), project, work.filePath("run"), work.filePath("modelsim.ini"));
        QVERIFY(script.contains("Scoreboard did not finish")); QVERIFY(script.contains("Scoreboard checks failed"));
        project.stimulus["scoreboard"] = defaultScoreboard();
        QVERIFY(!runScript(work.path(), project, "run", "modelsim.ini").contains("examine -radix"));
    }
    void liveScoreboards_data() {
        QTest::addColumn<QString>("kind"); QTest::addColumn<int>("fault");
        QTest::addColumn<int>("bits"); QTest::addColumn<QString>("parity");
        QTest::addColumn<int>("stop"); QTest::addColumn<bool>("keep"); QTest::addColumn<bool>("pass");
        QTest::newRow("uart_8n1") << "uart_tx" << 0 << 8 << "none" << 2 << false << true;
        QTest::newRow("uart_9odd2") << "uart_tx" << 0 << 9 << "odd" << 4 << false << true;
        QTest::newRow("uart_7even1_5") << "uart_tx" << 0 << 7 << "even" << 3 << false << true;
        QTest::newRow("uart_wrong_data") << "uart_tx" << 1 << 8 << "none" << 2 << false << false;
        QTest::newRow("uart_wrong_parity") << "uart_tx" << 2 << 8 << "even" << 2 << false << false;
        QTest::newRow("uart_bad_stop") << "uart_tx" << 3 << 8 << "none" << 2 << false << false;
        QTest::newRow("uart_extra_frame") << "uart_tx" << 4 << 8 << "none" << 2 << false << false;
        QTest::newRow("uart_missing_frame") << "uart_tx" << 5 << 8 << "none" << 2 << false << false;
        QTest::newRow("uart_timeout") << "uart_tx" << 6 << 8 << "none" << 2 << false << false;
        QTest::newRow("uart_empty") << "uart_tx" << 7 << 8 << "none" << 2 << false << false;
        QTest::newRow("uart_reset_discard") << "uart_tx" << 8 << 8 << "none" << 2 << false << true;
        QTest::newRow("uart_reset_keep") << "uart_tx" << 8 << 8 << "none" << 2 << true << false;
        QTest::newRow("stream_passthrough") << "stream" << 0 << 8 << "none" << 2 << false << true;
        QTest::newRow("stream_bad_data") << "stream" << 1 << 8 << "none" << 2 << false << false;
        QTest::newRow("expected_values") << "values" << 0 << 8 << "none" << 2 << false << true;
        QTest::newRow("expected_bad_values") << "values" << 1 << 8 << "none" << 2 << false << false;
    }
    void liveScoreboards() {
        const auto executable = qEnvironmentVariable("SIMDOCK_TEST_QUESTA");
        if (executable.isEmpty()) QSKIP("Set SIMDOCK_TEST_QUESTA for executable scoreboard verification.");
        QFETCH(QString, kind); QFETCH(int, fault); QFETCH(int, bits); QFETCH(QString, parity);
        QFETCH(int, stop); QFETCH(bool, keep); QFETCH(bool, pass);
        QTemporaryDir work; QVERIFY(work.isValid());
        QString dut = QString::fromLatin1(R"sv(
`timescale 1ns/1ps
module device(input logic clk, rst, valid, out_ready, input logic [8:0] data,
              output logic ready, tx, out_valid, output logic [8:0] out_data);
localparam integer BITS=@BITS@, PARITY=@PARITY@, STOP=@STOP@, FAULT=@FAULT@;
task automatic transmit(input logic [8:0] payload);
    logic parity_value;
    if (FAULT == 1) payload = payload ^ 9'h001;
    parity_value = ^payload[BITS-1:0];
    if (PARITY == 2) parity_value = ~parity_value;
    if (FAULT == 2) parity_value = ~parity_value;
    tx = 0; #100;
    for (int n=0; n<BITS; n++) begin tx = payload[n]; #100; end
    if (PARITY != 0) begin tx = parity_value; #100; end
    tx = (FAULT == 3) ? 0 : 1; #(STOP * 50); tx = 1;
endtask
initial begin
    tx = 1; ready = 0; out_valid = 0; out_data = 0;
    forever begin
        wait (rst === 0); ready = 1;
        @(posedge clk);
        if (valid === 1 && rst === 0) begin
            ready <= 0;
            fork : activity
                begin
                    #1;
                    if (FAULT != 5) begin transmit(data); if (FAULT == 4) transmit(data); end
                end
                begin wait (rst === 1); tx = 1; end
            join_any
            disable activity;
        end
    end
end
endmodule
)sv");
        if (kind != "uart_tx") dut = QString::fromLatin1(R"sv(
`timescale 1ns/1ps
module device(input logic clk, rst, valid, out_ready, input logic [8:0] data,
              output logic ready, tx, out_valid, output logic [8:0] out_data);
assign tx=1;
assign ready=out_ready;
assign out_valid=valid && !rst;
assign out_data=data ^ @FAULT@;
endmodule
)sv");
        dut.replace("@BITS@", QString::number(bits)).replace("@PARITY@", parity == "none" ? "0" : parity == "even" ? "1" : "2")
            .replace("@STOP@", QString::number(stop)).replace("@FAULT@", QString::number(kind == "uart_tx" ? fault : fault == 1 ? 1 : 0));
        QVERIFY(put(work.filePath("device.sv"), dut.toUtf8()));
        const auto scan = scanWorkspace(work.path()); const auto module = scan.files.first().modules.first();
        QString error; auto drawing = newStimulus(module, scan, suggestedTbOptions(module), 4000, &error);
        QVERIFY2(!drawing.isEmpty(), qPrintable(error));
        drawing = withSegments(drawing, "data", {{0,"0"}, {60,"0x1a5"}, {1500,"0x13c"}});
        drawing = withSegments(drawing, "valid", fault == 7 ? QList<QPair<qint64, QString>>{{0,"0"}}
            : kind == "uart_tx" ? QList<QPair<qint64, QString>>{{0,"0"},{60,"1"},{80,"0"},{1500,"1"},{1520,"0"}}
            : QList<QPair<qint64, QString>>{{0,"0"},{60,"1"},{70,"0"},{1500,"1"},{1510,"0"}});
        drawing = withSegments(drawing, "out_ready", {{0,"1"}});
        if (fault == 8) drawing = withSegments(drawing, "rst", {{0,"1"},{50,"0"},{250,"1"},{280,"0"}});
        auto checks = plan(kind); checks["dataBits"] = bits; checks["parity"] = parity; checks["stopHalfBits"] = stop;
        checks["resetPolicy"] = keep ? "keep" : "discard";
        if (fault == 6) checks["timeoutNs"] = 100;
        if (kind == "values") checks["values"] = QJsonArray{"1a5", "13c"};
        drawing["scoreboard"] = checks;
        const auto tb = stimulusTestbench(module, scan, drawing, &error);
        QVERIFY2(!tb.isEmpty(), qPrintable(error)); QVERIFY(put(work.filePath("tb.sv"), tb.toUtf8()));
        const auto tools = QFileInfo(executable).dir();
        const auto execute = [&](const QString &tool, const QStringList &args) {
            QProcess process; process.setWorkingDirectory(work.path()); process.setProcessChannelMode(QProcess::MergedChannels);
            process.start(tools.filePath(tool), args); bool ended = process.waitForFinished(45000);
            if (!ended) { process.kill(); process.waitForFinished(3000); }
            error = QString::fromLocal8Bit(process.readAll());
            return ended && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
        };
        QVERIFY2(execute("vlib", {"work"}), qPrintable(error));
        QVERIFY2(execute("vlog", {"-sv", "device.sv", "tb.sv"}), qPrintable(error));
        const auto checkResult = QStringLiteral("set failed [catch {%1} detail]; puts [list SIMDOCK_TCL_RESULT $failed $detail]; if {$failed != %2} {quit -code 3}; quit -code 0")
            .arg(scoreboardResultScript("tb_device")).arg(pass ? 0 : 1);
        QVERIFY(put(work.filePath("check.do"), ("onerror {quit -code 1}\nonbreak {resume}\nrun -all\n" + checkResult).toUtf8()));
        const auto simulated = execute("vsim", {"-c", "-onfinish", "stop", "-voptargs=+acc", "work.tb_device", "-do",
            "do check.do"});
        const auto report = qEnvironmentVariable("SIMDOCK_SCOREBOARD_REPORT");
        if (!report.isEmpty()) {
            QDir().mkpath(report); const auto name = QString::fromLatin1(QTest::currentDataTag());
            put(QDir(report).filePath(name + ".log"), error.toUtf8());
            put(QDir(report).filePath(name + "-tb.sv"), tb.toUtf8());
        }
        QVERIFY2(simulated, qPrintable(error));
        QVERIFY2(error.contains(pass ? "SIMDOCK_TCL_RESULT 0" : "SIMDOCK_TCL_RESULT 1 {Scoreboard checks failed."), qPrintable(error));
        QVERIFY2(error.contains(pass ? "SIMDOCK_CHECK_PASS " : "SIMDOCK_CHECK_FAILED "), qPrintable(error));
        QVERIFY2(!error.contains(pass ? "SIMDOCK_CHECK_FAIL " : "SIMDOCK_CHECK_PASS "), qPrintable(error));
        if (fault == 8 && !keep) QVERIFY(error.contains("reset_discarded=1"));
        if (fault == 7) QVERIFY(error.contains("No transactions were compared"));
        if (fault == 6) QVERIFY(error.contains("timed out"));
    }
};
QTEST_GUILESS_MAIN(ScoreboardTest)
#include "scoreboard_test.moc"
