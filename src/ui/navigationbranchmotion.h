#pragma once

#include <QPersistentModelIndex>
#include <QPixmap>
#include <QVariantAnimation>
#include <QWidget>

class QTreeView;

// The view owns expansion and hit testing. Only the strip below the parent
// is composited while a user gesture is in flight.
class NavigationBranchMotion final : public QWidget {
public:
    explicit NavigationBranchMotion(QTreeView* view);
    void toggle(const QModelIndex& index);
    void finish();
    void setSearchActive(bool active);
    bool runningFor(const QModelIndex& index) const;
    qreal progress() const { return timeline.currentValue().toReal(); }

protected:
    void paintEvent(QPaintEvent*) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    int visibleChildrenHeight(const QModelIndex& index, const QRect& strip) const;
    QTreeView* tree;
    QVariantAnimation timeline;
    QPersistentModelIndex parentRow;
    QPixmap children;
    QPixmap following;
    int childrenHeight = 0;
    bool searchActive = false;
    bool changingExpansion = false;
    QMetaObject::Connection resetConnection;
};
