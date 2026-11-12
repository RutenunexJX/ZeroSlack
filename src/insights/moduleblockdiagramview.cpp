#include "moduleblockdiagramview.h"
#include "uicontrols.h"
#include "roundedicons.h"
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QToolTip>
#include <QMenu>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <QTimer>
#include <QSet>
#include <QApplication>
#include <atomic>
#include <cmath>

namespace {
struct Geometry {
    QVector<QRectF> rects;
    QVector<QList<int>> children;
    QHash<QString, int> byId;
    QVector<qreal> headers;
};
using Report = std::shared_ptr<const ModuleBlockDiagramReport>;
Geometry arrange(const Report& report, const QFont& font, qreal width, const std::shared_ptr<std::atomic_bool>& cancel)
{
    Geometry g;
    const int count = report->nodes.size();
    g.rects.resize(count); g.children.resize(count); g.headers.resize(count);
    QHash<int,int> positions;
    const QFontMetricsF fm(font);
    for (int i = 0; i < count; ++i) { positions.insert(report->nodes[i].nodeId, i); g.byId.insert(report->nodes[i].stableId, i); }
    for (int i = 0; i < count; ++i) {
        const int parent = positions.value(report->nodes[i].parentNodeId, -1);
        if (parent >= 0) g.children[parent].append(i);
    }
    QVector<QPointF> offsets(count);
    for (int i = count - 1; i >= 0; --i) {
        if (cancel->load()) return {};
        const auto& node = report->nodes[i];
        const bool type = node.moduleTypeDisplayName != node.instanceDisplayName;
        const qreal header = fm.height() * (type ? 2 : 1) + 14;
        g.headers[i] = header;
        const qreal titleWidth = qBound(160.0, qMax(fm.horizontalAdvance(node.instanceDisplayName), fm.horizontalAdvance(node.moduleTypeDisplayName)) + 24, 280.0);
        qreal x = 12, y = header, rowHeight = 0, right = titleWidth;
        const qreal budget = qMax(180.0, width - node.depth * 24 - 24);
        int columns = 0;
        for (int child : g.children[i]) {
            const auto size = g.rects[child].size();
            if (columns && (columns == 4 || x + size.width() > budget)) {
                y += rowHeight + 10; x = 12; rowHeight = 0; columns = 0;
            }
            offsets[child] = {x, y}; x += size.width() + 10; ++columns;
            rowHeight = qMax(rowHeight, size.height()); right = qMax(right, x + 2);
        }
        g.rects[i] = QRectF(0, 0, right, y + rowHeight + (g.children[i].isEmpty() ? 0 : 12));
    }
    for (int i = 0; i < count; ++i) {
        if (cancel->load()) return {};
        const int parent = positions.value(report->nodes[i].parentNodeId, -1);
        g.rects[i].moveTopLeft(parent < 0 ? QPointF() : g.rects[parent].topLeft() + offsets[i]);
    }
    return g;
}
class Canvas final : public QWidget {
public:
    explicit Canvas(QWidget* parent) : QWidget(parent) { setMouseTracking(true); setFocusPolicy(Qt::StrongFocus); }
    std::function<void(QPainter&)> paint;
    std::function<void(QMouseEvent*, int)> mouse;
    std::function<void(QWheelEvent*)> wheel;
    void paintEvent(QPaintEvent*) override { QPainter p(this); if (paint) paint(p); }
    void mousePressEvent(QMouseEvent* e) override { if(mouse) mouse(e,0); }
    void mouseMoveEvent(QMouseEvent* e) override { if(mouse) mouse(e,1); }
    void mouseReleaseEvent(QMouseEvent* e) override { if(mouse) mouse(e,2); }
    void mouseDoubleClickEvent(QMouseEvent* e) override { if(mouse) mouse(e,3); }
    void wheelEvent(QWheelEvent* e) override { if(wheel) wheel(e); }
};
}

struct ModuleBlockDiagramView::Impl {
    ModuleBlockDiagramView* owner;
    Canvas* canvas;
    QLabel* path;
    QToolButton *previous, *next, *parent;
    Report report, requestedReport;
    Geometry geometry;
    QString workspace, key, focus, selected, lastActivated;
    QStringList history;
    int historyIndex = -1;
    qreal scale = 1;
    QPointF pan{16,16}, press, panAtPress;
    bool dragging = false, sourceEnabled = true, needsFit = true, doubleClickRelease = false;
    quint64 generation = 0;
    QFutureWatcher<Geometry>* watcher = nullptr;
    std::shared_ptr<std::atomic_bool> cancellation;
    QTimer *resizeTimer, *sourceTimer;
    QString pendingSourceId;
    Report pendingSourceReport;
    QHash<QString,QVariantMap> states;
    QVariantMap pendingState;
    std::function<void(const ModuleBlockDiagramNode&,bool)> navigate;

    int focused() const { return geometry.byId.value(focus, report && !report->nodes.isEmpty() ? 0 : -1); }
    QTransform transform() const { QTransform t; t.translate(pan.x(),pan.y()); t.scale(scale,scale); return t; }
    void controls() {
        previous->setEnabled(historyIndex > 0); next->setEnabled(historyIndex + 1 < history.size());
        const int i = focused(); parent->setEnabled(i >= 0 && report->nodes[i].parentNodeId >= 0);
        const auto text = i < 0 ? QString() : report->nodes[i].instancePath;
        path->setText(path->fontMetrics().elidedText(text,Qt::ElideMiddle,qMax(20,path->width()))); path->setToolTip(text);
        canvas->update();
    }
    void requestLayout() {
        ++generation;
        if (cancellation) cancellation->store(true);
        if (!requestedReport || watcher) return;
        launch();
    }
    void launch() {
        const auto current = requestedReport; const auto request = generation;
        const auto cancel = std::make_shared<std::atomic_bool>(false); cancellation = cancel;
        auto* job = new QFutureWatcher<Geometry>(owner); watcher = job;
        QObject::connect(job,&QFutureWatcher<Geometry>::finished,owner,[this,job,current,request,cancel] {
            auto result = job->result(); watcher = nullptr; job->deleteLater();
            if (request == generation && !cancel->load() && current == requestedReport) {
                report = current;
                geometry = std::move(result);
                if (!geometry.byId.contains(focus)) focus = report->root.stableId;
                if (!pendingState.isEmpty()) { const auto saved=pendingState; pendingState.clear(); owner->restoreViewState(saved); }
                if (needsFit) { needsFit=false; owner->fit(); }
                controls();
            } else if (requestedReport) launch();
        });
        const auto font=canvas->font(); const qreal width=qMax(200,canvas->width()-32);
        job->setFuture(QtConcurrent::run([current,font,width,cancel] { return arrange(current,font,width,cancel); }));
    }
    int hit(const QPointF& point) const {
        if (!report || geometry.rects.size()!=report->nodes.size()) return -1;
        const auto local=transform().inverted().map(point);
        const int root=focused(); if(root<0)return -1;
        int result=-1; QList<int> pending{root};
        while(!pending.isEmpty()) {
            const int i=pending.takeLast(); if(!geometry.rects[i].contains(local))continue;
            result=i; for(int child:geometry.children[i])pending.append(child);
        }
        return result;
    }
    void activate(int i, bool instance=false) {
        if(i<0 || !report)return;
        selected=report->nodes[i].stableId; lastActivated=selected; canvas->update();
        if(sourceEnabled && !watcher && navigate) navigate(report->nodes[i],instance);
    }
};

ModuleBlockDiagramView::ModuleBlockDiagramView(QWidget* parent) : QWidget(parent),d(std::make_unique<Impl>())
{
    d->owner=this; setObjectName("moduleBlockDiagramView");
    auto* root=new QVBoxLayout(this); root->setContentsMargins(0,0,0,0); root->setSpacing(4);
    auto* bar=new QHBoxLayout; bar->setContentsMargins(0,0,0,0); bar->setSpacing(3);
    const auto button=[&](const QString& text,const char* name,auto callback) {
        auto* b=UiControls::toolButton(this); b->setText(text); b->setToolTip(text); b->setAccessibleName(text);
        b->setAutoRaise(true); b->setMinimumSize(30,30); b->setSizePolicy(QSizePolicy::Fixed,QSizePolicy::Fixed);
        b->setIconSize({18,18}); b->setObjectName(name);
        bar->addWidget(b); connect(b,&QToolButton::clicked,this,callback); return b;
    };
    d->previous=button(tr("Back"),"moduleBlockBack",[this]{back();});
    d->next=button(tr("Forward"),"moduleBlockForward",[this]{forward();});
    d->parent=button(tr("Up"),"moduleBlockUp",[this]{up();});
    d->previous->setIcon(RoundedIcons::icon(RoundedIcons::Left));
    d->next->setIcon(RoundedIcons::icon(RoundedIcons::Right));
    d->parent->setIcon(RoundedIcons::icon(RoundedIcons::Up));
    d->path=new QLabel(this); d->path->setMinimumWidth(0); d->path->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred); bar->addWidget(d->path,1);
    button(tr("Fit"),"moduleBlockFit",[this]{fit();});
    button(QStringLiteral("−"),"moduleBlockZoomOut",[this]{zoom(1/1.2);});
    button(QStringLiteral("+"),"moduleBlockZoomIn",[this]{zoom(1.2);});
    root->addLayout(bar);
    d->canvas=new Canvas(this); d->canvas->setObjectName("moduleBlockCanvas"); root->addWidget(d->canvas,1);
    d->sourceTimer=new QTimer(this);d->sourceTimer->setSingleShot(true);
    connect(d->sourceTimer,&QTimer::timeout,this,[this] {
        const auto id=d->pendingSourceId;const auto report=d->pendingSourceReport;
        d->pendingSourceId.clear();d->pendingSourceReport.reset();
        if(report==d->report)d->activate(d->geometry.byId.value(id,-1));
    });
    d->resizeTimer=new QTimer(this); d->resizeTimer->setSingleShot(true); d->resizeTimer->setInterval(120);
    connect(d->resizeTimer,&QTimer::timeout,this,[this]{d->requestLayout();});
    d->canvas->paint=[this](QPainter& p) {
        p.fillRect(d->canvas->rect(),palette().base());
        if(!d->report || d->geometry.rects.size()!=d->report->nodes.size())return;
        p.setRenderHint(QPainter::Antialiasing); p.setTransform(d->transform());
        const auto visible=d->transform().inverted().mapRect(QRectF(d->canvas->rect()));
        const int root=d->focused(); if(root<0)return;
        QList<int> pending{root};
        while(!pending.isEmpty()) {
            const int i=pending.takeLast(); const auto rect=d->geometry.rects[i]; if(!visible.intersects(rect))continue;
            const auto& node=d->report->nodes[i];
            QPen pen(node.stableId==d->selected ? palette().highlight().color() : palette().mid().color());
            pen.setWidthF((node.stableId==d->selected?2.0:1.0)/d->scale); if(node.unresolved)pen.setStyle(Qt::DashLine);
            p.setPen(pen); p.setBrush(i==root ? palette().base() : palette().alternateBase()); p.drawRoundedRect(rect,5,5);
            if(rect.width()*d->scale<30 || rect.height()*d->scale<16)continue;
            p.save(); p.setClipRect(rect.adjusted(6,3,-6,-3));
            auto label=rect.adjusted(10,5,-10,0); label.setHeight(fontMetrics().height());
            p.setPen(palette().text().color()); p.drawText(label,Qt::AlignLeft|Qt::AlignVCenter,fontMetrics().elidedText(node.instanceDisplayName,Qt::ElideRight,int(label.width())));
            if(node.moduleTypeDisplayName!=node.instanceDisplayName) {
                label.translate(0,fontMetrics().height()); p.setPen(palette().color(QPalette::PlaceholderText));
                p.drawText(label,Qt::AlignLeft|Qt::AlignVCenter,fontMetrics().elidedText(node.moduleTypeDisplayName,Qt::ElideRight,int(label.width())));
            }
            p.restore(); for(auto it=d->geometry.children[i].crbegin();it!=d->geometry.children[i].crend();++it)pending.append(*it);
        }
    };
    d->canvas->mouse=[this](QMouseEvent* e,int phase) {
        if(phase==0 && (e->button()==Qt::LeftButton || e->button()==Qt::MiddleButton)) {
            d->press=e->position(); d->panAtPress=d->pan; d->dragging=false; d->doubleClickRelease=false;d->canvas->setFocus();
        } else if(phase==1) {
            if(e->buttons()&(Qt::LeftButton|Qt::MiddleButton)) {
                if((e->position()-d->press).manhattanLength()>4)d->dragging=true;
                if(d->dragging){d->pan=d->panAtPress+e->position()-d->press;d->canvas->update();}
            } else {
                const int i=d->hit(e->position());
                d->canvas->setToolTip(i<0?QString():d->report->nodes[i].instancePath+'\n'+d->report->nodes[i].moduleTypeDisplayName+'\n'+d->report->nodes[i].status+'\n'+d->report->nodes[i].unresolvedReason);
            }
        } else if(phase==2 && !d->dragging) {
            if(d->doubleClickRelease){d->doubleClickRelease=false;return;}
            const int i=d->hit(e->position());
            if(e->button()==Qt::LeftButton && i>=0) {
                d->selected=d->report->nodes[i].stableId;d->canvas->update();
                // Keep the canvas focused until a possible native double-click
                // has been classified; opening source on the first release steals it.
                d->pendingSourceId=d->selected;d->pendingSourceReport=d->report;
                d->sourceTimer->start(QApplication::doubleClickInterval());
            }
            else if(e->button()==Qt::RightButton && i>=0) {
                QMenu menu; auto* definition=menu.addAction(tr("Open definition")); auto* instance=menu.addAction(tr("Go to instance"));
                definition->setEnabled(d->sourceEnabled&&!d->report->nodes[i].definitionCodeLink.fileName.isEmpty());
                instance->setEnabled(d->sourceEnabled&&!d->report->nodes[i].instanceCodeLink.fileName.isEmpty());
                const auto* action=menu.exec(e->globalPosition().toPoint()); if(action==definition)d->activate(i);else if(action==instance)d->activate(i,true);
            }
        } else if(phase==3 && e->button()==Qt::LeftButton) {
            d->sourceTimer->stop();d->pendingSourceId.clear();d->pendingSourceReport.reset();d->doubleClickRelease=true;
            const int i=d->hit(e->position());
            if(i>=0){const auto id=d->report->nodes[i].stableId;enter(id);d->activate(i);}
        }
    };
    d->canvas->wheel=[this](QWheelEvent* e) {
        const auto point=d->transform().inverted().map(e->position());
        d->scale=qBound(0.0001,d->scale*std::pow(1.0015,e->angleDelta().y()),8.0);
        d->pan=e->position()-point*d->scale; d->canvas->update();e->accept();
    };
    d->controls();
}
ModuleBlockDiagramView::~ModuleBlockDiagramView() { if(d->cancellation)d->cancellation->store(true); }
void ModuleBlockDiagramView::setReport(const ModuleBlockDiagramReport& value,const QString& workspace)
{
    if(!value.found)return;
    d->sourceTimer->stop();d->pendingSourceId.clear();d->pendingSourceReport.reset();
    const auto key=workspace+'\n'+value.root.stableId;
    if(key!=d->key) {
        if(!d->key.isEmpty())d->states.insert(d->key,saveViewState());
        d->key=key;d->workspace=workspace;d->focus=value.initialFocusId;d->history={d->focus};d->historyIndex=0;
        d->selected.clear();d->lastActivated.clear();d->needsFit=true;
        if (d->pendingState.value("key").toString()!=key) d->pendingState=d->states.value(key);
    } else if(value.initialFocusId!=d->lastActivated && d->report && value.initialFocusId!=d->report->initialFocusId) {
        d->focus=value.initialFocusId;d->history=d->history.mid(0,d->historyIndex+1);d->history.append(d->focus);d->historyIndex=d->history.size()-1;d->needsFit=true;
    }
    // When an edited node disappears, recover to its closest retained ancestor.
    QSet<QString> retained;for(const auto& node:value.nodes)retained.insert(node.stableId);
    while(!retained.contains(d->focus) && d->report) {
        const int i=d->geometry.byId.value(d->focus,-1);if(i<0)break;
        const int parent=d->report->nodes[i].parentNodeId;if(parent<0)break;
        d->focus=d->report->nodes[parent].stableId;d->needsFit=true;
    }
    d->requestedReport=std::make_shared<ModuleBlockDiagramReport>(value);d->requestLayout();
}
void ModuleBlockDiagramView::setNavigationHandler(std::function<void(const ModuleBlockDiagramNode&,bool)> handler){d->navigate=std::move(handler);}
void ModuleBlockDiagramView::setSourceNavigationEnabled(bool enabled){d->sourceEnabled=enabled;}
void ModuleBlockDiagramView::fit(){const int i=d->focused();if(i<0||i>=d->geometry.rects.size())return;const auto rect=d->geometry.rects[i];d->scale=qBound(0.0001,qMin((d->canvas->width()-32)/rect.width(),(d->canvas->height()-32)/rect.height()),1.4);d->pan=QPointF(d->canvas->width()/2.0,d->canvas->height()/2.0)-rect.center()*d->scale;d->canvas->update();}
void ModuleBlockDiagramView::zoom(qreal factor){const QPointF center(d->canvas->width()/2.0,d->canvas->height()/2.0);const auto point=d->transform().inverted().map(center);d->scale=qBound(0.0001,d->scale*factor,8.0);d->pan=center-point*d->scale;d->canvas->update();}
bool ModuleBlockDiagramView::enter(const QString& id){if(!d->geometry.byId.contains(id))return false;if(d->focus!=id){d->focus=id;d->history=d->history.mid(0,d->historyIndex+1);d->history.append(id);d->historyIndex=d->history.size()-1;}fit();d->controls();return true;}
bool ModuleBlockDiagramView::up(){const int i=d->focused();if(i<0)return false;const int p=d->report->nodes[i].parentNodeId;return p>=0&&enter(d->report->nodes[p].stableId);}
bool ModuleBlockDiagramView::back(){while(d->historyIndex>0){const auto id=d->history[--d->historyIndex];if(d->geometry.byId.contains(id)){d->focus=id;fit();d->controls();return true;}}d->controls();return false;}
bool ModuleBlockDiagramView::forward(){while(d->historyIndex+1<d->history.size()){const auto id=d->history[++d->historyIndex];if(d->geometry.byId.contains(id)){d->focus=id;fit();d->controls();return true;}}d->controls();return false;}
QString ModuleBlockDiagramView::focusId()const{return d->focus;}
bool ModuleBlockDiagramView::layoutReady()const{return d->report&&!d->watcher&&!d->geometry.rects.isEmpty();}
QRectF ModuleBlockDiagramView::nodeRect(const QString& id)const{const int i=d->geometry.byId.value(id,-1);return i<0?QRectF():d->transform().mapRect(d->geometry.rects[i]).translated(d->canvas->pos());}
QVariantMap ModuleBlockDiagramView::saveViewState()const{return {{"key",d->key},{"focus",d->focus},{"selected",d->selected},{"history",d->history},{"historyIndex",d->historyIndex},{"zoom",d->scale},{"pan",d->pan}};}
void ModuleBlockDiagramView::restoreViewState(const QVariantMap& state){if(state.isEmpty())return;if(!layoutReady()){d->pendingState=state;return;}if(state.value("key").toString()!=d->key)return;const auto id=state.value("focus").toString();if(!d->geometry.byId.contains(id))return;d->focus=id;d->selected=state.value("selected").toString();d->history=state.value("history").toStringList();d->historyIndex=qBound(0,state.value("historyIndex").toInt(),qMax(0,int(d->history.size())-1));d->scale=qBound(0.0001,state.value("zoom",1).toDouble(),8.0);d->pan=state.value("pan").toPointF();d->needsFit=false;d->controls();}
void ModuleBlockDiagramView::resizeEvent(QResizeEvent* event){QWidget::resizeEvent(event);d->resizeTimer->start();d->controls();}
void ModuleBlockDiagramView::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type()==QEvent::FontChange) { d->needsFit=true; d->resizeTimer->start(); }
    if (event->type()==QEvent::PaletteChange) d->canvas->update();
}
