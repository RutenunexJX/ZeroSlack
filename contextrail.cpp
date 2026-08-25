#include "contextrail.h"

#include <QAction>

ContextRail::ContextRail(QWidget* parent)
    : QToolBar(parent)
{
    setObjectName(QStringLiteral("contextRail"));
    setWindowTitle(tr("Context"));
    setOrientation(Qt::Vertical);
    setMovable(false);
    setFloatable(false);
    setToolButtonStyle(Qt::ToolButtonIconOnly);
    setIconSize(QSize(18, 18));
    setVisible(false);
}

bool ContextRail::addEntry(const ContextRailEntry& entry)
{
    const QString id = entry.id.trimmed();
    if (id.isEmpty() || actionsById.contains(id))
        return false;

    QAction* action = addAction(entry.icon, entry.title);
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
    QAction* action = actionsById.take(id.trimmed());
    if (!action)
        return false;
    removeAction(action);
    action->deleteLater();
    if (activeId == id)
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
