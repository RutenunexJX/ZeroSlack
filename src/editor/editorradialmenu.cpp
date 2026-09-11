#include "editorradialmenu.h"

#include "insightvisualstyle.h"
#include <QAction>
#include <QEventLoop>
#include <QFrame>
#include <QGuiApplication>
#include <QIconEngine>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QToolButton>
#include <QToolTip>
#include <cmath>
#include <algorithm>

namespace {
constexpr int radius = 84;
constexpr int hole = 28;
constexpr int cell = 28;
constexpr double pi = 3.141592653589793;
enum Glyph { Block, Hotspot, Kernel, State, Run, Observe, Reveal, Link, OpenLink,
    Markers, Organize, CreateSignal, Ports, Expose, Queue, Case, Spaces, Temporary,
    GraphGroup, WaveGroup, PinloomGroup, RefactorGroup, TextGroup, TemporaryGroup, Close };
struct Entry { const char* id; int group; Glyph glyph; };
constexpr Entry entries[] = {
    {"insight.moduleBlockDiagram", 0, Block},
    {"insight.signalUsageHotspot", 0, Hotspot},
    {"insight.signalKernelGraph", 0, Kernel},
    {"insight.stateTransitionGraph", 0, State},
    {"waveSimulation.runCurrentContext", 1, Run},
    {"waveSimulation.observeSignal", 1, Observe},
    {"waveSimulation.revealSignalInResult", 1, Reveal},
    {"pinloom.linkSelection", 2, Link},
    {"pinloom.openLinkedContent", 2, OpenLink},
    {"pinloom.toggleBindingMarkers", 2, Markers},
    {"refactor.organizeSignalDeclarations", 3, Organize},
    {"refactor.createSignalDefinition", 3, CreateSignal},
    {"refactor.editInstanceSlots", 3, Ports},
    {"refactor.exposeSignalToTop", 3, Expose},
    {"refactor.createAssignmentQueue", 3, Queue},
    {"edit.toggleSelectionCase", 4, Case},
    {"edit.replaceSelectionWithSpaces", 4, Spaces},
    {"view.temporaryEditor.open", 5, Temporary},
};
const Entry* entryFor(const QString& id) {
    for (const auto& entry : entries) if (id == QLatin1String(entry.id)) return &entry;
    return nullptr;
}
bool executable(QAction* action) {
    return action && action->isEnabled() && action->property("executable").toBool();
}
QColor accent() {
    return InsightVisualStyle::theme().panelBackground.lightness() > 128
        ? QColor("#4f63e6") : QColor("#a6b1ff");
}
class IconEngine final : public QIconEngine {
public:
    explicit IconEngine(Glyph glyph) : glyph(glyph) {}
    QIconEngine* clone() const override { return new IconEngine(glyph); }
    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
        QPixmap result(size);
        result.fill(Qt::transparent);
        QPainter painter(&result);
        paint(&painter,QRect(QPoint(),size),mode,state);
        return result;
    }
    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state, qreal scale) override {
        QPixmap result=pixmap(size*scale,mode,state);
        result.setDevicePixelRatio(scale);
        return result;
    }
    void paint(QPainter* p, const QRect& rect, QIcon::Mode mode, QIcon::State) override {
        p->save();
        const qreal size = qMin(rect.width(), rect.height());
        p->translate(rect.x() + (rect.width() - size) / 2, rect.y() + (rect.height() - size) / 2);
        p->scale(size / 24, size / 24);
        p->setRenderHint(QPainter::Antialiasing);
        p->setPen(QPen(mode == QIcon::Disabled || glyph == Close ? QColor("#a0a7b4") : accent(), 1.7,
                       Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p->setBrush(Qt::NoBrush);
        auto line = [p](qreal x, qreal y, qreal a, qreal b) { p->drawLine(QPointF(x,y), QPointF(a,b)); };
        auto box = [p](qreal x, qreal y, qreal w, qreal h) { p->drawRoundedRect(QRectF(x,y,w,h), 1.5, 1.5); };
        auto circle = [p](qreal x, qreal y, qreal r) { p->drawEllipse(QPointF(x,y), r,r); };
        auto path = [p](std::initializer_list<QPointF> points) { QPainterPath s; bool first=true;
            for (auto pt : points) { if (first) s.moveTo(pt); else s.lineTo(pt); first=false; } p->drawPath(s); };
        auto wave = [&] { path({{2,14},{5,14},{5,6},{10,6},{10,17},{14,17}}); };
        auto eye = [&] { path({{13,14},{17,10},{22,14},{17,18},{13,14}}); circle(17.5,14,1); };
        auto bookmark = [&] { path({{4,3},{13,3},{13,21},{8.5,17},{4,21},{4,3}}); };
        auto pencil = [&] { path({{12,19},{14,14},{20,8},{23,11},{17,17},{12,19}}); };
        switch (glyph) {
        case Block: box(2,3,20,18); box(6,7,12,10); line(6,12,18,12); break;
        case Hotspot: for(int x : {3,10,17}) for(int y : {3,10,17}) {
            if(x==10 && y==10) p->setBrush(p->pen().color()); else p->setBrush(Qt::NoBrush);
            box(x,y,4,4); } break;
        case Kernel: circle(12,12,3); for(int i=0;i<6;++i) { double a=i*pi/3;
            line(12+5*std::cos(a),12+5*std::sin(a),12+8*std::cos(a),12+8*std::sin(a));
            circle(12+9*std::cos(a),12+9*std::sin(a),1); } break;
        case State: circle(12,4,2.5); circle(4,18,2.5); circle(20,18,2.5);
            path({{8,7},{4,12},{3,10}}); path({{8,19},{15,19},{13,17}}); path({{20,13},{16,7},{19,8}}); break;
        case Run: wave(); path({{17,5},{23,11},{17,17},{17,5}}); break;
        case Observe: wave(); eye(); break;
        case Reveal: wave(); circle(18,10,4); line(18,3,18,7); line(18,13,18,17); line(11,10,15,10); line(21,10,24,10); break;
        case Link: bookmark(); path({{16,10},{19,7},{22,7},{23,10},{20,13}}); path({{21,13},{18,16},{15,16},{14,13},{17,10}}); break;
        case OpenLink: bookmark(); path({{16,4},{22,4},{22,10}}); line(15,11,22,4); break;
        case Markers: bookmark(); eye(); break;
        case Organize: line(3,5,14,5);line(3,10,12,10);line(3,15,10,15);line(3,20,14,20);path({{17,7},{20,4},{23,7}});path({{17,17},{20,20},{23,17}});line(20,4,20,20);break;
        case CreateSignal: wave();line(19,4,19,12);line(15,8,23,8);break;
        case Ports: box(4,4,12,12);for(int i : {6,10,14}) {line(i,1,i,4);line(1,i,4,i);} pencil();break;
        case Expose: box(2,17,5,5);box(17,17,5,5);path({{4.5,17},{4.5,13},{19.5,13},{19.5,17}});path({{8,7},{12,3},{16,7}});line(12,3,12,13);break;
        case Queue: for(int y : {4,11,18}) {box(2,y,6,4);path({{11,y+2.0},{21,y+2.0},{19,y+0.0}});}break;
        case Case: case TextGroup: path({{2,18},{7,4},{12,18}});line(4,13,10,13);circle(18,14,4);line(22,10,22,18);break;
        case Spaces: box(2,7,5,8);box(17,7,5,8);path({{9,11},{15,11},{13,9}});break;
        case Temporary: case TemporaryGroup: box(2,3,17,17);line(2,7,19,7);pencil();break;
        case GraphGroup: circle(12,4,3);circle(4,19,3);circle(20,19,3);line(10,7,5,16);line(14,7,19,16);break;
        case WaveGroup: path({{2,17},{6,17},{6,5},{12,5},{12,17},{18,17},{18,5},{22,5}});break;
        case PinloomGroup: bookmark();line(16,8,22,8);line(16,12,20,12);break;
        case RefactorGroup: path({{13,4},{13,9},{18,9},{21,6},{21,12},{17,16},{13,16},{6,23},{2,19},{9,12},{9,8},{13,4}});break;
        case Close: line(7,7,17,17);line(17,7,7,17);break;
        }
        p->restore();
    }
private: Glyph glyph;
};
QIcon icon(Glyph glyph) { return QIcon(new IconEngine(glyph)); }
}

QIcon EditorRadialMenu::actionIcon(const QString& actionId) {
    const auto* entry = entryFor(actionId);
    return entry ? icon(entry->glyph) : QIcon();
}

EditorRadialMenu::EditorRadialMenu(QMenu* commands)
    : QWidget(nullptr, Qt::Popup | Qt::FramelessWindowHint) {
    setObjectName(QStringLiteral("editorRadialMenu"));
    setAccessibleName(tr("Editor actions"));
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_AlwaysShowToolTips);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    // Reserve the union of all possible bars so moving across the gap keeps the popup open.
    resize(392, 284);
    center = QPoint(width()/2, height()/2);
    collect(commands);
    const QStringList titles {tr("Graphs"),tr("Wave simulation"),tr("Pinloom"),tr("Refactor"),tr("Text"),tr("Temporary editor")};
    const Glyph glyphs[] {GraphGroup,WaveGroup,PinloomGroup,RefactorGroup,TextGroup,TemporaryGroup};
    for (int i=0;i<6;++i) {
        auto* button = new QToolButton(this);
        groupButtons[i]=button;
        button->setObjectName(QStringLiteral("radialGroup.%1").arg(i));
        button->setAccessibleName(titles[i]);
        button->setToolTip(titles[i]);
        bool available=false;
        for (auto action : groups[i]) available |= executable(action);
        button->setIcon(icon(glyphs[i]));
        // Keep categories inspectable even when every contained action is disabled.
        if (!available) {
            QIcon gray; gray.addPixmap(icon(glyphs[i]).pixmap(QSize(32,32),QIcon::Disabled));
            button->setIcon(gray);
        }
        button->setIconSize(QSize(16,16));
        const double angle=(-60+i*60)*pi/180;
        const QPoint at=center+QPoint(qRound(56*std::cos(angle)),qRound(56*std::sin(angle)));
        button->setGeometry(QRect(at-QPoint(14,14),QSize(cell,cell)));
        button->installEventFilter(this);
        connect(button,&QToolButton::clicked,this,[this,i] {selectGroup(i,true);});
    }
    closeButton=new QToolButton(this);
    closeButton->setObjectName(QStringLiteral("radialClose"));
    closeButton->setAccessibleName(tr("Close menu"));
    closeButton->setToolTip(tr("Close (Esc)"));
    closeButton->setIcon(icon(Close));
    closeButton->setIconSize(QSize(14,14));
    closeButton->setGeometry(QRect(center-QPoint(14,14),QSize(cell,cell)));
    closeButton->installEventFilter(this);
    connect(closeButton,&QToolButton::clicked,this,&QWidget::close);
    const auto& theme=InsightVisualStyle::theme();
    setStyleSheet(QStringLiteral(
        "QWidget#editorRadialMenu QToolButton {border:0; background:transparent; padding:0; border-radius:6px;}"
        "QWidget#editorRadialMenu QToolButton:hover:enabled, QWidget#editorRadialMenu QToolButton:focus {background:%1;}"
        "QFrame#radialActionBar {background:%2; border:1px solid %3; border-radius:9px;}")
        .arg(theme.itemView.selectedBackground.name(),theme.panelBackground.name(),theme.border.name()));
}

void EditorRadialMenu::collect(QMenu* menu) {
    if (!menu) return;
    for (auto* action : menu->actions()) {
        if (action->menu()) {collect(action->menu());continue;}
        if (!action->isVisible()) continue;
        const auto* entry=entryFor(action->property("actionId").toString());
        if (entry) groups[entry->group].append(action);
    }
    for (auto& group : groups) std::sort(group.begin(),group.end(),[](auto a,auto b) {
        return entryFor(a->property("actionId").toString()) < entryFor(b->property("actionId").toString());
    });
}

QRect EditorRadialMenu::barGeometry(int group,int count) const {
    const int columns=count<=4 ? qMax(1,count) : 3;
    const QSize size(columns*cell+12, ((count+columns-1)/columns)*cell+12);
    // Rectangular bars align to the selected category without wrapping around the ring.
    const int x=(group<3) ? center.x()+radius+6 : center.x()-radius-6-size.width();
    const int y=group==0 || group==5 ? center.y()-radius-size.height()/2
        : group==2 || group==3 ? center.y()+radius-size.height()/2 : center.y()-size.height()/2;
    return QRect(QPoint(qBound(2,x,width()-size.width()-2),qBound(2,y,height()-size.height()-2)),size);
}

void EditorRadialMenu::selectGroup(int index,bool focusFirst) {
    if (index<0 || index>=6) return;
    if (index!=currentGroup) {
        currentGroup=index;
        delete bar;
        bar=nullptr;
        if (!groups[index].isEmpty()) {
            bar=new QFrame(this);
            bar->setObjectName(QStringLiteral("radialActionBar"));
            bar->setGeometry(barGeometry(index,groups[index].size()));
            const int columns=groups[index].size()<=4 ? groups[index].size() : 3;
            for(int n=0;n<groups[index].size();++n) {
                QPointer<QAction> action=groups[index][n];
                if (!action) continue;
                auto* button=new QToolButton(bar);
                const QString id=action->property("actionId").toString();
                button->setObjectName(id);
                button->setProperty("actionId", id);
                button->setProperty("executionRoute", action->property("executionRoute"));
                button->setAccessibleName(action->text());
                button->setToolTip(action->text());
                button->setIcon(actionIcon(id));
                button->setIconSize(QSize(16,16));
                button->setGeometry(6+(n%columns)*cell,6+(n/columns)*cell,cell,cell);
                button->setEnabled(executable(action));
                button->installEventFilter(this);
                connect(button,&QToolButton::clicked,this,[this,action] {
                    if (!executable(action)) return;
                    chosen=action;
                    close();
                });
            }
            bar->show();
        }
        update();
    }
    if (focusFirst && bar) for(auto* button : bar->findChildren<QToolButton*>()) {
        if (button->isEnabled()) {button->setFocus();break;}
    }
}

int EditorRadialMenu::groupAt(const QPoint& point) const {
    const QPoint d=point-center;
    const double r=std::hypot(d.x(),d.y());
    if(r<hole || r>radius) return -1;
    double angle=std::atan2(d.y(),d.x())*180/pi+90;
    if(angle<0)angle+=360;
    return int(angle/60)%6;
}
void EditorRadialMenu::paintEvent(QPaintEvent*) {
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
    const auto& t=InsightVisualStyle::theme();
    const QRectF outer(center.x()-radius,center.y()-radius,2*radius,2*radius);
    const QRectF inner(center.x()-hole,center.y()-hole,2*hole,2*hole);
    p.setPen(QPen(t.border,1));p.setBrush(t.panelBackground);p.drawEllipse(outer);
    if(currentGroup>=0) {
        QPainterPath wedge;wedge.moveTo(center);wedge.arcTo(outer,90-currentGroup*60,-60);wedge.closeSubpath();
        p.setPen(Qt::NoPen);p.setBrush(t.itemView.selectedBackground);p.drawPath(wedge);
    }
    p.setPen(QPen(t.border,1));
    for(int i=0;i<6;++i) {const double a=(-90+i*60)*pi/180;
        p.drawLine(QPointF(center)+QPointF(hole*std::cos(a),hole*std::sin(a)),
                   QPointF(center)+QPointF(radius*std::cos(a),radius*std::sin(a)));}
    p.setBrush(t.panelBackground);p.drawEllipse(inner);
}
void EditorRadialMenu::mouseMoveEvent(QMouseEvent* e) {const int i=groupAt(e->position().toPoint());if(i>=0)selectGroup(i);}
void EditorRadialMenu::mousePressEvent(QMouseEvent* e) {
    const int i=groupAt(e->position().toPoint());
    if(e->button()==Qt::LeftButton && i>=0) selectGroup(i,true); else close();
}
void EditorRadialMenu::keyPressEvent(QKeyEvent* e) {
    if(e->key()==Qt::Key_Escape) {close();return;}
    if(e->key()==Qt::Key_Left || e->key()==Qt::Key_Right) {
        const int next=(qMax(0,currentGroup)+(e->key()==Qt::Key_Right?1:5))%6;
        selectGroup(next);groupButtons[next]->setFocus();return;
    }
    QWidget::keyPressEvent(e);
}
bool EditorRadialMenu::eventFilter(QObject* watched,QEvent* e) {
    if(e->type()==QEvent::KeyPress) {
        auto* key=static_cast<QKeyEvent*>(e);
        if(key->key()==Qt::Key_Escape) {close();return true;}
        if(key->key()==Qt::Key_Return || key->key()==Qt::Key_Enter) {
            if(auto* button=qobject_cast<QToolButton*>(watched)) {button->click();return true;}
        }
    }
    if(e->type()==QEvent::Enter || e->type()==QEvent::FocusIn) {
        for(int i=0;i<6;++i) if(watched==groupButtons[i]) {selectGroup(i);break;}
    }
    return QWidget::eventFilter(watched,e);
}
void EditorRadialMenu::popupAt(const QPoint& position) {
    QScreen* screen=QGuiApplication::screenAt(position);
    if(!screen)screen=QGuiApplication::primaryScreen();
    const QRect bounds=screen->availableGeometry();
    const QPoint origin=position-center;
    move(qBound(bounds.left(),origin.x(),qMax(bounds.left(),bounds.right()-width()+1)),
         qBound(bounds.top(),origin.y(),qMax(bounds.top(),bounds.bottom()-height()+1)));
    setFocus(Qt::PopupFocusReason);show();
}
void EditorRadialMenu::exec(QMenu* commands,const QPoint& globalPosition) {
    if(!commands) return;
    QPointer<QAction> selected;
    {
        EditorRadialMenu popup(commands);
        QEventLoop loop;
        QObject::connect(&popup,&QWidget::destroyed,&loop,&QEventLoop::quit);
        // A close or an outside click hides a Qt::Popup and ends this local loop.
        struct HideFilter : QObject {
            QEventLoop* loop;
            bool eventFilter(QObject*,QEvent* e) override {if(e->type()==QEvent::Hide)loop->quit();return false;}
        } filter;
        filter.loop=&loop;popup.installEventFilter(&filter);
        popup.popupAt(globalPosition);
        loop.exec();
        selected=popup.chosen;
        QToolTip::hideText();
    }
    if(executable(selected))selected->trigger();
}
