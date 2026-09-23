#include "contextrail.h"
#include "uicontrols.h"

#include <QAction>
#include <QToolButton>
#include <QWidgetAction>

ContextRail::ContextRail(QWidget* parent)
    : ContextRailBase(parent)
{
    setObjectName(QStringLiteral("contextRail"));
    setWindowTitle(tr("Context"));
    setOrientation(Qt::Vertical);
    setMovable(false);
    setFloatable(false);
    setToolButtonStyle(Qt::ToolButtonIconOnly);
    setIconSize(QSize(22, 22));
#ifdef ZEROSLACK_ENABLE_ELA
    setToolBarSpacing(2);
    setToolButtonSize(QSize(38, 38));
#endif
    setVisible(false);
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QAction* action = actionAt(pos);
        if (action && actionsById.value(action->data().toString()) == action)
            emit entryContextMenuRequested(action->data().toString(), mapToGlobal(pos));
    });
}

QAction* ContextRail::addRailAction(const QIcon& icon, const QString& title, QAction* before)
{
    auto* action = new QWidgetAction(this);
    action->setIcon(icon);
    action->setText(title);
    auto* button = UiControls::railButton(this);
    button->setProperty("panelMotionToggle", true);
    button->setIconSize(iconSize());
    button->setDefaultAction(action);
    action->setDefaultWidget(button);
    insertAction(before, action);
    connect(this, &QToolBar::iconSizeChanged, button, &QToolButton::setIconSize);
    button->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(button, &QWidget::customContextMenuRequested, this, [this, button, action](const QPoint& pos) {
        const QString id = action->data().toString();
        if (actionsById.value(id) == action)
            emit entryContextMenuRequested(id, button->mapToGlobal(pos));
    });
    return action;
}

bool ContextRail::addEntry(const ContextRailEntry& entry)
{
    const QString id = entry.id.trimmed();
    if (id.isEmpty() || actionsById.contains(id))
        return false;

    QAction* footer = findChild<QAction*>(QStringLiteral("contextFloatingFooter"));
    QAction* action = addRailAction(entry.icon, entry.title, footer);
    action->setObjectName(
        QStringLiteral("contextRail.%1").arg(id));
    action->setCheckable(true);
    action->setData(id);
    action->setToolTip(
        entry.toolTip.isEmpty() ? entry.title : entry.toolTip);
    connect(action,
            &QAction::triggered,
            this,
            [this, id]() { emit entryActivated(id); });
    actionsById.insert(id, action);
    updateVisibility();
    return true;
}

bool ContextRail::removeEntry(const QString& id)
{
    const QString normalized = id.trimmed();
    QAction* action = actionsById.take(normalized);
    if (!action)
        return false;
    removeAction(action);
    action->deleteLater();
    if (activeId == normalized)
        activeId.clear();
    updateVisibility();
    return true;
}

bool ContextRail::setEntryIcon(
    const QString& id,
    const QIcon& icon)
{
    QAction* action = actionsById.value(id.trimmed(), nullptr);
    if (!action)
        return false;
    action->setIcon(icon);
    return true;
}

void ContextRail::clearEntries()
{
    const QList<QAction*> entries = actionsById.values();
    actionsById.clear();
    activeId.clear();
    for (QAction* action : entries) {
        removeAction(action);
        action->deleteLater();
    }
    updateVisibility();
}

QStringList ContextRail::entryIds() const
{
    QStringList result;
    for (QAction* action : actions()) {
        if (action && actionsById.contains(action->data().toString()))
            result.append(action->data().toString());
    }
    return result;
}

QString ContextRail::activeEntryId() const
{
    return activeId;
}

void ContextRail::setActiveEntryId(const QString& id)
{
    const QString normalized = id.trimmed();
    activeId = actionsById.contains(normalized)
        ? normalized
        : QString();
    for (auto it = actionsById.cbegin();
         it != actionsById.cend();
         ++it) {
        if (it.value())
            it.value()->setChecked(it.key() == activeId);
    }
}

void ContextRail::updateVisibility()
{
    setVisible(!actionsById.isEmpty());
}
