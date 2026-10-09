#include "../../src/simulation/simdock/core/workspace.h"
#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTextStream>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir fixture;
    if (!fixture.isValid()) return 1;
    for (int i = 0; i < 240; ++i) {
        QFile file(QDir(fixture.path()).filePath(QStringLiteral("source_%1.sv").arg(i)));
        if (!file.open(QIODevice::WriteOnly)) return 2;
        QByteArray text = "module source_" + QByteArray::number(i) + "(input logic clk, input logic [31:0] data, output logic [31:0] result);\n";
        for (int n = 0; n < 256; ++n)
            text += "wire [31:0] stage_" + QByteArray::number(n) + " = data ^ 32'd" + QByteArray::number(n) + ";\n";
        text += "always_ff @(posedge clk) result <= stage_255;\nendmodule\n";
        if (file.write(text) != text.size()) return 3;
    }
    QJsonArray samples;
    simdock::SourceCache cache;
    for (int i = 0; i < 4; ++i) {
        simdock::ScanMetrics metrics;
        const auto scan = simdock::scanWorkspace(fixture.path(), nullptr, &metrics, &cache);
        if (scan.files.size() != 240 || !scan.messages.isEmpty()) return 4;
        samples.append(QJsonObject{{"iteration", i}, {"files", scan.files.size()},
            {"readFiles", metrics.readFiles}, {"parsedFiles", metrics.parsedFiles},
            {"reusedFiles", metrics.reusedFiles}, {"readMs", metrics.readNs / 1e6},
            {"analysisMs", metrics.analysisNs / 1e6}, {"totalMs", metrics.totalNs / 1e6}});
    }
    QTextStream(stdout) << QJsonDocument(QJsonObject{{"fixture", "240 independent SV modules, 256 assignments each"},
        {"timingConditions", "Other application builds may run concurrently; wall times are noisy."},
        {"samples", samples}}).toJson();
}
