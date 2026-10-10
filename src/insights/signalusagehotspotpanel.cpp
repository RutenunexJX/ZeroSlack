#include "signalusagehotspotpanel.h"

#include "compactlayout.h"
#include "editorfileidentity.h"
#include "graphexportui.h"
#include "insightgraphview.h"
#include "insightvisualstyle.h"
#include "semanticindexsnapshot.h"
#include "uicontrols.h"

#include <QAction>
#include <QApplication>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QFutureWatcher>
#include <QGraphicsLineItem>
#include <QGraphicsProxyWidget>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSimpleTextItem>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cmath>

namespace {
constexpr int kItemIndexRole = Qt::UserRole + 6100;
constexpr double kMinTrackZoom = 1.0;
constexpr double kMaxTrackZoom = 16.0;
const char* kSettingsGroup = "SignalUsageHotspotPanel";

QString roleName(SignalUsageHotspotRole role)
{
    return SignalUsageHotspotService::roleDisplayName(role);
}

QString roleSymbol(SignalUsageHotspotRole role)
{
    static const QStringList symbols{"W", "R", "P", "C", "K", "T", "?"};
    return symbols.value(static_cast<int>(role), QStringLiteral("?"));
}

QColor roleColor(SignalUsageHotspotRole role)
{
    const auto& theme = InsightVisualStyle::theme();
    switch (role) {
    case SignalUsageHotspotRole::Write: return theme.accent;
    case SignalUsageHotspotRole::Read: return QColor("#299783");
    case SignalUsageHotspotRole::Port: return QColor("#9971c2");
    case SignalUsageHotspotRole::Condition: return QColor("#ac8033");
    case SignalUsageHotspotRole::Case: return QColor("#a26785");
    case SignalUsageHotspotRole::Timing: return QColor("#5285aa");
    case SignalUsageHotspotRole::Unknown: return theme.textSecondary;
    }
    return theme.textSecondary;
}

QString searchableText(const SignalUsageHotspotItem& item)
{
    return QStringList{roleName(item.role), item.moduleName, item.fileName,
        item.snippet, item.evidenceText, item.evidenceKindDisplayName,
        item.roleReasonDisplayName}.join(QLatin1Char(' '));
}

QString usageToolTip(const SignalUsageHotspotItem& item)
{
    return QStringLiteral("%1 · %2:%3\n%4\n%5")
        .arg(roleName(item.role)).arg(item.line).arg(item.column)
        .arg(item.snippet.trimmed(), item.fileName);
}

SignalUsageHotspotReport buildReportFromSnapshot(const SignalUsageHotspotQuery& query,
    const std::shared_ptr<const SemanticIndexSnapshot>& snapshot)
{
    if (!snapshot) {
        SignalUsageHotspotReport report;
        report.notFoundReasonDisplayName = QStringLiteral("Analysis is not available yet.");
        return report;
    }
    SemanticIndex index;
    index.setSnapshot(snapshot);
    return SignalUsageHotspotService(&index).buildSignalUsageHotspot(query);
}

// Wheel scrolling retains the fixed text size; the existing graph Actions
// change only the horizontal source-line axis.
class HotspotView final : public InsightGraphView {
public:
    HotspotView(QGraphicsScene* scene, QWidget* parent) : InsightGraphView(scene, parent) {
        setAlignment(Qt::AlignLeft | Qt::AlignTop);
        setFrameShape(QFrame::NoFrame);
        setDragMode(QGraphicsView::NoDrag);
        setBorderVisible(false);
        setZoomRange(1.0, 1.0);
    }
protected:
    void wheelEvent(QWheelEvent* event) override { QGraphicsView::wheelEvent(event); }
};

class UsageMark final : public QGraphicsItem {
public:
    UsageMark(const QRectF& rect, const QString& text, const QFont& font,
              const QColor& color, bool highlighted,
              std::function<void(bool)> activated)
        : rect(rect), text(text), font(font), color(color), highlighted(highlighted),
          activated(std::move(activated)) {
        setAcceptedMouseButtons(Qt::LeftButton);
        setCursor(Qt::PointingHandCursor);
    }
    QRectF boundingRect() const override { return rect.adjusted(-1, -1, 1, 1); }
    void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override {
        const auto& theme = InsightVisualStyle::theme();
        QColor fill = color;
        fill.setAlpha(highlighted ? 65 : 28);
        painter->setPen(QPen(highlighted ? theme.accent : color, highlighted ? 2 : 1));
        painter->setBrush(fill);
        painter->drawRoundedRect(rect, 4, 4);
        painter->setFont(font);
        painter->setPen(theme.textPrimary);
        painter->drawText(rect, Qt::AlignCenter, text);
    }
protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override { event->accept(); }
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override {
        event->accept();
        if (activated) activated(false);
    }
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override {
        event->accept();
        if (activated) activated(true);
    }
private:
    QRectF rect;
    QString text;
    QFont font;
    QColor color;
    bool highlighted;
    std::function<void(bool)> activated;
};

QGraphicsSimpleTextItem* sceneText(QGraphicsScene* scene, const QString& text,
    const QFont& font, const QColor& color, qreal x, qreal y)
{
    auto* label = scene->addSimpleText(text, font);
    label->setBrush(color);
    label->setPos(x, y);
    return label;
}
}

SignalUsageHotspotPanel::SignalUsageHotspotPanel(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("signalUsageHotspotPanel"));
    InsightVisualStyle::applyPanel(this);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 3, 8, 3);
    root->setSpacing(3);

    auto* toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->setSpacing(4);
    titleLabel = new CompactTitleLabel(this);
    titleLabel->setObjectName(QStringLiteral("signalUsageHotspotTitle"));
    titleLabel->setMinimumWidth(24);
    titleLabel->setMaximumWidth(220);
    matrixModeButton = UiControls::pushButton(QStringLiteral("Matrix"), this);
    trackModeButton = UiControls::pushButton(QStringLiteral("Track"), this);
    matrixModeButton->setObjectName(QStringLiteral("signalUsageHotspotMatrixModeButton"));
    trackModeButton->setObjectName(QStringLiteral("signalUsageHotspotTrackModeButton"));
    for (auto* button : {matrixModeButton, trackModeButton}) {
        button->setCheckable(true);
        InsightVisualStyle::applyToolbarButton(button);
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }
    searchEdit = UiControls::lineEdit(this);
    searchEdit->setObjectName(QStringLiteral("signalUsageHotspotSearch"));
    searchEdit->setPlaceholderText(QStringLiteral("Search usage"));
    searchEdit->setAccessibleName(QStringLiteral("Search usage"));
    searchEdit->setMinimumWidth(48);
    searchEdit->setMaximumWidth(240);
    mainAreaButton = UiControls::toolButton(this);
    mainAreaButton->setObjectName(QStringLiteral("signalUsageHotspotMainAreaButton"));
    mainAreaButton->setText(QStringLiteral("Expand"));
    mainAreaButton->hide();
    auto* more = UiControls::toolButton(this);
    more->setObjectName(QStringLiteral("signalUsageHotspotMoreButton"));
    more->setText(QStringLiteral("More"));
    more->setPopupMode(QToolButton::InstantPopup);
    auto* menu = UiControls::menu(more);
    fitViewAction = createGraphViewAction(QString::fromLatin1(ActionIds::GraphViewFit));
    zoomInViewAction = createGraphViewAction(QString::fromLatin1(ActionIds::GraphViewZoomIn));
    zoomOutViewAction = createGraphViewAction(QString::fromLatin1(ActionIds::GraphViewZoomOut));
    centerCurrentViewAction = createGraphViewAction(QString::fromLatin1(ActionIds::GraphViewCenterCurrent));
    resetLayoutViewAction = createGraphViewAction(QString::fromLatin1(ActionIds::GraphViewResetLayout));
    for (auto* action : graphViewActions()) menu->addAction(action);
    menu->addSeparator();
    const auto exportAction = [this, menu](SignalUsageHotspotExportSurface surface, const char* id) {
        auto* action = GraphExportUi::bindRegistryAction(this, QString::fromLatin1(id),
            [this] { return currentReport.found && !filteredItemIndexes(false).isEmpty(); },
            [this, surface](const QString& path, const GraphExportOptions& options) {
                return exportGraph(surface, path, options);
            }, [this](const QString& message, int timeout) { showStatusMessage(message, timeout); });
        menu->addAction(action);
        return action;
    };
    exportTrackAction = exportAction(SignalUsageHotspotExportSurface::Track, ActionIds::GraphExportUsageHotspotTrack);
    exportMatrixAction = exportAction(SignalUsageHotspotExportSurface::Matrix, ActionIds::GraphExportUsageHotspotMatrix);
    more->setMenu(menu);
    titleLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    toolbar->addWidget(titleLabel);
    toolbar->addWidget(matrixModeButton);
    toolbar->addWidget(trackModeButton);
    toolbar->addWidget(searchEdit);
    toolbar->addStretch(1);
    toolbar->addWidget(mainAreaButton);
    toolbar->addWidget(more);
    root->addLayout(toolbar);

    auto* legendHost = new CompactToolbar(this);
    legendHost->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    auto* legend = new CompactFlowLayout(3);
    legend->setContentsMargins(0, 0, 0, 0);
    legendHost->setLayout(legend);
    for (int i = 0; i < 7; ++i) {
        enabledRoles.insert(i);
        auto* button = UiControls::toolButton(legendHost);
        const auto role = static_cast<SignalUsageHotspotRole>(i);
        button->setObjectName(QStringLiteral("hotspotRole_%1").arg(i));
        button->setText(roleName(role));
        button->setCheckable(true);
        button->setChecked(true);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setToolTip(QStringLiteral("Show %1 references").arg(roleName(role)));
        roleButtons.append(button);
        legend->addWidget(button);
        connect(button, &QToolButton::toggled, this, [this, i](bool checked) {
            if (checked) enabledRoles.insert(i); else enabledRoles.remove(i);
            rebuild();
        });
    }
    root->addWidget(legendHost);
    modeStack = new QStackedWidget(this);
    modeStack->setObjectName(QStringLiteral("signalUsageHotspotModeStack"));
    trackScene = new QGraphicsScene(this);
    matrixScene = new QGraphicsScene(this);
    trackScene->setItemIndexMethod(QGraphicsScene::NoIndex);
    matrixScene->setItemIndexMethod(QGraphicsScene::NoIndex);
    trackView = new HotspotView(trackScene, modeStack);
    matrixView = new HotspotView(matrixScene, modeStack);
    trackView->setObjectName(QStringLiteral("signalUsageHotspotTrackView"));
    matrixView->setObjectName(QStringLiteral("signalUsageHotspotMatrixView"));
    modeStack->addWidget(trackView);
    modeStack->addWidget(matrixView);
    root->addWidget(modeStack, 1);
    connect(trackModeButton, &QPushButton::clicked, this, [this] { setMode(false); });
    connect(matrixModeButton, &QPushButton::clicked, this, [this] { setMode(true); });
    connect(searchEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        searchText = text.trimmed();
        rebuild();
    });
    connect(mainAreaButton, &QToolButton::clicked, this, [this] {
        if (mainAreaHandler) mainAreaHandler(!mainArea);
    });
    connect(&ApplicationThemeManager::instance(), &ApplicationThemeManager::themeChanged,
            this, [this] { refreshThemePresentation(); });
    restoreLayout();
    rebuild();
}

void SignalUsageHotspotPanel::setNavigationHandler(std::function<bool(const QString&, int, int)> handler)
{ navigationHandler = std::move(handler); }
void SignalUsageHotspotPanel::setStatusMessageHandler(std::function<void(const QString&, int)> handler)
{ statusMessageHandler = std::move(handler); }
void SignalUsageHotspotPanel::setMainAreaHandler(std::function<void(bool)> handler)
{ mainAreaHandler = std::move(handler); mainAreaButton->setVisible(bool(mainAreaHandler)); }
void SignalUsageHotspotPanel::setInMainArea(bool expanded)
{
    mainArea = expanded;
    mainAreaButton->setText(expanded ? QStringLiteral("Return to bottom") : QStringLiteral("Expand"));
    mainAreaButton->setToolTip(mainAreaButton->text());
}
bool SignalUsageHotspotPanel::isInMainArea() const { return mainArea; }

void SignalUsageHotspotPanel::resizeEvent(QResizeEvent* event)
{ QWidget::resizeEvent(event); queueRebuild(); }

void SignalUsageHotspotPanel::queueRebuild()
{
    if (renderQueued) return;
    renderQueued = true;
    QTimer::singleShot(0, this, [this] { renderQueued = false; rebuild(); });
}

void SignalUsageHotspotPanel::setWorkspaceRoot(const QString& root)
{
    if (root == workspaceRoot) return;
    if (EditorFileIdentity::lookupKey(root) == EditorFileIdentity::lookupKey(workspaceRoot)) return;
    workspaceRoot = root;
    clearTarget();
}

void SignalUsageHotspotPanel::clearTarget()
{
    ++reportGeneration;
    currentReportPending = false;
    currentQuery = {};
    currentReport = {};
    reportFileKeys.clear();
    {
        QSignalBlocker guard(searchEdit);
        searchEdit->clear();
        searchText.clear();
    }
    enabledRoles.clear();
    for (int i = 0; i < roleButtons.size(); ++i) {
        enabledRoles.insert(i);
        QSignalBlocker guard(roleButtons[i]);
        roleButtons[i]->setChecked(true);
    }
    selectedItemIndex = -1;
    pendingSelectedIdentity.clear();
    expandedLane.clear();
    detailIndexes.clear();
    clearMatrixFocus();
    pendingReadingPosition = {};
    pendingReadingPositionValid = true;
    requestedSnapshotRevision = 0;
    rebuild();
}

void SignalUsageHotspotPanel::refreshSemanticSnapshot()
{
    if (!currentQuery.signalName.isEmpty()
        && requestedSnapshotRevision != SemanticIndex::getInstance()->snapshotToken().revision)
        refreshReport();
}

void SignalUsageHotspotPanel::refreshReport()
{
    if (currentQuery.signalName.isEmpty()) return;
    const auto query = currentQuery;
    showHotspotForSymbol(query.signalName, query.fileName, query.moduleName, query.signalAccessPath);
}

void SignalUsageHotspotPanel::showHotspotForSymbol(const QString& symbol, const QString& file,
    const QString& module, const QString& accessPath)
{
    const bool same = currentQuery.signalName == symbol && EditorFileIdentity::same(currentQuery.fileName, file)
        && currentQuery.moduleName == module && currentQuery.signalAccessPath == accessPath;
    if (same) {
        if (pendingSelectedIdentity.isEmpty()) pendingSelectedIdentity = itemIdentity(selectedItemIndex);
    } else {
        selectedItemIndex = -1;
        pendingSelectedIdentity.clear();
        clearMatrixFocus();
        expandedLane.clear();
        detailIndexes.clear();
        pendingReadingPosition = {};
        pendingReadingPositionValid = true;
    }
    currentQuery = {};
    currentQuery.signalName = symbol;
    currentQuery.fileName = file;
    currentQuery.moduleName = module;
    currentQuery.signalAccessPath = accessPath;
    const auto token = SemanticIndex::getInstance()->snapshotToken();
    requestedSnapshotRevision = token.revision;
    const auto generation = ++reportGeneration;
    ++reportBuildRequestCount;
    ++activeReportBuilds;
    currentReportPending = true;
    if (!same) currentReport = {};
    titleLabel->setText(accessPath.isEmpty() ? symbol : accessPath);
    if (!same) renderUnavailable(QStringLiteral("Analyzing usage…"));
    const auto builder = reportBuilder ? reportBuilder : ReportBuilder(buildReportFromSnapshot);
    const auto query = currentQuery;
    auto* watcher = new QFutureWatcher<SignalUsageHotspotReport>(this);
    connect(watcher, &QFutureWatcher<SignalUsageHotspotReport>::finished, this,
        [this, watcher, generation, revision = token.revision] {
            const auto report = watcher->result();
            watcher->deleteLater();
            activeReportBuilds = qMax(0, activeReportBuilds - 1);
            if (generation != reportGeneration) return;
            currentReportPending = false;
            if (revision != SemanticIndex::getInstance()->snapshotToken().revision) {
                refreshReport();
                return;
            }
            currentReport = report;
            cacheReportFileKeys();
            selectedItemIndex = -1;
            for (int i = 0; !pendingSelectedIdentity.isEmpty() && i < currentReport.items.size(); ++i) {
                if (itemIdentity(i) == pendingSelectedIdentity) { selectedItemIndex = i; break; }
            }
            pendingSelectedIdentity.clear();
            detailIndexes.clear();
            if (selectedItemIndex >= 0) detailIndexes.append(selectedItemIndex);
            rebuild();
        });
    watcher->setFuture(QtConcurrent::run([builder, query, snapshot = token.snapshot] { return builder(query, snapshot); }));
}

void SignalUsageHotspotPanel::setCurrentEditorLocation(const QString& file, int line)
{
    if (currentEditorFileName == file && currentEditorLine == line) return;
    if (currentEditorFileName != file) currentEditorFileKey = EditorFileIdentity::lookupKey(file);
    currentEditorFileName = file;
    currentEditorLine = line;
    updateEditorMarkers();
    refreshGraphViewActionAvailability();
}

void SignalUsageHotspotPanel::updateEditorMarkers()
{
    for (const auto& marker : editorMarkers) {
        const auto& lane = currentReport.trackLanes.at(marker.laneIndex);
        const bool visible = fileKey(lane.fileName) == currentEditorFileKey
            && currentEditorLine >= lane.startLine && currentEditorLine <= lane.endLine;
        marker.item->setVisible(visible);
        if (!visible) continue;
        const qreal x = railLeft + lastTrackRailWidth * (currentEditorLine - lane.startLine)
            / qMax(1, lane.endLine - lane.startLine);
        marker.item->setLine(x, marker.y + qMax<qreal>(fontMetrics().height() + 6, rowHeight * 0.55),
                            x, marker.y + rowHeight - 2);
        marker.item->setToolTip(QStringLiteral("Current editor line %1").arg(currentEditorLine));
    }
}

void SignalUsageHotspotPanel::buildProjection()
{
    visibleLanes.clear();
    QHash<QString, int> lanes;
    for (int i = 0; i < currentReport.trackLanes.size(); ++i) {
        const auto& lane = currentReport.trackLanes.at(i);
        lanes.insert(laneKey(lane.moduleName, lane.fileName), i);
    }
    QHash<int, VisibleLane> rows;
    for (int i = 0; i < currentReport.items.size(); ++i) {
        const auto& item = currentReport.items.at(i);
        if (!itemPassesFilters(item)) continue;
        const auto found = lanes.constFind(laneKey(item.moduleName, item.fileName));
        if (found == lanes.cend()) continue;
        auto& row = rows[*found];
        row.laneIndex = *found;
        row.indexes.append(i);
        row.roles[static_cast<int>(item.role)].append(i);
    }
    auto order = rows.keys();
    std::sort(order.begin(), order.end());
    for (int i : order) visibleLanes.append(rows.value(i));
    focusedItemIndexes.clear();
    if (activeMatrixCellValid) {
        for (int i : filteredItemIndexes(false)) {
            const auto& item = currentReport.items.at(i);
            if (item.role == activeMatrixRole && laneKey(item.moduleName, item.fileName)
                == laneKey(activeMatrixModuleName, activeMatrixFileName)) focusedItemIndexes.insert(i);
        }
        detailIndexes = focusedItemIndexes.values();
        std::sort(detailIndexes.begin(), detailIndexes.end());
    } else {
        detailIndexes.removeIf([this](int i) {
            return i < 0 || i >= currentReport.items.size() || !itemPassesFilters(currentReport.items.at(i));
        });
    }
}

SignalUsageHotspotPanel::ReadingPosition SignalUsageHotspotPanel::captureReadingPosition() const
{
    ReadingPosition result;
    if (!modeStack) return result;
    const int mode = modeStack->currentIndex();
    const auto* view = mode ? matrixView : trackView;
    const qreal top = view->verticalScrollBar()->value();
    qreal best = -1;
    for (auto it = rowPositions[mode].cbegin(); it != rowPositions[mode].cend(); ++it) {
        if (it.value() <= top + 1 && it.value() > best) { result.lane = it.key(); best = it.value(); }
    }
    result.offset = best >= 0 ? top - best : top;
    result.horizontal = trackView->horizontalScrollBar()->value();
    return result;
}

void SignalUsageHotspotPanel::restoreReadingPosition(const ReadingPosition& position)
{
    for (int mode = 0; mode < 2; ++mode) {
        auto* view = mode ? matrixView : trackView;
        const auto row = rowPositions[mode].constFind(position.lane);
        view->verticalScrollBar()->setValue(qRound((row == rowPositions[mode].cend() ? 0 : *row) + position.offset));
    }
    trackView->horizontalScrollBar()->setValue(position.horizontal);
}

void SignalUsageHotspotPanel::rebuild()
{
    if (rebuilding) return;
    rebuilding = true;
    const auto reading = pendingReadingPositionValid ? pendingReadingPosition : captureReadingPosition();
    if (currentReport.found) pendingReadingPositionValid = false;
    buildProjection();
    QFontMetricsF metrics(font());
    rowHeight = qMax<qreal>(40, std::ceil(metrics.height() * 2 + 6));
    railLeft = qBound<qreal>(130, metrics.horizontalAdvance(QLatin1Char('M')) * 19, 220);
    for (int i = 0; i < roleButtons.size(); ++i) {
        QPixmap icon(12, 12);
        icon.fill(Qt::transparent);
        QPainter painter(&icon);
        painter.setPen(Qt::NoPen);
        painter.setBrush(roleColor(static_cast<SignalUsageHotspotRole>(i)));
        painter.drawEllipse(QRectF(2, 2, 8, 8));
        painter.end();
        roleButtons.at(i)->setIcon(QIcon(icon));
        roleButtons.at(i)->setIconSize(QSize(12, 12));
    }
    const QString signal = currentReport.declarationDisplayName.isEmpty()
        ? currentQuery.signalName : currentReport.declarationDisplayName;
    titleLabel->setText(signal.isEmpty() ? QStringLiteral("Hotspot") : signal);
    titleLabel->setToolTip(signal);
    if (!currentReport.found) {
        renderUnavailable(currentQuery.signalName.isEmpty()
            ? QStringLiteral("Select a signal in the editor to inspect its usage.")
            : currentReportPending ? QStringLiteral("Analyzing usage…")
            : currentReport.notFoundReasonDisplayName.isEmpty() ? QStringLiteral("Signal is unavailable in the current analysis.")
            : currentReport.notFoundReasonDisplayName);
    } else if (visibleLanes.isEmpty()) {
        renderUnavailable(currentReport.items.isEmpty() ? QStringLiteral("No usages found.")
                                                       : QStringLiteral("No usages match the filters."));
    } else {
        renderTrack();
        renderMatrix();
    }
    restoreReadingPosition(reading);
    refreshGraphViewActionAvailability();
    rebuilding = false;
}

void SignalUsageHotspotPanel::renderUnavailable(const QString& message)
{
    editorMarkers.clear();
    trackClusters.clear();
    trackClusterRects.clear();
    lastTrackBlockCount = lastTrackLaneCount = lastMatrixNonEmptyCellCount = 0;
    for (auto* scene : {trackScene, matrixScene}) {
        scene->clear();
        sceneText(scene, message, font(), InsightVisualStyle::theme().textSecondary, 8, 12);
        scene->setSceneRect(scene->itemsBoundingRect().adjusted(-8, -8, 8, 8));
    }
    rowPositions[0].clear(); rowPositions[1].clear();
    refreshGraphViewActionAvailability();
}

qreal SignalUsageHotspotPanel::targetTrackRailWidth() const
{
    const qreal width = modeStack ? modeStack->width() : 900;
    return qMax<qreal>(180, width - railLeft - 44) * trackZoomFactor;
}

void SignalUsageHotspotPanel::renderTrack()
{
    editorMarkers.clear();
    trackScene->clear();
    trackClusters.clear(); trackClusterRects.clear(); rowPositions[0].clear();
    lastTrackRailWidth = targetTrackRailWidth();
    lastTrackSceneWidth = railLeft + lastTrackRailWidth + 24;
    lastTrackLaneCount = visibleLanes.size();
    lastTrackBlockCount = 0;
    const auto& theme = InsightVisualStyle::theme();
    const QFontMetricsF metrics(font());
    const qreal headerHeight = metrics.height() + 7;
    sceneText(trackScene, QStringLiteral("Module / file"), font(), theme.textSecondary, 5, 1);
    sceneText(trackScene, QStringLiteral("Source lines"), font(), theme.textSecondary, railLeft, 1);
    qreal y = headerHeight;
    for (const auto& row : visibleLanes) {
        const auto& lane = currentReport.trackLanes.at(row.laneIndex);
        const QString key = laneKey(lane.moduleName, lane.fileName);
        rowPositions[0].insert(key, y);
        trackScene->addLine(0, y + rowHeight, lastTrackSceneWidth, y + rowHeight, QPen(theme.border));
        auto* label = sceneText(trackScene, metrics.elidedText(lane.moduleName.isEmpty() ? QFileInfo(lane.fileName).fileName() : lane.moduleName,
                               Qt::ElideRight, railLeft - 16), font(), theme.textPrimary, 5, y + 2);
        label->setToolTip(lane.moduleName + QLatin1Char('\n') + lane.fileName);
        auto* file = sceneText(trackScene, metrics.elidedText(QFileInfo(lane.fileName).fileName(), Qt::ElideMiddle, railLeft - 16),
                               font(), theme.textSecondary, 5, y + rowHeight - metrics.height() - 2);
        file->setToolTip(lane.fileName);
        const qreal centerY = y + rowHeight / 2;
        trackScene->addLine(railLeft, centerY, railLeft + lastTrackRailWidth, centerY, QPen(theme.border));
        for (int tick = 0; tick <= 4; ++tick) {
            const qreal x = railLeft + lastTrackRailWidth * tick / 4;
            const int line = lane.startLine + qRound((lane.endLine - lane.startLine) * tick / 4.0);
            auto* text = sceneText(trackScene, QString::number(line), font(), theme.textMuted, x, y + rowHeight - metrics.height());
            text->setScale(0.85);
            text->setPos(x - text->boundingRect().width() * 0.425, text->pos().y());
        }
        struct Cluster { QList<int> indexes; qreal first = 0; qreal last = 0; qreal width = 0; };
        QList<Cluster> clusters;
        QList<int> ordered = row.indexes;
        std::stable_sort(ordered.begin(), ordered.end(), [this](int a, int b) {
            return currentReport.items.at(a).line < currentReport.items.at(b).line;
        });
        const auto markWidth = [&metrics](int count) { return qMax<qreal>(18, metrics.horizontalAdvance(count > 1 ? QString::number(count) : QStringLiteral("W")) + 10); };
        for (int index : ordered) {
            const auto& item = currentReport.items.at(index);
            const qreal x = railLeft + lastTrackRailWidth * (item.line - lane.startLine) / qMax(1, lane.endLine - lane.startLine);
            clusters.append({{index}, x, x, markWidth(1)});
            // Merging can widen the count badge. Recheck the previous interval
            // so every overlapping mark joins one complete, selectable cluster.
            while (clusters.size() > 1) {
                auto& right = clusters.last();
                auto& left = clusters[clusters.size() - 2];
                if ((left.first + left.last + left.width) / 2 + 2
                    < (right.first + right.last - right.width) / 2) break;
                left.indexes.append(right.indexes);
                left.last = right.last;
                left.width = markWidth(left.indexes.size());
                clusters.removeLast();
            }
        }
        for (const auto& cluster : clusters) {
            const auto& first = currentReport.items.at(cluster.indexes.first());
            QMap<int, int> counts;
            for (int index : cluster.indexes) ++counts[static_cast<int>(currentReport.items.at(index).role)];
            QStringList roles;
            for (auto it = counts.cbegin(); it != counts.cend(); ++it)
                roles.append(QStringLiteral("%1 %2").arg(roleName(static_cast<SignalUsageHotspotRole>(it.key()))).arg(it.value()));
            const qreal x = (cluster.first + cluster.last) / 2;
            const QRectF rect(x - cluster.width / 2, y + 2, cluster.width, qMax<qreal>(18, metrics.height() + 2));
            bool highlighted = false;
            for (int index : cluster.indexes) highlighted |= index == selectedItemIndex || focusedItemIndexes.contains(index);
            auto* mark = new UsageMark(rect, cluster.indexes.size() > 1 ? QString::number(cluster.indexes.size()) : roleSymbol(first.role),
                font(), counts.size() == 1 ? roleColor(first.role) : theme.textSecondary, highlighted,
                [this, indexes = cluster.indexes, key, generation = reportGeneration](bool jump) {
                    if (generation != reportGeneration) return;
                    if (jump && indexes.size() == 1) { navigateItem(indexes.first()); return; }
                    // Scene items must survive their current event dispatch.
                    QTimer::singleShot(0, this, [this, indexes, key, generation] {
                        if (generation == reportGeneration) selectIndexes(indexes, key);
                    });
                });
            mark->setData(kItemIndexRole, cluster.indexes.first());
            mark->setToolTip(cluster.indexes.size() == 1 ? usageToolTip(first)
                : QStringLiteral("%1 references · lines %2–%3\n%4\n%5\nSelect to inspect all references.")
                    .arg(cluster.indexes.size()).arg(first.line).arg(currentReport.items.at(cluster.indexes.last()).line)
                    .arg(roles.join(QStringLiteral(" · ")), lane.fileName));
            trackScene->addItem(mark);
            trackClusters.append(cluster.indexes); trackClusterRects.append(rect);
            ++lastTrackBlockCount;
        }
        auto* marker = trackScene->addLine(QLineF(), QPen(theme.accent, 2, Qt::DashLine));
        marker->setZValue(-1);
        marker->setData(Qt::UserRole, QStringLiteral("currentEditorLine"));
        editorMarkers.append({row.laneIndex, y, marker});
        y += rowHeight;
        if (!matrixModeForTest() && key == expandedLane && !detailIndexes.isEmpty())
            y += addInlineDetails(trackScene, y, qMax<qreal>(280, modeStack->width() - 24));
    }
    trackScene->setSceneRect(0, 0, lastTrackSceneWidth, y + 3);
    updateEditorMarkers();
}

void SignalUsageHotspotPanel::renderMatrix()
{
    matrixScene->clear(); rowPositions[1].clear();
    lastMatrixNonEmptyCellCount = 0;
    const auto& theme = InsightVisualStyle::theme();
    const QFontMetricsF metrics(font());
    const qreal headerHeight = metrics.height() + 7;
    const qreal columnWidth = qMax<qreal>(metrics.horizontalAdvance(QStringLiteral("Condition")) + 16,
                                        (modeStack->width() - railLeft - 24) / 7.0);
    const qreal width = railLeft + columnWidth * 7;
    sceneText(matrixScene, QStringLiteral("Module / file"), font(), theme.textSecondary, 5, 1);
    for (int role = 0; role < 7; ++role) {
        const QString name = roleName(static_cast<SignalUsageHotspotRole>(role));
        sceneText(matrixScene, name, font(), theme.textSecondary,
            railLeft + role * columnWidth + (columnWidth - metrics.horizontalAdvance(name)) / 2, 1);
    }
    qreal y = headerHeight;
    for (const auto& row : visibleLanes) {
        const auto& lane = currentReport.trackLanes.at(row.laneIndex);
        const QString key = laneKey(lane.moduleName, lane.fileName);
        rowPositions[1].insert(key, y);
        matrixScene->addLine(0, y + rowHeight, width, y + rowHeight, QPen(theme.border));
        auto* title = sceneText(matrixScene, metrics.elidedText(lane.moduleName.isEmpty() ? QFileInfo(lane.fileName).fileName() : lane.moduleName,
            Qt::ElideRight, railLeft - 16), font(), theme.textPrimary, 5, y + 2);
        title->setToolTip(lane.moduleName + QLatin1Char('\n') + lane.fileName);
        auto* file = sceneText(matrixScene, metrics.elidedText(QFileInfo(lane.fileName).fileName(), Qt::ElideMiddle, railLeft - 16),
            font(), theme.textSecondary, 5, y + rowHeight - metrics.height() - 2);
        file->setToolTip(lane.fileName);
        for (int role = 0; role < 7; ++role) {
            const auto indexes = row.roles.value(role);
            const QRectF rect(railLeft + role * columnWidth + 5, y + 5, columnWidth - 10, rowHeight - 10);
            if (indexes.isEmpty()) {
                sceneText(matrixScene, QStringLiteral("—"), font(), theme.textMuted,
                    rect.center().x() - metrics.horizontalAdvance(QStringLiteral("—")) / 2, y + (rowHeight - metrics.height()) / 2);
                continue;
            }
            const auto usageRole = static_cast<SignalUsageHotspotRole>(role);
            const bool selected = activeMatrixCellValid && activeMatrixRole == usageRole
                && activeMatrixModuleName == lane.moduleName && EditorFileIdentity::same(activeMatrixFileName, lane.fileName);
            auto* cell = new UsageMark(rect, QString::number(indexes.size()), font(), roleColor(usageRole), selected,
                [this, usageRole, module = lane.moduleName, file = lane.fileName, indexes, generation = reportGeneration](bool jump) {
                    if (generation != reportGeneration) return;
                    if (jump && indexes.size() == 1) { navigateItem(indexes.first()); return; }
                    QTimer::singleShot(0, this, [this, usageRole, module, file, generation] {
                        if (generation == reportGeneration) activateMatrixCell(usageRole, module, file, true);
                    });
                });
            cell->setToolTip(QStringLiteral("%1 · %2 references\n%3").arg(roleName(usageRole)).arg(indexes.size()).arg(lane.fileName));
            matrixScene->addItem(cell);
            ++lastMatrixNonEmptyCellCount;
        }
        y += rowHeight;
        if (matrixModeForTest() && key == expandedLane && !detailIndexes.isEmpty())
            y += addInlineDetails(matrixScene, y, qMax<qreal>(280, modeStack->width() - 24));
    }
    matrixScene->setSceneRect(0, 0, width, y + 3);
}

qreal SignalUsageHotspotPanel::addInlineDetails(QGraphicsScene* scene, qreal y, qreal width)
{
    auto* panel = new QWidget;
    panel->setObjectName(QStringLiteral("hotspotInlineDetails"));
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(8, 4, 8, 4); layout->setSpacing(2);
    auto* bar = new QHBoxLayout;
    auto* label = UiControls::label(QStringLiteral("%1 references").arg(detailIndexes.size()), panel);
    auto* jump = UiControls::toolButton(panel);
    jump->setObjectName(QStringLiteral("hotspotInlineJump")); jump->setText(QStringLiteral("Go to source"));
    auto* collapse = UiControls::toolButton(panel);
    collapse->setObjectName(QStringLiteral("hotspotInlineCollapse")); collapse->setText(QStringLiteral("Collapse"));
    bar->addWidget(label); bar->addStretch(); bar->addWidget(jump); bar->addWidget(collapse);
    layout->addLayout(bar);
    inlineItems = UiControls::treeWidget(panel);
    inlineItems->setObjectName(QStringLiteral("hotspotInlineItems"));
    inlineItems->setRootIsDecorated(false); inlineItems->setUniformRowHeights(true);
    inlineItems->setColumnCount(3);
    inlineItems->setHeaderLabels({QStringLiteral("Role"), QStringLiteral("Location"), QStringLiteral("Source")});
    inlineItems->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    inlineItems->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    inlineItems->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    inlineItems->setSelectionMode(QAbstractItemView::SingleSelection);
    QTreeWidgetItem* selected = nullptr;
    for (int index : detailIndexes) {
        const auto& item = currentReport.items.at(index);
        auto* row = new QTreeWidgetItem(inlineItems);
        row->setText(0, roleName(item.role));
        row->setText(1, QStringLiteral("%1:%2").arg(item.line).arg(item.column));
        row->setText(2, item.snippet.trimmed());
        row->setData(0, kItemIndexRole, index);
        for (int column = 0; column < 3; ++column) row->setToolTip(column, usageToolTip(item));
        if (index == selectedItemIndex) selected = row;
    }
    inlineItems->setCurrentItem(selected);
    layout->addWidget(inlineItems);
    connect(inlineItems, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
        if (item) selectedItemIndex = item->data(0, kItemIndexRole).toInt();
        refreshGraphViewActionAvailability();
    });
    connect(inlineItems, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
        if (item) navigateItem(item->data(0, kItemIndexRole).toInt());
    });
    connect(jump, &QToolButton::clicked, this, [this] { navigateItem(selectedItemIndex); });
    connect(collapse, &QToolButton::clicked, this, [this] { QTimer::singleShot(0, this, [this] { collapseDetails(); }); });
    const qreal height = qMin<qreal>(230, qMax<qreal>(100, fontMetrics().height() * (qMin(5, detailIndexes.size()) + 3) + 22));
    panel->setFixedSize(qRound(width), qRound(height));
    auto* proxy = scene->addWidget(panel);
    proxy->setPos(0, y);
    return height + 2;
}

void SignalUsageHotspotPanel::selectIndexes(const QList<int>& indexes, const QString& lane)
{
    if (indexes.isEmpty()) return;
    clearMatrixFocus();
    detailIndexes = indexes;
    expandedLane = lane;
    if (!indexes.contains(selectedItemIndex)) selectedItemIndex = indexes.first();
    rebuild();
}

void SignalUsageHotspotPanel::activateMatrixCell(SignalUsageHotspotRole role, const QString& module,
    const QString& file, bool focusTrack)
{
    Q_UNUSED(focusTrack);
    activeMatrixRole = role; activeMatrixModuleName = module; activeMatrixFileName = file;
    activeMatrixCellValid = true;
    expandedLane = laneKey(module, file);
    buildProjection();
    if (!focusedItemIndexes.contains(selectedItemIndex)) selectedItemIndex = detailIndexes.value(0, -1);
    rebuild();
}

void SignalUsageHotspotPanel::clearMatrixFocus()
{ activeMatrixCellValid = false; focusedItemIndexes.clear(); }
void SignalUsageHotspotPanel::collapseDetails()
{ expandedLane.clear(); rebuild(); }

void SignalUsageHotspotPanel::setMode(bool matrix)
{
    matrixModeButton->setChecked(matrix); trackModeButton->setChecked(!matrix);
    if (modeStack->currentIndex() == int(matrix)) return;
    pendingReadingPosition = captureReadingPosition(); pendingReadingPositionValid = true;
    modeStack->setCurrentIndex(int(matrix));
    matrixModeButton->setChecked(matrix); trackModeButton->setChecked(!matrix);
    rebuild();
    if (!matrix && activeMatrixCellValid) {
        const auto rect = trackRectForItem(selectedItemIndex);
        if (!rect.isEmpty()) trackView->ensureVisible(rect, 20, 10);
    }
    saveLayout();
}

bool SignalUsageHotspotPanel::itemPassesFilters(const SignalUsageHotspotItem& item) const
{
    return enabledRoles.contains(static_cast<int>(item.role))
        && (searchText.isEmpty() || searchableText(item).contains(searchText, Qt::CaseInsensitive));
}
QList<int> SignalUsageHotspotPanel::filteredItemIndexes(bool includeMatrixFocus) const
{
    QList<int> indexes;
    for (int i = 0; i < currentReport.items.size(); ++i) {
        if (!itemPassesFilters(currentReport.items.at(i))) continue;
        if (includeMatrixFocus && activeMatrixCellValid && !focusedItemIndexes.contains(i)) continue;
        indexes.append(i);
    }
    return indexes;
}

QRectF SignalUsageHotspotPanel::trackRectForItem(int index) const
{
    for (int i = 0; i < trackClusters.size(); ++i)
        if (trackClusters.at(i).contains(index)) return trackClusterRects.at(i);
    return {};
}
bool SignalUsageHotspotPanel::setTrackZoom(double factor)
{
    trackZoomFactor = std::clamp(factor, kMinTrackZoom, kMaxTrackZoom);
    rebuild(); saveLayout(); return true;
}
bool SignalUsageHotspotPanel::zoomTrack(double factor)
{ if (matrixModeForTest()) setMode(false); return setTrackZoom(trackZoomFactor * factor); }
bool SignalUsageHotspotPanel::fitTrackToView()
{ if (matrixModeForTest()) setMode(false); return setTrackZoom(1.0); }
bool SignalUsageHotspotPanel::resetLayout()
{ return setTrackZoom(1.0); }
bool SignalUsageHotspotPanel::centerCurrentUsage()
{
    if (matrixModeForTest()) setMode(false);
    for (const auto& marker : editorMarkers) {
        if (marker.item->isVisible()) { trackView->centerOn(marker.item->line().center()); return true; }
    }
    const auto rect = trackRectForItem(selectedItemIndex);
    if (rect.isEmpty()) return false;
    trackView->ensureVisible(rect, 20, 10); return true;
}
void SignalUsageHotspotPanel::focusFit() { requestGraphViewAction(QString::fromLatin1(ActionIds::GraphViewFit)); }
void SignalUsageHotspotPanel::focusZoomIn() { requestGraphViewAction(QString::fromLatin1(ActionIds::GraphViewZoomIn)); }
void SignalUsageHotspotPanel::focusZoomOut() { requestGraphViewAction(QString::fromLatin1(ActionIds::GraphViewZoomOut)); }
void SignalUsageHotspotPanel::setFocusSearchText(const QString& text) { searchEdit->setText(text); }
QString SignalUsageHotspotPanel::focusSearchText() const { return searchEdit->text(); }
void SignalUsageHotspotPanel::focusInspector()
{
    if (selectedItemIndex >= 0) {
        const auto& item = currentReport.items.at(selectedItemIndex);
        expandedLane = laneKey(item.moduleName, item.fileName);
        if (detailIndexes.isEmpty()) detailIndexes.append(selectedItemIndex);
        rebuild();
        if (inlineItems) inlineItems->setFocus();
    }
}
void SignalUsageHotspotPanel::refreshThemePresentation() { rebuild(); queueRebuild(); }
void SignalUsageHotspotPanel::navigateItem(int index)
{
    if (index < 0 || index >= currentReport.items.size()) return;
    const auto& item = currentReport.items.at(index);
    if (navigationHandler && item.line > 0 && item.column > 0)
        navigationHandler(item.fileName, item.line, item.column);
}
void SignalUsageHotspotPanel::showStatusMessage(const QString& message, int timeout) const
{ if (statusMessageHandler) statusMessageHandler(message, timeout); }

QString SignalUsageHotspotPanel::itemIdentity(int index) const
{
    if (index < 0 || index >= currentReport.items.size()) return {};
    const auto& item = currentReport.items.at(index);
    return QString::fromUtf8(QJsonDocument::fromVariant(QVariantList{static_cast<int>(item.role),
        item.moduleName, fileKey(item.fileName), item.line, item.column, item.snippet}).toJson(QJsonDocument::Compact));
}

void SignalUsageHotspotPanel::cacheReportFileKeys()
{
    QSet<QString> files;
    for (const auto& item : currentReport.items) files.insert(item.fileName);
    for (const auto& lane : currentReport.trackLanes) files.insert(lane.fileName);
    reportFileKeys = EditorFileIdentity::lookupKeys(files.values());
}
QString SignalUsageHotspotPanel::fileKey(const QString& file) const
{
    if (file.isEmpty() || file == QStringLiteral("<unknown>")) return QStringLiteral("<unknown>");
    const auto key = reportFileKeys.constFind(file);
    return key == reportFileKeys.cend() ? EditorFileIdentity::lookupKey(file) : *key;
}
QString SignalUsageHotspotPanel::laneKey(const QString& module, const QString& file) const
{ return (module.isEmpty() ? QStringLiteral("<unknown>") : module) + QChar(0x1f) + fileKey(file); }

QVariantMap SignalUsageHotspotPanel::saveViewState() const
{
    const auto reading = captureReadingPosition();
    QVariantList roles;
    for (int i = 0; i < 7; ++i) if (enabledRoles.contains(i)) roles.append(i);
    return {{"hotspotStateVersion", 1}, {"signal", currentQuery.signalName}, {"file", currentQuery.fileName},
        {"module", currentQuery.moduleName}, {"accessPath", currentQuery.signalAccessPath},
        {"matrix", matrixModeForTest()}, {"search", searchEdit->text()}, {"roles", roles},
        {"zoom", trackZoomFactor}, {"selected", itemIdentity(selectedItemIndex)}, {"expandedLane", expandedLane},
        {"matrixSelection", activeMatrixCellValid}, {"matrixRole", static_cast<int>(activeMatrixRole)},
        {"matrixModule", activeMatrixModuleName}, {"matrixFile", activeMatrixFileName},
        {"readingLane", reading.lane}, {"readingOffset", reading.offset}, {"horizontal", reading.horizontal}};
}

void SignalUsageHotspotPanel::restoreViewState(const QVariantMap& state)
{
    if (!state.contains(QStringLiteral("hotspotStateVersion"))) return;
    const QString signal = state.value("signal").toString();
    if (signal.isEmpty()) clearTarget();
    else showHotspotForSymbol(signal, state.value("file").toString(), state.value("module").toString(), state.value("accessPath").toString());
    QSignalBlocker block(searchEdit);
    searchEdit->setText(state.value("search").toString()); searchText = searchEdit->text().trimmed();
    enabledRoles.clear();
    for (const auto& role : state.value("roles").toList()) if (role.toInt() >= 0 && role.toInt() < 7) enabledRoles.insert(role.toInt());
    for (int i = 0; i < roleButtons.size(); ++i) { QSignalBlocker guard(roleButtons[i]); roleButtons[i]->setChecked(enabledRoles.contains(i)); }
    trackZoomFactor = std::clamp(state.value("zoom", 1.0).toDouble(), kMinTrackZoom, kMaxTrackZoom);
    const bool matrix = state.value("matrix", true).toBool();
    modeStack->setCurrentIndex(int(matrix)); matrixModeButton->setChecked(matrix); trackModeButton->setChecked(!matrix);
    pendingSelectedIdentity = state.value("selected").toString();
    expandedLane = state.value("expandedLane").toString();
    activeMatrixCellValid = state.value("matrixSelection").toBool();
    activeMatrixRole = static_cast<SignalUsageHotspotRole>(qBound(0, state.value("matrixRole").toInt(), 6));
    activeMatrixModuleName = state.value("matrixModule").toString(); activeMatrixFileName = state.value("matrixFile").toString();
    pendingReadingPosition = {state.value("readingLane").toString(), state.value("readingOffset").toDouble(), state.value("horizontal").toInt()};
    pendingReadingPositionValid = true;
    if (signal.isEmpty()) rebuild();
}

void SignalUsageHotspotPanel::restoreLayout()
{
    QSettings settings;
    settings.beginGroup(QString::fromLatin1(kSettingsGroup));
    const bool matrix = settings.value(QStringLiteral("mode"), 1).toInt() == 1;
    trackZoomFactor = std::clamp(settings.value(QStringLiteral("trackZoom"), 1.0).toDouble(), kMinTrackZoom, kMaxTrackZoom);
    modeStack->setCurrentIndex(int(matrix)); matrixModeButton->setChecked(matrix); trackModeButton->setChecked(!matrix);
}
void SignalUsageHotspotPanel::saveLayout() const
{
    QSettings settings;
    settings.beginGroup(QString::fromLatin1(kSettingsGroup));
    settings.setValue(QStringLiteral("mode"), modeStack->currentIndex());
    settings.setValue(QStringLiteral("trackZoom"), trackZoomFactor);
}

GraphExportResult SignalUsageHotspotPanel::exportGraph(SignalUsageHotspotExportSurface surface,
    const QString& path, const GraphExportOptions& options) const
{ return GraphExportService::exportGraphicsScene(surface == SignalUsageHotspotExportSurface::Matrix ? matrixScene : trackScene, path, options); }
QAction* SignalUsageHotspotPanel::graphExportAction(SignalUsageHotspotExportSurface surface) const
{ return surface == SignalUsageHotspotExportSurface::Matrix ? exportMatrixAction : exportTrackAction; }

void SignalUsageHotspotPanel::renderReportForTest(const SignalUsageHotspotReport& report)
{ ++reportGeneration; currentReportPending = false; currentReport = report; cacheReportFileKeys(); selectedItemIndex = -1; detailIndexes.clear(); expandedLane.clear(); clearMatrixFocus(); rebuild(); }
int SignalUsageHotspotPanel::trackBlockCountForTest() const { return lastTrackBlockCount; }
int SignalUsageHotspotPanel::trackLaneCountForTest() const { return lastTrackLaneCount; }
int SignalUsageHotspotPanel::matrixNonEmptyCellCountForTest() const { return lastMatrixNonEmptyCellCount; }
int SignalUsageHotspotPanel::matrixItemCountForTest() const { return filteredItemIndexes().size(); }
QList<qreal> SignalUsageHotspotPanel::trackBlockCenterXsForTest() const
{ QList<qreal> result; for (const auto& rect : trackClusterRects) result.append(rect.center().x()); std::sort(result.begin(), result.end()); return result; }
qreal SignalUsageHotspotPanel::firstTrackRailWidthForTest() const { return lastTrackRailWidth; }
qreal SignalUsageHotspotPanel::trackSceneWidthForTest() const { return lastTrackSceneWidth; }
qreal SignalUsageHotspotPanel::trackRailLeftForTest() const { return railLeft; }
qreal SignalUsageHotspotPanel::rowHeightForTest() const { return rowHeight; }
int SignalUsageHotspotPanel::firstTrackLaneStartLineForTest() const { return currentReport.trackLanes.isEmpty() ? 0 : currentReport.trackLanes.first().startLine; }
int SignalUsageHotspotPanel::firstTrackLaneEndLineForTest() const { return currentReport.trackLanes.isEmpty() ? 0 : currentReport.trackLanes.first().endLine; }
bool SignalUsageHotspotPanel::selectMatrixCellForTest(SignalUsageHotspotRole role, const QString& module, const QString& file)
{ activateMatrixCell(role, module, file, true); return !focusedItemIndexes.isEmpty(); }
bool SignalUsageHotspotPanel::selectUsageForTest(int index)
{
    if (index < 0 || index >= currentReport.items.size()) return false;
    selectedItemIndex = index;
    const auto& item = currentReport.items.at(index);
    selectIndexes({index}, laneKey(item.moduleName, item.fileName)); return true;
}
bool SignalUsageHotspotPanel::triggerFirstUsageNavigationForTest()
{ const auto indexes = filteredItemIndexes(); if (indexes.isEmpty()) return false; navigateItem(indexes.first()); return true; }
void SignalUsageHotspotPanel::setReportBuilderForTest(ReportBuilder builder) { reportBuilder = std::move(builder); }
bool SignalUsageHotspotPanel::reportBuildInFlightForTest() const { return activeReportBuilds > 0; }
quint64 SignalUsageHotspotPanel::reportBuildRequestCountForTest() const { return reportBuildRequestCount; }
int SignalUsageHotspotPanel::selectedItemIndexForTest() const { return selectedItemIndex; }
QString SignalUsageHotspotPanel::currentDeclarationDisplayNameForTest() const { return currentReport.declarationDisplayName; }
void SignalUsageHotspotPanel::setMatrixModeForTest(bool matrix) { setMode(matrix); }
bool SignalUsageHotspotPanel::matrixModeForTest() const { return modeStack->currentIndex() == 1; }
QList<QList<int>> SignalUsageHotspotPanel::trackClustersForTest() const { return trackClusters; }
bool SignalUsageHotspotPanel::selectTrackClusterForTest(int index)
{
    if (index < 0 || index >= trackClusters.size()) return false;
    const auto indexes = trackClusters.at(index);
    const auto& item = currentReport.items.at(indexes.first());
    selectIndexes(indexes, laneKey(item.moduleName, item.fileName)); return true;
}
QTreeWidget* SignalUsageHotspotPanel::inlineItemsForTest() const { return inlineItems; }
