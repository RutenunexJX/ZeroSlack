#include "editingtimestore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QUuid>
#include <limits>

EditingTimeStore::EditingTimeStore(QString path) : filePath(std::move(path)) {}

EditingTimeSaveResult EditingTimeStore::synchronize(
    const QString& generation, const QList<EditingTimeInterval>& intervals) const
{
    return transact(generation, intervals, false, 0);
}

EditingTimeSaveResult EditingTimeStore::reset(qint64 now) const
{
    return transact({}, {}, true, now);
}

EditingTimeSaveResult EditingTimeStore::transact(
    const QString& generation, const QList<EditingTimeInterval>& intervals,
    bool resetRequested, qint64 now) const
{
    EditingTimeSaveResult result;
    const auto fail = [&result](const QString& detail) {
        result.error = detail;
        return result;
    };
    if (filePath.isEmpty() || !QDir().mkpath(QFileInfo(filePath).absolutePath()))
        return fail(QStringLiteral("Cannot create the editing-time storage directory."));
    QLockFile lock(filePath + QStringLiteral(".lock"));
    if (!lock.tryLock(0))
        return fail(QStringLiteral("Editing-time storage is locked or unavailable."));

    QFile existing(filePath);
    if (existing.exists()) {
        if (!existing.open(QIODevice::ReadOnly))
            return fail(existing.errorString());
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(existing.readAll(), &error);
        if (existing.error() != QFileDevice::NoError)
            return fail(existing.errorString());
        const QJsonObject object = document.object();
        bool validTotal = false, validReset = false;
        result.snapshot.total = object.value(QStringLiteral("totalNs")).toString().toLongLong(&validTotal);
        result.snapshot.resetAt = object.value(QStringLiteral("resetAtNs")).toString().toLongLong(&validReset);
        result.snapshot.generation = object.value(QStringLiteral("generation")).toString();
        if (error.error != QJsonParseError::NoError || !document.isObject()
            || object.value(QStringLiteral("version")).toInt() != 1
            || !validTotal || !validReset || result.snapshot.total < 0
            || result.snapshot.resetAt < 0 || result.snapshot.generation.isEmpty())
            return fail(QStringLiteral("Editing-time storage is invalid; its contents were preserved."));
        existing.close();
    }

    qint64 increment = 0;
    for (const auto& interval : intervals) {
        // A live older instance uses the same monotonic clock on this machine.
        // Trim its pending input at a newer reset, including a held key spanning it.
        const qint64 start = generation == result.snapshot.generation
            ? interval.start : qMax(interval.start, result.snapshot.resetAt);
        const qint64 duration = qMax(qint64(0), interval.end - start);
        if (duration > std::numeric_limits<qint64>::max() - increment)
            return fail(QStringLiteral("Editing-time duration is out of range."));
        increment += duration;
    }
    if (resetRequested) {
        result.snapshot = {QUuid::createUuid().toString(QUuid::WithoutBraces), now, 0};
    } else {
        if (increment > std::numeric_limits<qint64>::max() - result.snapshot.total)
            return fail(QStringLiteral("Editing-time total is out of range."));
        result.snapshot.total += increment;
    }
    if (resetRequested || increment > 0) {
        const QJsonObject object{
            {QStringLiteral("version"), 1},
            {QStringLiteral("generation"), result.snapshot.generation},
            {QStringLiteral("resetAtNs"), QString::number(result.snapshot.resetAt)},
            {QStringLiteral("totalNs"), QString::number(result.snapshot.total)}};
        const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
        QSaveFile output(filePath);
        if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size()
            || !output.commit())
            return fail(output.errorString());
    }
    result.ok = true;
    return result;
}
