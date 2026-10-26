#pragma once
#include "insightcontrolstyle.h"
#include <QApplication>
#include <QCheckBox>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QProxyStyle>
#include <QStyleOption>
#include <QToolButton>
#include <cmath>

namespace RoundedIcons {
// New icon paths use this 24-unit grid and the [3,21] content box. Align the
// leading outline at x=3; keep its natural width in [13,18] without stretching.
// Use the shared round stroke and scale uniformly with the outer icon rect.
// Folder/FolderOpen/File follow this convention; other legacy kinds are not
// redrawn here and must be audited when they are next changed.
inline constexpr qreal GridSize = 24;
inline constexpr qreal ContentLeading = 3;
inline constexpr qreal ContentTrailing = 21;
inline constexpr qreal MinimumContentWidth = 13;
inline constexpr qreal NormalStroke = 1.75;
inline constexpr qreal SelectedStroke = 2.1;
enum Kind { Folder, File, Settings, Search, Replace, Filter, Refresh, Context,
    Module, Hierarchy, Signals, Wave, Connections, Bookmark, Pin, Warning,
    Activity, Error, Info, Success, Change, Left, Right, Down, Up,
    Expand, Collapse, Close, Minimize, Restore, Maximize, Grid, Sidebar,
    OpenProject, ZoomIn, ZoomOut, More,
    KernelGraph, ModuleGraph, HotspotGraph, StateGraph, IpCatalog, Clear, FolderOpen,
    BuildInputs, Simulation, Subroutine, Parameter, Port, Type, Rename, CopyLink,
    Add = ZoomIn }; // Reuse the existing pure plus glyph with its action meaning.

class Engine final : public QIconEngine {
public:
    explicit Engine(Kind value) : kind(value) {}
    QIconEngine* clone() const override { return new Engine(kind); }
    void paint(QPainter* p, const QRect& rect, QIcon::Mode mode, QIcon::State state) override {
        p->save();
        const qreal side = qMin(rect.width(), rect.height());
        p->translate(rect.center().x() - side / 2, rect.center().y() - side / 2);
        p->scale(side / GridSize, side / GridSize);
        p->setRenderHint(QPainter::Antialiasing);
        const auto palette = QApplication::palette();
        const QColor color = mode == QIcon::Disabled
            ? palette.color(QPalette::Disabled, QPalette::Text)
            : palette.color(QPalette::Text);
        p->setPen(QPen(color, state == QIcon::On ? SelectedStroke : NormalStroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
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
        case Folder: {
            QPainterPath folder; folder.moveTo(ContentLeading,8); folder.lineTo(ContentLeading,6);
            folder.quadTo(ContentLeading,4.5,4.5,4.5); folder.lineTo(9,4.5); folder.lineTo(11.5,7);
            folder.lineTo(19.5,7); folder.quadTo(ContentTrailing,7,ContentTrailing,8.5); folder.lineTo(ContentTrailing,18.5);
            folder.quadTo(ContentTrailing,20,19.5,20); folder.lineTo(4.5,20); folder.quadTo(ContentLeading,20,ContentLeading,18.5);
            folder.closeSubpath(); p->drawPath(folder); break;
        }
        case FolderOpen: {
            path({{ContentLeading,17},{ContentLeading,6},{9,6},{11,8},{20,8},{20,10}});
            QPainterPath folder; folder.moveTo(ContentLeading,20); folder.lineTo(6,11); folder.lineTo(ContentTrailing,11);
            folder.lineTo(18,20); folder.closeSubpath(); p->drawPath(folder); break;
        }
        case File: {
            const qreal right = ContentLeading + MinimumContentWidth;
            const qreal fold = right - 5;
            path({{ContentLeading,3},{fold,3},{right,8},{right,21},{ContentLeading,21}},true);
            path({{fold,3},{fold,8},{right,8}});
            line(ContentLeading+3,12,right-3,12); line(ContentLeading+3,16,right-3,16); break;
        }
        case Settings: {
            QPolygonF gear;
            for (int i=0;i<32;++i) { qreal a=i*6.28318530718/32; qreal r=(i%4==0 || i%4==3)?9:7.3; gear<<QPointF(12+std::cos(a)*r,12+std::sin(a)*r); }
            p->drawPolygon(gear); circle(12,12,3); break;
        }
        case Search: circle(10,10,7); line(15,15,21,21); break;
        case ZoomIn: line(5,12,19,12); line(12,5,12,19); break;
        case ZoomOut: line(5,12,19,12); break;
        case More: circle(5,12,.65); circle(12,12,.65); circle(19,12,.65); break;
        case Clear: path({{3,14},{12,4},{21,12},{13,21},{9,21}},true);line(7,10,16,18);line(13,21,22,21);break;
        case Replace: path({{4,7},{20,7},{16,3}}); path({{20,17},{4,17},{8,21}}); break;
        case Filter: path({{3,4},{21,4},{14,12},{14,20},{10,18},{10,12}},true); break;
        case Refresh: p->drawArc(QRectF(4,4,16,16),45*16,290*16); path({{20,3},{20,8},{15,8}}); break;
        case Context: line(7,10,16,6); line(7,14,16,18); circle(5,12,3); circle(19,5,3); circle(19,19,3); break;
        case KernelGraph:
            path({{12,7},{17,10},{17,15},{12,18},{7,15},{7,10}},true);
            circle(12,12.5,1.6);
            line(12,7,12,3); line(7,11,3,8); line(17,11,21,8);
            line(8,16,5,21); line(16,16,19,21); break;
        case ModuleGraph:
            box(3,3,18,18); box(6,7,5,5); box(13,13,5,5);
            path({{11,9.5},{15.5,9.5},{15.5,13}}); break;
        case HotspotGraph: {
            QPainterPath flame;
            flame.moveTo(13,2);
            flame.cubicTo(14,8,19,8,20,13);
            flame.cubicTo(22,23,3,24,4,14);
            flame.cubicTo(4,10,8,8,8,5);
            flame.lineTo(10,10);
            flame.cubicTo(13,8,13,5,13,2);
            p->drawPath(flame);
            QPainterPath core;
            core.moveTo(12,13); core.cubicTo(9,16,9,20,12,21);
            core.cubicTo(15,20,15,17,12,13); p->drawPath(core); break;
        }
        case StateGraph:
            circle(5,12,3); circle(19,12,3);
            p->drawArc(QRectF(5,3,14,14),25*16,130*16);
            path({{16,3},{19,6},{15,7}});
            p->drawArc(QRectF(5,7,14,14),205*16,130*16);
            path({{8,21},{5,18},{9,17}}); break;
        case IpCatalog:
            box(6,6,12,12); line(10,10,14,14); line(14,10,10,14);
            for (int i : {8,12,16}) {
                line(i,3,i,6); line(i,18,i,21);
                line(3,i,6,i); line(18,i,21,i);
            } break;
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
        case Sidebar: box(3,4,18,16);line(9,4,9,20);break;
        case BuildInputs: {
            QPainterPath shape;
            shape.moveTo(7.5, 3);
            shape.lineTo(5, 3);
            shape.arcTo(3, 3, 4, 4, 90, 90);
            shape.lineTo(3, 17);
            shape.arcTo(3, 15, 4, 4, -180, 90);
            shape.moveTo(15, 5);
            shape.lineTo(9, 5);
            shape.arcTo(7, 5, 4, 4, 90, 90);
            shape.lineTo(7, 19);
            shape.arcTo(7, 17, 4, 4, -180, 90);
            shape.lineTo(12, 21);
            shape.moveTo(15, 5);
            shape.lineTo(19, 9);
            shape.lineTo(15, 9);
            shape.lineTo(15, 5);
            shape.moveTo(19, 9);
            shape.lineTo(19, 12);
            shape.moveTo(10, 10);
            shape.lineTo(12, 10);
            shape.moveTo(10, 14);
            shape.lineTo(13, 14);
            shape.moveTo(15, 18);
            shape.lineTo(17.2, 20.2);
            shape.lineTo(22, 15);
            p->drawPath(shape);
            break;
        }
        case Simulation: {
            circle(12, 13, 8.2);
            QPainterPath shape;
            shape.moveTo(9, 2);
            shape.lineTo(15, 2);
            shape.moveTo(12, 2);
            shape.lineTo(12, 4.8);
            shape.moveTo(18.6, 6.6);
            shape.lineTo(20.3, 4.9);
            shape.moveTo(10, 9.5);
            shape.lineTo(15.2, 13);
            shape.lineTo(10, 16.5);
            shape.closeSubpath();
            p->drawPath(shape);
            break;
        }
        case Subroutine: {
            QPainterPath shape;
            shape.moveTo(7, 3.5);
            shape.lineTo(6, 3.5);
            shape.arcTo(4.5, 3.5, 3, 3, 90, 90);
            shape.lineTo(4.5, 9.5);
            shape.cubicTo(4.5, 11, 3.8, 12, 2.5, 12);
            shape.cubicTo(3.8, 12, 4.5, 13, 4.5, 14.5);
            shape.lineTo(4.5, 19);
            shape.arcTo(4.5, 17.5, 3, 3, -180, 90);
            shape.lineTo(7, 20.5);
            shape.moveTo(17, 3.5);
            shape.lineTo(18, 3.5);
            shape.arcTo(16.5, 3.5, 3, 3, 90, -90);
            shape.lineTo(19.5, 9.5);
            shape.cubicTo(19.5, 11, 20.2, 12, 21.5, 12);
            shape.cubicTo(20.2, 12, 19.5, 13, 19.5, 14.5);
            shape.lineTo(19.5, 19);
            shape.arcTo(16.5, 17.5, 3, 3, 0, -90);
            shape.lineTo(17, 20.5);
            shape.moveTo(10, 8);
            shape.lineTo(14.5, 8);
            shape.lineTo(14.5, 15);
            shape.lineTo(9.5, 15);
            shape.moveTo(11.5, 13);
            shape.lineTo(9.5, 15);
            shape.lineTo(11.5, 17);
            p->drawPath(shape);
            break;
        }
        case Parameter: {
            QPainterPath shape;
            shape.moveTo(8.3, 3.7);
            shape.lineTo(15.7, 3.7);
            shape.arcTo(14.7336479, 3.6994337, 2, 2, 91.9282476, -61.9657035);
            shape.lineTo(20.8, 11.5);
            shape.arcTo(18.9339746, 11, 2, 2, 30, -60);
            shape.lineTo(16.6, 19.8);
            shape.arcTo(14.7336479, 18.3005663, 2, 2, -29.9625442, -61.9657035);
            shape.lineTo(8.3, 20.3);
            shape.arcTo(7.2663521, 18.3005663, 2, 2, -88.0717524, -61.9657035);
            shape.lineTo(3.2, 12.5);
            shape.arcTo(3.0660254, 11, 2, 2, -150, -60);
            shape.lineTo(7.4, 4.2);
            shape.arcTo(7.2663521, 3.6994337, 2, 2, 150.0374558, -61.9657035);
            shape.closeSubpath();
            shape.moveTo(8.7, 10);
            shape.lineTo(15.3, 10);
            shape.moveTo(8.7, 14);
            shape.lineTo(15.3, 14);
            p->drawPath(shape);
            break;
        }
        case Port: {
            QPainterPath shape;
            shape.moveTo(13, 3.5);
            shape.lineTo(19, 3.5);
            shape.arcTo(17, 3.5, 4, 4, 90, -90);
            shape.lineTo(21, 18.5);
            shape.arcTo(17, 16.5, 4, 4, 0, -90);
            shape.lineTo(13, 20.5);
            shape.moveTo(13, 3.5);
            shape.lineTo(13, 8.7);
            shape.moveTo(13, 15.3);
            shape.lineTo(13, 20.5);
            shape.moveTo(3, 12);
            shape.lineTo(9.5, 12);
            p->drawRoundedRect(QRectF(9.5, 8.7, 6.5, 6.6), 1.4, 1.4);
            p->drawPath(shape);
            break;
        }
        case Type: {
            p->drawRoundedRect(QRectF(3.5, 3.5, 17, 17), 3, 3);
            QPainterPath shape;
            shape.moveTo(8, 8);
            shape.lineTo(16, 8);
            shape.moveTo(12, 8);
            shape.lineTo(12, 17);
            shape.moveTo(10, 17);
            shape.lineTo(14, 17);
            p->drawPath(shape);
            break;
        }
        case Rename: {
            QPainterPath shape;
            shape.moveTo(4, 17);
            shape.lineTo(5, 12.5);
            shape.lineTo(16.8, 3);
            shape.arcTo(16.2488826, 2.6617322, 3, 3, 129.2413515, -89.3633671);
            shape.lineTo(20.2, 4.7);
            shape.arcTo(17.5382678, 4.1488826, 3, 3, 39.2413515, -89.3633671);
            shape.lineTo(8.2, 16.3);
            shape.lineTo(4, 17);
            shape.closeSubpath();
            shape.moveTo(14.3, 5);
            shape.lineTo(17.5, 8.7);
            shape.moveTo(4, 21);
            shape.lineTo(20, 21);
            p->drawPath(shape);
            break;
        }
        case CopyLink: {
            QPainterPath shape;
            shape.moveTo(13.7, 5.7);
            shape.lineTo(15.3, 4.1);
            shape.arcTo(14.0987807, 2.8987807, 8.2024387, 8.2024387, 135, -180);
            shape.lineTo(16.6, 14.4);
            shape.arcTo(9.6, 7.4017247, 8.2, 8.2, -44.9829578, -90.0340843);
            shape.moveTo(10.3, 18.3);
            shape.lineTo(8.7, 19.9);
            shape.arcTo(1.6987807, 12.8987806, 8.2024387, 8.2024387, -45.0000008, -179.9999984);
            shape.lineTo(7.4, 9.6);
            shape.arcTo(6.2, 8.3982753, 8.2, 8.2, 135.0170422, -90.0340843);
            shape.moveTo(8.7, 15.3);
            shape.lineTo(15.3, 8.7);
            p->drawPath(shape);
            break;
        }
        case OpenProject: {
            p->setPen(QPen(QColor(35, 50, 86), 2.7,
                            Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            QPainterPath eyebrow;
            eyebrow.moveTo(5.5, 7.8);
            eyebrow.cubicTo(9, 5.1, 13.5, 4.1, 18.3, 4);
            p->drawPath(eyebrow);
            p->setBrush(QColor(35, 50, 86));
            p->setPen(Qt::NoPen);
            p->drawEllipse(QPointF(7.2, 10.5), 1.2, 1.2);
            p->setBrush(Qt::NoBrush);
            p->setPen(QPen(QColor(39, 105, 239), 2.8,
                            Qt::SolidLine, Qt::RoundCap));
            line(18.3, 10.5, 5.7, 17.7);
            p->setPen(QPen(QColor(35, 50, 86), 2.7,
                            Qt::SolidLine, Qt::RoundCap));
            QPainterPath smile;
            smile.moveTo(5.3, 21);
            smile.cubicTo(10, 23.1, 15.8, 23.2, 19, 19.2);
            p->drawPath(smile);
            break;
        }
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
    int pixelMetric(PixelMetric metric, const QStyleOption* option=nullptr,
                    const QWidget* widget=nullptr) const override {
        if (metric == PM_TabBar_ScrollButtonOverlap) return 0;
        int value = QProxyStyle::pixelMetric(metric, option, widget);
        // Qt's tab arrows must accommodate the application's button padding.
        if (metric == PM_TabBarScrollButtonWidth && widget) {
            for (auto* button : widget->findChildren<QToolButton*>(QString(), Qt::FindDirectChildrenOnly))
                value = qMax(value, button->minimumSizeHint().width());
        }
        return value;
    }
    void drawPrimitive(PrimitiveElement element, const QStyleOption* option,
                       QPainter* painter, const QWidget* widget=nullptr) const override {
        if (element == PE_IndicatorCheckBox && option && qobject_cast<const QCheckBox*>(widget)) {
            InsightControlStyle::drawCheckBoxIndicator(*option, *painter);
            return;
        }
        if (element == PE_IndicatorTabClose && option) {
            const QRect area = option->rect;
            if (option->state & State_MouseOver) {
                painter->save(); painter->setPen(Qt::NoPen);
                painter->setBrush(option->palette.color(QPalette::Mid));
                painter->drawRoundedRect(area, 4, 4); painter->restore();
            }
            const int size = qBound(12, option->fontMetrics.height(), 18);
            const QRect glyph(area.center().x() - size / 2, area.center().y() - size / 2, size, size);
            icon(Close).paint(painter, glyph, Qt::AlignCenter,
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
