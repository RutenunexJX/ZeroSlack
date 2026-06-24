#include "wavepreviewpanelcoordinator.h"

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
#include <utility>

namespace {
constexpr int kRoleFileName = Qt::UserRole + 1;
constexpr int kRoleLine = Qt::UserRole + 2;
constexpr int kRoleColumn = Qt::UserRole + 3;
constexpr qsizetype kLargeDirtyRefreshCharacterThreshold = 64 * 1024;
constexpr int kLargeDirtyRefreshDelayMs = 180;

QString canvasEventLabel(const WavePreviewAssignment& assignment);
QString assignmentDetailTooltip(const WavePreviewAssignment& assignment,
                                const WavePreviewReport& report);

bool shouldQueueRefresh(const QString& documentText, bool)
{
    return documentText.size() >= kLargeDirtyRefreshCharacterThreshold;
}

struct CanvasEventHit {
    QRect rect;
    QString tooltip;
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
        const int laneCount = report.available ? report.lanes.size() : 2;
        return QSize(460, qBound(132, 52 + laneCount * 34, 260));
    }

    void setReport(const WavePreviewReport& nextReport)
    {
        report = nextReport;
        eventHits.clear();
        setToolTip(QString());
        updateGeometry();
        update();
    }

    void clearReport()
    {
        report = WavePreviewReport();
        eventHits.clear();
        setToolTip(QString());
        updateGeometry();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        eventHits.clear();
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
                             QStringLiteral("No waveform sketch"));
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

            painter.setPen(QPen(QColor(QStringLiteral("#e2e8f0"))));
            painter.drawLine(left, centerY, timelineRight, centerY);
            painter.setPen(QColor(QStringLiteral("#0f172a")));
            painter.drawText(QRect(left, y + 4, labelWidth - 8, rowHeight - 8),
                             Qt::AlignVCenter | Qt::AlignRight,
                             lane.signalName);

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

                painter.setPen(QPen(fill.darker(125)));
                painter.setBrush(fill.lighter(180));
                painter.drawRoundedRect(eventRect, 4, 4);
                painter.setPen(QColor(QStringLiteral("#111827")));
                painter.drawText(eventRect.adjusted(5, 0, -5, 0),
                                 Qt::AlignCenter,
                                 canvasEventLabel(assignment));
                eventHits.append({eventRect,
                                  assignmentDetailTooltip(assignment, report)});
            }
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        for (auto it = eventHits.crbegin(); it != eventHits.crend(); ++it) {
            if (!it->rect.contains(event->pos()))
                continue;
            setToolTip(it->tooltip);
            QToolTip::showText(event->globalPosition().toPoint(),
                               it->tooltip,
                               this,
                               it->rect);
            return;
        }
        setToolTip(QString());
        QToolTip::hideText();
    }

    void leaveEvent(QEvent*) override
    {
        setToolTip(QString());
        QToolTip::hideText();
    }

private:
    WavePreviewReport report;
    QVector<CanvasEventHit> eventHits;
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
        QStringLiteral("events: %1").arg(lane.assignments.size()),
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
    bool dirty)
{
    currentFileName = fileName;
    if (documentText.trimmed().isEmpty()) {
        renderUnavailable(QStringLiteral("No SystemVerilog text to preview."));
        return;
    }

    if (shouldQueueRefresh(documentText, dirty)) {
        queueRefresh(fileName, documentText, dirty);
        return;
    }

    clearQueuedRefresh();
    renderDocumentNow(fileName, documentText, dirty);
}

void WavePreviewPanelCoordinator::queueRefresh(
    const QString& fileName,
    const QString& documentText,
    bool dirty)
{
    pendingFileName = fileName;
    pendingDocumentText = documentText;
    pendingDirty = dirty;
    pendingRefresh = true;
    currentFileName = fileName;

    if (titleLabel) {
        titleLabel->setText(QStringLiteral("Wave Preview - %1")
                                .arg(displayFileName(fileName)));
    }
    if (summaryLabel) {
        summaryLabel->setText(QStringLiteral("Refresh queued for large buffer"));
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
    const bool dirty = pendingDirty;
    pendingFileName.clear();
    pendingDocumentText.clear();
    pendingDirty = false;
    pendingRefresh = false;

    if (documentText.trimmed().isEmpty()) {
        renderUnavailable(QStringLiteral("No SystemVerilog text to preview."));
        return;
    }
    renderDocumentNow(fileName, documentText, dirty);
}

void WavePreviewPanelCoordinator::clearQueuedRefresh()
{
    pendingFileName.clear();
    pendingDocumentText.clear();
    pendingDirty = false;
    pendingRefresh = false;
    if (refreshTimer)
        refreshTimer->stop();
}

void WavePreviewPanelCoordinator::renderDocumentNow(
    const QString& fileName,
    const QString& documentText,
    bool dirty)
{
    const WavePreviewReport report =
        WavePreviewService::getInstance()->previewForDocument(
            {fileName, documentText});
    renderReport(report, fileName, dirty);
}

void WavePreviewPanelCoordinator::renderUnavailable(const QString& message)
{
    clearQueuedRefresh();
    if (titleLabel)
        titleLabel->setText(QStringLiteral("Wave Preview"));
    if (summaryLabel)
        summaryLabel->setText(message);
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
        canvas->setReport(report);
    if (titleLabel) {
        titleLabel->setText(QStringLiteral("Wave Preview - %1")
                                .arg(displayFileName(fileName)));
    }
    if (summaryLabel) {
        summaryLabel->setText(
            QStringLiteral("%1 lanes, %2 events, %3 blocks, %4 clock/reset groups%5")
                .arg(report.lanes.size())
                .arg(report.assignmentCount)
                .arg(report.blocks.size())
                .arg(report.clockResetGroups.size())
                .arg(dirty ? QStringLiteral(" - live dirty buffer")
                           : QString()));
    }

    if (!report.available) {
        auto* item = new QTreeWidgetItem(previewTree);
        item->setText(0, QStringLiteral("No assign/always waveform sketch found"));
        item->setText(1, QStringLiteral("-"));
        item->setText(2, QStringLiteral("-"));
        item->setText(3, QStringLiteral("-"));
        item->setText(4, QStringLiteral("-"));
        item->setText(5, QStringLiteral("-"));
        item->setText(6, QStringLiteral("-"));
        return;
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

    for (const WavePreviewLane& lane : report.lanes) {
        auto* laneItem = new QTreeWidgetItem(previewTree);
        laneItem->setText(0, lane.signalName);
        laneItem->setText(1, QStringLiteral("%1 events").arg(lane.assignments.size()));
        laneItem->setText(2, QStringLiteral("-"));
        laneItem->setText(3, QStringLiteral("-"));
        laneItem->setText(4, QStringLiteral("-"));
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
