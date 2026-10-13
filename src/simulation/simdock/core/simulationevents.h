#pragma once
#include <QByteArray>
#include <QList>
#include <QString>
#include <QVariantMap>

namespace simdock
{
struct SimulationEvent
{
    QString runId, timePs, level, tag, instance, sourceFile, message;
    int sourceLine = 0;
    QList<QVariantMap> values;
};

class SimulationEventDecoder
{
  public:
    void reset(const QString &runId);
    QList<SimulationEvent> feed(const QByteArray &bytes);
    quint64 rejectedRecords() const
    {
        return rejected;
    }

  private:
    QString run;
    QByteArray pending;
    bool active = false, dropping = false;
    quint64 rejected = 0;
    void consume(QByteArray line, QList<SimulationEvent> &result);
};
} // namespace simdock
Q_DECLARE_METATYPE(simdock::SimulationEvent)
Q_DECLARE_METATYPE(QList<simdock::SimulationEvent>)
