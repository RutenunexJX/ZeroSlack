#include <QPainter>
#include "insightcanvas.h"

#include "applicationthememanager.h"
#include "insightgraphview.h"
#include "insightvisualstyle.h"
#include "signalkernelgraphservice.h"

#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QHBoxLayout>
#include <QPainterPath>
#include <QPen>
#include <QScrollBar>
#include <QSet>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {
constexpr int kNodeIdRole = Qt::UserRole + 210;
constexpr qreal kNodeWidth = 188.0;
constexpr qreal kNodeHeight = 68.0;
constexpr qreal kHorizontalGap = 88.0;
constexpr qreal kVerticalGap = 34.0;

class CanvasNodeItem final : public QGraphicsRectItem
{
public:
    void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        QPen outline = pen();
        if (isSelected()) outline = InsightVisualStyle::selectedPen();
        painter->setPen(outline);
        painter->setBrush(brush());
        painter->drawRoundedRect(rect(), 8, 8);
        painter->restore();
    }

    std::function<void()> moved;

    QVariant itemChange(GraphicsItemChange change,
                        const QVariant& value) override
    {
        const QVariant result = QGraphicsRectItem::itemChange(change, value);
        if (change == ItemPositionHasChanged && moved)
            moved();
        return result;
    }
};

QColor nodeFill(const InsightGraphNode& node)
{
    const InsightTheme& theme = InsightVisualStyle::theme();
    if (node.domain == InsightGraphDomain::Hotspot) {
        return InsightVisualStyle::heatIntensityColor(
            qBound(0.0, node.heatScore / 100.0, 1.0));
    }
    if (node.domain == InsightGraphDomain::Fsm)
        return theme.semantic.conditionFill;
    if (node.domain == InsightGraphDomain::Hierarchy)
        return theme.semantic.portFill;
    if (node.domain == InsightGraphDomain::DataFlow)
        return theme.semantic.dataFill;
    return theme.graph.nodeFill;
}

QString nodeTooltip(const InsightGraphNode& node)
{
    QStringList rows;
    rows.append(node.displayName);
    if (!node.detail.trimmed().isEmpty())
        rows.append(node.detail);
    if (!node.sourceFile.trimmed().isEmpty()) {
        rows.append(node.sourceLine > 0
                        ? QStringLiteral("%1:%2").arg(node.sourceFile).arg(node.sourceLine)
                        : node.sourceFile);
    }
    return rows.join(QLatin1Char('\n'));
}

QString elidedDetail(const QString& detail)
{
    QString normalized = detail.simplified();
    if (normalized.size() > 54)
        normalized = normalized.left(51) + QStringLiteral("...");
    return normalized;
}
}

InsightCanvas::InsightCanvas(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("insightCanvas"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(4);

    graphScene = new QGraphicsScene(this);
    graphScene->setObjectName(QStringLiteral("insightCanvasScene"));
    graphView = new InsightGraphView(graphScene, this);
    graphView->setObjectName(QStringLiteral("insightCanvasView"));
    graphView->setGridVisible(true);
    graphView->setClearSelectionOnEmptyLeftClick(true);
    graphView->setDragMode(QGraphicsView::RubberBandDrag);
    graphView->setDoubleClickHandler(
        [this](const QPoint& position) {
            if (!activationHandler || !graphView)
                return false;
            for (QGraphicsItem* item : graphView->items(position)) {
                QGraphicsItem* current = item;
                while (current
                       && current->data(kNodeIdRole).toString().isEmpty()) {
                    current = current->parentItem();
                }
                const QString id = current
                    ? current->data(kNodeIdRole).toString() : QString();
                if (!id.isEmpty() && currentSnapshot.nodes.contains(id)) {
                    activationHandler(currentSnapshot.nodes.value(id));
                    return true;
                }
            }
            return false;
        });
    root->addWidget(graphView, 1);

    auto* minimapRow = new QHBoxLayout;
    minimapRow->setContentsMargins(0, 0, 4, 4);
    minimapRow->addStretch(1);
    minimapView = new InsightGraphView(graphScene, this);
    minimapView->setObjectName(QStringLiteral("insightCanvasMinimap"));
    minimapView->setFixedSize(176, 112);
    minimapView->setInteractive(false);
    minimapView->setFocusPolicy(Qt::NoFocus);
    minimapView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    minimapView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    minimapRow->addWidget(minimapView);
    root->addLayout(minimapRow);

    QObject::connect(
        graphScene,
        &QGraphicsScene::selectionChanged,
        this,
        [this]() {
            if (selectionChangedHandler)
                selectionChangedHandler(selectedNodeIds());
        });
    QObject::connect(
        &ApplicationThemeManager::instance(),
        &ApplicationThemeManager::themeChanged,
        this,
        [this](ThemeMode) { applyTheme(); });
    applyTheme();
}

InsightCanvas::~InsightCanvas()
{
    clearVisuals();
}

void InsightCanvas::applyUpdate(const InsightGraphUpdate& update)
{
    setSnapshot(update.snapshot, update.diff);
}

void InsightCanvas::setSnapshot(const InsightGraphSnapshot& snapshotValue,
                                const InsightGraphDiff& diffValue)
{
    InsightGraphDiff effectiveDiff = diffValue;
    if (effectiveDiff.isEmpty()
        && currentSnapshot.contentFingerprint != snapshotValue.contentFingerprint) {
        effectiveDiff = InsightGraphCore::diff(currentSnapshot, snapshotValue);
    }
    const bool rebuild = !currentSnapshot.isValid()
        || effectiveDiff.contextChanged
        || currentSnapshot.layoutHint != snapshotValue.layoutHint;
    const QHash<QString, QPointF> positions = layoutPositions(snapshotValue);

    if (rebuild) {
        clearVisuals();
        currentSnapshot = snapshotValue;
        QStringList ids = snapshotValue.nodes.keys();
        ids.sort();
        for (const QString& id : ids) {
            const QPointF position = positions.value(id);
            addOrUpdateNode(snapshotValue.nodes.value(id), &position);
        }
        QStringList edgeIds = snapshotValue.edges.keys();
        edgeIds.sort();
        for (const QString& edgeId : edgeIds)
            addOrUpdateEdge(snapshotValue.edges.value(edgeId));
    } else {
        for (const QString& edgeId : effectiveDiff.removedEdgeIds)
            removeEdge(edgeId);
        for (const QString& nodeId : effectiveDiff.removedNodeIds)
            removeNode(nodeId);
        currentSnapshot = snapshotValue;
        for (const QString& nodeId : effectiveDiff.addedNodeIds) {
            const QPointF position = positions.value(nodeId);
            addOrUpdateNode(snapshotValue.nodes.value(nodeId), &position);
        }
        for (const QString& nodeId : effectiveDiff.updatedNodeIds)
            addOrUpdateNode(snapshotValue.nodes.value(nodeId));
        for (const QString& edgeId : effectiveDiff.addedEdgeIds)
            addOrUpdateEdge(snapshotValue.edges.value(edgeId));
        for (const QString& edgeId : effectiveDiff.updatedEdgeIds)
            addOrUpdateEdge(snapshotValue.edges.value(edgeId));
    }
    updateAllEdgePaths();
    applySearch();
    updateSceneRect();
    updateMinimap();
}

const InsightGraphSnapshot& InsightCanvas::snapshot() const
{
    return currentSnapshot;
}

QGraphicsScene* InsightCanvas::scene() const
{
    return graphScene;
}

InsightGraphView* InsightCanvas::view() const
{
    return graphView;
}

InsightGraphView* InsightCanvas::minimap() const
{
    return minimapView;
}

void InsightCanvas::zoomIn()
{
    if (graphView)
        graphView->zoomIn();
}

void InsightCanvas::zoomOut()
{
    if (graphView)
        graphView->zoomOut();
}

void InsightCanvas::fit()
{
    if (graphView)
        graphView->fitScene();
}

void InsightCanvas::resetView()
{
    if (graphView)
        graphView->resetView();
}

qreal InsightCanvas::zoomFactor() const
{
    return graphView ? graphView->currentZoom() : 1.0;
}

void InsightCanvas::setPanMode(bool enabled)
{
    panModeValue = enabled;
    if (graphView) {
        graphView->setDragMode(
            enabled ? QGraphicsView::ScrollHandDrag
                    : QGraphicsView::RubberBandDrag);
    }
}

bool InsightCanvas::panMode() const
{
    return panModeValue;
}

void InsightCanvas::setMinimapVisible(bool visible)
{
    if (minimapView)
        minimapView->setVisible(visible);
}

bool InsightCanvas::isMinimapVisible() const
{
    return minimapView && !minimapView->isHidden();
}

void InsightCanvas::setSearchText(const QString& text)
{
    searchText = text.trimmed();
    applySearch();
}

QStringList InsightCanvas::selectedNodeIds() const
{
    QStringList result;
    if (!graphScene)
        return result;
    for (QGraphicsItem* item : graphScene->selectedItems()) {
        const QString id = item
            ? item->data(kNodeIdRole).toString() : QString();
        if (!id.isEmpty() && !result.contains(id))
            result.append(id);
    }
    result.sort();
    return result;
}

void InsightCanvas::selectNodeIds(const QStringList& nodeIds)
{
    const QSet<QString> selected(nodeIds.cbegin(), nodeIds.cend());
    for (auto it = nodeItems.cbegin(); it != nodeItems.cend(); ++it) {
        if (it.value())
            it.value()->setSelected(selected.contains(it.key()));
    }
}

QHash<QString, QPointF> InsightCanvas::nodePositions() const
{
    QHash<QString, QPointF> result;
    for (auto it = nodeItems.cbegin(); it != nodeItems.cend(); ++it) {
        if (it.value())
            result.insert(it.key(), it.value()->pos());
    }
    return result;
}

void InsightCanvas::setSelectionChangedHandler(
    SelectionChangedHandler handler)
{
    selectionChangedHandler = std::move(handler);
}

void InsightCanvas::setActivationHandler(ActivationHandler handler)
{
    activationHandler = std::move(handler);
}

GraphExportResult InsightCanvas::exportGraph(
    const QString& outputPath,
    const GraphExportOptions& options) const
{
    return GraphExportService::exportGraphicsScene(
        graphScene, outputPath, options);
}

int InsightCanvas::nodeItemCountForTest() const
{
    return nodeItems.size();
}

int InsightCanvas::edgeItemCountForTest() const
{
    return edgeItems.size();
}

void InsightCanvas::clearVisuals()
{
    nodeItems.clear();
    edgeItems.clear();
    if (graphScene)
        graphScene->clear();
}

void InsightCanvas::removeNode(const QString& nodeId)
{
    QGraphicsRectItem* item = nodeItems.take(nodeId);
    if (item && graphScene) {
        graphScene->removeItem(item);
        delete item;
    }
}

void InsightCanvas::removeEdge(const QString& edgeId)
{
    const EdgeVisual visual = edgeItems.take(edgeId);
    if (visual.path && graphScene) {
        graphScene->removeItem(visual.path);
        delete visual.path;
    }
    if (visual.label && graphScene) {
        graphScene->removeItem(visual.label);
        delete visual.label;
    }
}

void InsightCanvas::addOrUpdateNode(
    const InsightGraphNode& node,
    const QPointF* initialPosition)
{
    if (!graphScene || !node.isValid())
        return;
    auto* item = dynamic_cast<CanvasNodeItem*>(
        nodeItems.value(node.nodeId, nullptr));
    if (!item) {
        item = new CanvasNodeItem;
        item->setRect(0.0, 0.0, kNodeWidth, kNodeHeight);
        item->setData(kNodeIdRole, node.nodeId);
        item->setFlags(
            QGraphicsItem::ItemIsSelectable
            | QGraphicsItem::ItemIsMovable
            | QGraphicsItem::ItemSendsGeometryChanges);
        item->setZValue(10.0);
        item->moved = [this]() {
            updateAllEdgePaths();
            updateSceneRect();
            updateMinimap();
        };
        graphScene->addItem(item);
        nodeItems.insert(node.nodeId, item);
        if (initialPosition)
            item->setPos(*initialPosition);
    }

    const InsightTheme& theme = InsightVisualStyle::theme();
    QPen border(theme.graph.nodeBorder, 1.4);
    if (item->isSelected())
        border.setColor(theme.graph.nodeSelectedBorder);
    item->setPen(border);
    item->setBrush(nodeFill(node));
    item->setToolTip(nodeTooltip(node));

    QList<QGraphicsSimpleTextItem*> texts;
    for (QGraphicsItem* child : item->childItems()) {
        if (auto* text = dynamic_cast<QGraphicsSimpleTextItem*>(child))
            texts.append(text);
    }
    while (texts.size() < 2)
        texts.append(new QGraphicsSimpleTextItem(item));
    texts.at(0)->setText(node.displayName);
    texts.at(0)->setBrush(theme.textPrimary);
    QFont titleFont = InsightVisualStyle::titleFont(texts.at(0)->font());
    texts.at(0)->setFont(titleFont);
    texts.at(0)->setPos(10.0, 8.0);
    texts.at(1)->setText(elidedDetail(node.detail));
    texts.at(1)->setBrush(theme.textSecondary);
    texts.at(1)->setPos(10.0, 36.0);
}

void InsightCanvas::addOrUpdateEdge(const InsightGraphEdge& edge)
{
    if (!graphScene || !edge.isValid())
        return;
    EdgeVisual visual = edgeItems.value(edge.edgeId);
    if (!visual.path) {
        visual.path = new QGraphicsPathItem;
        visual.path->setZValue(1.0);
        graphScene->addItem(visual.path);
    }
    if (!edge.displayName.trimmed().isEmpty() && !visual.label) {
        visual.label = new QGraphicsSimpleTextItem;
        visual.label->setZValue(3.0);
        graphScene->addItem(visual.label);
    }
    const InsightTheme& theme = InsightVisualStyle::theme();
    QPen pen(theme.graph.edge, qBound(1.0, edge.weight / 25.0, 3.5));
    pen.setCosmetic(true);
    visual.path->setPen(pen);
    if (visual.label) {
        visual.label->setText(edge.displayName);
        visual.label->setBrush(theme.textSecondary);
    }
    edgeItems.insert(edge.edgeId, visual);
    updateEdgePath(edge.edgeId);
}

void InsightCanvas::updateAllEdgePaths()
{
    const QStringList ids = edgeItems.keys();
    for (const QString& id : ids)
        updateEdgePath(id);
}

void InsightCanvas::updateEdgePath(const QString& edgeId)
{
    const InsightGraphEdge edge = currentSnapshot.edges.value(edgeId);
    const EdgeVisual visual = edgeItems.value(edgeId);
    QGraphicsRectItem* from = nodeItems.value(edge.fromNodeId, nullptr);
    QGraphicsRectItem* to = nodeItems.value(edge.toNodeId, nullptr);
    if (!visual.path || !from || !to)
        return;
    const QPointF start = from->sceneBoundingRect().center();
    const QPointF end = to->sceneBoundingRect().center();
    QPainterPath path(start);
    if (currentSnapshot.layoutHint == QStringLiteral("state")) {
        const qreal bend = qMax(36.0, std::abs(end.x() - start.x()) * 0.35);
        path.cubicTo(
            QPointF(start.x() + bend, start.y()),
            QPointF(end.x() - bend, end.y()),
            end);
    } else {
        const qreal middleX = (start.x() + end.x()) * 0.5;
        path.lineTo(QPointF(middleX, start.y()));
        path.lineTo(QPointF(middleX, end.y()));
        path.lineTo(end);
    }
    visual.path->setPath(path);
    if (visual.label) {
        const QPointF midpoint = path.pointAtPercent(0.5);
        visual.label->setPos(
            midpoint + QPointF(4.0, -visual.label->boundingRect().height() - 2.0));
    }
}

void InsightCanvas::updateSceneRect()
{
    if (!graphScene)
        return;
    QRectF bounds = graphScene->itemsBoundingRect();
    if (bounds.isEmpty())
        bounds = QRectF(0.0, 0.0, 320.0, 220.0);
    graphScene->setSceneRect(bounds.adjusted(-56.0, -56.0, 56.0, 56.0));
}

void InsightCanvas::updateMinimap()
{
    if (minimapView && minimapView->isVisible())
        minimapView->fitScene();
}

void InsightCanvas::applyTheme()
{
    if (graphView)
        graphView->applyInsightGraphStyle();
    if (minimapView)
        minimapView->applyInsightGraphStyle();
    const QList<InsightGraphNode> nodes = currentSnapshot.nodes.values();
    for (const InsightGraphNode& node : nodes)
        addOrUpdateNode(node);
    const QList<InsightGraphEdge> edges = currentSnapshot.edges.values();
    for (const InsightGraphEdge& edge : edges)
        addOrUpdateEdge(edge);
    update();
}

void InsightCanvas::applySearch()
{
    const QString needle = searchText.trimmed();
    for (auto it = nodeItems.cbegin(); it != nodeItems.cend(); ++it) {
        QGraphicsRectItem* item = it.value();
        if (!item)
            continue;
        const InsightGraphNode node = currentSnapshot.nodes.value(it.key());
        const bool matches = needle.isEmpty()
            || node.displayName.contains(needle, Qt::CaseInsensitive)
            || node.detail.contains(needle, Qt::CaseInsensitive);
        item->setOpacity(matches ? 1.0 : 0.24);
    }
}

QHash<QString, QPointF> InsightCanvas::layoutPositions(
    const InsightGraphSnapshot& snapshotValue) const
{
    QHash<QString, QPointF> result;
    QStringList ids = snapshotValue.nodes.keys();
    std::sort(ids.begin(), ids.end(), [&snapshotValue](const QString& lhs,
                                                       const QString& rhs) {
        const InsightGraphNode& left = snapshotValue.nodes.value(lhs);
        const InsightGraphNode& right = snapshotValue.nodes.value(rhs);
        const int leftDepth = left.attributes.value(QStringLiteral("depth")).toInt();
        const int rightDepth = right.attributes.value(QStringLiteral("depth")).toInt();
        if (leftDepth != rightDepth)
            return leftDepth < rightDepth;
        return left.displayName.compare(right.displayName, Qt::CaseInsensitive) < 0;
    });

    if (snapshotValue.layoutHint == QStringLiteral("kernel")) {
        QHash<int, int> laneIndexes;
        for (const QString& id : ids) {
            const InsightGraphNode& node = snapshotValue.nodes.value(id);
            const int role = node.attributes.value(QStringLiteral("role")).toInt();
            const int lane = role == static_cast<int>(SignalKernelGraphNodeRole::Input)
                ? 0 : role == static_cast<int>(SignalKernelGraphNodeRole::Output) ? 2 : 1;
            const int index = laneIndexes.value(lane);
            laneIndexes.insert(lane, index + 1);
            result.insert(id, QPointF(
                lane * (kNodeWidth + kHorizontalGap),
                index * (kNodeHeight + kVerticalGap)));
        }
        return result;
    }
    if (snapshotValue.layoutHint == QStringLiteral("block")) {
        QHash<int, int> depthRows;
        for (const QString& id : ids) {
            const int depth = snapshotValue.nodes.value(id)
                                  .attributes.value(QStringLiteral("depth")).toInt();
            const int row = depthRows.value(depth);
            depthRows.insert(depth, row + 1);
            result.insert(id, QPointF(
                depth * (kNodeWidth + kHorizontalGap),
                row * (kNodeHeight + kVerticalGap)));
        }
        return result;
    }
    if (snapshotValue.layoutHint == QStringLiteral("state")) {
        const int count = qMax(1, ids.size());
        const qreal radius = qMax(170.0, count * 26.0);
        for (int index = 0; index < ids.size(); ++index) {
            const qreal angle = (2.0 * std::acos(-1.0) * index / count)
                - std::acos(-1.0) / 2.0;
            result.insert(ids.at(index), QPointF(
                radius * std::cos(angle),
                radius * std::sin(angle)));
        }
        return result;
    }
    if (snapshotValue.layoutHint == QStringLiteral("hotspot")) {
        QString rootId;
        for (const QString& id : ids) {
            if (snapshotValue.nodes.value(id)
                    .attributes.value(QStringLiteral("root")).toBool()) {
                rootId = id;
                break;
            }
        }
        if (!rootId.isEmpty()) {
            result.insert(rootId, QPointF(260.0, 0.0));
            ids.removeAll(rootId);
        }
        std::sort(ids.begin(), ids.end(), [&snapshotValue](const QString& lhs,
                                                          const QString& rhs) {
            const int left = snapshotValue.nodes.value(lhs).heatScore;
            const int right = snapshotValue.nodes.value(rhs).heatScore;
            return left == right ? lhs < rhs : left > right;
        });
    }
    const int columns = qMax(1, static_cast<int>(std::ceil(std::sqrt(ids.size()))));
    for (int index = 0; index < ids.size(); ++index) {
        result.insert(ids.at(index), QPointF(
            (index % columns) * (kNodeWidth + kHorizontalGap),
            (index / columns) * (kNodeHeight + kVerticalGap) + 110.0));
    }
    return result;
}
