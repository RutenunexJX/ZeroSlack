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
                                const QString& correlationId,
                                bool requiresAttention)
{
    if (message.trimmed().isEmpty())
        return;
    ActivityLogEvent event;
    event.timestamp = QDateTime::currentDateTime();
    event.source = source;
    event.level = level;
    event.message = message;
    event.durationMs = durationMs;
    event.correlationId = correlationId;
    event.requiresAttention = requiresAttention
        || level == ActivityLogLevel::Warning || level == ActivityLogLevel::Error;

    {
        QMutexLocker locker(&mutex);
        event.sequence = nextSequence++;
        eventList.append(event);
        while (eventList.size() > kMaxActivityLogEvents)
            eventList.removeFirst();
    }

    emit eventAppended(event);
    emit unreadChanged();
}

int ActivityLogService::unreadCount() const
{
    QMutexLocker locker(&mutex);
    int count = 0;
    for (const auto& event : eventList) {
        if (event.requiresAttention && event.sequence > readThrough)
            ++count;
    }
    return count;
}

void ActivityLogService::markReadThrough(quint64 sequence)
{
    {
        QMutexLocker locker(&mutex);
        const quint64 bounded = qMin(sequence, nextSequence - 1);
        if (bounded <= readThrough)
            return;
        readThrough = bounded;
    }
    emit unreadChanged();
}

ActivityLogLevel ActivityLogService::unreadLevel() const
{
    QMutexLocker locker(&mutex);
    ActivityLogLevel level = ActivityLogLevel::Info;
    for (const auto& event : eventList) {
        if (event.requiresAttention && event.sequence > readThrough
            && static_cast<int>(event.level) > static_cast<int>(level))
            level = event.level;
    }
    return level;
}

void ActivityLogService::clear()
{
    QMutexLocker locker(&mutex);
    eventList.clear();
    readThrough = nextSequence - 1;
    locker.unlock();
    emit cleared();
    emit unreadChanged();
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
