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
#include <QFileDialog>
#include <QFrame>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLockFile>
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

#include "fixtures/linked_rails_layout/default_native_observation.h"

namespace {
QJsonObject nativeState(QJsonArray expanded = {}) {
    return {{"version",3},{"codeRatio",.5},{"referenceIds",QJsonArray{"a/link-0","a/link-1"}},
        {"expandedReferences",QJsonArray{}},{"nativeView",QJsonObject{{"version",1},
            {"weights",QJsonArray{.25,.5,.25}},{"expanded",expanded}}}};
}
QJsonObject diskState(const QString& path) { return QJsonDocument::fromJson(readFile(path)).object()["state"].toObject(); }
bool drainSaveWindow() {
    QElapsedTimer elapsed;elapsed.start();
    return QTest::qWaitFor([&]{return elapsed.elapsed()>=650;},2000);
}
bool dragSplitter(Harness& h,int index,int delta) {
    auto* handle=h.rails()->handle(index);if(!handle||!handle->isVisible())return false;
    const auto before=h.rails()->sizes();const auto point=handle->rect().center();const auto destination=point+QPoint(delta,0);
    const auto global=handle->mapToGlobal(destination);
    QTest::mousePress(handle,Qt::LeftButton,Qt::NoModifier,point);
    QMouseEvent move(QEvent::MouseMove,QPointF(destination),QPointF(global),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(handle,&move);QTest::mouseRelease(handle,Qt::LeftButton,Qt::NoModifier,handle->mapFromGlobal(global));
    return h.rails()->sizes()!=before;
}
bool recoveryDialog(Harness& h,const QString& destination,bool cancel=false,bool removeParentAfterAccept=false) {
    auto* button=h.window.findChild<QPushButton*>("linkedSaveCopy");if(!button||!button->isVisible())return false;
    bool seen=false,finished=false;
    QTimer::singleShot(0,&h.window,[&]{
        auto* dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget());if(!dialog)return;
        seen=true;
        if(cancel){dialog->reject();finished=true;return;}
        dialog->setOption(QFileDialog::DontConfirmOverwrite);dialog->selectFile(destination);
        finished=QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection)&&dialog->result()==QDialog::Accepted;
        if(finished&&removeParentAfterAccept)finished=QDir().rmdir(QFileInfo(destination).absolutePath());
    });
    button->click();return seen&&finished;
}
}

class LinkedNativeRailsTest : public QObject {
    Q_OBJECT
private slots:
    void defaultNativeFrozen() {
        const QByteArray bytes=readFile(QFileInfo(QString::fromUtf8(__FILE__)).dir().filePath("fixtures/linked_rails_layout/default-native.json"));
        QCOMPARE(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex(),QByteArray("07fd31225cc5d11be617bdbf3c4ec3b6e093e1035b8a7530e67d562fa7f4816d"));
        const auto expected=QJsonDocument::fromJson(bytes).object();QVERIFY(!expected.isEmpty());
        Harness h;QVERIFY(h.ready);const auto actual=DefaultNativeObservation::capture(h);QVERIFY(!actual.isEmpty());
        const auto differences=differentFields(expected,actual);
        QVERIFY2(differences.isEmpty(),qPrintable(differences.join('\n')));
        const auto phases=actual["phases"].toArray();QCOMPARE(phases.size(),7);
        for(qsizetype i=1;i<phases.size();++i)QVERIFY(!differentFields(phases[i-1].toObject()["state"],phases[i].toObject()["state"]).isEmpty());
        auto changed=actual;auto alteredPhases=phases;auto phase=alteredPhases[0].toObject();
        auto state=phase["state"].toObject();auto editor=state["editor"].toObject();
        editor["position"]=editor["position"].toInt()+1;state["editor"]=editor;phase["state"]=state;
        alteredPhases[0]=phase;changed["phases"]=alteredPhases;
        QCOMPARE(differentFields(actual,changed),QStringList({"/phases/0/state/editor/position"}));
    }
    void frozenNativeSensitivity() {
        const auto bytes=readFile(QFileInfo(QString::fromUtf8(__FILE__)).dir().filePath("fixtures/linked_rails_layout/default-native.json"));
        const auto expected=QJsonDocument::fromJson(bytes).object();QVERIFY(!expected.isEmpty());
        for(const auto& field:QStringList{"references","paintedPaths","association"}){
            auto altered=expected;auto phases=altered["phases"].toArray();auto phase=phases[0].toObject();auto state=phase["state"].toObject();QString path;
            if(field=="references"){state.remove(field);path="/phases/0/state/references";}
            else if(field=="paintedPaths"){auto connector=state["connector"].toObject();connector.remove(field);state["connector"]=connector;path="/phases/0/state/connector/paintedPaths";}
            else {auto references=state["references"].toArray();auto first=references[0].toObject();first["anchor"]="wrong-anchor";references[0]=first;state["references"]=references;path="/phases/0/state/references/0/anchor";}
            phase["state"]=state;phases[0]=phase;altered["phases"]=phases;QCOMPARE(differentFields(expected,altered),QStringList{path});
        }
    }
    void foldAggregationAndRetainedCards() {
        Harness h;QVERIFY(h.ready);QVERIFY(prepareFoldSource(h));
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
        Harness h;QVERIFY(h.ready);QVERIFY(prepareFoldSource(h));
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
        Harness h;QVERIFY(h.ready);QVERIFY(prepareFoldSource(h));
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
        Harness h;QVERIFY(h.ready);h.defer=true;
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
        Harness h;QVERIFY(h.ready);
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
        Harness h;QVERIFY(h.ready);
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
        Harness h;QVERIFY(h.ready);
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
        Harness h;QVERIFY(h.ready);
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
        Harness h;QVERIFY(h.ready);h.defer=true;
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
        Harness h;QVERIFY(h.ready);
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
        Harness h;QVERIFY(h.ready);
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
        Harness h;QVERIFY(h.ready);
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
    void nativeReentryAndRestoration() {
        Harness h;QVERIFY(h.ready);QVERIFY(h.controller->setActive(true));
        QVERIFY(h.snapshot()["native"].toBool());QVERIFY(h.rails());
        QVERIFY(h.window.findChildren<WebEditorContainer*>("linkedContextWebView").isEmpty());
        auto* original=h.page;auto* editor=h.editor();auto* document=editor->document();
        const QJsonValue session=h.snapshot()["session"];
        QVERIFY(h.controller->setActive(true));QVERIFY(h.rails());
        QCOMPARE(h.snapshot()["session"],session);
        QVERIFY(h.controller->setActive(false));QCOMPARE(h.stack->currentWidget(),original);
        QCOMPARE(h.editor(),editor);QCOMPARE(editor->document(),document);
        QVERIFY(h.controller->setActive(true));QVERIFY(h.rails());
        QVERIFY(h.window.findChildren<WebEditorContainer*>("linkedContextWebView").isEmpty());
        QVERIFY(h.controller->setActive(false));
    }
    void nativeSessionAndStructure() {
        Harness h;QVERIFY(h.ready);
        QVERIFY(h.store({h.anchor("a",2,{"text"}),h.anchor("b",3,{"image"})}));
        auto* editor=h.editor();auto* document=editor->document();
        QVERIFY(h.controller->setActive(true));auto* rails=h.rails();QVERIFY(rails);
        QCOMPARE(rails->count(),3);QCOMPARE(rails->widget(1),h.page);QCOMPARE(h.editor(),editor);QCOMPARE(editor->document(),document);
        QCOMPARE(rails->findChildren<QScrollArea*>().size(),2);
        QVERIFY(h.window.findChildren<WebEditorContainer*>().isEmpty());
        QVERIFY(!h.window.findChild<QWidget*>("linkedContextActions"));
        auto* recovery=h.window.findChild<QPushButton*>("linkedSaveCopy");QVERIFY(recovery);QVERIFY(!recovery->isVisible());
        QVERIFY(h.snapshot()["initialized"].toBool());
        QTRY_VERIFY(h.allResourcesComplete());
        const QJsonValue session=h.snapshot()["session"];
        QVERIFY(h.controller->setActive(true));QCOMPARE(h.rails(),rails);QCOMPARE(h.snapshot()["session"],session);
        QVERIFY(h.tabs->openFileInTab(h.second));QVERIFY(h.snapshot()["native"].toBool());QCOMPARE(h.rails(),rails);
        QVERIFY(h.snapshot()["session"]!=session);
        QElapsedTimer elapsed;elapsed.start();QVERIFY(h.controller->setActive(false));QVERIFY(elapsed.elapsed()<1000);
        QCOMPARE(h.stack->currentWidget(),h.page);QVERIFY(!h.rails());
        QVERIFY(h.controller->setActive(true));QVERIFY(h.rails());QVERIFY(h.controller->setActive(false));
    }
    void groupsReferencesAndUnavailable() {
        Harness h;QVERIFY(h.ready);
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
        Harness h;QVERIFY(h.ready);h.defer=true;
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
        Harness h;QVERIFY(h.ready);
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
        Harness h;QVERIFY(h.ready);
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
        Harness h;QVERIFY(h.ready);h.defer=true;h.errorSources.insert("error");
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
        Harness h;QVERIFY(h.ready);h.defer=true;
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
    void nativeSchemaRejectsInvalid_data() {
        QTest::addColumn<QJsonObject>("state");
        const auto add=[](const char* name,QJsonObject value){QTest::newRow(name)<<value;};
        auto value=nativeState();value["version"]=3.5;add("fractional-version",value);
        value=nativeState();value["version"]=4;add("unknown-version",value);
        value=nativeState();value["codeRatio"]=.9;add("compat-ratio",value);
        value=nativeState();value["referenceIds"]=QJsonArray{"same","same"};add("duplicate-reference",value);
        value=nativeState();value["referenceIds"]=QJsonArray{12};add("non-string-reference",value);
        value=nativeState();value["referenceIds"]=QJsonArray{QString(513,'x')};add("long-reference",value);
        value=nativeState();QJsonArray tooMany;for(int i=0;i<10001;++i)tooMany.append(QString::number(i));value["referenceIds"]=tooMany;add("reference-limit",value);
        value=nativeState();value.remove("expandedReferences");add("missing-deep-list",value);
        value=nativeState();value["expandedReferences"]=QJsonArray{true};add("non-object-deep",value);
        value=nativeState();value["nativeView"]=false;add("non-object-view",value);
        const auto changeView=[&](const char* name,const QString& key,QJsonValue field){auto state=nativeState();auto view=state["nativeView"].toObject();view[key]=field;state["nativeView"]=view;add(name,state);};
        changeView("unknown-view-version","version",2);changeView("fractional-view-version","version",1.25);
        changeView("short-weights","weights",QJsonArray{1,2});changeView("negative-weight","weights",QJsonArray{1,-1,2});
        changeView("zero-weights","weights",QJsonArray{0,0,0});changeView("string-weight","weights",QJsonArray{1,"2",3});
        changeView("large-weight","weights",QJsonArray{1,1e10,3});changeView("null-weight","weights",QJsonArray{1,QJsonValue::Null,3});
        changeView("duplicate-expansion","expanded",QJsonArray{"a","a"});changeView("empty-expansion-id","expanded",QJsonArray{""});
        changeView("long-expansion-id","expanded",QJsonArray{QString(513,'x')});changeView("expansion-limit","expanded",tooMany);
        QJsonObject deep{{"id","a/link-1"},{"parent","a/link-0"},{"anchor","a"},{"title","deep"},{"source",sourceUri("deep")}};
        const auto changeDeep=[&](const char* name,const QString& key,QJsonValue field){auto state=nativeState();auto item=deep;item[key]=field;state["expandedReferences"]=QJsonArray{item};add(name,state);};
        changeDeep("missing-parent","parent","absent");changeDeep("self-parent","parent","a/link-1");
        changeDeep("invalid-source","source","invalid://source");changeDeep("wrong-title-type","title",9);
        changeDeep("long-title","title",QString(8193,'x'));changeDeep("long-anchor","anchor",QString(513,'x'));
        value=nativeState();value["expandedReferences"]=QJsonArray{deep,deep};add("duplicate-deep",value);
        auto first=deep;first["id"]="a/link-0";first["parent"]="a/link-1";
        value=nativeState();value["expandedReferences"]=QJsonArray{first,deep};add("cyclic-deep",value);
        value=savedState();value["nativeView"]=QJsonObject{{"version",1},{"weights",QJsonArray{0,0,0}},{"expanded",QJsonArray{}}};add("legacy-invalid-view",value);
    }
    void nativeSchemaRejectsInvalid() {
        QFETCH(QJsonObject,state);QString failure;QVERIFY(!LinkedContextState::validate(state,&failure));QVERIFY(!failure.isEmpty());
    }
    void nativeSchemaAndLegacyCompatibility() {
        QString failure;QVERIFY(LinkedContextState::validate(nativeState({"a"}),&failure));
        auto legacy=savedState();legacy["expandedReferences"]=QJsonArray{QJsonObject{{"legacyUnknown",42}}};
        QVERIFY(LinkedContextState::validate(legacy,&failure));legacy["nativeView"]=nativeState()["nativeView"];
        QVERIFY(LinkedContextState::validate(legacy,&failure));
        auto state=nativeState();QJsonArray ids,deeper;
        ids.append("root");
        for(int i=1;i<10000;++i){const auto id=QString::number(i),parent=i==1?QString("root"):QString::number(i-1);ids.append(id);
            deeper.append(QJsonObject{{"id",id},{"parent",parent},{"anchor","a"},{"title",""},{"source",sourceUri("deep")}});}
        state["referenceIds"]=ids;state["expandedReferences"]=deeper;QVERIFY(LinkedContextState::validate(state,&failure));
        deeper.append(deeper.last());state["expandedReferences"]=deeper;QVERIFY(!LinkedContextState::validate(state,&failure));
    }
    void nativeEmptyBoardAndAsymmetricViewRoundtrip() {
        Harness h;QVERIFY(h.ready);QVERIFY(h.store({h.anchor("a",2,{"one","two"})}));
        LinkedContextState board;QVERIFY(board.load(h.temp.path(),h.file));const auto path=board.path;
        QVERIFY(h.controller->setActive(true));QTRY_VERIFY(h.allResourcesComplete());QVERIFY(drainSaveWindow());QVERIFY(!QFileInfo::exists(path));
        auto* button=expandButton(h,"a");QVERIFY(button);button->click();QVERIFY(dragSplitter(h,1,420));
        const auto sizes=h.rails()->sizes();const int total=sizes[0]+sizes[1]+sizes[2];
        QVERIFY(sizes[0]!=sizes[2]);QVERIFY(double(sizes[1])/total<.3||double(sizes[1])/total>.7);
        QTRY_VERIFY(QFileInfo::exists(path));const auto saved=diskState(path);QCOMPARE(saved["version"].toInt(),3);
        QVERIFY(!saved.contains("native"));QVERIFY(!saved.contains("viewport"));QVERIFY(!saved.contains("documentViews"));
        const auto weights=saved["nativeView"].toObject()["weights"].toArray();QCOMPARE(weights.size(),3);
        for(int i=0;i<3;++i)QCOMPARE(weights[i].toDouble(),double(sizes[i])/total);
        QCOMPARE(saved["nativeView"].toObject()["expanded"].toArray(),QJsonArray{"a"});
        QVERIFY(saved["codeRatio"].toDouble()>=.3&&saved["codeRatio"].toDouble()<=.7);
        const auto bytes=readFile(path);QVERIFY(h.controller->setActive(false));h.window.resize(1800,850);
        QVERIFY(h.controller->setActive(true));QTRY_VERIFY(h.allResourcesComplete());
        QCOMPARE(h.rails()->expandedAnchors(),QStringList{"a"});const auto restored=h.rails()->sizes();const int sum=restored[0]+restored[1]+restored[2];
        QCOMPARE(restored[0],qRound(weights[0].toDouble()*sum));QCOMPARE(restored[1],qRound(weights[1].toDouble()*sum));
        QCOMPARE(restored[2],sum-restored[0]-restored[1]);QVERIFY(drainSaveWindow());QCOMPARE(readFile(path),bytes);
        QVERIFY(h.controller->setActive(false));QCOMPARE(readFile(path),bytes);
    }
    void nativeDeepReferencesAndReadOnlyIdentity() {
        Harness h;QVERIFY(h.ready);QVERIFY(h.store({h.anchor("a",2,{"one","two"})}));
        auto original=savedState(.6,QJsonArray{QJsonObject{{"id","a/link-0/deep"},{"anchor","a"},{"parent","a/link-0"},{"title","Preserved title"},{"source",sourceUri("deep")}}});
        const auto path=h.board(h.file,original);QVERIFY(!path.isEmpty());const auto bytes=readFile(path);
        QVERIFY(h.controller->setActive(true));QTRY_VERIFY(h.allResourcesComplete());QCOMPARE(h.snapshot()["references"].toArray().size(),3);
        QVERIFY(h.controller->setActive(false));QCOMPARE(readFile(path),bytes);
        QVERIFY(h.controller->setActive(true));expandButton(h,"a")->click();QTRY_VERIFY(readFile(path)!=bytes);
        const auto changed=diskState(path);for(const auto& key:QStringList{"version","native","viewport","documentViews","expandedReferences","referenceIds","unknownStage1Sentinel"})QCOMPARE(changed[key],original[key]);
        QCOMPARE(changed["nativeView"].toObject()["expanded"].toArray(),QJsonArray{"a"});QVERIFY(h.controller->setActive(false));
    }
    void nativeInvalidBoardPreserved_data() {
        QTest::addColumn<int>("kind");QTest::newRow("corrupt")<<0;QTest::newRow("unknown-version")<<1;QTest::newRow("invalid-native")<<2;
    }
    void nativeInvalidBoardPreserved() {
        QFETCH(int,kind);Harness h;QVERIFY(h.ready);QVERIFY(h.store({h.anchor("a",2,{"one","two"})}));
        LinkedContextState board;QVERIFY(board.load(h.temp.path(),h.file));QVERIFY(board.save(nativeState()));
        const auto identity=QJsonDocument::fromJson(readFile(board.path)).object()["file"].toString();QVERIFY(!identity.isEmpty());
        auto invalid=nativeState();invalid["version"]=kind==1?4:3;
        if(kind==2){auto view=invalid["nativeView"].toObject();view["weights"]=QJsonArray{0,0,0};invalid["nativeView"]=view;}
        const QByteArray bytes=kind==0?QByteArray("unreadable board bytes"):QJsonDocument(QJsonObject{{"file",identity},{"state",invalid}}).toJson();
        QVERIFY(writeFile(board.path,bytes));QVERIFY(h.controller->setActive(true));QTRY_VERIFY(h.allResourcesComplete());
        auto* copy=h.window.findChild<QPushButton*>("linkedSaveCopy");QVERIFY(copy);QVERIFY(copy->isVisible());
        QVERIFY(drainSaveWindow());QCOMPARE(readFile(board.path),bytes);QVERIFY(h.controller->setActive(false));QCOMPARE(readFile(board.path),bytes);
        QVERIFY(h.controller->setActive(true));const auto destination=h.temp.filePath("recovery.json");QVERIFY(recoveryDialog(h,destination));
        const auto recovered=diskState(destination);QString failure;QVERIFY(LinkedContextState::validate(recovered,&failure));QCOMPARE(recovered["version"].toInt(),3);
        QCOMPARE(QJsonDocument::fromJson(readFile(destination)).object()["file"].toString(),identity);QCOMPARE(readFile(board.path),bytes);
        QVERIFY(h.controller->setActive(false));
    }
    void nativeRecoveryActualButtons() {
        Harness h;QVERIFY(h.ready);QVERIFY(h.store({h.anchor("a",2,{"one","two"})}));const auto path=h.board(h.file,savedState());QVERIFY(!path.isEmpty());
        QVERIFY(h.controller->setActive(true));QTRY_VERIFY(h.allResourcesComplete());const QByteArray external="preserve external";QVERIFY(writeFile(path,external));
        expandButton(h,"a")->click();auto* status=h.window.findChild<QLabel*>("linkedContextStatus");QVERIFY(status);QTRY_VERIFY(status->text().contains("changed on disk"));
        QVERIFY(!h.controller->setActive(false));QVERIFY(recoveryDialog(h,{},true));QCOMPARE(readFile(path),external);QVERIFY(!h.controller->setActive(false));
        QVERIFY(recoveryDialog(h,path));QVERIFY(status->text().contains("Choose another file"));QCOMPARE(readFile(path),external);
        const auto absentParent=h.temp.filePath("remove-after-dialog");QVERIFY(QDir().mkpath(absentParent));const auto failed=absentParent+"/copy.json";
        QVERIFY(recoveryDialog(h,failed,false,true));QVERIFY(!QFileInfo::exists(failed));QVERIFY(!h.controller->setActive(false));QCOMPARE(readFile(path),external);
        const auto first=h.temp.filePath("first-copy.json");QVERIFY(recoveryDialog(h,first));QVERIFY(QFileInfo::exists(first));
        const auto exported=diskState(first);QCOMPARE(exported["nativeView"].toObject()["expanded"].toArray(),QJsonArray{"a"});
        expandButton(h,"a")->click();QVERIFY(!h.controller->setActive(false));QCOMPARE(diskState(first),exported);QCOMPARE(readFile(path),external);
        const auto second=h.temp.filePath("second-copy.json");QVERIFY(recoveryDialog(h,second));QVERIFY(diskState(second)!=exported);
        QCOMPARE(diskState(second)["nativeView"].toObject()["expanded"].toArray(),QJsonArray{});
        QVERIFY(h.controller->setActive(false));QVERIFY(drainSaveWindow());QCOMPARE(readFile(path),external);
    }
    void nativeReloadCancelsPendingSave() {
        Harness h;QVERIFY(h.ready);QVERIFY(h.store({h.anchor("a",2,{"one","two"})}));const auto path=h.board(h.file,nativeState());QVERIFY(!path.isEmpty());
        const auto bytes=readFile(path);QVERIFY(h.controller->setActive(true));QTRY_VERIFY(h.allResourcesComplete());
        const auto session=h.snapshot().value("session");expandButton(h,"a")->click();h.controller->reloadBoard();
        QVERIFY(h.snapshot()["session"]!=session);QCOMPARE(h.rails()->expandedAnchors(),QStringList{});QVERIFY(drainSaveWindow());QCOMPARE(readFile(path),bytes);
        expandButton(h,"a")->click();auto envelope=QJsonDocument::fromJson(bytes).object();auto external=nativeState({"a"});
        auto view=external["nativeView"].toObject();view["weights"]=QJsonArray{.1,.6,.3};external["nativeView"]=view;envelope["state"]=external;
        const auto externalBytes=QJsonDocument(envelope).toJson();QVERIFY(writeFile(path,externalBytes));h.controller->reloadBoard();
        QCOMPARE(h.rails()->expandedAnchors(),QStringList{"a"});QVERIFY(drainSaveWindow());QCOMPARE(readFile(path),externalBytes);
        QVERIFY(h.controller->setActive(false));QCOMPARE(readFile(path),externalBytes);
    }
    void nativeTransitionCommitAndRename() {
        Harness h;QVERIFY(h.ready);QVERIFY(h.store({h.anchor("a",2,{"one","two"})}));
        LinkedContextState first,second;QVERIFY(first.load(h.temp.path(),h.file));QVERIFY(second.load(h.temp.path(),h.second));
        QVERIFY(h.controller->setActive(true));QTRY_VERIFY(h.allResourcesComplete());expandButton(h,"a")->click();
        QVERIFY(h.tabs->openFileInTab(h.second));QVERIFY(QFileInfo::exists(first.path));QVERIFY(drainSaveWindow());QVERIFY(!QFileInfo::exists(second.path));
        QVERIFY(h.tabs->activateOpenFile(h.file));QCOMPARE(h.rails()->expandedAnchors(),QStringList{"a"});
        const auto previous=h.editor();const auto renamed=h.temp.filePath("renamed.sv");const auto sizes=h.rails()->sizes();
        QVERIFY(h.tabs->saveEditorView(previous,true,renamed));QCOMPARE(h.editor(),previous);QCOMPARE(h.editor()->documentFileName(),renamed);QCOMPARE(h.rails()->sizes(),sizes);
        LinkedContextState moved;QVERIFY(moved.load(h.temp.path(),renamed));QTRY_VERIFY(QFileInfo::exists(moved.path));
        QCOMPARE(diskState(moved.path)["nativeView"].toObject()["expanded"].toArray(),QJsonArray{"a"});
        QVERIFY(h.controller->setActive(false));
    }
    void nativeFailedTransitionsAndDestructor() {
        Harness h;QVERIFY(h.ready);QVERIFY(h.store({h.anchor("a",2,{"one","two"})}));const auto path=h.board(h.file,savedState());QVERIFY(!path.isEmpty());
        const auto bytes=readFile(path);QVERIFY(h.controller->setActive(true));QTRY_VERIFY(h.allResourcesComplete());
        QLockFile lock(path+".lock");QVERIFY(lock.tryLock(0));expandButton(h,"a")->click();
        const auto session=h.snapshot().value("session");auto* editor=h.editor();
        QVERIFY(h.tabs->openFileInTab(h.second));QCOMPARE(h.editor(),editor);QCOMPARE(h.editor()->documentFileName(),h.file);
        QCOMPARE(h.snapshot()["session"],session);QVERIFY(h.controller->isActive());
        const auto tabCount=h.tabWidget->count();h.tabs->closeTab(h.tabWidget->currentIndex());QCOMPARE(h.tabWidget->count(),tabCount);QCOMPARE(h.editor(),editor);
        const auto rejectedRename=h.temp.filePath("must-not-rename.sv");QVERIFY(!h.tabs->saveEditorView(editor,true,rejectedRename));QVERIFY(!QFileInfo::exists(rejectedRename));
        QVERIFY(!h.controller->setActive(false));QCOMPARE(readFile(path),bytes);lock.unlock();QVERIFY(drainSaveWindow());QCOMPARE(readFile(path),bytes);
        const auto other=h.temp.filePath("another-workspace");QVERIFY(QDir().mkpath(other));
        const QByteArray external="changed outside";QVERIFY(writeFile(path,external));h.links.setWorkspaceRoot(other);h.controller->refreshAnchors();
        QVERIFY(h.controller->isActive());QCOMPARE(readFile(path),external);
        LinkedContextState otherBoard;QVERIFY(otherBoard.load(other,h.file));QVERIFY(!QFileInfo::exists(otherBoard.path));
        const auto recovery=h.temp.filePath("old-identity-copy.json");QVERIFY(recoveryDialog(h,recovery));
        QCOMPARE(QJsonDocument::fromJson(readFile(recovery)).object()["file"],QJsonDocument::fromJson(bytes).object()["file"]);
        QVERIFY(h.controller->setActive(false));QVERIFY(!QFileInfo::exists(otherBoard.path));
        h.links.setWorkspaceRoot(h.temp.path());QVERIFY(writeFile(path,bytes));QVERIFY(h.controller->setActive(true));
        QTRY_VERIFY(h.allResourcesComplete());expandButton(h,"a")->click();h.controller.reset();
        QCOMPARE(h.stack->currentWidget(),h.page);QVERIFY(!h.rails());QVERIFY(drainSaveWindow());QCOMPARE(readFile(path),bytes);
    }
    void nativeReferenceChangesAndParserFailure() {
        Harness h;QVERIFY(h.ready);QVERIFY(h.store({h.anchor("a",2,{"one","two"})}));const auto path=h.board(h.file,nativeState());QVERIFY(!path.isEmpty());
        QVERIFY(h.controller->setActive(true));QTRY_VERIFY(h.allResourcesComplete());const auto before=readFile(path);
        h.editor()->setPlainText("// temporarily no parsed anchors\n");QVERIFY(drainSaveWindow());QCOMPARE(readFile(path),before);
        h.editor()->setPlainText(h.text);QVERIFY(drainSaveWindow());QCOMPARE(readFile(path),before);
        QVERIFY(h.store({h.anchor("a",2,{"one","two","third"})}));h.controller->refreshAnchors();
        QTRY_COMPARE(diskState(path)["referenceIds"].toArray().size(),3);
        const auto saved=readFile(path);h.controller->setPinloomHostClient(h.client.get());QVERIFY(drainSaveWindow());QCOMPARE(readFile(path),saved);
        QVERIFY(h.controller->setActive(false));
    }
    void sharedGeometrySourcesAndCoalescing() {
        Harness h;QVERIFY(h.ready);QVERIFY(h.store({h.anchor("a",2,{"one","two"}),h.anchor("b",20,{"three"})}));
        QVERIFY(h.controller->setActive(true));QVERIFY(DefaultNativeObservation::settled(h));
        auto* layer=h.window.findChild<LinkedConnectorOverlay*>();QVERIFY(layer);
        const auto fresh=[&](quint64 previous){return DefaultNativeObservation::settled(h,double(previous));};
        auto serial=layer->geometrySerial();h.window.resize(h.window.width()-40,h.window.height());QVERIFY(fresh(serial));
        serial=layer->geometrySerial();QTextCursor cursor(h.editor()->document());cursor.setPosition(h.text.indexOf("signal_002"));h.editor()->setTextCursor(cursor);QVERIFY(fresh(serial));
        serial=layer->geometrySerial();cursor.setPosition(cursor.position()+10,QTextCursor::KeepAnchor);h.editor()->setTextCursor(cursor);QVERIFY(fresh(serial));
        serial=layer->geometrySerial();h.editor()->verticalScrollBar()->setValue(1);QVERIFY(fresh(serial));
        serial=layer->geometrySerial();QTextCursor edit(h.editor()->document());edit.movePosition(QTextCursor::End);edit.insertText("// shared geometry edit\n");QVERIFY(fresh(serial));
        QCOMPARE(layer->completion().identity.contentRevision,qint64(h.editor()->semanticDocumentRevision()));
        serial=layer->geometrySerial();auto row=h.anchor("a",2,{"one","two"});auto links=row["links"].toArray();auto link=links[0].toObject();link["title"]="updated";links[0]=link;row["links"]=links;
        QVERIFY(h.store({row,h.anchor("b",20,{"three"})}));h.controller->refreshAnchors();
        QTRY_COMPARE(h.snapshot()["references"].toArray()[0].toObject()["title"].toString(),QString("updated"));QVERIFY(fresh(serial));
        auto* locate=h.rails()->findChild<QPushButton*>("linkedNativeLocate");QVERIFY(locate);serial=layer->geometrySerial();locate->click();QVERIFY(fresh(serial));
        QSignalSpy applied(layer,&LinkedConnectorOverlay::geometryApplied);serial=layer->geometrySerial();
        for(int i=0;i<40;++i){QTextCursor c(h.editor()->document());c.setPosition(i+1);h.editor()->setTextCursor(c);}
        QVERIFY(fresh(serial));QVERIFY(!applied.isEmpty());QVERIFY(applied.size()<40);
        QCOMPARE(layer->completion().geometrySerial,layer->geometrySerial());QCOMPARE(layer->completion().identity.requestSerial,layer->scene().identity.requestSerial);
        QCOMPARE(layer->completion().identity.layoutSerial,layer->scene().identity.layoutSerial);QCOMPARE(layer->completion().identity.contentRevision,qint64(h.editor()->semanticDocumentRevision()));
        QVERIFY(h.controller->setActive(false));
    }
    void nativeBoardBytesAndDefaultSaveFailure() {
        Harness h;QVERIFY(h.ready);
        QVERIFY(h.store({h.anchor("a",2,{"text","second"})}));const auto original=savedState(.6);
        const auto path=h.board(h.file,original);QVERIFY(!path.isEmpty());const auto bytes=readFile(path);
        QVERIFY(h.controller->setActive(true));QTRY_VERIFY(h.allResourcesComplete());
        h.rails()->setSizes({200,800,400});h.controller->reloadBoard();QVERIFY(h.tabs->openFileInTab(h.second));
        QVERIFY(h.tabs->activateOpenFile(h.file));QVERIFY(h.controller->setActive(false));QCOMPARE(readFile(path),bytes);
        QVERIFY(h.controller->setActive(true));QTRY_VERIFY(h.allResourcesComplete());
        auto* toggle=expandButton(h,"a");QVERIFY(toggle);toggle->click();
        QTRY_VERIFY(readFile(path)!=bytes);
        const auto changed=diskState(path);QCOMPARE(changed["native"],original["native"]);QCOMPARE(changed["viewport"],original["viewport"]);
        QCOMPARE(changed["documentViews"],original["documentViews"]);QCOMPARE(changed["unknownStage1Sentinel"],original["unknownStage1Sentinel"]);
        QCOMPARE(changed["nativeView"].toObject()["expanded"].toArray(),QJsonArray{"a"});
        const QByteArray external="external revision preserved";QVERIFY(writeFile(path,external));toggle->click();
        auto* status=h.window.findChild<QLabel*>("linkedContextStatus");QVERIFY(status);
        QTRY_VERIFY(status->text().contains("changed on disk"));QCOMPARE(readFile(path),external);
        auto* recovery=h.window.findChild<QPushButton*>("linkedSaveCopy");QVERIFY(recovery);QVERIFY(recovery->isVisible());
        QVERIFY(!h.controller->setActive(false));QCOMPARE(readFile(path),external);
    }
};
int main(int argc,char** argv) {
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc,argv);
    QTemporaryDir settings(QDir::tempPath()+"/rails-settings-XXXXXX");if(!settings.isValid())return 2;
    QCoreApplication::setOrganizationName("ZeroSlackNativeRailsTest");QCoreApplication::setApplicationName("Stage1");
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    if(!initializeUiStyleForTest())return 3;
    LinkedNativeRailsTest test;return QTest::qExec(&test,argc,argv);
}
#include "linked_native_rails_test.moc"
