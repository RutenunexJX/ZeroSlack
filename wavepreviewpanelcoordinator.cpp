#include "wavepreviewpanelcoordinator.h"

#include "semanticindex.h"

#include <QFileInfo>
#include <QFont>
#include <QHeaderView>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QSizePolicy>
#include <QTimer>
#include <QToolTip>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>

#include <algorithm>
#include <functional>
#include <utility>

namespace {
constexpr int kRoleFileName = Qt::UserRole + 1;
constexpr int kRoleLine = Qt::UserRole + 2;
constexpr int kRoleColumn = Qt::UserRole + 3;
constexpr qsizetype kLargeDirtyRefreshCharacterThreshold = 64 * 1024;
constexpr int kLargeDirtyRefreshDelayMs = 180;

QString canvasEventLabel(const WavePreviewAssignment& assignment);
QString canvasEventSelectionText(const WavePreviewAssignment& assignment,
                                 const WavePreviewReport& report);
QString canvasLaneSelectionText(const WavePreviewLane& lane);
QString assignmentDetailTooltip(const WavePreviewAssignment& assignment,
                                const WavePreviewReport& report);
QString laneDetailTooltip(const WavePreviewLane& lane);
QString laneSummaryText(const WavePreviewLaneSummary& summary);
QString laneWarningText(const WavePreviewLaneSummary& summary);
QString reportSummaryText(const WavePreviewReport& report, bool dirty);

bool shouldQueueRefresh(const QString& documentText, bool)
{
    return documentText.size() >= kLargeDirtyRefreshCharacterThreshold;
}

struct CanvasEventHit {
    QRect rect;
    QString tooltip;
    QString selectionText;
    int line = 0;
    int column = 0;
};

struct CanvasLaneHit {
    QRect rect;
    QString tooltip;
    QString selectionText;
    QString signalName;
};

class WavePreviewCanvas : public QWidget
{
public:
    explicit WavePreviewCanvas(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("wavePreviewCanvas"));
        setMinimumHeight(132);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        setMouseTracking(true);
    }

    QSize sizeHint() const override
    {
        const int laneCount = report.trace.isValid()
            ? report.trace.traceSignals.size()
            : (report.available ? report.lanes.size() : 2);
        return QSize(460, qBound(132, 52 + laneCount * 34, 260));
    }

    void setReport(const WavePreviewReport& nextReport,
                   const QString& fileName)
    {
        report = nextReport;
        currentFileName = fileName;
        eventHits.clear();
        laneHits.clear();
        selectedLine = 0;
        selectedColumn = 0;
        selectedSignalName.clear();
        setToolTip(QString());
        unsetCursor();
        updateGeometry();
        update();
    }

    void clearReport()
    {
        report = WavePreviewReport();
        currentFileName.clear();
        eventHits.clear();
        laneHits.clear();
        selectedLine = 0;
        selectedColumn = 0;
        selectedSignalName.clear();
        setToolTip(QString());
        unsetCursor();
        updateGeometry();
        update();
    }

    void setNavigationHandler(
        std::function<void(const QString&, int, int)> handler)
    {
        navigationHandler = std::move(handler);
    }

    void setSelectionHandler(std::function<void(const QString&)> handler)
    {
        selectionHandler = std::move(handler);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        eventHits.clear();
        laneHits.clear();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRect canvasRect = rect().adjusted(0, 0, -1, -1);
        painter.fillRect(canvasRect, QColor(QStringLiteral("#f8fafc")));
        painter.setPen(QPen(QColor(QStringLiteral("#cbd5e1"))));
        painter.drawRect(canvasRect);

        if (!report.available || report.lanes.isEmpty()) {
            painter.setPen(QColor(QStringLiteral("#64748b")));
            painter.drawText(canvasRect,
                             Qt::AlignCenter,
                             QStringLiteral("No local waveform"));
            return;
        }

        if (report.trace.isValid()) {
            paintTrace(painter, canvasRect);
            return;
        }

        int maxCycle = 0;
        for (const WavePreviewLane& lane : report.lanes) {
            for (const WavePreviewAssignment& assignment : lane.assignments)
                maxCycle = std::max(maxCycle, assignment.cycleOffset);
        }
        maxCycle = std::max(maxCycle, 2);

        const int labelWidth = qMin(130, qMax(84, width() / 4));
        const int left = 10;
        const int right = 12;
        const int top = 28;
        const int rowHeight = 32;
        const int timelineLeft = left + labelWidth;
        const int timelineRight = width() - right;
        const int timelineWidth = std::max(80, timelineRight - timelineLeft);
        const int laneBottom = top + report.lanes.size() * rowHeight;

        painter.setPen(QColor(QStringLiteral("#64748b")));
        for (int cycle = 0; cycle <= maxCycle; ++cycle) {
            const int x = timelineLeft
                + static_cast<int>((timelineWidth * cycle) / maxCycle);
            painter.drawLine(x, top - 8, x, laneBottom + 4);
            painter.drawText(QRect(x - 20, 4, 40, 18),
                             Qt::AlignCenter,
                             QStringLiteral("t+%1").arg(cycle));
        }

        QFont labelFont = painter.font();
        labelFont.setBold(true);
        painter.setFont(labelFont);
        painter.setPen(QColor(QStringLiteral("#0f172a")));

        for (int laneIndex = 0; laneIndex < report.lanes.size(); ++laneIndex) {
            const WavePreviewLane& lane = report.lanes.at(laneIndex);
            const int y = top + laneIndex * rowHeight;
            const int centerY = y + rowHeight / 2;
            const QRect laneRect(left,
                                 y + 2,
                                 timelineRight - left,
                                 rowHeight - 4);

            if (lane.signalName == selectedSignalName) {
                painter.fillRect(laneRect,
                                 QColor(QStringLiteral("#e0f2fe")));
            }
            laneHits.append({laneRect,
                             laneDetailTooltip(lane),
                             canvasLaneSelectionText(lane),
                             lane.signalName});

            painter.setPen(QPen(QColor(QStringLiteral("#e2e8f0"))));
            painter.drawLine(left, centerY, timelineRight, centerY);
            painter.setPen(QColor(QStringLiteral("#0f172a")));
            painter.drawText(QRect(left, y + 2, labelWidth - 8, 15),
                             Qt::AlignRight | Qt::AlignVCenter,
                             lane.signalName);
            QFont summaryFont = painter.font();
            summaryFont.setBold(false);
            painter.setFont(summaryFont);
            painter.setPen(QColor(QStringLiteral("#64748b")));
            painter.drawText(QRect(left, y + 17, labelWidth - 8, 13),
                             Qt::AlignRight | Qt::AlignVCenter,
                             laneSummaryText(lane.summary));
            painter.setFont(labelFont);
            painter.setPen(QColor(QStringLiteral("#0f172a")));

            for (int eventIndex = 0;
                 eventIndex < lane.assignments.size();
                 ++eventIndex) {
                const WavePreviewAssignment& assignment =
                    lane.assignments.at(eventIndex);
                const int clampedCycle =
                    qBound(0, assignment.cycleOffset, maxCycle);
                const int eventCenterX = timelineLeft
                    + static_cast<int>((timelineWidth * clampedCycle)
                                       / maxCycle);
                const int eventWidth = qBound(52,
                                              timelineWidth / 3,
                                              112);
                const int yOffset =
                    (eventIndex % 2 == 0) ? -10 : 4;
                QRect eventRect(eventCenterX - eventWidth / 2,
                                centerY + yOffset,
                                eventWidth,
                                18);
                eventRect = eventRect.intersected(
                    QRect(timelineLeft + 2, y + 2,
                          timelineWidth - 4, rowHeight - 4));

                QColor fill(QStringLiteral("#2f855a"));
                if (assignment.kind == WavePreviewAssignmentKind::Blocking)
                    fill = QColor(QStringLiteral("#2563eb"));
                else if (assignment.kind
                         == WavePreviewAssignmentKind::NonBlocking)
                    fill = QColor(QStringLiteral("#9333ea"));

                const bool selected =
                    assignment.line > 0
                    && assignment.line == selectedLine
                    && qMax(1, assignment.column) == selectedColumn;
                painter.setPen(QPen(selected
                                        ? QColor(QStringLiteral("#0f172a"))
                                        : fill.darker(125),
                                    selected ? 2 : 1));
                painter.setBrush(fill.lighter(180));
                painter.drawRoundedRect(eventRect, 4, 4);
                painter.setPen(QColor(QStringLiteral("#111827")));
                painter.drawText(eventRect.adjusted(5, 0, -5, 0),
                                 Qt::AlignCenter,
                                 canvasEventLabel(assignment));
                eventHits.append({eventRect,
                                  assignmentDetailTooltip(assignment, report),
                                  canvasEventSelectionText(assignment, report),
                                  assignment.line,
                                  assignment.column});
            }
        }
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton) {
            QWidget::mousePressEvent(event);
            return;
        }

        for (auto it = eventHits.crbegin(); it != eventHits.crend(); ++it) {
            if (!it->rect.contains(event->pos()))
                continue;
            selectedLine = it->line;
            selectedColumn = qMax(1, it->column);
            selectedSignalName.clear();
            if (selectionHandler)
                selectionHandler(it->selectionText);
            update();
            event->accept();
            return;
        }
        for (auto it = laneHits.crbegin(); it != laneHits.crend(); ++it) {
            if (!it->rect.contains(event->pos()))
                continue;
            selectedLine = 0;
            selectedColumn = 0;
            selectedSignalName = it->signalName;
            if (selectionHandler)
                selectionHandler(it->selectionText);
            update();
            event->accept();
            return;
        }
        selectedLine = 0;
        selectedColumn = 0;
        selectedSignalName.clear();
        if (selectionHandler)
            selectionHandler(QString());
        update();
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        for (auto it = eventHits.crbegin(); it != eventHits.crend(); ++it) {
            if (!it->rect.contains(event->pos()))
                continue;
            setToolTip(it->tooltip);
            setCursor(Qt::PointingHandCursor);
            QToolTip::showText(event->globalPosition().toPoint(),
                               it->tooltip,
                               this,
                               it->rect);
            return;
        }
        for (auto it = laneHits.crbegin(); it != laneHits.crend(); ++it) {
            if (!it->rect.contains(event->pos()))
                continue;
            setToolTip(it->tooltip);
            setCursor(Qt::PointingHandCursor);
            QToolTip::showText(event->globalPosition().toPoint(),
                               it->tooltip,
                               this,
                               it->rect);
            return;
        }
        setToolTip(QString());
        unsetCursor();
        QToolTip::hideText();
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (!navigationHandler || event->button() != Qt::LeftButton) {
            QWidget::mouseDoubleClickEvent(event);
            return;
        }

        for (auto it = eventHits.crbegin(); it != eventHits.crend(); ++it) {
            if (!it->rect.contains(event->pos()))
                continue;
            if (it->line > 0) {
                navigationHandler(currentFileName,
                                  it->line,
                                  qMax(1, it->column));
                event->accept();
                return;
            }
        }
        QWidget::mouseDoubleClickEvent(event);
    }

    void leaveEvent(QEvent*) override
    {
        setToolTip(QString());
        unsetCursor();
        QToolTip::hideText();
    }

private:
    void paintTrace(QPainter& painter, const QRect& canvasRect)
    {
        const int labelWidth = qMin(130, qMax(84, width() / 4));
        const int left = 10;
        const int right = 12;
        const int top = 28;
        const int rowHeight = 32;
        const int timelineLeft = left + labelWidth;
        const int timelineRight = width() - right;
        const int timelineWidth = std::max(80, timelineRight - timelineLeft);
        const int maxCycle = qMax(1, report.trace.cycleCount);
        const int laneBottom = top + report.trace.traceSignals.size() * rowHeight;

        painter.setPen(QColor(QStringLiteral("#64748b")));
        for (int cycle = 0; cycle <= maxCycle; ++cycle) {
            const int x = timelineLeft
                + static_cast<int>((timelineWidth * cycle) / maxCycle);
            painter.drawLine(x, top - 8, x, laneBottom + 4);
            painter.drawText(QRect(x - 20, 4, 40, 18),
                             Qt::AlignCenter,
                             QStringLiteral("%1").arg(cycle));
        }

        QFont labelFont = painter.font();
        labelFont.setBold(true);
        painter.setFont(labelFont);

        for (int signalIndex = 0;
             signalIndex < report.trace.traceSignals.size();
             ++signalIndex) {
            const WavePreviewTraceSignal& signal =
                report.trace.traceSignals.at(signalIndex);
            const int y = top + signalIndex * rowHeight;
            const int centerY = y + rowHeight / 2;
            const QRect laneRect(left,
                                 y + 2,
                                 timelineRight - left,
                                 rowHeight - 4);
            if (signal.signalName == selectedSignalName)
                painter.fillRect(laneRect, QColor(QStringLiteral("#e0f2fe")));
            const QString signalValues =
                signal.values.join(QStringLiteral(" -> "));
            laneHits.append(
                {laneRect,
                 QStringLiteral("waveform signal: %1\nvalues: %2")
                     .arg(signal.signalName, signalValues),
                 QStringLiteral("Selected waveform %1 values %2")
                     .arg(signal.signalName, signalValues),
                 signal.signalName});
            painter.setPen(QPen(QColor(QStringLiteral("#e2e8f0"))));
            painter.drawLine(left, centerY, timelineRight, centerY);
            painter.setPen(QColor(QStringLiteral("#0f172a")));
            painter.drawText(QRect(left, y + 2, labelWidth - 8, rowHeight - 4),
                             Qt::AlignRight | Qt::AlignVCenter,
                             signal.signalName);

            const int highY = y + 7;
            const int lowY = y + rowHeight - 8;
            const int busTop = y + 7;
            const int busHeight = rowHeight - 14;
            QPen wavePen(QColor(QStringLiteral("#0f766e")), 2);
            painter.setPen(wavePen);
            painter.setBrush(Qt::NoBrush);

            const int sampleCount = signal.values.size();
            if (sampleCount < 2)
                continue;

            auto xForSample = [&](int sample) {
                return timelineLeft
                    + static_cast<int>((timelineWidth * sample)
                                       / qMax(1, sampleCount - 1));
            };
            if (signal.width <= 1) {
                for (int sample = 0; sample < sampleCount - 1; ++sample) {
                    const QString value = signal.values.at(sample);
                    const QString nextValue = signal.values.at(sample + 1);
                    const bool unknown = value == QStringLiteral("x");
                    const int yValue =
                        unknown ? centerY
                                : (value == QStringLiteral("0") ? lowY : highY);
                    const int x0 = xForSample(sample);
                    const int x1 = xForSample(sample + 1);
                    if (unknown) {
                        painter.setPen(QPen(QColor(QStringLiteral("#94a3b8")), 1));
                        painter.drawLine(x0, centerY, x1, centerY);
                        painter.drawText(QRect(x0, y + 2, x1 - x0, 12),
                                         Qt::AlignCenter,
                                         QStringLiteral("x"));
                    } else {
                        painter.setPen(wavePen);
                        painter.drawLine(x0, yValue, x1, yValue);
                    }
                    if (nextValue != value) {
                        const bool nextUnknown = nextValue == QStringLiteral("x");
                        const int nextY =
                            nextUnknown ? centerY
                                        : (nextValue == QStringLiteral("0")
                                               ? lowY
                                               : highY);
                        painter.drawLine(x1, yValue, x1, nextY);
                    }
                }
            } else {
                for (int sample = 0; sample < sampleCount - 1; ++sample) {
                    const int x0 = xForSample(sample);
                    const int x1 = xForSample(sample + 1);
                    QRect segment(x0 + 1,
                                  busTop,
                                  qMax(8, x1 - x0 - 2),
                                  busHeight);
                    painter.setPen(QPen(QColor(QStringLiteral("#0f766e")), 1));
                    painter.setBrush(QColor(QStringLiteral("#ccfbf1")));
                    painter.drawRect(segment);
                    painter.setPen(QColor(QStringLiteral("#0f172a")));
                    painter.drawText(segment.adjusted(2, 0, -2, 0),
                                     Qt::AlignCenter,
                                     signal.values.at(sample));
                }
            }
        }
    }

    WavePreviewReport report;
    QVector<CanvasEventHit> eventHits;
    QVector<CanvasLaneHit> laneHits;
    QString currentFileName;
    int selectedLine = 0;
    int selectedColumn = 0;
    QString selectedSignalName;
    std::function<void(const QString&, int, int)> navigationHandler;
    std::function<void(const QString&)> selectionHandler;
};

QString displayFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QStringLiteral("Untitled");
    return QFileInfo(fileName).fileName();
}

QString assignmentKindText(WavePreviewAssignmentKind kind)
{
    switch (kind) {
    case WavePreviewAssignmentKind::Continuous:
        return QStringLiteral("assign");
    case WavePreviewAssignmentKind::Blocking:
        return QStringLiteral("blocking");
    case WavePreviewAssignmentKind::NonBlocking:
        return QStringLiteral("nonblocking");
    }
    return QStringLiteral("assignment");
}

QString blockKindText(WavePreviewBlockKind kind)
{
    switch (kind) {
    case WavePreviewBlockKind::AlwaysComb:
        return QStringLiteral("always_comb");
    case WavePreviewBlockKind::AlwaysFf:
        return QStringLiteral("always_ff");
    case WavePreviewBlockKind::AlwaysLatch:
        return QStringLiteral("always_latch");
    case WavePreviewBlockKind::AlwaysClocked:
        return QStringLiteral("clocked always");
    case WavePreviewBlockKind::AlwaysLevel:
        return QStringLiteral("level/continuous");
    case WavePreviewBlockKind::Unknown:
        break;
    }
    return QStringLiteral("block");
}

QString timingText(const WavePreviewAssignment& assignment)
{
    if (assignment.kind == WavePreviewAssignmentKind::Continuous)
        return QStringLiteral("t+0 continuous");
    if (assignment.cycleOffset > 0)
        return QStringLiteral("t+%1 cycle").arg(assignment.cycleOffset);
    return QStringLiteral("t+0");
}

QString guardText(const WavePreviewAssignment& assignment)
{
    return assignment.guardText.isEmpty()
        ? QStringLiteral("-")
        : assignment.guardText;
}

QString clockResetText(const QStringList& clockSignals,
                       const QStringList& resetSignals,
                       const QList<WavePreviewEdgeSignal>& clockEdgeSignals = {},
                       const QList<WavePreviewEdgeSignal>& resetEdgeSignals = {})
{
    QStringList parts;
    QStringList clockLabels;
    for (const WavePreviewEdgeSignal& signal : clockEdgeSignals)
        clockLabels.append(signal.label());
    if (clockLabels.isEmpty())
        clockLabels = clockSignals;

    QStringList resetLabels;
    for (const WavePreviewEdgeSignal& signal : resetEdgeSignals)
        resetLabels.append(signal.label());
    if (resetLabels.isEmpty())
        resetLabels = resetSignals;

    if (!clockLabels.isEmpty())
        parts.append(QStringLiteral("clk ") + clockLabels.join(QStringLiteral(", ")));
    if (!resetLabels.isEmpty())
        parts.append(QStringLiteral("rst ") + resetLabels.join(QStringLiteral(", ")));
    return parts.isEmpty()
        ? QStringLiteral("-")
        : parts.join(QStringLiteral(" / "));
}

QString clockResetText(const WavePreviewBlock& block)
{
    return clockResetText(block.clockSignals,
                          block.resetSignals,
                          block.clockEdgeSignals,
                          block.resetEdgeSignals);
}

QString clockResetText(const WavePreviewAssignment& assignment,
                       const WavePreviewReport& report)
{
    if (assignment.blockIndex < 0
        || assignment.blockIndex >= report.blocks.size()) {
        return QStringLiteral("-");
    }
    return clockResetText(report.blocks.at(assignment.blockIndex));
}

QString sourcesText(const QStringList& sourceSignals)
{
    return sourceSignals.isEmpty()
        ? QStringLiteral("-")
        : sourceSignals.join(QStringLiteral(", "));
}

QString countText(int count,
                  const QString& singular,
                  const QString& plural)
{
    return QStringLiteral("%1 %2").arg(count).arg(count == 1 ? singular : plural);
}

QString laneActivityText(const WavePreviewLaneSummary& summary)
{
    QStringList parts;
    if (summary.hasContinuousEvent)
        parts.append(QStringLiteral("assign %1")
                         .arg(summary.continuousEventCount));
    if (summary.hasCombinationalEvent)
        parts.append(QStringLiteral("comb %1")
                         .arg(summary.combinationalEventCount));
    if (summary.hasSequentialEvent)
        parts.append(QStringLiteral("seq %1")
                         .arg(summary.sequentialEventCount));
    return parts.isEmpty()
        ? QStringLiteral("-")
        : parts.join(QStringLiteral("/"));
}

QString activitySummaryText(const WavePreviewActivitySummary& summary)
{
    QStringList parts;
    if (summary.hasContinuousEvent) {
        parts.append(QStringLiteral("assign %1")
                         .arg(summary.continuousEventCount));
    }
    if (summary.hasCombinationalEvent) {
        parts.append(QStringLiteral("comb %1")
                         .arg(summary.combinationalEventCount));
    }
    if (summary.hasSequentialEvent) {
        parts.append(QStringLiteral("seq %1")
                         .arg(summary.sequentialEventCount));
    }
    return parts.isEmpty()
        ? QStringLiteral("-")
        : parts.join(QStringLiteral("/"));
}

QString laneGuardText(const WavePreviewLaneSummary& summary)
{
    if (summary.guardTexts.isEmpty())
        return QStringLiteral("-");

    QStringList visible;
    const int visibleCount = qMin(2, static_cast<int>(summary.guardTexts.size()));
    for (int i = 0; i < visibleCount; ++i)
        visible.append(summary.guardTexts.at(i));
    if (summary.guardTexts.size() > visibleCount) {
        visible.append(QStringLiteral("+%1 more")
                           .arg(summary.guardTexts.size() - visibleCount));
    }
    return visible.join(QStringLiteral("; "));
}

QString laneWarningText(const WavePreviewLaneSummary& summary)
{
    if (summary.warningTexts.isEmpty())
        return QStringLiteral("-");

    QStringList visible;
    const int visibleCount =
        qMin(2, static_cast<int>(summary.warningTexts.size()));
    for (int i = 0; i < visibleCount; ++i)
        visible.append(summary.warningTexts.at(i));
    if (summary.warningTexts.size() > visibleCount) {
        visible.append(QStringLiteral("+%1 more")
                           .arg(summary.warningTexts.size() - visibleCount));
    }
    return visible.join(QStringLiteral("; "));
}

QString laneSummaryText(const WavePreviewLaneSummary& summary)
{
    if (!summary.isValid())
        return QStringLiteral("-");
    const QString warningText =
        summary.warningTexts.isEmpty()
            ? QString()
            : QStringLiteral(", %1")
                  .arg(countText(summary.warningTexts.size(),
                                 QStringLiteral("warning"),
                                 QStringLiteral("warnings")));
    return QStringLiteral("%1, %2, %3, max t+%4, %5, %6%7")
        .arg(countText(summary.eventCount,
                       QStringLiteral("event"),
                       QStringLiteral("events")),
             countText(summary.sourceSignalCount,
                       QStringLiteral("src"),
                       QStringLiteral("src")),
             countText(summary.guardTexts.size(),
                       QStringLiteral("guard"),
                       QStringLiteral("guards")),
             QString::number(summary.maxCycleOffset),
             countText(summary.blockCount,
                       QStringLiteral("block"),
                       QStringLiteral("blocks")),
             laneActivityText(summary),
             warningText);
}

QString busiestLaneText(const WavePreviewReport& report)
{
    const WavePreviewLane* busiest = nullptr;
    for (const WavePreviewLane& lane : report.lanes) {
        if (!lane.summary.isValid())
            continue;
        if (!busiest
            || lane.summary.eventCount > busiest->summary.eventCount
            || (lane.summary.eventCount == busiest->summary.eventCount
                && lane.signalName < busiest->signalName)) {
            busiest = &lane;
        }
    }
    if (!busiest)
        return QString();
    return QStringLiteral(", busiest %1 (%2, %3)")
        .arg(busiest->signalName,
             countText(busiest->summary.eventCount,
                       QStringLiteral("event"),
                       QStringLiteral("events")),
             laneActivityText(busiest->summary));
}

QString reportSummaryText(const WavePreviewReport& report, bool dirty)
{
    const QString warningText =
        report.warnings.isEmpty()
            ? QString()
            : QStringLiteral(", %1")
                  .arg(countText(report.warnings.size(),
                                 QStringLiteral("warning"),
                                 QStringLiteral("warnings")));
    const QString scopeText =
        report.scoped
            ? QStringLiteral("%1 scope, ").arg(report.scopeLabel)
            : QString();
    if (report.trace.isValid()) {
        return QStringLiteral("%1local waveform: %2 signals, %3 cycles, %4 lanes, %5 events, activity %6%7%8%9")
            .arg(scopeText)
            .arg(report.trace.traceSignals.size())
            .arg(report.trace.cycleCount)
            .arg(report.lanes.size())
            .arg(report.assignmentCount)
            .arg(activitySummaryText(report.activitySummary))
            .arg(busiestLaneText(report))
            .arg(warningText)
            .arg(dirty ? QStringLiteral(" - live dirty buffer") : QString());
    }
    return QStringLiteral("%1code sketch, not simulated waveform: %2 lanes, %3 events, %4 blocks, %5 clock/reset groups, activity %6%7%8%9")
        .arg(scopeText)
        .arg(report.lanes.size())
        .arg(report.assignmentCount)
        .arg(report.blocks.size())
        .arg(report.clockResetGroups.size())
        .arg(activitySummaryText(report.activitySummary))
        .arg(busiestLaneText(report))
        .arg(warningText)
        .arg(dirty ? QStringLiteral(" - live dirty buffer") : QString());
}

const WavePreviewSignalContext* signalContextFor(
    const WavePreviewReport& report,
    const QString& signalName)
{
    for (const WavePreviewSignalContext& context : report.signalContexts) {
        if (context.signalName == signalName)
            return &context;
    }
    return nullptr;
}

QString signalContextBrief(const WavePreviewSignalContext* context)
{
    if (!context || !context->isValid())
        return QStringLiteral("-");
    return context->label();
}

QString signalContextDetail(const WavePreviewSignalContext* context)
{
    if (!context || !context->isValid())
        return QStringLiteral("-");
    QString detail = context->label();
    if (!context->declarationText.isEmpty())
        detail += QStringLiteral(" | declaration: ") + context->declarationText;
    if (context->line > 0) {
        detail += QStringLiteral(" | location: %1:%2")
                      .arg(context->line)
                      .arg(qMax(1, context->column));
    }
    return detail;
}

QString sourceContextText(const WavePreviewAssignment& assignment,
                          const WavePreviewReport& report)
{
    if (assignment.sourceSignals.isEmpty())
        return QStringLiteral("-");
    QStringList parts;
    for (const QString& source : assignment.sourceSignals) {
        parts.append(QStringLiteral("%1: %2")
                         .arg(source,
                              signalContextBrief(signalContextFor(report,
                                                                 source))));
    }
    return parts.join(QStringLiteral("; "));
}

QString locationText(const WavePreviewAssignment& assignment)
{
    if (assignment.line <= 0)
        return QStringLiteral("-");
    return QStringLiteral("%1:%2").arg(assignment.line).arg(assignment.column);
}

QString triggerText(const WavePreviewAssignment& assignment)
{
    return assignment.trigger.isEmpty()
        ? QStringLiteral("-")
        : assignment.trigger;
}

QString expressionText(const WavePreviewAssignment& assignment)
{
    return assignment.expression.isEmpty()
        ? QStringLiteral("<expr>")
        : assignment.expression;
}

QString assignmentDetailTooltip(const WavePreviewAssignment& assignment,
                                const WavePreviewReport& report)
{
    QString blockText = QStringLiteral("-");
    if (assignment.blockIndex >= 0
        && assignment.blockIndex < report.blocks.size()) {
        blockText = blockKindText(report.blocks.at(assignment.blockIndex).kind);
    }

    return QStringList{
        QStringLiteral("target: %1").arg(assignment.target),
        QStringLiteral("expression: %1").arg(expressionText(assignment)),
        QStringLiteral("sources: %1").arg(sourcesText(assignment.sourceSignals)),
        QStringLiteral("target context: %1")
            .arg(signalContextDetail(signalContextFor(report,
                                                      assignment.target))),
        QStringLiteral("source context: %1")
            .arg(sourceContextText(assignment, report)),
        QStringLiteral("kind: %1").arg(assignmentKindText(assignment.kind)),
        QStringLiteral("block: %1").arg(blockText),
        QStringLiteral("timing: %1").arg(timingText(assignment)),
        QStringLiteral("trigger: %1").arg(triggerText(assignment)),
        QStringLiteral("clock/reset: %1").arg(clockResetText(assignment, report)),
        QStringLiteral("guard: %1").arg(guardText(assignment)),
        QStringLiteral("location: %1").arg(locationText(assignment))
    }.join(QStringLiteral("\n"));
}

QString canvasEventSelectionText(const WavePreviewAssignment& assignment,
                                 const WavePreviewReport& report)
{
    QStringList parts;
    parts.append(QStringLiteral("Selected %1").arg(assignment.target));
    parts.append(timingText(assignment));
    if (!assignment.guardText.isEmpty())
        parts.append(QStringLiteral("guard %1").arg(assignment.guardText));
    const QString sources = sourcesText(assignment.sourceSignals);
    if (sources != QStringLiteral("-"))
        parts.append(QStringLiteral("sources %1").arg(sources));
    const QString clockReset = clockResetText(assignment, report);
    if (clockReset != QStringLiteral("-"))
        parts.append(clockReset);
    const QString location = locationText(assignment);
    if (location != QStringLiteral("-"))
        parts.append(location);
    return parts.join(QStringLiteral(" - "));
}

QString canvasLaneSelectionText(const WavePreviewLane& lane)
{
    QStringList parts;
    parts.append(QStringLiteral("Selected lane %1").arg(lane.signalName));
    parts.append(laneSummaryText(lane.summary));
    const QString activity = laneActivityText(lane.summary);
    if (activity != QStringLiteral("-"))
        parts.append(QStringLiteral("activity %1").arg(activity));
    const QString warnings = laneWarningText(lane.summary);
    if (warnings != QStringLiteral("-"))
        parts.append(QStringLiteral("warnings %1").arg(warnings));
    const QString context =
        signalContextBrief(lane.context.isValid() ? &lane.context : nullptr);
    if (context != QStringLiteral("-"))
        parts.append(QStringLiteral("context %1").arg(context));
    return parts.join(QStringLiteral(" - "));
}

QString laneDetailTooltip(const WavePreviewLane& lane)
{
    QStringList sources;
    for (const WavePreviewAssignment& assignment : lane.assignments) {
        for (const QString& source : assignment.sourceSignals) {
            if (!sources.contains(source))
                sources.append(source);
        }
    }
    sources.sort();
    return QStringList{
        QStringLiteral("signal: %1").arg(lane.signalName),
        QStringLiteral("context: %1")
            .arg(signalContextDetail(lane.context.isValid()
                                     ? &lane.context
                                     : nullptr)),
        QStringLiteral("summary: %1").arg(laneSummaryText(lane.summary)),
        QStringLiteral("activity: %1").arg(laneActivityText(lane.summary)),
        QStringLiteral("guards: %1").arg(laneGuardText(lane.summary)),
        QStringLiteral("warnings: %1").arg(laneWarningText(lane.summary)),
        QStringLiteral("sources: %1").arg(sourcesText(sources))
    }.join(QStringLiteral("\n"));
}

QString eventText(const WavePreviewAssignment& assignment,
                  const WavePreviewReport& report)
{
    QString prefix = assignmentKindText(assignment.kind);
    if (assignment.blockIndex >= 0
        && assignment.blockIndex < report.blocks.size()) {
        prefix = blockKindText(report.blocks.at(assignment.blockIndex).kind);
    }
    return QStringLiteral("%1  %2 = %3")
        .arg(prefix,
             assignment.target,
             assignment.expression.isEmpty()
                 ? QStringLiteral("<expr>")
                 : assignment.expression);
}

QString canvasEventLabel(const WavePreviewAssignment& assignment)
{
    QString label = assignment.target;
    if (!assignment.guardText.isEmpty())
        label += QStringLiteral(" if ") + assignment.guardText;
    return label;
}

void setNavigationData(QTreeWidgetItem* item,
                       const QString& fileName,
                       int line,
                       int column)
{
    if (!item)
        return;
    item->setData(0, kRoleFileName, fileName);
    item->setData(0, kRoleLine, line);
    item->setData(0, kRoleColumn, column);
}

void setItemTooltip(QTreeWidgetItem* item, const QString& tooltip)
{
    if (!item)
        return;
    for (int column = 0; column < item->columnCount(); ++column)
        item->setToolTip(column, tooltip);
}
}

WavePreviewPanelCoordinator::WavePreviewPanelCoordinator(QWidget* parent)
{
    auto* panel = new QWidget(parent);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    titleLabel = new QLabel(QStringLiteral("Wave Preview"), panel);
    titleLabel->setObjectName(QStringLiteral("wavePreviewTitle"));
    titleLabel->setStyleSheet(QStringLiteral(
        "QLabel#wavePreviewTitle {"
        "  color: #0f172a;"
        "  padding: 3px 4px;"
        "  font-weight: 600;"
        "}"));
    layout->addWidget(titleLabel);

    summaryLabel = new QLabel(QStringLiteral("No document selected."), panel);
    summaryLabel->setObjectName(QStringLiteral("wavePreviewSummary"));
    summaryLabel->setStyleSheet(QStringLiteral(
        "QLabel#wavePreviewSummary {"
        "  color: #475569;"
        "  padding: 0 4px 3px 4px;"
        "}"));
    layout->addWidget(summaryLabel);

    auto* canvas = new WavePreviewCanvas(panel);
    canvas->setNavigationHandler(
        [this](const QString& fileName, int line, int column) {
            if (!navigationHandler || line <= 0)
                return;
            navigationHandler(fileName.isEmpty() ? currentFileName : fileName,
                              line,
                              qMax(1, column));
        });
    canvas->setSelectionHandler([this](const QString& selectionText) {
        if (!summaryLabel)
            return;
        summaryLabel->setText(selectionText.isEmpty()
                                  ? currentSummaryText
                                  : selectionText);
    });
    previewCanvas = canvas;
    layout->addWidget(previewCanvas, 0);

    previewTree = new QTreeWidget(panel);
    previewTree->setObjectName(QStringLiteral("wavePreviewTree"));
    previewTree->setColumnCount(7);
    previewTree->setHeaderLabels({
        QStringLiteral("Signal / Event"),
        QStringLiteral("Timing"),
        QStringLiteral("Clock/Reset"),
        QStringLiteral("Guard"),
        QStringLiteral("Sources"),
        QStringLiteral("Context"),
        QStringLiteral("Location")
    });
    previewTree->setAlternatingRowColors(true);
    previewTree->setUniformRowHeights(true);
    previewTree->setRootIsDecorated(true);
    previewTree->header()->setStretchLastSection(false);
    previewTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    previewTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    previewTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    previewTree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    previewTree->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    previewTree->header()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    previewTree->header()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
    layout->addWidget(previewTree, 1);

    previewDock = new QDockWidget(QStringLiteral("Wave Preview"), parent);
    previewDock->setObjectName(QStringLiteral("wavePreviewDock"));
    previewDock->setWidget(panel);
    previewDock->setFeatures(QDockWidget::DockWidgetMovable |
                             QDockWidget::DockWidgetFloatable |
                             QDockWidget::DockWidgetClosable);
    previewDock->hide();

    refreshTimer = new QTimer(previewDock);
    refreshTimer->setSingleShot(true);
    refreshTimer->setInterval(kLargeDirtyRefreshDelayMs);
    QObject::connect(refreshTimer,
                     &QTimer::timeout,
                     previewDock,
                     [this]() {
                         flushQueuedRefresh();
                     });
    QObject::connect(previewDock,
                     &QDockWidget::visibilityChanged,
                     previewDock,
                     [this](bool visible) {
                         if (!visible)
                             clearQueuedRefresh();
                     });

    QObject::connect(previewTree,
                     &QTreeWidget::itemDoubleClicked,
                     previewTree,
                     [this](QTreeWidgetItem* item, int) {
                         navigateItem(item);
                     });
}

WavePreviewPanelCoordinator::~WavePreviewPanelCoordinator() = default;

void WavePreviewPanelCoordinator::setNavigationHandler(
    std::function<void(const QString&, int, int)> handler)
{
    navigationHandler = std::move(handler);
}

void WavePreviewPanelCoordinator::refreshFromDocument(
    const QString& fileName,
    const QString& documentText,
    bool dirty,
    int scopeStartPosition,
    int scopeEndPosition,
    const QString& scopeLabel)
{
    currentFileName = fileName;
    if (documentText.trimmed().isEmpty()) {
        renderUnavailable(QStringLiteral("No SystemVerilog text to preview."));
        return;
    }

    if (shouldQueueRefresh(documentText, dirty)) {
        queueRefresh(fileName,
                     documentText,
                     dirty,
                     scopeStartPosition,
                     scopeEndPosition,
                     scopeLabel);
        return;
    }

    clearQueuedRefresh();
    renderDocumentNow(fileName,
                      documentText,
                      dirty,
                      scopeStartPosition,
                      scopeEndPosition,
                      scopeLabel);
}

void WavePreviewPanelCoordinator::queueRefresh(
    const QString& fileName,
    const QString& documentText,
    bool dirty,
    int scopeStartPosition,
    int scopeEndPosition,
    const QString& scopeLabel)
{
    pendingFileName = fileName;
    pendingDocumentText = documentText;
    pendingScopeStartPosition = scopeStartPosition;
    pendingScopeEndPosition = scopeEndPosition;
    pendingScopeLabel = scopeLabel;
    pendingDirty = dirty;
    pendingRefresh = true;
    currentFileName = fileName;

    if (titleLabel) {
        const QString titleScope =
            scopeLabel.isEmpty()
                ? QString()
                : QStringLiteral(" - %1").arg(scopeLabel);
        titleLabel->setText(QStringLiteral("Wave Preview - %1%2")
                                .arg(displayFileName(fileName), titleScope));
    }
    if (summaryLabel) {
        currentSummaryText = QStringLiteral("Refresh queued for large buffer");
        summaryLabel->setText(currentSummaryText);
    }

    if (refreshTimer)
        refreshTimer->start();
}

void WavePreviewPanelCoordinator::flushQueuedRefresh()
{
    if (!pendingRefresh)
        return;

    const QString fileName = pendingFileName;
    const QString documentText = pendingDocumentText;
    const int scopeStartPosition = pendingScopeStartPosition;
    const int scopeEndPosition = pendingScopeEndPosition;
    const QString scopeLabel = pendingScopeLabel;
    const bool dirty = pendingDirty;
    pendingFileName.clear();
    pendingDocumentText.clear();
    pendingScopeStartPosition = -1;
    pendingScopeEndPosition = -1;
    pendingScopeLabel.clear();
    pendingDirty = false;
    pendingRefresh = false;

    if (documentText.trimmed().isEmpty()) {
        renderUnavailable(QStringLiteral("No SystemVerilog text to preview."));
        return;
    }
    renderDocumentNow(fileName,
                      documentText,
                      dirty,
                      scopeStartPosition,
                      scopeEndPosition,
                      scopeLabel);
}

void WavePreviewPanelCoordinator::clearQueuedRefresh()
{
    pendingFileName.clear();
    pendingDocumentText.clear();
    pendingScopeStartPosition = -1;
    pendingScopeEndPosition = -1;
    pendingScopeLabel.clear();
    pendingDirty = false;
    pendingRefresh = false;
    if (refreshTimer)
        refreshTimer->stop();
}

void WavePreviewPanelCoordinator::renderDocumentNow(
    const QString& fileName,
    const QString& documentText,
    bool dirty,
    int scopeStartPosition,
    int scopeEndPosition,
    const QString& scopeLabel)
{
    const WavePreviewReport report =
        WavePreviewService::getInstance()->previewForDocument(
            {fileName,
             documentText,
             SemanticIndex::getInstance()->snapshot(),
             scopeStartPosition,
             scopeEndPosition,
             scopeLabel});
    renderReport(report, fileName, dirty);
}

void WavePreviewPanelCoordinator::renderUnavailable(const QString& message)
{
    clearQueuedRefresh();
    if (titleLabel)
        titleLabel->setText(QStringLiteral("Wave Preview"));
    if (summaryLabel) {
        currentSummaryText = message;
        summaryLabel->setText(message);
    }
    if (auto* canvas = static_cast<WavePreviewCanvas*>(previewCanvas))
        canvas->clearReport();
    if (previewTree)
        previewTree->clear();
}

void WavePreviewPanelCoordinator::renderReport(
    const WavePreviewReport& report,
    const QString& fileName,
    bool dirty)
{
    if (!previewTree)
        return;

    previewTree->clear();
    if (auto* canvas = static_cast<WavePreviewCanvas*>(previewCanvas))
        canvas->setReport(report, fileName);
    if (titleLabel) {
        const QString titleScope =
            report.scoped && !report.scopeLabel.isEmpty()
                ? QStringLiteral(" - %1").arg(report.scopeLabel)
                : QString();
        titleLabel->setText(QStringLiteral("Wave Preview - %1%2")
                                .arg(displayFileName(fileName), titleScope));
    }
    if (summaryLabel) {
        currentSummaryText = reportSummaryText(report, dirty);
        summaryLabel->setText(currentSummaryText);
    }

    if (!report.available) {
        auto* item = new QTreeWidgetItem(previewTree);
        item->setText(0, QStringLiteral("No assign/always code-sketch events found"));
        item->setText(1, QStringLiteral("-"));
        item->setText(2, QStringLiteral("-"));
        item->setText(3, QStringLiteral("-"));
        item->setText(4, QStringLiteral("-"));
        item->setText(5, QStringLiteral("-"));
        item->setText(6, QStringLiteral("-"));
        return;
    }

    if (report.trace.isValid()) {
        auto* traceRoot = new QTreeWidgetItem(previewTree);
        traceRoot->setText(0, QStringLiteral("Waveform Trace"));
        traceRoot->setText(1,
                           QStringLiteral("%1 cycles")
                               .arg(report.trace.cycleCount));
        traceRoot->setText(2, QStringLiteral("-"));
        traceRoot->setText(3, QStringLiteral("-"));
        traceRoot->setText(4,
                           QStringLiteral("%1 signals")
                               .arg(report.trace.traceSignals.size()));
        traceRoot->setText(5, QStringLiteral("local waveform"));
        traceRoot->setText(6, QStringLiteral("-"));
        QFont traceFont = traceRoot->font(0);
        traceFont.setBold(true);
        traceRoot->setFont(0, traceFont);
        setItemTooltip(traceRoot,
                       QStringLiteral("local waveform trace: %1 signals, %2 cycles")
                           .arg(report.trace.traceSignals.size())
                           .arg(report.trace.cycleCount));
        for (const WavePreviewTraceSignal& signal : report.trace.traceSignals) {
            auto* signalItem = new QTreeWidgetItem(traceRoot);
            signalItem->setText(0, signal.signalName);
            signalItem->setText(1,
                                signal.values.join(QStringLiteral(" -> ")));
            signalItem->setText(2,
                                signal.clock ? QStringLiteral("clock")
                                             : QStringLiteral("-"));
            signalItem->setText(3, QStringLiteral("-"));
            signalItem->setText(4,
                                QStringLiteral("%1-bit").arg(signal.width));
            signalItem->setText(5, QStringLiteral("trace"));
            signalItem->setText(6, QStringLiteral("-"));
            setItemTooltip(
                signalItem,
                QStringLiteral("%1: %2")
                    .arg(signal.signalName,
                         signal.values.join(QStringLiteral(" -> "))));
        }
        traceRoot->setExpanded(true);
    }

    if (!report.warnings.isEmpty()) {
        auto* warningRoot = new QTreeWidgetItem(previewTree);
        warningRoot->setText(0, QStringLiteral("Warnings"));
        warningRoot->setText(1,
                             countText(report.warnings.size(),
                                       QStringLiteral("warning"),
                                       QStringLiteral("warnings")));
        warningRoot->setText(2, QStringLiteral("-"));
        warningRoot->setText(3, QStringLiteral("-"));
        warningRoot->setText(4, QStringLiteral("report"));
        warningRoot->setText(5, QStringLiteral("-"));
        warningRoot->setText(6, QStringLiteral("-"));
        QFont warningFont = warningRoot->font(0);
        warningFont.setBold(true);
        warningRoot->setFont(0, warningFont);
        setItemTooltip(warningRoot,
                       report.warnings.join(QStringLiteral("\n")));

        for (const QString& warning : report.warnings) {
            auto* warningItem = new QTreeWidgetItem(warningRoot);
            warningItem->setText(0, warning);
            warningItem->setText(1, QStringLiteral("-"));
            warningItem->setText(2, QStringLiteral("-"));
            warningItem->setText(3, QStringLiteral("-"));
            warningItem->setText(4, QStringLiteral("warning"));
            warningItem->setText(5, QStringLiteral("-"));
            warningItem->setText(6, QStringLiteral("-"));
            setItemTooltip(warningItem, warning);
        }
        warningRoot->setExpanded(true);
    }

    if (!report.clockResetGroups.isEmpty()) {
        auto* groupRoot = new QTreeWidgetItem(previewTree);
        groupRoot->setText(0, QStringLiteral("Clock/Reset Groups"));
        groupRoot->setText(1,
                           QStringLiteral("%1 groups")
                               .arg(report.clockResetGroups.size()));
        groupRoot->setText(2, QStringLiteral("-"));
        groupRoot->setText(3, QStringLiteral("-"));
        groupRoot->setText(4, QStringLiteral("-"));
        groupRoot->setText(5, QStringLiteral("-"));
        groupRoot->setText(6, QStringLiteral("-"));
        QFont groupFont = groupRoot->font(0);
        groupFont.setBold(true);
        groupRoot->setFont(0, groupFont);

        for (const WavePreviewClockResetGroup& group : report.clockResetGroups) {
            auto* groupItem = new QTreeWidgetItem(groupRoot);
            groupItem->setText(0, clockResetText(group.clockSignals,
                                                 group.resetSignals,
                                                 group.clockEdgeSignals,
                                                 group.resetEdgeSignals));
            groupItem->setText(1,
                               QStringLiteral("%1 blocks, %2 events")
                                   .arg(group.blockIndexes.size())
                                   .arg(group.assignmentCount));
            groupItem->setText(2, QStringLiteral("-"));
            groupItem->setText(3, QStringLiteral("-"));
            groupItem->setText(4, QStringLiteral("-"));
            groupItem->setText(5, QStringLiteral("-"));
            groupItem->setText(6, QStringLiteral("-"));
        }
        groupRoot->setExpanded(true);
    }

    if (report.activitySummary.isValid()) {
        auto* activityRoot = new QTreeWidgetItem(previewTree);
        activityRoot->setText(0, QStringLiteral("Activity Mix"));
        activityRoot->setText(1,
                              countText(report.activitySummary.eventCount,
                                        QStringLiteral("event"),
                                        QStringLiteral("events")));
        activityRoot->setText(2, QStringLiteral("-"));
        activityRoot->setText(3, QStringLiteral("-"));
        activityRoot->setText(4, activitySummaryText(report.activitySummary));
        activityRoot->setText(5, QStringLiteral("report"));
        activityRoot->setText(6, QStringLiteral("-"));
        QFont activityFont = activityRoot->font(0);
        activityFont.setBold(true);
        activityRoot->setFont(0, activityFont);
        setItemTooltip(
            activityRoot,
            QStringList{
                QStringLiteral("activity: %1")
                    .arg(activitySummaryText(report.activitySummary)),
                QStringLiteral("events: %1")
                    .arg(report.activitySummary.eventCount)
            }.join(QStringLiteral("\n")));
    }

    for (const WavePreviewLane& lane : report.lanes) {
        auto* laneItem = new QTreeWidgetItem(previewTree);
        laneItem->setText(0, lane.signalName);
        laneItem->setText(1, laneSummaryText(lane.summary));
        laneItem->setText(2, QStringLiteral("-"));
        laneItem->setText(3, laneGuardText(lane.summary));
        laneItem->setText(4, laneActivityText(lane.summary));
        laneItem->setText(5, signalContextBrief(lane.context.isValid()
                                                ? &lane.context
                                                : nullptr));
        laneItem->setText(6, QStringLiteral("-"));
        QFont laneFont = laneItem->font(0);
        laneFont.setBold(true);
        laneItem->setFont(0, laneFont);
        setItemTooltip(laneItem, laneDetailTooltip(lane));

        for (const WavePreviewAssignment& assignment : lane.assignments) {
            auto* eventItem = new QTreeWidgetItem(laneItem);
            eventItem->setText(0, eventText(assignment, report));
            eventItem->setText(1, timingText(assignment));
            eventItem->setText(2, clockResetText(assignment, report));
            eventItem->setText(3, guardText(assignment));
            eventItem->setText(4, sourcesText(assignment.sourceSignals));
            eventItem->setText(5,
                               signalContextBrief(signalContextFor(
                                   report,
                                   assignment.target)));
            eventItem->setText(6,
                               locationText(assignment));
            setItemTooltip(eventItem,
                           assignmentDetailTooltip(assignment, report));
            setNavigationData(eventItem,
                              fileName,
                              assignment.line,
                              assignment.column);
        }
        laneItem->setExpanded(true);
    }
}

void WavePreviewPanelCoordinator::navigateItem(QTreeWidgetItem* item) const
{
    if (!item || !navigationHandler)
        return;

    const QString fileName = item->data(0, kRoleFileName).toString();
    const int line = item->data(0, kRoleLine).toInt();
    const int column = item->data(0, kRoleColumn).toInt();
    if (line <= 0)
        return;
    navigationHandler(fileName.isEmpty() ? currentFileName : fileName,
                      line,
                      qMax(1, column));
}
