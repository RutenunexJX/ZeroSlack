#pragma once
#include "insightcontrolstyle.h"
#include "roundediconcatalog.h"
#include <QApplication>
#include <QCheckBox>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QProxyStyle>
#include <QStyleOption>
#include <QToolButton>
#include <QMenu>
#include <cmath>

namespace RoundedIcons {
// Shared sidebar geometry: a uniform 24-unit grid, 18-unit ink width and
// round strokes. Selection changes color/background without changing ink size.
inline constexpr int SidebarSize = 18;
inline constexpr qreal GridSize = 24;
inline constexpr qreal ContentLeading = 3;
inline constexpr qreal ContentTrailing = 21;
inline constexpr qreal MinimumContentWidth = 18;
inline constexpr qreal NormalStroke = 1.75;
inline constexpr qreal SelectedStroke = NormalStroke;
class Engine final : public QIconEngine {
public:
    explicit Engine(Kind value) : kind(value) {}
    QIconEngine* clone() const override { return new Engine(kind); }
    void paint(QPainter* p, const QRect& rect, QIcon::Mode mode, QIcon::State state) override {
        p->save();
        const qreal side = qMin(rect.width(), rect.height());
        p->translate(rect.x() + (rect.width() - side) / 2, rect.y() + (rect.height() - side) / 2);
        p->scale(side / GridSize, side / GridSize);
        p->setRenderHint(QPainter::Antialiasing);
        const auto palette = QApplication::palette();
        const QColor color = mode == QIcon::Disabled
            ? palette.color(QPalette::Disabled, QPalette::Text)
            : palette.color(QPalette::Text);
        p->setPen(QPen(color, state == QIcon::On ? SelectedStroke : NormalStroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p->setBrush(Qt::NoBrush);
        p->drawPath(glyph(kind));
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
inline QIcon icon(Kind kind) {
    if (kind < 0 || kind >= CatalogSize) return {};
    static const auto icons = [] {
        std::array<QIcon, CatalogSize> result;
        for (int i = 0; i < CatalogSize; ++i) result[i] = QIcon(new Engine(static_cast<Kind>(i)));
        return result;
    }();
    return icons[kind];
}
inline QIcon iconForAction(const QString& id) { return icon(actionKind(id)); }
inline QIcon iconForLabel(const QString& text) {
    auto kind = labelKind(text);
    if (kind != CatalogSize) return icon(kind);
    // Existing host menu labels whose wording differs from the catalog title.
    static const QHash<QString, Kind> labels{
        {"Attach", AttachBlock}, {"Detach", DetachBlock},
        {"Copy block", Copy}, {"Cut block", Cut}, {"Delete block", Delete},
        {"Open original", OpenOriginal}, {"Open definition", GoDefinition},
        {"Go to instance", GoInstance}, {"Auto height", AutoHeight},
        {"Appearance", Appearance}, {"Height", Height}, {"Width", Width},
        {"Caption", Caption}, {"Block associations", BlockAssociations},
        {"Attach associations…", AttachBlock}, {"Blocks", BlockAssociations},
        {"New note", BlockText}, {"New code block", BlockCode},
        {"Upstream", TraceUpstream}, {"Downstream", TraceDownstream},
        {"Expand cone", ExpandCone}, {"Reload PDF", Refresh},
        {"Open path", OpenOriginal}, {"Block diagram", ModuleDiagram},
        {"State", StateGraph}, {"Pinloom", PinloomLibrary}, {"xIPs", IpLibrary},
        {"Fit", GraphFit}, {"Clear Activity", Clear},
    };
    return icon(labels.value(text, CatalogSize));
}
inline void applyMenuIcons(QMenu* menu) {
    for (auto* action : menu->actions()) {
        if (action->isSeparator()) continue;
        const auto id = action->property("actionId").toString();
        const QIcon mapped = id.isEmpty() ? iconForLabel(action->text()) : iconForAction(id);
        if (!mapped.isNull() && (action->icon().isNull() || !id.isEmpty())) action->setIcon(mapped);
        if (action->menu()) applyMenuIcons(action->menu());
    }
}
inline Kind blockKind(const QString& type) {
    if (type == "wave") return BlockWave;
    if (type == "image") return BlockImage;
    if (type == "reference") return BlockReference;
    if (type == "code") return BlockCode;
    if (type == "attachment") return Attachment;
    return BlockText;
}
inline Kind providerKind(const QString& id) {
    if (id == "temporaryEditor") return TemporaryEditor;
    if (id == "rtlInsight.kernel") return SignalTrace;
    if (id == "rtlInsight.block") return ModuleDiagram;
    if (id == "rtlInsight.hotspot") return SignalHeatmap;
    if (id == "rtlInsight.state") return StateGraph;
    if (id == "liveInsights") return Activity;
    if (id == "xips") return IpLibrary;
    if (id == "simdock") return SimulationSettings;
    if (id == "pinloom") return PinloomLibrary;
    return Information;
}


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
        case SP_DirIcon: case SP_DirLinkIcon: case SP_DirClosedIcon: return icon(Folder);
        case SP_DirOpenIcon: return icon(FolderOpen);
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
        case SP_MessageBoxInformation: return icon(Information);
        case SP_MessageBoxQuestion: return icon(Question);
        case SP_DialogApplyButton: case SP_DialogOkButton: return icon(Success);
        case SP_DialogCancelButton: return icon(Cancel);
        default: return QProxyStyle::standardIcon(id,option,widget);
        }
    }
};
}
