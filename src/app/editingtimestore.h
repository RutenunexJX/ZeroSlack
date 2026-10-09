#pragma once

#include "zeroslackexport.h"
#include <QList>
#include <QString>

struct EditingTimeInterval {
    qint64 start = 0;
    qint64 end = 0;
};

struct EditingTimeSnapshot {
    QString generation = QStringLiteral("initial");
    qint64 resetAt = 0;
    qint64 total = 0;
};

struct EditingTimeSaveResult {
    bool ok = false;
    EditingTimeSnapshot snapshot;
    QString error;
};

// Additive transactions, never a last-writer-wins copy of an instance's total.
class ZEROSLACK_API EditingTimeStore {
public:
    explicit EditingTimeStore(QString path);
    EditingTimeSaveResult synchronize(const QString& generation,
                                     const QList<EditingTimeInterval>& intervals) const;
    EditingTimeSaveResult reset(qint64 now) const;
    QString path() const { return filePath; }

private:
    EditingTimeSaveResult transact(const QString& generation,
                                  const QList<EditingTimeInterval>& intervals,
                                  bool reset, qint64 now) const;
    QString filePath;
};
