#pragma once
#include "signaltracepanel.h"
#include <zeroslack/semantic/incrementalsemanticanalysisworker.h>
#include <zeroslack/semantic/semanticindexsnapshot.h>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>

namespace SignalTraceTest {
inline QString branchingSource(int fanout = 240)
{
    QString source = QStringLiteral(
        "module leaf(input wire d, output wire q);\nassign q = d;\nendmodule\n"
        "module top(input wire source, output wire done);\n"
        "wire core, remote;\nassign core = source;\nassign done = core;\n"
        "leaf consumer(.d(core), .q(remote));\n");
    for (int i = 0; i < fanout; ++i)
        source += QStringLiteral("wire reader_%1;\nassign reader_%1 = core;\n").arg(i);
    return source + QStringLiteral("endmodule\n");
}

struct Fixture {
    QTemporaryDir directory;
    QString file;
    QString source;
    SignalTraceContext context;

    bool build(const QString& text)
    {
        if (!directory.isValid()) return false;
        source = text;
        file = directory.filePath(QStringLiteral("trace.sv"));
        QFile output(file);
        if (!output.open(QIODevice::WriteOnly)) return false;
        if (output.write(source.toUtf8()) != source.toUtf8().size()) return false;
        output.close();
        SemanticAnalysisRequest request;
        request.generation = request.computationRevision = 1;
        request.reason = SemanticAnalysisReason::WorkspaceOpen;
        request.impactHint = SemanticChangeImpact::WorkspaceConfig;
        request.project.workspaceRoot = directory.path();
        request.project.systemVerilogFiles = request.project.allFiles = {file};
        request.project.topModule = QStringLiteral("top");
        request.changedFiles = {file};
        request.sourceOverrides = {{file, source}};
        const auto result = IncrementalSemanticAnalysisWorker::analyze(request, {}, {}, [] { return false; });
        context = {directory.path(), "self-contained", {}, {result.preparedSnapshot, 1}};
        return result.error.isEmpty() && bool(context.token.snapshot);
    }
};

inline bool settled(SignalTracePanel& panel, int timeoutMs = 10000)
{
    QElapsedTimer timer;
    timer.start();
    while (panel.busy() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return !panel.busy();
}
}
