#include "notificationcenter.h"

#include <QMutexLocker>
#include <QSet>

#include <algorithm>
#include <utility>

namespace {
bool actionsEqual(const QList<NotificationAction>& lhs,
                  const QList<NotificationAction>& rhs)
{
    if (lhs.size() != rhs.size())
        return false;

    for (qsizetype index = 0; index < lhs.size(); ++index) {
        if (lhs.at(index).id != rhs.at(index).id
            || lhs.at(index).label != rhs.at(index).label) {
            return false;
        }
    }
    return true;
}
}

NotificationCenter::NotificationCenter(QObject* parent)
    : NotificationCenter(kDefaultCapacity, parent)
{
}

NotificationCenter::NotificationCenter(int capacity, QObject* parent)
    : QObject(parent)
    , maximumItems(std::max(1, capacity))
{
    qRegisterMetaType<NotificationItem>("NotificationItem");
    qRegisterMetaType<NotificationRemovalReason>(
        "NotificationRemovalReason");
}

int NotificationCenter::capacity() const
{
    QMutexLocker locker(&mutex);
    return maximumItems;
}

void NotificationCenter::setCapacity(int capacity)
{
    const int normalizedCapacity = std::max(1, capacity);
    QList<NotificationItem> removedItems;
    {
        QMutexLocker locker(&mutex);
        if (maximumItems == normalizedCapacity)
            return;

        maximumItems = normalizedCapacity;
        while (items.size() > maximumItems)
            removedItems.append(items.takeFirst());
    }

    emit capacityChanged(normalizedCapacity);
    for (const NotificationItem& item : removedItems)
        emit notificationRemoved(item, NotificationRemovalReason::Capacity);
}

int NotificationCenter::size() const
{
    QMutexLocker locker(&mutex);
    return items.size();
}

bool NotificationCenter::isEmpty() const
{
    QMutexLocker locker(&mutex);
    return items.isEmpty();
}

QList<NotificationItem> NotificationCenter::notifications() const
{
    QMutexLocker locker(&mutex);
    return items;
}

bool NotificationCenter::notificationById(const QString& id,
                                          NotificationItem* item) const
{
    QMutexLocker locker(&mutex);
    const int index = indexOfId(id);
    if (index < 0)
        return false;
    if (item)
        *item = items.at(index);
    return true;
}

bool NotificationCenter::notificationByKey(const QString& key,
                                           NotificationItem* item) const
{
    if (key.isEmpty())
        return false;

    QMutexLocker locker(&mutex);
    const int index = indexOfKey(key);
    if (index < 0)
        return false;
    if (item)
        *item = items.at(index);
    return true;
}

NotificationPostResult NotificationCenter::post(
    const NotificationDraft& draft)
{
    const QList<NotificationAction> actions =
        normalizedActions(draft.actions);
    NotificationItem changedItem;
    QList<NotificationItem> removedItems;
    NotificationPostDisposition disposition =
        NotificationPostDisposition::Added;

    {
        QMutexLocker locker(&mutex);
        const int existingIndex =
            draft.key.isEmpty() ? -1 : indexOfKey(draft.key);
        if (existingIndex >= 0) {
            NotificationItem& existing = items[existingIndex];
            if (payloadEquals(existing, draft, actions)) {
                return {existing.id,
                        NotificationPostDisposition::Duplicate};
            }

            existing.topic = draft.topic;
            existing.severity = draft.severity;
            existing.source = draft.source;
            existing.message = draft.message;
            existing.actions = actions;
            ++existing.revision;
            changedItem = existing;
            disposition = NotificationPostDisposition::Updated;
        } else {
            NotificationItem item;
            item.id =
                QStringLiteral("zs.notification.%1").arg(nextId++);
            item.key = draft.key;
            item.topic = draft.topic;
            item.severity = draft.severity;
            item.source = draft.source;
            item.message = draft.message;
            item.actions = actions;
            item.revision = 1;
            item.sequence = nextSequence++;
            items.append(item);
            changedItem = item;

            while (items.size() > maximumItems)
                removedItems.append(items.takeFirst());
        }
    }

    for (const NotificationItem& item : removedItems)
        emit notificationRemoved(item, NotificationRemovalReason::Capacity);

    if (disposition == NotificationPostDisposition::Added)
        emit notificationAdded(changedItem);
    else
        emit notificationUpdated(changedItem);
    return {changedItem.id, disposition};
}

bool NotificationCenter::dismiss(const QString& id)
{
    NotificationItem removedItem;
    {
        QMutexLocker locker(&mutex);
        const int index = indexOfId(id);
        if (index < 0)
            return false;
        removedItem = items.takeAt(index);
    }

    emit notificationRemoved(removedItem,
                             NotificationRemovalReason::Dismissed);
    return true;
}

bool NotificationCenter::dismissByKey(const QString& key)
{
    if (key.isEmpty())
        return false;

    NotificationItem removedItem;
    {
        QMutexLocker locker(&mutex);
        const int index = indexOfKey(key);
        if (index < 0)
            return false;
        removedItem = items.takeAt(index);
    }

    emit notificationRemoved(removedItem,
                             NotificationRemovalReason::Dismissed);
    return true;
}

void NotificationCenter::clear()
{
    QList<NotificationItem> removedItems;
    {
        QMutexLocker locker(&mutex);
        if (items.isEmpty())
            return;
        removedItems.swap(items);
    }

    for (const NotificationItem& item : removedItems)
        emit notificationRemoved(item, NotificationRemovalReason::Cleared);
}

bool NotificationCenter::requestAction(const QString& notificationId,
                                       const QString& actionId)
{
    bool found = false;
    {
        QMutexLocker locker(&mutex);
        const int index = indexOfId(notificationId);
        if (index < 0)
            return false;

        const QList<NotificationAction>& actions = items.at(index).actions;
        for (const NotificationAction& action : actions) {
            if (action.id == actionId) {
                found = true;
                break;
            }
        }
    }

    if (!found)
        return false;
    emit actionRequested(notificationId, actionId);
    return true;
}

QList<NotificationAction> NotificationCenter::normalizedActions(
    const QList<NotificationAction>& actions)
{
    QList<NotificationAction> normalized;
    QSet<QString> seenIds;
    normalized.reserve(actions.size());
    for (NotificationAction action : actions) {
        action.id = action.id.trimmed();
        action.label = action.label.trimmed();
        if (action.id.isEmpty() || seenIds.contains(action.id))
            continue;
        if (action.label.isEmpty())
            action.label = action.id;
        seenIds.insert(action.id);
        normalized.append(std::move(action));
    }
    return normalized;
}

bool NotificationCenter::payloadEquals(
    const NotificationItem& item,
    const NotificationDraft& draft,
    const QList<NotificationAction>& actions)
{
    return item.topic == draft.topic
           && item.severity == draft.severity
           && item.source == draft.source
           && item.message == draft.message
           && actionsEqual(item.actions, actions);
}

int NotificationCenter::indexOfId(const QString& id) const
{
    for (qsizetype index = 0; index < items.size(); ++index) {
        if (items.at(index).id == id)
            return static_cast<int>(index);
    }
    return -1;
}

int NotificationCenter::indexOfKey(const QString& key) const
{
    for (qsizetype index = 0; index < items.size(); ++index) {
        if (items.at(index).key == key)
            return static_cast<int>(index);
    }
    return -1;
}
