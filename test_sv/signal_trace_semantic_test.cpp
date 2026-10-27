#include <zeroslack/semantic/incrementalsemanticanalysisworker.h>
#include <zeroslack/semantic/semanticindexsnapshot.h>
#include <zeroslack/semantic/signaltraceservice.h>
#include <zeroslack/semantic/signalfeedbackservice.h>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class SignalTraceSemanticTest final : public QObject {
    Q_OBJECT
    QTemporaryDir temporary;
    QString file;
    quint64 revision=1;
    WorkspaceAnalysisResult build(const QString& source,const QMap<QString,QString>& companions={}) {
        file=temporary.filePath(QStringLiteral("trace.sv"));
        QFile out(file);
        if (!out.open(QIODevice::WriteOnly) || out.write(source.toUtf8())!=source.toUtf8().size()) return {};
        out.close();
        QHash<QString,QString> inputs{{file,source}};
        for(auto it=companions.cbegin();it!=companions.cend();++it){const auto path=temporary.filePath(it.key());QFile child(path);
            if(!child.open(QIODevice::WriteOnly) || child.write(it.value().toUtf8())<0)return {};inputs.insert(path,it.value());}
        SemanticAnalysisRequest request;
        request.generation=++revision; request.computationRevision=revision;
        request.reason=SemanticAnalysisReason::WorkspaceOpen;
        request.impactHint=SemanticChangeImpact::WorkspaceConfig;
        request.project.workspaceRoot=temporary.path(); request.project.systemVerilogFiles=inputs.keys();
        request.project.allFiles=inputs.keys(); request.project.topModule=QStringLiteral("top");
        request.changedFiles=inputs.keys(); request.sourceOverrides=inputs;
        auto result=IncrementalSemanticAnalysisWorker::analyze(request,{}, {},[]{return false;});
        qInfo()<<"production-analysis"<<"symbols"<<result.totalSymbols<<"diagnostics"<<result.diagnostics.size()
            <<"error"<<result.error<<"slang"<<result.slangInvoked;
        if (result.preparedSnapshot) {
            qInfo()<<"relationships"<<result.preparedSnapshot->relationshipCount();
        }
        for (const auto& diagnostic:result.diagnostics) qInfo()<<diagnostic.message;
        return result;
    }
    SignalTraceContext context(const WorkspaceAnalysisResult& result,QString instance={}) const {
        return {temporary.path(),QStringLiteral("project-a"),instance,{result.preparedSnapshot,revision}};
    }
    static QList<SignalTraceGroup> data(const SignalTracePage& page) {
        QList<SignalTraceGroup> result;
        for (const auto& group:page.groups) if (group.dataPath) result.append(group);
        return result;
    }
private slots:
    void crossFileEditsInvalidateDependencyEvidence(){
        const QString source="module top(input wire a,output wire y);child u(.p(a),.q(y));endmodule\n";
        const auto result=build(source,{{"child.sv","module child(input wire p,output wire q);assign q=p;endmodule\n"}});
        QVERIFY(result.preparedSnapshot);auto captured=context(result,"top.u");
        SignalTraceService current(captured);const auto target=current.resolve({},"p",temporary.filePath("child.sv"),"child");
        QVERIFY(target.object.isConcrete());QVERIFY(!current.oneHop(target.object,SignalTraceDirection::Upstream).groups.isEmpty());
        captured.currentSourceHashes[file]=SemanticSignalRoleEvidence::sourceHash(source+"// unsaved\n");
        SignalTraceService edited(captured);QVERIFY(edited.validate(target.object));
        const auto page=edited.oneHop(target.object,SignalTraceDirection::Upstream);
        QCOMPARE(page.status,SignalTraceStatus::Stale);QVERIFY(page.groups.isEmpty());
        QCOMPARE(SignalFeedbackService(captured).analyze("top.u").status,SignalTraceStatus::Stale);
    }
    void referenceEventsUseExactAnchorsAndRoles() {
        const auto result=build("module top(input logic b,c, output logic a);\n"
            "always_comb begin a=b+b; if(c) a=b; end\nendmodule\n\n\n");
        SignalTraceService service(context(result));
        auto a=service.resolve({},"a",file,"top"), b=service.resolve({},"b",file,"top"), c=service.resolve({},"c",file,"top");
        QCOMPARE(service.references(a.object).references.size(),2);
        QCOMPARE(service.references(b.object).references.size(),3);
        const auto controls=service.references(c.object).references;
        QCOMPARE(controls.size(),1); QCOMPARE(controls.first().role,SignalTraceRole::Control);
        QCOMPARE(controls.first().fileLineCount,6);
        QSet<int> columns;
        for (const auto& item:service.references(b.object).references) {
            QCOMPARE(item.role,SignalTraceRole::Read); columns.insert(item.range.column);
        }
        QCOMPARE(columns.size(),3);
    }
    void feedbackClassification_data() {
        QTest::addColumn<QString>("source"); QTest::addColumn<int>("combinational");
        QTest::addColumn<int>("sequential"); QTest::addColumn<int>("uncertain");
        QTest::newRow("ring")<<QString("module top(output wire a,b); assign a=~b; assign b=~a; endmodule")<<1<<0<<0;
        QTest::newRow("self")<<QString("module top(output wire a); assign a=~a; endmodule")<<1<<0<<0;
        QTest::newRow("reconvergence")<<QString("module top(input wire s,output wire a,b,c); assign a=s; assign b=s; assign c=a|b; endmodule")<<0<<0<<0;
        QTest::newRow("register")<<QString("module top(input logic clk,output logic a); always_ff @(posedge clk) a<=~a; endmodule")<<0<<1<<0;
        QTest::newRow("mixed")<<QString("module top(input logic clk,output wire a,b,output logic q); assign a=b|q; assign b=~a; always_ff @(posedge clk) q<=a; endmodule")<<1<<1<<0;
        QTest::newRow("ordered")<<QString("module top(input logic s,output logic a,b); always_comb begin a=b; b=a; a=s; b=a; end endmodule")<<0<<0<<0;
        QTest::newRow("shift-no-loop")<<QString("module top(input wire s,output wire [3:0] a); assign a[0]=s; assign a[3:1]=a[2:0]; endmodule")<<0<<0<<0;
        QTest::newRow("invert-slices-no-loop")<<QString("module top(input wire s,output wire [3:0] a); assign a[0]=s; assign a[3:1]=~a[2:0]; endmodule")<<0<<0<<0;
        QTest::newRow("arithmetic-bits-uncertain")<<QString("module top(output wire [3:0] a); assign a=a+1'b1; endmodule")<<0<<0<<1;
        QTest::newRow("partial-overwrite-no-loop")<<QString("module top(input logic [3:0] s,output logic [3:0] a); always_comb begin a=~a; a[1:0]=s[1:0]; a[3:2]=s[3:2]; end endmodule")<<0<<0<<0;
        QTest::newRow("one-time-timed")<<QString("module top(input logic clk,output logic a); initial @(posedge clk) a=~a; endmodule")<<0<<0<<1;
        QTest::newRow("bit-loop")<<QString("module top(output wire [1:0] a); assign a[0]=a[1]; assign a[1]=a[0]; endmodule")<<1<<0<<0;
        QTest::newRow("latch")<<QString("module top(input logic c,output logic a); always_latch if(c) a=~a; endmodule")<<0<<0<<1;
        QTest::newRow("multidriver")<<QString("module top(input wire s,output wire a,b); assign a=b; assign a=s; assign b=a; endmodule")<<0<<0<<1;
        QTest::newRow("constant-driver")<<QString("module top(output wire a,b); assign a=b; assign a=1'b0; assign b=a; endmodule")<<0<<0<<1;
        QTest::newRow("unresolved-driver")<<QString("module top(output wire a); missing u(.x(a)); assign a=~a; endmodule")<<0<<0<<1;
        QTest::newRow("dynamic")<<QString("module top(input wire i,output wire [1:0] a); assign a[0]=a[i]; assign a[1]=a[0]; endmodule")<<0<<0<<1;
    }
    void feedbackClassification() {
        QFETCH(QString,source); QFETCH(int,combinational); QFETCH(int,sequential); QFETCH(int,uncertain);
        const auto result=build(source);
        SignalFeedbackService service(context(result));
        const auto report=service.analyze("top");
        int counts[3]={0,0,0};
        for (const auto& finding:report.findings) {
            ++counts[int(finding.kind)]; QVERIFY(!finding.id.isEmpty()); QVERIFY(!finding.edges.isEmpty());
            for (const auto& edge:finding.edges) { QVERIFY(edge.evidence.range.line>0); QVERIFY(edge.from.isConcrete()); }
        }
        QCOMPARE(counts[0],combinational); QCOMPARE(counts[1],sequential); QCOMPARE(counts[2],uncertain);
        QCOMPARE(service.analyze("top",[]{return true;}).status,SignalTraceStatus::Cancelled);
        QCOMPARE(service.analyze({}).status,SignalTraceStatus::Unavailable);
    }
    void assignmentDirectionHasNoMirrorCycle() {
        auto result=build("module top(input logic b, output wire a); assign a=b; endmodule\n");
        QVERIFY2(result.error.isEmpty(),qPrintable(result.error)); QVERIFY(result.preparedSnapshot);
        SignalTraceService service(context(result));
        auto a=service.resolve({},"a",file,"top"), b=service.resolve({},"b",file,"top");
        QCOMPARE(a.status,SignalTraceStatus::Ready); QCOMPARE(b.status,SignalTraceStatus::Ready);
        auto upstream=service.oneHop(a.object,SignalTraceDirection::Upstream);
        auto downstream=service.oneHop(b.object,SignalTraceDirection::Downstream);
        QCOMPARE(data(upstream).size(),1); QCOMPARE(data(downstream).size(),1);
        QCOMPARE(data(upstream).first().object.symbol.symbolName,QString("b"));
        QCOMPARE(data(downstream).first().object.symbol.symbolName,QString("a"));
        QVERIFY(data(service.oneHop(a.object,SignalTraceDirection::Downstream)).isEmpty());
        QVERIFY(data(service.oneHop(b.object,SignalTraceDirection::Upstream)).isEmpty());
        const auto references=service.references(a.object);
        QCOMPARE(references.references.size(),1);
        QCOMPARE(references.references.first().role,SignalTraceRole::Write);
        QCOMPARE(references.references.first().fileLineCount,2);
        for (const auto& group:upstream.groups) for (const auto& evidence:group.evidence) {
            QVERIFY(evidence.range.line>0); QVERIFY(!evidence.sourceHash.isEmpty());
            QCOMPARE(evidence.dependency.boundary,SignalStorageBoundary::Combinational);
        }
    }
    void actualInstancesAndPortDirections() {
        auto result=build(
            "module child #(parameter W=1)(input logic [W-1:0] p, output wire [W-1:0] q); assign q=p; endmodule\n"
            "module top(input logic [3:0] src, output wire [3:0] out0,out1);\n"
            "child #(.W(4)) first(.p(src),.q(out0)); child #(.W(4)) second(.p(src),.q(out1)); endmodule\n");
        QVERIFY2(result.error.isEmpty(),qPrintable(result.error)); QVERIFY(result.preparedSnapshot);
        SignalTraceService definitions(context(result));
        auto ambiguous=definitions.resolve({},"q",file,"child");
        QCOMPARE(ambiguous.status,SignalTraceStatus::DefinitionOnly); QCOMPARE(ambiguous.choices.size(),2);
        QVERIFY(!ambiguous.object.isConcrete());
        SignalTraceService first(context(result,"top.first"));
        auto q=first.resolve({},"q",file,"child"), p=first.resolve({},"p",file,"child");
        QCOMPARE(q.status,SignalTraceStatus::Ready); QCOMPARE(p.status,SignalTraceStatus::Ready);
        QCOMPARE(q.object.objectPath,QString("top.first.q"));
        auto qInputs=data(first.oneHop(q.object,SignalTraceDirection::Upstream));
        QCOMPARE(qInputs.size(),1); QCOMPARE(qInputs.first().object.objectPath,QString("top.first.p"));
        auto portInput=data(first.oneHop(p.object,SignalTraceDirection::Upstream));
        QCOMPARE(portInput.size(),1); QCOMPARE(portInput.first().object.objectPath,QString("top.src"));
        auto portOutput=data(first.oneHop(q.object,SignalTraceDirection::Downstream));
        QCOMPARE(portOutput.size(),1); QCOMPARE(portOutput.first().object.objectPath,QString("top.out0"));
        QVERIFY(ambiguous.choices.at(0).id()!=ambiguous.choices.at(1).id());
    }
    void aliasAndBidirectionalConnectionsRemainExplicitlyUncertain(){
        const auto result=build("module child(inout wire p); endmodule\n"
            "module top(inout wire a,b); alias a=b; child u(.p(a)); endmodule\n");
        QVERIFY(result.preparedSnapshot);SignalTraceService service(context(result));
        const auto a=service.resolve({},"a",file,"top");QVERIFY(a.object.isConcrete());
        QVERIFY(data(service.oneHop(a.object,SignalTraceDirection::Upstream)).isEmpty());
        QVERIFY(data(service.oneHop(a.object,SignalTraceDirection::Downstream)).isEmpty());
        bool alias=false,bidirectional=false;
        for(const auto& edge:result.preparedSnapshot->relationships())if(edge.signalDependency){
            const auto& fact=*edge.signalDependency;
            if(fact.kind==SignalDependencyKind::Coverage){
                alias|=fact.processKind=="Net alias";bidirectional|=fact.processKind=="Bidirectional/ref port";
            }
        }
        QVERIFY(alias);QVERIFY(bidirectional);QVERIFY(!service.references(a.object).references.isEmpty());
        const auto feedback=SignalFeedbackService(context(result)).analyze("top");
        QCOMPARE(feedback.status,SignalTraceStatus::Partial);QVERIFY(!feedback.limitations.isEmpty());
    }
    void parameterGenerateMembersAndRanges() {
        auto result=build(
            "module child #(parameter W=1)(input logic [W-1:0] p, output wire [W-1:0] q); assign q=p; endmodule\n"
            "module top(input logic [3:0] src, output wire [1:0] dst);\n"
            "for(genvar i=0;i<2;i++) begin:g child #(.W(i+1)) u(.p(src[i:0]),.q()); end\n"
            "typedef struct packed {logic [1:0] hi;logic [1:0] lo;} packet_t; packet_t packet;\n"
            "assign packet.lo=src[1:0]; assign packet.hi=src[3:2]; assign dst=packet.hi; endmodule\n");
        QVERIFY2(result.error.isEmpty(),qPrintable(result.error)); QVERIFY(result.preparedSnapshot);
        QMap<QString,SignalBitRange> widths;
        bool lower=false,upper=false;
        for (const auto& edge:result.preparedSnapshot->relationships()) {
            if (!edge.signalDependency) continue;
            const auto& fact=*edge.signalDependency;
            if (fact.kind==SignalDependencyKind::Coverage && fact.toObject.endsWith(".q")) widths.insert(fact.toObject,fact.toBits);
            if (fact.kind==SignalDependencyKind::Data && fact.toObject=="top.packet") {
                lower |= fact.fromBits.known && fact.fromBits.low==0 && fact.fromBits.high==1 && fact.toBits.low==0 && fact.toBits.high==1;
                upper |= fact.fromBits.known && fact.fromBits.low==2 && fact.fromBits.high==3 && fact.toBits.low==2 && fact.toBits.high==3;
            }
        }
        QVERIFY(widths.contains("top.g[0].u.q")); QVERIFY(widths.contains("top.g[1].u.q"));
        QCOMPARE(widths.value("top.g[0].u.q").high,qint64(0));
        QCOMPARE(widths.value("top.g[1].u.q").high,qint64(1));
        QVERIFY(lower); QVERIFY(upper);
        SignalTraceService service(context(result));
        const auto member=service.resolve({},"packet",file,"top","packet.hi");
        QCOMPARE(member.status,SignalTraceStatus::Ready);QVERIFY(member.object.bits.known);
        QCOMPARE(member.object.bits.low,qint64(2));QCOMPARE(member.object.bits.high,qint64(3));
        const auto slice=service.resolve({},"src",file,"top","src[1:0]");
        QCOMPARE(slice.status,SignalTraceStatus::Ready);QCOMPARE(slice.object.bits.high,qint64(1));
        QVERIFY(service.validate(slice.object));auto invalid=slice.object;invalid.bits.high=100;QVERIFY(!service.validate(invalid));
    }
    void controlIsRelatedAndProcessOrderIsRespected() {
        auto result=build("module top(input logic b,c, output logic a,tmp);\n"
            "always_comb begin tmp=b; a=tmp; if(c) a=1'b0; end endmodule\n");
        QVERIFY(result.preparedSnapshot);
        SignalTraceService service(context(result));
        auto target=service.resolve({},"a",file,"top");
        QCOMPARE(target.status,SignalTraceStatus::Ready);
        auto page=service.oneHop(target.object,SignalTraceDirection::Upstream);
        auto dependencies=data(page); QCOMPARE(dependencies.size(),1);
        QCOMPARE(dependencies.first().object.symbol.symbolName,QString("b"));
        bool control=false;
        for (const auto& group:page.groups) if (!group.dataPath && group.object.symbol.symbolName=="c") control=true;
        QVERIFY(control);
        for (const auto& group:page.groups) for (const auto& evidence:group.evidence)
            QCOMPARE(evidence.dependency.boundary,SignalStorageBoundary::Combinational);
    }
    void savedIdentityRejectsEditsAndProjectChanges() {
        const QString source="module top(input logic b,output wire a);assign a=b;endmodule\n";
        auto result=build(source); QVERIFY(result.preparedSnapshot);
        SignalTraceService service(context(result));
        const auto saved=service.resolve({},"a",file,"top").object;
        const auto restored=SignalTraceObject::fromJson(saved.toJson());
        QCOMPARE(restored.id(),saved.id()); QVERIFY(service.validate(restored));
        auto otherContext=context(result); otherContext.projectId="project-b";
        QVERIFY(!SignalTraceService(otherContext).validate(saved));
        auto edited=build(source+"// edit\n"); QVERIFY(edited.preparedSnapshot);
        SignalTraceService after(context(edited));
        QVERIFY(!after.validate(saved));
        QCOMPARE(after.oneHop(saved,SignalTraceDirection::Upstream).status,SignalTraceStatus::Stale);
        QVERIFY(service.validate(saved)); // The retained publication remains immutable.
    }
    void boundedFanoutAndCancellation() {
        QString source="module top(input logic seed);\n";
        for (int i=0;i<350;++i) source+=QString("wire out_%1;assign out_%1=seed;\n").arg(i);
        source+="endmodule\n";
        auto result=build(source); QVERIFY(result.preparedSnapshot);
        SignalTraceService service(context(result));
        auto root=service.resolve({},"seed",file,"top").object;
        QVERIFY(root.isConcrete());
        QElapsedTimer elapsed;elapsed.start();
        auto first=service.oneHop(root,SignalTraceDirection::Downstream,0,37);
        qInfo()<<"first-page-ms"<<elapsed.elapsed()<<"scanned"<<first.scanned;
        QCOMPARE(first.status,SignalTraceStatus::Partial); QCOMPARE(data(first).size(),37);
        QVERIFY(first.nextCursor>0);
        QSet<QString> objects;
        int cursor=0,pages=0;
        do {
            auto page=service.oneHop(root,SignalTraceDirection::Downstream,cursor,37);
            for (const auto& group:data(page)) objects.insert(group.object.objectPath);
            cursor=page.nextCursor;
            QVERIFY(++pages<=12);
        } while (cursor>=0);
        QCOMPARE(objects.size(),350);
        auto cancelled=service.oneHop(root,SignalTraceDirection::Downstream,0,100,[]{return true;});
        QCOMPARE(cancelled.status,SignalTraceStatus::Cancelled); QVERIFY(cancelled.groups.isEmpty());
    }
};
QTEST_GUILESS_MAIN(SignalTraceSemanticTest)
#include "signal_trace_semantic_test.moc"
