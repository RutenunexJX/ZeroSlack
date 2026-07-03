#include "signalusagehotspotpanel.h"

#include "insightvisualstyle.h"

#include <QApplication>
#include <QBrush>
#include <QCheckBox>
#include <QFileInfo>
#include <QFontMetrics>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {
constexpr int kItemIndexRole = Qt::UserRole + 6100;
constexpr int kMatrixRoleRole = Qt::UserRole + 6101;
constexpr int kMatrixModuleRole = Qt::UserRole + 6102;
constexpr int kMatrixFileRole = Qt::UserRole + 6103;
constexpr qreal kLaneLabelWidth = 220.0;
constexpr qreal kLaneHeight = 58.0;
constexpr qreal kLaneTop = 34.0;
constexpr qreal kTrackMinWidth = 900.0;

QList<SignalUsageHotspotRole> hotspotRoles()
{
    return {SignalUsageHotspotRole::Write,
            SignalUsageHotspotRole::Read,
            SignalUsageHotspotRole::Port,
            SignalUsageHotspotRole::Condition,
            SignalUsageHotspotRole::Case,
            SignalUsageHotspotRole::Timing,
            SignalUsageHotspotRole::Unknown};
}

InsightVisualRole visualRoleForHotspotRole(SignalUsageHotspotRole role)
{
    switch (role) {
    case SignalUsageHotspotRole::Write:
        return InsightVisualRole::Write;
    case SignalUsageHotspotRole::Read:
        return InsightVisualRole::Read;
    case SignalUsageHotspotRole::Port:
        return InsightVisualRole::Port;
    case SignalUsageHotspotRole::Condition:
        return InsightVisualRole::Condition;
    case SignalUsageHotspotRole::Case:
        return InsightVisualRole::Case;
    case SignalUsageHotspotRole::Timing:
        return InsightVisualRole::Timing;
    case SignalUsageHotspotRole::Unknown:
        return InsightVisualRole::Unknown;
    }
    return InsightVisualRole::Unknown;
}

QString compactFileName(const QString& fileName)
{
    const QString leaf = QFileInfo(fileName).fileName();
    return leaf.isEmpty() ? fileName : leaf;
}

QString itemSearchText(const SignalUsageHotspotItem& item)
{
    return QStringList{SignalUsageHotspotService::roleDisplayName(item.role),
                       item.moduleName,
                       item.fileName,
                       compactFileName(item.fileName),
                       item.snippet,
                       item.evidenceText,
                       item.evidenceKindDisplayName,
                       item.roleReasonDisplayName}
        .join(QLatin1Char(' '));
}

QString inspectorTextForItem(const SignalUsageHotspotItem& item)
{
    QStringList lines;
    lines.append(QStringLiteral("Role: %1")
                     .arg(SignalUsageHotspotService::roleDisplayName(item.role)));
    if (!item.moduleName.isEmpty())
        lines.append(QStringLiteral("Module: %1").arg(item.moduleName));
    if (!item.fileName.isEmpty()) {
        lines.append(QStringLiteral("File: %1:%2")
                         .arg(compactFileName(item.fileName))
                         .arg(item.line));
    }
    if (!item.roleReasonDisplayName.isEmpty())
        lines.append(QStringLiteral("Reason: %1")
                         .arg(item.roleReasonDisplayName));
    if (!item.evidenceKindDisplayName.isEmpty())
        lines.append(QStringLiteral("Evidence: %1")
                         .arg(item.evidenceKindDisplayName));
    if (!item.evidenceText.isEmpty())
        lines.append(item.evidenceText);
    if (!item.snippet.isEmpty())
        lines.append(QStringLiteral("Snippet: %1").arg(item.snippet.trimmed()));
    return lines.join(QLatin1Char('\n'));
}

class HotspotUsageBlockItem : public QGraphicsRectItem
{
public:
    using ItemHandler = std::function<void(int)>;

    HotspotUsageBlockItem(int itemIndex,
                          SignalUsageHotspotRole role,
                          const QRectF& rect)
        : QGraphicsRectItem(rect),
          index(itemIndex)
    {
        setAcceptHoverEvents(true);
        setAcceptedMouseButtons(Qt::LeftButton);
        setFlag(QGraphicsItem::ItemIsSelectable, true);
        normalPen = InsightVisualStyle::rolePen(visualRoleForHotspotRole(role), 1.3);
        setPen(normalPen);
        setBrush(InsightVisualStyle::roleFillColor(visualRoleForHotspotRole(role)));
        setData(kItemIndexRole, itemIndex);
    }

    ItemHandler hoverHandler;
    ItemHandler selectHandler;
    ItemHandler navigateHandler;

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override
    {
        setPen(InsightVisualStyle::hoverPen());
        if (hoverHandler)
            hoverHandler(index);
        QGraphicsRectItem::hoverEnterEvent(event);
    }

    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override
    {
        setPen(isSelected() ? InsightVisualStyle::selectedPen() : normalPen);
        QGraphicsRectItem::hoverLeaveEvent(event);
    }

    void mousePressEvent(QGraphicsSceneMouseEvent* event) override
    {
        setSelected(true);
        setPen(InsightVisualStyle::selectedPen());
        if (selectHandler)
            selectHandler(index);
        QGraphicsRectItem::mousePressEvent(event);
    }

    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override
    {
        if (event && event->button() == Qt::LeftButton && navigateHandler) {
            navigateHandler(index);
            event->accept();
            return;
        }
        QGraphicsRectItem::mouseDoubleClickEvent(event);
    }

private:
    int index = -1;
    QPen normalPen;
};

QPushButton* modeButton(const QString& text, QWidget* parent)
{
    auto* button = new QPushButton(text, parent);
    button->setCheckable(true);
    button->setMinimumHeight(28);
    button->setStyleSheet(
        QStringLiteral(
            "QPushButton { padding: 4px 10px; border: 1px solid %1; "
            "background: %2; color: %3; border-radius: 6px; }"
            "QPushButton:checked { background: %4; color: white; "
            "border-color: %4; }")
            .arg(InsightVisualStyle::theme().border.name(),
                 InsightVisualStyle::theme().panelBackground.name(),
                 InsightVisualStyle::theme().textSecondary.name(),
                 InsightVisualStyle::theme().accent.name()));
    return button;
}
}

SignalUsageHotspotPanel::SignalUsageHotspotPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("signalUsageHotspotPanel"));
    InsightVisualStyle::applyPanel(this);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(8, 8, 8, 8);
    rootLayout->setSpacing(6);

    titleLabel = new QLabel(QStringLiteral("Signal Usage Hotspot"), this);
    titleLabel->setObjectName(QStringLiteral("signalUsageHotspotTitle"));
    InsightVisualStyle::applyTitleLabel(titleLabel);
    rootLayout->addWidget(titleLabel);

    auto* toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->setSpacing(6);
    trackModeButton = modeButton(QStringLiteral("Track"), this);
    matrixModeButton = modeButton(QStringLiteral("Matrix"), this);
    trackModeButton->setChecked(true);
    searchEdit = new QLineEdit(this);
    searchEdit->setPlaceholderText(QStringLiteral("Search usage"));
    InsightVisualStyle::applySearchField(searchEdit);
    toolbar->addWidget(trackModeButton);
    toolbar->addWidget(matrixModeButton);
    toolbar->addWidget(searchEdit, 1);
    rootLayout->addLayout(toolbar);

    auto* roleLayout = new QHBoxLayout;
    roleLayout->setContentsMargins(0, 0, 0, 0);
    roleLayout->setSpacing(4);
    for (SignalUsageHotspotRole role : hotspotRoles()) {
        auto* check = new QCheckBox(
            SignalUsageHotspotService::roleDisplayName(role),
            this);
        check->setChecked(true);
        check->setProperty("hotspotRole", static_cast<int>(role));
        InsightVisualStyle::applySegmentedCheckBox(check);
        roleChecks.append(check);
        roleLayout->addWidget(check);
    }
    roleLayout->addStretch(1);
    rootLayout->addLayout(roleLayout);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);

    modeStack = new QStackedWidget(splitter);
    trackScene = new QGraphicsScene(modeStack);
    trackView = new QGraphicsView(trackScene, modeStack);
    trackView->setObjectName(QStringLiteral("signalUsageHotspotTrackView"));
    trackView->setRenderHint(QPainter::Antialiasing, true);
    trackView->setDragMode(QGraphicsView::ScrollHandDrag);
    trackView->setBackgroundBrush(InsightVisualStyle::canvasBrush());
    trackView->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    trackView->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    modeStack->addWidget(trackView);

    auto* matrixPage = new QWidget(modeStack);
    auto* matrixLayout = new QVBoxLayout(matrixPage);
    matrixLayout->setContentsMargins(0, 0, 0, 0);
    matrixLayout->setSpacing(6);
    matrixTable = new QTableWidget(matrixPage);
    matrixTable->setObjectName(QStringLiteral("signalUsageHotspotMatrix"));
    matrixTable->setSelectionMode(QAbstractItemView::SingleSelection);
    matrixTable->setSelectionBehavior(QAbstractItemView::SelectItems);
    matrixTable->horizontalHeader()->setStretchLastSection(true);
    matrixTable->verticalHeader()->setVisible(false);
    matrixTable->setAlternatingRowColors(false);
    matrixItemsTree = new QTreeWidget(matrixPage);
    matrixItemsTree->setObjectName(QStringLiteral("signalUsageHotspotMatrixItems"));
    matrixItemsTree->setColumnCount(5);
    matrixItemsTree->setHeaderLabels(
        {QStringLiteral("Role"),
         QStringLiteral("Module"),
         QStringLiteral("File"),
         QStringLiteral("Line"),
         QStringLiteral("Snippet")});
    matrixItemsTree->header()->setStretchLastSection(true);
    matrixItemsTree->setRootIsDecorated(false);
    matrixLayout->addWidget(matrixTable, 2);
    matrixLayout->addWidget(matrixItemsTree, 1);
    modeStack->addWidget(matrixPage);
    splitter->addWidget(modeStack);

    auto* inspector = new QWidget(splitter);
    inspector->setObjectName(QStringLiteral("signalUsageHotspotInspector"));
    inspector->setMinimumWidth(250);
    inspector->setMaximumWidth(360);
    inspector->setStyleSheet(
        InsightVisualStyle::inspectorCardStyleSheet(inspector->objectName()));
    auto* inspectorLayout = new QVBoxLayout(inspector);
    inspectorLayout->setContentsMargins(10, 10, 10, 10);
    inspectorLayout->setSpacing(8);
    inspectorTitleLabel = new QLabel(QStringLiteral("Inspector"), inspector);
    inspectorTitleLabel->setFont(
        InsightVisualStyle::titleFont(inspectorTitleLabel->font()));
    inspectorDetailLabel = new QLabel(
        QStringLiteral("Select or hover a usage block."), inspector);
    inspectorDetailLabel->setWordWrap(true);
    inspectorDetailLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    inspectorDetailLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    inspectorLayout->addWidget(inspectorTitleLabel);
    inspectorLayout->addWidget(inspectorDetailLabel, 1);
    splitter->addWidget(inspector);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    rootLayout->addWidget(splitter, 1);

    connect(trackModeButton, &QPushButton::clicked, this, [this]() {
        setMode(false);
    });
    connect(matrixModeButton, &QPushButton::clicked, this, [this]() {
        setMode(true);
    });
    connect(searchEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        searchText = text.trimmed();
        rebuild();
    });
    for (QCheckBox* check : roleChecks) {
        connect(check, &QCheckBox::toggled, this, [this]() {
            rebuild();
        });
    }
    connect(matrixTable, &QTableWidget::cellClicked, this, [this](int row, int column) {
        QTableWidgetItem* item = matrixTable->item(row, column);
        if (!item)
            return;
        activeMatrixRole =
            static_cast<SignalUsageHotspotRole>(
                item->data(kMatrixRoleRole).toInt());
        activeMatrixModuleName = item->data(kMatrixModuleRole).toString();
        activeMatrixFileName = item->data(kMatrixFileRole).toString();
        renderMatrixItems();
    });
    connect(matrixItemsTree, &QTreeWidget::itemClicked, this,
            [this](QTreeWidgetItem* item, int) {
                if (!item)
                    return;
                showInspectorForItem(item->data(0, kItemIndexRole).toInt());
            });
    connect(matrixItemsTree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item, int) {
                if (!item)
                    return;
                navigateItem(item->data(0, kItemIndexRole).toInt());
            });
}

void SignalUsageHotspotPanel::setNavigationHandler(
    std::function<bool(const QString&, int, int)> handler)
{
    navigationHandler = std::move(handler);
}

void SignalUsageHotspotPanel::setStatusMessageHandler(
    std::function<void(const QString&, int)> handler)
{
    statusMessageHandler = std::move(handler);
}

void SignalUsageHotspotPanel::showHotspotForSymbol(
    const QString& symbolName,
    const QString& fileName,
    const QString& moduleName,
    const QString& signalAccessPath)
{
    currentQuery = {};
    currentQuery.signalName = symbolName;
    currentQuery.fileName = fileName;
    currentQuery.moduleName = moduleName;
    currentQuery.signalAccessPath = signalAccessPath;
    titleLabel->setText(QStringLiteral("Signal Usage Hotspot: analyzing %1")
                            .arg(signalAccessPath.isEmpty()
                                     ? symbolName
                                     : signalAccessPath));
    showInspectorMessage(QStringLiteral("Analyzing"),
                         QStringLiteral("Building usage hotspot report..."));
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    currentReport =
        SignalUsageHotspotService::getInstance()->buildSignalUsageHotspot(
            currentQuery);
    rebuild();
}

void SignalUsageHotspotPanel::renderReportForTest(
    const SignalUsageHotspotReport& report)
{
    currentReport = report;
    rebuild();
}

int SignalUsageHotspotPanel::trackBlockCountForTest() const
{
    return lastTrackBlockCount;
}

int SignalUsageHotspotPanel::matrixNonEmptyCellCountForTest() const
{
    return lastMatrixNonEmptyCellCount;
}

bool SignalUsageHotspotPanel::triggerFirstUsageNavigationForTest()
{
    const QList<int> indexes = filteredItemIndexes();
    if (indexes.isEmpty())
        return false;
    navigateItem(indexes.first());
    return true;
}

void SignalUsageHotspotPanel::setMode(bool matrixMode)
{
    trackModeButton->setChecked(!matrixMode);
    matrixModeButton->setChecked(matrixMode);
    modeStack->setCurrentIndex(matrixMode ? 1 : 0);
    rebuild();
}

void SignalUsageHotspotPanel::rebuild()
{
    if (!currentReport.found) {
        renderUnavailable(currentReport.notFoundReasonDisplayName.isEmpty()
                              ? QStringLiteral("No signal usage hotspot.")
                              : currentReport.notFoundReasonDisplayName);
        return;
    }

    titleLabel->setText(QStringLiteral("Signal Usage Hotspot: %1")
                            .arg(currentReport.declarationDisplayName));
    if (currentReport.items.isEmpty()) {
        renderUnavailable(QStringLiteral("No usage found for this signal."));
        return;
    }

    renderTrack();
    renderMatrix();
    if (selectedItemIndex >= 0)
        showInspectorForItem(selectedItemIndex);
    else
        showInspectorMessage(QStringLiteral("Inspector"),
                             QStringLiteral("Select or hover a usage block."));
}

void SignalUsageHotspotPanel::renderUnavailable(const QString& message)
{
    lastTrackBlockCount = 0;
    lastMatrixNonEmptyCellCount = 0;
    if (trackScene) {
        trackScene->clear();
        auto* text = trackScene->addText(message);
        text->setDefaultTextColor(InsightVisualStyle::theme().textMuted);
        text->setPos(16, 16);
        trackScene->setSceneRect(text->boundingRect().adjusted(-16, -16, 280, 80));
    }
    if (matrixTable) {
        matrixTable->clear();
        matrixTable->setRowCount(0);
        matrixTable->setColumnCount(0);
    }
    if (matrixItemsTree)
        matrixItemsTree->clear();
    showInspectorMessage(QStringLiteral("Unavailable"), message);
}

void SignalUsageHotspotPanel::renderTrack()
{
    if (!trackScene)
        return;

    trackScene->clear();
    lastTrackBlockCount = 0;
    QSet<int> visibleIndexes;
    for (int index : filteredItemIndexes())
        visibleIndexes.insert(index);
    if (visibleIndexes.isEmpty()) {
        auto* text = trackScene->addText(QStringLiteral("No usage matches filters."));
        text->setDefaultTextColor(InsightVisualStyle::theme().textMuted);
        text->setPos(16, 16);
        trackScene->setSceneRect(text->boundingRect().adjusted(-16, -16, 260, 80));
        return;
    }

    QFont baseFont = font();
    QFont labelFont = InsightVisualStyle::labelFont(baseFont);
    QFont detailFont = InsightVisualStyle::compactFont(baseFont);
    qreal y = kLaneTop;
    qreal sceneWidth = kTrackMinWidth;
    for (const SignalUsageHotspotTrackLane& lane : currentReport.trackLanes) {
        QList<SignalUsageHotspotTrackPosition> positions;
        for (const SignalUsageHotspotTrackPosition& position : lane.positions) {
            if (visibleIndexes.contains(position.itemIndex))
                positions.append(position);
        }
        if (positions.isEmpty())
            continue;

        const int lineSpan = qMax(1, lane.endLine - lane.startLine + 1);
        const qreal trackWidth = qMax<qreal>(kTrackMinWidth - kLaneLabelWidth - 60,
                                             lineSpan * 34.0);
        sceneWidth = qMax(sceneWidth, kLaneLabelWidth + trackWidth + 44.0);
        const qreal laneY = y;
        auto* laneLabel = trackScene->addSimpleText(
            QStringLiteral("%1  %2:%3-%4")
                .arg(lane.moduleName.isEmpty()
                         ? compactFileName(lane.fileName)
                         : lane.moduleName,
                     compactFileName(lane.fileName))
                .arg(lane.startLine)
                .arg(lane.endLine),
            labelFont);
        laneLabel->setBrush(QBrush(InsightVisualStyle::theme().textPrimary));
        laneLabel->setPos(8, laneY + 7);

        auto* countLabel = trackScene->addSimpleText(
            QStringLiteral("%1 usage(s)").arg(positions.size()),
            detailFont);
        countLabel->setBrush(QBrush(InsightVisualStyle::theme().textMuted));
        countLabel->setPos(8, laneY + 30);

        QRectF rail(kLaneLabelWidth, laneY + 20, trackWidth, 8);
        auto* railItem = trackScene->addRect(
            rail,
            InsightVisualStyle::hairlinePen(InsightVisualStyle::theme().border),
            QBrush(InsightVisualStyle::theme().panelSubtle));
        railItem->setZValue(-2);

        for (const SignalUsageHotspotTrackPosition& position : positions) {
            const double lineRatio =
                static_cast<double>(qMax(0, position.line - lane.startLine))
                / static_cast<double>(lineSpan);
            const qreal x = rail.left() + lineRatio * trackWidth;
            QRectF blockRect(x - 6.0, laneY + 11.0, 14.0, 26.0);
            auto* block =
                new HotspotUsageBlockItem(position.itemIndex,
                                          position.role,
                                          blockRect);
            block->setToolTip(
                QStringLiteral("%1 line %2")
                    .arg(position.roleDisplayName)
                    .arg(position.line));
            block->hoverHandler = [this](int itemIndex) {
                showInspectorForItem(itemIndex);
            };
            block->selectHandler = [this](int itemIndex) {
                selectedItemIndex = itemIndex;
                showInspectorForItem(itemIndex);
            };
            block->navigateHandler = [this](int itemIndex) {
                navigateItem(itemIndex);
            };
            trackScene->addItem(block);
            ++lastTrackBlockCount;
        }
        y += kLaneHeight;
    }

    if (lastTrackBlockCount == 0) {
        auto* text = trackScene->addText(QStringLiteral("No track lanes match filters."));
        text->setDefaultTextColor(InsightVisualStyle::theme().textMuted);
        text->setPos(16, 16);
        trackScene->setSceneRect(text->boundingRect().adjusted(-16, -16, 260, 80));
        return;
    }

    trackScene->setSceneRect(0, 0, sceneWidth, y + 30);
}

void SignalUsageHotspotPanel::renderMatrix()
{
    if (!matrixTable)
        return;

    matrixTable->clear();
    matrixTable->setColumnCount(hotspotRoles().size());
    matrixTable->setHorizontalHeaderLabels(
        {QStringLiteral("Write"),
         QStringLiteral("Read"),
         QStringLiteral("Port"),
         QStringLiteral("Condition"),
         QStringLiteral("Case"),
         QStringLiteral("Timing"),
         QStringLiteral("Unknown")});

    QList<QString> rows;
    for (const SignalUsageHotspotMatrixCell& cell : currentReport.matrixCells) {
        const QString rowKey =
            cell.moduleName.isEmpty() ? compactFileName(cell.fileName)
                                      : cell.moduleName;
        if (!rowKey.isEmpty() && !rows.contains(rowKey))
            rows.append(rowKey);
    }
    rows.sort(Qt::CaseInsensitive);
    matrixTable->setRowCount(rows.size());
    for (int row = 0; row < rows.size(); ++row) {
        auto* header = new QTableWidgetItem(rows.at(row));
        matrixTable->setVerticalHeaderItem(row, header);
    }

    int maxCount = 1;
    QHash<QString, SignalUsageHotspotMatrixCell> cellsByKey;
    for (const SignalUsageHotspotMatrixCell& cell : currentReport.matrixCells) {
        const QString rowKey =
            cell.moduleName.isEmpty() ? compactFileName(cell.fileName)
                                      : cell.moduleName;
        const QString key =
            rowKey + QLatin1Char('\n')
            + QString::number(static_cast<int>(cell.role));
        cellsByKey.insert(key, cell);
        maxCount = qMax(maxCount, cell.count);
    }

    lastMatrixNonEmptyCellCount = 0;
    for (int row = 0; row < rows.size(); ++row) {
        for (int column = 0; column < hotspotRoles().size(); ++column) {
            const SignalUsageHotspotRole role = hotspotRoles().at(column);
            const QString key =
                rows.at(row) + QLatin1Char('\n')
                + QString::number(static_cast<int>(role));
            SignalUsageHotspotMatrixCell cell = cellsByKey.value(key);
            auto* item = new QTableWidgetItem;
            item->setTextAlignment(Qt::AlignCenter);
            item->setData(kMatrixRoleRole, static_cast<int>(role));
            item->setData(kMatrixModuleRole, cell.moduleName);
            item->setData(kMatrixFileRole, cell.fileName);
            if (cell.count > 0) {
                item->setText(QString::number(cell.count));
                item->setBackground(
                    InsightVisualStyle::heatIntensityColor(
                        static_cast<double>(cell.count)
                        / static_cast<double>(maxCount)));
                item->setForeground(QBrush(InsightVisualStyle::theme().textPrimary));
                ++lastMatrixNonEmptyCellCount;
            } else {
                item->setText(QStringLiteral("-"));
                item->setBackground(InsightVisualStyle::theme().panelSubtle);
                item->setForeground(QBrush(InsightVisualStyle::theme().textMuted));
            }
            matrixTable->setItem(row, column, item);
        }
    }
    matrixTable->resizeColumnsToContents();
    matrixTable->resizeRowsToContents();
    renderMatrixItems();
}

void SignalUsageHotspotPanel::renderMatrixItems()
{
    if (!matrixItemsTree)
        return;

    matrixItemsTree->clear();
    for (int index : filteredItemIndexes()) {
        const SignalUsageHotspotItem& item = currentReport.items.at(index);
        if (activeMatrixModuleName.isEmpty()
            && activeMatrixFileName.isEmpty()
            && activeMatrixRole == SignalUsageHotspotRole::Unknown) {
            // Unknown is a real role; only skip this default before a cell click.
        } else {
            if (item.role != activeMatrixRole)
                continue;
            if (!activeMatrixModuleName.isEmpty()
                && item.moduleName != activeMatrixModuleName) {
                continue;
            }
            if (!activeMatrixFileName.isEmpty()
                && item.fileName != activeMatrixFileName) {
                continue;
            }
        }

        auto* row = new QTreeWidgetItem(matrixItemsTree);
        row->setText(0, SignalUsageHotspotService::roleDisplayName(item.role));
        row->setText(1, item.moduleName);
        row->setText(2, compactFileName(item.fileName));
        row->setText(3, QString::number(item.line));
        row->setText(4, item.snippet.trimmed());
        row->setData(0, kItemIndexRole, index);
    }
    matrixItemsTree->resizeColumnToContents(0);
    matrixItemsTree->resizeColumnToContents(1);
    matrixItemsTree->resizeColumnToContents(2);
    matrixItemsTree->resizeColumnToContents(3);
}

void SignalUsageHotspotPanel::showInspectorForItem(int itemIndex)
{
    if (itemIndex < 0 || itemIndex >= currentReport.items.size())
        return;
    selectedItemIndex = itemIndex;
    const SignalUsageHotspotItem& item = currentReport.items.at(itemIndex);
    showInspectorMessage(
        QStringLiteral("%1 usage")
            .arg(SignalUsageHotspotService::roleDisplayName(item.role)),
        inspectorTextForItem(item));
}

void SignalUsageHotspotPanel::showInspectorMessage(
    const QString& title,
    const QString& message)
{
    if (inspectorTitleLabel)
        inspectorTitleLabel->setText(title);
    if (inspectorDetailLabel)
        inspectorDetailLabel->setText(message);
}

void SignalUsageHotspotPanel::navigateItem(int itemIndex)
{
    if (itemIndex < 0 || itemIndex >= currentReport.items.size())
        return;
    const SignalUsageHotspotItem& item = currentReport.items.at(itemIndex);
    if (item.fileName.isEmpty() || item.line <= 0) {
        showStatusMessage(QStringLiteral("Hotspot item has no source location"),
                          3000);
        return;
    }
    if (!navigationHandler
        || !navigationHandler(item.fileName, item.line, item.column)) {
        showStatusMessage(QStringLiteral("Signal usage hotspot jump failed"),
                          4000);
        return;
    }
    showStatusMessage(QStringLiteral("Jumped to %1:%2")
                          .arg(compactFileName(item.fileName))
                          .arg(item.line),
                      1500);
}

bool SignalUsageHotspotPanel::itemPassesFilters(
    const SignalUsageHotspotItem& item) const
{
    if (!roleEnabled(item.role))
        return false;
    if (searchText.isEmpty())
        return true;
    return itemSearchText(item).contains(searchText, Qt::CaseInsensitive);
}

bool SignalUsageHotspotPanel::roleEnabled(SignalUsageHotspotRole role) const
{
    for (QCheckBox* check : roleChecks) {
        if (!check)
            continue;
        if (check->property("hotspotRole").toInt() == static_cast<int>(role))
            return check->isChecked();
    }
    return true;
}

QList<int> SignalUsageHotspotPanel::filteredItemIndexes() const
{
    QList<int> indexes;
    for (int i = 0; i < currentReport.items.size(); ++i) {
        if (itemPassesFilters(currentReport.items.at(i)))
            indexes.append(i);
    }
    return indexes;
}

void SignalUsageHotspotPanel::showStatusMessage(
    const QString& message,
    int timeoutMs) const
{
    if (statusMessageHandler)
        statusMessageHandler(message, timeoutMs);
}
