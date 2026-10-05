#pragma once
#include <QKeyEvent>
#include <QListWidget>
#include <QRect>
#include <functional>

// Shared candidate-list mechanics. Query/version ownership and activation stay
// with each popup's domain controller.
namespace CandidatePopupNavigation {
inline bool selectable(const QListWidgetItem* item) {
    return item && !item->isHidden() && item->flags().testFlag(Qt::ItemIsEnabled)
        && item->flags().testFlag(Qt::ItemIsSelectable);
}
inline bool canActivate(const QListWidget* list) {
    return list && selectable(list->currentItem());
}
inline void moveSelection(QListWidget* list, int delta, bool previousStartsAtEnd = false) {
    if (!list || list->count() == 0 || delta == 0) return;
    const int current = list->currentRow();
    int step = delta > 0 ? 1 : -1;
    int row = qBound(0, current + step, list->count() - 1);
    if (current < 0) {
        row = previousStartsAtEnd && step < 0 ? list->count() - 1 : 0;
        step = row == 0 ? 1 : -1;
    }
    for (; row >= 0 && row < list->count(); row += step) {
        if (!selectable(list->item(row))) continue;
        list->setCurrentRow(row);
        list->scrollToItem(list->item(row));
        return;
    }
    if (!canActivate(list)) list->setCurrentRow(-1);
}
inline bool handleKey(QKeyEvent* event, const std::function<void(int)>& move,
    const std::function<bool()>& confirm, const std::function<void()>& cancel) {
    if (!event) return false;
    switch (event->key()) {
    case Qt::Key_Down: move(1); break;
    case Qt::Key_Up: move(-1); break;
    case Qt::Key_Return:
    case Qt::Key_Enter: if (!confirm()) return false; break;
    case Qt::Key_Escape: cancel(); break;
    default: return false;
    }
    event->accept();
    return true;
}
inline QRect fitToBounds(const QRect& desired, const QRect& bounds) {
    if (!bounds.isValid()) return desired;
    const QSize size = desired.size().expandedTo(QSize(1,1)).boundedTo(bounds.size());
    const int x = qBound(bounds.left(), desired.left(), bounds.right() - size.width() + 1);
    const int y = qBound(bounds.top(), desired.top(), bounds.bottom() - size.height() + 1);
    return QRect(QPoint(x,y), size);
}
}
