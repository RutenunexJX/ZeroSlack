#include <QtWidgets>
#include <QtTest>
#include <atomic>
#include <cstdio>
#include <thread>
#define private public
#include "symbolanalyzer.h"
#undef private
#include "semanticindexsnapshot.h"
#include "semanticanalysisinput.h"
#include "slangmanager.h"
#include "workspacemanager.h"
#include "workspaceconfigurationservice.h"
#include "tsdocument.h"

namespace {
int checks = 0, failures = 0;
void expect(const char* name, bool ok) { ++checks; failures += !ok; std::printf("[%s] %s\n",ok?"PASS":"FAIL",name); }
bool waitFor(const std::function<bool()>& fn, int timeout=10000) {
 QElapsedTimer timer;timer.start();while(timer.elapsed()<timeout){QCoreApplication::processEvents(QEventLoop::AllEvents,10);if(fn())return true;QTest::qWait(2);}return fn();
}
bool write(const QString& path,const QString& text){QDir().mkpath(QFileInfo(path).absolutePath());QFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(text.toUtf8())==text.toUtf8().size();}
QString read(const QString& path){QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};return QString::fromUtf8(f.readAll());}
bool symbol(const char* name){return !SemanticIndex::getInstance()->getSymbolRecordsByName(QString::fromLatin1(name)).isEmpty();}
struct Harness {
 SymbolAnalyzer analyzer; quint64 generation=1000, terminal=0; bool committed=false, slang=false;QString error;
 Harness(){
  QObject::connect(&analyzer,&SymbolAnalyzer::semanticAnalysisCommitted,[this](const auto&r,const auto&){terminal=r.generation;committed=true;});
  QObject::connect(&analyzer,&SymbolAnalyzer::semanticAnalysisFailed,[this](const auto&r,const auto&e){terminal=r.generation;committed=false;error=e;});
  QObject::connect(&analyzer,&SymbolAnalyzer::semanticAnalysisDropped,[this](const auto&r,auto){terminal=r.generation;committed=false;});
  QObject::connect(&analyzer,&SymbolAnalyzer::semanticAnalysisTelemetry,[this](const auto&t){if(t.stage==SemanticAnalysisStage::Worker)slang=t.slangInvoked;});
 }
 bool run(const ProjectSnapshot& project, const QHash<QString,QString>& overrides={}, SemanticAnalysisReason reason=SemanticAnalysisReason::WorkspaceOpen,const QString& file={}) {
  SemanticAnalysisRequest r;r.generation=++generation;r.project=project;r.reason=reason;r.sourceOverrides=overrides;r.triggerFile=file;
  r.changedFiles=file.isEmpty()?project.systemVerilogFiles:QStringList{file};r.impactHint=reason==SemanticAnalysisReason::WorkspaceOpen?SemanticChangeImpact::WorkspaceConfig:SemanticChangeImpact::Unknown;
  r.runtimePolicy.maxDiagnostics=2;if(!file.isEmpty())r.documentRevisions.insert(file,generation);committed=false;slang=false;error.clear();
  analyzer.startSemanticAnalysisAsync(r);
  bool ready=waitFor([&]{return terminal==generation&&!analyzer.hasWorkspaceAnalysisInFlight();});
  if(!error.isEmpty())std::printf("semantic error: %s\n",qPrintable(error));
  return ready&&committed;
 }
};
ProjectSnapshot single(const QString& root, const QString& name){ProjectSnapshot p;p.workspaceRoot=root;p.systemVerilogFiles={QDir(root).filePath(name+".sv")};p.allFiles=p.systemVerilogFiles;p.includeDirs={root};write(p.systemVerilogFiles.first(),"module "+name+"; logic data; endmodule\n");return p;}
void retainedInputs(const QString& root){
 auto a=single(root+"/a","a");auto b=single(root+"/b","b");Harness h;
 expect("cold A commits",h.run(a)&&h.slang);const auto original=SemanticIndex::getInstance()->snapshot();
 h.analyzer.clearSemanticIndex();expect("cold B commits",h.run(b)&&h.slang);
 h.analyzer.clearSemanticIndex();expect("A revisit reuses complete immutable analysis",h.run(a)&&!h.slang&&SemanticIndex::getInstance()->snapshot()->symbolRecordCount()==original->symbolRecordCount());
 h.analyzer.forgetWorkspace(a.workspaceRoot);h.analyzer.clearSemanticIndex();
 expect("closing active A retains B reuse",h.run(b)&&!h.slang);
 expect("external source mutation written",write(b.systemVerilogFiles.first(),"module b; logic fresh; endmodule\n"));
 h.analyzer.clearSemanticIndex();expect("external content invalidates retained state",h.run(b)&&h.slang&&symbol("fresh"));
 auto flags=b;flags.defines.insert("ACTIVE_FLAG","1");write(b.systemVerilogFiles.first(),"module b;\n`ifdef ACTIVE_FLAG\nlogic defined_value;\n`else\nlogic default_value;\n`endif\nendmodule\n");
 expect("complete configuration participates in retained identity",h.run(flags)&&h.slang&&symbol("defined_value")&&!symbol("default_value"));
 expect("removing a define invalidates retained identity",h.run(b)&&h.slang&&symbol("default_value")&&!symbol("defined_value"));
 for(int i=0;i<4;++i){auto p=single(root+QString("/evict%1").arg(i),QString("cache_%1").arg(i));h.analyzer.clearSemanticIndex();expect("capacity fixture commits",h.run(p));}
 expect("retained workspace count is bounded and LRU is evicted",h.analyzer.retainedWorkspaces.size()<=3&&!h.analyzer.retainedWorkspaces.contains(SemanticInputCapture::pathKey(b.workspaceRoot)));
 ProjectSnapshot empty=b;empty.systemVerilogFiles.clear();empty.allFiles.clear();h.analyzer.clearSemanticIndex();
 expect("empty authoritative workspace clears symbols relationships diagnostics",h.run(empty)&&SemanticIndex::getInstance()->snapshot()->symbolRecordCount()==0&&SemanticIndex::getInstance()->snapshot()->relationshipCount()==0&&SemanticIndex::getInstance()->snapshot()->rawDiagnosticCount()==0);
}
void includesAndAdapters(const QString& root){
 ProjectSnapshot p;p.workspaceRoot=root;p.includeDirs={root+"/first",root+"/second"};p.systemVerilogFiles={root+"/a.sv",root+"/b.sv"};p.allFiles=p.systemVerilogFiles;
 QDir().mkpath(p.includeDirs.first());
 write(root+"/second/pick.svh","module picked_low; endmodule\n");
 write(root+"/a.sv","`define ACROSS_FILES 7\n`include \"pick.svh\"\npackage shared_p; parameter K=`ACROSS_FILES; endpackage\n");
 write(root+"/b.sv","module b; localparam FROM_PREVIOUS=`ACROSS_FILES; logic [shared_p::K-1:0] data; endmodule\n");
 Harness h;h.analyzer.setMaxPublishedDiagnostics(2);h.analyzer.analyzeProject(p);
 expect("sync project adapter commits common symbols and ordered CU",h.committed&&symbol("FROM_PREVIOUS")&&symbol("picked_low")&&SemanticIndex::getInstance()->snapshot()->rawDiagnosticCount()==0);
 expect("async project uses identical captured scope",h.run(p)&&!h.slang&&symbol("FROM_PREVIOUS"));
 expect("higher precedence include created",write(root+"/first/pick.svh","module picked_high; endmodule\n"));
 expect("negative include lookup creation invalidates retained state",h.run(p)&&h.slang&&symbol("picked_high")&&!symbol("picked_low"));
 expect("higher precedence include deleted",QFile::remove(root+"/first/pick.svh"));
 expect("include deletion restores lower precedence source",h.run(p)&&h.slang&&symbol("picked_low")&&!symbol("picked_high"));
 const auto overlay=read(root+"/b.sv")+"module overlay_member; endmodule\n";
 h.committed=false;h.analyzer.analyzeOpenDocuments({{root+"/b.sv",overlay,21}});
 expect("sync open-document adapter preserves ordered workspace configuration",h.committed&&symbol("overlay_member")&&symbol("picked_low")&&SemanticIndex::getInstance()->snapshot()->rawDiagnosticCount()==0);
 h.committed=false;h.analyzer.analyzeFileContentAsync(root+"/b.sv",overlay+"module async_member; endmodule\n",22);
 expect("async overlay publishes through common terminal signal",waitFor([&]{return h.committed&&!h.analyzer.hasWorkspaceAnalysisInFlight();})&&symbol("async_member")&&symbol("picked_low"));
 const auto temp=root+"/outside/temp.sv";h.committed=false;
 h.analyzer.analyzeStandaloneFileContentAsync(temp,"module temporary_member; endmodule\n",7);
 expect("standalone adapter preserves out-of-scope publication",waitFor([&]{return h.committed&&!h.analyzer.hasWorkspaceAnalysisInFlight();})&&symbol("temporary_member")&&symbol("async_member"));
 expect("standalone text cannot contaminate workspace reuse",h.run(p)&&!symbol("temporary_member")&&!symbol("async_member")&&symbol("FROM_PREVIOUS"));
 h.analyzer.analyzeFileContent(root+"/b.sv",QStringLiteral(""),23);
 expect("empty synchronous file content is a real replacement",!symbol("b")&&symbol("picked_low"));
}
void rawDiagnostics(const QString& root){
 auto p=single(root,"diagnostics");QString text="module diagnostics; logic y;\n";for(int i=0;i<12;++i)text+=QString("assign y = undefined_%1;\n").arg(i);text+="endmodule\n";write(p.systemVerilogFiles.first(),text);
 Harness h;expect("diagnostic baseline commits",h.run(p));const auto before=SemanticIndex::getInstance()->snapshot();const auto count=before->rawDiagnosticCount();
 expect("display cap retains complete raw diagnostic ownership",count>2&&before->diagnostics().size()==2&&before->suppressedDiagnosticCount()==count-2);
 const auto file=p.systemVerilogFiles.first();const auto edited=QString("// prefix\n\n")+text;
 expect("trivia save remaps capped and uncapped diagnostics without Slang",h.run(p,{{file,edited}},SemanticAnalysisReason::Save,file)&&!h.slang);
 const auto after=SemanticIndex::getInstance()->snapshot();bool mapped=after->rawDiagnosticCount()==count&&before->rawDiagnosticCount()==count;
 if(mapped)for(int i=0;i<count;++i)mapped &= after->rawDiagnostics().at(i).line==before->rawDiagnostics().at(i).line+2;
 expect("suppressed diagnostic source ranges advance with the same publication",mapped);
 p.systemVerilogFiles.clear();p.allFiles.clear();expect("deleted file cannot be revived by old raw diagnostics",h.run(p)&&SemanticIndex::getInstance()->snapshot()->rawDiagnosticCount()==0);
}
void configurationActivation(const QString& root){
 const auto a=single(root+"/a","a"),b=single(root+"/b","b");WorkspaceConfigurationService service;
 auto configuration=service.defaultConfiguration(a.workspaceRoot);configuration.defines={{"BEFORE","1"}};expect("initial project configuration saved",service.save(configuration));
 WorkspaceManager manager;manager.setRecentWorkspacePersistenceEnabledForTesting(false);
 expect("actual workspace A discovers files",manager.openWorkspace(a.workspaceRoot)&&waitFor([&]{return !manager.isWorkspaceScanActive();}));
 expect("actual workspace B discovers files",manager.openWorkspace(b.workspaceRoot)&&waitFor([&]{return !manager.isWorkspaceScanActive();}));
 configuration=service.load(a.workspaceRoot);configuration.defines={{"AFTER","2"}};configuration.topModule="a";expect("external project configuration saved",service.save(configuration));
 expect("reactivation reloads external configuration atomically",manager.switchWorkspace(0)&&waitFor([&]{return !manager.isWorkspaceScanActive();})&&manager.projectSnapshot().defines==configuration.defines&&manager.projectSnapshot().topModule=="a");
 manager.switchWorkspace(1);waitFor([&]{return !manager.isWorkspaceScanActive();});expect("external configuration removed",service.clear(a.workspaceRoot));
 expect("removing configuration does not revive cached settings",manager.switchWorkspace(0)&&waitFor([&]{return !manager.isWorkspaceScanActive();})&&manager.projectSnapshot().defines.isEmpty()&&manager.projectSnapshot().topModule.isEmpty());
}
void cancellation(const QString& root){
 const QString file=root+"/cancel.sv";QString source="module cancel;\n";for(int i=0;i<50000;++i)source+=QString("logic signal_%1;\n").arg(i);source+="endmodule\n";
 TSDocument doc;int checksInTree=0;QElapsedTimer timer;timer.start();
 expect("Tree-sitter full dependency parse obeys cancellation callback",!doc.setText(source,[&]{return ++checksInTree>=8;})&&checksInTree>=8&&timer.elapsed()<2000);
 SemanticAnalysisRequest r;r.sourceOverrides={{file,source}};SemanticInputCapture capture(r);SlangManager slang;int checksInSlang=0;timer.restart();
 auto parsed=slang.analyzeCapturedWorkspace(capture,{file},{root},{},{},[&]{return ++checksInSlang>=20;});
 expect("Slang parser unwinds cooperatively inside a source",parsed.cancelled&&checksInSlang>=20&&timer.elapsed()<2000);
 r.sourceOverrides={{file,"module cancel; function automatic int f(); int x=0; for(int i=0;i<1000000;i++) x+=i; return x; endfunction localparam P=f(); endmodule\n"}};SemanticInputCapture evalCapture(r);int checksInEval=0;timer.restart();
 auto evaluated=slang.analyzeCapturedWorkspace(evalCapture,{file},{root},{},{},[&]{return ++checksInEval>=30;});
 expect("Slang constant evaluation unwinds cooperatively",evaluated.cancelled&&checksInEval>=30&&timer.elapsed()<2000);
 Harness h;ProjectSnapshot p;p.workspaceRoot=root;p.systemVerilogFiles={file};p.allFiles={file};
 std::atomic<int> starts{0};h.analyzer.setWorkspaceWorkerStartGateForTesting([&](const auto&){++starts;});
 SemanticAnalysisRequest old;old.generation=9000;old.reason=SemanticAnalysisReason::WorkspaceOpen;old.project=p;old.sourceOverrides={{file,source}};old.changedFiles={file};
 h.analyzer.startSemanticAnalysisAsync(old);expect("obsolete worker actually starts",waitFor([&]{return starts.load()>0;}));QTest::qWait(30);
 timer.restart();for(int i=1;i<=8;++i){auto next=old;next.generation+=i;next.sourceOverrides={{file,QString("module latest_%1; endmodule\n").arg(i)}};h.analyzer.startSemanticAnalysisAsync(next);}
 expect("bounded latest-request slot converges without obsolete publication",waitFor([&]{return h.terminal==9008&&h.committed&&!h.analyzer.hasWorkspaceAnalysisInFlight();},5000)&&symbol("latest_8")&&!symbol("latest_1")&&starts.load()<=3);
 std::printf("latest_request_ready_ms=%lld worker_starts=%d\n",timer.elapsed(),starts.load());timer.restart();h.analyzer.shutdown();expect("cancelled work and retained results drain on shutdown",timer.elapsed()<2000&&h.analyzer.pendingPublicationRetirementsForTesting()==0);
}

bool blockingTerminalLifecycle(const QString& root, const QString& mode)
{
    SymbolAnalyzer analyzer;
    const bool pending = mode == "pending-shutdown";
    const bool workerBoundary = mode == "cancel" || mode == "shutdown"
        || mode == "supersede" || pending;
    std::atomic<int> started{0};
    std::atomic<bool> release{false};
    if (workerBoundary) {
        analyzer.setWorkspaceWorkerStartGateForTesting([&](const auto& cancelled) {
            if (++started != 1)
                return;
            while (!release.load() && !cancelled())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        });
    }
    QHash<quint64, int> dropped, committed, failed;
    QObject::connect(&analyzer, &SymbolAnalyzer::semanticAnalysisDropped,
        [&](const auto& request, auto) { ++dropped[request.generation]; });
    QObject::connect(&analyzer, &SymbolAnalyzer::semanticAnalysisCommitted,
        [&](const auto& request, const auto&) { ++committed[request.generation]; });
    QObject::connect(&analyzer, &SymbolAnalyzer::semanticAnalysisFailed,
        [&](const auto& request, const auto&) { ++failed[request.generation]; });

    bool actionReturned = false, watchdogFired = false;
    auto stop = [&] {
        release.store(true);
        if (mode == "cancel") analyzer.cancelAllAnalysesAndWait();
        else analyzer.shutdown();
        actionReturned = true;
    };
    QTimer action;
    if (pending) {
        analyzer.analyzeFileContentAsync(root + "/first.sv", "module first; endmodule\n", 1);
        analyzer.startSemanticAnalysisAsync(analyzer.documentRequest(
            {{root + "/queued.sv", "module queued; endmodule\n", 1}}, false));
        QObject::connect(&analyzer, &SymbolAnalyzer::semanticAnalysisDropped,
            [&](const auto& request, auto) {
                if (request.generation == 2) {
                    expect("pending shutdown owns the queued synchronous request",
                           analyzer.pendingCompatibilityRequest.has_value());
                    stop();
                }
            });
    } else if (workerBoundary) {
        QObject::connect(&action, &QTimer::timeout, [&] {
            if (!started.load()) return;
            action.stop();
            if (mode == "supersede") {
                analyzer.startSemanticAnalysisAsync(analyzer.documentRequest(
                    {{root + "/sync.sv", "module latest_terminal; endmodule\n", 2}}, false));
                actionReturned = true;
            } else {
                stop();
            }
        });
        action.start(1);
    } else if (mode == "publication-shutdown") {
        QObject::connect(&analyzer, &SymbolAnalyzer::semanticAnalysisTelemetry,
            [&](const auto& telemetry) {
                if (telemetry.stage == SemanticAnalysisStage::Worker) {
                    expect("publication shutdown occurs after worker handoff",
                           !analyzer.workspaceAnalysisWatcher && analyzer.pendingWorkspacePublication);
                    stop();
                }
            });
    } else if (mode == "observer-shutdown") {
        QObject::connect(&analyzer, &SymbolAnalyzer::semanticAnalysisTelemetry,
            [&](const auto& telemetry) {
                if (telemetry.stage == SemanticAnalysisStage::Publication)
                    stop();
            });
    } else {
        QObject::connect(&analyzer, &SymbolAnalyzer::semanticAnalysisCommitted,
                         &analyzer, [&](const auto&, const auto&) { stop(); });
    }
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, [&] {
        watchdogFired = true;
        release.store(true);
        analyzer.cancelAllAnalysesAndWait();
        QCoreApplication::exit(99); // Test escape only; never a product timeout.
    });
    watchdog.start(3000);
    const auto before = SemanticIndex::getInstance()->snapshotToken();
    QElapsedTimer elapsed;
    elapsed.start();
    analyzer.analyzeFileContent(root + "/sync.sv", "module synchronous_terminal; endmodule\n", 1);
    watchdog.stop();
    action.stop();
    release.store(true);
    const auto generation = pending ? 3u : 1u;
    expect("blocking compatibility call returns through its terminal, without watchdog",
           actionReturned && !watchdogFired && elapsed.elapsed() < 3000);
    if (mode == "supersede") {
        expect("replacement request still commits after synchronous predecessor returns",
               waitFor([&] { return committed.value(2) == 1 && !analyzer.hasWorkspaceAnalysisInFlight(); })
                   && symbol("latest_terminal") && !symbol("synchronous_terminal"));
    } else if (workerBoundary || mode == "publication-shutdown") {
        expect("cancelled request never installs a snapshot",
               SemanticIndex::getInstance()->snapshotToken().revision == before.revision);
    }
    analyzer.shutdown();
    QTest::qWait(10); // A queued finished notification must not send a second terminal.
    expect("accepted synchronous request receives exactly one terminal",
           dropped.value(generation) + committed.value(generation) + failed.value(generation) == 1
               && failed.value(generation) == 0
               && committed.value(generation) == (mode == "committed-shutdown" ? 1 : 0));
    if (pending)
        expect("shutdown completes active and pending requests independently",
               dropped.value(1) == 1 && dropped.value(2) == 1);
    expect("terminal notification teardown drains owned results",
           analyzer.pendingPublicationRetirementsForTesting() == 0);
    std::printf("terminal_mode=%s worker_starts=%d dropped=%d committed=%d watchdog=%d elapsed_ms=%lld\n",
                qPrintable(mode), started.load(), dropped.value(generation), committed.value(generation),
                watchdogFired, elapsed.elapsed());
    return !watchdogFired;
}
}
int main(int argc,char**argv){
 std::setvbuf(stdout,nullptr,_IONBF,0);QApplication app(argc,argv);QTemporaryDir temp,profile;if(!temp.isValid()||!profile.isValid())return 2;
 QCoreApplication::setApplicationName("ZeroSlack-Semantic-Contract");QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,profile.path());QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,profile.path());qputenv("ZEROSLACK_SESSION_STORAGE_PATH",(profile.path()+"/sessions.ini").toUtf8());
 retainedInputs(temp.path()+"/retained");SemanticIndex::getInstance()->clearSemanticState();EffectiveValueService::getInstance()->clearPublishedFacts();
 includesAndAdapters(temp.path()+"/adapters");SemanticIndex::getInstance()->clearSemanticState();EffectiveValueService::getInstance()->clearPublishedFacts();
 rawDiagnostics(temp.path()+"/raw");SemanticIndex::getInstance()->clearSemanticState();EffectiveValueService::getInstance()->clearPublishedFacts();
 configurationActivation(temp.path()+"/configuration");cancellation(temp.path()+"/cancellation");
 for (const auto& mode : {"cancel", "shutdown", "supersede", "pending-shutdown",
                          "publication-shutdown", "observer-shutdown", "committed-shutdown"}) {
     if (!blockingTerminalLifecycle(temp.path()+"/terminal", QString::fromLatin1(mode))) break;
 }
 std::printf("%d checks, %d failures\n",checks,failures);return failures?1:0;
}
