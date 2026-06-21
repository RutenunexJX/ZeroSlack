#include "activitylogservice.h"

#include <QMutexLocker>

namespace {
constexpr int kMaxActivityLogEvents = 2000;
}

ActivityLogService::ActivityLogService(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<ActivityLogEvent>("ActivityLogEvent");
}

ActivityLogService* ActivityLogService::getInstance()
{
    static ActivityLogService service;
    return &service;
}

void ActivityLogService::append(const QString& source,
                                ActivityLogLevel level,
                                const QString& message,
                                int durationMs,
                                const QString& correlationId)
{
    ActivityLogEvent event;
    event.timestamp = QDateTime::currentDateTime();
    event.source = source;
    event.level = level;
    event.message = message;
    event.durationMs = durationMs;
    event.correlationId = correlationId;

    {
        QMutexLocker locker(&mutex);
        eventList.append(event);
        while (eventList.size() > kMaxActivityLogEvents)
            eventList.removeFirst();
    }

    emit eventAppended(event);
}

void ActivityLogService::clear()
{
    QMutexLocker locker(&mutex);
    eventList.clear();
    locker.unlock();
    emit cleared();
}

QList<ActivityLogEvent> ActivityLogService::events() const
{
    QMutexLocker locker(&mutex);
    return eventList;
}

QString ActivityLogService::levelDisplayName(ActivityLogLevel level)
{
    switch (level) {
    case ActivityLogLevel::Trace:
        return QStringLiteral("Trace");
    case ActivityLogLevel::Info:
        return QStringLiteral("Info");
    case ActivityLogLevel::Warning:
        return QStringLiteral("Warning");
    case ActivityLogLevel::Error:
        return QStringLiteral("Error");
    }
    return QStringLiteral("Info");
}

QString ActivityLogService::formatEvent(const ActivityLogEvent& event)
{
    QString line = QStringLiteral("[%1] [%2] [%3] %4")
                       .arg(event.timestamp.time().toString(QStringLiteral("HH:mm:ss.zzz")),
                            event.source,
                            levelDisplayName(event.level),
                            event.message);
    if (event.durationMs >= 0)
        line += QStringLiteral(" (%1 ms)").arg(event.durationMs);
    if (!event.correlationId.isEmpty())
        line += QStringLiteral(" #%1").arg(event.correlationId);
    return line;
}
