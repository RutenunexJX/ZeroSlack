#ifndef NOTIFICATIONCENTER_H
#define NOTIFICATIONCENTER_H

#include "zeroslackexport.h"

#include <QList>
#include <QMetaType>
#include <QMutex>
#include <QObject>
#include <QString>

enum class NotificationSeverity {
    Information,
    Warning,
    Error,
    Critical,
};

enum class NotificationTopic {
    General,
    Save,
    Analysis,
    ExternalModification,
    TransactionConflict,
};

enum class NotificationPostDisposition {
    Added,
    Updated,
    Duplicate,
};

enum class NotificationRemovalReason {
    Dismissed,
    Capacity,
    Cleared,
};

struct NotificationAction
{
    QString id;
    QString label;
};

struct NotificationDraft
{
    QString key;
    NotificationTopic topic = NotificationTopic::General;
    NotificationSeverity severity = NotificationSeverity::Information;
    QString source;
    QString message;
    QList<NotificationAction> actions;
};

struct NotificationItem
{
    QString id;
    QString key;
    NotificationTopic topic = NotificationTopic::General;
    NotificationSeverity severity = NotificationSeverity::Information;
    QString source;
    QString message;
    QList<NotificationAction> actions;
    quint64 revision = 0;
    quint64 sequence = 0;
};

struct NotificationPostResult
{
    QString id;
    NotificationPostDisposition disposition =
        NotificationPostDisposition::Added;
};

Q_DECLARE_METATYPE(NotificationSeverity)
Q_DECLARE_METATYPE(NotificationTopic)
Q_DECLARE_METATYPE(NotificationPostDisposition)
Q_DECLARE_METATYPE(NotificationRemovalReason)
Q_DECLARE_METATYPE(NotificationAction)
Q_DECLARE_METATYPE(NotificationDraft)
Q_DECLARE_METATYPE(NotificationItem)
Q_DECLARE_METATYPE(NotificationPostResult)

class ZEROSLACK_API NotificationCenter final : public QObject
{
    Q_OBJECT

public:
    explicit NotificationCenter(QObject* parent = nullptr);
    explicit NotificationCenter(int capacity, QObject* parent = nullptr);

    int capacity() const;
    void setCapacity(int capacity);

    int size() const;
    bool isEmpty() const;
    QList<NotificationItem> notifications() const;
    bool notificationById(const QString& id, NotificationItem* item) const;
    bool notificationByKey(const QString& key, NotificationItem* item) const;

    NotificationPostResult post(const NotificationDraft& draft);
    bool dismiss(const QString& id);
    bool dismissByKey(const QString& key);
    void clear();

    // Requests execution without mutating or dismissing the notification.
    bool requestAction(const QString& notificationId,
                       const QString& actionId);

signals:
    void notificationAdded(const NotificationItem& item);
    void notificationUpdated(const NotificationItem& item);
    void notificationRemoved(const NotificationItem& item,
                             NotificationRemovalReason reason);
    void actionRequested(const QString& notificationId,
                         const QString& actionId);
    void capacityChanged(int capacity);

private:
    static constexpr int kDefaultCapacity = 100;

    mutable QMutex mutex;
    QList<NotificationItem> items;
    int maximumItems = kDefaultCapacity;
    quint64 nextId = 1;
    quint64 nextSequence = 1;

    static QList<NotificationAction> normalizedActions(
        const QList<NotificationAction>& actions);
    static bool payloadEquals(const NotificationItem& item,
                              const NotificationDraft& draft,
                              const QList<NotificationAction>& actions);
    int indexOfId(const QString& id) const;
    int indexOfKey(const QString& key) const;
};

#endif // NOTIFICATIONCENTER_H
