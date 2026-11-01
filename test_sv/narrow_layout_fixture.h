#pragma once
#include "trace_repair_fixture.h"
#include <QDir>

namespace NarrowLayoutTest {
struct Fixture {
    QTemporaryDir directory;
    QString file;
    QString leafFile;
    QHash<QString, QString> sources;
    SignalTraceContext context;

    bool build()
    {
        if (!directory.isValid()) return false;
        file=directory.filePath("top_with_a_long_but_complete_source_name.sv");
        leafFile=directory.filePath("leaf.sv");
        sources[leafFile]="module leaf(input wire d, output wire q); assign q=d; endmodule\n";
        QString top="module top(input logic clk, enable, source, output wire done);\n"
            "wire core; assign core=source; assign done=core;\n"
            "wire left_q, right_q;\n"
            "leaf instance_with_a_long_name_left(.d(core),.q(left_q));\n"
            "leaf instance_with_a_long_name_right(.d(core),.q(right_q));\n"
            "wire loop_a,loop_b; assign loop_a=~loop_b; assign loop_b=loop_a;\n"
            "typedef enum logic [1:0] {IDLE,RUN,WAITING} state_t;\n"
            "state_t state;\nstate_t next_state;\n"
            "always_ff @(posedge clk) begin\nstate<=next_state;\nend\n"
            "always_comb begin\nnext_state=state;\ncase(state)\n"
            "IDLE: if(enable) next_state=RUN;\n"
            "RUN: next_state=WAITING;\n"
            "WAITING: next_state=IDLE;\n"
            "default: next_state=IDLE;\nendcase\nend\n";
        for(int part=0;part<6;++part){
            const QString name=QString("references_with_a_long_file_name_%1.svh").arg(part);
            top+="`include \""+name+"\"\n";
            QString body;
            for(int row=0;row<40;++row){
                const int id=part*40+row;
                body+=QString("wire reader_%1; assign reader_%1=core;\n").arg(id);
            }
            body+=QString("logic registered_%1; always_ff @(posedge clk) if(enable) registered_%1<=core;\n").arg(part);
            sources[directory.filePath(name)]=body;
        }
        sources[file]=top+"endmodule\n";
        for(auto it=sources.cbegin();it!=sources.cend();++it){
            QFile out(it.key());
            if(!out.open(QIODevice::WriteOnly) || out.write(it.value().toUtf8())!=it.value().toUtf8().size())return false;
        }
        SemanticAnalysisRequest request;
        request.generation=request.computationRevision=1;
        request.reason=SemanticAnalysisReason::WorkspaceOpen;
        request.impactHint=SemanticChangeImpact::WorkspaceConfig;
        request.project.workspaceRoot=directory.path();
        request.project.systemVerilogFiles={file,leafFile};
        request.project.allFiles=sources.keys();
        request.project.includeDirs={directory.path()};
        request.project.topModule="top";
        request.changedFiles=sources.keys();
        request.sourceOverrides=sources;
        const auto result=IncrementalSemanticAnalysisWorker::analyze(request,{}, {},[]{return false;});
        context={directory.path(),"narrow-layout",{}, {result.preparedSnapshot,1}};
        return result.error.isEmpty() && bool(context.token.snapshot);
    }
};
}
