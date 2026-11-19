#include "navigationbranchmotion.h"
#include "applicationthememanager.h"
#include "workspacemotion.h"

#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QTreeView>

NavigationBranchMotion::NavigationBranchMotion(QTreeView* view)
    : QWidget(view->viewport()), tree(view), timeline(this)
{
    setObjectName(QStringLiteral("navigationBranchMotion"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    hide();
    tree->setAnimated(false); // Restores, search and model-driven changes take effect immediately.
    tree->viewport()->installEventFilter(this);
    timeline.setStartValue(0.0);
    timeline.setEndValue(1.0);
    timeline.setDuration(120);
    timeline.setEasingCurve(QEasingCurve::OutCubic);
    connect(&timeline, &QVariantAnimation::valueChanged, this, [this] {
        if (!parentRow.isValid() || tree->model() != parentRow.model()) {
            finish();
            return;
        }
        tree->viewport()->update(tree->visualRect(parentRow));
        update();
    });
    connect(&timeline, &QVariantAnimation::finished, this, &NavigationBranchMotion::finish);
    const auto stopExternal = [this] { if (!changingExpansion) finish(); };
    connect(tree, &QTreeView::expanded, this, stopExternal);
    connect(tree, &QTreeView::collapsed, this, stopExternal);
    connect(tree->verticalScrollBar(), &QScrollBar::valueChanged, this, stopExternal);
    connect(tree->horizontalScrollBar(), &QScrollBar::valueChanged, this, stopExternal);
    connect(&ApplicationThemeManager::instance(), &ApplicationThemeManager::animationsEnabledChanged,
            this, [this](bool on) { if (!on) finish(); });
    connect(&ApplicationThemeManager::instance(), &ApplicationThemeManager::themeAboutToChange,
            this, &NavigationBranchMotion::finish);
}

bool NavigationBranchMotion::runningFor(const QModelIndex& index) const
{
    return timeline.state() == QAbstractAnimation::Running && parentRow == index;
}

void NavigationBranchMotion::setSearchActive(bool active)
{
    searchActive = active;
    finish();
}

int NavigationBranchMotion::visibleChildrenHeight(const QModelIndex& index, const QRect& strip) const
{
    // The next sibling of this branch (or of an ancestor) bounds the whole
    // subtree without materializing or traversing its descendants.
    for (auto row = index; row.isValid(); row = row.parent()) {
        const auto next = row.siblingAtRow(row.row() + 1);
        if (next.isValid()) {
            const auto bounds = tree->visualRect(next);
            if (bounds.isValid())
                return qBound(0, bounds.top() - strip.top(), strip.height());
        }
    }
    int height = 0;
    for (auto child = tree->indexBelow(index); child.isValid(); child = tree->indexBelow(child)) {
        const auto bounds = tree->visualRect(child);
        if (!bounds.isValid()) break;
        height = bounds.bottom() + 1 - strip.top();
        if (height >= strip.height()) return strip.height();
    }
    return qMax(0, height);
}

void NavigationBranchMotion::toggle(const QModelIndex& index)
{
    if (!index.isValid()) return;
    const bool opening = !tree->isExpanded(index);
    const auto direction = opening ? QAbstractAnimation::Forward : QAbstractAnimation::Backward;
    QScopedValueRollback<bool> gesture(changingExpansion, true);
    if (searchActive || !WorkspaceMotion::enabled()) {
        finish();
        tree->setExpanded(index, opening);
        return;
    }
    if (runningFor(index)) {
        tree->setExpanded(index, opening);
        timeline.setDirection(direction);
        return;
    }
    finish();
    QWidget* viewport = tree->viewport();
    const QRect row = tree->visualRect(index);
    QRect strip = viewport->rect();
    strip.setTop(row.bottom() + 1);
    if (!viewport->isVisible() || !viewport->updatesEnabled() || !row.intersects(viewport->rect())
        || strip.isEmpty()) {
        tree->setExpanded(index, opening);
        return;
    }

    const QPersistentModelIndex stable(index);
    const auto capture = [&](bool expanded) {
        if (!expanded) {
            following = viewport->grab(strip);
            return;
        }
        childrenHeight = visibleChildrenHeight(stable, strip);
        if (childrenHeight > 0)
            children = viewport->grab(QRect(strip.topLeft(), QSize(strip.width(), childrenHeight)));
    };
    capture(!opening);
    tree->setExpanded(stable, opening);
    if (stable.isValid() && tree->model() == stable.model()
        && tree->verticalScrollMode() == QAbstractItemView::ScrollPerPixel) {
        const int drift = tree->visualRect(stable).top() - row.top();
        if (drift) {
            auto* bar = tree->verticalScrollBar();
            bar->setValue(bar->value() + drift);
        }
    }
    if (!stable.isValid() || tree->model() != stable.model() || tree->isExpanded(stable) != opening
        || tree->visualRect(stable).bottom() != row.bottom()) {
        finish();
        return;
    }
    capture(opening);
    if (!childrenHeight || children.isNull() || following.isNull()) {
        finish();
        return;
    }
    parentRow = stable;
    resetConnection = connect(tree->model(), &QAbstractItemModel::modelAboutToBeReset,
                              this, &NavigationBranchMotion::finish);
    setGeometry(strip);
    timeline.setDirection(direction);
    show();
    raise();
    timeline.start();
    viewport->update(row);
}

void NavigationBranchMotion::finish()
{
    if (timeline.state() != QAbstractAnimation::Stopped) timeline.stop();
    if (!isHidden()) hide();
    if (parentRow.isValid() && tree->model() == parentRow.model())
        tree->viewport()->update(tree->visualRect(parentRow));
    parentRow = QPersistentModelIndex();
    disconnect(resetConnection);
    children = {};
    following = {};
    childrenHeight = 0;
}

bool NavigationBranchMotion::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget() && !changingExpansion && !isHidden()) {
        const auto type = event->type();
        if (type == QEvent::Hide || type == QEvent::Resize || type == QEvent::Wheel
            || type == QEvent::DevicePixelRatioChange
            || (type == QEvent::MouseButtonPress
                && !tree->visualRect(parentRow).contains(static_cast<QMouseEvent*>(event)->position().toPoint())))
            finish();
    }
    return false;
}

void NavigationBranchMotion::paintEvent(QPaintEvent*)
{
    const int reveal = qRound(childrenHeight * progress());
    QPainter painter(this);
    painter.setClipRect(QRect(0, 0, width(), reveal));
    painter.drawPixmap(0, 0, children);
    painter.setClipping(false);
    painter.drawPixmap(0, reveal, following);
}
