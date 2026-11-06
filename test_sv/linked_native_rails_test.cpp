#include "linkedcontextcontroller.h"
#include "linkedcontextstate.h"
#include "linkedanchorgeometry.h"
#include "linkednativerails.h"
#include "linkedconnectoroverlay.h"
#include "mycodeeditor.h"
#include "editorfoldviewstate.h"
#include "pinloomcodelinkstore.h"
#include "pinloomhostclient.h"
#include "tabmanager.h"
#include "testuistyle.h"
#include "webeditorcontainer.h"
#include <QAbstractTextDocumentLayout>
#include <QAbstractAnimation>
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFrame>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QStyleOptionSlider>
#include <QtTest>
#include <algorithm>

namespace {
constexpr auto flag = "ZEROSLACK_LINKED_NATIVE_RAILS";
struct Environment {
    bool existed = qEnvironmentVariableIsSet(flag);
    QByteArray old = qgetenv(flag);
    ~Environment() { if (existed) qputenv(flag, old); else qunsetenv(flag); }
};
bool writeFile(const QString& path, const QByteArray& bytes) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray readFile(const QString& path) {
    QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
QString sourceUri(const QString& entry) {
    PinloomHostIdentity identity; identity.entryId = entry;
    return identity.toUri().toString(QUrl::FullyEncoded);
}
QJsonObject savedState(double ratio = .5, QJsonArray expanded = {}) {
    return {{"version",2},{"codeRatio",ratio},{"referenceIds",QJsonArray{}},
        {"expandedReferences",expanded},{"unknownStage1Sentinel","preserve me"},
        {"native",QJsonObject{{"format","zeroslack.linked-board"},{"engine","blocksuite-0.22.4"},
            {"version",1},{"workspaceId","task"},{"documentId","document"},{"attachments",QJsonArray{}},
            {"root","AA=="},{"state","AA=="},{"unknownNative",17}}},
        {"viewport",QJsonObject{{"zoom",1},{"centerX",72},{"centerY",31}}},
        {"documentViews",QJsonObject{}}};
}
struct Harness {
    struct Pending { QJsonObject request; PinloomHostClient::RawReplyHandler reply; };
    QTemporaryDir temp{QDir::tempPath()+"/native-rails-XXXXXX"};
    QMainWindow window;
    QStackedWidget* stack = new QStackedWidget(&window);
    QWidget* page = new QWidget(stack);
    QTabWidget* tabWidget = new QTabWidget(page);
    PinloomCodeLinkStore links;
    std::unique_ptr<TabManager> tabs;
    std::unique_ptr<PinloomHostClient> client;
    std::unique_ptr<LinkedContextController> controller;
    QString file, second, text, imagePath;
    QJsonArray anchorRows;
    QList<Pending> pending;
    QMap<QString,int> requested;
    QStringList opened;
    QSet<QString> errorSources;
    bool defer = false, ready = false;

    Harness() {
        if (!temp.isValid()) return;
        file = temp.filePath("first.sv"); second = temp.filePath("second.sv");
        text = "module native_rails_fixture;\n";
        for (int i=0;i<100;++i) text += QString("logic signal_%1; // ").arg(i,3,10,QChar('0')) + QString(130,'x') + '\n';
        text += "// repeat_marker\n// repeat_marker\nendmodule\n";
        if (!writeFile(file,text.toUtf8()) || !writeFile(second,text.toUtf8())) return;
        imagePath=temp.filePath("preview.png");
        QImage image(80,50,QImage::Format_ARGB32);image.fill(Qt::blue);
        if (!image.save(imagePath)) return;
        window.setCentralWidget(stack); page->setObjectName("originalEditorPage");
        auto* layout=new QVBoxLayout(page);layout->setContentsMargins(0,0,0,0);layout->addWidget(tabWidget);
        stack->addWidget(page); tabs=std::make_unique<TabManager>(tabWidget);
        tabs->setWorkspaceScope({temp.path()},temp.path());links.setWorkspaceRoot(temp.path());
        client=std::make_unique<PinloomHostClient>([this](const QJsonObject& request, PinloomHostClient::RawReplyHandler reply){
            const QString entry=request["params"].toObject()["identity"].toObject()["entryId"].toString();
            if(request["method"]=="open") {opened.append(entry);reply({{"positionVerified",true},{"state","located"}},{});return;}
            ++requested[entry];Pending item{request,std::move(reply)};
            if(defer) pending.append(std::move(item));
            else QTimer::singleShot(5,client.get(),[this,item=std::move(item)]{complete(item);});
        });
        controller=std::make_unique<LinkedContextController>(&window,stack,page,tabs.get(),&links,client.get(),QList<QWidget*>{});
        ready=tabs->openFileInTab(file); if(!ready)return;
        editor()->setFont(QFont("Consolas",10)); editor()->setLineWrapMode(QPlainTextEdit::NoWrap);
        editor()->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        editor()->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        window.resize(1440,720);window.show();QCoreApplication::processEvents();
    }
    ~Harness() { controller.reset();tabs.reset();client.reset();window.close(); }
    MyCodeEditor* editor() const { return tabs->getCurrentEditor(); }
    LinkedNativeRails* rails() const { return window.findChild<LinkedNativeRails*>(); }
    QJsonObject snapshot() const { return controller->viewSnapshot(); }
    QJsonObject railsSnapshot() const { return snapshot()["rails"].toObject(); }
    QJsonArray resources() const { return snapshot()["resources"].toArray(); }
    QJsonObject result(const QJsonObject& request) const {
        const auto identity=request["params"].toObject()["identity"].toObject();
        const auto entry=identity["entryId"].toString();
        QJsonObject value{{"entry",QJsonObject{{"identity",identity},{"title","Title "+entry},
            {"type","text"},{"location","Task fixture"},{"deleted",entry=="deleted"}}},
            {"content",entry=="empty"?QString():QString("<a href='https://example.invalid/'>literal</a> ")+entry},
            {"contentType","text/plain"}};
        if(entry=="image")value["preview"]=QJsonObject{{"kind","image"},{"state","ready"},
            {"mimeType","image/png"},{"uri",QUrl::fromLocalFile(imagePath).toString()},
            {"filePath",imagePath},{"byteSize",QFileInfo(imagePath).size()},{"pixelWidth",80},{"pixelHeight",50}};
        return value;
    }
    void complete(const Pending& item) {
        const auto entry=item.request["params"].toObject()["identity"].toObject()["entryId"].toString();
        item.reply(errorSources.contains(entry)?QJsonObject{}:result(item.request),
            errorSources.contains(entry)?QStringLiteral("Controlled unavailable reference"):QString());
    }
    void releasePending() { const auto work=pending;pending.clear();for(const auto& item:work)complete(item); }
    QJsonObject anchor(QString id,int row,QStringList entries,QString path={}) const {
        if(path.isEmpty())path=file;
        const QString needle=QString("signal_%1").arg(row,3,10,QChar('0'));
        const int at=text.indexOf(needle);
        auto source=PinloomSourceSelection::fromDocumentSelection(temp.path(),path,"native_rails_fixture",text,at,at+needle.size());
        source.anchorId=id;
        QJsonArray targets;
        for(int i=0;i<entries.size();++i)targets.append(QJsonObject{{"id",QString("link-%1").arg(i)},
            {"title",entries[i]},{"uri",sourceUri(entries[i])},{"identity",QJsonObject{{"entryId",entries[i]}}}});
        return {{"id",id},{"source",QJsonObject::fromVariantMap(source.toVariantMap())},{"links",targets}};
    }
    bool store(QJsonArray rows) {
        anchorRows=rows;
        return writeFile(links.storagePath(),QJsonDocument(QJsonObject{{"schema","ZeroSlack.PinloomCodeLinks"},{"version",2},{"anchors",rows}}).toJson())&&links.reload();
    }
    QString board(const QString& path,const QJsonObject& state) {
        LinkedContextState value;
        if(!value.load(temp.path(),path)||!value.save(state))return {};
        return value.path;
    }
    bool allResourcesComplete() const {
        const auto items=resources();if(items.isEmpty())return false;
        for(const auto& item:items)if(!item.toObject()["complete"].toBool())return false;
        return true;
    }
    QString cardState(const QString& id) const {
        for(const auto& c:railsSnapshot()["cards"].toArray())if(c.toObject()["id"]==id)return c.toObject()["state"].toString();
        return {};
    }
};
QJsonObject view(MyCodeEditor* editor) {
    const auto c=editor->textCursor();
    return {{"position",c.position()},{"anchor",c.anchor()},{"vertical",editor->verticalScrollBar()->value()},
        {"horizontal",editor->horizontalScrollBar()->value()},{"width",editor->viewport()->width()},
        {"height",editor->viewport()->height()}};
}
void expandAllReferenceGroups(Harness& h) {
    for(auto* button:h.rails()->findChildren<QPushButton*>("linkedNativeExpand"))
        if(button->isVisible()&&button->text()=="Expand references")button->click();
}
QJsonObject trackState(const Harness& h,int side) { return h.railsSnapshot()["tracks"].toArray()[side].toObject(); }
void wheelOn(QWidget* viewport,int angle,QPoint pixels={}) {
    const QPoint point=viewport->rect().center();
    QWheelEvent event(QPointF(point),QPointF(viewport->mapToGlobal(point)),pixels,QPoint(0,angle),
        Qt::NoButton,Qt::NoModifier,pixels.isNull()?Qt::NoScrollPhase:Qt::ScrollUpdate,false);
    QApplication::sendEvent(viewport,&event);
}
bool scrollFinished(QScrollBar* bar) {
    return QTest::qWaitFor([bar]{
        for(const auto* animation:bar->findChildren<QAbstractAnimation*>())
            if(animation->state()==QAbstractAnimation::Running)return false;
        return true;
    },5000);
}
QMap<QString,QPoint> cardLocations(Harness& h,int side) {
    QMap<QString,QPoint> result;
    const auto groups=h.railsSnapshot()["groups"].toArray();QSet<QString> ids;
    for(const auto& row:groups)if(row.toObject()["side"].toInt()==side)
        for(const auto& id:row.toObject()["references"].toArray())ids.insert(id.toString());
    for(auto* card:h.rails()->findChildren<QFrame*>("linkedNativeCard")) {
        const auto id=card->property("referenceId").toString();if(ids.contains(id))result[id]=card->mapTo(h.rails(),QPoint());
    }
    return result;
}
QJsonObject motion(MyCodeEditor* editor) {
    auto result=view(editor);result.remove("width");result.remove("height");return result;
}
struct MotionWatch {
    MyCodeEditor* editor;
    QJsonObject before;
    QSignalSpy cursor,selection,vertical,horizontal;
    explicit MotionWatch(MyCodeEditor* value):editor(value),before(motion(value)),
        cursor(value,&QPlainTextEdit::cursorPositionChanged),selection(value,&QPlainTextEdit::selectionChanged),
        vertical(value->verticalScrollBar(),&QScrollBar::valueChanged),horizontal(value->horizontalScrollBar(),&QScrollBar::valueChanged){}
    bool unchanged() const {return before==motion(editor)&&cursor.isEmpty()&&selection.isEmpty()&&vertical.isEmpty()&&horizontal.isEmpty();}
};
bool prepareFoldSource(Harness& h) {
    h.text="module native_rails_fixture;\nlogic clk, rst_n;\nlogic [31:0] signal_000, signal_001, signal_002, signal_003;\n"
        "always_ff @(posedge clk) begin\n    if (!rst_n) begin\n        signal_000 <= 0;\n        signal_001 <= 0;\n"
        "    end else begin\n        signal_002 <= signal_000+1;\n    end\nend\n"
        "always_comb begin\n    signal_003 = signal_002;\nend\n";
    for(int i=4;i<100;++i)h.text+=QString("logic signal_%1; // ").arg(i,3,10,QChar('0'))+QString(130,'x')+'\n';
    h.text+="endmodule\n";
    if(!writeFile(h.file,h.text.toUtf8()))return false;
    h.editor()->setPlainText(h.text);return true;
}
QJsonObject anchorAt(const Harness& h,const QString& id,const QString& needle,const QStringList& entries) {
    auto row=h.anchor(id,0,entries);const int start=h.text.indexOf(needle);
    auto source=PinloomSourceSelection::fromDocumentSelection(h.temp.path(),h.file,"native_rails_fixture",h.text,start,start+needle.size());
    source.anchorId=id;row["source"]=QJsonObject::fromVariantMap(source.toVariantMap());return row;
}
QJsonObject presentationFor(const Harness& h,const QString& member) {
    for(const auto& row:h.railsSnapshot()["presentation"].toArray())
        if(row.toObject()["members"].toArray().contains(member))return row.toObject();
    return {};
}
bool completeCoverage(const Harness& h) {
    QSet<QString> logicalAnchors,logicalReferences,shownAnchors,shownReferences;QSet<int> sides;
    for(const auto& row:h.railsSnapshot()["groups"].toArray()) {
        const auto group=row.toObject();logicalAnchors.insert(group["anchor"].toString());
        for(const auto& ref:group["references"].toArray())logicalReferences.insert(ref.toString());
    }
    for(const auto& row:h.railsSnapshot()["presentation"].toArray()) {
        const auto group=row.toObject();sides.insert(group["side"].toInt());
        for(const auto& member:group["members"].toArray()) {
            if(shownAnchors.contains(member.toString()))return false;shownAnchors.insert(member.toString());
        }
        for(const auto& ref:group["references"].toArray()) {
            if(shownReferences.contains(ref.toString()))return false;shownReferences.insert(ref.toString());
        }
    }
    return logicalAnchors==shownAnchors&&logicalReferences==shownReferences&&sides==QSet<int>({0,1});
}
QPushButton* expandButton(Harness& h,const QString& group) {
    for(auto* button:h.rails()->findChildren<QPushButton*>("linkedNativeExpand"))
        if(button->property("anchorId").toString()==group)return button;
    return nullptr;
}
QStringList differentFields(const QJsonValue& a,const QJsonValue& b,const QString& path={}) {
    QStringList result;
    if(a.isObject()&&b.isObject()) {
        const auto left=a.toObject(),right=b.toObject();auto keys=left.keys();
        for(const auto& key:right.keys())if(!keys.contains(key))keys.append(key);
        keys.sort();for(const auto& key:keys)result.append(differentFields(left.value(key),right.value(key),path+'/'+key));
    } else if(a.isArray()&&b.isArray()&&a.toArray().size()==b.toArray().size()) {
        const auto left=a.toArray(),right=b.toArray();
        for(qsizetype i=0;i<left.size();++i)result.append(differentFields(left.at(i),right.at(i),path+'/'+QString::number(i)));
    } else if(a!=b)result.append(path);
    return result;
}
}

#include "fixtures/linked_rails_layout/default_web_observation.h"

class LinkedNativeRailsTest : public QObject {
    Q_OBJECT
private slots:
    void defaultWebFrozen_data() {
        QTest::addColumn<bool>("zero");QTest::newRow("unset")<<false;QTest::newRow("zero")<<true;
    }
    void defaultWebFrozen() {
        QFETCH(bool,zero);Environment env;if(zero)qputenv(flag,"0");else qunsetenv(flag);
        const QByteArray bytes=readFile(QFileInfo(QString::fromUtf8(__FILE__)).dir().filePath("fixtures/linked_rails_layout/default-web.json"));
        QCOMPARE(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex(),QByteArray("5a4024d1416031735f246d2ca5b304a487d94ef3b5343426f500278854992210"));
        const auto expected=QJsonDocument::fromJson(bytes).object();QVERIFY(!expected.isEmpty());
        Harness h;QVERIFY(h.ready);const auto actual=DefaultWebObservation::capture(h);QVERIFY(!actual.isEmpty());
        const auto differences=differentFields(expected,actual);
        QVERIFY2(differences.isEmpty(),qPrintable(differences.join('\n')));
        const auto phases=actual["phases"].toArray();QCOMPARE(phases.size(),2);
        QVERIFY(!differentFields(phases[0].toObject()["state"],phases[1].toObject()["state"]).isEmpty());
        auto changed=actual;auto alteredPhases=phases;auto phase=alteredPhases[0].toObject();
        auto state=phase["state"].toObject();auto editor=state["editor"].toObject();
        editor["dpr"]=editor["dpr"].toDouble()+1;state["editor"]=editor;phase["state"]=state;
        alteredPhases[0]=phase;changed["phases"]=alteredPhases;
        QCOMPARE(differentFields(actual,changed),QStringList({"/phases/0/state/editor/dpr"}));
    }
    void foldAggregationAndRetainedCards() {
        Environment env;qputenv(flag,"1");Harness h;QVERIFY(h.ready);QVERIFY(prepareFoldSource(h));
        QVERIFY(h.store({anchorAt(h,"a","signal_000 <=",{"a0","a1"}),anchorAt(h,"b","signal_002 <=",{"b0","b1"}),
            anchorAt(h,"head","always_ff",{"head"}),anchorAt(h,"plain-a","logic clk",{"p0","p1"}),
            anchorAt(h,"plain-b","rst_n;",{"p2"}),anchorAt(h,"other","signal_003 =",{"other"})}));
        QVERIFY(h.controller->setActive(true));expandAllReferenceGroups(h);QTRY_VERIFY(h.allResourcesComplete());
        QVERIFY(completeCoverage(h));
        QVERIFY(presentationFor(h,"plain-a")["id"]!=presentationFor(h,"plain-b")["id"]);
        QPointer<QFrame> first,extra;
        for(auto* card:h.rails()->findChildren<QFrame*>("linkedNativeCard")) {
            if(card->property("referenceId")=="a/link-0")first=card;
            if(card->property("referenceId")=="a/link-1")extra=card;
        }
        QVERIFY(first);QVERIFY(extra);
        QVERIFY(h.editor()->toggleFoldAtLineForTest(3));
        QTRY_VERIFY(presentationFor(h,"a")["folded"].toBool());
        const auto aggregate=presentationFor(h,"a");
        QCOMPARE(aggregate["members"].toArray(),QJsonArray({"a","b","head"}));
        QCOMPARE(aggregate["memberExpansion"].toObject(),QJsonObject({{"a",true},{"b",true},{"head",false}}));
        QCOMPARE(aggregate["references"].toArray().size(),5);QCOMPARE(aggregate["foldHeadSourceLine"].toInt(),3);
        QCOMPARE(aggregate["foldHeadVisibleRow"].toInt(),3);QVERIFY(aggregate["positioned"].toBool());
        QTextCursor head(h.editor()->document()->findBlockByNumber(3));
        const int headY=h.editor()->cursorRect(head).center().y()+h.editor()->viewport()->mapTo(h.rails(),QPoint()).y()-h.rails()->railViewportTop();
        QCOMPARE(aggregate["anchorViewportY"].toInt(),headY);QVERIFY(headY>0);
        QCOMPARE(aggregate["outlines"].toInt(),2);QVERIFY(first);QVERIFY(extra.isNull());
        QCOMPARE(first->parentWidget()->findChildren<QFrame*>("linkedNativeCard").size(),1);
        QVERIFY(completeCoverage(h));QCOMPARE(h.railsSnapshot()["groups"].toArray().size(),6);
        auto* expand=expandButton(h,aggregate["id"].toString());QVERIFY(expand);QVERIFY(expand->isVisible());
        const int width=h.rails()->sizes()[1];MotionWatch watch(h.editor());expand->click();
        QTRY_VERIFY(!presentationFor(h,"a")["folded"].toBool());
        QTRY_VERIFY(presentationFor(h,"b")["expanded"].toBool());
        QVERIFY(h.editor()->sourceLineVisible(5));QVERIFY(h.editor()->sourceLineVisible(8));
        QVERIFY(presentationFor(h,"a")["expanded"].toBool());QVERIFY(presentationFor(h,"head")["expanded"].toBool());
        QVERIFY(presentationFor(h,"plain-a")["expanded"].toBool());QVERIFY(first);
        QCOMPARE(first->parentWidget()->property("anchorId").toString(),QStringLiteral("a"));
        QCOMPARE(h.rails()->sizes()[1],width);QVERIFY(completeCoverage(h));QVERIFY(watch.unchanged());
    }
    void nestedFoldRemappingAndUnrelatedFold() {
        Environment env;qputenv(flag,"1");Harness h;QVERIFY(h.ready);QVERIFY(prepareFoldSource(h));
        QVERIFY(h.store({anchorAt(h,"a","signal_000 <=",{"a0","a1"}),anchorAt(h,"b","signal_002 <=",{"b0"}),
            anchorAt(h,"head","always_ff",{"head"}),anchorAt(h,"other","signal_003 =",{"other"}),
            anchorAt(h,"plain","logic clk",{"p0","p1"})}));
        QVERIFY(h.controller->setActive(true));expandAllReferenceGroups(h);QTRY_VERIFY(h.allResourcesComplete());
        QVERIFY(h.editor()->toggleFoldAtLineForTest(4));QVERIFY(h.editor()->toggleFoldAtLineForTest(3));
        QVERIFY(h.editor()->toggleFoldAtLineForTest(11));
        QTRY_VERIFY(presentationFor(h,"a")["folded"].toBool());
        QTRY_VERIFY(presentationFor(h,"other")["folded"].toBool());
        QCOMPARE(h.editor()->foldingViewState().collapsedRanges.size(),3);
        const QString id=presentationFor(h,"a")["id"].toString();
        QTextCursor edit(h.editor()->document());edit.setPosition(0);edit.insertText("// prefix\n");
        QTRY_COMPARE(h.railsSnapshot()["appliedContentRevision"].toInteger(),qint64(h.editor()->semanticDocumentRevision()));
        QCOMPARE(presentationFor(h,"a")["id"].toString(),id);
        QCOMPARE(presentationFor(h,"a")["foldHeadSourceLine"].toInt(),4);
        QCOMPARE(h.editor()->foldingViewState().collapsedRanges.size(),3);QVERIFY(completeCoverage(h));
        h.editor()->verticalScrollBar()->setValue(2);h.editor()->horizontalScrollBar()->setValue(10);
        const int width=h.rails()->sizes()[1];MotionWatch watch(h.editor());
        auto* expand=expandButton(h,id);QVERIFY(expand);expand->click();
        QTRY_VERIFY(presentationFor(h,"a")["expanded"].toBool());
        QVERIFY(!presentationFor(h,"a")["folded"].toBool());QVERIFY(!presentationFor(h,"b")["folded"].toBool());
        QVERIFY(h.editor()->sourceLineVisible(6));QVERIFY(h.editor()->sourceLineVisible(9));
        QCOMPARE(h.editor()->foldingViewState().collapsedRanges.size(),1);
        QVERIFY(h.editor()->foldCollapsedAtLineForTest(12));QVERIFY(presentationFor(h,"other")["folded"].toBool());
        QVERIFY(presentationFor(h,"plain")["expanded"].toBool());QVERIFY(completeCoverage(h));
        QCOMPARE(h.rails()->sizes()[1],width);QVERIFY(watch.unchanged());
    }
    void singleHiddenAnchorCanExpand() {
        Environment env;qputenv(flag,"1");Harness h;QVERIFY(h.ready);QVERIFY(prepareFoldSource(h));
        QVERIFY(h.store({anchorAt(h,"a","signal_000 <=",{"one"}),anchorAt(h,"plain","logic clk",{"p"})}));
        QVERIFY(h.controller->setActive(true));QTRY_VERIFY(h.allResourcesComplete());
        QVERIFY(h.editor()->toggleFoldAtLineForTest(3));QTRY_VERIFY(presentationFor(h,"a")["folded"].toBool());
        const auto group=presentationFor(h,"a");QCOMPARE(group["members"].toArray(),QJsonArray({"a"}));
        QCOMPARE(group["references"].toArray().size(),1);QCOMPARE(group["outlines"].toInt(),0);
        auto* expand=expandButton(h,group["id"].toString());QVERIFY(expand);QVERIFY(expand->isVisible());
        MotionWatch watch(h.editor());expand->click();QTRY_VERIFY(presentationFor(h,"a")["expanded"].toBool());
        QVERIFY(h.editor()->sourceLineVisible(5));QVERIFY(completeCoverage(h));QVERIFY(watch.unchanged());
    }
    void railOperationsNeverNavigateEditor() {
        Environment env;qputenv(flag,"1");Harness h;QVERIFY(h.ready);h.defer=true;
        QJsonArray rows;for(int i=0;i<20;++i)rows.append(h.anchor(QString("g%1").arg(i,2,10,QChar('0')),i*3,{QString::number(i),QString("extra-%1").arg(i)}));
        QVERIFY(h.store(rows));QVERIFY(h.controller->setActive(true));QTRY_VERIFY(!h.pending.isEmpty());
        MotionWatch watch(h.editor());const int width=h.rails()->sizes()[1];
        expandAllReferenceGroups(h);h.defer=false;h.releasePending();QTRY_VERIFY(h.allResourcesComplete());QVERIFY(watch.unchanged());
        auto* left=h.rails()->findChild<QScrollArea*>("linkedNativeLeftRail");QVERIFY(left);
        wheelOn(left->viewport(),-120);QVERIFY(scrollFinished(left->verticalScrollBar()));
        QCOMPARE(trackState(h,0)["state"].toString(),QStringLiteral("Detached"));QVERIFY(watch.unchanged());
        auto* expand=expandButton(h,"g00");QVERIFY(expand);expand->click();QVERIFY(watch.unchanged());
        auto* follow=h.rails()->findChild<QPushButton*>("linkedNativeFollowLeft");QVERIFY(follow);follow->click();
        QCOMPARE(trackState(h,0)["state"].toString(),QStringLiteral("Follow"));QVERIFY(watch.unchanged());
        QCOMPARE(h.rails()->sizes()[1],width);
        auto* connector=h.window.findChild<LinkedConnectorOverlay*>();QVERIFY(connector);
        QTRY_COMPARE(connector->completion().geometrySerial,connector->geometrySerial());QVERIFY(watch.unchanged());
        const auto sizes=h.rails()->sizes();const auto layoutSerial=h.rails()->connectorSnapshot(connector).layoutSerial;
        QSignalSpy layouts(h.rails(),&LinkedNativeRails::layoutApplied),demands(h.rails(),&LinkedNativeRails::visibleReferencesChanged);
        h.rails()->setActiveAnchors({"g00"});h.rails()->setActiveAnchors({"g00"});h.rails()->setActiveAnchors({});
        QCOMPARE(layouts.size(),0);QCOMPARE(demands.size(),0);
        QCOMPARE(h.rails()->connectorSnapshot(connector).layoutSerial,layoutSerial);QVERIFY(watch.unchanged());
        const auto paint=connector->completion().paintSerial;connector->update();
        QTRY_VERIFY(connector->completion().paintSerial>paint);QVERIFY(watch.unchanged());
        connector->parentWidget()->hide();QVERIFY(!connector->scene().valid);QVERIFY(watch.unchanged());
        connector->parentWidget()->show();
        QTRY_VERIFY(connector->completion().valid&&connector->completion().geometrySerial==connector->geometrySerial());
        QCOMPARE(h.rails()->sizes(),sizes);QVERIFY(watch.unchanged());
    }
    void reentryKeepsVisibleDemand() {
        Environment env;qputenv(flag,"1");Harness h;QVERIFY(h.ready);
        QVERIFY(h.store({h.anchor("first",1,{"reentry"})}));
        QVERIFY(h.controller->setActive(true));QTRY_VERIFY(h.allResourcesComplete());
        QVERIFY(h.controller->setActive(false));
        for(int i=0;i<3;++i) {
            QVERIFY(h.controller->setActive(true));
            const auto sizes=h.rails()->sizes();QCOMPARE(sizes.size(),3);
            QVERIFY(sizes[0]>0);QVERIFY(sizes[1]>0);QVERIFY(sizes[2]>0);
            QVERIFY(h.controller->setActive(false));
        }
        h.defer=true;QVERIFY(h.controller->setActive(true));
        const bool requested=QTest::qWaitFor([&]{return !h.pending.isEmpty();},5000);
        for(auto* area:h.rails()->findChildren<QScrollArea*>())
            QVERIFY(area->viewport()->width()>0);
        QVERIFY(requested);
    }
    void identityDuringRealEdits() {
        Environment env;qputenv(flag,"1");Harness h;QVERIFY(h.ready);
        QJsonArray rows;
        for(int i=0;i<12;++i)rows.append(h.anchor(QString("stable-%1").arg(i,2,10,QChar('0')),i*3,
            {QString("first-%1").arg(i),QString("second-%1").arg(i)}));
        QVERIFY(h.store(rows));QVERIFY(h.controller->setActive(true));expandAllReferenceGroups(h);QTRY_VERIFY(h.allResourcesComplete());
        auto* editor=h.editor();QTextCursor edit(editor->document());edit.movePosition(QTextCursor::End);edit.insertText("// edits ");
        QTRY_COMPARE(h.railsSnapshot()["appliedContentRevision"].toInteger(),qint64(editor->semanticDocumentRevision()));
        auto* left=h.rails()->findChild<QScrollArea*>("linkedNativeLeftRail");QVERIFY(left);
        QTRY_VERIFY(left->verticalScrollBar()->maximum()>250);left->verticalScrollBar()->setValue(250);
        wheelOn(left->viewport(),-120);QTRY_COMPARE(trackState(h,0)["state"].toString(),QStringLiteral("Detached"));
        QVERIFY(scrollFinished(left->verticalScrollBar()));
        QCoreApplication::processEvents();QTRY_VERIFY(h.allResourcesComplete());
        QMap<QString,QPointer<QFrame>> cards,groups;
        QMap<QString,QPoint> locations;
        for(auto* card:h.rails()->findChildren<QFrame*>("linkedNativeCard")) {
            const auto id=card->property("referenceId").toString();cards[id]=card;
            locations[id]=card->mapTo(h.rails(),QPoint());
        }
        for(auto* group:h.rails()->findChildren<QFrame*>("linkedNativeGroup"))groups[group->property("anchorId").toString()]=group;
        QCOMPARE(cards.size(),24);QCOMPARE(groups.size(),12);
        for(const auto& group:h.railsSnapshot()["presentation"].toArray())QVERIFY(group.toObject()["expanded"].toBool());
        const int scroll=left->verticalScrollBar()->value();
        const auto baselinePresentation=h.railsSnapshot();
        const auto editorState=view(editor);
        QSignalSpy changed(editor,&QPlainTextEdit::textChanged);
        const auto startingRevision=editor->semanticDocumentRevision();
        const auto beforeBurst=h.railsSnapshot()["reconciliationSerial"].toInteger();
        for(int i=0;i<100;++i)edit.insertText("x");
        QCOMPARE(changed.size(),100);
        QCOMPARE(editor->semanticDocumentRevision(),startingRevision+100);
        QTRY_COMPARE(h.railsSnapshot()["appliedContentRevision"].toInteger(),qint64(startingRevision+100));
        QVERIFY(h.railsSnapshot()["reconciliationSerial"].toInteger()>beforeBurst);
        for(auto it=cards.cbegin();it!=cards.cend();++it) {
            QVERIFY(it.value());QCOMPARE(it.value()->property("referenceId").toString(),it.key());
            if(it.value()->mapTo(h.rails(),QPoint())!=locations[it.key()]) {
                qInfo().noquote()<<"identity-moved"<<it.key()<<"before"<<QJsonDocument(baselinePresentation).toJson(QJsonDocument::Compact)
                    <<"after"<<QJsonDocument(h.railsSnapshot()).toJson(QJsonDocument::Compact);
            }
            QCOMPARE(it.value()->mapTo(h.rails(),QPoint()),locations[it.key()]);
        }
        for(const auto& group:groups)QVERIFY(group);
        for(const auto& group:h.railsSnapshot()["presentation"].toArray())QVERIFY(group.toObject()["expanded"].toBool());
        QCOMPARE(left->verticalScrollBar()->value(),scroll);QCOMPARE(view(editor),editorState);
        const auto beforeRepeated=h.railsSnapshot()["reconciliationSerial"].toInteger();
        for(int i=0;i<100;++i) {
            const auto signalCount=changed.size();edit.insertText("y");
            QVERIFY(changed.size()>signalCount);
            const auto revision=editor->semanticDocumentRevision();QCOMPARE(revision,startingRevision+101+i);
            QTRY_COMPARE(h.railsSnapshot()["appliedContentRevision"].toInteger(),qint64(revision));
            QCOMPARE(left->verticalScrollBar()->value(),scroll);
            QCOMPARE(trackState(h,0)["state"].toString(),QStringLiteral("Detached"));
            for(auto it=cards.cbegin();it!=cards.cend();++it) {
                QVERIFY(it.value());QCOMPARE(it.value()->mapTo(h.rails(),QPoint()),locations[it.key()]);
            }
            for(const auto& group:groups)QVERIFY(group);
            for(const auto& group:h.railsSnapshot()["presentation"].toArray())QVERIFY(group.toObject()["expanded"].toBool());
        }
        QVERIFY(changed.size()>=200);QCOMPARE(editor->semanticDocumentRevision(),startingRevision+200);
        QVERIFY(h.railsSnapshot()["reconciliationSerial"].toInteger()>=beforeRepeated+100);
        QCOMPARE(h.rails()->findChildren<QFrame*>("linkedNativeCard").size(),cards.size());
        QCOMPARE(view(editor),editorState);
    }
    void independentFollowAndOuterIntent() {
        Environment env;qputenv(flag,"1");Harness h;QVERIFY(h.ready);
        QJsonArray rows;for(int i=0;i<24;++i)rows.append(h.anchor(QString("g%1").arg(i,2,10,QChar('0')),i*3,{QString::number(i)}));
        QVERIFY(h.store(rows));QVERIFY(h.controller->setActive(true));QTRY_VERIFY(h.allResourcesComplete());
        auto* left=h.rails()->findChild<QScrollArea*>("linkedNativeLeftRail");
        auto* right=h.rails()->findChild<QScrollArea*>("linkedNativeRightRail");QVERIFY(left);QVERIFY(right);
        QCOMPARE(trackState(h,0)["state"].toString(),QStringLiteral("Follow"));
        QCOMPARE(trackState(h,1)["state"].toString(),QStringLiteral("Follow"));
        const auto beforeScroll=h.railsSnapshot()["layoutSerial"].toInteger();
        h.editor()->verticalScrollBar()->setValue(12);
        const bool laidOut=QTest::qWaitFor([&]{return h.railsSnapshot()["layoutSerial"].toInteger()>beforeScroll;},5000);
        if(!laidOut||left->verticalScrollBar()->value()==0)
            qInfo().noquote()<<"follow-scroll-observation"<<QJsonDocument(view(h.editor())).toJson(QJsonDocument::Compact)
                <<QJsonDocument(h.railsSnapshot()).toJson(QJsonDocument::Compact);
        QVERIFY(laidOut);
        QTRY_VERIFY(left->verticalScrollBar()->value()>0);
        left->verticalScrollBar()->setValue(100);
        QCOMPARE(trackState(h,0)["state"].toString(),QStringLiteral("Follow"));
        const auto beforeSecond=h.railsSnapshot();
        h.editor()->verticalScrollBar()->setValue(16);
        const bool realigned=QTest::qWaitFor([&]{return left->verticalScrollBar()->value()!=100;},5000);
        if(!realigned)qInfo().noquote()<<"follow-second-scroll"<<QJsonDocument(view(h.editor())).toJson(QJsonDocument::Compact)
            <<"before"<<QJsonDocument(beforeSecond).toJson(QJsonDocument::Compact)
            <<"after"<<QJsonDocument(h.railsSnapshot()).toJson(QJsonDocument::Compact);
        QVERIFY(realigned);
        left->verticalScrollBar()->setValue(100);
        h.editor()->verticalScrollBar()->setValue(20);h.controller->refreshAnchors();
        QTRY_VERIFY(left->verticalScrollBar()->value()!=100);
        const int top=left->viewport()->mapTo(h.rails(),QPoint()).y();
        wheelOn(left->viewport(),-120);QTRY_COMPARE(trackState(h,0)["state"].toString(),QStringLiteral("Detached"));
        QVERIFY(scrollFinished(left->verticalScrollBar()));
        QCOMPARE(trackState(h,1)["state"].toString(),QStringLiteral("Follow"));
        auto* follow=h.rails()->findChild<QPushButton*>("linkedNativeFollowLeft");QVERIFY(follow);QVERIFY(follow->isVisible());
        QCOMPARE(left->viewport()->mapTo(h.rails(),QPoint()).y(),top);
        const auto locations=cardLocations(h,0);const int scroll=left->verticalScrollBar()->value();
        const QJsonValue first=h.railsSnapshot()["groups"].toArray()[0].toObject()["y"];
        h.editor()->verticalScrollBar()->setValue(30);
        QTRY_VERIFY(h.railsSnapshot()["groups"].toArray()[0].toObject()["y"]!=first);
        QCOMPARE(left->verticalScrollBar()->value(),scroll);QCOMPARE(cardLocations(h,0),locations);
        QCOMPARE(trackState(h,0)["state"].toString(),QStringLiteral("Detached"));
        QTextCursor cursor(h.editor()->document());cursor.setPosition(h.text.indexOf("signal_030"));h.editor()->setTextCursor(cursor);
        QCoreApplication::processEvents();QCOMPARE(trackState(h,0)["state"].toString(),QStringLiteral("Detached"));
        const auto original=view(h.editor());QSignalSpy cursorSignals(h.editor(),&QPlainTextEdit::cursorPositionChanged);
        follow->click();QTRY_COMPARE(trackState(h,0)["state"].toString(),QStringLiteral("Follow"));
        QVERIFY(!follow->isVisible());QCOMPARE(left->viewport()->mapTo(h.rails(),QPoint()).y(),top);
        QCOMPARE(view(h.editor()),original);QCOMPARE(cursorSignals.size(),0);
    }
    void typingAndExplicitNavigationIntent() {
        Environment env;qputenv(flag,"1");Harness h;QVERIFY(h.ready);
        QJsonArray rows;for(int i=0;i<20;++i)rows.append(h.anchor(QString("g%1").arg(i,2,10,QChar('0')),1+i*3,{QString::number(i)}));
        QVERIFY(h.store(rows));QVERIFY(h.controller->setActive(true));QTRY_VERIFY(h.allResourcesComplete());
        auto* left=h.rails()->findChild<QScrollArea*>("linkedNativeLeftRail");QVERIFY(left);
        left->verticalScrollBar()->setValue(200);wheelOn(left->viewport(),-120);
        QTRY_COMPARE(trackState(h,0)["state"].toString(),QStringLiteral("Detached"));
        QVERIFY(scrollFinished(left->verticalScrollBar()));
        QTextCursor cursor(h.editor()->document());cursor.setPosition(h.text.indexOf(';')+1);h.editor()->setTextCursor(cursor);
        const int scroll=left->verticalScrollBar()->value();const auto locations=cardLocations(h,0);
        QTest::keyClicks(h.editor()," // typing");
        QTRY_COMPARE(h.railsSnapshot()["appliedContentRevision"].toInteger(),qint64(h.editor()->semanticDocumentRevision()));
        QCOMPARE(trackState(h,0)["state"].toString(),QStringLiteral("Detached"));
        QCOMPARE(left->verticalScrollBar()->value(),scroll);QCOMPARE(cardLocations(h,0),locations);
        cursor.setPosition(h.editor()->cachedDocumentText().indexOf("signal_001"));h.editor()->setTextCursor(cursor);
        QCoreApplication::processEvents();QCOMPARE(trackState(h,0)["state"].toString(),QStringLiteral("Detached"));
        QTest::mouseClick(h.editor()->viewport(),Qt::LeftButton,Qt::NoModifier,h.editor()->cursorRect(cursor).center());
        QTRY_COMPARE(trackState(h,0)["state"].toString(),QStringLiteral("Follow"));
        QCOMPARE(trackState(h,0)["reason"].toString(),QStringLiteral("code-mouse-selection"));
        wheelOn(left->viewport(),-120);QTRY_COMPARE(trackState(h,0)["state"].toString(),QStringLiteral("Detached"));
        QTest::keyClick(h.editor(),Qt::Key_Right);
        QTRY_COMPARE(trackState(h,0)["state"].toString(),QStringLiteral("Follow"));
        QCOMPARE(trackState(h,0)["reason"].toString(),QStringLiteral("code-navigation-key"));
        wheelOn(left->viewport(),-120);QTRY_COMPARE(trackState(h,0)["state"].toString(),QStringLiteral("Detached"));
        QPushButton* locate=nullptr;
        for(auto* card:h.rails()->findChildren<QFrame*>("linkedNativeCard"))if(card->property("referenceId")=="g00/link-0")locate=card->findChild<QPushButton*>("linkedNativeLocate");
        QVERIFY(locate);locate->click();QTRY_COMPARE(trackState(h,0)["state"].toString(),QStringLiteral("Follow"));
        QCOMPARE(trackState(h,0)["reason"].toString(),QStringLiteral("go-to-code"));
        QCOMPARE(h.editor()->textCursor().selectedText(),QStringLiteral("signal_001"));
    }
    void previewWheelAndPixelScrollIntent() {
        Environment env;qputenv(flag,"1");Harness h;QVERIFY(h.ready);h.defer=true;
        QVERIFY(h.store({h.anchor("a",1,{"long"}),h.anchor("b",2,{"peer"}),h.anchor("c",80,{"far"})}));
        QVERIFY(h.controller->setActive(true));QTRY_VERIFY(!h.pending.isEmpty());
        const auto work=h.pending;h.pending.clear();h.defer=false;
        for(const auto& item:work) {auto result=h.result(item.request);result["content"]=QString("long preview line\n").repeated(200);item.reply(result,{});}
        QTRY_VERIFY(h.allResourcesComplete());
        auto* left=h.rails()->findChild<QScrollArea*>("linkedNativeLeftRail");QVERIFY(left);
        QTextBrowser* preview=nullptr;
        for(auto* card:h.rails()->findChildren<QFrame*>("linkedNativeCard"))if(card->property("referenceId")=="a/link-0")preview=card->findChild<QTextBrowser*>();
        QVERIFY(preview);QTRY_VERIFY(preview->verticalScrollBar()->maximum()>0);
        const int outer=left->verticalScrollBar()->value();wheelOn(preview->viewport(),-120);
        QTRY_VERIFY(preview->verticalScrollBar()->value()>0);QCOMPARE(left->verticalScrollBar()->value(),outer);
        QCOMPARE(trackState(h,0)["state"].toString(),QStringLiteral("Follow"));
        QTRY_VERIFY(left->verticalScrollBar()->maximum()>0);wheelOn(left->viewport(),0,QPoint(0,-80));
        QTRY_COMPARE(trackState(h,0)["state"].toString(),QStringLiteral("Detached"));
        QVERIFY(left->verticalScrollBar()->value()!=outer);
    }
    void scrollbarIntentAndEmptyTrack() {
        Environment env;qputenv(flag,"1");Harness h;QVERIFY(h.ready);
        QJsonArray rows;for(int i=0;i<20;++i)rows.append(h.anchor(QString::number(i),i*3,{QString::number(i)}));
        QVERIFY(h.store(rows));QVERIFY(h.controller->setActive(true));QTRY_VERIFY(h.allResourcesComplete());
        auto* left=h.rails()->findChild<QScrollArea*>("linkedNativeLeftRail");QVERIFY(left);
        auto* bar=left->verticalScrollBar();QTRY_VERIFY(bar->maximum()>0);bar->setValue(bar->maximum()/2);
        QCOMPARE(trackState(h,0)["state"].toString(),QStringLiteral("Follow"));
        QStyleOptionSlider option;option.initFrom(bar);option.orientation=Qt::Vertical;
        option.minimum=bar->minimum();option.maximum=bar->maximum();option.sliderPosition=bar->sliderPosition();option.pageStep=bar->pageStep();
        const auto thumb=bar->style()->subControlRect(QStyle::CC_ScrollBar,&option,QStyle::SC_ScrollBarSlider,bar);
        const int beforeDrag=bar->value();
        QTest::mousePress(bar,Qt::LeftButton,Qt::NoModifier,thumb.center());
        QTest::mouseMove(bar,thumb.center()+QPoint(0,20));QTest::mouseRelease(bar,Qt::LeftButton,Qt::NoModifier,thumb.center()+QPoint(0,20));
        QVERIFY(bar->value()!=beforeDrag);
        QCOMPARE(trackState(h,0)["state"].toString(),QStringLiteral("Detached"));
        QCOMPARE(trackState(h,0)["reason"].toString(),QStringLiteral("scrollbar-mouse"));
        auto* follow=h.rails()->findChild<QPushButton*>("linkedNativeFollowLeft");QVERIFY(follow);follow->click();
        QCOMPARE(trackState(h,0)["state"].toString(),QStringLiteral("Follow"));
        QTest::keyClick(bar,Qt::Key_End);QCOMPARE(trackState(h,0)["state"].toString(),QStringLiteral("Detached"));
        QCOMPARE(bar->value(),bar->maximum());
        QCOMPARE(trackState(h,0)["reason"].toString(),QStringLiteral("scrollbar-key"));
        QVERIFY(h.store({}));h.controller->refreshAnchors();QTRY_VERIFY(h.railsSnapshot()["groups"].toArray().isEmpty());
        const auto original=view(h.editor());follow->click();QCOMPARE(bar->value(),0);QCOMPARE(view(h.editor()),original);
        QCOMPARE(trackState(h,0)["state"].toString(),QStringLiteral("Follow"));
    }
    void individualReferenceChangesPreservePeers() {
        Environment env;qputenv(flag,"1");Harness h;QVERIFY(h.ready);
        QVERIFY(h.store({h.anchor("a",2,{"one","two"}),h.anchor("b",4,{"peer"})}));
        QVERIFY(h.controller->setActive(true));expandAllReferenceGroups(h);QTRY_VERIFY(h.allResourcesComplete());
        QMap<QString,QPointer<QFrame>> before;
        for(auto* card:h.rails()->findChildren<QFrame*>("linkedNativeCard"))before[card->property("referenceId").toString()]=card;
        QCOMPARE(before.size(),3);
        QVERIFY(h.store({h.anchor("a",2,{"one","two","added"}),h.anchor("b",4,{"peer"})}));
        h.controller->refreshAnchors();QTRY_COMPARE(h.rails()->findChildren<QFrame*>("linkedNativeCard").size(),4);
        QPointer<QFrame> added;
        for(auto* card:h.rails()->findChildren<QFrame*>("linkedNativeCard")) {
            const auto id=card->property("referenceId").toString();
            if(id=="a/link-2")added=card;else QCOMPARE(card,before.value(id).data());
        }
        QVERIFY(added);
        auto changed=h.anchor("a",2,{"one","two"});auto links=changed["links"].toArray();
        auto first=links[0].toObject();first["title"]="Changed title";links[0]=first;changed["links"]=links;
        QVERIFY(h.store({changed,h.anchor("b",4,{"peer"})}));h.controller->refreshAnchors();
        QTRY_VERIFY(added.isNull());QCOMPARE(h.rails()->findChildren<QFrame*>("linkedNativeCard").size(),3);
        for(auto* card:h.rails()->findChildren<QFrame*>("linkedNativeCard"))
            QCOMPARE(card,before.value(card->property("referenceId").toString()).data());
        h.controller->reloadBoard();
        for(const auto& card:before)QVERIFY(card.isNull());
        QTRY_VERIFY(h.allResourcesComplete());
    }
    void collapsedStackAndExpandedCards() {
        Environment env;qputenv(flag,"1");Harness h;QVERIFY(h.ready);
        QStringList entries;for(int i=0;i<20;++i)entries.append(QString("stack-%1").arg(i));
        QVERIFY(h.store({h.anchor("one",1,{"single"}),h.anchor("twenty",2,entries)}));
        QVERIFY(h.controller->setActive(true));QTRY_VERIFY(h.allResourcesComplete());
        auto* rails=h.rails();QVERIFY(rails);
        QTRY_COMPARE(rails->findChildren<QFrame*>("linkedNativeCard").size(),2);
        QPointer<QFrame> first,peer;
        for(auto* card:rails->findChildren<QFrame*>("linkedNativeCard")) {
            if(card->property("referenceId")=="twenty/link-0")first=card;else peer=card;
        }
        QVERIFY(first);QVERIFY(peer);
        QPushButton* toggle=nullptr;
        for(auto* button:rails->findChildren<QPushButton*>("linkedNativeExpand")) {
            if(button->property("anchorId")=="twenty")toggle=button;
            else QVERIFY(!button->isVisible());
        }
        QVERIFY(toggle);QCOMPARE(toggle->text(),QStringLiteral("Expand references"));
        for(const auto& row:h.railsSnapshot()["presentation"].toArray()) {
            const auto item=row.toObject();QVERIFY(!item["expanded"].toBool());
            QCOMPARE(item["outlines"].toInt(),item["id"]=="twenty"?2:0);
            QCOMPARE(item["height"],item["preferredHeight"]);
        }
        const auto collapsed=first->grab().toImage();QVERIFY(!collapsed.isNull());
        const int codeWidth=rails->sizes()[1];const auto editorState=view(h.editor());
        toggle->click();QTRY_COMPARE(rails->findChildren<QFrame*>("linkedNativeCard").size(),21);
        QCOMPARE(rails->sizes()[1],codeWidth);QCOMPARE(toggle->text(),QStringLiteral("Collapse references"));
        QVERIFY(first);QVERIFY(peer);QCOMPARE(view(h.editor()),editorState);
        const auto expanded=first->grab().toImage();QVERIFY(!expanded.isNull());
        QVERIFY(collapsed!=expanded);
        QList<QPointer<QFrame>> extra;
        for(auto* card:rails->findChildren<QFrame*>("linkedNativeCard"))if(card!=first&&card!=peer)extra.append(card);
        QCOMPARE(extra.size(),19);
        auto* right=rails->findChild<QScrollArea*>("linkedNativeRightRail");QVERIFY(right);
        QTRY_VERIFY(right->verticalScrollBar()->maximum()>0);
        right->verticalScrollBar()->setValue(right->verticalScrollBar()->maximum());
        QTRY_VERIFY(h.requested.contains("stack-9"));
        toggle->click();QTRY_COMPARE(rails->findChildren<QFrame*>("linkedNativeCard").size(),2);
        QVERIFY(first);QVERIFY(peer);for(const auto& card:extra)QVERIFY(card.isNull());
        QCOMPARE(rails->sizes()[1],codeWidth);QCOMPARE(view(h.editor()),editorState);
        QCOMPARE(h.requested.value("single"),1);
    }
    void disabledValues_data() {
        QTest::addColumn<QByteArray>("value");QTest::addColumn<bool>("unset");
        QTest::newRow("unset")<<QByteArray{}<<true;
        QTest::newRow("zero")<<QByteArray("0")<<false;
        QTest::newRow("empty")<<QByteArray("")<<false;
        QTest::newRow("true-word")<<QByteArray("true")<<false;
        QTest::newRow("padded-one")<<QByteArray(" 1")<<false;
    }
    void disabledValues() {
        QFETCH(QByteArray,value);QFETCH(bool,unset);Environment env;
        if(unset)qunsetenv(flag);else qputenv(flag,value);
        Harness h;QVERIFY(h.ready);QVERIFY(h.controller->setActive(true));
        QVERIFY(!h.snapshot()["native"].toBool());QVERIFY(!h.rails());
        QCOMPARE(h.window.findChildren<WebEditorContainer*>("linkedContextWebView").size(),1);
        auto* original=h.page;auto* editor=h.editor();auto* document=editor->document();
        const QJsonValue session=h.snapshot()["session"];
        qputenv(flag,"1");QVERIFY(h.controller->setActive(true));QVERIFY(!h.rails());
        QCOMPARE(h.snapshot()["session"],session);
        QVERIFY(h.controller->setActive(false));QCOMPARE(h.stack->currentWidget(),original);
        QCOMPARE(h.editor(),editor);QCOMPARE(editor->document(),document);
        QVERIFY(h.controller->setActive(true));QVERIFY(h.rails());
        QVERIFY(h.window.findChildren<WebEditorContainer*>("linkedContextWebView").isEmpty());
        QVERIFY(h.controller->setActive(false));
    }
    void nativeSessionAndStructure() {
        Environment env;qputenv(flag,"1");Harness h;QVERIFY(h.ready);
        QVERIFY(h.store({h.anchor("a",2,{"text"}),h.anchor("b",3,{"image"})}));
        auto* editor=h.editor();auto* document=editor->document();
        QVERIFY(h.controller->setActive(true));auto* rails=h.rails();QVERIFY(rails);
        QCOMPARE(rails->count(),3);QCOMPARE(rails->widget(1),h.page);QCOMPARE(h.editor(),editor);QCOMPARE(editor->document(),document);
        QCOMPARE(rails->findChildren<QScrollArea*>().size(),2);
        QVERIFY(h.window.findChildren<WebEditorContainer*>().isEmpty());
        QVERIFY(!h.window.findChild<QWidget*>("linkedContextActions"));
        QVERIFY(!h.window.findChild<QWidget*>("linkedSaveCopy"));
        QVERIFY(h.snapshot()["initialized"].toBool());
        QTRY_VERIFY(h.allResourcesComplete());
        const QJsonValue session=h.snapshot()["session"];qputenv(flag,"0");
        QVERIFY(h.controller->setActive(true));QCOMPARE(h.rails(),rails);QCOMPARE(h.snapshot()["session"],session);
        QVERIFY(h.tabs->openFileInTab(h.second));QVERIFY(h.snapshot()["native"].toBool());QCOMPARE(h.rails(),rails);
        QVERIFY(h.snapshot()["session"]!=session);
        QElapsedTimer elapsed;elapsed.start();QVERIFY(h.controller->setActive(false));QVERIFY(elapsed.elapsed()<1000);
        QCOMPARE(h.stack->currentWidget(),h.page);QVERIFY(!h.rails());
        QVERIFY(h.controller->setActive(true));QVERIFY(!h.rails());QVERIFY(h.controller->setActive(false));
    }
    void groupsReferencesAndUnavailable() {
        Environment env;qputenv(flag,"1");Harness h;QVERIFY(h.ready);
        auto missing=h.anchor("missing",7,{"never-missing"});auto ambiguous=h.anchor("ambiguous",8,{"never-ambiguous"});
        for(auto* item:{&missing,&ambiguous}) {
            auto source=(*item)["source"].toObject();
            const QString selected=item==&missing?QStringLiteral("absent_marker"):QStringLiteral("repeat_marker");
            source["selectedText"]=selected;source["selectedTextHash"]=QString::fromLatin1(QCryptographicHash::hash(selected.toUtf8(),QCryptographicHash::Sha256).toHex());
            source["startPosition"]=h.text.size()+100;source["endPosition"]=h.text.size()+100+selected.size();
            source["prefixContext"]="";source["suffixContext"]="";(*item)["source"]=source;
        }
        QVERIFY(h.store({h.anchor("z-tie",2,{"shared"}),h.anchor("a-tie",2,{"shared","other"}),
            h.anchor("far",95,{"far-source"}),missing,ambiguous}));
        const auto resolved=h.links.linksForDocument(h.file,h.text);
        QCOMPARE(std::count_if(resolved.begin(),resolved.end(),[](const auto& a){return a.available();}),3);
        QCOMPARE(std::count_if(resolved.begin(),resolved.end(),[](const auto& a){return a.resolution==PinloomCodeLinkResolution::Ambiguous;}),1);
        const QJsonArray deep{QJsonObject{{"id","a-tie/link-0/deep"},{"anchor","a-tie"},{"parent","a-tie/link-0"},{"title","Deep"},{"source",sourceUri("deep")}},
            QJsonObject{{"id","orphan"},{"anchor","a-tie"},{"parent","gone"},{"source",sourceUri("orphan")}}};
        QVERIFY(!h.board(h.file,savedState(.5,deep)).isEmpty());QVERIFY(h.controller->setActive(true));
        QTRY_COMPARE(h.railsSnapshot()["groups"].toArray().size(),3);
        const auto groups=h.railsSnapshot()["groups"].toArray();QSet<QString> unionIds,referenceIds;
        int lastY=INT_MIN;QString lastId;
        for(int i=0;i<groups.size();++i) {
            const auto group=groups[i].toObject();const auto id=group["anchor"].toString();const int y=group["y"].toInt();
            QVERIFY(!unionIds.contains(id));unionIds.insert(id);QCOMPARE(group["side"].toInt(),i%2);
            QVERIFY(y>lastY||(y==lastY&&id>lastId));lastY=y;lastId=id;
            for(const auto& value:group["references"].toArray()){QVERIFY(!referenceIds.contains(value.toString()));referenceIds.insert(value.toString());}
        }
        QCOMPARE(unionIds,QSet<QString>({"a-tie","z-tie","far"}));QCOMPARE(referenceIds.size(),5);
        QVERIFY(referenceIds.contains("a-tie/link-0/deep"));QVERIFY(!referenceIds.contains("orphan"));
        QCOMPARE(groups[0].toObject()["anchor"].toString(),QStringLiteral("a-tie"));
        QTRY_VERIFY(h.allResourcesComplete());QCOMPARE(h.requested.value("shared"),1);
        QCOMPARE(h.requested.value("never-missing"),0);QCOMPARE(h.requested.value("never-ambiguous"),0);
        QVERIFY(h.store({}));h.controller->refreshAnchors();
        QTRY_VERIFY(h.railsSnapshot()["groups"].toArray().isEmpty());QTRY_VERIFY(h.resources().isEmpty());
    }
    void editorStateAndExplicitNavigation() {
        Environment env;qputenv(flag,"1");Harness h;QVERIFY(h.ready);h.defer=true;
        QVERIFY(h.store({h.anchor("near",22,{"text"}),h.anchor("far",82,{"image"})}));
        auto* editor=h.editor();editor->setFixedSize(520,360);QCoreApplication::processEvents();
        QTextCursor cursor(editor->document());cursor.setPosition(h.text.indexOf("signal_022"));cursor.movePosition(QTextCursor::Right,QTextCursor::KeepAnchor,7);
        editor->setTextCursor(cursor);editor->verticalScrollBar()->setValue(18);editor->horizontalScrollBar()->setValue(27);
        const auto before=view(editor);QVERIFY(before["vertical"].toInt()>0);QVERIFY(before["horizontal"].toInt()>0);
        QVERIFY(before["position"]!=before["anchor"]);auto* document=editor->document();
        QVERIFY(h.controller->setActive(true));QTRY_VERIFY(!h.pending.isEmpty());
        QCOMPARE(h.editor(),editor);QCOMPARE(editor->document(),document);QCOMPARE(view(editor),before);
        h.defer=false;h.releasePending();QTRY_VERIFY(h.allResourcesComplete());QCOMPARE(view(editor),before);
        h.controller->reloadBoard();QTRY_VERIFY(h.allResourcesComplete());QCOMPARE(view(editor),before);
        QFrame* far=nullptr;for(auto* card:h.rails()->findChildren<QFrame*>("linkedNativeCard"))if(card->property("referenceId")=="far/link-0")far=card;
        QVERIFY(far);auto* locate=far->findChild<QPushButton*>("linkedNativeLocate");QVERIFY(locate);locate->click();
        QCOMPARE(editor->textCursor().selectionStart(),h.text.indexOf("signal_082"));
        QCOMPARE(editor->textCursor().selectedText(),QStringLiteral("signal_082"));QVERIFY(editor->verticalScrollBar()->value()!=before["vertical"].toInt());
        far->findChild<QPushButton*>("linkedNativeOpen")->click();QTRY_COMPARE(h.opened,QStringList{"image"});
        QVERIFY(h.controller->setActive(false));QCOMPARE(editor->document(),document);QCOMPARE(view(editor),before);
    }
    void widthRatioAndLogicalY() {
        Environment env;qputenv(flag,"1");Harness h;QVERIFY(h.ready);
        QVERIFY(h.store({h.anchor("a",2,{"a"}),h.anchor("b",5,{"b"})}));
        QVERIFY(!h.board(h.file,savedState(.6)).isEmpty());QVERIFY(h.controller->setActive(true));
        auto* rails=h.rails();QVERIFY(rails);
        QTRY_COMPARE(([&]{const auto current=rails->sizes();return current[0]+current[1]+current[2];})(),
                     rails->width()-2*rails->handleWidth());
        auto sizes=rails->sizes();const int available=rails->width()-2*rails->handleWidth();
        QCOMPARE(sizes[0]+sizes[1]+sizes[2],available);QVERIFY(qAbs(sizes[1]-qRound(available*.6))<=2);QVERIFY(qAbs(sizes[0]-sizes[2])<=1);
        auto* editor=h.editor();const auto height=editor->viewport()->height();const auto origin=editor->viewport()->mapTo(rails,QPoint()).y();
        const auto vertical=editor->verticalScrollBar()->value();const auto before=h.railsSnapshot()["groups"].toArray();
        rails->setSizes({250,available-500,250});QCoreApplication::processEvents();
        QCOMPARE(editor->viewport()->height(),height);QCOMPARE(editor->viewport()->mapTo(rails,QPoint()).y(),origin);QCOMPARE(editor->verticalScrollBar()->value(),vertical);
        QTRY_COMPARE(h.railsSnapshot()["groups"].toArray(),before);
        for(const auto& value:before) {
            const auto group=value.toObject();const auto anchors=h.links.linksForDocument(h.file,h.text);
            const auto found=std::find_if(anchors.begin(),anchors.end(),[&](const auto& a){return a.anchor.id==group["anchor"].toString();});QVERIFY(found!=anchors.end());
            QCOMPARE(group["y"].toInt(),LinkedAnchorGeometry::sampleAnchor(*editor,{found->anchor.id,true,found->startPosition,found->endPosition}).first.center().y());
        }
        auto* tinyPage=new QWidget;LinkedNativeRails tiny(tinyPage);tiny.resize(5,80);tiny.setInitialSizes({},.5);tiny.show();QCoreApplication::processEvents();
        for(int value:tiny.sizes())QVERIFY(value>=0);
    }
    void visibilityCapAndLaterSources() {
        Environment env;qputenv(flag,"1");Harness h;QVERIFY(h.ready);
        QJsonArray rows;for(int i=0;i<40;++i)rows.append(h.anchor(QString("g%1").arg(i,3,10,QChar('0')),i,{QString("source-%1").arg(i,3,10,QChar('0'))}));
        QVERIFY(h.store(rows));h.window.resize(1500,18000);QCoreApplication::processEvents();QVERIFY(h.controller->setActive(true));
        QTRY_VERIFY(h.snapshot()["demanded"].toArray().size()>24);QTRY_COMPARE(h.resources().size(),24);QTRY_VERIFY(h.allResourcesComplete());
        QCOMPARE(h.requested.size(),24);
        QSet<QString> first;for(const auto& value:h.resources())first.insert(value.toObject()["source"].toString());
        h.window.resize(1500,640);QCoreApplication::processEvents();
        for(auto* rail:h.rails()->findChildren<QScrollArea*>())rail->verticalScrollBar()->setValue(rail->verticalScrollBar()->maximum());
        QTRY_VERIFY(h.requested.contains("source-039"));QTRY_VERIFY(h.allResourcesComplete());
        QVERIFY(h.requested.size()>24);QVERIFY(h.resources().size()<=24);
        bool released=false;for(const auto& source:first){bool present=false;for(const auto& item:h.resources())present|=item.toObject()["source"]==source;released|=!present;}QVERIFY(released);
        const auto ids=h.railsSnapshot()["visible"].toArray();QCOMPARE(h.snapshot()["demanded"].toArray(),ids);
        QVERIFY(h.controller->setActive(false));QVERIFY(h.resources().isEmpty());
    }
    void previewStatesAndRetry() {
        Environment env;qputenv(flag,"1");Harness h;QVERIFY(h.ready);h.defer=true;h.errorSources.insert("error");
        QVERIFY(h.store({h.anchor("a",1,{"empty"}),h.anchor("b",2,{"error"}),h.anchor("c",3,{"deleted"}),h.anchor("d",4,{"image"}),h.anchor("e",5,{"text"})}));
        h.window.resize(1500,1600);QVERIFY(h.controller->setActive(true));QTRY_VERIFY(!h.pending.isEmpty());
        QCOMPARE(h.cardState("a/link-0"),QStringLiteral("loading"));
        auto* originalCard=h.rails()->findChildren<QFrame*>("linkedNativeCard").first();QPointer<QFrame> cardIdentity=originalCard;
        h.defer=false;h.releasePending();QTRY_COMPARE(h.cardState("a/link-0"),QStringLiteral("empty"));
        QTRY_COMPARE(h.cardState("b/link-0"),QStringLiteral("error"));QTRY_COMPARE(h.cardState("c/link-0"),QStringLiteral("error"));
        QTRY_COMPARE(h.cardState("d/link-0"),QStringLiteral("image"));QTRY_COMPARE(h.cardState("e/link-0"),QStringLiteral("text"));
        QVERIFY(cardIdentity);QCOMPARE(cardIdentity.data(),originalCard);
        for(auto* browser:h.rails()->findChildren<QTextBrowser*>()){QVERIFY(browser->isReadOnly());QVERIFY(!browser->openLinks());QVERIFY(!browser->openExternalLinks());}
        const auto requested=h.requested;QTest::qWait(60);QCOMPARE(h.requested,requested);
        h.errorSources.clear();h.controller->reloadBoard();QTRY_COMPARE(h.cardState("b/link-0"),QStringLiteral("text"));
        QVERIFY(!cardIdentity);QCOMPARE(h.requested.value("error"),2);
    }
    void lifecycleAndStaleReplies() {
        Environment env;qputenv(flag,"1");Harness h;QVERIFY(h.ready);h.defer=true;
        QVERIFY(h.store({h.anchor("first",1,{"late-first"}),h.anchor("second",2,{"late-second"},h.second)}));
        QVERIFY(h.controller->setActive(true));QTRY_VERIFY(!h.pending.isEmpty());
        const auto old=h.pending.takeFirst();const auto generation=h.snapshot()["generation"].toDouble();
        QVERIFY(h.tabs->openFileInTab(h.second));QVERIFY(h.snapshot()["generation"].toDouble()>generation);
        h.complete(old);QTRY_COMPARE(h.railsSnapshot()["groups"].toArray().size(),1);
        QCOMPARE(h.railsSnapshot()["groups"].toArray()[0].toObject()["anchor"].toString(),QStringLiteral("second"));
        for(const auto& item:h.resources())QVERIFY(item.toObject()["source"]!=sourceUri("late-first"));
        h.defer=false;h.releasePending();QTRY_VERIFY(h.allResourcesComplete());
        const auto sizes=h.rails()->sizes();h.rails()->setSizes({sizes[0]+30,sizes[1]-30,sizes[2]});const auto adjusted=h.rails()->sizes();
        QVERIFY(h.tabs->activateOpenFile(h.file));QVERIFY(h.tabs->activateOpenFile(h.second));QCOMPARE(h.rails()->sizes(),adjusted);
        h.tabs->closeTab(h.tabWidget->currentIndex());QTRY_COMPARE(h.editor()->documentFileName(),h.file);
        h.links.setWorkspaceRoot(h.temp.filePath("other-workspace"));h.controller->refreshAnchors();QVERIFY(!h.controller->isActive());QVERIFY(!h.rails());QVERIFY(h.resources().isEmpty());
        h.links.setWorkspaceRoot(h.temp.path());
        for(int i=0;i<3;++i){QVERIFY(h.controller->setActive(true));QCOMPARE(h.window.findChildren<LinkedNativeRails*>().size(),1);QVERIFY(h.controller->setActive(false));QVERIFY(!h.rails());}
        h.defer=true;QVERIFY(h.controller->setActive(true));QTRY_VERIFY(!h.pending.isEmpty());
        const auto late=h.pending.takeFirst();h.controller.reset();h.complete(late);QCoreApplication::processEvents();
        QCOMPARE(h.stack->currentWidget(),h.page);QVERIFY(!h.rails());QVERIFY(h.editor());
    }
    void nativeBoardBytesAndDefaultSaveFailure() {
        Environment env;qputenv(flag,"1");Harness h;QVERIFY(h.ready);
        QVERIFY(h.store({h.anchor("a",2,{"text"})}));const auto original=savedState(.6);
        const auto path=h.board(h.file,original);QVERIFY(!path.isEmpty());const auto bytes=readFile(path);
        QVERIFY(h.controller->setActive(true));QTRY_VERIFY(h.allResourcesComplete());
        h.rails()->setSizes({200,800,400});h.controller->reloadBoard();QVERIFY(h.tabs->openFileInTab(h.second));
        QVERIFY(h.tabs->activateOpenFile(h.file));QVERIFY(h.controller->setActive(false));QCOMPARE(readFile(path),bytes);
        qputenv(flag,"0");QVERIFY(h.controller->setActive(true));
        auto* web=h.window.findChild<WebEditorContainer*>("linkedContextWebView");QVERIFY(web);
        const auto session=h.snapshot()["session"].toString();auto changed=original;changed["testSaveSentinel"]=43;
        web->messageReceived({{"type","layout"},{"session",session},{"state",changed}});
        QTRY_VERIFY(readFile(path)!=bytes);
        QCOMPARE(QJsonDocument::fromJson(readFile(path)).object()["state"].toObject()["testSaveSentinel"].toInt(),43);
        const QByteArray external="external revision preserved";QVERIFY(writeFile(path,external));changed["testSaveSentinel"]=44;
        web->messageReceived({{"type","layout"},{"session",session},{"state",changed}});
        auto* status=h.window.findChild<QLabel*>("linkedContextStatus");QVERIFY(status);
        QTRY_VERIFY(status->text().contains("changed on disk"));QCOMPARE(readFile(path),external);
        auto* recovery=h.window.findChild<QPushButton*>("linkedSaveCopy");QVERIFY(recovery);QVERIFY(recovery->isVisible());
        QVERIFY(h.controller->setActive(false));QCOMPARE(readFile(path),external);
    }
};
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    QTemporaryDir settings(QDir::tempPath()+"/rails-settings-XXXXXX");if(!settings.isValid())return 2;
    QCoreApplication::setOrganizationName("ZeroSlackNativeRailsTest");QCoreApplication::setApplicationName("Stage1");
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    if(!initializeUiStyleForTest())return 3;
    LinkedNativeRailsTest test;return QTest::qExec(&test,argc,argv);
}
#include "linked_native_rails_test.moc"
