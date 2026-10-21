#include "simulationevents.h"
#include <QRegularExpression>
#include <QStringDecoder>

namespace simdock
{
namespace
{
QString decoded(const QByteArray &hex, bool &valid, bool nativeFileName = false)
{
    if (hex.size() % 2)
        valid = false;
    for (const auto c : hex)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            valid = false;
    if (!valid)
        return {};
    QStringDecoder utf8(QStringDecoder::Utf8);
    const auto bytes = QByteArray::fromHex(hex);
    QString value = utf8.decode(bytes);
#ifdef Q_OS_WIN
    // Questa's __FILE__ is encoded using the Windows system code page. Other
    // event fields retain the protocol's strict UTF-8 validation.
    if (utf8.hasError() && nativeFileName) {
        QStringDecoder local(QStringDecoder::System);
        value = local.decode(bytes);
        valid &= !local.hasError();
        return value;
    }
#else
    Q_UNUSED(nativeFileName);
#endif
    valid &= !utf8.hasError();
    return value;
}
} // namespace
void SimulationEventDecoder::reset(const QString &runId)
{
    run = runId;
    pending.clear();
    active = dropping = false;
    rejected = 0;
}
QList<SimulationEvent> SimulationEventDecoder::feed(const QByteArray &bytes)
{
    QList<SimulationEvent> result;
    constexpr int limit = 1024 * 1024;
    for (const auto c : bytes)
    {
        if (c == '\n')
        {
            if (!dropping)
                consume(pending, result);
            pending.clear();
            dropping = false;
        }
        else if (!dropping)
        {
            pending += c;
            if (pending.size() > limit)
            {
                pending.clear();
                dropping = true;
                ++rejected;
            }
        }
    }
    return result;
}
void SimulationEventDecoder::consume(QByteArray line, QList<SimulationEvent> &result)
{
    if (line.endsWith('\r'))
        line.chop(1);
    if (line.startsWith("# "))
        line.remove(0, 2);
    if (line.startsWith("ZS_RUN_V1|"))
    {
        active = QString::fromUtf8(line.mid(10)) == run;
        return;
    }
    if (!active || !line.startsWith("ZS_EVT_V1|"))
        return;
    const auto fields = line.split('|');
    if (fields.size() != 9)
    {
        ++rejected;
        return;
    }
    SimulationEvent event;
    event.runId = run;
    event.timePs = QString::fromLatin1(fields[1]);
    event.level = QString::fromLatin1(fields[2]);
    bool valid =
        QRegularExpression(QStringLiteral("^[0-9]+(?:\\.[0-9]+)?$")).match(event.timePs).hasMatch() &&
        (event.level == "Info" || event.level == "Warning" || event.level == "Error");
    event.tag = decoded(fields[3], valid);
    event.instance = decoded(fields[4], valid);
    event.sourceFile = decoded(fields[5], valid, true);
    bool lineValid = false;
    event.sourceLine = fields[6].toInt(&lineValid);
    valid &= lineValid && event.sourceLine > 0;
    event.message = decoded(fields[7], valid);
    const auto values = fields[8].split(',');
    for (const auto &value : values)
    {
        if (value.isEmpty())
            continue;
        const auto parts = value.split(':');
        if (parts.size() != 4)
        {
            valid = false;
            break;
        }
        event.values << QVariantMap{{"name", decoded(parts[0], valid)},
                                    {"value", decoded(parts[1], valid)},
                                    {"radix", decoded(parts[2], valid)},
                                    {"unit", decoded(parts[3], valid)}};
    }
    if (valid)
        result << event;
    else
        ++rejected;
}
} // namespace simdock
