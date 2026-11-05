#include "linkedcontextcontroller.h"
#include "linkedcontextstate.h"
#include "linkedanchorgeometry.h"
#include "linkednativerails.h"
#include "mycodeeditor.h"
#include "pinloomcodelinkstore.h"
#include "pinloomhostclient.h"
#include "tabmanager.h"
#include "testuistyle.h"
#include "webeditorcontainer.h"
#include <QAbstractTextDocumentLayout>
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
}

class LinkedNativeRailsTest : public QObject {
    Q_OBJECT
private slots:
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
        auto* rails=h.rails();QVERIFY(rails);QTest::qWait(30);
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
