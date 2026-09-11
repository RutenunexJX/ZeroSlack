#include "wavepreviewpayloadadapter.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <atomic>

namespace {
constexpr qsizetype kMaximumPayloadBytes = 8 * 1024 * 1024;
constexpr int kMaximumLanes = 512;
constexpr int kMaximumSegments = 200'000;
constexpr int kMaximumLaneWidth = 65'536;

std::atomic<quint64> globalGeneration{0};

QString portableDocumentPath(const QString& documentPath,
                             const QString& workspaceRoot)
{
    const QString normalized = QDir::cleanPath(
        QDir::fromNativeSeparators(documentPath.trimmed()));
    if (normalized.isEmpty() || normalized == QLatin1String("."))
        return {};

    QString relative = normalized;
    if (QDir::isAbsolutePath(normalized)) {
        const QString normalizedRoot = QDir::cleanPath(
            QDir::fromNativeSeparators(workspaceRoot.trimmed()));
        if (normalizedRoot.isEmpty() || !QDir::isAbsolutePath(normalizedRoot))
            return {};
        relative = QDir(normalizedRoot).relativeFilePath(normalized);
        relative = QDir::cleanPath(QDir::fromNativeSeparators(relative));
    }
    if (QDir::isAbsolutePath(relative)
        || relative == QLatin1String("..")
        || relative.startsWith(QStringLiteral("../"))) {
        return {};
    }
    return relative.size() <= 512 ? relative : QString{};
}

bool containsUnknown(const QString& value)
{
    return value.contains(QLatin1Char('x'), Qt::CaseInsensitive)
        || value.contains(QLatin1Char('z'), Qt::CaseInsensitive)
        || value == QLatin1String("?");
}

QString stableLaneId(const QString& portableFile,
                     const WavePreviewReport& report,
                     const WavePreviewTraceSignal& signal)
{
    const QByteArray identity = QStringLiteral("%1\n%2\n%3\n%4")
                                    .arg(portableFile,
                                         report.scopeLabel,
                                         signal.signalName,
                                         QString::number(signal.width))
                                    .toUtf8();
    return QStringLiteral("zeroslack-symbolic:%1")
        .arg(QString::fromLatin1(
            QCryptographicHash::hash(identity, QCryptographicHash::Sha256)
                .toHex()));
}

const WavePreviewSignalContext* contextForSignal(
    const WavePreviewReport& report,
    const QString& signalName)
{
    const auto iterator = std::find_if(
        report.signalContexts.cbegin(),
        report.signalContexts.cend(),
        [&signalName](const WavePreviewSignalContext& context) {
            return context.signalName == signalName;
        });
    return iterator == report.signalContexts.cend() ? nullptr : &*iterator;
}

QJsonObject segmentObject(int start,
                          int end,
                          const QString& value,
                          bool unknown)
{
    QJsonObject segment{
        {QStringLiteral("start"), start},
        {QStringLiteral("end"), end},
        {QStringLiteral("value"), value}};
    if (unknown)
        segment.insert(QStringLiteral("unknown"), true);
    return segment;
}
}

quint64 WavePreviewPayloadAdapter::nextGeneration()
{
    quint64 observed = globalGeneration.load(std::memory_order_relaxed);
    while (observed < kMaximumGeneration) {
        if (globalGeneration.compare_exchange_weak(
                observed,
                observed + 1,
                std::memory_order_relaxed,
                std::memory_order_relaxed)) {
            return observed + 1;
        }
    }
    return 0;
}

WavePreviewPayloadBuildResult WavePreviewPayloadAdapter::buildSymbolic(
    const WavePreviewReport& report,
    const QString& documentPath,
    const QString& workspaceRoot,
    quint64 generation)
{
    WavePreviewPayloadBuildResult result;
    result.generation = generation == 0 ? nextGeneration() : generation;
    if (result.generation == 0 || result.generation > kMaximumGeneration) {
        result.error = QStringLiteral(
            "Wave Preview generation is outside the wave-preview/v1 range.");
        return result;
    }
    if (generation != 0) {
        quint64 observed = globalGeneration.load(std::memory_order_relaxed);
        while (observed < generation
               && !globalGeneration.compare_exchange_weak(
                   observed,
                   generation,
                   std::memory_order_relaxed,
                   std::memory_order_relaxed)) {
        }
    }
    if (!report.trace.isValid()) {
        result.error = QStringLiteral(
            "Wave Preview has no valid symbolic trace to publish.");
        return result;
    }
    if (report.trace.traceSignals.size() > kMaximumLanes) {
        result.error = QStringLiteral(
            "Wave Preview exceeds the wave-preview/v1 lane limit.");
        return result;
    }

    const QString portableFile = portableDocumentPath(
        documentPath, workspaceRoot);
    QJsonArray lanes;
    QSet<QString> laneIds;
    int segmentCount = 0;
    for (const WavePreviewTraceSignal& signal : report.trace.traceSignals) {
        const QString signalName = signal.signalName.trimmed();
        if (signalName.isEmpty() || signalName.size() > 512) {
            result.error = QStringLiteral(
                "Wave Preview contains an empty or oversized signal name.");
            return result;
        }
        if (signal.width <= 0 || signal.width > kMaximumLaneWidth) {
            result.error = QStringLiteral(
                "Wave Preview signal %1 has unsupported width %2.")
                               .arg(signalName)
                               .arg(signal.width);
            return result;
        }
        if (signal.clock && signal.width != 1) {
            result.error = QStringLiteral(
                "Wave Preview clock %1 must have width 1.")
                               .arg(signalName);
            return result;
        }

        const QString laneId = stableLaneId(portableFile, report, signal);
        if (laneIds.contains(laneId)) {
            result.error = QStringLiteral(
                "Wave Preview contains duplicate stable signal identity %1.")
                               .arg(laneId);
            return result;
        }
        laneIds.insert(laneId);

        QJsonArray segments;
        const int intervalCount = std::min(
            report.trace.cycleCount,
            static_cast<int>(signal.values.size()));
        int runStart = 0;
        QString runValue;
        bool runUnknown = false;
        for (int interval = 0; interval < intervalCount; ++interval) {
            QString value = signal.values.at(interval).trimmed();
            if (value.isEmpty())
                value = QStringLiteral("X");
            if (value.size() > 128) {
                result.error = QStringLiteral(
                    "Wave Preview value for %1 exceeds 128 characters.")
                                   .arg(signalName);
                return result;
            }
            const bool unknown = containsUnknown(value);
            if (interval == 0) {
                runValue = value;
                runUnknown = unknown;
                continue;
            }
            if (value == runValue && unknown == runUnknown)
                continue;
            segments.append(segmentObject(
                runStart, interval, runValue, runUnknown));
            ++segmentCount;
            runStart = interval;
            runValue = value;
            runUnknown = unknown;
        }
        if (intervalCount > 0) {
            segments.append(segmentObject(
                runStart, intervalCount, runValue, runUnknown));
            ++segmentCount;
        }
        if (segmentCount > kMaximumSegments) {
            result.error = QStringLiteral(
                "Wave Preview exceeds the wave-preview/v1 segment limit.");
            return result;
        }

        QJsonObject lane{
            {QStringLiteral("id"), laneId},
            {QStringLiteral("name"), signalName},
            {QStringLiteral("kind"),
             signal.clock
                 ? QStringLiteral("clock")
                 : signal.width == 1 ? QStringLiteral("bit")
                                     : QStringLiteral("bus")},
            {QStringLiteral("width"), signal.width},
            {QStringLiteral("provenance"),
             QStringLiteral("zeroslack-symbolic")},
            {QStringLiteral("segments"), segments}};

        const WavePreviewSignalContext* context =
            contextForSignal(report, signalName);
        if (!portableFile.isEmpty() && context
            && context->line > 0 && context->line <= 10'000'000
            && qMax(1, context->column) <= 1'000'000) {
            lane.insert(
                QStringLiteral("source"),
                QJsonObject{
                    {QStringLiteral("file"), portableFile},
                    {QStringLiteral("line"), context->line},
                    {QStringLiteral("column"), qMax(1, context->column)},
                    {QStringLiteral("semanticId"), laneId}});
        }
        lanes.append(lane);
    }

    const QJsonObject root{
        {QStringLiteral("contract"), QStringLiteral("wave-preview/v1")},
        {QStringLiteral("generation"),
         static_cast<double>(result.generation)},
        {QStringLiteral("mode"), QStringLiteral("symbolic")},
        {QStringLiteral("timebase"),
         QJsonObject{{QStringLiteral("unit"), QStringLiteral("tick")},
                     {QStringLiteral("start"), 0},
                     {QStringLiteral("end"), report.trace.cycleCount}}},
        {QStringLiteral("lanes"), lanes}};
    result.payload = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (result.payload.size() > kMaximumPayloadBytes) {
        result.payload.clear();
        result.error = QStringLiteral(
            "Wave Preview exceeds the wave-preview/v1 8 MiB limit.");
    }
    return result;
}
