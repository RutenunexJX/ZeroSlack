#pragma once
#include "roundedicons.h"
#include <QHelpEvent>
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
    std::function<void(const QString&)> activateProject;

    explicit NavigationRowStyle(QTreeWidget* view, bool design)
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
            row.icon = RoundedIcons::icon(tree->isExpanded(index) ? RoundedIcons::Down : RoundedIcons::Right);
        const bool project = !index.data(ProjectRole).toString().isEmpty();
        auto* style = tree->style();
        QStyleOptionViewItem background(row);
        background.text.clear(); background.icon = {};
        style->drawControl(QStyle::CE_ItemViewItem, &background, painter, tree);
        row.rect.adjust(2 + depth * 14, 0, project ? -28 : 0, 0);
        // The full row already owns selection/hover painting.
        row.state &= ~(QStyle::State_Selected | QStyle::State_MouseOver | QStyle::State_HasFocus);
        if (option.state & QStyle::State_Selected)
            row.palette.setColor(QPalette::Text, option.palette.color(QPalette::HighlightedText));
        style->drawControl(QStyle::CE_ItemViewItem, &row, painter, tree);
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
    void paintDesign(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
        QStyleOptionViewItem row(option);
        initStyleOption(&row, index);
        QStyleOptionViewItem background(row);
        background.text.clear(); background.icon = {};
        tree->style()->drawControl(QStyle::CE_ItemViewItem, &background, painter, tree);
        const bool project = !index.data(ProjectRole).toString().isEmpty();
        const bool unresolved = index.data(UnresolvedRole).toBool();
        const bool expandable = tree->model()->hasChildren(index);
        int depth = 0;
        for (auto parent = index.parent(); parent.isValid(); parent = parent.parent()) ++depth;
        const auto metrics = row.fontMetrics;
        const int reserved = (project ? 28 : 0) + (unresolved ? 22 : 0);
        // Keep one elision glyph after the icon slot (18), gap (6) and margins (2 + 4).
        const int indent = qMin(depth * 14, qMax(0, row.rect.width() - reserved - 30 - metrics.horizontalAdvance(QStringLiteral("…"))));
        QRect content = row.rect.adjusted(2 + indent, 0, -reserved - 4, 0);
        QRect icon(content.left(), content.center().y() - 9, 18, 18);
        const auto decoration = expandable
            ? RoundedIcons::icon(tree->isExpanded(index) ? RoundedIcons::Down : RoundedIcons::Right) : row.icon;
        decoration.paint(painter, icon);
        QRect text = content.adjusted(24, 0, 0, 0);
        const bool selected = option.state & QStyle::State_Selected;
        const QColor primary = row.palette.color(selected ? QPalette::HighlightedText : QPalette::Text);
        const QColor backdrop = row.palette.color(selected ? QPalette::Highlight : QPalette::Base);
        const QColor secondary((primary.red()*85 + backdrop.red()*15)/100,
                               (primary.green()*85 + backdrop.green()*15)/100,
                               (primary.blue()*85 + backdrop.blue()*15)/100);
        const auto name = index.data(NameRole).toString();
        const auto type = index.data(TypeRole).toString();
        painter->save(); painter->setFont(row.font);
        painter->setPen(index.data(MissingNameRole).toBool() ? secondary : primary);
        const int nameWidth = metrics.horizontalAdvance(name);
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
    QTreeWidget* tree;
    bool design;
    bool suppressRelease = false;
    QPersistentModelIndex pressed;
    bool pressedAction = false;
};
