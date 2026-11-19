#pragma once
#include "roundedicons.h"
#include "navigationbranchmotion.h"
#include <QKeyEvent>
#include "insightvisualstyle.h"
#include <QAccessible>
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

    NavigationRowStyle(QTreeView* view, bool design, NavigationBranchMotion* motion)
        : QStyledItemDelegate(view), tree(view), design(design), motion(motion) {
        tree->setIndentation(0);
        tree->setRootIsDecorated(false);
        tree->setExpandsOnDoubleClick(false);
        tree->setUniformRowHeights(true);
        tree->setIconSize({18, 18});
        tree->setItemDelegate(this);
        tree->installEventFilter(this);
        tree->viewport()->installEventFilter(this);
        connect(tree, &QTreeView::collapsed, this, [this] {
            if (!QAccessible::isActive()) return;
            // Qt 6.10 invalidates its flattened accessible rows on expansion only.
            QAccessibleTableModelChangeEvent change(tree, QAccessibleTableModelChangeEvent::ModelReset);
            QAccessible::updateAccessibility(&change);
        });
    }
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        auto size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(qMax(28, option.fontMetrics.height() + 8));
        return size;
    }
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        paintRow(painter, option, index);
    }
protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == tree && event->type() == QEvent::KeyPress) {
            auto* key = static_cast<QKeyEvent*>(event);
            const auto index = tree->currentIndex();
            const int openKey = tree->isRightToLeft() ? Qt::Key_Left : Qt::Key_Right;
            const int closeKey = tree->isRightToLeft() ? Qt::Key_Right : Qt::Key_Left;
            if (key->modifiers() == Qt::NoModifier && tree->itemsExpandable() && index.isValid()
                && tree->model()->hasChildren(index)
                && key->key() == (tree->isExpanded(index) ? closeKey : openKey)) {
                motion->toggle(index);
                return true;
            }
        }
        if (watched != tree->viewport()) return false;
        if (design) {
            if (event->type() == QEvent::MouseButtonDblClick) {
                auto* mouse = static_cast<QMouseEvent*>(event);
                const auto index = tree->indexAt(mouse->position().toPoint());
                if (mouse->button() != Qt::LeftButton) return false;
                if (index.isValid() && tree->model()->hasChildren(index)
                    && disclosureRect(tree->visualRect(index), index).contains(mouse->position().toPoint())) {
                    pressed = index; pressedAction = true; suppressRelease = false;
                    return true;
                }
                pressed = QPersistentModelIndex(); pressedAction = false; suppressRelease = true;
                if (index.isValid()
                    && !disclosureRect(tree->visualRect(index), index).contains(mouse->position().toPoint())) {
                    if (index.data(CategoryRole).toBool()) {
                        if (tree->model()->hasChildren(index)) motion->toggle(index);
                    } else if (activateDesign) activateDesign(index);
                }
                return true;
            }
            if (event->type() == QEvent::Hide || event->type() == QEvent::Leave
                || event->type() == QEvent::UngrabMouse) { pressed = QPersistentModelIndex(); pressedAction = false; suppressRelease = false; }
            if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease) {
                auto* mouse = static_cast<QMouseEvent*>(event);
                if (mouse->button() != Qt::LeftButton) return false;
                if (event->type() == QEvent::MouseButtonRelease && suppressRelease) { suppressRelease = false; return true; }
                const auto index = tree->indexAt(mouse->position().toPoint());
                const bool disclosure = index.isValid() && tree->model()->hasChildren(index)
                    && disclosureRect(tree->visualRect(index), index).contains(mouse->position().toPoint());
                if (event->type() == QEvent::MouseButtonPress) {
                    suppressRelease = false;
                    pressed = index; pressedAction = disclosure;
                    return disclosure;
                }
                const bool toggle = pressedAction && pressed == index && disclosure;
                const bool consumed = pressedAction;
                pressed = QPersistentModelIndex(); pressedAction = false;
                if (toggle) motion->toggle(index);
                return consumed;
            }
            return false;
        }
        if (event->type() == QEvent::ToolTip) {
            auto* help = static_cast<QHelpEvent*>(event);
            const auto index = tree->indexAt(help->pos());
            if (!index.data(ProjectRole).toString().isEmpty() && actionRect(tree->visualRect(index)).contains(help->pos())) {
                QToolTip::showText(help->globalPos(), index.data(ActiveRole).toBool()
                    ? tr("Active project") : tr("Activate project"), tree);
                return true;
            }
        }
        if (event->type() == QEvent::MouseButtonDblClick) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            const auto index = tree->indexAt(mouse->position().toPoint());
            if (mouse->button() == Qt::LeftButton && index.isValid()
                && (tree->model()->hasChildren(index) || index.data(FolderRole).toBool()
                    || !index.data(ProjectRole).toString().isEmpty())) {
                suppressRelease = !disclosureRect(tree->visualRect(index), index).contains(mouse->position().toPoint());
                return true;
            }
        }
        if (event->type() == QEvent::MouseButtonRelease) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() != Qt::LeftButton) return false;
            if (suppressRelease) { suppressRelease = false; return true; }
            const auto index = tree->indexAt(mouse->position().toPoint());
            if (!index.isValid()) return false;
            const auto project = index.data(ProjectRole).toString();
            if (!project.isEmpty() && actionRect(tree->visualRect(index)).contains(mouse->position().toPoint())) {
                if (activateProject) activateProject(project);
                return true;
            }
            if (tree->model()->hasChildren(index) || index.data(FolderRole).toBool()) {
                motion->toggle(index);
                return true;
            }
        }
        return false;
    }
private:
    void paintBackground(QPainter* painter, QStyleOptionViewItem row) const {
        const bool focus=row.state & QStyle::State_HasFocus;
        QString region=QStringLiteral("left");
        for(auto* widget=static_cast<QWidget*>(tree);widget;widget=widget->parentWidget())
            if(widget->property("workspaceToolRegion").isValid()) {region=widget->property("workspaceToolRegion").toString();break;}
        const auto colors=InsightVisualStyle::workspaceRegionColors(region);
        if(row.state & (QStyle::State_Selected | QStyle::State_MouseOver)) {
            painter->save();painter->setRenderHint(QPainter::Antialiasing);painter->setPen(Qt::NoPen);
            painter->setBrush(row.state & QStyle::State_Selected ? (tree->hasFocus()?colors.selectedFocused:colors.selected) : colors.hover);
            painter->drawRoundedRect(QRectF(row.rect).adjusted(2,2,-2,-2),4,4);painter->restore();
        }
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
        const int indent = qMin(depth * 16, qMax(0, rect.width() - reserved - 52
            - tree->fontMetrics().horizontalAdvance(QStringLiteral("…"))));
        return QRect(rect.left() + 4 + indent, rect.top(), 18, rect.height());
    }
    void paintRow(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
        QStyleOptionViewItem row(option);
        initStyleOption(&row, index);
        QStyleOptionViewItem background(row);
        background.text.clear(); background.icon = {};
        paintBackground(painter, background);
        const bool project = !index.data(ProjectRole).toString().isEmpty();
        const bool unresolved = index.data(UnresolvedRole).toBool();
        const bool simulation = index.data(SimulationRole).toBool();
        const bool source = index.data(SourceRole).toBool();
        const bool expandable = tree->model()->hasChildren(index) || index.data(FolderRole).toBool();
        auto primaryFont=tree->font();
        primaryFont.setWeight(index.data(CategoryRole).toBool() || project ? QFont::DemiBold : QFont::Normal);
        const QFontMetrics metrics(primaryFont);
        const int reserved = (unresolved ? 22 : 0) + (simulation ? 28 : 0) + (source ? 28 : 0) + (project ? 28 : 0);
        const QRect disclosure=disclosureRect(row.rect,index);
        if (expandable) {
            painter->save();
            painter->translate(disclosure.center());
            const qreal fraction = motion->runningFor(index) ? motion->progress() : (tree->isExpanded(index) ? 1.0 : 0.0);
            painter->rotate(90.0 * fraction);
            RoundedIcons::icon(RoundedIcons::TreeCollapsed).paint(painter, QRect(-7, -7, 14, 14));
            painter->restore();
        }
        const QRect icon(disclosure.right()+3,row.rect.center().y()-9,18,18);
        if(index.data(FolderRole).toBool() || index.data(CategoryRole).toBool())
            row.icon=RoundedIcons::icon(tree->isExpanded(index)?RoundedIcons::FolderOpen:RoundedIcons::Folder);
        row.icon.paint(painter,icon);
        QRect text = row.rect.adjusted(icon.right()+7-row.rect.left(),0,-reserved-4,0);
        const QColor primary = InsightVisualStyle::theme().textPrimary;
        const QColor secondary = InsightVisualStyle::theme().textSecondary;
        const auto name = design ? index.data(NameRole).toString() : row.text;
        const auto type = design ? index.data(TypeRole).toString() : QString();
        painter->save(); painter->setFont(primaryFont);
        painter->setPen(index.data(MissingNameRole).toBool() ? secondary : primary);
        const int nameWidth = QFontMetrics(primaryFont).horizontalAdvance(name);
        painter->drawText(text, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine,
                          metrics.elidedText(name, Qt::ElideRight, qMax(0,text.width())));
        if (!type.isEmpty() && nameWidth + metrics.horizontalAdvance(QStringLiteral(" […]")) <= text.width()) {
            text.setLeft(text.left() + nameWidth + metrics.horizontalAdvance(QLatin1Char(' ')));
            painter->setPen(secondary);
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
    NavigationBranchMotion* motion;
    bool suppressRelease = false;
    QPersistentModelIndex pressed;
    bool pressedAction = false;
};
