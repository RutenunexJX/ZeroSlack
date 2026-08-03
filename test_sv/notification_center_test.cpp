#include "notificationcenter.h"

#include <QCoreApplication>
#include <QList>
#include <QString>

#include <cstdio>

namespace {
int checks = 0;
int failures = 0;

void expect(const char* name, bool value)
{
    ++checks;
    if (!value)
        ++failures;
    std::printf("[%s] %s\n", value ? "PASS" : "FAIL", name);
}

NotificationDraft failureDraft(const QString& key,
                               NotificationTopic topic,
                               const QString& source,
                               const QString& message)
{
    NotificationDraft draft;
    draft.key = key;
    draft.topic = topic;
    draft.severity = NotificationSeverity::Error;
    draft.source = source;
    draft.message = message;
    return draft;
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    NotificationCenter center;
    int added = 0;
    int updated = 0;
    int removed = 0;
    int actionRequests = 0;
    NotificationItem lastChanged;
    NotificationRemovalReason lastRemovalReason =
        NotificationRemovalReason::Dismissed;
    QString requestedNotification;
    QString requestedAction;

    QObject::connect(&center,
                     &NotificationCenter::notificationAdded,
                     [&](const NotificationItem& item) {
                         ++added;
                         lastChanged = item;
                     });
    QObject::connect(&center,
                     &NotificationCenter::notificationUpdated,
                     [&](const NotificationItem& item) {
                         ++updated;
                         lastChanged = item;
                     });
    QObject::connect(&center,
                     &NotificationCenter::notificationRemoved,
                     [&](const NotificationItem&,
                         NotificationRemovalReason reason) {
                         ++removed;
                         lastRemovalReason = reason;
                     });
    QObject::connect(&center,
                     &NotificationCenter::actionRequested,
                     [&](const QString& notificationId,
                         const QString& actionId) {
                         ++actionRequests;
                         requestedNotification = notificationId;
                         requestedAction = actionId;
                     });

    NotificationDraft save =
        failureDraft(QStringLiteral("save:C:/rtl/top.sv"),
                     NotificationTopic::Save,
                     QStringLiteral("DocumentSave"),
                     QStringLiteral("Could not write top.sv"));
    save.actions = {
        {QStringLiteral(" retry "), QStringLiteral(" Retry ")},
        {QStringLiteral("retry"), QStringLiteral("Duplicate")},
        {QString(), QStringLiteral("Invalid")},
        {QStringLiteral("details"), QString()},
    };

    const NotificationPostResult first = center.post(save);
    expect("first keyed notification is added",
           first.disposition == NotificationPostDisposition::Added
               && !first.id.isEmpty() && center.size() == 1 && added == 1
               && updated == 0 && removed == 0);
    expect("notification carries save failure payload",
           lastChanged.id == first.id
               && lastChanged.key == save.key
               && lastChanged.topic == NotificationTopic::Save
               && lastChanged.severity == NotificationSeverity::Error
               && lastChanged.source == QStringLiteral("DocumentSave")
               && lastChanged.message
                      == QStringLiteral("Could not write top.sv")
               && lastChanged.revision == 1
               && lastChanged.sequence == 1);
    expect("actions are normalized and de-duplicated",
           lastChanged.actions.size() == 2
               && lastChanged.actions.at(0).id == QStringLiteral("retry")
               && lastChanged.actions.at(0).label
                      == QStringLiteral("Retry")
               && lastChanged.actions.at(1).id == QStringLiteral("details")
               && lastChanged.actions.at(1).label
                      == QStringLiteral("details"));

    const NotificationPostResult duplicate = center.post(save);
    NotificationItem storedSave;
    expect("identical key and payload is a no-op duplicate",
           duplicate.disposition
                   == NotificationPostDisposition::Duplicate
               && duplicate.id == first.id && center.size() == 1
               && added == 1 && updated == 0
               && center.notificationByKey(save.key, &storedSave)
               && storedSave.revision == 1);

    save.severity = NotificationSeverity::Critical;
    save.message = QStringLiteral("Disk rejected the save");
    const NotificationPostResult changed = center.post(save);
    expect("changed payload updates the keyed notification in place",
           changed.disposition == NotificationPostDisposition::Updated
               && changed.id == first.id && center.size() == 1
               && added == 1 && updated == 1
               && lastChanged.id == first.id
               && lastChanged.revision == 2
               && lastChanged.sequence == 1
               && lastChanged.severity == NotificationSeverity::Critical);

    expect("valid action request is non-mutating",
           center.requestAction(first.id, QStringLiteral("retry"))
               && actionRequests == 1
               && requestedNotification == first.id
               && requestedAction == QStringLiteral("retry")
               && center.size() == 1
               && center.notificationById(first.id, &storedSave)
               && storedSave.revision == 2);
    expect("unknown action is rejected without side effects",
           !center.requestAction(first.id, QStringLiteral("missing"))
               && !center.requestAction(QStringLiteral("missing"),
                                        QStringLiteral("retry"))
               && actionRequests == 1 && center.size() == 1);

    const QList<NotificationDraft> operationalFailures = {
        failureDraft(QStringLiteral("analysis:workspace"),
                     NotificationTopic::Analysis,
                     QStringLiteral("Analysis"),
                     QStringLiteral("Elaboration failed")),
        failureDraft(QStringLiteral("external:C:/rtl/top.sv"),
                     NotificationTopic::ExternalModification,
                     QStringLiteral("FileWatcher"),
                     QStringLiteral("File changed outside ZeroSlack")),
        failureDraft(QStringLiteral("transaction:rename-port"),
                     NotificationTopic::TransactionConflict,
                     QStringLiteral("WorkspaceEdit"),
                     QStringLiteral("Document revision changed")),
    };
    for (const NotificationDraft& draft : operationalFailures)
        center.post(draft);

    const QList<NotificationItem> operationalItems =
        center.notifications();
    expect("save analysis external and transaction failures are expressible",
           operationalItems.size() == 4
               && operationalItems.at(0).topic == NotificationTopic::Save
               && operationalItems.at(1).topic
                      == NotificationTopic::Analysis
               && operationalItems.at(2).topic
                      == NotificationTopic::ExternalModification
               && operationalItems.at(3).topic
                      == NotificationTopic::TransactionConflict);
    expect("posting does not request actions or remove notifications",
           actionRequests == 1 && removed == 0 && center.size() == 4);

    const QString externalKey = operationalFailures.at(1).key;
    expect("dismiss by key is explicit and reports its reason",
           center.dismissByKey(externalKey)
               && !center.dismissByKey(externalKey)
               && lastRemovalReason
                      == NotificationRemovalReason::Dismissed
               && removed == 1 && center.size() == 3);
    expect("dismiss by stable id is explicit",
           center.dismiss(first.id) && !center.dismiss(first.id)
               && removed == 2 && center.size() == 2);

    NotificationCenter bounded(2);
    QList<NotificationItem> capacityRemovals;
    QObject::connect(&bounded,
                     &NotificationCenter::notificationRemoved,
                     [&](const NotificationItem& item,
                         NotificationRemovalReason reason) {
                         if (reason == NotificationRemovalReason::Capacity)
                             capacityRemovals.append(item);
                     });
    NotificationDraft a;
    a.key = QStringLiteral("a");
    a.message = QStringLiteral("A");
    NotificationDraft b;
    b.key = QStringLiteral("b");
    b.message = QStringLiteral("B");
    NotificationDraft c;
    c.key = QStringLiteral("c");
    c.message = QStringLiteral("C");
    const QString aId = bounded.post(a).id;
    const QString bId = bounded.post(b).id;
    a.message = QStringLiteral("A updated");
    bounded.post(a);
    const QString cId = bounded.post(c).id;
    const QList<NotificationItem> boundedItems = bounded.notifications();
    expect("updates retain insertion order for deterministic eviction",
           capacityRemovals.size() == 1
               && capacityRemovals.constFirst().id == aId
               && boundedItems.size() == 2
               && boundedItems.at(0).id == bId
               && boundedItems.at(1).id == cId);

    int capacityChanges = 0;
    QObject::connect(&bounded,
                     &NotificationCenter::capacityChanged,
                     [&](int) { ++capacityChanges; });
    bounded.setCapacity(0);
    expect("capacity has a predictable minimum of one",
           bounded.capacity() == 1 && bounded.size() == 1
               && bounded.notifications().constFirst().id == cId
               && capacityChanges == 1
               && capacityRemovals.size() == 2
               && capacityRemovals.constLast().id == bId);
    bounded.setCapacity(-8);
    expect("equivalent normalized capacity is a no-op",
           bounded.capacity() == 1 && capacityChanges == 1
               && capacityRemovals.size() == 2);

    NotificationDraft unkeyed;
    unkeyed.message = QStringLiteral("Unkeyed");
    const QString firstUnkeyed = center.post(unkeyed).id;
    const QString secondUnkeyed = center.post(unkeyed).id;
    expect("unkeyed notifications remain independent",
           firstUnkeyed != secondUnkeyed && center.size() == 4);

    const int removedBeforeClear = removed;
    center.clear();
    expect("clear is explicit and reports every removed item",
           center.isEmpty()
               && removed == removedBeforeClear + 4
               && lastRemovalReason == NotificationRemovalReason::Cleared);
    center.clear();
    expect("clearing an empty center is a no-op",
           removed == removedBeforeClear + 4);

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
