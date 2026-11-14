#pragma once
#include "roundedicons.h"
#include <QHelpEvent>
#include <QPainterPath>
#include <QMouseEvent>
#include <QPersistentModelIndex>
#include <QStyledItemDelegate>
#include <QToolTip>
#include <QTreeWidget>
#include <functional>

// Local to the two navigation trees; other Ela trees keep their native layout.
class NavigationRowStyle final : public QStyledItemDelegate {
public:
    static constexpr int ProjectRole = Qt::UserRole + 40;
    static constexpr int ActiveRole = Qt::UserRole + 41;
    static constexpr int FolderRole = Qt::UserRole + 42;
    static constexpr int NameRole = Qt::UserRole + 43;
    static constexpr int TypeRole = Qt::UserRole + 44;
    static constexpr int UnresolvedRole = Qt::UserRole + 45;
    static constexpr int MissingNameRole = Qt::UserRole + 46;
    static constexpr int MissingTypeRole = Qt::UserRole + 47;
    static constexpr int SimulationRole = Qt::UserRole + 48;
    static constexpr int SourceRole = Qt::UserRole + 49;
    static constexpr int CategoryRole = Qt::UserRole + 50;
    std::function<void(const QString&)> activateProject;
    std::function<void(const QModelIndex&)> activateDesign;

    explicit NavigationRowStyle(QTreeView* view, bool design)
        : QStyledItemDelegate(view), tree(view), design(design) {
        tree->setIndentation(0);
        tree->setRootIsDecorated(false);
        tree->setExpandsOnDoubleClick(false);
        tree->setUniformRowHeights(true);
        tree->setIconSize({18, 18});
        tree->setItemDelegate(this);
        tree->viewport()->installEventFilter(this);
    }
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        auto size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(qMax(28, option.fontMetrics.height() + 8));
        return size;
    }
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        if (design && index.data(NameRole).isValid()) { paintDesign(painter, option, index); return; }
        QStyleOptionViewItem row(option);
        initStyleOption(&row, index);
        int depth = 0;
        for (auto parent = index.parent(); parent.isValid(); parent = parent.parent()) ++depth;
        const bool expandable = tree->model()->hasChildren(index) || index.data(FolderRole).toBool();
        if (index.data(FolderRole).toBool())
            row.icon = RoundedIcons::icon(tree->isExpanded(index) ? RoundedIcons::FolderOpen : RoundedIcons::Folder);
        else if (design && expandable && (option.state & (QStyle::State_MouseOver | QStyle::State_HasFocus)))
            row.icon = RoundedIcons::icon(tree->isExpanded(index) ? RoundedIcons::TreeExpanded : RoundedIcons::TreeCollapsed);
        const bool project = !index.data(ProjectRole).toString().isEmpty();
        auto* style = tree->style();
        QStyleOptionViewItem background(row);
        background.text.clear(); background.icon = {};
        paintBackground(painter, background);
        row.rect.adjust(2 + depth * 14, 0, project ? -28 : 0, 0);
        // The full row already owns selection/hover painting.
        row.state &= ~(QStyle::State_Selected | QStyle::State_MouseOver | QStyle::State_HasFocus);
        if (option.state & QStyle::State_Selected)
            row.palette.setColor(QPalette::Text, option.palette.color(QPalette::HighlightedText));
        painter->save(); painter->setFont(row.font); painter->setPen(row.palette.color(QPalette::Text));
        row.icon.paint(painter, QRect(row.rect.left(),row.rect.center().y()-9,18,18));
        const auto textRect=row.rect.adjusted(24,0,-4,0);
        painter->drawText(textRect,Qt::AlignVCenter|Qt::AlignLeft,
            row.fontMetrics.elidedText(row.text,Qt::ElideRight,qMax(0,textRect.width())));
        painter->restore();
        if (project) {
            const auto active = index.data(ActiveRole).toBool();
            if (active || (option.state & (QStyle::State_MouseOver | QStyle::State_HasFocus)))
                RoundedIcons::icon(active ? RoundedIcons::Success : RoundedIcons::Pin)
                    .paint(painter, actionRect(option.rect).adjusted(4, 5, -4, -5));
        }
    }
protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched != tree->viewport()) return false;
        if (design) {
            if (event->type() == QEvent::MouseButtonDblClick) {
                auto* mouse = static_cast<QMouseEvent*>(event);
                const auto index = tree->indexAt(mouse->position().toPoint());
                if (mouse->button() == Qt::LeftButton && index.isValid()
                    && !disclosureRect(tree->visualRect(index), index).contains(mouse->position().toPoint())
                    && activateDesign) activateDesign(index);
                return true;
            }
            if (event->type() == QEvent::Hide || event->type() == QEvent::Leave
                || event->type() == QEvent::UngrabMouse) { pressed = QPersistentModelIndex(); pressedAction = false; }
            if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease) {
                auto* mouse = static_cast<QMouseEvent*>(event);
                if (mouse->button() != Qt::LeftButton) return false;
                const auto index = tree->indexAt(mouse->position().toPoint());
                const bool disclosure = index.isValid() && tree->model()->hasChildren(index)
                    && disclosureRect(tree->visualRect(index), index).contains(mouse->position().toPoint());
                if (event->type() == QEvent::MouseButtonPress) {
                    pressed = index; pressedAction = disclosure;
                    return disclosure;
                }
                const bool toggle = pressedAction && pressed == index && disclosure;
                const bool consumed = pressedAction;
                pressed = QPersistentModelIndex(); pressedAction = false;
                if (toggle) tree->setExpanded(index, !tree->isExpanded(index));
                return consumed;
            }
            return false;
        }
        if (design && (event->type() == QEvent::Hide || event->type() == QEvent::Leave
            || event->type() == QEvent::UngrabMouse)) { pressed = QPersistentModelIndex(); suppressRelease = false; }
        if (design && event->type() == QEvent::MouseButtonPress) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton) {
                pressed = tree->indexAt(mouse->position().toPoint());
                pressedAction = !pressed.data(ProjectRole).toString().isEmpty()
                    && actionRect(tree->visualRect(pressed)).contains(mouse->position().toPoint());
                if (pressedAction) return true;
            }
        }
        if (event->type() == QEvent::ToolTip) {
            auto* help = static_cast<QHelpEvent*>(event);
            const auto index = tree->indexAt(help->pos());
            if (!index.data(ProjectRole).toString().isEmpty() && actionRect(tree->visualRect(index)).contains(help->pos())) {
                QToolTip::showText(help->globalPos(), design && index.data(UnresolvedRole).toBool()
                    ? tr("Unresolved project cannot be activated")
                    : index.data(ActiveRole).toBool() ? tr("Active project") : tr("Activate project"), tree);
                return true;
            }
        }
        if (event->type() == QEvent::MouseButtonDblClick) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            const auto index = tree->indexAt(mouse->position().toPoint());
            if (mouse->button() == Qt::LeftButton && (tree->model()->hasChildren(index) || index.data(FolderRole).toBool()
                || !index.data(ProjectRole).toString().isEmpty())) {
                suppressRelease = true;
                return true;
            }
        }
        if (event->type() == QEvent::MouseButtonRelease) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() != Qt::LeftButton) return false;
            if (suppressRelease) { suppressRelease = false; return true; }
            const auto index = tree->indexAt(mouse->position().toPoint());
            if (design) {
                const bool action = !index.data(ProjectRole).toString().isEmpty()
                    && actionRect(tree->visualRect(index)).contains(mouse->position().toPoint());
                const bool same = pressed.isValid() && pressed == index && action == pressedAction;
                pressed = QPersistentModelIndex();
                if (!same) return true;
                if (action && index.data(UnresolvedRole).toBool()) return true;
            }
            if (!index.isValid()) return false;
            const auto project = index.data(ProjectRole).toString();
            if (!project.isEmpty() && actionRect(tree->visualRect(index)).contains(mouse->position().toPoint())) {
                if (activateProject) activateProject(project);
                return true;
            }
            if (tree->model()->hasChildren(index) || index.data(FolderRole).toBool()) {
                tree->setExpanded(index, !tree->isExpanded(index));
                return true;
            }
        }
        return false;
    }
private:
    void paintBackground(QPainter* painter, QStyleOptionViewItem row) const {
        const bool focus=row.state & QStyle::State_HasFocus;
        row.state &= ~QStyle::State_HasFocus;
        // Ela owns this primitive; bypass QProxyStyle's native focus frame and
        // CE_ItemViewItem's second content/background pass.
        tree->style()->drawPrimitive(QStyle::PE_PanelItemViewItem,&row,painter,tree);
        if(focus) {
            painter->save();painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(QPen(row.palette.color(QPalette::Highlight),2,Qt::SolidLine,Qt::RoundCap));
            painter->drawLine(QPointF(row.rect.left()+2,row.rect.top()+8),QPointF(row.rect.left()+2,row.rect.bottom()-8));
            painter->restore();
        }
    }
    QRect disclosureRect(const QRect& rect, const QModelIndex& index) const {
        int depth = 0;
        for (auto parent = index.parent(); parent.isValid(); parent = parent.parent()) ++depth;
        const int reserved = (index.data(UnresolvedRole).toBool() ? 22 : 0)
            + (index.data(SimulationRole).toBool() ? 28 : 0) + (index.data(SourceRole).toBool() ? 28 : 0);
        const int indent = qMin(depth * 14, qMax(0, rect.width() - reserved - 30
            - tree->fontMetrics().horizontalAdvance(QStringLiteral("…"))));
        return QRect(rect.left() + 2 + indent, rect.top(), 24, rect.height());
    }
    void paintDesign(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
        QStyleOptionViewItem row(option);
        initStyleOption(&row, index);
        QStyleOptionViewItem background(row);
        background.text.clear(); background.icon = {};
        paintBackground(painter, background);
        const bool project = false;
        const bool unresolved = index.data(UnresolvedRole).toBool();
        const bool simulation = index.data(SimulationRole).toBool();
        const bool source = index.data(SourceRole).toBool();
        const bool expandable = tree->model()->hasChildren(index);
        int depth = 0;
        for (auto parent = index.parent(); parent.isValid(); parent = parent.parent()) ++depth;
        const auto metrics = row.fontMetrics;
        const int reserved = (unresolved ? 22 : 0) + (simulation ? 28 : 0) + (source ? 28 : 0);
        // Keep one elision glyph after the icon slot (18), gap (6) and margins (2 + 4).
        const int indent = qMin(depth * 14, qMax(0, row.rect.width() - reserved - 30 - metrics.horizontalAdvance(QStringLiteral("…"))));
        QRect content = row.rect.adjusted(2 + indent, 0, -reserved - 4, 0);
        QRect icon(content.left(), content.center().y() - RoundedIcons::SidebarSize/2, RoundedIcons::SidebarSize, RoundedIcons::SidebarSize);
        if(expandable) {
            painter->save();painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(QPen(row.palette.color(QPalette::Text),1.25,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
            const QPointF c=QRectF(icon).center();QPainterPath path;
            if(tree->isExpanded(index)){path.moveTo(c+QPointF(-3,-1.5));path.lineTo(c+QPointF(0,1.5));path.lineTo(c+QPointF(3,-1.5));}
            else {path.moveTo(c+QPointF(-1.5,-3));path.lineTo(c+QPointF(1.5,0));path.lineTo(c+QPointF(-1.5,3));}
            painter->drawPath(path);painter->restore();
        } else row.icon.paint(painter, icon);
        QRect text = content.adjusted(24, 0, 0, 0);
        const bool selected = option.state & QStyle::State_Selected;
        const QColor primary = row.palette.color(selected ? QPalette::HighlightedText : QPalette::Text);
        const QColor backdrop = row.palette.color(selected ? QPalette::Highlight : QPalette::Base);
        const QColor secondary((primary.red()*65 + backdrop.red()*35)/100,
                               (primary.green()*65 + backdrop.green()*35)/100,
                               (primary.blue()*65 + backdrop.blue()*35)/100);
        const auto name = index.data(NameRole).toString();
        const auto type = index.data(TypeRole).toString();
        painter->save(); auto primaryFont=row.font; primaryFont.setWeight(QFont::DemiBold); painter->setFont(primaryFont);
        painter->setPen(index.data(MissingNameRole).toBool() ? secondary : primary);
        const int nameWidth = QFontMetrics(primaryFont).horizontalAdvance(name);
        painter->drawText(text, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine,
                          metrics.elidedText(name, Qt::ElideRight, qMax(0,text.width())));
        if (!type.isEmpty() && nameWidth + metrics.horizontalAdvance(QStringLiteral(" […]")) <= text.width()) {
            text.setLeft(text.left() + nameWidth + metrics.horizontalAdvance(QLatin1Char(' ')));
            painter->setPen(secondary); painter->setFont(row.font);
            const int brackets = metrics.horizontalAdvance(QStringLiteral("[]"));
            painter->drawText(text, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine,
                '[' + metrics.elidedText(type, Qt::ElideRight, qMax(0,text.width()-brackets)) + ']');
        }
        painter->restore();
        if (source || simulation) {
            painter->save(); auto font = row.font; font.setPointSizeF(qMax(7.0, font.pointSizeF()-2));
            painter->setFont(font); painter->setPen(secondary);
            painter->drawText(QRect(row.rect.right() - (unresolved ? 22 : 0) - 28, row.rect.top(), 26, row.rect.height()),
                Qt::AlignCenter, simulation ? QStringLiteral("SIM") : QStringLiteral("SRC"));
            if(source && simulation)painter->drawText(QRect(row.rect.right()-(unresolved?22:0)-56,row.rect.top(),26,row.rect.height()),Qt::AlignCenter,QStringLiteral("SRC"));
            painter->restore();
        }
        if (unresolved) RoundedIcons::icon(RoundedIcons::Warning).paint(painter,
            QRect(row.rect.right() - (project ? 28 : 0) - 20, row.rect.center().y()-9, 18,18));
        if (project) {
            const bool active = index.data(ActiveRole).toBool();
            if (active || (option.state & (QStyle::State_MouseOver | QStyle::State_HasFocus)))
                RoundedIcons::icon(active ? RoundedIcons::Success : RoundedIcons::Pin)
                    .paint(painter, actionRect(option.rect).adjusted(4,5,-4,-5), Qt::AlignCenter,
                           unresolved ? QIcon::Disabled : QIcon::Normal);
        }
    }
    static QRect actionRect(const QRect& row) { return {row.right() - 27, row.top(), 28, row.height()}; }
    QTreeView* tree;
    bool design;
    bool suppressRelease = false;
    QPersistentModelIndex pressed;
    bool pressedAction = false;
};
