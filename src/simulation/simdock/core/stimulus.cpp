#include "stimulus.h"
#include "scoreboard.h"
#include "testbench.h"
#include "workspace.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <stdexcept>

namespace simdock
{
namespace
{
QString key(const char *value) { return QString::fromLatin1(value); }
[[noreturn]] void fail(const QString &text) { throw std::runtime_error(text.toUtf8().constData()); }
QJsonObject segment(const QString &id, qint64 begin, qint64 end, const QString &value)
{
    return {{key("id"), id},
            {key("startTick"), QString::number(begin)},
            {key("endTick"), QString::number(end)},
            {key("value"), value}};
}
QString signature(const Module &module, const QList<StimulusSignal> &ports)
{
    QJsonArray entries;
    for (const auto &s : ports)
    {
        QJsonObject enums;
        for (auto i = s.enumValues.begin(); i != s.enumValues.end(); ++i)
            enums.insert(i.key(), i.value());
        entries << QJsonObject{{key("name"), s.name},
                               {key("type"), s.type},
                               {key("width"), s.width},
                               {key("signed"), s.isSigned},
                               {key("enum"), enums}};
    }
    QJsonArray params;
    for (const auto &p : module.parameters)
        params << QJsonObject{{key("name"), p.name}, {key("value"), p.value}, {key("type"), p.type}};
    return QString::fromLatin1(
        QCryptographicHash::hash(
            QJsonDocument(QJsonObject{{key("file"), module.file},
                                      {key("module"), module.name},
                                      {key("ports"), entries},
                                      {key("parameters"), params},
                                      {key("imports"), QJsonArray::fromStringList(module.imports)}})
                .toJson(QJsonDocument::Compact),
            QCryptographicHash::Sha256)
            .toHex());
}
qint64 tick(const QJsonValue &value)
{
    bool ok = false;
    const auto n = value.toString().toLongLong(&ok);
    if (!ok || n < 0 || n > 3600000000000000LL)
        fail(key("Invalid stimulus time."));
    return n;
}
QString literal(const StimulusSignal &signal, QString text, const QString &radix)
{
    const bool isEnum = !signal.enumValues.isEmpty();
    if (isEnum && signal.enumValues.contains(text))
        text = signal.enumValues[text];
    else if (isEnum && text.compare(key("x"), Qt::CaseInsensitive) &&
             text.compare(key("z"), Qt::CaseInsensitive))
        fail(QStringLiteral("Choose a declared enum value for %1.").arg(signal.name));
    text = text.toLower().remove(QLatin1Char('_')).trimmed();
    if (text == key("x") || text == key("z"))
        return QStringLiteral("%1'b%2").arg(signal.width).arg(QString(signal.width, text.front()));
    int base =
        isEnum || signal.width == 1 || text.startsWith(QLatin1Char('-')) || text.startsWith(QLatin1Char('+'))
            ? 10
        : radix == key("hexadecimal") ? 16
        : radix == key("binary")      ? 2
        : radix == key("octal")       ? 8
                                      : 10;
    if (text.startsWith(key("0x")))
    {
        base = 16;
        text.remove(0, 2);
    }
    else if (text.startsWith(key("0b")))
    {
        base = 2;
        text.remove(0, 2);
    }
    else if (text.startsWith(key("0o")))
    {
        base = 8;
        text.remove(0, 2);
    }
    if (base != 10 && (text.contains(QLatin1Char('x')) || text.contains(QLatin1Char('z'))))
    {
        const int digits = base == 16 ? 4 : base == 8 ? 3 : 1;
        QString bits;
        for (const auto c : text)
        {
            if (c == QLatin1Char('x') || c == QLatin1Char('z'))
                bits += QString(digits, c);
            else
            {
                bool ok = false;
                const auto number = QString(c).toUInt(&ok, base);
                if (!ok)
                    fail(key("Invalid waveform value: ") + signal.name);
                bits += QString::number(number, 2).rightJustified(digits, QLatin1Char('0'));
            }
        }
        while (bits.size() > signal.width && bits.startsWith(QLatin1Char('0')))
            bits.remove(0, 1);
        if (bits.size() > signal.width && bits.size() - signal.width < digits &&
            (bits.front() == QLatin1Char('x') || bits.front() == QLatin1Char('z')))
            bits = bits.right(signal.width);
        if (bits.size() > signal.width)
            fail(key("Waveform value exceeds input width: ") + signal.name);
        return QStringLiteral("%1'b%2")
            .arg(signal.width)
            .arg(bits.rightJustified(signal.width, signal.isSigned ? bits.front() : QLatin1Char('0')));
    }
    bool ok = false;
    quint64 number = 0;
    if (text.startsWith(QLatin1Char('-')))
    {
        const auto n = text.toLongLong(&ok, base);
        if (!signal.isSigned || (signal.width < 64 && n < -(qint64(1) << (signal.width - 1))))
            ok = false;
        number = quint64(n);
    }
    else
    {
        number = text.toULongLong(&ok, base);
        if (signal.width < 64 && number >= (quint64(1) << signal.width))
            ok = false;
    }
    if (!ok)
        fail(QStringLiteral("Invalid or out-of-range value for %1.").arg(signal.name));
    if (signal.isSigned && base != 10)
    {
        const auto writtenBits = text.size() * (base == 16 ? 4 : base == 8 ? 3 : 1);
        if (writtenBits > 0 && writtenBits < signal.width && (number & (quint64(1) << (writtenBits - 1))))
            number |= ~((quint64(1) << writtenBits) - 1);
    }
    if (signal.width < 64)
        number &= (quint64(1) << signal.width) - 1;
    return QStringLiteral("%1'h%2").arg(signal.width).arg(QString::number(number, 16));
}
} // namespace

QString stimulusSignature(const Module &m, const Scan &scan, QString *error)
{
    const auto ports = stimulusSignals(m, scan, error);
    return error->isEmpty() ? signature(m, ports) : QString();
}
TbOptions stimulusOptions(const QJsonObject &o)
{
    const auto t = o.value(key("timing")).toObject();
    return {t.value(key("name")).toString(),    t.value(key("clock")).toString(),
            t.value(key("reset")).toString(),   t.value(key("activeLow")).toBool(),
            t.value(key("periodNs")).toInt(10), t.value(key("resetCycles")).toInt(5)};
}
QJsonObject newStimulus(const Module &module, const StimulusSemantics &semantics, const TbOptions &options, qint64 durationNs,
                        QString *error)
{
    const auto &ports = semantics.inputs;
    *error = semantics.error;
    if (error->isEmpty() && ports.isEmpty()) *error = key("The DUT has no editable inputs.");
    if (!error->isEmpty())
        return {};
    if (generateTestbench(module, options, durationNs, error).isEmpty())
        return {};
    if (durationNs < 1 || durationNs > 3600000000000LL)
    {
        *error = key("Stimulus duration is too large.");
        return {};
    }
    const auto duration = durationNs * 1000, period = qint64(options.clockPeriodNs) * 1000;
    QJsonArray lanes, clocks;
    if (!options.clock.isEmpty())
        clocks << QJsonObject{{key("id"), key("clock")},
                              {key("name"), options.clock},
                              {key("periodTick"), QString::number(period)},
                              {key("phaseTick"), QString::number(period / 2)},
                              {key("dutyNumerator"), key("1")},
                              {key("dutyDenominator"), key("2")},
                              {key("activeEdge"), key("falling")}};
    for (const auto &p : ports)
    {
        const bool clock = p.name == options.clock, reset = p.name == options.reset;
        if ((clock || reset) && p.width != 1)
        {
            *error = key("Clock and reset must be single-bit inputs.");
            return {};
        }
        QJsonObject enums;
        QString initial = key("0");
        for (auto it = p.enumValues.begin(); it != p.enumValues.end(); ++it)
        {
            enums.insert(it.key(), it.value());
            if (initial == key("0") || it.value() == key("0"))
                initial = it.key();
        }
        QJsonArray segments;
        if (reset)
        {
            const qint64 end = qMin(duration, period * options.resetCycles);
            segments << segment(p.name + key("-reset"), 0, end, options.resetActiveLow ? key("0") : key("1"));
            if (end < duration)
                segments << segment(p.name + key("-released"), end, duration,
                                    options.resetActiveLow ? key("1") : key("0"));
        }
        else if (!clock)
            segments << segment(p.name + key("-initial"), 0, duration, initial);
        lanes << QJsonObject{{key("id"), p.name},
                             {key("name"), p.name},
                             {key("kind"), clock              ? key("clock")
                                           : !enums.isEmpty() ? key("enum")
                                           : p.width == 1     ? key("bit")
                                                              : key("bus")},
                             {key("width"), p.width},
                             {key("signed"), p.isSigned},
                             {key("radix"), enums.isEmpty() ? key("hexadecimal") : key("decimal")},
                             {key("enumMap"), enums},
                             {key("clockDomainId"), options.clock.isEmpty() ? QString() : key("clock")},
                             {key("color"), clock              ? key("#a6adc8")
                                            : reset            ? key("#fab387")
                                            : !enums.isEmpty() ? key("#cba6f7")
                                            : p.width == 1     ? key("#a6e3a1")
                                                               : key("#89b4fa")},
                             {key("height"), 48},
                             {key("visible"), true},
                             {key("groupId"), QString()},
                             {key("segments"), segments}};
    }
    const QJsonObject wave{
        {key("schemaVersion"), 1},
        {key("projectId"), key("simdock-") + module.name},
        {key("name"), module.name},
        {key("timebase"), QJsonObject{{key("picosecondsPerTick"), key("1")}}},
        {key("clockDomains"), clocks},
        {key("scenarios"), QJsonArray{QJsonObject{{key("id"), key("stimulus")},
                                                  {key("name"), key("Inputs")},
                                                  {key("durationTick"), QString::number(duration)},
                                                  {key("lanes"), lanes},
                                                  {key("events"), QJsonArray{}},
                                                  {key("relations"), QJsonArray{}},
                                                  {key("markers"), QJsonArray{}}}}},
        {key("importedTraces"), QJsonArray{}},
        {key("linkedResources"), QJsonArray{}}};
    const auto timing = QJsonObject{{key("name"), options.name},
                                        {key("clock"), options.clock},
                                        {key("reset"), options.reset},
                                        {key("activeLow"), options.resetActiveLow},
                                        {key("periodNs"), options.clockPeriodNs},
                                        {key("resetCycles"), options.resetCycles}};
    auto editableWave = wave;
    // Tickx preserves unknown root extensions in its command history.
    editableWave.insert(key("simdockTiming"), timing);
    return {{key("schema"), key("simdock.stimulus/v1")},
            {key("signature"), signature(module, ports)},
            {key("timing"), timing}, {key("wave"), editableWave}};
}
QJsonObject retimeStimulus(const Module &module, const StimulusSemantics &semantics, const QJsonObject &previous,
                           const TbOptions &options, qint64 durationNs, QString *error)
{
    auto result = newStimulus(module, semantics, options, durationNs, error);
    if (result.isEmpty() || previous.isEmpty())
        return result;
    if (previous.value(key("signature")) != result.value(key("signature")))
    {
        *error = key("The DUT inputs or types changed. Use Reset drawing to recreate the inputs.");
        return {};
    }
    const auto oldOptions = stimulusOptions(previous);
    const bool resetChanged =
        oldOptions.reset != options.reset || oldOptions.resetActiveLow != options.resetActiveLow ||
        oldOptions.clockPeriodNs != options.clockPeriodNs || oldOptions.resetCycles != options.resetCycles;
    const auto oldScenarios = previous.value(key("wave")).toObject().value(key("scenarios")).toArray();
    if (oldScenarios.size() != 1)
    {
        *error = key("Invalid saved stimulus.");
        return {};
    }
    QMap<QString, QJsonObject> oldLanes;
    for (const auto &v : oldScenarios.first().toObject().value(key("lanes")).toArray())
    {
        const auto lane = v.toObject();
        oldLanes.insert(lane.value(key("id")).toString(), lane);
    }
    auto wave = result.value(key("wave")).toObject();
    auto scenario = wave.value(key("scenarios")).toArray().first().toObject();
    auto lanes = scenario.value(key("lanes")).toArray();
    const auto duration = durationNs * 1000;
    for (int i = 0; i < lanes.size(); ++i)
    {
        auto lane = lanes[i].toObject();
        const auto id = lane.value(key("id")).toString();
        if (id == options.clock || id == oldOptions.clock || (id == options.reset && resetChanged) ||
            !oldLanes.contains(id))
            continue;
        QJsonArray segments;
        for (const auto &v : oldLanes[id].value(key("segments")).toArray())
        {
            auto part = v.toObject();
            const auto begin = part.value(key("startTick")).toString().toLongLong();
            if (begin >= duration)
                break;
            part[key("endTick")] =
                QString::number(qMin(duration, part.value(key("endTick")).toString().toLongLong()));
            segments << part;
        }
        if (!segments.isEmpty())
        {
            auto last = segments.last().toObject();
            last[key("endTick")] = QString::number(duration);
            segments[segments.size() - 1] = last;
            lane[key("segments")] = segments;
            lanes[i] = lane;
        }
    }
    scenario[key("lanes")] = lanes;
    wave[key("scenarios")] = QJsonArray{scenario};
    result[key("wave")] = wave;
    if (previous.contains(key("scoreboard"))) result[key("scoreboard")] = previous.value(key("scoreboard"));
    return result;
}
QString stimulusTestbench(const Module &module, const StimulusSemantics &semantics, const QJsonObject &stimulus, QString *error)
{
    error->clear();
    try
    {
        const auto &ports = semantics.inputs;
        *error = semantics.error;
        if (error->isEmpty() && ports.isEmpty()) *error = key("The DUT has no editable inputs.");
        if (!error->isEmpty())
            return {};
        if (stimulus.value(key("schema")).toString() != key("simdock.stimulus/v1") ||
            stimulus.value(key("signature")).toString() != signature(module, ports))
            fail(key("The DUT inputs or types changed. Recreate the drawing for the current DUT."));
        const auto options = stimulusOptions(stimulus);
        const auto wave = stimulus.value(key("wave")).toObject();
        if (wave.value(key("timebase")).toObject().value(key("picosecondsPerTick")).toString() != key("1"))
            fail(key("Unsupported stimulus timebase."));
        const auto scenarios = wave.value(key("scenarios")).toArray();
        if (scenarios.size() != 1)
            fail(key("Exactly one stimulus scenario is required."));
        const auto scenario = scenarios.first().toObject();
        const auto duration = tick(scenario.value(key("durationTick")));
        if (duration < 1000)
            fail(key("Stimulus must last at least 1 ns."));
        const auto lanes = scenario.value(key("lanes")).toArray();
        if (lanes.size() != ports.size())
            fail(key("Stimulus inputs no longer match the DUT."));
        QString timingError;
        if (generateTestbench(module, options, (duration + 999) / 1000, &timingError).isEmpty())
            fail(timingError);
        QMap<QString, QJsonObject> byName;
        for (const auto &value : lanes)
        {
            auto lane = value.toObject();
            const auto id = lane.value(key("id")).toString();
            if (byName.contains(id))
                fail(key("Duplicate stimulus signal."));
            byName[id] = lane;
        }
        QString drivers =
            key("    // Graphical stimulus: generated from saved Tickx input waveforms.\n");
        for (const auto &p : ports)
        {
            if (!byName.contains(p.name))
                fail(key("Missing input waveform: ") + p.name);
            const auto lane = byName[p.name];
            if (lane.value(key("name")).toString() != p.name || lane.value(key("width")).toInt() != p.width)
                fail(key("Input waveform type changed: ") + p.name);
            const auto segments = lane.value(key("segments")).toArray();
            if (lane.value(key("clockDomainId")).toString() !=
                (options.clock.isEmpty() ? QString() : key("clock")))
                fail(key("Input clock binding changed: ") + p.name);
            const auto expectedKind = p.name == options.clock   ? key("clock")
                                      : !p.enumValues.isEmpty() ? key("enum")
                                      : p.width == 1            ? key("bit")
                                                                : key("bus");
            QJsonObject expectedEnum;
            for (auto it = p.enumValues.begin(); it != p.enumValues.end(); ++it)
                expectedEnum.insert(it.key(), it.value());
            if (lane.value(key("kind")).toString() != expectedKind ||
                lane.value(key("signed")).toBool() != p.isSigned ||
                lane.value(key("enumMap")).toObject() != expectedEnum)
                fail(key("Input waveform type changed: ") + p.name);
            if (p.name == options.clock)
            {
                if (lane.value(key("kind")).toString() != key("clock") || !segments.isEmpty())
                    fail(key("Clock gates are not supported in this editor. Use the clock period setting."));
                const auto clocks = wave.value(key("clockDomains")).toArray();
                if (clocks.size() != 1)
                    fail(key("One clock domain is required."));
                const auto clock = clocks.first().toObject();
                const auto period = qint64(options.clockPeriodNs) * 1000;
                if (clock.value(key("id")).toString() != key("clock") ||
                    clock.value(key("name")).toString() != options.clock ||
                    clock.value(key("activeEdge")).toString() != key("falling") ||
                    tick(clock.value(key("periodTick"))) != period ||
                    tick(clock.value(key("phaseTick"))) != period / 2 ||
                    clock.value(key("dutyNumerator")).toString() != key("1") ||
                    clock.value(key("dutyDenominator")).toString() != key("2"))
                    fail(key("Clock parameters changed outside the timing controls."));
                drivers += QStringLiteral("    initial %1 = 1'b0;\n    always #(%2 * 1ps) %1 = ~%1;\n")
                               .arg(p.name)
                               .arg(period / 2);
                continue;
            }
            drivers += QStringLiteral("    initial begin : drive_%1\n").arg(p.name);
            qint64 cursor = 0, last = 0;
            if (segments.size() > 100000)
                fail(key("Too many waveform intervals."));
            const auto radix = lane.value(key("radix")).toString();
            if (!QStringList{key("binary"), key("octal"), key("decimal"), key("hexadecimal")}.contains(radix))
                fail(key("Invalid waveform radix."));
            const auto drive = [&](qint64 at, const QString &value)
            {
                if (at > last)
                    drivers += QStringLiteral("        #(%1 * 1ps);\n").arg(at - last);
                const auto valueText = literal(p, value, radix);
                drivers += QStringLiteral("        %1 = %2;\n")
                               .arg(p.name, p.namedType ? QStringLiteral("%1'(%2)").arg(p.type, valueText)
                                                        : valueText);
                last = at;
            };
            // Match Tickx's implicit values in erased/unassigned intervals.
            const auto implicit = expectedKind == key("bit") ? key("0") : key("X");
            for (const auto &value : segments)
            {
                const auto part = value.toObject();
                const auto start = tick(part.value(key("startTick"))), end = tick(part.value(key("endTick")));
                if (start < cursor || end <= start || end > duration)
                    fail(key("Waveform intervals overlap or exceed the duration: ") + p.name);
                if (start > cursor)
                    drive(cursor, implicit);
                drive(start, part.value(key("value")).toString());
                cursor = end;
            }
            if (cursor < duration)
                drive(cursor, implicit);
            drivers += key("    end\n");
        }
        QString checks;
        if (stimulus.contains(key("scoreboard")) && !stimulus.value(key("scoreboard")).isObject())
            fail(key("Invalid saved scoreboard plan."));
        const auto plan = stimulus.value(key("scoreboard")).toObject();
        if (scoreboardEnabled(plan)) {
            if (!semantics.checksError.isEmpty()) fail(semantics.checksError);
            checks = scoreboardCode(semantics.ports, options, plan, error);
            if (!error->isEmpty()) return {};
        }
        return generateTestbench(module, options, (duration + 999) / 1000, error, drivers, checks);
    }
    catch (const std::exception &e)
    {
        *error = QString::fromUtf8(e.what());
        return {};
    }
}
bool saveStimulus(const QString &root, Project &p, const Module &module, const Scan &scan,
                  const QJsonObject &stimulus, QString *error)
{
    const auto content = stimulusTestbench(module, scan, stimulus, error);
    if (content.isEmpty())
        return false;
    return saveGeneratedStimulus(root, p, stimulus, content, error);
}
bool saveGeneratedStimulus(const QString &root, Project &p, const QJsonObject &stimulus,
                           const QString &content, QString *error)
{
    error->clear();
    if (content.isEmpty()) { *error = key("No generated stimulus is available."); return false; }
    const Project updated = generatedStimulusProject(p, stimulus, content);
    const auto relative = updated.tbFile;
    if (!insideWorkspace(root, QDir(root).filePath(relative)))
    {
        *error = key("Stimulus output is outside the workspace.");
        return false;
    }
    QFile existing(QDir(root).filePath(relative));
    if (existing.exists())
    {
        if (!existing.open(QIODevice::ReadOnly) || existing.readAll() != content.toUtf8())
        {
            *error = key("The generated TB was modified. Your file has been preserved.");
            return false;
        }
    }
    else if (!writeNewTb(root, relative, content, error))
        return false;
    if (!saveProject(root, updated, error))
        return false;
    p = updated;
    return true;
}
Project generatedStimulusProject(Project updated, const QJsonObject &stimulus, const QString &content)
{
    setInputMode(updated, InputMode::Graphical);
    const auto hash =
        QString::fromLatin1(QCryptographicHash::hash(content.toUtf8(), QCryptographicHash::Sha256).toHex());
    const auto options = stimulusOptions(stimulus);
    const auto relative =
        QStringLiteral("sim/%1/%2_stimulus_%3.sv").arg(updated.id.left(8), options.name, hash.left(16));
    updated.stimulus = stimulus;
    updated.tbFile = relative;
    updated.tbName = options.name;
    updated.durationNs = (stimulus.value(key("wave"))
                              .toObject()
                              .value(key("scenarios"))
                              .toArray()
                              .first()
                              .toObject()
                              .value(key("durationTick"))
                              .toString()
                              .toLongLong() +
                          999) /
                         1000;
    return updated;
}
QJsonObject newStimulus(const Module &m, const Scan &scan, const TbOptions &options, qint64 duration, QString *error)
{
    return newStimulus(m, resolveStimulus(m, scan), options, duration, error);
}
QJsonObject retimeStimulus(const Module &m, const Scan &scan, const QJsonObject &previous,
                           const TbOptions &options, qint64 duration, QString *error)
{
    return retimeStimulus(m, resolveStimulus(m, scan), previous, options, duration, error);
}
QString stimulusTestbench(const Module &m, const Scan &scan, const QJsonObject &drawing, QString *error)
{
    return stimulusTestbench(m, resolveStimulus(m, scan), drawing, error);
}
} // namespace simdock
