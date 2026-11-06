#include "linkedcontextcontroller.h"
#include "linkedanchorgeometry.h"
#include "linkednativerails.h"
#include "linkedconnectoroverlay.h"
#include "linkedcontextstate.h"
#include "tabmanager.h"
#include "mycodeeditor.h"
#include "editorfoldviewstate.h"
#include "pinloomcodelinkstore.h"
#include "pinloomhostclient.h"
#include "pinloompreviewimage.h"
#include "uicontrols.h"
#include "roundedicons.h"
#include <QApplication>
#include <QDockWidget>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QKeyEvent>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTextBlock>
#include <QTimer>
#include <QToolBar>
#include <QUuid>
#include <QVBoxLayout>
#include <algorithm>
#include <map>

namespace {
QString sourceKey(const PinloomHostIdentity& identity) { return identity.toUri().toString(QUrl::FullyEncoded); }
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
        const auto selection = LinkedAnchorGeometry::sampleSelection(*editor, selected);
        for (const auto& anchor : anchors) {
            const LinkedAnchorGeometry::Anchor input{anchor.anchor.id, anchor.available(), anchor.startPosition, anchor.endPosition};
            if (!input.available || !LinkedAnchorGeometry::active(input, selection)) continue;
            const auto sample = LinkedAnchorGeometry::sampleAnchor(*editor, input, true);
            const auto geometry = LinkedAnchorGeometry::overlay(sample, selection, size());
            if (!geometry.draw) continue;
            painter.fillRect(geometry.band,QColor(52,120,212,18)); painter.setPen(QPen(QColor(52,120,212,180),1.5));
            painter.drawRoundedRect(geometry.outline,2,2);
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
    QPointer<QWidget> page,statusRow;
    QPointer<LinkedNativeRails> nativeRails;
    QPointer<LinkedConnectorOverlay> connector;
    quint64 connectorRequestSerial = 0;
    QHash<QString,QList<int>> nativeSizes;
    bool nativeMouseSelection=false;
    quint64 nativeMouseRevision=0;
    QString nativeMouseSession;
    QPointer<QTextDocument> nativeMouseDocument;
    qint64 nativeResolvedRevision=-1;
    QList<QPair<int,int>> nativeFoldSignature;
    QPointer<QLabel> notice;
    QPointer<QPushButton> saveCopyButton;
    QPointer<AnchorOverlay> overlay;
    QPointer<MyCodeEditor> editor;
    QTimer anchorTimer,geometryTimer,saveTimer;
    QList<QMetaObject::Connection> editorConnections;
    LinkedContextState board;
    QJsonObject boardState;
    QString file,workspace,session,selected;
    double codeRatio=.5;
    bool active=false,initialized=false,switching=false,destroying=false,boardReadOnly=false;
    QJsonObject exportedState;
    QJsonObject nativeViewState;
    QJsonArray nativeReferenceBaseline;
    bool nativeDirty=false,nativeRestoring=false,nativeReferencesReady=false,nativeReferencesChanged=false;
    quint64 generation=0;
    QList<ResolvedPinloomCodeLink> anchors;
    struct Reference { QString id,anchor,parent,title; PinloomHostIdentity identity; };
    QMap<QString,Reference> references;
    struct Resource {
        QPointer<QObject> consumer;
        PinloomHostDocument document;
        QImage image;
        QString error;
        bool complete=false;
        QList<PinloomHostIdentity> deeper;
    };
    QMap<QString,std::shared_ptr<Resource>> resources;
    QSet<QString> demanded;
    QHash<QString,QJsonObject> memoryBoards;

    Private(LinkedContextController* owner,QMainWindow* w,QStackedWidget* s,QWidget* e,TabManager* t,PinloomCodeLinkStore* l,PinloomHostClient* p)
        :q(owner),window(w),stack(s),editorPage(e),tabs(t),links(l),pinloom(p) {
        anchorTimer.setSingleShot(true); anchorTimer.setInterval(180);
        geometryTimer.setSingleShot(true); geometryTimer.setInterval(20);
        saveTimer.setSingleShot(true); saveTimer.setInterval(300);
        QObject::connect(&anchorTimer,&QTimer::timeout,q,[this]{resolveAnchors();});
        QObject::connect(&geometryTimer,&QTimer::timeout,q,[this]{sendGeometry();});
        QObject::connect(&saveTimer,&QTimer::timeout,q,[this]{save();});
    }
    void status(const QString& text) {
        const bool recovery=!board.path.isEmpty()&&!board.error.isEmpty();
        if(notice){notice->setText(text);notice->setToolTip(text);}
        if(saveCopyButton)saveCopyButton->setVisible(recovery);
        if(statusRow)statusRow->setVisible(!text.isEmpty()||recovery);
        if(!text.isEmpty())emit q->message(text);
    }
    QString boardKey() const { return workspace+QLatin1Char('\n')+(file.isEmpty()&&editor?editor->property("sharedDocumentId").toString():file); }
    void restoreExpandedReferences() {
        references.clear();
        for(const auto& value:boardState.value("expandedReferences").toArray()){
            const auto r=value.toObject();const auto identity=PinloomHostIdentity::fromUri(QUrl(r.value("source").toString()));
            if(identity.isValid())references.insert(r.value("id").toString(),{r.value("id").toString(),r.value("anchor").toString(),r.value("parent").toString(),r.value("title").toString(),identity});
        }
    }
    QJsonObject nativeView() const {
        QJsonArray weights;
        const auto sizes=nativeRails?nativeRails->sizes():QList<int>{1,2,1};
        double total=0;for(int size:sizes)total+=size;
        for(int size:sizes)weights.append(total>0?size/total:1./3);
        return {{"version",1},{"weights",weights},{"expanded",QJsonArray::fromStringList(nativeRails?nativeRails->expandedAnchors():QStringList{})}};
    }
    QList<int> savedNativeSizes() const {
        QList<int> result;const auto weights=nativeViewState.value("weights").toArray();
        double sum=0;for(const auto& weight:weights)sum+=weight.toDouble();
        if(weights.size()==3&&sum>0)for(const auto& weight:weights)result.append(qRound(weight.toDouble()/sum*1000000));
        return result;
    }
    void restoreNativeExpansion() {
        if(!nativeRails)return;QStringList expanded;
        for(const auto& id:nativeViewState.value("expanded").toArray())expanded.append(id.toString());
        nativeRails->restoreExpandedAnchors(expanded);
    }
    QJsonArray referenceState() const {
        QJsonArray result;
        for(const auto& r:references)result.append(QJsonObject{{"id",r.id},{"anchor",r.anchor},{"parent",r.parent},{"title",r.title},{"source",sourceKey(r.identity)}});
        return result;
    }
    QJsonObject nativeCandidate() const {
        auto candidate=boardState;
        if(candidate.isEmpty())candidate={{"version",3}};
        candidate["codeRatio"]=qBound(.3,codeRatio,.7);
        candidate["nativeView"]=nativeViewState.isEmpty()?nativeView():nativeViewState;
        if(boardState.isEmpty()||nativeReferencesChanged){
            candidate["referenceIds"]=QJsonArray::fromStringList(references.keys());QJsonArray deeper;
            for(const auto& value:referenceState())if(!value.toObject().value("parent").toString().isEmpty())deeper.append(value);
            candidate["expandedReferences"]=deeper;
        }
        return candidate;
    }
    void nativeChanged(bool viewChanged) {
        if(!active||!initialized||nativeRestoring)return;
        if(viewChanged)nativeViewState=nativeView();
        const auto candidate=nativeCandidate();
        nativeDirty=candidate!=board.value&&candidate!=exportedState;
        if(nativeDirty)saveTimer.start();else saveTimer.stop();
    }
    bool save() {
        if(destroying||!active||!nativeDirty)return true;
        const auto candidate=nativeCandidate();
        if(candidate==board.value||candidate==exportedState){nativeDirty=false;return true;}
        if(board.path.isEmpty()){memoryBoards.insert(boardKey(),candidate);boardState=candidate;nativeDirty=false;return true;}
        if(!boardReadOnly&&board.save(candidate)){
            boardState=candidate;exportedState={};nativeDirty=false;nativeReferencesChanged=false;status({});return true;
        }
        status(board.error+QStringLiteral(" Save a copy or reload before leaving."));return false;
    }
    void saveRecoveryCopy() {
        if(!flush(false))return;
        const auto candidate=nativeCandidate();
        const auto destination=QFileDialog::getSaveFileName(window,QStringLiteral("Save board recovery copy"),board.path+QStringLiteral(".copy.json"),QStringLiteral("Board state (*.json)"));
        if(destination.isEmpty())return;QString error;
        if(board.writeCopy(destination,candidate,&error)){
            exportedState=candidate;nativeDirty=nativeCandidate()!=candidate;
            status(QStringLiteral("Recovery copy saved: ")+destination);
        }else status(error);
    }
    bool flush(bool persist=true) {
        saveTimer.stop();return !persist||save();
    }
    void dropResources() {
        if(nativeRails)for(auto it=resources.cbegin();it!=resources.cend();++it)nativeRails->clearPreview(it.key());
        for(const auto& resource:resources)if(resource->consumer){pinloom->cancelReadRequests(resource->consumer);delete resource->consumer;}
        resources.clear();demanded.clear();
    }
    void restoreNativeView(const EditorView& view) {
        if(!active||!view.editor||view.editor!=editor||!view.document||view.document!=editor->document())return;
        if(window->layout())window->layout()->activate();
        if(page&&page->layout())page->layout()->activate();
        if(editorPage->layout())editorPage->layout()->activate();
        QTextCursor cursor(view.document);const int last=view.document->characterCount()-1;
        cursor.setPosition(qBound(0,view.anchor,last));cursor.setPosition(qBound(0,view.position,last),QTextCursor::KeepAnchor);
        if(editor->textCursor().position()!=cursor.position()||editor->textCursor().anchor()!=cursor.anchor())editor->setTextCursor(cursor);
        editor->verticalScrollBar()->setValue(view.vertical);editor->horizontalScrollBar()->setValue(view.horizontal);
    }
    QPair<int,int> nativeFoldRange(const EditorFoldAnchorState& fold) const {
        const auto* document=editor->document();
        const int start=!fold.startAnchor.isNull()&&fold.startAnchor.document()==document
            ?fold.startAnchor.blockNumber():fold.fallbackStartLine;
        const int end=!fold.endAnchor.isNull()&&fold.endAnchor.document()==document
            ?fold.endAnchor.blockNumber():fold.fallbackEndLine;
        return start>=0&&end>start&&end<document->blockCount()?qMakePair(start,end):qMakePair(-1,-1);
    }
    QList<QPair<int,int>> nativeFoldRanges() const {
        QList<QPair<int,int>> ranges;
        for(const auto& fold:editor->foldingViewState().collapsedRanges) {
            const auto range=nativeFoldRange(fold);if(range.first>=0)ranges.append(range);
        }
        std::sort(ranges.begin(),ranges.end());return ranges;
    }
    LinkedNativeRails::Group nativeGroupGeometry(const ResolvedPinloomCodeLink& anchor,
                                                const QList<QPair<int,int>>& folds) const {
        LinkedNativeRails::Group group;group.anchor=anchor.anchor.id;group.title=anchor.anchor.source.suggestedTitle();
        const auto original=LinkedAnchorGeometry::sampleAnchor(*editor,{anchor.anchor.id,true,anchor.startPosition,anchor.endPosition}).first;
        group.y=original.center().y();
        group.sourceLine=editor->document()->findBlock(anchor.startPosition).blockNumber();
        QRect rectangle=original;
        if(!editor->sourceLineVisible(group.sourceLine)) {
            for(const auto& range:folds)if(range.first<group.sourceLine&&range.second>=group.sourceLine
                &&editor->sourceLineVisible(range.first)) {
                group.foldHead=range.first;break;
            }
            if(group.foldHead<0) {group.positioned=false;return group;}
            QTextCursor head(editor->document()->findBlockByNumber(group.foldHead));
            rectangle=editor->cursorRect(head);
        }
        group.visibleRow=editor->visibleRowForSourceLine(group.foldHead>=0?group.foldHead:group.sourceLine);
        if(rectangle.isEmpty()||group.visibleRow<0) {group.positioned=false;return group;}
        QTextCursor origin(editor->document());origin.setPosition(0);
        const int offset=editor->viewport()->mapTo(nativeRails,QPoint()).y()-nativeRails->railViewportTop();
        group.contentY=qint64(rectangle.center().y())-editor->cursorRect(origin).top()+offset;
        group.viewportY=rectangle.center().y()+offset;return group;
    }
    void refreshNativeGroups() {
        if(!nativeRails||!editor)return;
        const auto folds=nativeFoldRanges();nativeFoldSignature=folds;
        QList<LinkedNativeRails::Group> groups;
        for(const auto& anchor:anchors){
            if(!anchor.available())continue;
            auto group=nativeGroupGeometry(anchor,folds);
            for(const auto& ref:references)if(ref.anchor==group.anchor)group.references.append({ref.id,sourceKey(ref.identity),ref.title});
            if(!group.references.isEmpty())groups.append(group);
        }
        nativeRails->setGroups(groups,nativeResolvedRevision);
        for(auto it=resources.cbegin();it!=resources.cend();++it)publish(it.key(),it.value());
    }
    void updateNativePositions() {
        if(!nativeRails||!editor)return;
        const auto folds=nativeFoldRanges();
        if(folds!=nativeFoldSignature&&nativeResolvedRevision==qint64(editor->semanticDocumentRevision())) {
            refreshNativeGroups();return;
        }
        QMap<QString,LinkedNativeRails::Position> positions;
        for(const auto& anchor:anchors)if(anchor.available()) {
            const auto group=nativeGroupGeometry(anchor,folds);
            if(group.positioned)positions.insert(anchor.anchor.id,{group.y,group.contentY,group.viewportY});
        }
        nativeRails->updateAnchorPositions(positions);
    }
    void expandNativeFolded(const QStringList& members,qint64 revision) {
        if(!active||!nativeRails||!editor||revision!=nativeResolvedRevision
            ||revision!=qint64(editor->semanticDocumentRevision())||members.isEmpty())return;
        QMap<QString,int> lines;
        for(const auto& anchor:anchors)if(anchor.available()&&members.contains(anchor.anchor.id))
            lines.insert(anchor.anchor.id,editor->document()->findBlock(anchor.startPosition).blockNumber());
        if(lines.size()!=members.size())return;
        const auto state=editor->foldingViewState();EditorFoldViewState unfolded;
        bool changed=false;
        for(const auto& fold:state.collapsedRanges) {
            const auto range=nativeFoldRange(fold);bool blocking=false;
            for(int line:lines)blocking|=range.first>=0&&range.first<line&&range.second>=line;
            if(blocking)changed=true;else unfolded.collapsedRanges.append(fold);
        }
        if(!changed)return;
        const QPointer<MyCodeEditor> source=editor;const QPointer<QTextDocument> document=editor->document();
        const QString token=session;
        editor->restoreFoldingViewState(unfolded);
        QTimer::singleShot(0,q,[this,source,document,token,revision,members,lines]{
            if(!active||!nativeRails||!source||source!=editor||!document
                ||document!=editor->document()||token!=session||revision!=qint64(editor->semanticDocumentRevision()))return;
            for(int line:lines)if(!editor->sourceLineVisible(line))return;
            refreshNativeGroups();
            for(const auto& member:members)nativeRails->setGroupExpanded(member,true);
        });
    }
    void deferNativeSelection(const QString& reason, quint64 revision, const QString& token, QTextDocument* document) {
        const QPointer<MyCodeEditor> source=editor;
        const QPointer<QTextDocument> sourceDocument=document;
        QTimer::singleShot(0,q,[this,source,sourceDocument,revision,token,reason]{
            if(!active||!nativeRails||!source||source!=editor||!sourceDocument
                ||sourceDocument!=editor->document()||session!=token||editor->semanticDocumentRevision()!=revision)return;
            const auto selection=LinkedAnchorGeometry::sampleSelection(*editor,{});
            const ResolvedPinloomCodeLink* closest=nullptr;
            for(const auto& anchor:anchors)if(anchor.available()
                &&LinkedAnchorGeometry::active({anchor.anchor.id,true,anchor.startPosition,anchor.endPosition},selection)
                &&(!closest||qAbs(anchor.startPosition-editor->textCursor().position())<qAbs(closest->startPosition-editor->textCursor().position())))closest=&anchor;
            if(closest)nativeRails->followAnchor(closest->anchor.id,reason);
        });
    }
    void nativeInput(QObject* watched,QEvent* event) {
        if(!nativeRails||!editor)return;
        if(watched==editor->viewport()&&event->type()==QEvent::MouseButtonPress) {
            const auto* mouse=static_cast<QMouseEvent*>(event);
            nativeMouseSelection=mouse->button()==Qt::LeftButton;
            nativeMouseRevision=editor->semanticDocumentRevision();nativeMouseSession=session;nativeMouseDocument=editor->document();
        }
        if(watched==editor->viewport()&&event->type()==QEvent::MouseButtonRelease) {
            const auto* mouse=static_cast<QMouseEvent*>(event);
            if(nativeMouseSelection&&mouse->button()==Qt::LeftButton)
                deferNativeSelection(QStringLiteral("code-mouse-selection"),nativeMouseRevision,nativeMouseSession,nativeMouseDocument);
            nativeMouseSelection=false;
        }
        if(watched==editor&&event->type()==QEvent::KeyPress) {
            const auto* key=static_cast<QKeyEvent*>(event);const int code=key->key();
            const bool navigation=code==Qt::Key_Left||code==Qt::Key_Right||code==Qt::Key_Up||code==Qt::Key_Down
                ||code==Qt::Key_Home||code==Qt::Key_End||code==Qt::Key_PageUp||code==Qt::Key_PageDown
                ||(code==Qt::Key_A&&key->modifiers().testFlag(Qt::ControlModifier));
            if(navigation)deferNativeSelection(QStringLiteral("code-navigation-key"),editor->semanticDocumentRevision(),session,editor->document());
        }
    }
    void refreshConnector() {
        if(!connector)return;
        LinkedConnectorOverlay::Scene scene;
        scene.identity.session=session;scene.identity.generation=generation;
        scene.identity.requestSerial=++connectorRequestSerial;
        if(active&&nativeRails&&editor&&editor->isVisible()&&connector->isVisible()) {
            const auto endpoints=nativeRails->connectorSnapshot(connector);
            scene.identity.document=editor->document();
            scene.identity.documentRevision=editor->document()->revision();
            scene.identity.contentRevision=qint64(editor->semanticDocumentRevision());
            scene.identity.layoutSerial=endpoints.layoutSerial;
            scene.identity.presentationSerial=endpoints.presentationSerial;
            scene.central=QRectF(connector->mapFromGlobal(editorPage->mapToGlobal(QPoint())),editorPage->size());
            scene.codeViewport=QRectF(connector->mapFromGlobal(editor->viewport()->mapToGlobal(QPoint())),editor->viewport()->size());
            scene.occlusions=endpoints.occlusions;
            scene.valid=!session.isEmpty()&&nativeResolvedRevision==scene.identity.contentRevision
                &&endpoints.contentRevision==scene.identity.contentRevision&&endpoints.layoutSerial>0
                &&nativeFoldSignature==nativeFoldRanges();
            if(scene.valid) {
                QSet<QString> available,activeIds;
                const auto selection=LinkedAnchorGeometry::sampleSelection(*editor,selected);
                for(const auto& anchor:anchors)if(anchor.available()) {
                    available.insert(anchor.anchor.id);
                    if(LinkedAnchorGeometry::active({anchor.anchor.id,true,anchor.startPosition,anchor.endPosition},selection))
                        activeIds.insert(anchor.anchor.id);
                }
                nativeRails->setActiveAnchors(activeIds);
                for(const auto& endpoint:endpoints.endpoints) {
                    bool membersValid=!endpoint.members.isEmpty(),highlighted=false;
                    for(const auto& member:endpoint.members) {membersValid&=available.contains(member);highlighted|=activeIds.contains(member);}
                    const auto block=editor->document()->findBlockByNumber(endpoint.sourceLine);
                    const QRect line=block.isValid()?editor->cursorRect(QTextCursor(block)):QRect();
                    const qreal codeY=scene.codeViewport.top()+line.center().y();
                    const bool codeVisible=block.isValid()&&!line.isEmpty()&&editor->sourceLineVisible(endpoint.sourceLine)
                        &&codeY>=scene.codeViewport.top()&&codeY<scene.codeViewport.bottom();
                    LinkedConnectorPath::Input input{endpoint.id,
                        endpoint.side==0?LinkedConnectorPath::Side::Left:LinkedConnectorPath::Side::Right,
                        codeY,endpoint.header,scene.central,QRectF(connector->rect()),
                        endpoint.positioned&&membersValid,codeVisible,endpoint.visible,highlighted};
                    const auto& viewport=endpoints.viewports[endpoint.side];
                    const qreal edge=endpoint.side==0?scene.central.left():scene.central.right();
                    const QRectF channel(QPointF(qMin(edge,endpoint.header.x()),viewport.top()),
                        QPointF(qMax(edge,endpoint.header.x()),viewport.bottom()));
                    scene.strokes.append({input,channel.intersected(QRectF(connector->rect()))});
                }
            }
        }
        if(nativeRails&&!scene.valid)nativeRails->setActiveAnchors({});
        connector->setScene(scene);
    }
    void createNativePage() {
        page=new QWidget(stack);page->setObjectName("linkedContextPage");
        auto* layout=new QVBoxLayout(page);layout->setContentsMargins(0,0,0,0);layout->setSpacing(0);
        auto* bar=new QWidget(page);bar->setObjectName("linkedContextControls");
        auto* row=new QHBoxLayout(bar);row->setContentsMargins(8,5,8,5);
        auto* exit=UiControls::pushButton(QStringLiteral("Exit linked context"),bar);exit->setObjectName("exitLinkedContext");
        exit->setIcon(RoundedIcons::icon(RoundedIcons::Left));row->addWidget(exit);
        QObject::connect(exit,&QPushButton::clicked,q,[this]{q->setActive(false);});row->addStretch();
        auto* reload=UiControls::pushButton(QStringLiteral("Reload references"),bar);reload->setObjectName("linkedNativeReload");
        row->addWidget(reload);QObject::connect(reload,&QPushButton::clicked,q,[this]{q->reloadBoard();});layout->addWidget(bar);
        statusRow=new QWidget(page);auto* statusLayout=new QHBoxLayout(statusRow);statusLayout->setContentsMargins(8,2,8,5);
        notice=new QLabel(statusRow);notice->setObjectName("linkedContextStatus");notice->setWordWrap(true);notice->setMinimumWidth(0);
        statusLayout->addWidget(notice,1);
        saveCopyButton=UiControls::pushButton(QStringLiteral("Save a copy…"),statusRow);saveCopyButton->setObjectName("linkedSaveCopy");statusLayout->addWidget(saveCopyButton);
        QObject::connect(saveCopyButton,&QPushButton::clicked,q,[this]{saveRecoveryCopy();});saveCopyButton->hide();
        statusRow->hide();layout->addWidget(statusRow);
        stack->removeWidget(editorPage);
        auto* host=new QWidget(page);host->setObjectName("linkedNativeHost");
        auto* hostLayout=new QVBoxLayout(host);hostLayout->setContentsMargins(0,0,0,0);hostLayout->setSpacing(0);
        nativeRails=new LinkedNativeRails(editorPage,host);
        hostLayout->addWidget(nativeRails);layout->addWidget(host,1);editorPage->show();nativeRails->installEventFilter(q);
        connector=new LinkedConnectorOverlay(host);
        QObject::connect(connector,&LinkedConnectorOverlay::presentationRequested,q,[this]{refreshConnector();});
        QObject::connect(nativeRails,&LinkedNativeRails::layoutApplied,q,[this]{refreshConnector();});
        QObject::connect(nativeRails,&LinkedNativeRails::presentationChanged,q,[this]{refreshConnector();});
        connector->show();connector->raise();
        QObject::connect(nativeRails,&LinkedNativeRails::visibleReferencesChanged,q,[this](const QStringList& ids){if(active)demand(ids);});
        QObject::connect(nativeRails,&LinkedNativeRails::locateCodeRequested,q,[this](const QString& id){select(id);});
        QObject::connect(nativeRails,&LinkedNativeRails::openOriginalRequested,q,[this](const QString& id){open(id);});
        QObject::connect(nativeRails,&LinkedNativeRails::codeRatioChanged,q,[this](double ratio){codeRatio=ratio;nativeChanged(true);});
        QObject::connect(nativeRails,&LinkedNativeRails::groupExpansionChanged,q,[this]{nativeChanged(true);});
        QObject::connect(nativeRails,&LinkedNativeRails::expandFoldedRequested,q,[this](const QStringList& members,qint64 revision){expandNativeFolded(members,revision);});
        stack->addWidget(page);stack->setCurrentWidget(page);
    }
    void arrange() {
        if(!nativeRails||!editor)return;
        updateNativePositions();geometryTimer.start();
        if(overlay){overlay->setGeometry(editor->viewport()->rect());overlay->raise();overlay->update();}
    }
    void sendGeometry(){
        if(active&&initialized){updateNativePositions();refreshConnector();if(overlay)overlay->update();}
    }
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
    void initialize() { initialized=active&&editor&&nativeRails; }
    void switchEditor(MyCodeEditor* next) {
        if(!active||switching)return;
        if(!next){q->setActive(false);return;}
        const QString nextFile=next->documentFileName();
        if(next==editor&&nextFile==file&&workspace==links->workspaceRoot())return;
        EditorView nativeView{next,next->document(),next->textCursor().position(),next->textCursor().anchor(),next->verticalScrollBar()->value(),next->horizontalScrollBar()->value()};
        if(!editor)for(const auto& view:originalViews)if(view.editor==next&&view.document==next->document()){nativeView=view;break;}
        switching=true;
        if(!flush()){if(editor&&!file.isEmpty())tabs->activateOpenFile(file);switching=false;return;}
        nativeRestoring=true;nativeDirty=false;nativeReferencesReady=false;nativeReferencesChanged=false;
        const bool renamed=next==editor&&nextFile!=file;
        const auto previousNativeSizes=nativeRails?nativeRails->sizes():QList<int>{};
        if(nativeRails&&editor)nativeSizes.insert(boardKey(),previousNativeSizes);
        const auto previousState=boardState;
        ++generation;dropResources();
        if(connector)connector->setScene({});
        if(nativeRails)nativeRails->resetSession();
        for(const auto& c:editorConnections)QObject::disconnect(c);editorConnections.clear();
        if(overlay)delete overlay;
        if(editor){editor->viewport()->removeEventFilter(q);editor->removeEventFilter(q);}
        nativeMouseSelection=false;
        nativeResolvedRevision=-1;nativeFoldSignature.clear();
        editor=next;file=nextFile;workspace=links->workspaceRoot();selected.clear();
        session=QUuid::createUuid().toString(QUuid::WithoutBraces);initialized=false;
        board.load(workspace,file);boardState=board.value;references.clear();exportedState={};
        boardReadOnly=!board.path.isEmpty()&&!board.error.isEmpty();
        if(board.path.isEmpty()&&memoryBoards.contains(boardKey()))boardState=memoryBoards.value(boardKey());
        if(renamed&&boardState.isEmpty()&&board.error.isEmpty())boardState=previousState;
        nativeViewState=boardState.value("nativeView").toObject();
        restoreExpandedReferences();
        codeRatio=boardState.value("codeRatio").toDouble(.5);
        if(nativeRails){if(renamed)nativeSizes.insert(boardKey(),previousNativeSizes);nativeRails->setInitialSizes(nativeSizes.contains(boardKey())?nativeSizes.value(boardKey()):savedNativeSizes(),codeRatio);}
        overlay=new AnchorOverlay(editor);overlay->show();editor->viewport()->installEventFilter(q);
        editor->installEventFilter(q);
        editorConnections.append(QObject::connect(editor,&QPlainTextEdit::textChanged,q,[this]{anchorTimer.start();geometryTimer.start();}));
        if(connector)editorConnections.append(QObject::connect(editor,&QPlainTextEdit::textChanged,q,[this]{nativeRails->setActiveAnchors({});connector->setScene({});}));
        editorConnections.append(QObject::connect(editor,&QPlainTextEdit::cursorPositionChanged,q,[this]{selected.clear();if(overlay)overlay->selected.clear();geometryTimer.start();}));
        editorConnections.append(QObject::connect(editor,&QPlainTextEdit::selectionChanged,q,[this]{geometryTimer.start();}));
        editorConnections.append(QObject::connect(editor,&QPlainTextEdit::updateRequest,q,[this]{geometryTimer.start();}));
        resolveAnchors();restoreNativeExpansion();arrange();initialize();switching=false;nativeRestoring=false;
        if(renamed&&!boardState.isEmpty()&&boardState!=board.value)nativeChanged(false);
        restoreNativeView(nativeView);
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
        if(links->loadFailureReason().isEmpty()){
            const auto current=referenceState();
            const bool changed=nativeReferencesReady&&current!=nativeReferenceBaseline;
            nativeReferenceBaseline=current;nativeReferencesReady=true;
            if(changed){nativeReferencesChanged=true;nativeChanged(false);}
        }
        if(board.path.isEmpty())status(QStringLiteral("Layout kept for this session"));
        else if(!board.error.isEmpty())status(board.error);
        else if(!links->loadFailureReason().isEmpty())status(links->loadFailureReason());
        else status(references.isEmpty()?QStringLiteral("Attach a Pinloom link to a code selection to add references."):QString());
        geometryTimer.start();
        nativeResolvedRevision=qint64(editor->semanticDocumentRevision());refreshNativeGroups();
    }
    void select(const QString& id) {
        if(!references.contains(id)||!editor)return;
        const auto& ref=references[id];
        for(const auto& anchor:anchors)if(anchor.anchor.id==ref.anchor){
            if(!anchor.available()){status(QStringLiteral("This code anchor is missing or ambiguous. Update its link before navigating."));return;}
            QTextCursor cursor(editor->document());cursor.setPosition(qBound(0,anchor.startPosition,editor->document()->characterCount()-1));
            cursor.setPosition(qBound(0,anchor.endPosition,editor->document()->characterCount()-1),QTextCursor::KeepAnchor);
            editor->setTextCursor(cursor);editor->ensureCursorVisible();selected=anchor.anchor.id;
            if(nativeRails)nativeRails->followAnchor(anchor.anchor.id,QStringLiteral("go-to-code"));
            if(overlay){overlay->selected=selected;overlay->update();}geometryTimer.start();return;
        }
    }
    void open(const QString& id) {
        if(!references.contains(id))return;
        const auto identity=references[id].identity;
        if(!identity.isValid()){status(QStringLiteral("This reference has no valid Pinloom identity."));return;}
        const QString token=session;
        pinloom->open(identity,[self=QPointer<LinkedContextController>(q),token](bool ok,const QString& error){if(self&&self->d->session==token&&!ok)self->d->status(error);},true);
    }
    void publish(const QString& source,const std::shared_ptr<Resource>& resource) {
        if(nativeRails)nativeRails->setPreview(source,{resource->document.entry.title,resource->document.content,
            resource->document.entry.location,resource->error,resource->image,resource->complete});
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
                    if(!result.image.isNull())resource->image=result.image.scaled(1024,768,Qt::KeepAspectRatio,Qt::SmoothTransformation);
                    publish(source,resource);
                });
            }else if(doc.preview.present&&!doc.preview.isReady())resource->error=doc.preview.validationError.isEmpty()?doc.preview.error:doc.preview.validationError;
            publish(source,resource);
        },consumer);
    }
    void demand(const QStringList& ids) {
        demanded=QSet<QString>(ids.begin(),ids.end());QStringList ordered=ids;ordered.sort();
        QMap<QString,PinloomHostIdentity> wanted;
        for(const auto& id:ordered){if(!references.contains(id))continue;const auto identity=references[id].identity;if(identity.isValid()&&(wanted.size()<24||wanted.contains(sourceKey(identity))))wanted.insert(sourceKey(identity),identity);}
        QJsonArray released;
        for(auto it=resources.begin();it!=resources.end();){if(!wanted.contains(it.key())){released.append(it.key());if(it.value()->consumer){pinloom->cancelReadRequests(it.value()->consumer);delete it.value()->consumer;}it=resources.erase(it);}else ++it;}
        if(nativeRails)for(const auto& source:released)nativeRails->clearPreview(source.toString());
        for(auto it=wanted.begin();it!=wanted.end();++it)if(!resources.contains(it.key()))loadResource(it.key(),it.value());
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
        d->nativeSizes.clear();
        emit aboutToEnter();d->normalWindowState=d->window->saveState();d->originalFocus=QApplication::focusWidget();
        d->originalViews.clear();for(auto* view:d->tabs->openEditors())d->originalViews.append({view,view->document(),view->textCursor().position(),view->textCursor().anchor(),view->verticalScrollBar()->value(),view->horizontalScrollBar()->value()});
        d->visibility.clear();QSet<QWidget*> seen;
        auto hide=[this,&seen](QWidget* widget){if(!widget||seen.contains(widget))return;seen.insert(widget);d->visibility.append({widget,widget->isVisible()});widget->installEventFilter(this);widget->hide();};
        for(auto* dock:d->window->findChildren<QDockWidget*>())hide(dock);
        for(auto* toolbar:d->window->findChildren<QToolBar*>())hide(toolbar);
        for(auto widget:d->chrome)hide(widget);
        d->active=true;d->initialized=false;d->createNativePage();d->switchEditor(editor);editor->setFocus();
        for(const auto& view:d->originalViews)if(view.editor==editor){d->restoreNativeView(view);break;}
    }else{
        if(!d->destroying&&!d->flush())return false;
        d->active=false;++d->generation;d->dropResources();d->anchorTimer.stop();d->geometryTimer.stop();d->saveTimer.stop();
        if(d->connector)d->connector->setScene({});
        for(const auto& connection:d->editorConnections)disconnect(connection);d->editorConnections.clear();
        if(d->overlay)delete d->overlay;if(d->editor){d->editor->viewport()->removeEventFilter(this);d->editor->removeEventFilter(this);}d->editor.clear();
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
        d->originalViews.clear();d->visibility.clear();d->initialized=false;d->session.clear();
    }
    emit activeChanged(active);return true;
}
void LinkedContextController::refreshAnchors(){if(d->active){if(d->workspace!=d->links->workspaceRoot())setActive(false);else d->anchorTimer.start();}}
void LinkedContextController::setPinloomHostClient(PinloomHostClient* client){if(!client||client==d->pinloom)return;const auto demand=d->demanded.values();++d->generation;d->dropResources();d->pinloom=client;if(d->active)d->demand(demand);if(d->connector)d->refreshConnector();}
void LinkedContextController::reloadBoard(){
    if(!d->active)return;
    d->nativeRestoring=true;d->nativeDirty=false;d->nativeReferencesReady=false;d->nativeReferencesChanged=false;
    d->anchorTimer.stop();d->geometryTimer.stop();d->nativeSizes.remove(d->boardKey());
    d->saveTimer.stop();d->memoryBoards.remove(d->boardKey());d->board.load(d->workspace,d->file);d->boardState=d->board.value;d->exportedState={};
    d->boardReadOnly=!d->board.path.isEmpty()&&!d->board.error.isEmpty();
    ++d->generation;d->dropResources();d->initialized=false;
    if(d->connector)d->connector->setScene({});
    d->session=QUuid::createUuid().toString(QUuid::WithoutBraces);d->restoreExpandedReferences();
    d->codeRatio=d->boardState.value("codeRatio").toDouble(.5);
    d->nativeViewState=d->boardState.value("nativeView").toObject();
    if(d->nativeRails){d->nativeRails->resetSession();d->nativeRails->setInitialSizes(d->savedNativeSizes(),d->codeRatio);}
    d->resolveAnchors();d->restoreNativeExpansion();d->arrange();d->initialize();d->nativeRestoring=false;
}
QJsonObject LinkedContextController::viewSnapshot()const{
    QJsonArray resources;QStringList demanded=d->demanded.values();demanded.sort();
    for(auto it=d->resources.cbegin();it!=d->resources.cend();++it)resources.append(QJsonObject{
        {"source",it.key()},{"complete",it.value()->complete},{"error",it.value()->error},
        {"image",!it.value()->image.isNull()},{"consumer",!it.value()->consumer.isNull()}});
    QJsonObject result{{"active",d->active},{"native",true},{"initialized",d->initialized},
        {"session",d->session},{"generation",double(d->generation)},{"references",d->referenceJson()},
        {"demanded",QJsonArray::fromStringList(demanded)},{"resources",resources}};
    if(d->nativeRails)result["rails"]=d->nativeRails->snapshot();
    if(d->connector) {
        const auto& scene=d->connector->scene();const auto& painted=d->connector->completion();
        result["connector"]=QJsonObject{{"geometrySerial",double(d->connector->geometrySerial())},
            {"requestSerial",double(scene.identity.requestSerial)},{"layoutSerial",double(scene.identity.layoutSerial)},
            {"contentRevision",scene.identity.contentRevision},{"valid",scene.valid},
            {"paintSerial",double(painted.paintSerial)},{"paintedGeometrySerial",double(painted.geometrySerial)},
            {"paintedRequestSerial",double(painted.identity.requestSerial)},{"paintedSession",painted.identity.session},
            {"paintedContentRevision",painted.identity.contentRevision},{"paintedLayoutSerial",double(painted.identity.layoutSerial)},
            {"paintedValid",painted.valid},{"paintedPaths",painted.paths}};
    }
    return result;
}
bool LinkedContextController::eventFilter(QObject* watched,QEvent* event){
    if(d->active){
        d->nativeInput(watched,event);
        if(event->type()==QEvent::Show)for(const auto& entry:d->visibility)if(entry.first==watched){entry.first->hide();return false;}
        if(event->type()==QEvent::Resize&&(watched==d->nativeRails||(d->editor&&watched==d->editor->viewport())))d->arrange();
        if(d->connector&&d->editor&&watched==d->editor->viewport()
            &&(event->type()==QEvent::Move||event->type()==QEvent::Show||event->type()==QEvent::Hide))d->refreshConnector();
    }
    return QObject::eventFilter(watched,event);
}
