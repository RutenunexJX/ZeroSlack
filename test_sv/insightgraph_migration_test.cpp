#include "fsmgraphlayout.h"
#include "insightgraphcore.h"
#include "graphexportservice.h"
#include "liveinsighttypes.h"
#include "signaltracepanel.h"
#include "signaltracemodels.h"
#if __has_include("signaltracedistributiondelegate.h")
#include "signaltracedistributiondelegate.h"
#endif
#include "testuistyle.h"
#include "applicationthememanager.h"
#include <zeroslack/semantic/signalfeedbackservice.h>
#include <zeroslack/semantic/incrementalsemanticanalysisworker.h>
#include <zeroslack/semantic/moduleblockdiagramservice.h>
#include <QApplication>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFontMetricsF>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>
#include <cstdio>
#include <limits>

namespace {
int failures = 0;
void check(bool ok, const char* label)
{ std::printf("[%s] %s\n",ok?"PASS":"FAIL",label);failures += !ok; }
QJsonArray point(const QPointF& p) { return {p.x(),p.y()}; }
QJsonArray rect(const QRectF& r) { return {r.x(),r.y(),r.width(),r.height()}; }
QJsonObject anchor(const RtlInsightCodeLink& a)
{ return {{"file",a.fileName},{"line",a.line},{"column",a.column},{"displayFile",a.fileDisplayName},{"displayLine",a.lineDisplayName}}; }
QJsonObject layout(const FsmGraphLayout& value)
{
    QJsonArray nodes,edges;
    for(const auto& n:value.nodes)nodes.append(QJsonObject{{"id",n.nodeId},{"state",n.stateName},{"display",n.displayName},
        {"detail",n.detail},{"rect",rect(n.rect)},{"title",rect(n.titleTextRect)},{"detailRect",rect(n.detailTextRect)},
        {"rank",n.rank},{"order",n.order},{"alias",n.alias},{"initial",n.initialState},{"implicit",n.implicitState},
        {"showDetail",n.showDetail},{"canonical",n.canonicalNodeId},{"deadEnd",n.deadEndState},{"anchor",anchor(n.codeLink)}});
    for(const auto& e:value.edges){QJsonArray points;for(const auto& p:e.points)points.append(point(p));
        edges.append(QJsonObject{{"id",e.edgeId},{"transition",e.transitionIndex},{"from",e.fromNodeId},{"to",e.toNodeId},
            {"fromState",e.fromState},{"toState",e.toState},{"condition",e.condition},{"label",e.label},{"points",points},
            {"tip",point(e.arrowTip)},{"angle",e.arrowAngle},{"labelAnchor",point(e.labelAnchor)},{"labelRect",rect(e.labelRect)},
            {"fromRank",e.layoutFromRank},{"toRank",e.layoutToRank},{"self",e.selfLoop},{"reversed",e.reversedForLayout},
            {"curved",e.curved},{"anchor",anchor(e.codeLink)}});}
    return {{"nodes",nodes},{"edges",edges},{"nodeBounds",rect(value.nodeBounds)},{"sceneBounds",rect(value.sceneBounds)},
        {"warnings",QJsonArray::fromStringList(value.warnings)},{"crossings",value.renderedCrossingCount},{"aliases",value.fallbackAliasCount}};
}
QJsonObject draft(InsightGraphDraft value)
{
    InsightGraphCore core;const auto s=core.update("equivalence",value).snapshot;
    QJsonObject nodes,edges;
    for(const auto& n:s.nodes)nodes[n.nodeId]=QJsonObject{{"name",n.displayName},{"detail",n.detail},{"file",n.sourceFile},
        {"line",n.sourceLine},{"domain",int(n.domain)},{"heat",n.heatScore},{"attributes",QJsonObject::fromVariantMap(n.attributes)}};
    for(const auto& e:s.edges)edges[e.edgeId]=QJsonObject{{"from",e.fromNodeId},{"to",e.toNodeId},{"name",e.displayName},
        {"domain",int(e.domain)},{"weight",e.weight},{"attributes",QJsonObject::fromVariantMap(e.attributes)}};
    return {{"nodes",nodes},{"edges",edges},{"context",s.contextKey},{"hint",s.layoutHint},
        {"metadata",QJsonObject::fromVariantMap(s.metadata)},{"fingerprint",QString::fromLatin1(s.contentFingerprint.toHex())}};
}
QJsonObject evidence(const SignalTraceEvidence& e)
{
    return {{"id",e.id},{"role",SignalTraceService::roleName(e.role)},{"file",e.range.fileName},
        {"line",e.range.line},{"column",e.range.column},{"endLine",e.range.endLine},{"endColumn",e.range.endColumn},
        {"anchor",anchor(e.link)},{"text",e.text},{"module",e.moduleName},{"object",e.objectId},
        {"hash",QString::fromLatin1(e.sourceHash.toHex())},{"lines",e.fileLineCount},{"definition",e.definitionOnly},
        {"boundary",SignalTraceService::boundaryName(e.dependency.boundary)},
        {"limitations",QJsonArray::fromStringList(e.dependency.limitations)}};
}
QString hash(const QByteArray& bytes)
{ return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()); }
QJsonObject heat(SignalTraceHeatModel& model)
{
    QJsonArray rows;
    for(int r=0;r<model.rowCount();++r){QJsonArray cells,refs;
        for(int c=0;c<model.columnCount();++c)cells.append(QJsonValue::fromVariant(model.data(model.index(r,c))));
        for(const auto& e:model.references(model.index(r,0)))refs.append(evidence(e));
        rows.append(QJsonObject{{"cells",cells},{"references",refs}});}
    QImage distribution(400,qMax(1,model.rowCount())*42,QImage::Format_ARGB32_Premultiplied);distribution.fill(Qt::white);
    QPainter painter(&distribution);SignalTraceDistributionDelegate delegate;
    for(int r=0;r<model.rowCount();++r){QStyleOptionViewItem option;option.rect=QRect(0,r*42,400,42);
        option.font=QApplication::font();option.fontMetrics=QFontMetrics(option.font);option.palette=QApplication::palette();
        delegate.paint(&painter,option,model.index(r,SignalTraceHeatModel::Distribution));}
    painter.end();
    return {{"rows",rows},{"maximum",model.maximumCount()},{"csv",QString::fromUtf8(model.csv())},
        {"distributionPixels",hash(QByteArray(reinterpret_cast<const char*>(distribution.constBits()),distribution.sizeInBytes()))}};
}
const QString design=QStringLiteral(R"SV(module leaf(input wire d, output wire q);
  assign q = d;
endmodule
module top(input logic clk, reset_n, enable, source, output wire done);
  wire a, b, loop_a, loop_b;
  logic registered;
  leaf left(.d(source), .q(a));
  leaf right(.d(source), .q(b));
  assign done = a | b;
  always_ff @(posedge clk) if (enable) registered <= registered ^ done;
  assign loop_a = ~loop_b;
  assign loop_b = loop_a;
  typedef enum logic [1:0] { IDLE, RUN, FINISH } state_t;
  state_t state_q;
  state_t state_d;
  always_ff @(posedge clk or negedge reset_n) begin
    if (!reset_n) state_q <= IDLE;
    else state_q <= state_d;
  end
  always_comb begin
    state_d = state_q;
    case (state_q)
      IDLE: if (enable) state_d = RUN;
      RUN: if (done) state_d = FINISH;
      FINISH: state_d = IDLE;
      default: state_d = IDLE;
    endcase
  end
endmodule
)SV");
}
int main(int argc,char** argv)
{
    QStandardPaths::setTestModeEnabled(true);QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
    QTemporaryDir settings, temporary;
    QCoreApplication::setOrganizationName("ZeroSlackTest");QCoreApplication::setApplicationName("InsightGraphMigration");
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,settings.path());
    if(!initializeUiStyleForTest())return 2;ApplicationThemeManager::instance().applyToApplication();
    const QString input=qEnvironmentVariable("INSIGHTGRAPH_MIGRATION_INPUT",temporary.path());
    const QString output=qEnvironmentVariable("INSIGHTGRAPH_MIGRATION_OUTPUT",temporary.filePath("output"));
    QDir().mkpath(input);QDir().mkpath(output);const QString file=QDir(input).filePath("top.sv");
    {QFile source(file);if(source.exists()){check(source.open(QIODevice::ReadOnly),"read shared original fixture");check(source.readAll()==design.toUtf8(),"original fixture content unchanged");}
     else {check(source.open(QIODevice::WriteOnly),"create original fixture");check(source.write(design.toUtf8())==design.toUtf8().size(),"write complete original fixture");}}
    SemanticAnalysisRequest request;request.generation=16;request.computationRevision=16;
    request.reason=SemanticAnalysisReason::WorkspaceOpen;request.impactHint=SemanticChangeImpact::WorkspaceConfig;
    request.project.workspaceRoot=input;request.project.allFiles={file};request.project.systemVerilogFiles={file};request.project.topModule="top";
    request.changedFiles={file};request.sourceOverrides={{file,design}};
    const auto analysis=IncrementalSemanticAnalysisWorker::analyze(request,{}, {},[]{return false;});
    check(analysis.error.isEmpty() && bool(analysis.preparedSnapshot),"real semantic producer on original SV");
    if(!analysis.preparedSnapshot)return 3;
    SignalTraceContext context{input,"migration","top",{analysis.preparedSnapshot,16}};
    QJsonObject result{{"fixtureHash",hash(design.toUtf8())}};
    FsmGraphService fsm(context.token);const auto report=fsm.buildFsmGraph({{},"top",file});
    check(report.found && !report.graphs.isEmpty(),"real FSM producer returns graph");
    QJsonArray layouts;
    QFontMetricsF metrics(QApplication::font());
    for(const auto& graph:report.graphs)for(const auto direction:{FsmLayoutDirection::TopDown,FsmLayoutDirection::LeftRight}){
        FsmLayoutOptions options;options.direction=direction;options.textWidth=[&](const QString& text){return metrics.horizontalAdvance(text);};
        layouts.append(layout(layoutFsmGraph(graph,options)));}
    result["fsm"]=layouts;result["fsmProjection"]=draft(InsightGraphCore::fromFsmGraph(report));
    FsmGraph manual;
    for(const auto& name:{"A","B","C","D"}){FsmStateRow row;row.stateDisplayName=name;row.detailDisplayName="state_q";
        row.codeLink={file,37+manual.stateRows.size(),5,"top.sv","unchanged display"};manual.stateRows.append(row);}
    for(const auto& pair:QList<QPair<QString,QString>>{{"A","B"},{"B","C"},{"C","D"},{"D","A"},{"B","B"},{"A","B"},{"C","implicit"}}){
        FsmTransitionRow row;row.fromStateDisplayName=pair.first;row.toStateDisplayName=pair.second;row.conditionDisplayName="condition";
        row.codeLink={file,60+manual.transitionRows.size(),7,"top.sv","source anchor"};manual.transitionRows.append(row);}
    result["manualFsm"]=layout(layoutFsmGraph(manual));
    ModuleBlockDiagramService modules(context.token);const auto module=modules.buildModuleBlockDiagram({{},"top",file});
    check(module.found && module.nodes.size()>=3,"real hierarchy retains two leaf instances");
    result["hierarchy"]=draft(InsightGraphCore::fromModuleBlockDiagram(module));
    SignalTraceService trace(context);const auto resolution=trace.resolve({},"done",file,"top");
    check(resolution.object.isConcrete(),"trace identity resolves actual top instance");
    result["object"]=resolution.object.toJson();
    check(SignalTraceObject::fromJson(resolution.object.toJson()).id()==resolution.object.id(),"existing object JSON round trip");
    const auto page=trace.oneHop(resolution.object,SignalTraceDirection::Upstream,0,1);
    QJsonArray groups;for(const auto& group:page.groups){QJsonArray values;for(const auto& e:group.evidence)values.append(evidence(e));
        groups.append(QJsonObject{{"object",group.object.toJson()},{"data",group.dataPath},{"evidence",values},{"limitations",QJsonArray::fromStringList(group.limitations)}});}
    result["tracePage"]=QJsonObject{{"groups",groups},{"status",int(page.status)},{"cursor",page.nextCursor},{"scanned",page.scanned},{"limitations",QJsonArray::fromStringList(page.limitations)}};
    const auto references=trace.references(resolution.object);SignalTraceHeatModel model;
    model.setReferences(references.references);check(model.rowCount()>0,"real heatmap references available");
    QJsonArray heats;heats.append(heat(model));model.setComplete(false);model.sort(SignalTraceHeatModel::Total,Qt::DescendingOrder);heats.append(heat(model));
    model.setRoleMaskFilter("top",3);heats.append(heat(model));model.setRoleMaskFilter("not present",63);heats.append(heat(model));result["heatmap"]=heats;
    const auto feedback=SignalFeedbackService(context).analyze("top");QJsonArray findings;
    for(const auto& finding:feedback.findings){QJsonArray edges;for(const auto& edge:finding.edges)edges.append(QJsonObject{{"id",edge.id},{"from",edge.from.toJson()},{"to",edge.to.toJson()},{"evidence",evidence(edge.evidence)}});
        findings.append(QJsonObject{{"id",finding.id},{"kind",int(finding.kind)},{"edges",edges},{"limitations",QJsonArray::fromStringList(finding.limitations)}});}
    check(!findings.isEmpty(),"real feedback producer classifies controlled loops");
    result["feedback"]=QJsonObject{{"findings",findings},{"status",int(feedback.status)},{"limitations",QJsonArray::fromStringList(feedback.limitations)}};
    SignalTracePanel panel;panel.setContext(context);panel.resize(760,300);panel.show();panel.showObject(resolution.object);
    check(QTest::qWaitFor([&]{return !panel.busy();},10000),"actual host trace query completes");
    panel.analyzeFeedback();check(QTest::qWaitFor([&]{return !panel.busy();},10000),"actual host feedback query completes");
    QJsonArray labels;auto* cycles=panel.findChild<QComboBox*>("traceCycles");check(cycles,"feedback selector exists");
    if(cycles)for(int i=0;i<cycles->count();++i)labels.append(QJsonObject{{"label",cycles->itemText(i)},{"tip",cycles->itemData(i,Qt::ToolTipRole).toString()}});
    result["feedbackPresentation"]=labels;
    const auto saved=panel.saveViewState();panel.hide();panel.show();panel.restoreViewState(saved);
    check(QTest::qWaitFor([&]{return !panel.busy();},10000),"hide show and saved view restore complete");
    check(panel.currentObject().id()==resolution.object.id(),"restored view retains stable source identity");
    LiveInsightRequestKey key;key.kind=LiveInsightKind::State;key.workspaceId=input;key.documentId=file;key.contextKey="top/state";
    key.documentRevision=std::numeric_limits<quint64>::max();key.semanticRevision=(Q_UINT64_C(1)<<63)+1;
    bool valid=false;check(LiveInsightRequestKey::fromVariantMap(key.toVariantMap(),&valid)==key && valid,"native request revisions retain 64-bit precision");
    auto renderer=GraphExportSource::fromRenderer([]{return QRectF(0,0,160,80);},[]{return true;},
        [](QPainter& painter,const QRectF& target,const QRectF&,QString*){painter.fillRect(target,QColor(30,80,130));painter.drawLine(target.topLeft(),target.bottomRight());return true;});
    QJsonArray exports;
    for(const auto& extension:{"png","svg","pdf"}){const QString path=QDir(output).filePath(QString("render.")+extension);
        const auto exported=GraphExportService::exportSource(renderer,path);check(exported.success,"callback export succeeds");
        QJsonObject item{{"format",extension},{"size",QJsonArray{exported.outputPixelSize.width(),exported.outputPixelSize.height()}},{"source",rect(exported.sourceRect)}};
        if(QString(extension)!="pdf"){QFile f(path);if(f.open(QIODevice::ReadOnly))item["sha256"]=hash(f.readAll());}exports.append(item);}
    result["exports"]=exports;
    QFile outputFile(QDir(output).filePath("behavior.json"));check(outputFile.open(QIODevice::WriteOnly),"behavior evidence writable");
    outputFile.write(QJsonDocument(result).toJson(QJsonDocument::Indented));
    return failures ? 1 : 0;
}
