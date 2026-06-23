#include "wavepreviewpanelcoordinator.h"

#include <QFileInfo>
#include <QFont>
#include <QHeaderView>
#include <QLabel>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

#include <utility>

namespace {
constexpr int kRoleFileName = Qt::UserRole + 1;
constexpr int kRoleLine = Qt::UserRole + 2;
constexpr int kRoleColumn = Qt::UserRole + 3;

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

QString sourcesText(const QStringList& sourceSignals)
{
    return sourceSignals.isEmpty()
        ? QStringLiteral("-")
        : sourceSignals.join(QStringLiteral(", "));
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

    previewTree = new QTreeWidget(panel);
    previewTree->setObjectName(QStringLiteral("wavePreviewTree"));
    previewTree->setColumnCount(4);
    previewTree->setHeaderLabels({
        QStringLiteral("Signal / Event"),
        QStringLiteral("Timing"),
        QStringLiteral("Sources"),
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
    layout->addWidget(previewTree, 1);

    previewDock = new QDockWidget(QStringLiteral("Wave Preview"), parent);
    previewDock->setObjectName(QStringLiteral("wavePreviewDock"));
    previewDock->setWidget(panel);
    previewDock->setFeatures(QDockWidget::DockWidgetMovable |
                             QDockWidget::DockWidgetFloatable |
                             QDockWidget::DockWidgetClosable);
    previewDock->hide();

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

    const WavePreviewReport report =
        WavePreviewService::getInstance()->previewForDocument(
            {fileName, documentText});
    renderReport(report, fileName, dirty);
}

void WavePreviewPanelCoordinator::renderUnavailable(const QString& message)
{
    if (titleLabel)
        titleLabel->setText(QStringLiteral("Wave Preview"));
    if (summaryLabel)
        summaryLabel->setText(message);
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
    if (titleLabel) {
        titleLabel->setText(QStringLiteral("Wave Preview - %1")
                                .arg(displayFileName(fileName)));
    }
    if (summaryLabel) {
        summaryLabel->setText(
            QStringLiteral("%1 lanes, %2 events, %3 blocks%4")
                .arg(report.lanes.size())
                .arg(report.assignmentCount)
                .arg(report.blocks.size())
                .arg(dirty ? QStringLiteral(" - live dirty buffer")
                           : QString()));
    }

    if (!report.available) {
        auto* item = new QTreeWidgetItem(previewTree);
        item->setText(0, QStringLiteral("No assign/always waveform sketch found"));
        item->setText(1, QStringLiteral("-"));
        item->setText(2, QStringLiteral("-"));
        item->setText(3, QStringLiteral("-"));
        return;
    }

    for (const WavePreviewLane& lane : report.lanes) {
        auto* laneItem = new QTreeWidgetItem(previewTree);
        laneItem->setText(0, lane.signalName);
        laneItem->setText(1, QStringLiteral("%1 events").arg(lane.assignments.size()));
        laneItem->setText(2, QStringLiteral("-"));
        laneItem->setText(3, QStringLiteral("-"));
        QFont laneFont = laneItem->font(0);
        laneFont.setBold(true);
        laneItem->setFont(0, laneFont);

        for (const WavePreviewAssignment& assignment : lane.assignments) {
            auto* eventItem = new QTreeWidgetItem(laneItem);
            eventItem->setText(0, eventText(assignment, report));
            eventItem->setText(1, timingText(assignment));
            eventItem->setText(2, sourcesText(assignment.sourceSignals));
            eventItem->setText(3,
                               assignment.line > 0
                                   ? QStringLiteral("%1:%2")
                                         .arg(assignment.line)
                                         .arg(assignment.column)
                                   : QStringLiteral("-"));
            eventItem->setToolTip(
                0,
                QStringLiteral("%1\ntrigger: %2")
                    .arg(assignment.expression,
                         assignment.trigger.isEmpty()
                             ? QStringLiteral("-")
                             : assignment.trigger));
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
