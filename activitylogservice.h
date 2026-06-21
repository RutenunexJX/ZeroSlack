#ifndef ACTIVITYLOGSERVICE_H
#define ACTIVITYLOGSERVICE_H

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
};

Q_DECLARE_METATYPE(ActivityLogEvent)

class ActivityLogService : public QObject
{
    Q_OBJECT

public:
    static ActivityLogService* getInstance();

    void append(const QString& source,
                ActivityLogLevel level,
                const QString& message,
                int durationMs = -1,
                const QString& correlationId = QString());
    void clear();

    QList<ActivityLogEvent> events() const;

    static QString levelDisplayName(ActivityLogLevel level);
    static QString formatEvent(const ActivityLogEvent& event);

signals:
    void eventAppended(const ActivityLogEvent& event);
    void cleared();

private:
    explicit ActivityLogService(QObject* parent = nullptr);

    mutable QMutex mutex;
    QList<ActivityLogEvent> eventList;
};

#endif // ACTIVITYLOGSERVICE_H
