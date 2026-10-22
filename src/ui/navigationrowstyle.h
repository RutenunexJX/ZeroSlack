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
        if (event->type() == QEvent::ToolTip) {
            auto* help = static_cast<QHelpEvent*>(event);
            const auto index = tree->indexAt(help->pos());
            if (!index.data(ProjectRole).toString().isEmpty() && actionRect(tree->visualRect(index)).contains(help->pos())) {
                QToolTip::showText(help->globalPos(), index.data(ActiveRole).toBool() ? tr("Active project") : tr("Activate project"), tree);
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
    static QRect actionRect(const QRect& row) { return {row.right() - 27, row.top(), 28, row.height()}; }
    QTreeWidget* tree;
    bool design;
    bool suppressRelease = false;
};
