#include "linkedcontextcontroller.h"
#include "linkedcontextstate.h"
#include "webeditorcontainer.h"
#include "tabmanager.h"
#include "mycodeeditor.h"
#include "pinloomcodelinkstore.h"
#include "pinloomhostclient.h"
#include "pinloompreviewimage.h"
#include <QApplication>
#include <QBuffer>
#include <QCryptographicHash>
#include <QClipboard>
#include <QMimeData>
#include <QDockWidget>
#include <QDesktopServices>
#include <QEventLoop>
#include <QFileInfo>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMainWindow>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSlider>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTextBrowser>
#include <QTimer>
#include <QToolBar>
#include <QUuid>
#include <QVBoxLayout>
#include <algorithm>
#include <map>

namespace {
QString sourceKey(const PinloomHostIdentity& identity) { return identity.toUri().toString(QUrl::FullyEncoded); }
QString shortKey(const QString& value) { return QString::fromLatin1(QCryptographicHash::hash(value.toUtf8(), QCryptographicHash::Sha256).toHex()); }
class AnchorOverlay final : public QWidget {
public:
    explicit AnchorOverlay(MyCodeEditor* owner) : QWidget(owner->viewport()), editor(owner) {
        setAttribute(Qt::WA_TransparentForMouseEvents); setAttribute(Qt::WA_NoSystemBackground);
    }
    QPointer<MyCodeEditor> editor;
    QList<ResolvedPinloomCodeLink> anchors;
    QString selected;
    void paintEvent(QPaintEvent*) override {
        if (!editor) return;
        QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
        const auto selection = editor->textCursor();
        for (const auto& anchor : anchors) {
            if (!anchor.available()) continue;
            const bool active = anchor.anchor.id == selected || (selection.selectionStart() <= anchor.endPosition && selection.selectionEnd() >= anchor.startPosition);
            if (!active) continue;
            QTextCursor cursor(editor->document()); cursor.setPosition(qBound(0, anchor.startPosition, editor->document()->characterCount()-1));
            const QRect first = editor->cursorRect(cursor);
            cursor.setPosition(qBound(0, anchor.endPosition, editor->document()->characterCount()-1));
            const QRect last = editor->cursorRect(cursor);
            if (last.bottom()<0 || first.top()>height()) continue;
            const QRect band(0, qMax(0,first.top()), width(), qMin(height(),last.bottom()+1)-qMax(0,first.top()));
            painter.fillRect(band,QColor(52,120,212,18)); painter.setPen(QPen(QColor(52,120,212,180),1.5));
            const int y=qBound(0,first.center().y(),height()-1);
            painter.drawLine(0,y,qMax(0,first.x()-3),y); painter.drawLine(qMin(width(),first.x()+6),y,width(),y);
            painter.drawRoundedRect(first.adjusted(-2,-1,2,1),2,2);
        }
    }
};
}

struct LinkedContextController::Private {
    LinkedContextController* q;
    QMainWindow* window;
    QStackedWidget* stack;
    QWidget* editorPage;
    TabManager* tabs;
    PinloomCodeLinkStore* links;
    PinloomHostClient* pinloom;
    QList<QPointer<QWidget>> chrome;
    QList<QPair<QPointer<QWidget>,bool>> visibility;
    QByteArray normalWindowState;
    QPointer<QWidget> originalFocus;
    struct EditorView { QPointer<MyCodeEditor> editor; QPointer<QTextDocument> document; int position=0,anchor=0,vertical=0,horizontal=0; };
    QList<EditorView> originalViews;
    QPointer<QWidget> page,canvas,center,detail;
    QPointer<WebEditorContainer> web;
    QPointer<QLabel> notice,detailTitle;
    QPointer<QTextBrowser> detailText;
    QPointer<PinloomPreviewImageLabel> detailImage;
    QPointer<QSlider> ratioControl;
    QPointer<QPushButton> saveCopyButton,primaryBoardButton;
    QPointer<AnchorOverlay> overlay;
    QPointer<MyCodeEditor> editor;
    QTimer anchorTimer,geometryTimer,saveTimer;
    QList<QMetaObject::Connection> editorConnections;
    LinkedContextState board;
    QJsonObject boardState;
    QString file,workspace,session,selected,expanded;
    double codeRatio=.5,webScale=1;
    bool active=false,ready=false,initialized=false,flushing=false,switching=false,destroying=false,boardReadOnly=false;
    QJsonObject exportedState;
    quint64 generation=0,layoutSerial=0;
    QList<ResolvedPinloomCodeLink> anchors;
    struct Reference { QString id,anchor,parent,title; PinloomHostIdentity identity; };
    QMap<QString,Reference> references;
    struct Resource {
        QPointer<QObject> consumer;
        PinloomHostDocument document;
        QImage image;
        QByteArray thumbnail;
        QString error;
        bool complete=false;
        QList<PinloomHostIdentity> deeper;
    };
    QMap<QString,std::shared_ptr<Resource>> resources;
    QSet<QString> demanded;
    QHash<QString,QJsonObject> memoryBoards;
    QRegion popupRegion;

    Private(LinkedContextController* owner,QMainWindow* w,QStackedWidget* s,QWidget* e,TabManager* t,PinloomCodeLinkStore* l,PinloomHostClient* p)
        :q(owner),window(w),stack(s),editorPage(e),tabs(t),links(l),pinloom(p) {
        anchorTimer.setSingleShot(true); anchorTimer.setInterval(180);
        geometryTimer.setSingleShot(true); geometryTimer.setInterval(20);
        saveTimer.setSingleShot(true); saveTimer.setInterval(300);
        QObject::connect(&anchorTimer,&QTimer::timeout,q,[this]{resolveAnchors();});
        QObject::connect(&geometryTimer,&QTimer::timeout,q,[this]{sendGeometry();});
        QObject::connect(&saveTimer,&QTimer::timeout,q,[this]{save();});
    }
    void status(const QString& text) { if(notice) notice->setText(text);if(saveCopyButton)saveCopyButton->setVisible(!board.error.isEmpty()&&!boardState.isEmpty());emit q->message(text); }
    void post(QJsonObject object) { object[QStringLiteral("session")]=session;if(web)web->postMessage(object); }
    void command(const QString& name) { post({{"type","command"},{"name",name}}); }
    QString boardKey() const { return workspace+QLatin1Char('\n')+(file.isEmpty()&&editor?editor->property("sharedDocumentId").toString():file); }
    void restoreExpandedReferences() {
        references.clear();
        for(const auto& value:boardState.value("expandedReferences").toArray()){
            const auto r=value.toObject();const auto identity=PinloomHostIdentity::fromUri(QUrl(r.value("source").toString()));
            if(identity.isValid())references.insert(r.value("id").toString(),{r.value("id").toString(),r.value("anchor").toString(),r.value("parent").toString(),r.value("title").toString(),identity});
        }
    }
    void prepareState() {
        boardState[QStringLiteral("codeRatio")]=codeRatio;
        QJsonArray deeper;
        for(const auto& r:references)if(!r.parent.isEmpty())deeper.append(QJsonObject{{"id",r.id},{"anchor",r.anchor},{"parent",r.parent},{"title",r.title},{"source",sourceKey(r.identity)}});
        boardState[QStringLiteral("expandedReferences")]=deeper;
    }
    bool save() {
        if(boardState.isEmpty())return true;
        prepareState();
        if(board.path.isEmpty()){memoryBoards.insert(boardKey(),boardState);return true;}
        if(boardReadOnly)return true;
        if(board.save(boardState)){exportedState={};return true;}
        status(board.error+QStringLiteral(" Save a copy or reload before leaving."));
        return exportedState==boardState;
    }
    bool flush(bool persist=true) {
        if(!active||!initialized||flushing)return true;
        flushing=true;saveTimer.stop();
        const auto serial=layoutSerial;
        const auto request=QUuid::createUuid().toString(QUuid::WithoutBraces);
        bool received=false;
        QEventLoop wait;QTimer timeout;timeout.setSingleShot(true);
        const auto connection=QObject::connect(web,&WebEditorContainer::messageReceived,&wait,[this,&wait,&received,serial,request](const QJsonObject& m){
            if(m.value("session").toString()!=session)return;
            if(m.value("type")=="layout"&&m.value("request")==request&&layoutSerial>serial){received=true;wait.quit();}
            else if(m.value("type")=="error")wait.quit();
        });
        QObject::connect(&timeout,&QTimer::timeout,&wait,&QEventLoop::quit);timeout.start(28000);
        post({{"type","command"},{"name","save"},{"request",request}});wait.exec(QEventLoop::ExcludeUserInputEvents);
        QObject::disconnect(connection);flushing=false;
        if(!received){status(QStringLiteral("The board could not finish saving. Resolve the pending import or board error before leaving."));return false;}
        if(persist)return save();prepareState();return true;
    }
    void dropResources() {
        for(const auto& resource:resources)if(resource->consumer){pinloom->cancelReadRequests(resource->consumer);delete resource->consumer;}
        resources.clear();demanded.clear();
        if(detailImage)detailImage->clearSourcePixmap();
    }
    void createPage() {
        page=new QWidget(stack);page->setObjectName("linkedContextPage");
        auto* layout=new QVBoxLayout(page);layout->setContentsMargins(0,0,0,0);layout->setSpacing(0);
        auto* bar=new QWidget(page);bar->setObjectName("linkedContextControls");auto* row=new QHBoxLayout(bar);row->setContentsMargins(8,5,8,5);
        auto button=[this,row,bar](const QString& title,const QString& name,auto handler){auto* b=new QPushButton(title,bar);b->setObjectName(name);row->addWidget(b);QObject::connect(b,&QPushButton::clicked,q,handler);return b;};
        button(QStringLiteral("Exit linked context"),"exitLinkedContext",[this]{q->setActive(false);});
        button(QStringLiteral("Restore references"),"linkedRestoreReferences",[this]{command("restoreReferences");});
        button(QStringLiteral("Save layout"),"linkedSave",[this]{flush();});
        button(QStringLiteral("Reload board"),"linkedReload",[this]{q->reloadBoard();});
        primaryBoardButton=button(QStringLiteral("Back to file board"),"linkedPrimaryBoard",[this]{command("primaryDocument");});primaryBoardButton->hide();
        saveCopyButton=button(QStringLiteral("Save a copy…"),"linkedSaveCopy",[this]{
            if(!flush(false))return;
            const auto destination=QFileDialog::getSaveFileName(window,QStringLiteral("Save board recovery copy"),board.path+QStringLiteral(".copy.json"),QStringLiteral("Board state (*.json)"));
            if(destination.isEmpty())return;QString error;
            if(board.writeCopy(destination,boardState,&error)){exportedState=boardState;status(QStringLiteral("Recovery copy saved: ")+destination);}else status(error);
        });saveCopyButton->hide();
        row->addWidget(new QLabel(QStringLiteral("Code width"),bar));ratioControl=new QSlider(Qt::Horizontal,bar);ratioControl->setObjectName("linkedCodeWidth");ratioControl->setRange(30,70);ratioControl->setValue(50);ratioControl->setMaximumWidth(110);row->addWidget(ratioControl);
        QObject::connect(ratioControl,&QSlider::valueChanged,q,[this](int v){codeRatio=v/100.;arrange();saveTimer.start();});
        notice=new QLabel(bar);notice->setObjectName("linkedContextStatus");notice->setMinimumWidth(100);row->addWidget(notice,1);
        layout->addWidget(bar);canvas=new QWidget(page);canvas->setObjectName("linkedContextCanvas");canvas->installEventFilter(q);layout->addWidget(canvas,1);
        const QString profileRoot=qEnvironmentVariable("ZEROSLACK_PROFILE_ROOT");
        const QString profile=(profileRoot.isEmpty()?QStandardPaths::writableLocation(QStandardPaths::CacheLocation):profileRoot)+QStringLiteral("/linked-context-webview");
        web=new WebEditorContainer(QCoreApplication::applicationDirPath()+QStringLiteral("/resources/document-editors"),profile,"linked",canvas);
        webScale=1;
        web->setObjectName("linkedContextWebView");
        web->setAssetResolver([this](const QUrl& url){
            WebEditorResource result;result.status=404;
            if(url.host()!=QStringLiteral("assets.zeroslack.invalid")||url.query()!=session)return result;
            for(auto it=resources.begin();it!=resources.end();++it)if(url.path()==QLatin1Char('/')+shortKey(it.key())&&!it.value()->thumbnail.isEmpty()){
                result.bytes=it.value()->thumbnail;result.contentType="image/png";result.status=200;break;
            }
            return result;
        });
        QObject::connect(web,&WebEditorContainer::messageReceived,q,[this](const QJsonObject& m){receive(m);});
        QObject::connect(web,&WebEditorContainer::initializationFailed,q,[this](const QString& error){status(error);});
        QObject::connect(web,&WebEditorContainer::externalLinkRequested,q,[](const QUrl& url){QDesktopServices::openUrl(url);});
        QObject::connect(web,&WebEditorContainer::commandRequested,q,[this](const QString& name){if(name=="save"||name=="saveAs")flush();else if(name=="undo"||name=="redo")command(name);});
        center=new QWidget(canvas);center->setObjectName("linkedContextNativeCenter");center->setAttribute(Qt::WA_NativeWindow);
        auto* centralLayout=new QVBoxLayout(center);centralLayout->setContentsMargins(0,0,0,0);
        stack->removeWidget(editorPage);centralLayout->addWidget(editorPage);editorPage->show();
        detail=new QWidget(canvas);detail->setObjectName("linkedContextDetail");detail->setAttribute(Qt::WA_NativeWindow);
        auto* detailLayout=new QVBoxLayout(detail);detailTitle=new QLabel(detail);detailTitle->setWordWrap(true);detailLayout->addWidget(detailTitle);
        detailImage=new PinloomPreviewImageLabel(detail);detailLayout->addWidget(detailImage,2);
        detailText=new QTextBrowser(detail);detailText->setOpenLinks(false);detailText->setOpenExternalLinks(false);detailLayout->addWidget(detailText,1);
        auto* original=new QPushButton(QStringLiteral("Open original"),detail);detailLayout->addWidget(original);QObject::connect(original,&QPushButton::clicked,q,[this]{open(expanded);});
        auto* back=new QPushButton(QStringLiteral("Back to board"),detail);back->setObjectName("linkedDetailBack");detailLayout->addWidget(back);QObject::connect(back,&QPushButton::clicked,q,[this]{expanded.clear();detail->hide();arrange();demand(demanded.values());});
        detail->hide();stack->addWidget(page);stack->setCurrentWidget(page);arrange();
    }
    void arrange() {
        if(!canvas||!center||!web)return;
        const int w=canvas->width(),h=canvas->height();
        const int centerWidth=qBound(qMin(280,w),qRound(w*(expanded.isEmpty()?codeRatio:.4)),w);
        const int x=expanded.isEmpty()&&w>=1280?(w-centerWidth)/2:0;
        center->setGeometry(x,0,centerWidth,qMax(0,h-76));web->setGeometry(canvas->rect());
        QRegion visible(canvas->rect());visible-=QRegion(center->geometry());
        if(expanded.isEmpty()){visible+=popupRegion;center->setMask(QRegion(center->rect()).subtracted(popupRegion.translated(-center->pos())));}
        else center->clearMask();
        if(!expanded.isEmpty()){detail->setGeometry(centerWidth+8,0,qMax(0,w-centerWidth-8),h);detail->show();visible-=QRegion(detail->geometry());detail->raise();}
        web->setMask(visible);center->raise();geometryTimer.start();
        if(overlay&&editor){overlay->setGeometry(editor->viewport()->rect());overlay->raise();overlay->update();}
    }
    QJsonObject geometry() const {
        QJsonArray values;
        if(editor&&canvas){
            const auto cursor=editor->textCursor();
            const auto origin=editor->viewport()->mapTo(canvas,QPoint());
            for(const auto& anchor:anchors){
                int y=origin.y();bool visible=false;
                if(anchor.available()){
                    QTextCursor c(editor->document());c.setPosition(qBound(0,anchor.startPosition,editor->document()->characterCount()-1));
                    const auto r=editor->cursorRect(c);y+=r.center().y();visible=r.bottom()>=0&&r.top()<editor->viewport()->height();
                }
                const bool related=anchor.anchor.id==selected||(anchor.available()&&cursor.selectionStart()<=anchor.endPosition&&cursor.selectionEnd()>=anchor.startPosition);
                values.append(QJsonObject{{"id",anchor.anchor.id},{"y",y},{"visible",visible},{"active",related}});
            }
        }
        return {{"width",canvas?canvas->width():1},{"height",canvas?canvas->height():1},{"left",center?center->x():0},{"right",center?center->geometry().right()+1:0},{"top",0},{"bottom",center?center->height():0},{"detail",!expanded.isEmpty()},{"anchors",values}};
    }
    void sendGeometry(){if(active&&initialized){post({{"type","geometry"},{"geometry",geometry()}});if(overlay)overlay->update();}}
    QJsonArray referenceJson() const {
        QJsonArray result;
        auto ordered=references.values();QMap<QString,int> positions;
        for(const auto& anchor:anchors)positions.insert(anchor.anchor.id,anchor.available()?anchor.startPosition:INT_MAX);
        std::stable_sort(ordered.begin(),ordered.end(),[&positions](const Reference& a,const Reference& b){return positions.value(a.anchor,INT_MAX)<positions.value(b.anchor,INT_MAX);});
        for(const auto& r:ordered){
            QString status=QStringLiteral("Missing anchor");
            for(const auto& anchor:anchors)if(anchor.anchor.id==r.anchor){status=anchor.available()?QStringLiteral("resolved"):anchor.resolution==PinloomCodeLinkResolution::Ambiguous?QStringLiteral("Ambiguous anchor"):QStringLiteral("Missing anchor");break;}
            result.append(QJsonObject{{"id",r.id},{"source",sourceKey(r.identity)},{"anchor",r.anchor},{"title",r.title},{"status",status},{"parent",r.parent}});
        }
        return result;
    }
    void initialize() {
        if(!active||!ready)return;
        initialized=false;
        post({{"type","initialize"},{"references",referenceJson()},{"state",boardState},{"readOnly",boardReadOnly},{"geometry",geometry()}});
    }
    void switchEditor(MyCodeEditor* next) {
        if(!active||switching)return;
        if(!next){q->setActive(false);return;}
        const QString nextFile=next->documentFileName();
        if(next==editor&&nextFile==file&&workspace==links->workspaceRoot())return;
        switching=true;
        if(!flush()){if(editor&&!file.isEmpty())tabs->activateOpenFile(file);switching=false;return;}
        const bool renamed=next==editor&&nextFile!=file;
        const auto previousState=boardState;
        ++generation;dropResources();
        for(const auto& c:editorConnections)QObject::disconnect(c);editorConnections.clear();
        if(overlay)delete overlay;
        if(editor)editor->viewport()->removeEventFilter(q);
        editor=next;file=nextFile;workspace=links->workspaceRoot();selected.clear();expanded.clear();if(detail)detail->hide();
        session=QUuid::createUuid().toString(QUuid::WithoutBraces);initialized=false;
        board.load(workspace,file);boardState=board.value;popupRegion=QRegion();references.clear();exportedState={};
        boardReadOnly=!board.path.isEmpty()&&!board.error.isEmpty();
        if(board.path.isEmpty()&&memoryBoards.contains(boardKey()))boardState=memoryBoards.value(boardKey());
        if(renamed&&boardState.isEmpty()&&board.error.isEmpty())boardState=previousState;
        restoreExpandedReferences();
        codeRatio=boardState.value("codeRatio").toDouble(.5);if(ratioControl){QSignalBlocker block(ratioControl);ratioControl->setValue(qRound(codeRatio*100));}
        overlay=new AnchorOverlay(editor);overlay->show();editor->viewport()->installEventFilter(q);
        editorConnections.append(QObject::connect(editor,&QPlainTextEdit::textChanged,q,[this]{anchorTimer.start();geometryTimer.start();}));
        editorConnections.append(QObject::connect(editor,&QPlainTextEdit::cursorPositionChanged,q,[this]{selected.clear();if(overlay)overlay->selected.clear();geometryTimer.start();}));
        editorConnections.append(QObject::connect(editor,&QPlainTextEdit::selectionChanged,q,[this]{geometryTimer.start();}));
        editorConnections.append(QObject::connect(editor,&QPlainTextEdit::updateRequest,q,[this]{geometryTimer.start();}));
        resolveAnchors();arrange();initialize();switching=false;
    }
    void resolveAnchors() {
        if(!active||!editor)return;
        anchors=links->linksForDocument(file,editor->cachedDocumentText(),editor->syntaxDocument());
        QMap<QString,Reference> direct;
        for(const auto& anchor:anchors)for(const auto& link:anchor.anchor.links){
            auto identity=PinloomHostIdentity::fromVariantMap(link.identity);if(!identity.isValid())identity=PinloomHostIdentity::fromUri(link.uri);
            const QString id=anchor.anchor.id+QLatin1Char('/')+link.id;
            direct.insert(id,{id,anchor.anchor.id,{},link.title,identity});
        }
        bool added=true;
        while(added){added=false;for(const auto& r:references)if(!r.parent.isEmpty()&&!direct.contains(r.id)&&direct.contains(r.parent)){direct.insert(r.id,r);added=true;}}
        references=direct;if(overlay)overlay->anchors=anchors;
        if(!board.error.isEmpty())status(board.error);
        else if(!links->loadFailureReason().isEmpty())status(links->loadFailureReason());
        else status(references.isEmpty()?QStringLiteral("No linked references. Attach a Pinloom link to a code selection."):QStringLiteral("%1 references · layouts save automatically").arg(references.size()));
        if(initialized)post({{"type","references"},{"references",referenceJson()}});
        geometryTimer.start();
    }
    void select(const QString& id) {
        if(!references.contains(id)||!editor)return;
        const auto& ref=references[id];
        for(const auto& anchor:anchors)if(anchor.anchor.id==ref.anchor){
            if(!anchor.available()){status(QStringLiteral("This code anchor is missing or ambiguous. Update its link before navigating."));return;}
            QTextCursor cursor(editor->document());cursor.setPosition(qBound(0,anchor.startPosition,editor->document()->characterCount()-1));
            cursor.setPosition(qBound(0,anchor.endPosition,editor->document()->characterCount()-1),QTextCursor::KeepAnchor);
            editor->setTextCursor(cursor);editor->ensureCursorVisible();selected=anchor.anchor.id;
            if(overlay){overlay->selected=selected;overlay->update();}geometryTimer.start();return;
        }
    }
    void open(const QString& id) {
        if(!references.contains(id))return;
        const auto identity=references[id].identity;
        if(!identity.isValid()){status(QStringLiteral("This reference has no valid Pinloom identity."));return;}
        const QString token=session;
        pinloom->open(identity,[self=QPointer<LinkedContextController>(q),token](bool ok,const QString& error){if(self&&self->d->session==token)self->d->status(ok?QStringLiteral("Original location opened."):error);},true);
    }
    void showDetail(const QString& id) {
        if(!references.contains(id))return;expanded=id;select(id);arrange();
        demand(demanded.values());updateDetail();
    }
    void updateDetail() {
        if(expanded.isEmpty()||!references.contains(expanded))return;
        const auto ref=references.value(expanded);const auto resource=resources.value(sourceKey(ref.identity));
        detailTitle->setText(resource&& !resource->document.entry.title.isEmpty()?resource->document.entry.title:ref.title);
        detailText->setPlainText(resource?(resource->error.isEmpty()?resource->document.content:resource->error):QStringLiteral("Loading reference…"));
        if(resource&&!resource->image.isNull()){detailImage->setSourcePixmap(QPixmap::fromImage(resource->image),resource->document.preview.altText);detailImage->show();}
        else {detailImage->clearSourcePixmap();detailImage->hide();}
    }
    void publish(const QString& source,const std::shared_ptr<Resource>& resource) {
        QJsonObject preview{{"title",resource->document.entry.title},{"text",resource->document.content.left(12000)},{"error",resource->error},
            {"location",resource->document.entry.location},{"links",resource->deeper.size()},{"state",resource->complete?QString():QStringLiteral("Loading reference…")}};
        if(!resource->thumbnail.isEmpty())preview["image"]=QStringLiteral("https://assets.zeroslack.invalid/")+shortKey(source)+QLatin1Char('?')+session;
        post({{"type","preview"},{"source",source},{"preview",preview}});updateDetail();
    }
    void loadResource(const QString& source,const PinloomHostIdentity& identity) {
        auto resource=std::make_shared<Resource>();resource->consumer=new QObject(q);resources.insert(source,resource);publish(source,resource);
        const auto serial=generation;const QString token=session;const QPointer<QObject> consumer=resource->consumer;
        pinloom->resolve(identity,[this,serial,token,consumer,source,resource](const PinloomHostDocument& doc,const QString& error){
            if(!consumer||!active||serial!=generation||session!=token||resources.value(source)!=resource)return;
            resource->document=doc;resource->error=error;resource->complete=true;
            if(error.isEmpty()&&!doc.isValid())resource->error=QStringLiteral("The reference is unavailable.");
            if(doc.entry.deleted)resource->error=QStringLiteral("This reference has been deleted in Pinloom.");
            // Only explicit Pinloom URIs become deeper references; never infer identities from prose.
            const QRegularExpression link(QStringLiteral("pinloom://[^\\s<>\\\"')\\]]+"));auto matches=link.globalMatch(doc.content);
            QSet<QString> seen;while(matches.hasNext()&&resource->deeper.size()<100){const auto candidate=PinloomHostIdentity::fromUri(QUrl(matches.next().captured()));const auto key=sourceKey(candidate);if(candidate.isValid()&&key!=source&&!seen.contains(key)){seen.insert(key);resource->deeper.append(candidate);}}
            if(resource->error.isEmpty()&&doc.preview.present&&doc.preview.isReady()){
                auto* loader=new PinloomPreviewImageLoader(consumer);
                loader->load(doc.preview,[this,serial,token,consumer,source,resource](const PinloomPreviewImageResult& result){
                    if(!consumer||!active||serial!=generation||token!=session||resources.value(source)!=resource)return;
                    resource->error=result.error;
                    if(!result.image.isNull()){resource->image=result.image.scaled(1024,768,Qt::KeepAspectRatio,Qt::SmoothTransformation);QBuffer output(&resource->thumbnail);output.open(QIODevice::WriteOnly);resource->image.save(&output,"PNG");}
                    publish(source,resource);
                });
            }else if(doc.preview.present&&!doc.preview.isReady())resource->error=doc.preview.validationError.isEmpty()?doc.preview.error:doc.preview.validationError;
            publish(source,resource);
        },consumer);
    }
    void demand(const QStringList& ids) {
        demanded=QSet<QString>(ids.begin(),ids.end());QStringList ordered=ids;ordered.sort();if(!expanded.isEmpty())ordered.prepend(expanded);
        QMap<QString,PinloomHostIdentity> wanted;
        for(const auto& id:ordered){if(!references.contains(id))continue;const auto identity=references[id].identity;if(identity.isValid()&&(wanted.size()<24||wanted.contains(sourceKey(identity))))wanted.insert(sourceKey(identity),identity);}
        QJsonArray released;
        for(auto it=resources.begin();it!=resources.end();){if(!wanted.contains(it.key())){released.append(it.key());if(it.value()->consumer){pinloom->cancelReadRequests(it.value()->consumer);delete it.value()->consumer;}it=resources.erase(it);}else ++it;}
        if(!released.isEmpty())post({{"type","release"},{"sources",released}});
        for(auto it=wanted.begin();it!=wanted.end();++it)if(!resources.contains(it.key()))loadResource(it.key(),it.value());
    }
    void receive(const QJsonObject& m) {
        const auto type=m.value("type").toString();
        if(type=="ready"||(type=="metrics"&&m.value("session").toString()==session)){
            const double dpr=m.value("dpr").toDouble(1);
            if(dpr>.1&&dpr<16){const double scale=qBound(.5,webScale*web->devicePixelRatioF()/dpr,3.);if(qAbs(scale-webScale)>.005){webScale=scale;web->setZoomFactor(webScale);}}
            if(type=="ready"){ready=true;initialize();}else geometryTimer.start();return;
        }
        if(m.value("session").toString()!=session||!active)return;
        if(type=="initialized"){initialized=true;post({{"type","theme"},{"dark",window->palette().color(QPalette::Window).lightness()<128}});sendGeometry();return;}
        if(type=="internal-document"){primaryBoardButton->setVisible(!m.value("primary").toBool());return;}
        if(type=="error"){status(QStringLiteral("Linked context: ")+m.value("error").toString());return;}
        if(type=="occlusion"){
            QRegion region;const auto rectangles=m.value("rectangles").toArray();
            const double sx=canvas->width()/qMax(1.,m.value("width").toDouble(canvas->width())),sy=canvas->height()/qMax(1.,m.value("height").toDouble(canvas->height()));
            for(const auto& value:rectangles){const auto rect=value.toArray();if(rect.size()==4)region+=QRect(qRound(rect[0].toDouble()*sx),qRound(rect[1].toDouble()*sy),qBound(0,qRound(rect[2].toDouble()*sx),canvas->width()),qBound(0,qRound(rect[3].toDouble()*sy),canvas->height())).intersected(canvas->rect());}
            if(region!=popupRegion){popupRegion=region;arrange();}return;
        }
        if(type=="write-clipboard"){
            const auto value=m.value("value").toObject();auto* mime=new QMimeData;
            if(value.contains("text"))mime->setText(value.value("text").toString());
            if(value.contains("html"))mime->setHtml(value.value("html").toString());
            if(value.contains("image")){const auto bytes=QByteArray::fromBase64(value.value("image").toString().toLatin1());if(bytes.size()<=24*1024*1024)mime->setData("image/png",bytes);}
            QApplication::clipboard()->setMimeData(mime);return;
        }
        if(type=="layout"){
            const auto state=m.value("state").toObject();QString error;
            if(!LinkedContextState::validate(state,&error)){status(error);return;}
            boardState=state;++layoutSerial;saveTimer.start();return;
        }
        if(type=="visible"){QStringList ids;for(const auto& value:m.value("ids").toArray())ids.append(value.toString());demand(ids);return;}
        const QString id=m.value("id").toString();if(!references.contains(id))return;
        if(type=="select")select(id);
        else if(type=="enlarge")showDetail(id);
        else if(type=="open")open(id);
        else if(type=="retry"){
            const auto source=sourceKey(references[id].identity);auto resource=resources.take(source);if(resource&&resource->consumer){pinloom->cancelReadRequests(resource->consumer);delete resource->consumer;}demand(demanded.values());
        }else if(type=="deeper"){
            const auto ref=references[id];const auto resource=resources.value(sourceKey(ref.identity));if(!resource)return;
            for(const auto& identity:resource->deeper){const QString child=id+QLatin1Char('/')+shortKey(sourceKey(identity)).left(16);if(references.size()<10000&&child.size()<=512)references.insert(child,{child,ref.anchor,id,QStringLiteral("Linked reference"),identity});}
            post({{"type","references"},{"references",referenceJson()}});
        }
    }
};

LinkedContextController::LinkedContextController(QMainWindow* window,QStackedWidget* stack,QWidget* editorPage,
        TabManager* tabs,PinloomCodeLinkStore* links,PinloomHostClient* pinloom,const QList<QWidget*>& chrome,QObject* parent)
    :QObject(parent),d(std::make_unique<Private>(this,window,stack,editorPage,tabs,links,pinloom)) {
    window->installEventFilter(this);
    tabs->setEditorTransitionGuard([self=QPointer<LinkedContextController>(this)](MyCodeEditor* editor){
        return !self||!self->d->active||editor!=self->d->editor||self->d->flush();
    });
    for(auto* widget:chrome)if(widget)d->chrome.append(widget);
    connect(tabs,&TabManager::activeTabChanged,this,[this](MyCodeEditor* editor){if(d->active)d->switchEditor(editor);});
    connect(tabs,&TabManager::documentIdentityChanged,this,[this]{if(d->active)d->switchEditor(d->tabs->getCurrentEditor());});
    connect(tabs,&TabManager::splitLayoutChanged,this,[this]{if(d->active)d->arrange();});
}
LinkedContextController::~LinkedContextController(){d->destroying=true;setActive(false);}
bool LinkedContextController::isActive()const{return d->active;}
bool LinkedContextController::setActive(bool active) {
    if(active==d->active)return true;
    if(active){
        auto* editor=d->tabs->getCurrentEditor();if(!editor){emit message(QStringLiteral("Open a code file to use linked context."));return false;}
        emit aboutToEnter();d->normalWindowState=d->window->saveState();d->originalFocus=QApplication::focusWidget();
        d->originalViews.clear();for(auto* view:d->tabs->openEditors())d->originalViews.append({view,view->document(),view->textCursor().position(),view->textCursor().anchor(),view->verticalScrollBar()->value(),view->horizontalScrollBar()->value()});
        d->visibility.clear();QSet<QWidget*> seen;
        auto hide=[this,&seen](QWidget* widget){if(!widget||seen.contains(widget))return;seen.insert(widget);d->visibility.append({widget,widget->isVisible()});widget->installEventFilter(this);widget->hide();};
        for(auto* dock:d->window->findChildren<QDockWidget*>())hide(dock);
        for(auto* toolbar:d->window->findChildren<QToolBar*>())hide(toolbar);
        for(auto widget:d->chrome)hide(widget);
        d->active=true;d->ready=false;d->initialized=false;d->createPage();d->switchEditor(editor);editor->setFocus();
    }else{
        if(!d->flush()&&!d->destroying)return false;
        d->active=false;++d->generation;d->dropResources();d->anchorTimer.stop();d->geometryTimer.stop();d->saveTimer.stop();
        for(const auto& connection:d->editorConnections)disconnect(connection);d->editorConnections.clear();
        if(d->overlay)delete d->overlay;if(d->editor)d->editor->viewport()->removeEventFilter(this);d->editor.clear();
        if(d->web)d->web->shutdown();
        d->editorPage->setParent(d->stack);d->stack->addWidget(d->editorPage);d->stack->setCurrentWidget(d->editorPage);d->editorPage->show();
        if(d->page){d->stack->removeWidget(d->page);delete d->page;}
        d->window->restoreState(d->normalWindowState);
        for(const auto& entry:d->visibility)if(entry.first){entry.first->removeEventFilter(this);entry.first->setVisible(entry.second);}
        // A closed view can survive until deleteLater after its document was detached.
        const auto openViews=d->tabs->openEditors();
        for(const auto& view:d->originalViews)if(view.editor&&openViews.contains(view.editor)&&view.document&&view.document==view.editor->document()){
            QTextCursor cursor(view.document);const int last=view.document->characterCount()-1;
            cursor.setPosition(qBound(0,view.anchor,last));cursor.setPosition(qBound(0,view.position,last),QTextCursor::KeepAnchor);
            view.editor->setTextCursor(cursor);view.editor->verticalScrollBar()->setValue(view.vertical);view.editor->horizontalScrollBar()->setValue(view.horizontal);
        }
        if(d->originalFocus&&d->originalFocus->isVisible())d->originalFocus->setFocus();else if(auto* editor=d->tabs->getCurrentEditor())editor->setFocus();
        d->originalViews.clear();d->visibility.clear();d->initialized=false;d->ready=false;d->session.clear();
    }
    emit activeChanged(active);return true;
}
void LinkedContextController::refreshAnchors(){if(d->active){if(d->workspace!=d->links->workspaceRoot())setActive(false);else d->anchorTimer.start();}}
void LinkedContextController::setPinloomHostClient(PinloomHostClient* client){if(!client||client==d->pinloom)return;const auto demand=d->demanded.values();++d->generation;d->dropResources();d->pinloom=client;if(d->active)d->demand(demand);}
void LinkedContextController::reloadBoard(){
    if(!d->active)return;
    d->saveTimer.stop();d->memoryBoards.remove(d->boardKey());d->board.load(d->workspace,d->file);d->boardState=d->board.value;d->exportedState={};
    d->boardReadOnly=!d->board.path.isEmpty()&&!d->board.error.isEmpty();
    ++d->generation;d->dropResources();d->initialized=false;d->expanded.clear();d->detail->hide();d->popupRegion=QRegion();
    d->session=QUuid::createUuid().toString(QUuid::WithoutBraces);d->restoreExpandedReferences();
    d->codeRatio=d->boardState.value("codeRatio").toDouble(.5);{QSignalBlocker block(d->ratioControl);d->ratioControl->setValue(qRound(d->codeRatio*100));}
    d->resolveAnchors();d->arrange();d->initialize();
}
bool LinkedContextController::eventFilter(QObject* watched,QEvent* event){
    if(d->active){
        if(event->type()==QEvent::PaletteChange&&watched==d->window&&d->initialized)d->post({{"type","theme"},{"dark",d->window->palette().color(QPalette::Window).lightness()<128}});
        if(event->type()==QEvent::Show)for(const auto& entry:d->visibility)if(entry.first==watched){entry.first->hide();return false;}
        if(event->type()==QEvent::Resize&&(watched==d->canvas||(d->editor&&watched==d->editor->viewport())))d->arrange();
    }
    return QObject::eventFilter(watched,event);
}
