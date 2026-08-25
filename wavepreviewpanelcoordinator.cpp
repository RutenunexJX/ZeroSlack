#include "wavepreviewpanelcoordinator.h"

#include "applicationthememanager.h"
#include "graphexportui.h"
#include "insightvisualstyle.h"
#include "semanticindex.h"
#include "waveformpreviewloader.h"
#include "wavepreviewpayloadadapter.h"
#include "wavesimulationconfiguration.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QFont>
#include <QFontMetrics>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QSignalBlocker>
#include <QToolButton>
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
constexpr int kRoleDefaultExpanded = Qt::UserRole + 4;
constexpr int kRoleStableIdentity = Qt::UserRole + 5;

QString assignmentDetailTooltip(const WavePreviewAssignment& assignment,
                                const WavePreviewReport& report);
QString laneDetailTooltip(const WavePreviewLane& lane);
QString laneSummaryText(const WavePreviewLaneSummary& summary);
QString laneWarningText(const WavePreviewLaneSummary& summary);
QString reportSummaryText(const WavePreviewReport& report, bool dirty);
QString sketchLegendText(const WavePreviewReport& report);

class WaveformPreviewSurface final : public QWidget
{
public:
    explicit WaveformPreviewSurface(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("wavePreviewCanvas"));
        setMinimumHeight(132);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        surfaceLayout = new QVBoxLayout(this);
        surfaceLayout->setContentsMargins(0, 0, 0, 0);
        surfaceLayout->setSpacing(0);
        setUnavailableMessage(
            QStringLiteral("Waveform component is not configured."));
    }

    QSize sizeHint() const override
    {
        return QSize(460,
                     qBound(144,
                            66 + sizeHintLaneCapacity * 34,
                            280));
    }

    void installContent(QWidget* nextContent)
    {
        if (!nextContent || nextContent == content)
            return;
        const QSize previousSizeHint = sizeHint();
        if (content) {
            QWidget* previousContent = content.data();
            surfaceLayout->removeWidget(previousContent);
            content = nullptr;
            delete previousContent;
        }
        content = nextContent;
        content->setParent(this);
        surfaceLayout->addWidget(content);
        if (sizeHint() != previousSizeHint)
            updateGeometry();
        updateCompactMode();
    }

    void setUnavailableMessage(const QString& message)
    {
        if (content && content->objectName()
                != QStringLiteral("wavePreviewUnavailable")) {
            return;
        }
        QLabel* label = qobject_cast<QLabel*>(content.data());
        if (!label) {
            label = new QLabel(this);
            label->setObjectName(QStringLiteral("wavePreviewUnavailable"));
            label->setAlignment(Qt::AlignCenter);
            label->setWordWrap(true);
            InsightVisualStyle::applyLabel(label);
            installContent(label);
        }
        label->setText(message);
    }

    void setReportShape(const WavePreviewReport& report,
                        const QString& fileName)
    {
        const QSize previousSizeHint = sizeHint();
        const int nextLaneCount = report.trace.isValid()
            ? report.trace.traceSignals.size()
            : (report.available ? report.lanes.size() : 2);
        const bool sameFileSession = reportSessionActive
            && currentFileName == fileName;
        sizeHintLaneCapacity = sameFileSession
            ? qMax(sizeHintLaneCapacity, nextLaneCount)
            : nextLaneCount;
        reportSessionActive = true;
        currentFileName = fileName;
        if (sizeHint() != previousSizeHint)
            updateGeometry();
    }

    void clearReportShape()
    {
        const QSize previousSizeHint = sizeHint();
        currentFileName.clear();
        reportSessionActive = false;
        sizeHintLaneCapacity = 2;
        if (sizeHint() != previousSizeHint)
            updateGeometry();
    }

    void setCompactHandler(std::function<void(bool)> handler)
    {
        compactHandler = std::move(handler);
        updateCompactMode();
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        updateCompactMode();
    }

private:
    void updateCompactMode()
    {
        const bool compact = width() < 720;
        if (compact == lastCompact || !compactHandler)
            return;
        lastCompact = compact;
        compactHandler(compact);
    }

    QVBoxLayout* surfaceLayout = nullptr;
    QPointer<QWidget> content;
    QString currentFileName;
    bool reportSessionActive = false;
    int sizeHintLaneCapacity = 2;
    bool lastCompact = false;
    std::function<void(bool)> compactHandler;
};

QString displayFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QStringLiteral("Untitled");
    return QFileInfo(fileName).fileName();
}

void setLabelTextIfChanged(QLabel* label, const QString& text)
{
    if (label && label->text() != text)
        label->setText(text);
}

void replaceComboItemsIfChanged(QComboBox* combo,
                                const QStringList& items)
{
    if (!combo)
        return;
    bool matches = combo->count() == items.size();
    for (int index = 0; matches && index < items.size(); ++index)
        matches = combo->itemText(index) == items.at(index);
    if (matches)
        return;

    const QString previousSelection = combo->currentText();
    QSignalBlocker blocker(combo);
    combo->clear();
    combo->addItems(items);
    const int previousIndex = combo->findText(previousSelection);
    if (previousIndex >= 0)
        combo->setCurrentIndex(previousIndex);
}

void appendUnique(QStringList* items, const QString& value)
{
    if (!value.isEmpty() && !items->contains(value))
        items->append(value);
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

QString sketchLegendText(const WavePreviewReport& report)
{
    if (report.trace.isValid())
        return QStringLiteral("symbolic preview only - no testbench/simulator");
    return QStringLiteral("code sketch only - assign, blocking, nonblocking");
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
        return QStringLiteral("%1symbolic preview-only waveform, no testbench: %2 signals, %3 cycles, %4 lanes, %5 events, activity %6%7%8%9")
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
    return QStringLiteral("%1symbolic code preview, not simulated waveform: %2 lanes, %3 events, %4 blocks, %5 clock/reset groups, activity %6%7%8%9")
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

void setItemDefaultExpanded(QTreeWidgetItem* item, bool expanded)
{
    if (!item)
        return;
    item->setData(0, kRoleDefaultExpanded, expanded);
    if (item->treeWidget())
        item->setExpanded(expanded);
}

void setItemBold(QTreeWidgetItem* item, int column = 0)
{
    if (!item)
        return;
    QFont font = item->font(column);
    font.setBold(true);
    item->setFont(column, font);
}

bool laneMatchesFilter(const WavePreviewLane& lane, const QString& filter)
{
    if (filter.trimmed().isEmpty())
        return true;
    const QString needle = filter.trimmed();
    QStringList haystack{
        lane.signalName,
        lane.context.label(),
        lane.context.declarationText,
        laneSummaryText(lane.summary),
        laneGuardText(lane.summary),
        laneWarningText(lane.summary)
    };
    for (const WavePreviewAssignment& assignment : lane.assignments) {
        haystack.append(assignment.expression);
        haystack.append(assignment.guardText);
        haystack.append(assignment.trigger);
        haystack.append(assignment.sourceSignals);
    }
    return haystack.join(QLatin1Char(' '))
        .contains(needle, Qt::CaseInsensitive);
}

bool traceSignalMatchesFilter(const WavePreviewTraceSignal& signal,
                              const QString& filter)
{
    if (filter.trimmed().isEmpty())
        return true;
    const QString haystack =
        QStringList{signal.signalName,
                    signal.values.join(QStringLiteral(" ")),
                    signal.clock ? QStringLiteral("clock") : QString()}
            .join(QLatin1Char(' '));
    return haystack.contains(filter.trimmed(), Qt::CaseInsensitive);
}

bool copyTreeItemRoleIfChanged(QTreeWidgetItem* target,
                               const QTreeWidgetItem* source,
                               int column,
                               int role)
{
    const QVariant value = source->data(column, role);
    if (target->data(column, role) == value)
        return false;
    target->setData(column, role, value);
    return true;
}

void recordTreeColumnWidth(const QTreeWidgetItem* item,
                           int column,
                           QVector<int>* desiredColumnWidths)
{
    if (!item || column <= 0 || !desiredColumnWidths)
        return;
    if (desiredColumnWidths->size() <= column)
        desiredColumnWidths->resize(column + 1);
    const int desiredWidth =
        QFontMetrics(item->font(column))
            .horizontalAdvance(item->text(column))
        + 28;
    (*desiredColumnWidths)[column] =
        qMax((*desiredColumnWidths)[column], desiredWidth);
}

bool treeItemsHaveSameIdentity(const QTreeWidgetItem* left,
                               const QTreeWidgetItem* right)
{
    if (!left || !right)
        return false;
    const QVariant rightIdentity =
        right->data(0, kRoleStableIdentity);
    if (rightIdentity.isValid() && !rightIdentity.toString().isEmpty()) {
        return left->data(0, kRoleStableIdentity) == rightIdentity;
    }
    return left->data(0, Qt::DisplayRole)
        == right->data(0, Qt::DisplayRole);
}

bool treeStructureMatches(const QTreeWidgetItem* target,
                          const QTreeWidgetItem* source)
{
    if (!treeItemsHaveSameIdentity(target, source)
        || target->childCount() != source->childCount()) {
        return false;
    }
    for (int index = 0; index < source->childCount(); ++index) {
        if (!treeStructureMatches(target->child(index),
                                  source->child(index))) {
            return false;
        }
    }
    return true;
}

void synchronizeTreeItem(QTreeWidget* tree,
                         QTreeWidgetItem* target,
                         const QTreeWidgetItem* source,
                         bool isNew,
                         QVector<int>* desiredColumnWidths)
{
    const int columnCount =
        qMax(target->columnCount(), source->columnCount());
    for (int column = 0; column < columnCount; ++column) {
        const bool displayChanged = copyTreeItemRoleIfChanged(
            target, source, column, Qt::DisplayRole);
        copyTreeItemRoleIfChanged(
            target, source, column, Qt::ToolTipRole);
        const bool fontChanged = copyTreeItemRoleIfChanged(
            target, source, column, Qt::FontRole);
        if (displayChanged || fontChanged)
            recordTreeColumnWidth(
                source, column, desiredColumnWidths);
    }
    copyTreeItemRoleIfChanged(target, source, 0, kRoleFileName);
    copyTreeItemRoleIfChanged(target, source, 0, kRoleLine);
    copyTreeItemRoleIfChanged(target, source, 0, kRoleColumn);
    copyTreeItemRoleIfChanged(
        target, source, 0, kRoleDefaultExpanded);
    copyTreeItemRoleIfChanged(
        target, source, 0, kRoleStableIdentity);

    for (int index = 0; index < source->childCount(); ++index) {
        const QTreeWidgetItem* sourceChild = source->child(index);
        int matchingIndex = -1;
        for (int candidate = index;
             candidate < target->childCount();
             ++candidate) {
            if (treeItemsHaveSameIdentity(target->child(candidate),
                                          sourceChild)) {
                matchingIndex = candidate;
                break;
            }
        }

        bool childIsNew = matchingIndex < 0;
        QTreeWidgetItem* targetChild = nullptr;
        if (childIsNew) {
            targetChild = new QTreeWidgetItem;
            target->insertChild(index, targetChild);
        } else if (matchingIndex == index) {
            targetChild = target->child(index);
        } else {
            targetChild = target->takeChild(matchingIndex);
            target->insertChild(index, targetChild);
        }
        synchronizeTreeItem(tree,
                            targetChild,
                            sourceChild,
                            childIsNew,
                            desiredColumnWidths);
    }
    while (target->childCount() > source->childCount())
        delete target->takeChild(target->childCount() - 1);

    if (isNew) {
        const bool defaultExpanded =
            source->data(0, kRoleDefaultExpanded).toBool();
        if (target->isExpanded() != defaultExpanded)
            target->setExpanded(defaultExpanded);
    }
}

void synchronizeTree(QTreeWidget* target,
                     const QTreeWidgetItem& sourceRoot)
{
    if (!target)
        return;

    QVector<int> desiredColumnWidths;
    const bool restoreUpdates = target->updatesEnabled();
    bool structureMatches =
        target->topLevelItemCount() == sourceRoot.childCount();
    for (int index = 0;
         structureMatches && index < sourceRoot.childCount();
         ++index) {
        structureMatches = treeStructureMatches(
            target->topLevelItem(index), sourceRoot.child(index));
    }
    const bool suspendUpdates = restoreUpdates && !structureMatches;
    if (suspendUpdates)
        target->setUpdatesEnabled(false);
    const QSignalBlocker signalBlocker(target);

    for (int index = 0; index < sourceRoot.childCount(); ++index) {
        const QTreeWidgetItem* sourceItem = sourceRoot.child(index);
        int matchingIndex = -1;
        for (int candidate = index;
             candidate < target->topLevelItemCount();
             ++candidate) {
            if (treeItemsHaveSameIdentity(target->topLevelItem(candidate),
                                          sourceItem)) {
                matchingIndex = candidate;
                break;
            }
        }

        bool itemIsNew = matchingIndex < 0;
        QTreeWidgetItem* targetItem = nullptr;
        if (itemIsNew) {
            targetItem = new QTreeWidgetItem;
            target->insertTopLevelItem(index, targetItem);
        } else if (matchingIndex == index) {
            targetItem = target->topLevelItem(index);
        } else {
            targetItem = target->takeTopLevelItem(matchingIndex);
            target->insertTopLevelItem(index, targetItem);
        }
        synchronizeTreeItem(target,
                            targetItem,
                            sourceItem,
                            itemIsNew,
                            &desiredColumnWidths);
    }
    while (target->topLevelItemCount() > sourceRoot.childCount()) {
        delete target->takeTopLevelItem(
            target->topLevelItemCount() - 1);
    }

    QHeaderView* const header = target->header();
    if (header) {
        for (int column = 1;
             column < desiredColumnWidths.size();
             ++column) {
            const int desiredWidth = desiredColumnWidths.at(column);
            if (desiredWidth > header->sectionSize(column))
                header->resizeSection(column, desiredWidth);
        }
    }
    if (suspendUpdates)
        target->setUpdatesEnabled(true);
}

void addOverviewItems(QTreeWidgetItem* root,
                      const WavePreviewReport& report,
                      bool dirty)
{
    if (!root)
        return;

    auto* scopeItem = new QTreeWidgetItem(root);
    scopeItem->setText(0, QStringLiteral("Scope"));
    scopeItem->setText(1,
                       report.scoped && !report.scopeLabel.isEmpty()
                           ? report.scopeLabel
                           : QStringLiteral("full document"));
    scopeItem->setText(2,
                       report.trace.isValid()
                           ? QStringLiteral("symbolic preview")
                           : QStringLiteral("code sketch"));
    scopeItem->setText(3,
                       report.scoped
                           ? QStringLiteral("lines %1-%2")
                                 .arg(report.scopeStartLine)
                                 .arg(report.scopeEndLine)
                           : QStringLiteral("-"));
    scopeItem->setText(4,
                       countText(report.assignmentCount,
                                 QStringLiteral("event"),
                                 QStringLiteral("events")));
    scopeItem->setText(5,
                       countText(report.lanes.size(),
                                 QStringLiteral("lane"),
                                 QStringLiteral("lanes")));
    scopeItem->setText(6,
                       dirty ? QStringLiteral("dirty buffer")
                             : QStringLiteral("-"));
    setItemBold(scopeItem);
    setItemTooltip(
        scopeItem,
        QStringLiteral("Wave Preview report scope. UI consumes a scoped report; it does not simulate or scan the workspace."));

    auto* legendItem = new QTreeWidgetItem(root);
    legendItem->setText(0, QStringLiteral("Legend"));
    legendItem->setText(1, sketchLegendText(report));
    legendItem->setText(2, QStringLiteral("activity %1")
                              .arg(activitySummaryText(report.activitySummary)));
    legendItem->setText(3, QStringLiteral("preview only / no testbench"));
    legendItem->setText(4,
                        report.warnings.isEmpty()
                            ? QStringLiteral("-")
                            : countText(report.warnings.size(),
                                        QStringLiteral("warning"),
                                        QStringLiteral("warnings")));
    legendItem->setText(5, QStringLiteral("readability"));
    legendItem->setText(6, QStringLiteral("-"));
    setItemBold(legendItem);
    setItemTooltip(
        legendItem,
        QStringLiteral("Canvas legend for interpreting the Wave Preview sketch."));
}
}

WavePreviewPanelCoordinator::WavePreviewPanelCoordinator(QWidget* parent)
{
    auto* panel = new QWidget(parent);
    panel->setObjectName(QStringLiteral("wavePreviewPanel"));
    InsightVisualStyle::applyPanel(panel);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    titleLabel = new QLabel(QStringLiteral("Symbolic Preview"), panel);
    titleLabel->setObjectName(QStringLiteral("wavePreviewTitle"));
    InsightVisualStyle::applyTitleLabel(titleLabel);
    layout->addWidget(titleLabel);

    summaryLabel = new QLabel(QStringLiteral("No document selected."), panel);
    summaryLabel->setObjectName(QStringLiteral("wavePreviewSummary"));
    summaryLabel->setWordWrap(true);
    summaryLabel->setContentsMargins(4, 0, 4, 3);
    InsightVisualStyle::applyLabel(summaryLabel);
    layout->addWidget(summaryLabel);

    auto* toolbarLayout = new QHBoxLayout;
    toolbarLayout->setContentsMargins(0, 0, 0, 0);
    toolbarLayout->setSpacing(6);
    scopeLabel = new QLabel(QStringLiteral("Scope: full document"), panel);
    scopeLabel->setObjectName(QStringLiteral("wavePreviewScopeLabel"));
    InsightVisualStyle::applyLabel(scopeLabel);
    clockCombo = new QComboBox(panel);
    clockCombo->setObjectName(QStringLiteral("wavePreviewClockCombo"));
    clockCombo->setMinimumWidth(116);
    resetCombo = new QComboBox(panel);
    resetCombo->setObjectName(QStringLiteral("wavePreviewResetCombo"));
    resetCombo->setMinimumWidth(116);
    laneFilterEdit = new QLineEdit(panel);
    laneFilterEdit->setObjectName(QStringLiteral("wavePreviewLaneFilterEdit"));
    laneFilterEdit->setPlaceholderText(QStringLiteral("Filter lanes"));
    InsightVisualStyle::applySearchField(laneFilterEdit);
    assignsCheck = new QCheckBox(QStringLiteral("Assigns"), panel);
    assignsCheck->setObjectName(QStringLiteral("wavePreviewAssignsCheck"));
    conditionsCheck = new QCheckBox(QStringLiteral("Conditions"), panel);
    conditionsCheck->setObjectName(QStringLiteral("wavePreviewConditionsCheck"));
    stateLabelsCheck = new QCheckBox(QStringLiteral("State labels"), panel);
    stateLabelsCheck->setObjectName(QStringLiteral("wavePreviewStateLabelsCheck"));
    sourceLinesCheck = new QCheckBox(QStringLiteral("Source lines"), panel);
    sourceLinesCheck->setObjectName(QStringLiteral("wavePreviewSourceLinesCheck"));
    exportAction = GraphExportUi::bindRegistryAction(
        panel,
        QString::fromLatin1(
            ActionIds::GraphExportWavePreview),
        [this]() {
            return currentReport.available
                && previewCanvas
                && !previewCanvas->size().isEmpty();
        },
        [this](const QString& outputPath,
               const GraphExportOptions& options) {
            return exportPreview(outputPath, options);
        },
        [this](const QString& message, int timeoutMs) {
            if (statusMessageHandler)
                statusMessageHandler(message, timeoutMs);
        });
    auto* exportButton = new QToolButton(panel);
    exportButton->setObjectName(
        QStringLiteral("wavePreviewExportButton"));
    if (exportAction)
        exportButton->setDefaultAction(exportAction);
    exportButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    for (QCheckBox* checkBox :
         {assignsCheck, conditionsCheck, stateLabelsCheck, sourceLinesCheck}) {
        checkBox->setChecked(true);
        InsightVisualStyle::applySegmentedCheckBox(checkBox);
    }
    toolbarLayout->addWidget(scopeLabel);
    toolbarLayout->addWidget(new QLabel(QStringLiteral("Clock:"), panel));
    toolbarLayout->addWidget(clockCombo);
    toolbarLayout->addWidget(new QLabel(QStringLiteral("Reset:"), panel));
    toolbarLayout->addWidget(resetCombo);
    toolbarLayout->addWidget(laneFilterEdit, 1);
    toolbarLayout->addWidget(assignsCheck);
    toolbarLayout->addWidget(conditionsCheck);
    toolbarLayout->addWidget(stateLabelsCheck);
    toolbarLayout->addWidget(sourceLinesCheck);
    toolbarLayout->addWidget(exportButton);
    layout->addLayout(toolbarLayout);

    auto* canvas = new WaveformPreviewSurface(panel);
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
    previewTree->header()->setSectionResizeMode(1, QHeaderView::Interactive);
    previewTree->header()->setSectionResizeMode(2, QHeaderView::Interactive);
    previewTree->header()->setSectionResizeMode(3, QHeaderView::Interactive);
    previewTree->header()->setSectionResizeMode(4, QHeaderView::Interactive);
    previewTree->header()->setSectionResizeMode(5, QHeaderView::Interactive);
    previewTree->header()->setSectionResizeMode(6, QHeaderView::Interactive);
    for (int column = 1; column < previewTree->columnCount(); ++column) {
        const int headerWidth =
            QFontMetrics(previewTree->headerItem()->font(column))
                .horizontalAdvance(
                    previewTree->headerItem()->text(column))
            + 28;
        previewTree->header()->resizeSection(column, headerWidth);
    }
    layout->addWidget(previewTree, 1);
    focusBaseTreePointSize =
        previewTree->font().pointSizeF();
    if (focusBaseTreePointSize <= 0.0)
        focusBaseTreePointSize = 9.0;

    previewDock = new QDockWidget(QStringLiteral("Wave Preview"), parent);
    previewDock->setObjectName(QStringLiteral("wavePreviewDock"));
    previewDock->setWidget(panel);
    previewDock->setFeatures(QDockWidget::DockWidgetMovable |
                             QDockWidget::DockWidgetFloatable |
                             QDockWidget::DockWidgetClosable);
    previewDock->hide();

    waveformPreviewLoader = std::make_unique<WaveformPreviewLoader>();
    waveformPreviewLoader->setSourceNavigationHandler(
        [this](const QString& sourceFile,
               int sourceLine,
               int sourceColumn,
               const QString&,
               const QString&) {
            if (!navigationHandler || sourceLine <= 0)
                return;
            navigationHandler(resolveNavigationFile(sourceFile),
                              sourceLine,
                              qMax(1, sourceColumn));
        });
    canvas->setCompactHandler([this](bool compact) {
        if (waveformPreviewLoader && waveformView) {
            waveformPreviewLoader->setCompact(
                waveformView.data(), compact);
        }
    });
    themeConnection = QObject::connect(
        &ApplicationThemeManager::instance(),
        &ApplicationThemeManager::themeChanged,
        panel,
        [this](ThemeMode mode) {
            if (waveformPreviewLoader && waveformView) {
                waveformPreviewLoader->setTheme(
                    waveformView.data(),
                    mode == ThemeMode::Dark
                        ? QStringLiteral("dark")
                        : QStringLiteral("light"));
            }
        });
    const WaveSimulationConfiguration waveConfiguration;
    installWaveformView(waveConfiguration.toolPaths().widgetLibrary);

    QObject::connect(previewTree,
                     &QTreeWidget::itemDoubleClicked,
                     previewTree,
                     [this](QTreeWidgetItem* item, int) {
                         navigateItem(item);
                     });
    QObject::connect(laneFilterEdit,
                     &QLineEdit::textChanged,
                     previewDock,
                     [this](const QString& text) {
                         laneFilterText = text.trimmed();
                         renderReport(currentReport,
                                      currentFileName,
                                      currentDirty);
                     });
}

WavePreviewPanelCoordinator::~WavePreviewPanelCoordinator()
{
    QObject::disconnect(themeConnection);
    if (waveformPreviewLoader)
        waveformPreviewLoader->setSourceNavigationHandler({});
    waveformView = nullptr;
}

WavePreviewRefreshMetrics
WavePreviewPanelCoordinator::refreshMetricsForTest() const
{
    return refreshMetrics;
}

void WavePreviewPanelCoordinator::resetRefreshMetricsForTest()
{
    refreshMetrics = {};
    refreshTimingEnabled = true;
}

void WavePreviewPanelCoordinator::setNavigationHandler(
    std::function<void(const QString&, int, int)> handler)
{
    navigationHandler = std::move(handler);
}

void WavePreviewPanelCoordinator::setStatusMessageHandler(
    std::function<void(const QString&, int)> handler)
{
    statusMessageHandler = std::move(handler);
}

void WavePreviewPanelCoordinator::setWaveformLibraryPath(
    const QString& libraryPath)
{
    QWidget* previousView = waveformView.data();
    installWaveformView(libraryPath);
    if (waveformView && waveformView.data() != previousView
        && currentReport.trace.isValid()) {
        renderReport(currentReport, currentFileName, currentDirty);
    }
}

void WavePreviewPanelCoordinator::setWorkspaceRoot(const QString& rootPath)
{
    const QString normalized = QDir::cleanPath(
        QDir::fromNativeSeparators(rootPath.trimmed()));
    workspaceRoot = QDir::isAbsolutePath(normalized)
        ? normalized
        : QString();
    if (waveformView && currentReport.trace.isValid())
        renderReport(currentReport, currentFileName, currentDirty);
}

void WavePreviewPanelCoordinator::installWaveformView(
    const QString& libraryPath)
{
    auto* surface = static_cast<WaveformPreviewSurface*>(previewCanvas);
    if (!surface || !waveformPreviewLoader)
        return;

    const QString requestedPath = QFileInfo(libraryPath).absoluteFilePath();
    if (libraryPath.trimmed().isEmpty()) {
        waveformFailure = QStringLiteral(
            "Waveform component is not configured. Symbolic details remain available below.");
        if (!waveformView)
            surface->setUnavailableMessage(waveformFailure);
        return;
    }
    if (waveformView
        && waveformPreviewLoader->loadedLibraryPath() == requestedPath) {
        return;
    }

    QString failure;
    QWidget* nextView = waveformPreviewLoader->createView(
        requestedPath, surface, &failure);
    if (!nextView) {
        waveformFailure = failure;
        if (waveformView) {
            setWaveformPresentationState(QStringLiteral("failed"), failure);
        } else {
            surface->setUnavailableMessage(
                failure.isEmpty()
                    ? QStringLiteral("Waveform component is unavailable.")
                    : failure);
        }
        return;
    }

    surface->installContent(nextView);
    waveformView = nextView;
    waveformLibraryPath = waveformPreviewLoader->loadedLibraryPath();
    waveformFailure.clear();
    waveformPreviewLoader->setTheme(
        waveformView.data(),
        ApplicationThemeManager::instance().mode() == ThemeMode::Dark
            ? QStringLiteral("dark")
            : QStringLiteral("light"));
    waveformPreviewLoader->setCompact(
        waveformView.data(), surface->width() < 720);
    setWaveformPresentationState(
        QStringLiteral("empty"),
        QStringLiteral("No document selected."));
}

void WavePreviewPanelCoordinator::setWaveformPresentationState(
    const QString& state,
    const QString& message)
{
    if (!waveformPreviewLoader || !waveformView)
        return;
    QString failure;
    if (!waveformPreviewLoader->setPresentationState(
            waveformView.data(), state, message, &failure)
        && !failure.isEmpty()) {
        waveformFailure = failure;
    }
}

QString WavePreviewPanelCoordinator::resolveNavigationFile(
    const QString& sourceFile) const
{
    if (!currentFileName.trimmed().isEmpty())
        return currentFileName;
    const QString portable = QDir::cleanPath(
        QDir::fromNativeSeparators(sourceFile.trimmed()));
    if (portable.isEmpty() || portable == QLatin1String(".")
        || QDir::isAbsolutePath(portable)
        || portable == QLatin1String("..")
        || portable.startsWith(QStringLiteral("../"))) {
        return {};
    }
    return workspaceRoot.isEmpty()
        ? portable
        : QDir(workspaceRoot).absoluteFilePath(portable);
}

void WavePreviewPanelCoordinator::focusFit()
{
    if (waveformPreviewLoader && waveformView)
        waveformPreviewLoader->fitAll(waveformView.data());
    focusZoomFactor = 1.0;
    applyFocusZoom();
    if (!previewTree)
        return;
    for (int column = 1;
         column < previewTree->columnCount();
         ++column) {
        previewTree->resizeColumnToContents(column);
    }
}

void WavePreviewPanelCoordinator::focusZoomIn()
{
    if (waveformPreviewLoader && waveformView)
        waveformPreviewLoader->zoomIn(waveformView.data());
    focusZoomFactor =
        qMin<qreal>(1.75, focusZoomFactor * 1.12);
    applyFocusZoom();
}

void WavePreviewPanelCoordinator::focusZoomOut()
{
    if (waveformPreviewLoader && waveformView)
        waveformPreviewLoader->zoomOut(waveformView.data());
    focusZoomFactor =
        qMax<qreal>(0.75, focusZoomFactor / 1.12);
    applyFocusZoom();
}

void WavePreviewPanelCoordinator::setFocusSearchText(
    const QString& text)
{
    if (laneFilterEdit)
        laneFilterEdit->setText(text);
}

QString WavePreviewPanelCoordinator::focusSearchText() const
{
    return laneFilterEdit
        ? laneFilterEdit->text() : laneFilterText;
}

GraphExportResult WavePreviewPanelCoordinator::exportPreview(
    const QString& outputPath,
    const GraphExportOptions& options) const
{
    return GraphExportService::exportWidget(
        previewCanvas,
        outputPath,
        options);
}

void WavePreviewPanelCoordinator::focusInspector()
{
    if (!previewTree)
        return;
    QTreeWidgetItem* item = previewTree->currentItem();
    if (!item && previewTree->topLevelItemCount() > 0) {
        item = previewTree->topLevelItem(0);
        previewTree->setCurrentItem(item);
    }
    if (item)
        previewTree->scrollToItem(item);
    previewTree->setFocus();
}

void WavePreviewPanelCoordinator::applyFocusZoom()
{
    if (previewTree) {
        QFont font = previewTree->font();
        font.setPointSizeF(
            focusBaseTreePointSize * focusZoomFactor);
        previewTree->setFont(font);
        previewTree->setIndentation(
            qRound(20.0 * focusZoomFactor));
    }
    if (previewCanvas) {
        previewCanvas->setMinimumHeight(
            qRound(132.0 * focusZoomFactor));
        previewCanvas->updateGeometry();
        previewCanvas->update();
    }
}

void WavePreviewPanelCoordinator::refreshFromDocument(
    const QString& fileName,
    const QString& documentText,
    bool dirty,
    int scopeStartPosition,
    int scopeEndPosition,
    const QString& scopeLabel,
    int scopeStartLineZeroBased)
{
    currentFileName = fileName;
    const bool validScope = scopeStartPosition >= 0
        && scopeEndPosition > scopeStartPosition
        && scopeEndPosition <= documentText.size();
    if (validScope) {
        currentSourceIsScoped = true;
        currentScopeStartPosition = scopeStartPosition;
        currentScopeEndPosition = scopeEndPosition;
        currentScopeStartLineZeroBased = qMax(0, scopeStartLineZeroBased);
        currentScopeLabel = scopeLabel;
        currentScopeText = documentText.mid(
            scopeStartPosition, scopeEndPosition - scopeStartPosition);
        ++refreshMetrics.scopeRebuildCount;
        if (currentScopeText.trimmed().isEmpty()) {
            renderUnavailable(QStringLiteral("No SystemVerilog text to preview."));
            return;
        }
        renderDocumentNow(fileName,
                          currentScopeText,
                          dirty,
                          0,
                          currentScopeText.size(),
                          scopeLabel,
                          scopeStartPosition,
                          currentScopeStartLineZeroBased);
        return;
    }

    currentSourceIsScoped = false;
    currentScopeText.clear();
    currentScopeLabel.clear();
    currentScopeStartPosition = -1;
    currentScopeEndPosition = -1;
    currentScopeStartLineZeroBased = 0;
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

void WavePreviewPanelCoordinator::applyDocumentChange(
    const QString& fileName,
    const DocumentChange& change,
    const QString& latestDocumentText,
    bool dirty,
    int scopeStartPosition,
    int scopeEndPosition,
    const QString& scopeLabel,
    int scopeStartLineZeroBased)
{
    applyDocumentChange(
        fileName,
        change,
        latestDocumentText.size(),
        [&latestDocumentText](int position, int length) {
            return latestDocumentText.mid(position, length);
        },
        dirty,
        scopeStartPosition,
        scopeEndPosition,
        scopeLabel,
        scopeStartLineZeroBased);
}

void WavePreviewPanelCoordinator::applyDocumentChange(
    const QString& fileName,
    const DocumentChange& change,
    int latestDocumentLength,
    const std::function<QString(int, int)>& latestDocumentSlice,
    bool dirty,
    int scopeStartPosition,
    int scopeEndPosition,
    const QString& scopeLabel,
    int scopeStartLineZeroBased)
{
    QElapsedTimer scopeCacheTimer;
    if (refreshTimingEnabled)
        scopeCacheTimer.start();

    const bool validScope = scopeStartPosition >= 0
        && scopeEndPosition > scopeStartPosition
        && scopeEndPosition <= latestDocumentLength;
    if (!validScope) {
        if (refreshTimingEnabled) {
            refreshMetrics.scopeCacheUpdateNanoseconds +=
                static_cast<std::uint64_t>(scopeCacheTimer.nsecsElapsed());
        }
        renderUnavailable(
            QStringLiteral("Place the cursor in a module or always block to preview."));
        return;
    }

    bool updatedCachedScope = false;
    if (currentSourceIsScoped && currentFileName == fileName) {
        const int delta = change.characterDelta();
        const bool editWithinScope =
            change.position >= currentScopeStartPosition
            && change.oldEnd() <= currentScopeEndPosition
            && scopeStartPosition == currentScopeStartPosition
            && scopeEndPosition == currentScopeEndPosition + delta
            && scopeStartLineZeroBased
                   == currentScopeStartLineZeroBased;
        if (editWithinScope) {
            const int relativePosition =
                change.position - currentScopeStartPosition;
            const bool cachedFragmentMatches = relativePosition >= 0
                && relativePosition + change.removedLength
                       <= currentScopeText.size()
                && currentScopeText.mid(relativePosition,
                                        change.removedLength)
                       == change.removedText;
            if (cachedFragmentMatches) {
                currentScopeText.replace(relativePosition,
                                         change.removedLength,
                                         change.insertedText);
                updatedCachedScope = true;
            }
        } else {
            const bool editBeforeScope =
                change.oldEnd() <= currentScopeStartPosition
                && scopeStartPosition == currentScopeStartPosition + delta
                && scopeEndPosition == currentScopeEndPosition + delta
                && scopeStartLineZeroBased
                       == currentScopeStartLineZeroBased + change.lineDelta;
            const bool editAfterScope =
                change.position >= currentScopeEndPosition
                && scopeStartPosition == currentScopeStartPosition
                && scopeEndPosition == currentScopeEndPosition
                && scopeStartLineZeroBased
                       == currentScopeStartLineZeroBased;
            updatedCachedScope = editBeforeScope || editAfterScope;
        }
    }

    if (updatedCachedScope) {
        ++refreshMetrics.scopeDeltaUpdateCount;
    } else {
        currentScopeText = latestDocumentSlice
            ? latestDocumentSlice(
                  scopeStartPosition,
                  scopeEndPosition - scopeStartPosition)
            : QString();
        ++refreshMetrics.scopeRebuildCount;
    }

    currentFileName = fileName;
    currentSourceIsScoped = true;
    currentScopeStartPosition = scopeStartPosition;
    currentScopeEndPosition = scopeEndPosition;
    currentScopeStartLineZeroBased = qMax(0, scopeStartLineZeroBased);
    currentScopeLabel = scopeLabel;
    if (refreshTimingEnabled) {
        refreshMetrics.scopeCacheUpdateNanoseconds +=
            static_cast<std::uint64_t>(scopeCacheTimer.nsecsElapsed());
    }
    ++refreshMetrics.documentChangeRenderCount;
    renderDocumentNow(fileName,
                      currentScopeText,
                      dirty,
                      0,
                      currentScopeText.size(),
                      scopeLabel,
                      scopeStartPosition,
                      currentScopeStartLineZeroBased);
}

void WavePreviewPanelCoordinator::renderDocumentNow(
    const QString& fileName,
    const QString& documentText,
    bool dirty,
    int scopeStartPosition,
    int scopeEndPosition,
    const QString& scopeLabel,
    int sourcePositionOffset,
    int sourceLineOffset)
{
    setWaveformPresentationState(
        QStringLiteral("loading"),
        QStringLiteral("Updating Symbolic Preview"));
    QElapsedTimer waveServiceTimer;
    if (refreshTimingEnabled)
        waveServiceTimer.start();

    WavePreviewQuery query;
    query.fileName = fileName;
    query.documentText = documentText;
    query.semanticSnapshot = SemanticIndex::getInstance()->snapshot();
    query.scopeStartPosition = scopeStartPosition;
    query.scopeEndPosition = scopeEndPosition;
    query.scopeLabel = scopeLabel;
    query.sourcePositionOffset = sourcePositionOffset;
    query.sourceLineOffset = sourceLineOffset;
    const WavePreviewReport report =
        WavePreviewService::getInstance()->previewForDocument(query);
    if (refreshTimingEnabled) {
        refreshMetrics.waveServiceNanoseconds +=
            static_cast<std::uint64_t>(waveServiceTimer.nsecsElapsed());
    }
    ++refreshMetrics.renderCount;
    refreshMetrics.lastParsedCharacterCount = documentText.size();
    renderReport(report, fileName, dirty);
}

void WavePreviewPanelCoordinator::renderUnavailable(const QString& message)
{
    currentScopeText.clear();
    currentScopeLabel.clear();
    currentScopeStartPosition = -1;
    currentScopeEndPosition = -1;
    currentScopeStartLineZeroBased = 0;
    currentSourceIsScoped = false;
    currentReport = WavePreviewReport();
    currentDirty = false;
    if (titleLabel)
        titleLabel->setText(QStringLiteral("Symbolic Preview"));
    if (summaryLabel) {
        currentSummaryText = message;
        summaryLabel->setText(message);
    }
    if (scopeLabel)
        scopeLabel->setText(QStringLiteral("Scope: unavailable"));
    if (clockCombo)
        clockCombo->clear();
    if (resetCombo)
        resetCombo->clear();
    if (auto* canvas = static_cast<WaveformPreviewSurface*>(previewCanvas))
        canvas->clearReportShape();
    setWaveformPresentationState(QStringLiteral("empty"), message);
    if (previewTree)
        previewTree->clear();
    GraphExportUi::updateActionAvailability(
        exportAction,
        false);
}

void WavePreviewPanelCoordinator::renderReport(
    const WavePreviewReport& report,
    const QString& fileName,
    bool dirty)
{
    if (!previewTree)
        return;

    QElapsedTimer modelSceneTimer;
    if (refreshTimingEnabled)
        modelSceneTimer.start();
    qint64 canvasUpdateElapsed = 0;

    currentReport = report;
    currentDirty = dirty;
    QTreeWidgetItem stagingRoot;
    if (auto* canvas = static_cast<WaveformPreviewSurface*>(previewCanvas)) {
        QElapsedTimer canvasUpdateTimer;
        if (refreshTimingEnabled)
            canvasUpdateTimer.start();
        canvas->setReportShape(report, fileName);
        if (report.trace.isValid()) {
            QString payloadRoot = workspaceRoot;
            if (payloadRoot.isEmpty() && QFileInfo(fileName).isAbsolute())
                payloadRoot = QFileInfo(fileName).absolutePath();
            const WavePreviewPayloadBuildResult payload =
                WavePreviewPayloadAdapter::buildSymbolic(
                    report, fileName, payloadRoot);
            if (!payload.ok()) {
                waveformFailure = payload.error;
                setWaveformPresentationState(
                    QStringLiteral("failed"), payload.error);
            } else if (waveformPreviewLoader && waveformView) {
                QString failure;
                if (!waveformPreviewLoader->replacePreview(
                        waveformView.data(), payload.payload, &failure)) {
                    waveformFailure = failure;
                } else {
                    waveformFailure.clear();
                }
            }
        } else {
            setWaveformPresentationState(
                QStringLiteral("empty"),
                report.warnings.isEmpty()
                    ? QStringLiteral(
                          "Symbolic Preview is unavailable for this scope.")
                    : report.warnings.constFirst());
        }
        if (refreshTimingEnabled) {
            canvasUpdateElapsed = canvasUpdateTimer.nsecsElapsed();
            refreshMetrics.canvasUpdateNanoseconds +=
                static_cast<std::uint64_t>(canvasUpdateElapsed);
        }
    }
    if (titleLabel) {
        const QString titleScope =
            report.scoped && !report.scopeLabel.isEmpty()
                ? QStringLiteral(" - %1").arg(report.scopeLabel)
                : QString();
        setLabelTextIfChanged(
            titleLabel,
            QStringLiteral("Symbolic Preview - %1%2")
                .arg(displayFileName(fileName), titleScope));
    }
    if (summaryLabel) {
        currentSummaryText = reportSummaryText(report, dirty);
        setLabelTextIfChanged(summaryLabel, currentSummaryText);
    }
    if (scopeLabel) {
        setLabelTextIfChanged(
            scopeLabel,
            QStringLiteral("Scope: %1")
                .arg(report.scoped && !report.scopeLabel.isEmpty()
                         ? report.scopeLabel
                         : QStringLiteral("full document")));
    }

    QStringList clockItems{QStringLiteral("auto")};
    QStringList resetItems{QStringLiteral("auto")};
    for (const WavePreviewClockResetGroup& group :
         report.clockResetGroups) {
        for (const WavePreviewEdgeSignal& signal : group.clockEdgeSignals)
            appendUnique(&clockItems, signal.label());
        for (const QString& signal : group.clockSignals)
            appendUnique(&clockItems, signal);
        for (const WavePreviewEdgeSignal& signal : group.resetEdgeSignals)
            appendUnique(&resetItems, signal.label());
        for (const QString& signal : group.resetSignals)
            appendUnique(&resetItems, signal);
    }
    replaceComboItemsIfChanged(clockCombo, clockItems);
    replaceComboItemsIfChanged(resetCombo, resetItems);

    addOverviewItems(&stagingRoot, report, dirty);

    if (!report.available) {
        auto* item = new QTreeWidgetItem(&stagingRoot);
        item->setText(0, QStringLiteral("No assign/always code-sketch events found"));
        item->setText(1, QStringLiteral("-"));
        item->setText(2, QStringLiteral("-"));
        item->setText(3, QStringLiteral("-"));
        item->setText(4, QStringLiteral("-"));
        item->setText(5, QStringLiteral("-"));
        item->setText(6, QStringLiteral("-"));
        synchronizeTree(previewTree, stagingRoot);
        if (refreshTimingEnabled) {
            const qint64 modelSceneElapsed =
                modelSceneTimer.nsecsElapsed() - canvasUpdateElapsed;
            refreshMetrics.modelSceneRebuildNanoseconds +=
                static_cast<std::uint64_t>(
                    qMax<qint64>(0, modelSceneElapsed));
        }
        return;
    }

    if (report.trace.isValid()) {
        auto* traceRoot = new QTreeWidgetItem(&stagingRoot);
        traceRoot->setText(0, QStringLiteral("Symbolic Waveform Preview"));
        traceRoot->setText(1,
                           QStringLiteral("%1 cycles")
                               .arg(report.trace.cycleCount));
        traceRoot->setText(2, QStringLiteral("-"));
        traceRoot->setText(3, QStringLiteral("-"));
        traceRoot->setText(4,
                           QStringLiteral("%1 signals")
                               .arg(report.trace.traceSignals.size()));
        traceRoot->setText(5, QStringLiteral("preview-only"));
        traceRoot->setText(6, QStringLiteral("-"));
        QFont traceFont = traceRoot->font(0);
        traceFont.setBold(true);
        traceRoot->setFont(0, traceFont);
        setItemTooltip(traceRoot,
                       QStringLiteral("symbolic preview-only waveform: %1 signals, %2 cycles, no testbench")
                           .arg(report.trace.traceSignals.size())
                           .arg(report.trace.cycleCount));
        for (const WavePreviewTraceSignal& signal : report.trace.traceSignals) {
            if (!traceSignalMatchesFilter(signal, laneFilterText))
                continue;
            auto* signalItem = new QTreeWidgetItem(traceRoot);
            signalItem->setData(
                0,
                kRoleStableIdentity,
                QStringLiteral("trace:%1").arg(signal.signalName));
            signalItem->setText(0, signal.signalName);
            signalItem->setText(1,
                                signal.values.join(QStringLiteral(" -> ")));
            signalItem->setText(2,
                                signal.clock ? QStringLiteral("clock")
                                             : QStringLiteral("-"));
            signalItem->setText(3, QStringLiteral("-"));
            signalItem->setText(4,
                                QStringLiteral("%1-bit").arg(signal.width));
            signalItem->setText(5, QStringLiteral("symbolic preview"));
            signalItem->setText(6, QStringLiteral("-"));
            setItemTooltip(
                signalItem,
                QStringLiteral("%1: %2")
                    .arg(signal.signalName,
                         signal.values.join(QStringLiteral(" -> "))));
        }
        setItemDefaultExpanded(traceRoot, true);
    }

    if (!report.warnings.isEmpty()) {
        auto* warningRoot = new QTreeWidgetItem(&stagingRoot);
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
        setItemDefaultExpanded(warningRoot, true);
    }

    if (!report.clockResetGroups.isEmpty()) {
        auto* groupRoot = new QTreeWidgetItem(&stagingRoot);
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
        setItemDefaultExpanded(groupRoot, true);
    }

    if (report.activitySummary.isValid()) {
        auto* activityRoot = new QTreeWidgetItem(&stagingRoot);
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
        if (!laneMatchesFilter(lane, laneFilterText))
            continue;
        auto* laneItem = new QTreeWidgetItem(&stagingRoot);
        laneItem->setData(
            0,
            kRoleStableIdentity,
            QStringLiteral("lane:%1").arg(lane.signalName));
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
            eventItem->setData(
                0,
                kRoleStableIdentity,
                QStringLiteral("event:%1:%2:%3:%4")
                    .arg(assignment.target)
                    .arg(assignment.line)
                    .arg(assignment.column)
                    .arg(assignment.blockIndex));
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
        setItemDefaultExpanded(laneItem, true);
    }

    synchronizeTree(previewTree, stagingRoot);
    GraphExportUi::updateActionAvailability(
        exportAction,
        currentReport.available
            && previewCanvas
            && !previewCanvas->size().isEmpty());
    if (refreshTimingEnabled) {
        const qint64 modelSceneElapsed =
            modelSceneTimer.nsecsElapsed() - canvasUpdateElapsed;
        refreshMetrics.modelSceneRebuildNanoseconds +=
            static_cast<std::uint64_t>(
                qMax<qint64>(0, modelSceneElapsed));
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
