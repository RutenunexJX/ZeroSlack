#pragma once
#include <QApplication>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QProxyStyle>
#include <QStyleOption>
#include <cmath>

namespace RoundedIcons {
enum Kind { Folder, File, Settings, Search, Replace, Filter, Refresh, Context,
    Module, Hierarchy, Signals, Wave, Connections, Bookmark, Pin, Warning,
    Activity, Error, Info, Success, Change, Shelf, Left, Right, Down, Up,
    Expand, Collapse, Close, Minimize, Restore, Maximize, Grid };

class Engine final : public QIconEngine {
public:
    explicit Engine(Kind value) : kind(value) {}
    QIconEngine* clone() const override { return new Engine(kind); }
    void paint(QPainter* p, const QRect& rect, QIcon::Mode mode, QIcon::State state) override {
        p->save();
        const qreal side = qMin(rect.width(), rect.height());
        p->translate(rect.center().x() - side / 2, rect.center().y() - side / 2);
        p->scale(side / 24, side / 24);
        p->setRenderHint(QPainter::Antialiasing);
        const auto palette = QApplication::palette();
        const QColor color = mode == QIcon::Disabled
            ? palette.color(QPalette::Disabled, QPalette::Text)
            : palette.color(QPalette::Text);
        p->setPen(QPen(color, state == QIcon::On ? 2.1 : 1.75, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p->setBrush(Qt::NoBrush);
        auto line = [p](qreal x, qreal y, qreal a, qreal b) { p->drawLine(QPointF(x,y), QPointF(a,b)); };
        auto box = [p](qreal x, qreal y, qreal w, qreal h) { p->drawRoundedRect(QRectF(x,y,w,h), 2, 2); };
        auto circle = [p](qreal x, qreal y, qreal r) { p->drawEllipse(QPointF(x,y), r, r); };
        auto path = [p](std::initializer_list<QPointF> points, bool closed = false) {
            QPainterPath shape; bool first = true;
            for (auto point : points) { if (first) shape.moveTo(point); else shape.lineTo(point); first = false; }
            if (closed) shape.closeSubpath();
            p->drawPath(shape);
        };
        switch (kind) {
        case Folder: path({{3,7},{3,5},{9,5},{11,7},{21,7},{21,20},{3,20}},true); break;
        case File: path({{6,3},{14,3},{19,8},{19,21},{6,21}},true); path({{14,3},{14,8},{19,8}}); line(9,12,16,12); line(9,16,16,16); break;
        case Settings: {
            QPolygonF gear;
            for (int i=0;i<32;++i) { qreal a=i*6.28318530718/32; qreal r=(i%4==0 || i%4==3)?9:7.3; gear<<QPointF(12+std::cos(a)*r,12+std::sin(a)*r); }
            p->drawPolygon(gear); circle(12,12,3); break;
        }
        case Search: circle(10,10,7); line(15,15,21,21); break;
        case Replace: path({{4,7},{20,7},{16,3}}); path({{20,17},{4,17},{8,21}}); break;
        case Filter: path({{3,4},{21,4},{14,12},{14,20},{10,18},{10,12}},true); break;
        case Refresh: p->drawArc(QRectF(4,4,16,16),45*16,290*16); path({{20,3},{20,8},{15,8}}); break;
        case Context: line(7,10,16,6); line(7,14,16,18); circle(5,12,3); circle(19,5,3); circle(19,19,3); break;
        case Module: box(6,6,12,12); for(int i=8;i<=16;i+=4){line(i,3,i,6);line(i,18,i,21);line(3,i,6,i);line(18,i,21,i);} break;
        case Hierarchy: box(9,2,6,5); box(2,17,6,5); box(16,17,6,5); line(12,7,12,12); path({{5,17},{5,12},{19,12},{19,17}}); break;
        case Signals: for(int i=5;i<=19;i+=7){int y=i==12?15:8;line(i,3,i,y-2);line(i,y+2,i,21);circle(i,y,2);} break;
        case Wave: path({{2,17},{6,17},{6,6},{12,6},{12,17},{18,17},{18,6},{22,6}}); break;
        case Connections: path({{6,9},{18,9},{18,14},{15,18},{9,18},{6,14}},true); line(9,3,9,9);line(15,3,15,9);line(12,18,12,22);break;
        case Bookmark: path({{6,3},{18,3},{18,21},{12,17},{6,21}},true); break;
        case Pin: path({{9,3},{19,3},{17,8},{20,13},{8,13},{11,8}},true);line(14,13,8,21);break;
        case Warning: path({{12,3},{22,21},{2,21}},true);line(12,9,12,14);circle(12,18,.35);break;
        case Activity: path({{2,13},{7,13},{10,4},{14,21},{17,12},{22,12}});break;
        case Error: circle(12,12,9);line(8,8,16,16);line(16,8,8,16);break;
        case Info: circle(12,12,9);line(12,11,12,17);circle(12,7,.4);break;
        case Success: circle(12,12,9);path({{7,12},{10,15},{17,8}});break;
        case Change: box(2,4,8,16);box(14,4,8,16);path({{16,12},{20,12},{18,10}});break;
        case Shelf: path({{3,13},{7,5},{17,5},{21,13},{21,20},{3,20}},true);line(3,13,21,13);break;
        case Left: path({{15,5},{8,12},{15,19}});break;
        case Right: path({{9,5},{16,12},{9,19}});break;
        case Down: path({{5,9},{12,16},{19,9}});break;
        case Up: path({{5,15},{12,8},{19,15}});break;
        case Expand: path({{3,9},{3,3},{9,3}});path({{15,3},{21,3},{21,9}});path({{21,15},{21,21},{15,21}});path({{9,21},{3,21},{3,15}});break;
        case Collapse: path({{3,9},{9,9},{9,3}});path({{15,3},{15,9},{21,9}});path({{21,15},{15,15},{15,21}});path({{9,21},{9,15},{3,15}});break;
        case Close: line(6,6,18,18);line(18,6,6,18);break;
        case Minimize: line(5,16,19,16);break;
        case Restore: box(3,8,13,13);box(8,3,13,13);break;
        case Maximize: box(4,4,16,16);break;
        case Grid: for(int x:{3,14})for(int y:{3,14})box(x,y,7,7);break;
        }
        p->restore();
    }
    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
        QPixmap result(size); result.fill(Qt::transparent); QPainter painter(&result);
        paint(&painter,QRect(QPoint(),size),mode,state); return result;
    }
    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state, qreal scale) override {
        QPixmap result=pixmap(size*scale,mode,state); result.setDevicePixelRatio(scale); return result;
    }
private:
    Kind kind;
};
inline QIcon icon(Kind kind) { return QIcon(new Engine(kind)); }

// Standard controls and file views share the same scalable icon language.
class Style final : public QProxyStyle {
public:
    Style() : QProxyStyle(QStringLiteral("Fusion")) {}
    void drawPrimitive(PrimitiveElement element, const QStyleOption* option,
                       QPainter* painter, const QWidget* widget=nullptr) const override {
        if (element == PE_IndicatorTabClose && option) {
            const QRect area = option->rect;
            if (option->state & State_MouseOver) {
                painter->save(); painter->setPen(Qt::NoPen);
                painter->setBrush(option->palette.color(QPalette::Mid));
                painter->drawRoundedRect(area, 4, 4); painter->restore();
            }
            icon(Close).paint(painter, area, Qt::AlignCenter,
                option->state & State_Enabled ? QIcon::Normal : QIcon::Disabled);
            return;
        }
        QProxyStyle::drawPrimitive(element,option,painter,widget);
    }
    QIcon standardIcon(StandardPixmap id, const QStyleOption* option=nullptr, const QWidget* widget=nullptr) const override {
        switch(id) {
        case SP_DirIcon: case SP_DirLinkIcon: case SP_DirOpenIcon: case SP_DirClosedIcon: return icon(Folder);
        case SP_FileIcon: return icon(File);
        case SP_TitleBarMinButton: return icon(Minimize);
        case SP_TitleBarMaxButton: return icon(Maximize);
        case SP_TitleBarNormalButton: return icon(Restore);
        case SP_TitleBarCloseButton: case SP_DockWidgetCloseButton: case SP_DialogCloseButton: return icon(Close);
        case SP_ArrowLeft: case SP_ArrowBack: return icon(Left);
        case SP_ArrowRight: case SP_ArrowForward: return icon(Right);
        case SP_ArrowUp: return icon(Up);
        case SP_ArrowDown: return icon(Down);
        case SP_BrowserReload: return icon(Refresh);
        case SP_MessageBoxWarning: return icon(Warning);
        case SP_MessageBoxCritical: return icon(Error);
        case SP_MessageBoxInformation: case SP_MessageBoxQuestion: return icon(Info);
        case SP_DialogApplyButton: case SP_DialogOkButton: return icon(Success);
        case SP_DialogCancelButton: return icon(Close);
        default: return QProxyStyle::standardIcon(id,option,widget);
        }
    }
};
}
