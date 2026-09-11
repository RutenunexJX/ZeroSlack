#ifndef ACTIVITYLOGSERVICE_H
#define ACTIVITYLOGSERVICE_H

#include "zeroslackexport.h"

#include <QDateTime>
#include <QList>
#include <QMetaType>
#include <QMutex>
#include <QObject>
#include <QString>

enum class ActivityLogLevel {
    Trace,
    Info,
    Warning,
    Error,
};

struct ActivityLogEvent
{
    QDateTime timestamp;
    QString source;
    ActivityLogLevel level = ActivityLogLevel::Info;
    QString message;
    int durationMs = -1;
    QString correlationId;
    quint64 sequence = 0;
    bool requiresAttention = false;
};

Q_DECLARE_METATYPE(ActivityLogEvent)

class ZEROSLACK_API ActivityLogService : public QObject
{
    Q_OBJECT

public:
    static ActivityLogService* getInstance();

    void append(const QString& source,
                ActivityLogLevel level,
                const QString& message,
                int durationMs = -1,
                const QString& correlationId = QString(),
                bool requiresAttention = false);
    void clear();
    void markReadThrough(quint64 sequence);
    int unreadCount() const;
    ActivityLogLevel unreadLevel() const;

    QList<ActivityLogEvent> events() const;

    static QString levelDisplayName(ActivityLogLevel level);
    static QString formatEvent(const ActivityLogEvent& event);

signals:
    void eventAppended(const ActivityLogEvent& event);
    void cleared();
    void unreadChanged();

private:
    explicit ActivityLogService(QObject* parent = nullptr);

    mutable QMutex mutex;
    QList<ActivityLogEvent> eventList;
    quint64 nextSequence = 1;
    quint64 readThrough = 0;
};

#endif // ACTIVITYLOGSERVICE_H
