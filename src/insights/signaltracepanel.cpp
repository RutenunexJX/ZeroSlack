#include "signaltracepanel.h"
#include "signaltracemodels.h"
#include "compactlayout.h"
#include "insightvisualstyle.h"
#include "roundedicons.h"
#include <QMenu>
#include <QWidgetAction>
#include <QResizeEvent>
#include "semanticindexsnapshot.h"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFutureWatcher>
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QGraphicsView>
#include <QHeaderView>
#include <QHideEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QScrollBar>
#include <QTimer>
#include <QPainterPath>
#include <QPushButton>
#include <QQueue>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QShowEvent>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableView>
#include <QThreadPool>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <atomic>
#include <cmath>

namespace {
constexpr int NodeRole=Qt::UserRole+750,MoreRole=NodeRole+1;
class FeedbackView final : public QGraphicsView {
public:
    using QGraphicsView::QGraphicsView;
    bool automaticFit=true;
    void fitContents() {
        automaticFit=true;
        if(!scene() || scene()->items().isEmpty())return;
        const qreal margin=fontMetrics().height();
        const QRectF bounds=scene()->itemsBoundingRect().adjusted(-margin,-margin,margin,margin);
        setSceneRect(bounds);resetTransform();
        const qreal factor=qMin(1.0,qMin(qMax(1,viewport()->width()-4)/qMax(1.0,bounds.width()),
                                        qMax(1,viewport()->height()-4)/qMax(1.0,bounds.height())));
        scale(factor,factor);centerOn(bounds.center());
    }
    void zoom(qreal factor){automaticFit=false;const auto next=qBound(0.05,transform().m11()*factor,3.0);scale(next/transform().m11(),next/transform().m11());}
protected:
    void resizeEvent(QResizeEvent* event) override {QGraphicsView::resizeEvent(event);if(automaticFit)fitContents();}
};
QString statusName(SignalTraceStatus status) {
    switch(status) {
    case SignalTraceStatus::Ready:return QStringLiteral("Ready");
    case SignalTraceStatus::DefinitionOnly:return QStringLiteral("Definition only — choose an instance to follow dependencies");
    case SignalTraceStatus::Partial:return QStringLiteral("Partial");
    case SignalTraceStatus::Cancelled:return QStringLiteral("Cancelled");
    case SignalTraceStatus::Stale:return QStringLiteral("Stale — refresh or select the signal again");
    default:return QStringLiteral("Unavailable");
    }
}
bool writeFile(const QString& path,const QByteArray& bytes,QString* error) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit()) {
        if (error) *error=file.errorString(); return false;
    }
    return true;
}
}

struct SignalTracePanel::Private {
    SignalTracePanel* q;
    SignalTraceContext context;
    LiveInsightRequestKey requestKey;
    std::shared_ptr<SignalTraceService> service;
    SignalTraceObject root,selected,feedbackSelected;
    QThreadPool pool;
    std::shared_ptr<std::atomic_bool> cancelled=std::make_shared<std::atomic_bool>(false);
    quint64 generation=1,selectionGeneration=0;
    int inFlight=0,nextNode=0;
    QLabel *title=nullptr,*status=nullptr,*detailSummary=nullptr;
    QComboBox *mode=nullptr,*direction=nullptr,*instance=nullptr,*role=nullptr,*cycles=nullptr;
    QLineEdit* search=nullptr;
    QSpinBox* depth=nullptr;
    QStackedWidget *pages=nullptr,*directions=nullptr,*content=nullptr;
    QWidget *filterBar=nullptr,*detailPage=nullptr;
    QTreeWidget *trees[2]={},*details=nullptr;
    QTableView* table=nullptr;
    SignalTraceHeatModel* heat=nullptr;
    QGraphicsScene* cycleScene=nullptr;
    FeedbackView* cycleView=nullptr;
    QAction *back=nullptr,*forward=nullptr;
    QPushButton *moreReferences=nullptr;
    QToolButton *mainArea=nullptr,*detailsButton=nullptr,*cancelButton=nullptr;
    QMenu *operations=nullptr,*pathsMenu=nullptr;
    struct ReferenceState { QList<SignalTraceEvidence> values;int cursor=-1;SignalTraceStatus status=SignalTraceStatus::Ready;QStringList limitations; };
    QHash<QString,ReferenceState> referenceCache;
    QString referenceSubject;
    QString referenceNotice,feedbackNotice,notices[3];
    QList<SignalTraceEvidence> feedbackDetails;
    bool feedbackReady=false,feedbackLoading=false;
    struct Node { SignalTraceObject object; QTreeWidgetItem* item=nullptr; int direction=0; bool loaded=false,loading=false,data=true; int cursor=0; QList<SignalTraceEvidence> evidence; QString path; bool repeated=false; };
    QHash<int,Node> nodes;
    QHash<QString,QList<SignalTraceEvidence>> detailEvidence;
    QList<SignalTraceObject> choices;
    struct History { SignalTraceObject root; int direction=0; QStringList expanded; QString selectedPath; int scroll[2]={}; };
    QList<History> history;
    int historyIndex=-1;
    QList<QList<SignalTraceObject>> pinned;
    QList<int> pinDirections;
    QStringList restoreExpanded;
    SignalFeedbackReport feedback;
    QList<SignalTraceEvidence> currentDetails;
    QStringList queryLimitations[2];
    int referenceCursor=-1;
    bool restoring=false,syncing=false,expanded=false,stale=false;
    bool restoreReading=false,needsReload=false;
    std::function<bool(const SignalTraceEvidence&)> navigation;
    std::function<void(const QString&,int)> statusMessage;
    std::function<void(const QList<SignalTraceEvidence>&)> markers;
    std::function<void(const SignalTraceObject&)> selection;
    std::function<void(bool)> mainAreaHandler;

    explicit Private(SignalTracePanel* owner):q(owner) { pool.setMaxThreadCount(1); }
    void presentStatus() {
        const int active=mode->currentIndex();
        QStringList messages;
        if(stale)messages<<QStringLiteral("Source changed — refresh before continuing.");
        if(active==2)messages<<feedbackNotice;
        else {messages<<referenceNotice;messages+=queryLimitations[direction->currentIndex()];}
        messages<<notices[active];messages.removeAll(QString());messages.removeDuplicates();
        const auto text=messages.join(QStringLiteral(" · "));
        status->setText(text);status->setToolTip(text);status->setVisible(!text.isEmpty());
    }
    void message(const QString& text,bool persistent=true) {
        if(persistent){notices[mode->currentIndex()]=text;presentStatus();}
        if(statusMessage && !text.isEmpty())statusMessage(text,4000);
    }
    void present() {
        if(!details)return;
        if(mode->currentIndex()==2) {
            showDetails(feedbackDetails);
            QStringList summary=feedback.limitations;
            const int index=cycles->currentIndex();
            if(index>=0 && index<feedback.findings.size())summary+=feedback.findings[index].limitations;
            summary.removeDuplicates();
            detailSummary->setText(summary.join(QStringLiteral("\n")));
        } else {
            showDetails(heat->references(mode->currentIndex()==1 ? table->currentIndex() : QModelIndex()));
            detailSummary->setText(selected.displayName()+QStringLiteral(" · ")+selected.instancePath);
        }
        detailSummary->setVisible(!detailSummary->text().isEmpty());
        moreReferences->setVisible(mode->currentIndex()!=2 && referenceCursor>=0);
        presentStatus();
    }
    void setDetailsOpen(bool open) {
        {QSignalBlocker block(detailsButton);detailsButton->setChecked(open);}
        detailsButton->setText(open?QStringLiteral("Back"):QStringLiteral("References"));
        detailsButton->setIcon(RoundedIcons::icon(open?RoundedIcons::Left:RoundedIcons::Info));
        detailsButton->setToolTip(open?QStringLiteral("Return to the current view"):QStringLiteral("Show source references"));
        content->setCurrentIndex(open?1:0);
        filterBar->setVisible(mode->currentIndex()!=2 && (open || mode->currentIndex()==1));
    }
    void invalidate() {
        cancelled->store(true); pool.clear(); cancelled=std::make_shared<std::atomic_bool>(false);
        if(feedbackLoading)feedbackNotice.clear();
        ++generation; ++selectionGeneration; inFlight=0;feedbackLoading=false;
        if(cancelButton)cancelButton->hide();
        for (auto& node:nodes) node.loading=false;
        if (markers) markers({});
    }
    template<class Result,class Work,class Apply> void run(Work work,Apply apply) {
        const auto token=cancelled; const auto version=generation; const auto key=requestKey;
        auto* watcher=new QFutureWatcher<Result>(q); ++inFlight;cancelButton->show();
        QObject::connect(watcher,&QFutureWatcher<Result>::finished,q,[this,watcher,version,key,token,apply] {
            watcher->deleteLater();
            if (version!=generation || token->load() || !(key==requestKey)) return;
            --inFlight;cancelButton->setVisible(inFlight>0);
            if (!watcher->isCanceled() && watcher->future().resultCount()>0) apply(watcher->result());
            if(!inFlight && restoreReading && historyIndex>=0 && historyIndex<history.size()){
                restoreReading=false;const auto reading=history[historyIndex];
                for(auto it=nodes.cbegin();it!=nodes.cend();++it)if(it->path==reading.selectedPath){
                    {QSignalBlocker blocker(trees[it->direction]);trees[it->direction]->setCurrentItem(it->item);}
                    select(it.key(),false);break;
                }
                QTimer::singleShot(0,q,[this,reading,version]{if(version==generation)for(int dir=0;dir<2;++dir)trees[dir]->verticalScrollBar()->setValue(reading.scroll[dir]);});
            }
        });
        watcher->setFuture(QtConcurrent::run(&pool,[work,token] { return work([token]{return token->load();}); }));
    }
    QPushButton* button(QHBoxLayout* row,const QString& label,const QString& name,std::function<void()> action) {
        auto* value=new QPushButton(label,q); value->setObjectName(name); InsightVisualStyle::applyToolbarButton(value);
        row->addWidget(value); QObject::connect(value,&QPushButton::clicked,q,std::move(action)); return value;
    }
    void remember() {
        if (historyIndex<0 || historyIndex>=history.size()) return;
        auto& entry=history[historyIndex]; entry.direction=direction->currentIndex(); entry.expanded.clear();
        for (const auto& node:nodes) if (node.item && node.item->isExpanded()) entry.expanded<<node.path;
        if(auto* item=trees[entry.direction]->currentItem())entry.selectedPath=nodes.value(item->data(0,NodeRole).toInt()).path;
        for(int dir=0;dir<2;++dir)entry.scroll[dir]=trees[dir]->verticalScrollBar()->value();
    }
    void historyButtons() { back->setEnabled(historyIndex>0); forward->setEnabled(historyIndex+1<history.size()); }
    int roleMask()const{return role->currentData().isValid() ? role->currentData().toInt() : role->currentIndex()==0 ? 63 : 1<<(role->currentIndex()-1);}
    void restoreRoleMask(int mask){
        mask &=63;if(mask==63){role->setCurrentIndex(0);return;}
        for(int bit=0;bit<6;++bit)if(mask==(1<<bit)){role->setCurrentIndex(bit+1);return;}
        while(role->count()>7)role->removeItem(7);
        QStringList names;for(int bit=0;bit<6;++bit)if(mask&(1<<bit))names<<SignalTraceService::roleName(SignalTraceRole(bit));
        role->addItem(names.isEmpty()?QStringLiteral("No roles"):names.join(QStringLiteral(" + ")),mask);role->setCurrentIndex(7);
    }
    void applyFilter(){heat->setRoleMaskFilter(search->text(),roleMask());present();}
    int addNode(const SignalTraceObject& object,QTreeWidgetItem* parent,int dir,bool data,
        QList<SignalTraceEvidence> evidence={},const QString& path={}) {
        const int id=++nextNode;
        auto* item=parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(trees[dir]);
        item->setData(0,NodeRole,id);
        item->setText(0,object.displayName());
        const QString parentInstance=parent?nodes.value(parent->data(0,NodeRole).toInt()).object.instancePath:root.instancePath;
        item->setText(1,object.instancePath==parentInstance?QString():object.instancePath);
        item->setText(2,data ? QString() : QStringLiteral("Related — use as root"));
        item->setToolTip(0,object.objectPath+QStringLiteral("\nBits: ")+object.bits.key());
        const QString nodePath=path.isEmpty() ? QString::number(dir)+QLatin1Char('/')+object.id() : path;
        bool ancestor=false;
        for (auto* at=parent;at;at=at->parent()) {
            const auto before=nodes.value(at->data(0,NodeRole).toInt()).object;
            if (before.objectPath==object.objectPath && before.bits.overlaps(object.bits)) { ancestor=true; break; }
        }
        if (ancestor) item->setText(2,QStringLiteral("Repeated object — analyze feedback"));
        nodes[id]={object,item,dir,false,false,data,0,std::move(evidence),nodePath,ancestor};
        if (data && !ancestor && object.isConcrete()) new QTreeWidgetItem(item,{QStringLiteral("Expand to query")});
        if (restoreExpanded.contains(nodePath)) item->setExpanded(true);
        return id;
    }
    void load(int id,int cursor=0) {
        if (!service || !nodes.contains(id) || stale) return;
        auto& node=nodes[id];
        if (node.loading || (!cursor && node.loaded) || !node.data || node.repeated) return;
        node.loading=true;
        auto captured=service; const auto object=node.object; const auto dir=SignalTraceDirection(node.direction);
        run<SignalTracePage>([captured,object,dir,cursor](auto stop) { return captured->oneHop(object,dir,cursor,100,stop); },
            [this,id,cursor](const SignalTracePage& page) {
                if (!nodes.contains(id)) return;
                auto& parent=nodes[id]; parent.loading=false; parent.loaded=true; parent.cursor=page.nextCursor;
                if (!cursor) { while (parent.item->childCount()) delete parent.item->takeChild(0); }
                else for (int i=parent.item->childCount()-1;i>=0;--i) if (parent.item->child(i)->data(0,MoreRole).isValid()) delete parent.item->takeChild(i);
                const auto parentItem=parent.item; const int dir=parent.direction; const QString path=parent.path;
                for (const auto& group:page.groups) {
                    if (nodes.size()>=2000) { queryLimitations[dir]<<QStringLiteral("Tree limit reached (2000 objects). Choose a new root."); break; }
                    int existing=0;
                    for (int i=0;i<parentItem->childCount();++i) {
                        const int child=parentItem->child(i)->data(0,NodeRole).toInt();
                        if (nodes.contains(child) && nodes[child].object.id()==group.object.id() && nodes[child].data==group.dataPath) { existing=child; break; }
                    }
                    if (existing) nodes[existing].evidence.append(group.evidence);
                    else addNode(group.object,parentItem,dir,group.dataPath,group.evidence,path+QLatin1Char('/')+group.object.id()+(group.dataPath?"/data":"/related"));
                }
                if (page.nextCursor>=0) { auto* more=new QTreeWidgetItem(parentItem,{QStringLiteral("Load more…")}); more->setData(0,MoreRole,id); }
                parentItem->setText(2,page.status==SignalTraceStatus::Ready ? QString() : statusName(page.status));
                parentItem->setToolTip(2,page.limitations.join('\n'));
                queryLimitations[dir]+=page.limitations;queryLimitations[dir].removeDuplicates();
                if(page.status==SignalTraceStatus::Stale){stale=true;if(markers)markers({});}
                presentStatus();
            });
    }
    void showDetails(QList<SignalTraceEvidence> values) {
        currentDetails=std::move(values); details->clear();
        for (int i=0;i<currentDetails.size();++i) {
            const auto& value=currentDetails[i];
            auto* item=new QTreeWidgetItem(details,{SignalTraceService::roleName(value.role),
                QStringLiteral("%1:%2:%3").arg(QFileInfo(value.range.fileName).fileName()).arg(value.range.line).arg(value.range.column),value.text});
            item->setData(0,Qt::UserRole,i); item->setToolTip(2,value.dependency.limitations.join('\n'));
        }
        if (markers && !stale && q->isVisible()) markers(currentDetails);
    }
    void applyReferences(const ReferenceState& state) {
        heat->setReferences(state.values);referenceCursor=state.cursor;
        heat->setComplete(state.cursor<0 && state.status==SignalTraceStatus::Ready);
        referenceNotice=state.status==SignalTraceStatus::Ready ? QString() : statusName(state.status);
        if(state.cursor>=0)referenceNotice+=QStringLiteral(" Reference counts are partial; load more in References.");
        if(!state.limitations.isEmpty())referenceNotice+=QStringLiteral(" ")+state.limitations.join(QStringLiteral(" · "));
        if(state.status==SignalTraceStatus::Stale){stale=true;if(markers)markers({});}
        present();
    }
    void references(const SignalTraceObject& object,int cursor=0) {
        if (!service || stale) return;
        const QString subject=object.id();
        if (!cursor) {
            if(referenceSubject==subject && (referenceCache.contains(subject) || inFlight)) {present();return;}
            ++selectionGeneration;referenceSubject=subject;
            if(referenceCache.contains(subject)){applyReferences(referenceCache[subject]);return;}
            heat->setReferences({});referenceCursor=-1;referenceNotice.clear();present();
        }
        const auto chosen=selectionGeneration; auto captured=service;
        run<SignalTracePage>([captured,object,cursor](auto stop) { return captured->references(object,cursor,500,stop); },
            [this,chosen,subject,cursor](const SignalTracePage& page) {
                if (chosen!=selectionGeneration || subject!=referenceSubject) return;
                if(!referenceCache.contains(subject) && referenceCache.size()>=4)referenceCache.erase(referenceCache.begin());
                auto& state=referenceCache[subject];if(!cursor)state.values.clear();
                state.values+=page.references;state.cursor=page.nextCursor;state.status=page.status;
                state.limitations+=page.limitations;state.limitations.removeDuplicates();
                applyReferences(state);
            });
    }
    void select(int id,bool notify=true) {
        if (!nodes.contains(id)) return;
        if(notify)restoreReading=false;
        selected=nodes[id].object; references(selected);
        if (notify && selection && !syncing) selection(selected);
    }
    void rebuild(const SignalTraceObject& object) {
        invalidate(); root=selected=object;feedbackSelected={};nodes.clear();
        for(auto& limits:queryLimitations)limits.clear();
        for(auto& notice:notices)notice.clear();referenceNotice.clear();feedbackNotice.clear();
        referenceCache.clear();referenceSubject.clear();feedbackDetails.clear();feedbackReady=false;
        trees[0]->clear(); trees[1]->clear(); heat->setReferences({}); showDetails({});
        cycleScene->clear(); cycles->clear(); feedback={};
        restoreReading=historyIndex>=0 && historyIndex<history.size() && history[historyIndex].root.id()==root.id()
            && !history[historyIndex].selectedPath.isEmpty();
        QString reason; stale=!service || !service->validate(root,&reason);
        if(service && root.isValid()){
            choices=service->resolve(root.symbol).choices;
            QSignalBlocker block(instance);instance->clear();
            for(const auto& choice:choices)instance->addItem(choice.instancePath);
            int selectedInstance=-1;for(int i=0;i<choices.size();++i)if(choices[i].objectPath==root.objectPath)selectedInstance=i;
            instance->setCurrentIndex(selectedInstance);instance->setVisible(choices.size()>1);
            instance->setPlaceholderText(QStringLiteral("Choose instance"));
        }
        title->setText(root.displayName()+QStringLiteral(" · ")+root.instancePath);
        needsReload=false;
        if (stale) { message(reason); return; }
        if(!q->isVisible()){needsReload=true;historyButtons();return;}
        for (int dir=0;dir<2;++dir) { const int id=addNode(root,nullptr,dir,true); nodes[id].item->setExpanded(true); load(id); }
        references(root); historyButtons();
        if(mode->currentIndex()==2)q->analyzeFeedback();
    }
    void updatePins() {
        pathsMenu->clear();
        for (int i=0;i<pinned.size();++i) {
            auto* entry=pathsMenu->addMenu(QStringLiteral("%1: %2 → %3").arg(i+1).arg(pinned[i].first().displayName(),pinned[i].last().displayName()));
            QObject::connect(entry->addAction(QStringLiteral("Open path")),&QAction::triggered,q,[this,i]{q->openPin(i);});
            QObject::connect(entry->addAction(QStringLiteral("Unpin")),&QAction::triggered,q,[this,i]{pinned.removeAt(i);pinDirections.removeAt(i);updatePins();});
        }
        pathsMenu->setEnabled(!pinned.isEmpty());
    }
    void navigate(const SignalTraceEvidence& evidence) {
        if (!service || stale || evidence.sourceHash.isEmpty()) { message(QStringLiteral("Source evidence is unavailable or stale.")); return; }
        if (navigation && !navigation(evidence)) message(QStringLiteral("Source revision changed; refresh before navigating."));
    }
    void drawCycle(int index);
};

SignalTracePanel::SignalTracePanel(QWidget* parent):QWidget(parent),d(std::make_unique<Private>(this)) {
    setObjectName(QStringLiteral("signalTracePanel"));
    auto* layout=new QVBoxLayout(this); layout->setContentsMargins(6,6,6,6); layout->setSpacing(4);
    auto* top=new QHBoxLayout;top->setSpacing(4);
    d->title=new CompactTitleLabel(this);d->title->setText(QStringLiteral("Signal Trace"));
    d->title->setTextInteractionFlags(Qt::TextSelectableByMouse);top->addWidget(d->title,1);
    d->mode=new QComboBox(this);d->mode->setObjectName("traceMode");d->mode->addItems({"Trace","Heatmap","Feedback"});top->addWidget(d->mode);
    d->direction=new QComboBox(this);d->direction->setObjectName("traceDirection");d->direction->addItems({"Upstream","Downstream"});top->addWidget(d->direction);
    d->detailsButton=new QToolButton(this);d->detailsButton->setObjectName("traceDetails");d->detailsButton->setCheckable(true);
    d->detailsButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);top->addWidget(d->detailsButton);
    d->cancelButton=new QToolButton(this);d->cancelButton->setObjectName("traceCancel");d->cancelButton->setIcon(RoundedIcons::icon(RoundedIcons::Close));
    d->cancelButton->setToolTip("Cancel query");d->cancelButton->hide();top->addWidget(d->cancelButton);
    connect(d->cancelButton,&QToolButton::clicked,this,&SignalTracePanel::cancel);
    auto* more=new QToolButton(this);more->setObjectName("traceMore");more->setIcon(RoundedIcons::icon(RoundedIcons::More));more->setToolTip("Trace actions");
    more->setPopupMode(QToolButton::InstantPopup);d->operations=new QMenu(more);more->setMenu(d->operations);top->addWidget(more);
    const auto action=[this](QMenu* menu,const QString& text,const QString& name,std::function<void()> callback){
        auto* entry=menu->addAction(text);entry->setObjectName(name);connect(entry,&QAction::triggered,this,std::move(callback));return entry;
    };
    d->back=action(d->operations,"Back","traceBack",[this]{goBack();});d->back->setIcon(RoundedIcons::icon(RoundedIcons::Left));
    d->forward=action(d->operations,"Forward","traceForward",[this]{goForward();});d->forward->setIcon(RoundedIcons::icon(RoundedIcons::Right));
    action(d->operations,"Refresh","traceRefresh",[this]{refresh();})->setIcon(RoundedIcons::icon(RoundedIcons::Refresh));
    action(d->operations,"Use selected as root","traceRebase",[this]{const auto object=selectedObject();if(object.isValid())showObject(object);});
    auto* coneMenu=d->operations->addMenu("Expand cone");auto* cone=new QWidget(coneMenu);auto* coneLayout=new QHBoxLayout(cone);
    d->depth=new QSpinBox(cone);d->depth->setObjectName("traceConeDepth");d->depth->setRange(1,8);d->depth->setValue(2);d->depth->setPrefix("Levels ");coneLayout->addWidget(d->depth);
    d->button(coneLayout,"Expand","traceCone",[this,coneMenu]{coneMenu->close();d->operations->close();expandCone(d->depth->value());});
    auto* coneAction=new QWidgetAction(coneMenu);coneAction->setDefaultWidget(cone);coneMenu->addAction(coneAction);
    action(d->operations,"Analyze instance","traceAnalyze",[this]{analyzeFeedback();});
    d->operations->addSeparator();
    action(d->operations,"Pin selected path","tracePin",[this]{pinSelection();});d->pathsMenu=d->operations->addMenu("Pinned paths");d->updatePins();
    action(d->operations,"Save trace…","traceSave",[this]{const auto path=QFileDialog::getSaveFileName(this,"Save trace",{},"Trace (*.ztrace)");if(!path.isEmpty()){QString error;if(!saveSession(path,&error))d->message(error);}});
    action(d->operations,"Open trace…","traceOpen",[this]{const auto path=QFileDialog::getOpenFileName(this,"Open trace",{},"Trace (*.ztrace)");if(!path.isEmpty()){QString error;if(!openSession(path,&error))d->message(error);}});
    d->operations->addSeparator();
    action(d->operations,"Fit diagram","traceFit",[this]{focusFit();});
    action(d->operations,"Copy table","traceCopy",[this]{QApplication::clipboard()->setText(QString::fromUtf8(d->heat->csv()));});
    action(d->operations,"Export table…","traceExport",[this]{const auto path=QFileDialog::getSaveFileName(this,"Export heatmap",{},"CSV (*.csv)");if(!path.isEmpty()){QString error;if(!exportHeatmap(path,&error))d->message(error);}});
    d->mainArea=new QToolButton(this);d->mainArea->setObjectName("traceMainArea");d->mainArea->setIcon(RoundedIcons::icon(RoundedIcons::Expand));d->mainArea->setToolTip("Expand into main area");d->mainArea->hide();top->addWidget(d->mainArea);
    connect(d->mainArea,&QToolButton::clicked,this,[this]{if(d->mainAreaHandler)d->mainAreaHandler(!d->expanded);});layout->addLayout(top);
    d->instance=new QComboBox(this);d->instance->setObjectName("traceInstance");d->instance->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);d->instance->hide();layout->addWidget(d->instance);
    d->filterBar=new QWidget(this);auto* filters=new QHBoxLayout(d->filterBar);filters->setContentsMargins(0,0,0,0);
    d->search=new QLineEdit(this);d->search->setObjectName("traceFilter");d->search->setPlaceholderText("Filter references");filters->addWidget(d->search,1);
    d->role=new QComboBox(this);d->role->setObjectName("traceRole");d->role->addItem("All roles");for(int i=0;i<6;++i)d->role->addItem(SignalTraceService::roleName(SignalTraceRole(i)));filters->addWidget(d->role);layout->addWidget(d->filterBar);
    d->status=new QLabel(this);d->status->setObjectName("traceStatus");d->status->setWordWrap(true);layout->addWidget(d->status);
    d->content=new QStackedWidget(this);layout->addWidget(d->content,1);
    d->pages=new QStackedWidget(d->content);d->content->addWidget(d->pages); d->directions=new QStackedWidget(d->pages);
    for(int dir=0;dir<2;++dir) {
        auto* tree=new QTreeWidget(d->directions); d->trees[dir]=tree; tree->setObjectName(dir ? "traceDownstream" : "traceUpstream");
        tree->setHeaderLabels({"Signal / access","Instance","Relation / status"});tree->setHeaderHidden(true); tree->setUniformRowHeights(true); tree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
        d->directions->addWidget(tree);
        connect(tree,&QTreeWidget::itemExpanded,this,[this](QTreeWidgetItem* item){d->load(item->data(0,NodeRole).toInt());});
        connect(tree,&QTreeWidget::currentItemChanged,this,[this](QTreeWidgetItem* item){if(item)d->select(item->data(0,NodeRole).toInt());});
        connect(tree,&QTreeWidget::itemActivated,this,[this](QTreeWidgetItem* item){
            if(item->data(0,MoreRole).isValid()){int id=item->data(0,MoreRole).toInt();d->load(id,d->nodes.value(id).cursor);}
            else {const auto node=d->nodes.value(item->data(0,NodeRole).toInt());if(!node.evidence.isEmpty())d->navigate(node.evidence.first());}
        });
    }
    d->pages->addWidget(d->directions);
    d->heat=new SignalTraceHeatModel(this); d->table=new QTableView(d->pages); d->table->setObjectName("traceHeatmap"); d->table->setModel(d->heat);
    d->table->setSortingEnabled(true); d->table->setSelectionMode(QAbstractItemView::SingleSelection); d->table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    d->table->viewport()->installEventFilter(this);
    d->table->setItemDelegateForColumn(SignalTraceHeatModel::Distribution,new SignalTraceDistributionDelegate(d->table));
    d->table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents); d->table->horizontalHeader()->setSectionResizeMode(SignalTraceHeatModel::Distribution,QHeaderView::Stretch);
    d->pages->addWidget(d->table);
    auto* feedbackPage=new QWidget(d->pages); auto* feedbackLayout=new QVBoxLayout(feedbackPage); feedbackLayout->setContentsMargins(0,0,0,0);
    d->cycles=new QComboBox(feedbackPage); d->cycles->setObjectName("traceCycles");d->cycles->hide(); feedbackLayout->addWidget(d->cycles);
    d->cycleScene=new QGraphicsScene(this); d->cycleView=new FeedbackView(d->cycleScene,feedbackPage); d->cycleView->setObjectName("traceCycleView");
    d->cycleView->setRenderHint(QPainter::Antialiasing); feedbackLayout->addWidget(d->cycleView); d->pages->addWidget(feedbackPage);
    auto* detailPage=new QWidget(d->content);d->detailPage=detailPage;d->content->addWidget(detailPage);
    auto* detailLayout=new QVBoxLayout(detailPage);detailLayout->setContentsMargins(0,0,0,0);
    d->detailSummary=new QLabel(detailPage);d->detailSummary->setWordWrap(true);detailLayout->addWidget(d->detailSummary);
    d->details=new QTreeWidget(detailPage); d->details->setObjectName("traceReferences"); d->details->setHeaderLabels({"Role","Source","Evidence"}); d->details->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    detailLayout->addWidget(d->details); d->moreReferences=new QPushButton("Load more references",detailPage); detailLayout->addWidget(d->moreReferences); d->moreReferences->hide();
    const auto changeMode=[this,coneMenu](int mode){
        d->pages->setCurrentIndex(mode);d->direction->setVisible(mode==0);d->setDetailsOpen(false);
        coneMenu->menuAction()->setVisible(mode==0);findChild<QAction*>("tracePin")->setVisible(mode==0);
        for(const auto& name:{"traceCopy","traceExport"})findChild<QAction*>(name)->setVisible(mode==1);
        findChild<QAction*>("traceFit")->setVisible(mode==2);d->present();
        if(mode==2 && !d->feedbackReady && !d->feedbackLoading && d->root.isConcrete())analyzeFeedback();
        else if(mode==2 && !d->root.isConcrete()){d->feedbackNotice=QStringLiteral("Choose an actual instance before analyzing feedback.");d->presentStatus();}
        else if(mode==2 && d->cycleView->automaticFit)d->cycleView->fitContents();
    };
    connect(d->mode,&QComboBox::currentIndexChanged,this,changeMode);
    connect(d->detailsButton,&QToolButton::toggled,this,[this](bool open){d->setDetailsOpen(open);d->present();});
    connect(d->direction,&QComboBox::currentIndexChanged,this,[this](int dir){
        d->directions->setCurrentIndex(dir);
        auto* item=d->trees[dir]->currentItem();if(!item)item=d->trees[dir]->topLevelItem(0);
        if(item)d->select(item->data(0,NodeRole).toInt(),false);else d->present();
    });
    connect(d->instance,&QComboBox::activated,this,[this](int i){if(i>=0 && i<d->choices.size())showObject(d->choices[i]);});
    connect(d->search,&QLineEdit::textChanged,this,[this]{d->applyFilter();});
    connect(d->role,&QComboBox::currentIndexChanged,this,[this]{d->applyFilter();});
    connect(d->table->selectionModel(),&QItemSelectionModel::currentChanged,this,[this]{d->present();});
    connect(d->table,&QTableView::clicked,this,[this]{d->setDetailsOpen(true);d->present();});
    connect(d->heat,&QAbstractItemModel::modelReset,this,[this]{d->present();});
    connect(d->table,&QTableView::doubleClicked,this,[this](const QModelIndex& index){auto refs=d->heat->references(index);if(!refs.isEmpty())d->navigate(refs.first());});
    connect(d->details,&QTreeWidget::itemActivated,this,[this](QTreeWidgetItem* item){int index=item->data(0,Qt::UserRole).toInt();if(index>=0 && index<d->currentDetails.size())d->navigate(d->currentDetails[index]);});
    connect(d->moreReferences,&QPushButton::clicked,this,[this]{if(d->referenceCursor>=0)d->references(d->selected,d->referenceCursor);});
    connect(d->cycles,&QComboBox::currentIndexChanged,this,[this](int index){d->drawCycle(index);});
    connect(d->cycleScene,&QGraphicsScene::selectionChanged,this,[this]{
        const auto selected=d->cycleScene->selectedItems();if(selected.isEmpty())return;
        const QString object=selected.first()->data(0).toString(),edgeId=selected.first()->data(1).toString();
        QList<SignalTraceEvidence> evidence;QSet<QString> seen;
        for(const auto& finding:d->feedback.findings)for(const auto& edge:finding.edges)
            if(edge.id==edgeId || edge.from.objectPath==object || edge.to.objectPath==object){
                if(!seen.contains(edge.evidence.id)){seen.insert(edge.evidence.id);evidence<<edge.evidence;}
                d->feedbackSelected=edge.from.objectPath==object ? edge.from : edge.to;
            }
        if(!evidence.isEmpty())d->feedbackDetails=evidence;
        d->present();
        if(d->mode->currentIndex()==2){d->setDetailsOpen(true);if(d->selection && d->feedbackSelected.isValid())d->selection(d->feedbackSelected);}
    });
    changeMode(d->mode->currentIndex());d->historyButtons();d->message("Select a signal to begin.");
}
SignalTracePanel::~SignalTracePanel() { d->invalidate(); d->pool.waitForDone(); }
void SignalTracePanel::setContext(SignalTraceContext context,LiveInsightRequestKey key) {
    const bool changed=context.workspaceId!=d->context.workspaceId || context.projectId!=d->context.projectId;
    if (!changed && context.token.revision==d->context.token.revision && context.token.snapshot==d->context.token.snapshot
        && context.currentSourceHashes==d->context.currentSourceHashes) return;
    d->invalidate(); d->context=std::move(context); d->requestKey=std::move(key); d->service=std::make_shared<SignalTraceService>(d->context);
    if(changed){clearTarget();d->history.clear();d->historyIndex=-1;d->choices.clear();d->instance->clear();d->historyButtons();}
    else if(d->root.isValid()) {d->remember(); d->restoreExpanded=d->history.value(d->historyIndex).expanded;d->rebuild(d->root);}
}
void SignalTracePanel::showForSymbol(const QString& name,const QString& file,const QString& module,const QString& access) {
    if(!d->service){d->message("Semantic analysis is not available.");return;}
    const auto result=d->service->resolve({},name,file,module,access); d->choices=result.choices;
    {QSignalBlocker blocker(d->instance);d->instance->clear();for(const auto& choice:d->choices)d->instance->addItem(choice.instancePath);d->instance->setCurrentIndex(result.status==SignalTraceStatus::Ready ? 0 : -1);}
    if(result.object.isValid())showObject(result.object); else clearTarget();
    if(!result.message.isEmpty())d->message(result.message);
}
void SignalTracePanel::showObject(const SignalTraceObject& object,bool recordHistory) {
    if(!object.isValid())return; d->remember();
    if(recordHistory && (d->historyIndex<0 || d->history[d->historyIndex].root.id()!=object.id())) {
        while(d->history.size()>d->historyIndex+1)d->history.removeLast();
        d->history.append({object,d->direction->currentIndex(),{}}); if(d->history.size()>50)d->history.removeFirst();
        d->historyIndex=d->history.size()-1; d->restoreExpanded.clear();
    }
    d->rebuild(object);
}
void SignalTracePanel::clearTarget() {
    d->invalidate();d->root={};d->selected={};d->feedbackSelected={};d->feedbackReady=false;d->referenceCache.clear();d->referenceSubject.clear();d->referenceNotice.clear();d->feedbackNotice.clear();d->feedbackDetails.clear();for(auto& limits:d->queryLimitations)limits.clear();for(auto& notice:d->notices)notice.clear();d->nodes.clear();for(auto* tree:d->trees)tree->clear();
    d->heat->setReferences({});d->showDetails({});d->cycleScene->clear();d->feedback={};d->title->setText("Signal Trace");d->message("Select a signal to begin.");
}
void SignalTracePanel::refresh() {
    if(!d->root.isValid() || !d->service)return;
    const auto result=d->service->resolve(d->root.symbol,{}, {},{},d->root.accessPath);
    for(const auto& choice:result.choices)if(choice.objectPath==d->root.objectPath){showObject(choice,false);return;}
    if(!d->root.isConcrete() && result.object.isValid())showObject(result.object,false);else d->message("The saved declaration or instance is unavailable; select the signal again.");
}
void SignalTracePanel::cancel(){d->invalidate();d->message("Cancelled. Expand a branch or refresh to continue.");}
void SignalTracePanel::setHeatmapMode(bool enabled){d->mode->setCurrentIndex(enabled?1:0);}
void SignalTracePanel::setNavigationHandler(std::function<bool(const SignalTraceEvidence&)> handler){d->navigation=std::move(handler);}
void SignalTracePanel::setStatusMessageHandler(std::function<void(const QString&,int)> handler){d->statusMessage=std::move(handler);}
void SignalTracePanel::setMarkerHandler(std::function<void(const QList<SignalTraceEvidence>&)> handler){d->markers=std::move(handler);}
void SignalTracePanel::setSelectionHandler(std::function<void(const SignalTraceObject&)> handler){d->selection=std::move(handler);}
void SignalTracePanel::syncSelection(const SignalTraceObject& object){
    if(d->mode->currentIndex()==2 || d->syncing || object.workspaceId!=d->context.workspaceId || object.projectId!=d->context.projectId)return;
    d->syncing=true;
    const auto ids=d->nodes.keys();int selectedId=0;
    for(int id:ids){const auto& node=d->nodes[id];if(node.direction==d->direction->currentIndex() && node.object.objectPath==object.objectPath && node.object.bits.overlaps(object.bits)
        && (!selectedId || id<selectedId))selectedId=id;}
    if(selectedId){
        const auto& node=d->nodes[selectedId];
        {QSignalBlocker blocker(d->trees[node.direction]);d->trees[node.direction]->setCurrentItem(node.item);}
        if(d->selected.id()!=node.object.id() && isVisible())d->select(selectedId,false);
    }
    d->syncing=false;
}
void SignalTracePanel::syncInstanceSelection(const QString& instancePath){
    if(d->selected.instancePath==instancePath)return;
    int selectedId=0;for(auto it=d->nodes.cbegin();it!=d->nodes.cend();++it)
        if(it->direction==d->direction->currentIndex() && it->object.instancePath==instancePath && (!selectedId || it.key()<selectedId))selectedId=it.key();
    if(selectedId)syncSelection(d->nodes[selectedId].object);
}
void SignalTracePanel::setMainAreaHandler(std::function<void(bool)> handler){d->mainAreaHandler=std::move(handler);d->mainArea->setVisible(bool(d->mainAreaHandler));}
void SignalTracePanel::setInMainArea(bool expanded){d->expanded=expanded;d->mainArea->setText(expanded?"Dock":"Expand");d->mainArea->setToolTip(expanded?"Dock in bottom panel":"Expand into main area");d->mainArea->setIcon(RoundedIcons::icon(expanded?RoundedIcons::Collapse:RoundedIcons::Expand));}
bool SignalTracePanel::isInMainArea()const{return d->expanded;}
void SignalTracePanel::setFocusSearchText(const QString& text){d->search->setText(text);}
QString SignalTracePanel::focusSearchText()const{return d->search->text();}
void SignalTracePanel::focusFit(){if(d->mode->currentIndex()==2)d->cycleView->fitContents();else d->trees[d->direction->currentIndex()]->scrollToTop();}
void SignalTracePanel::focusZoomIn(){if(d->mode->currentIndex()==2)d->cycleView->zoom(1.2);else{auto f=font();f.setPointSizeF(qMin(24.0,f.pointSizeF()+1));setFont(f);}}
void SignalTracePanel::focusZoomOut(){if(d->mode->currentIndex()==2)d->cycleView->zoom(1/1.2);else{auto f=font();f.setPointSizeF(qMax(7.0,f.pointSizeF()-1));setFont(f);}}
void SignalTracePanel::refreshThemePresentation(){d->table->viewport()->update();for(auto* tree:d->trees)tree->viewport()->update();d->drawCycle(d->cycles->currentIndex());}
void SignalTracePanel::focusInspector(){d->setDetailsOpen(true);d->present();d->details->setFocus();}
SignalTraceObject SignalTracePanel::currentObject()const{return d->root;}
SignalTraceObject SignalTracePanel::selectedObject()const{return d->mode->currentIndex()==2 && d->feedbackSelected.isValid()?d->feedbackSelected:d->selected;}
SignalTraceHeatModel* SignalTracePanel::heatModel()const{return d->heat;}
QTreeWidget* SignalTracePanel::traceTree(SignalTraceDirection direction)const{return d->trees[int(direction)];}
QTableView* SignalTracePanel::heatTable()const{return d->table;}
bool SignalTracePanel::busy()const{return d->inFlight>0;}
void SignalTracePanel::goBack(){if(d->historyIndex<=0)return;d->remember();--d->historyIndex;const auto item=d->history[d->historyIndex];d->direction->setCurrentIndex(item.direction);d->restoreExpanded=item.expanded;d->rebuild(item.root);}
void SignalTracePanel::goForward(){if(d->historyIndex+1>=d->history.size())return;d->remember();++d->historyIndex;const auto item=d->history[d->historyIndex];d->direction->setCurrentIndex(item.direction);d->restoreExpanded=item.expanded;d->rebuild(item.root);}
void SignalTracePanel::hideEvent(QHideEvent* event){QWidget::hideEvent(event);d->remember();d->needsReload=d->needsReload||busy();d->invalidate();}
void SignalTracePanel::showEvent(QShowEvent* event){QWidget::showEvent(event);if(d->root.isValid() && d->needsReload && !busy()){d->restoreExpanded=d->history.value(d->historyIndex).expanded;d->rebuild(d->root);}else if(d->markers && !d->stale)d->markers(d->currentDetails);}
bool SignalTracePanel::eventFilter(QObject* object,QEvent* event){
    if(object==d->table->viewport() && event->type()==QEvent::MouseButtonDblClick){
        const auto* mouse=static_cast<QMouseEvent*>(event);const auto index=d->table->indexAt(mouse->position().toPoint());
        if(index.isValid() && index.column()==SignalTraceHeatModel::Distribution){
            const auto refs=d->heat->references(index);if(refs.isEmpty())return true;
            const auto rect=d->table->visualRect(index);const auto margin=fontMetrics().height()*0.45;
            const double fraction=qBound(0.0,(mouse->position().x()-rect.left()-margin)/qMax(1.0,rect.width()-2*margin),1.0);
            auto nearest=refs.first();double distance=2;
            for(const auto& ref:refs){const double delta=std::abs(fraction-double(ref.range.line-1)/qMax(1,ref.fileLineCount-1));if(delta<distance){distance=delta;nearest=ref;}}
            d->showDetails({nearest});d->setDetailsOpen(true);d->navigate(nearest);return true;
        }
    }
    return QWidget::eventFilter(object,event);
}

QVariantMap SignalTracePanel::saveViewState() const {
    d->remember();
    QJsonArray paths;
    for(int i=0;i<d->pinned.size();++i){QJsonArray objects;for(const auto& object:d->pinned[i])objects.append(object.toJson());paths.append(QJsonObject{{"direction",d->pinDirections[i]},{"objects",objects}});}
    QJsonArray history;
    for(const auto& item:d->history)history.append(QJsonObject{{"root",item.root.toJson()},{"direction",item.direction},{"expanded",QJsonArray::fromStringList(item.expanded)},
        {"selection",item.selectedPath},{"upstreamScroll",item.scroll[0]},{"downstreamScroll",item.scroll[1]}});
    return QJsonObject{{"schema","zeroslack-signal-trace/v1"},{"root",d->root.toJson()},{"pins",paths},{"history",history},
        {"historyIndex",d->historyIndex},{"mode",d->mode->currentIndex()},{"direction",d->direction->currentIndex()},
        {"filter",d->search->text()},{"role",d->role->currentIndex()},{"roleMask",d->roleMask()},{"depth",d->depth->value()},
        {"signal",d->root.symbol.symbolName}}.toVariantMap();
}
void SignalTracePanel::restoreViewState(const QVariantMap& state) {
    const auto json=QJsonObject::fromVariantMap(state);
    if(json["schema"].toString()!=QStringLiteral("zeroslack-signal-trace/v1")) {
        const auto name=state.value("signal",state.value("signalName")).toString();
        showForSymbol(name,state.value("file",state.value("fileName")).toString(),state.value("module",state.value("moduleName")).toString(),state.value("accessPath").toString());
        setHeatmapMode(state.contains("hotspotStateVersion") || state.value("matrix").toBool() || state.value("mode").toString()=="matrix" || state.value("matrixMode").toBool());
        setFocusSearchText(state.value("search").toString());
        if(state.contains("roles")){const int map[]={0,1,2,3,3,4,5};int mask=0;for(const auto& value:state.value("roles").toList()){int i=value.toInt();if(i>=0 && i<7)mask|=1<<map[i];}d->restoreRoleMask(mask);}
        d->message("Previous view migrated to Signal Trace. Target and filters restored; Track layout and old row positions are replaced by file distribution. Choose an instance if required.");
        return;
    }
    d->pinned.clear();d->pinDirections.clear();
    for(const auto& entry:json["pins"].toArray()) {
        if(d->pinned.size()>=50)break;
        QList<SignalTraceObject> path;
        for(const auto& value:entry.toObject()["objects"].toArray()){if(path.size()>=64)break;path.append(SignalTraceObject::fromJson(value.toObject()));}
        if(!path.isEmpty()){d->pinned.append(path);d->pinDirections.append(qBound(0,entry.toObject()["direction"].toInt(),1));}
    }
    d->updatePins();d->history.clear();
    for(const auto& entry:json["history"].toArray()) {
        if(d->history.size()>=50)break;
        const auto item=entry.toObject();QStringList expanded;
        for(const auto& value:item["expanded"].toArray())if(expanded.size()<2000)expanded<<value.toString();
        d->history.append({SignalTraceObject::fromJson(item["root"].toObject()),qBound(0,item["direction"].toInt(),1),expanded,item["selection"].toString(),
            {qMax(0,item["upstreamScroll"].toInt()),qMax(0,item["downstreamScroll"].toInt())}});
    }
    d->historyIndex=qBound(-1,json["historyIndex"].toInt(-1),int(d->history.size())-1);
    d->restoreExpanded=d->history.value(d->historyIndex).expanded;
    d->mode->setCurrentIndex(qBound(0,json["mode"].toInt(),2)); d->direction->setCurrentIndex(qBound(0,json["direction"].toInt(),1));
    d->search->setText(json["filter"].toString());d->role->setCurrentIndex(qBound(0,json["role"].toInt(),6));d->depth->setValue(json["depth"].toInt(2));
    if(json.contains("roleMask"))d->restoreRoleMask(json["roleMask"].toInt(63));
    d->rebuild(SignalTraceObject::fromJson(json["root"].toObject()));d->historyButtons();
}
bool SignalTracePanel::saveSession(const QString& path,QString* error)const{return writeFile(path,QJsonDocument(QJsonObject::fromVariantMap(saveViewState())).toJson(QJsonDocument::Indented),error);}
bool SignalTracePanel::openSession(const QString& path,QString* error){
    QFile file(path);if(!file.open(QIODevice::ReadOnly)){if(error)*error=file.errorString();return false;}
    if(file.size()>4*1024*1024){if(error)*error="Trace file exceeds 4 MiB.";return false;}
    QJsonParseError parse;const auto json=QJsonDocument::fromJson(file.readAll(),&parse);
    if(parse.error!=QJsonParseError::NoError || !json.isObject() || json.object()["schema"].toString()!="zeroslack-signal-trace/v1"){
        if(error)*error="Unsupported trace file.";return false;
    }
    restoreViewState(json.object().toVariantMap());return true;
}
bool SignalTracePanel::exportHeatmap(const QString& path,QString* error)const{
    if(d->stale || d->needsReload || busy() || !d->root.isValid()){if(error)*error=QStringLiteral("Wait for a current Signal Trace result before exporting.");return false;}
    return writeFile(path,d->heat->csv(),error);
}
GraphExportResult SignalTracePanel::exportGraph(const QString& path,const GraphExportOptions& options)const{
    if(d->stale || d->needsReload || busy() || !d->root.isValid()){GraphExportResult result;result.failure=GraphExportFailure::InvalidSource;
        result.outputPath=path;result.failureReason=QStringLiteral("Wait for a current Signal Trace result before exporting.");return result;}
    return GraphExportService::exportSource(GraphExportSource::fromWidget(d->pages->currentWidget()),path,options);
}
void SignalTracePanel::pinSelection(){
    auto* item=d->trees[d->direction->currentIndex()]->currentItem();QList<SignalTraceObject> path;
    for(auto* at=item;at;at=at->parent()) {const auto object=d->nodes.value(at->data(0,NodeRole).toInt()).object;if(object.isConcrete())path.prepend(object);}
    if(path.isEmpty() && d->root.isConcrete())path.append(d->root);
    if(path.isEmpty() || path.size()>64 || d->pinned.size()>=50){d->message("Pin a concrete path of at most 64 objects; up to 50 paths are retained.");return;}
    d->pinned.append(path);d->pinDirections.append(d->direction->currentIndex());d->updatePins();d->message("Path pinned. Save the trace to reopen it outside this view.",false);
}
void SignalTracePanel::openPin(int index){
    if(index<0 || index>=d->pinned.size() || !d->service)return;
    const auto path=d->pinned[index];const int dir=d->pinDirections[index];const auto service=d->service;
    d->run<QString>([service,path,dir](auto stop){
        QString reason;
        for(int i=0;i<path.size();++i){
            if(stop())return QStringLiteral("Cancelled.");
            if(!service->validate(path[i],&reason))return reason;
            if(!i)continue;
            bool connected=false;int cursor=0,total=0;
            do {const auto page=service->oneHop(path[i-1],SignalTraceDirection(dir),cursor,250,stop);total+=page.scanned;
                for(const auto& group:page.groups)if(group.dataPath && group.object.id()==path[i].id()){connected=true;break;}
                cursor=page.nextCursor;
            }while(!connected && cursor>=0 && total<20000 && !stop());
            if(!connected)return QStringLiteral("The pinned connection is unavailable or exceeds the validation budget; the path was not rebound.");
        }
        return QString();
    },[this,path,dir](const QString& error){
        if(!error.isEmpty()){d->message(error);return;}
        d->direction->setCurrentIndex(dir);showObject(path.first());d->restoreExpanded.clear();
        QString key=QString::number(dir);for(int i=0;i<path.size();++i){key+=QLatin1Char('/')+path[i].id();if(i)key+=QStringLiteral("/data");d->restoreExpanded<<key;}
        if(d->historyIndex>=0){d->history[d->historyIndex].selectedPath=key;d->restoreReading=true;}
        for(const auto& node:d->nodes)if(d->restoreExpanded.contains(node.path))node.item->setExpanded(true);
    });
}
void SignalTracePanel::analyzeFeedback(){
    if(!d->root.isConcrete() || !d->service || d->stale){d->message("Choose an actual instance before analyzing feedback.");return;}
    if(d->feedbackLoading)return;
    const auto context=d->context;const auto instance=d->root.instancePath;
    d->feedbackLoading=true;d->notices[2].clear();d->mode->setCurrentIndex(2);d->setDetailsOpen(false);
    d->feedbackNotice=QStringLiteral("Analyzing this instance and its descendants…");d->presentStatus();
    d->run<SignalFeedbackReport>([context,instance](auto stop){return SignalFeedbackService(context).analyze(instance,stop);},[this](const SignalFeedbackReport& report){
        d->feedback=report;d->feedbackLoading=false;d->feedbackReady=true;d->feedbackDetails.clear();d->feedbackSelected={};
        if(report.status==SignalTraceStatus::Stale){d->stale=true;if(d->markers)d->markers({});}
        d->feedbackNotice=report.status==SignalTraceStatus::Ready?QString():statusName(report.status)+QStringLiteral(" · ")+report.limitations.join(QStringLiteral(" · "));
        {QSignalBlocker block(d->cycles);d->cycles->clear();
            for(const auto& finding:report.findings){
                QStringList names;for(const auto& edge:finding.edges)if(!names.contains(edge.from.displayName()))names<<edge.from.displayName();
                const auto kind=finding.kind==SignalFeedbackKind::Combinational?QStringLiteral("Combinational"):finding.kind==SignalFeedbackKind::Sequential?QStringLiteral("Sequential"):QStringLiteral("Uncertain");
                d->cycles->addItem(kind+QStringLiteral(" — ")+names.join(QStringLiteral(" → ")));
                d->cycles->setItemData(d->cycles->count()-1,SignalFeedbackService::kindName(finding.kind)+QStringLiteral("\n")+finding.limitations.join('\n'),Qt::ToolTipRole);
            }
        }
        d->cycles->setVisible(!report.findings.isEmpty());d->drawCycle(d->cycles->currentIndex());
        if(report.findings.isEmpty()){d->cycleScene->clear();d->cycleScene->addText(report.status==SignalTraceStatus::Ready ? "No static feedback found in this captured scope." : "No confirmed result for the complete scope. See limitations.");d->cycleView->fitContents();}
        d->present();
    });
}
void SignalTracePanel::Private::drawCycle(int index){
    QSignalBlocker block(cycleScene);cycleScene->clear();feedbackDetails.clear();feedbackSelected={};
    if(index<0 || index>=feedback.findings.size()){present();return;}
    const auto& finding=feedback.findings[index];
    for(const auto& edge:finding.edges)feedbackDetails<<edge.evidence;
    if(!finding.edges.isEmpty())feedbackSelected=finding.edges.first().from;
    present();
    if(finding.edges.size()>48){cycleScene->addText(QStringLiteral("%1-edge cycle: open References. Diagrams are limited to 48 edges.").arg(finding.edges.size()));cycleView->fitContents();return;}
    QHash<QString,QGraphicsTextItem*> labels;QHash<QString,QRectF> boxes;QList<SignalTraceObject> objects;
    const qreal unit=QFontMetrics(q->font()).height();qreal width=0,height=0;
    const QColor accent=q->palette().color(QPalette::Highlight),text=q->palette().color(QPalette::Text);
    for(const auto& edge:finding.edges)for(const auto& object:{edge.from,edge.to})if(!labels.contains(object.id())){
        auto* label=cycleScene->addText(object.displayName()+QStringLiteral(" [%1]").arg(object.bits.key()),q->font());
        label->setDefaultTextColor(text);label->setZValue(2);label->setAcceptedMouseButtons(Qt::NoButton);
        labels[object.id()]=label;objects<<object;
        width=qMax(width,label->boundingRect().width()+unit);height=qMax(height,label->boundingRect().height()+unit*0.7);
    }
    const int count=objects.size();
    for(int i=0;i<count;++i){
        QPointF center;
        if(count==2)center=QPointF((i?1:-1)*(width+unit*5)/2,0);
        else if(count>2){const qreal angle=2*3.141592653589793*i/count-3.141592653589793/2;
            center={(width+unit*4)*count/(2*3.141592653589793)*std::cos(angle),(height+unit*5)*count/(2*3.141592653589793)*std::sin(angle)};}
        const auto& object=objects[i];auto* label=labels[object.id()];
        label->setPos(center-QPointF(label->boundingRect().width()/2,label->boundingRect().height()/2));
        const auto rect=label->sceneBoundingRect().adjusted(-unit/2,-unit/3,unit/2,unit/3);boxes[object.id()]=rect;
        auto* box=cycleScene->addRect(rect,QPen(accent),q->palette().base());box->setZValue(1);
        box->setToolTip(object.objectPath);box->setFlag(QGraphicsItem::ItemIsSelectable);box->setData(0,object.objectPath);box->setData(2,object.id());
    }
    QList<SignalFeedbackEdge> visible=finding.edges;QSet<QString> ids;for(const auto& edge:visible)ids.insert(edge.id);
    for(const auto& other:feedback.findings)for(const auto& edge:other.edges)if(!ids.contains(edge.id) && boxes.contains(edge.from.id()) && boxes.contains(edge.to.id())){ids.insert(edge.id);visible<<edge;}
    const auto boundary=[](const QRectF& rect,const QPointF& toward){
        const auto delta=toward-rect.center();const qreal scale=qMin(rect.width()/2/qMax(0.001,std::abs(delta.x())),rect.height()/2/qMax(0.001,std::abs(delta.y())));
        return rect.center()+delta*scale;
    };
    QHash<QString,int> parallel;
    for(const auto& edge:visible){
        const auto fromBox=boxes[edge.from.id()],toBox=boxes[edge.to.id()];
        const auto from=fromBox.center(),to=toBox.center();const int lane=parallel[edge.from.id()+edge.to.id()]++;
        QPainterPath path;
        if(from==to){
            const QPointF start(from.x()-fromBox.width()*0.3,fromBox.top()),end(from.x()+fromBox.width()*0.3,fromBox.top());
            const qreal rise=unit*(3+lane*1.5);path.moveTo(start);
            path.cubicTo(start+QPointF(-unit,-rise),end+QPointF(unit,-rise),end);
        } else {
            const auto delta=to-from;const qreal length=std::hypot(delta.x(),delta.y());const QPointF normal(-delta.y()/length,delta.x()/length);
            const auto control=(from+to)/2+normal*unit*(2+lane*1.5);
            path.moveTo(boundary(fromBox,control));path.quadTo(control,boundary(toBox,control));
        }
        auto* item=cycleScene->addPath(path,QPen(accent,1.6));item->setZValue(0);
        item->setToolTip(edge.evidence.text+"\n"+SignalTraceService::boundaryName(edge.evidence.dependency.boundary));
        item->setFlag(QGraphicsItem::ItemIsSelectable);item->setData(1,edge.id);item->setData(2,edge.from.id());item->setData(3,edge.to.id());
        const auto tip=path.pointAtPercent(1),before=path.pointAtPercent(0.98),delta=tip-before;const qreal angle=std::atan2(delta.y(),delta.x());
        const QPolygonF arrow{tip,tip-QPointF(std::cos(angle-0.45),std::sin(angle-0.45))*unit*0.6,tip-QPointF(std::cos(angle+0.45),std::sin(angle+0.45))*unit*0.6};
        auto* arrowItem=cycleScene->addPolygon(arrow,QPen(accent),QBrush(accent));arrowItem->setParentItem(item);arrowItem->setAcceptedMouseButtons(Qt::NoButton);
    }
    cycleView->fitContents();
}
void SignalTracePanel::expandCone(int depth){
    if(!d->service || !d->root.isConcrete() || d->stale)return;
    struct Cone { QList<QPair<QString,SignalTracePage>> pages; bool limited=false; };
    const auto root=d->root;const auto service=d->service;const int dir=d->direction->currentIndex();depth=qBound(1,depth,8);
    d->notices[0]=QStringLiteral("Expanding %1 levels…").arg(depth);d->presentStatus();
    d->run<Cone>([root,service,dir,depth](auto stop){
        Cone result;struct Pending{SignalTraceObject object;int level;QString path;};QQueue<Pending> queue;
        queue.enqueue({root,0,QString::number(dir)+QLatin1Char('/')+root.id()});QHash<QString,SignalTracePage> cache;int count=0;
        while(!queue.isEmpty() && !stop()){
            const auto current=queue.dequeue();if(current.level>=depth)continue;
            if(result.pages.size()>=400 || count>=1200){result.limited=true;break;}
            if(!cache.contains(current.object.id()))cache.insert(current.object.id(),service->oneHop(current.object,SignalTraceDirection(dir),0,100,stop));
            const auto page=cache.value(current.object.id());
            result.pages.append({current.path,page});if(page.nextCursor>=0)result.limited=true;
            for(const auto& group:page.groups)if(group.dataPath){++count;queue.enqueue({group.object,current.level+1,current.path+QLatin1Char('/')+group.object.id()+"/data"});}
        }
        return result;
    },[this,dir](const Cone& cone){
        for(const auto& pair:cone.pages){
            int id=0;for(auto it=d->nodes.cbegin();it!=d->nodes.cend();++it)if(it->path==pair.first){id=it.key();break;}
            if(!id || !d->nodes[id].data)continue;auto& parent=d->nodes[id];if(!parent.loaded){while(parent.item->childCount())delete parent.item->takeChild(0);}
            auto* item=parent.item;parent.loaded=true;parent.cursor=pair.second.nextCursor;const auto path=parent.path;
            for(const auto& group:pair.second.groups){bool present=false;for(int i=0;i<item->childCount();++i){const auto child=d->nodes.value(item->child(i)->data(0,NodeRole).toInt());if(child.object.id()==group.object.id() && child.data==group.dataPath){present=true;break;}}
                if(!present && d->nodes.size()<2000)d->addNode(group.object,item,dir,group.dataPath,group.evidence,path+QLatin1Char('/')+group.object.id()+(group.dataPath?"/data":"/related"));}
            item->setExpanded(true);
            if(pair.second.nextCursor>=0){bool more=false;for(int i=0;i<item->childCount();++i)if(item->child(i)->data(0,MoreRole).isValid())more=true;
                if(!more){auto* row=new QTreeWidgetItem(item,{QStringLiteral("Load more…")});row->setData(0,MoreRole,id);}}
        }
        d->notices[0]=cone.limited ? QStringLiteral("Cone is partial. Expand individual branches or load additional pages.") : QString();d->presentStatus();
    });
}
