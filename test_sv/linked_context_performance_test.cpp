#include "linkedcontextcontroller.h"
#include "linkednativerails.h"
#include "mycodeeditor.h"
#include "pinloomcodelinkstore.h"
#include "pinloomhostclient.h"
#include "tabmanager.h"
#include "testuistyle.h"
#include "webeditorcontainer.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMainWindow>
#include <QScrollBar>
#include <QSettings>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>
#include <QtTest>
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#endif

namespace {
QString digest(const QByteArray& bytes) { return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()); }
bool write(const QString& path,const QByteArray& bytes) {
    if(!QDir().mkpath(QFileInfo(path).absolutePath()))return false;
    QFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size();
}
QJsonObject memory() {
    QJsonObject result;
#ifdef Q_OS_WIN
    struct Identity {DWORD parent=0;QString name;quint64 created=0;};
    const auto creation=[](HANDLE process){FILETIME c{},e{},k{},u{};return GetProcessTimes(process,&c,&e,&k,&u)?(quint64(c.dwHighDateTime)<<32)|c.dwLowDateTime:quint64(0);};
    const auto processMemory=[&](DWORD pid,DWORD parent,const QString& name){
        QJsonObject row{{"pid",int(pid)},{"parentPid",int(parent)},{"name",name}};
        HANDLE h=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,pid);
        if(!h){row["queryError"]=int(GetLastError());return row;}
        row["createdFileTime"]=QString::number(creation(h));
        PROCESS_MEMORY_COUNTERS_EX c{};c.cb=sizeof(c);
        if(GetProcessMemoryInfo(h,reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&c),sizeof(c))){
            row["workingSetBytes"]=double(c.WorkingSetSize);row["privateBytes"]=double(c.PrivateUsage);
        }else row["memoryError"]=int(GetLastError());
        CloseHandle(h);return row;
    };
    const DWORD root=GetCurrentProcessId();QMap<DWORD,Identity> inventory,owned;
    owned[root]={0,QStringLiteral("test-host"),creation(GetCurrentProcess())};
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot!=INVALID_HANDLE_VALUE){PROCESSENTRY32W e{};e.dwSize=sizeof(e);
        if(Process32FirstW(snapshot,&e))do{inventory[e.th32ProcessID]={e.th32ParentProcessID,QString::fromWCharArray(e.szExeFile),0};}while(Process32NextW(snapshot,&e));
        CloseHandle(snapshot);
    }
    QJsonArray children;bool changed=true;double childBytes=0;
    while(changed){changed=false;for(auto it=inventory.cbegin();it!=inventory.cend();++it){
        if(owned.contains(it.key())||!owned.contains(it->parent))continue;
        const auto row=processMemory(it.key(),it->parent,it->name);
        const auto created=row["createdFileTime"].toString().toULongLong();
        if(!created||created<owned.value(it->parent).created)continue;
        owned[it.key()]={it->parent,it->name,created};children.append(row);
        childBytes+=row["workingSetBytes"].toDouble();changed=true;
    }}
    result={{"host",processMemory(root,inventory.value(root).parent,"test-host")},{"ownedChildren",children},
        {"ownedChildCount",children.size()},{"ownedChildWorkingSetBytes",childBytes},
        {"attribution","Snapshot parent chain rooted at this host, each child creation >= its observed parent creation; independent per-process profile. Other machine WebView2 processes excluded."}};
#endif
    return result;
}
struct Fixture {
    QTemporaryDir temp{QDir::tempPath()+"/lp-XXXXXX"};
    QMainWindow window;
    QStackedWidget* stack=new QStackedWidget(&window);
    QWidget* page=new QWidget(stack);
    QTabWidget* tabWidget=new QTabWidget(page);
    PinloomCodeLinkStore links;
    std::unique_ptr<TabManager> tabs;
    std::unique_ptr<PinloomHostClient> client;
    std::unique_ptr<LinkedContextController> controller;
    QString file,text;int pending=0,requests=0;bool ready=false;
    explicit Fixture(int count) {
        if(!temp.isValid())return;
        file=temp.filePath("fixture.sv");text="module linked_perf;\n";
        for(int i=0;i<count;++i)text+=QString("logic signal_%1; // fixed reference input\n").arg(i,4,10,QChar('0'));
        text+="endmodule\n";if(!write(file,text.toUtf8()))return;
        window.setCentralWidget(stack);stack->addWidget(page);page->setObjectName("performanceEditorPage");
        auto* layout=new QVBoxLayout(page);layout->setContentsMargins(0,0,0,0);layout->addWidget(tabWidget);
        tabs=std::make_unique<TabManager>(tabWidget);tabs->setWorkspaceScope({temp.path()},temp.path());links.setWorkspaceRoot(temp.path());
        QJsonArray anchors;
        for(int i=0;i<count;++i){
            const QString id=QString("a%1").arg(i,4,10,QChar('0')),needle=QString("signal_%1").arg(i,4,10,QChar('0'));
            const int at=text.indexOf(needle);auto source=PinloomSourceSelection::fromDocumentSelection(temp.path(),file,"linked_perf",text,at,at+needle.size());source.anchorId=id;
            anchors.append(QJsonObject{{"id",id},{"source",QJsonObject::fromVariantMap(source.toVariantMap())},
                {"links",QJsonArray{QJsonObject{{"id","reference"},{"title",id},{"uri","pinloom://entry/perf-"+id},{"identity",QJsonObject{{"entryId","perf-"+id}}}}}}});
        }
        if(!write(links.storagePath(),QJsonDocument(QJsonObject{{"schema","ZeroSlack.PinloomCodeLinks"},{"version",2},{"anchors",anchors}}).toJson())||!links.reload())return;
        client=std::make_unique<PinloomHostClient>([this](const QJsonObject& request,PinloomHostClient::RawReplyHandler reply){
            ++pending;++requests;const auto identity=request["params"].toObject()["identity"].toObject();
            QTimer::singleShot(20,client.get(),[this,identity,reply=std::move(reply)]{
                --pending;reply({{"entry",QJsonObject{{"identity",identity},{"type","text"},{"title",identity["entryId"].toString()},{"location","Performance task fixture"}}},
                    {"content","Identical controlled plain-text reference for both arms."},{"contentType","text/plain"}},{});
            });
        });
        if(!tabs->openFileInTab(file))return;
        editor()->setFont(QFont("Consolas",10));editor()->setLineWrapMode(QPlainTextEdit::NoWrap);
        editor()->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);editor()->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        controller=std::make_unique<LinkedContextController>(&window,stack,page,tabs.get(),&links,client.get(),QList<QWidget*>{});
        window.resize(1440,800);window.show();QApplication::processEvents();ready=true;
    }
    ~Fixture(){controller.reset();tabs.reset();client.reset();window.close();}
    MyCodeEditor* editor() const{return tabs->getCurrentEditor();}
    QJsonObject state() const{return controller->viewSnapshot();}
    bool resourcesComplete() const {
        const auto resources=state()["resources"].toArray();if(pending||resources.isEmpty())return false;
        for(const auto& r:resources)if(!r.toObject()["complete"].toBool()||!r.toObject()["error"].toString().isEmpty())return false;
        return true;
    }
};
}

class LinkedContextPerformanceTest:public QObject {
    Q_OBJECT
public:
    QJsonObject report;
    QJsonArray events,requests;
    QElapsedTimer clock;
    QSet<QString> renderedSources;
    double now() const{return clock.nsecsElapsed()/1e6;}
    void mark(const QString& operation,int request=0){qApp->setProperty("g7Meta",QVariant::fromValue(QJsonObject{{"operation",operation},{"request",request},{"hostRequestMs",now()}}));}
public slots:
    void acceptProbe(const QJsonObject& event){auto value=event;value["hostObservedMs"]=now();events.append(value);}
private:
    bool applied(const QString& operation,int request,bool native,const QString& webType="geometry") const {
        bool layout=false,processed=false;
        for(auto it=events.end();it!=events.begin();){const auto e=(*--it).toObject(),meta=e["meta"].toObject();
            if(meta["operation"]!=operation||meta["request"].toInt()!=request)continue;
            if(native&&e["kind"]=="native-layout")layout=true;
            if(native&&e["kind"]=="scope-end"&&e["scope"]==(webType=="references"?"resolve":"geometry"))processed=true;
            if(!native&&e["kind"]=="web-applied"&&e["messageType"]==webType)return true;
        }return native&&layout&&processed;
    }
    QJsonObject metric(const QString& operation,double started,double completed) const {
        QJsonArray selected;for(const auto& e:events)if(e.toObject()["meta"].toObject()["operation"]==operation)selected.append(e);
        return {{"startedHostMs",started},{"completedHostMs",completed},{"elapsedMs",completed-started},{"events",selected}};
    }
private slots:
    void boundedTrial(){
        clock.start();qApp->setProperty("g7EventSink",QVariant::fromValue<QObject*>(this));
        const int n=qEnvironmentVariableIntValue("ZEROSLACK_LINKED_PERFORMANCE_N"),trial=qEnvironmentVariableIntValue("ZEROSLACK_LINKED_PERFORMANCE_TRIAL");
        const bool native=qgetenv("ZEROSLACK_LINKED_NATIVE_RAILS")=="1";
        report={{"schema","zeroslack-linked-performance/v1"},{"arm",native?"native":"web"},{"anchorCount",n},{"trial",trial},
            {"platform",QGuiApplication::platformName()},{"windowLogical",QJsonArray{1440,800}},{"font","Consolas 10"},
            {"profile",qEnvironmentVariable("ZEROSLACK_PROFILE_ROOT")},{"debounceMs",QJsonObject{{"geometry",20},{"anchors",180}}},
            {"resourceCondition","Every currently demanded resource complete; Web additionally acknowledges consumed preview after Lit updates and two animation frames."},
            {"instrumentation","Task-only controller scope/post observers and Web consumption/view/paint acknowledgements; raw durations include observer overhead. No speedup claim."}};
        QVERIFY(n==50||n==200||n==1000);QVERIFY(trial>=1&&trial<=3);
        QCOMPARE(QGuiApplication::platformName(),QString("windows"));
        Fixture f(n);QVERIFY(f.ready);report["sourceSha256"]=digest(f.text.toUtf8());report["sourceBytes"]=f.text.toUtf8().size();
        report["fixtureFile"]=f.file;report["dpr"]=f.editor()->devicePixelRatioF();
        connect(f.editor(),&QPlainTextEdit::updateRequest,this,[this](const QRect& r,int dy){acceptProbe({{"kind","updateRequest"},{"meta",qApp->property("g7Meta").toJsonObject()},{"dy",dy},{"rect",QJsonArray{r.x(),r.y(),r.width(),r.height()}}});});
        connect(f.editor(),&QPlainTextEdit::textChanged,this,[this]{acceptProbe({{"kind","textChanged"},{"meta",qApp->property("g7Meta").toJsonObject()}});});
        report["beforeEntryMemory"]=memory();mark("entry");const double enterStart=now();
        QVERIFY(f.controller->setActive(true));const double enterReturned=now();
        auto* rails=f.window.findChild<LinkedNativeRails*>();auto* web=f.window.findChild<WebEditorContainer*>();
        QCOMPARE(bool(rails),native);QCOMPARE(bool(web),!native);
        if(rails)connect(rails,&LinkedNativeRails::layoutApplied,this,[this](quint64 serial,qint64 revision){acceptProbe({{"kind","native-layout"},{"meta",qApp->property("g7Meta").toJsonObject()},{"serial",qint64(serial)},{"revision",revision}});});
        if(web)connect(web,&WebEditorContainer::messageReceived,this,[this](const QJsonObject& m){
            if(m["type"]=="g7-applied"){auto e=m;e["kind"]="web-applied";acceptProbe(e);if(m["messageType"]=="preview")renderedSources.insert(m["source"].toString());}
            if(m["type"]=="error")acceptProbe({{"kind","web-error"},{"message",m}});
        });
        const auto previewsReady=[&]{if(native)return true;for(const auto& r:f.state()["resources"].toArray())if(!renderedSources.contains(r.toObject()["source"].toString()))return false;return true;};
        QVERIFY(QTest::qWaitFor([&]{return f.state()["initialized"].toBool()&&f.state()["references"].toArray().size()==n&&f.resourcesComplete()&&previewsReady()
            &&(native||applied("entry",0,false,"initialize"));},60000));
        report["entry"]=metric("entry",enterStart,now());report["entrySetActiveCallMs"]=enterReturned-enterStart;
        report["loadedMemory"]=memory();report["loadedState"]=f.state();
        auto* bar=f.editor()->verticalScrollBar();QVERIFY(bar->maximum()>2);
        mark("single-update",1);const double single=now();bar->setValue(bar->value()+1);
        QVERIFY(QTest::qWaitFor([&]{return applied("single-update",1,native);},60000));
        report["singleUpdate"]=metric("single-update",single,now());
        const double scrollStart=now();int sent=0;QTimer cadence;cadence.setTimerType(Qt::PreciseTimer);cadence.setInterval(8);
        connect(&cadence,&QTimer::timeout,this,[&]{
            ++sent;mark("scroll-500",sent);int target=(sent*7)%(bar->maximum()+1);if(target==bar->value())target=(target+1)%(bar->maximum()+1);
            const double began=now();bar->setValue(target);requests.append(QJsonObject{{"request",sent},{"hostMs",began},{"target",target},{"dispatchMs",now()-began}});
            if(sent==500)cadence.stop();
        });cadence.start();
        QVERIFY(QTest::qWaitFor([&]{return sent==500&&applied("scroll-500",500,native);},60000));
        auto scroll=metric("scroll-500",scrollStart,now());scroll["requestedCadenceMs"]=8;scroll["requests"]=requests;
        QMap<int,double> firstCompletion;int completionEvents=0;
        for(const auto& value:scroll["events"].toArray()){const auto e=value.toObject();
            if((native&&e["kind"]=="scope-end"&&e["scope"]=="geometry")||(!native&&e["kind"]=="web-applied"&&e["messageType"]=="geometry")){
                ++completionEvents;const int request=e["meta"].toObject()["request"].toInt();if(!firstCompletion.contains(request))firstCompletion[request]=e["hostObservedMs"].toDouble();
            }}
        double maxDispatch=0,maxCompletedLatency=0;QJsonArray completed;
        for(const auto& value:requests){const auto r=value.toObject();maxDispatch=qMax(maxDispatch,r["dispatchMs"].toDouble());const int id=r["request"].toInt();
            if(firstCompletion.contains(id)){const double latency=firstCompletion[id]-r["hostMs"].toDouble();maxCompletedLatency=qMax(maxCompletedLatency,latency);completed.append(QJsonObject{{"request",id},{"latencyMs",latency}});}}
        scroll["completionEvents"]=completionEvents;scroll["distinctCompletedRequestIds"]=completed;scroll["distinctCompletedCount"]=completed.size();
        scroll["requestsWithoutOwnCompletion"]=500-completed.size();scroll["maximumCompletedRequestLatencyMs"]=maxCompletedLatency;scroll["maximumSynchronousDispatchMs"]=maxDispatch;
        scroll["coalescingNote"]="Completions are associated with the latest request carried through the actual controller chain. Requests without their own completion were coalesced; no claim of 500 independent layouts.";report["scroll500"]=scroll;
        mark("text-change",1);const double edit=now();QTextCursor cursor(f.editor()->document());cursor.movePosition(QTextCursor::End);cursor.insertText("// task performance edit\n");
        const auto revision=qint64(f.editor()->semanticDocumentRevision());
        QVERIFY(QTest::qWaitFor([&]{return native?(f.state()["rails"].toObject()["appliedContentRevision"].toInteger()==revision&&applied("text-change",1,true,"references"))
            :applied("text-change",1,false,"references");},60000));
        report["textChange"]=metric("text-change",edit,now());report["expectedContentRevision"]=revision;
        report["beforeExitMemory"]=memory();mark("exit");const double exitStart=now();QVERIFY(f.controller->setActive(false));
        QVERIFY(!f.controller->isActive());QVERIFY(!f.window.findChild<LinkedNativeRails*>());QVERIFY(!f.window.findChild<WebEditorContainer*>());
        report["exit"]=metric("exit",exitStart,now());report["afterExitMemory"]=memory();report["resourceRequests"]=f.requests;
        report["complete"]=true;
    }
};

int main(int argc,char** argv){
    QApplication app(argc,argv);QCoreApplication::setOrganizationName("ZeroSlackLinkedPerformanceTest");QCoreApplication::setApplicationName("Stage2");
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,qEnvironmentVariable("ZEROSLACK_PROFILE_ROOT"));
    if(!initializeUiStyleForTest())return 3;
    LinkedContextPerformanceTest test;const int code=QTest::qExec(&test,argc,argv);
    test.report["exitCode"]=code;test.report["rawEvents"]=test.events;
    if(!write(qEnvironmentVariable("ZEROSLACK_LINKED_PERFORMANCE_OUT"),QJsonDocument(test.report).toJson()))return 4;
    return code;
}
#include "linked_context_performance_test.moc"
