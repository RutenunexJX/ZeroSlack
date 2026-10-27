#include "signal_trace_test_fixture.h"
#include <QComboBox>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTreeWidget>
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

namespace {
QJsonObject memory(){
    QJsonObject result;
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX counters{};counters.cb=sizeof(counters);
    if(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),sizeof(counters))){
        result["workingSetBytes"]=double(counters.WorkingSetSize);
        result["peakWorkingSetBytes"]=double(counters.PeakWorkingSetSize);
        result["privateBytes"]=double(counters.PrivateUsage);
    }
#endif
    return result;
}
}

// Same generated production input per trial. No repository examples or formal profile.
class SignalTracePerformanceTest:public QObject{
    Q_OBJECT
private slots:
    void boundedExploration(){
        constexpr int fanout=1600;
        QString source="module top(input logic source, output wire result);\nwire middle;assign middle=source;\n";
        for(int i=0;i<fanout;++i)source+=QString("wire n%1;assign n%1=middle;\n").arg(i);
        source+="assign result=n1599;\nendmodule\n";
        QJsonArray trials;
        for(int trial=0;trial<3;++trial){
            QJsonObject row{{"trial",trial+1},{"before",memory()}};QElapsedTimer timer;
            SignalTraceTestFixture fixture;timer.start();QVERIFY(fixture.build(source));
            row["productionAnalysisMs"]=timer.nsecsElapsed()/1e6;
            timer.restart();auto service=std::make_shared<SignalTraceService>(fixture.context);
            const auto resolved=service->resolve({},"middle",fixture.file,"top");QVERIFY(resolved.object.isConcrete());
            row["resolveMs"]=timer.nsecsElapsed()/1e6;
            timer.restart();const auto first=service->oneHop(resolved.object,SignalTraceDirection::Downstream);
            row["serviceFirstPageMs"]=timer.nsecsElapsed()/1e6;
            QVERIFY(first.nextCursor>0);QCOMPARE(first.groups.size(),100);
            timer.restart();const auto repeated=service->oneHop(resolved.object,SignalTraceDirection::Downstream);
            row["serviceRepeatedPageMs"]=timer.nsecsElapsed()/1e6;QCOMPARE(repeated.groups.size(),first.groups.size());
            int cursor=0,objects=0,pages=0;QSet<QString> ids;timer.restart();
            do{const auto page=service->oneHop(resolved.object,SignalTraceDirection::Downstream,cursor);
                for(const auto& group:page.groups)if(group.dataPath)ids.insert(group.object.id());
                objects+=page.groups.size();cursor=page.nextCursor;++pages;
            }while(cursor>=0 && pages<40);
            row["allPagesMs"]=timer.nsecsElapsed()/1e6;row["pages"]=pages;row["groups"]=objects;
            QCOMPARE(ids.size(),fanout);QCOMPARE(cursor,-1);
            auto panel=std::make_unique<SignalTracePanel>();panel->resize(1120,700);panel->setContext(fixture.context);panel->show();
            const auto settle=[&]{return QTest::qWaitFor([&]{return !panel->busy();},20000);};
            timer.restart();panel->showObject(resolved.object);QVERIFY(settle());
            row["panelFirstResultMs"]=timer.nsecsElapsed()/1e6;
            panel->findChild<QComboBox*>("traceDirection")->setCurrentIndex(1);
            auto* tree=panel->traceTree(SignalTraceDirection::Downstream);auto* root=tree->topLevelItem(0);QVERIFY(root && root->childCount()>0);
            timer.restart();root->setExpanded(false);root->setExpanded(true);QApplication::processEvents();
            row["collapseReexpandMs"]=timer.nsecsElapsed()/1e6;QVERIFY(!panel->busy());
            timer.restart();panel->showForSymbol("source",fixture.file,"top");QVERIFY(settle());
            panel->goBack();QVERIFY(settle());row["switchAndBackMs"]=timer.nsecsElapsed()/1e6;
            QCOMPARE(panel->currentObject().id(),resolved.object.id());
            timer.restart();panel->expandCone(4);panel->cancel();QApplication::processEvents();
            row["cancelVisibleMs"]=timer.nsecsElapsed()/1e6;QVERIFY(!panel->busy());
            row["loaded"]=memory();
            timer.restart();panel.reset();row["destroyAndDrainMs"]=timer.nsecsElapsed()/1e6;row["after"]=memory();
            trials.append(row);
        }
        const QJsonObject result{{"schema","zeroslack-trace-performance/v1"},{"platform",QGuiApplication::platformName()},
            {"fanout",fanout},{"sourceSha256",QString::fromLatin1(QCryptographicHash::hash(source.toUtf8(),QCryptographicHash::Sha256).toHex())},
            {"sourceBytes",source.toUtf8().size()},{"trials",trials},
            {"measurement","In-process production analysis and native Qt event completion (QTest::qWaitFor polling). Trial 1 is process-cold; subsequent trials reuse OS/runtime caches. Service repeat uses the same immutable snapshot and bounded page cache. Offscreen timing excludes desktop composition. No historical baseline or speedup claim."}};
        QFile output(qEnvironmentVariable("ZEROSLACK_TRACE_PERFORMANCE_OUT"));QVERIFY(output.open(QIODevice::WriteOnly));
        output.write(QJsonDocument(result).toJson());
    }
};
int main(int argc,char**argv){return runSignalTraceWidgetTest<SignalTracePerformanceTest>(argc,argv);}
#include "signal_trace_performance_test.moc"
