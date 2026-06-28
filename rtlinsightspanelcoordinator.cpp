#include "rtlinsightspanelcoordinator.h"

#include "activitylogservice.h"
#include "clockresetdomainservice.h"
#include "fsmgraphservice.h"
#include "moduleblockdiagramservice.h"
#include "modulebriefservice.h"
#include "semanticdiffservice.h"
#include "semanticpanelutils.h"
#include "signaljourneyservice.h"
#include "statetransitiongraphservice.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QGraphicsPathItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsRectItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPoint>
#include <QPolygonF>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <utility>

namespace {

constexpr int kGraphKindRole = Qt::UserRole + 1200;
constexpr int kGraphPrimaryRole = Qt::UserRole + 1201;
constexpr int kGraphSecondaryRole = Qt::UserRole + 1202;
constexpr int kGraphFileRole = Qt::UserRole + 1203;
constexpr int kGraphLineRole = Qt::UserRole + 1204;
constexpr int kGraphColumnRole = Qt::UserRole + 1205;
constexpr int kGraphDrillModuleRole = Qt::UserRole + 1206;
constexpr int kGraphDrillFileRole = Qt::UserRole + 1207;

constexpr qreal kInsightNodeWidth = 190.0;
constexpr qreal kInsightNodeHeight = 62.0;
constexpr qreal kStateNodeWidth = 250.0;
constexpr qreal kStateNodeHeight = 58.0;
constexpr qreal kGraphMinScale = 0.05;
constexpr qreal kGraphMaxScale = 6.0;
constexpr double kPi = 3.14159265358979323846;

struct RtlInsightGraphElement {
    QString kind;
    QString primary;
    QString secondary;
    QString detail;
    RtlInsightCodeLink codeLink;
    QString drillModuleName;
    QString drillFileName;
};

class RtlInsightsGraphView : public QGraphicsView
{
public:
    using QGraphicsView::QGraphicsView;

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override
    {
        QGraphicsView::drawBackground(painter, rect);
        if (!painter)
            return;

        constexpr qreal grid = 28.0;
        const qreal left = std::floor(rect.left() / grid) * grid;
        const qreal top = std::floor(rect.top() / grid) * grid;
        QPen pen(QColor(QStringLiteral("#d9dee8")));
        pen.setWidthF(0.0);
        painter->setPen(pen);
        for (qreal x = left; x <= rect.right(); x += grid)
            painter->drawLine(QLineF(x, rect.top(), x, rect.bottom()));
        for (qreal y = top; y <= rect.bottom(); y += grid)
            painter->drawLine(QLineF(rect.left(), y, rect.right(), y));
    }

    void wheelEvent(QWheelEvent* event) override
    {
        if (!event)
            return;
        const qreal currentScale = transform().m11();
        const qreal factor = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
        const qreal nextScale = currentScale * factor;
        if ((nextScale < kGraphMinScale && factor < 1.0)
            || (nextScale > kGraphMaxScale && factor > 1.0)) {
            event->accept();
            return;
        }
        scale(factor, factor);
        event->accept();
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event && event->button() == Qt::LeftButton
            && items(event->pos()).isEmpty() && scene()) {
            scene()->clearSelection();
        }
        QGraphicsView::mousePressEvent(event);
    }
};

QString graphElidedText(const QString& text, const QFont& font, int width)
{
    return QFontMetrics(font).elidedText(text, Qt::ElideRight, width);
}

QString graphElementTooltip(const RtlInsightGraphElement& element)
{
    QStringList lines;
    const auto appendLine = [&lines](const QString& line) {
        if (!line.trimmed().isEmpty())
            lines.append(line);
    };
    appendLine(element.primary);
    appendLine(element.secondary);
    appendLine(element.detail);
    if (!element.codeLink.fileDisplayName.isEmpty()
        || !element.codeLink.lineDisplayName.isEmpty()) {
        appendLine(QStringLiteral("%1:%2")
                       .arg(element.codeLink.fileDisplayName,
                            element.codeLink.lineDisplayName));
    }
    return lines.join(QLatin1Char('\n'));
}

void applyGraphElementData(QGraphicsItem* item,
                           const RtlInsightGraphElement& element)
{
    if (!item)
        return;
    item->setData(kGraphKindRole, element.kind);
    item->setData(kGraphPrimaryRole, element.primary);
    item->setData(kGraphSecondaryRole, element.secondary);
    item->setData(kGraphFileRole, element.codeLink.fileName);
    item->setData(kGraphLineRole, element.codeLink.line);
    item->setData(kGraphColumnRole, element.codeLink.column);
    item->setData(kGraphDrillModuleRole, element.drillModuleName);
    item->setData(kGraphDrillFileRole, element.drillFileName);
    item->setToolTip(graphElementTooltip(element));
}

class RtlInsightGraphNodeItem : public QGraphicsRectItem
{
public:
    using NavigateHandler = std::function<void(const RtlInsightGraphElement&)>;
    using SelectHandler = std::function<void(const RtlInsightGraphElement&)>;

    RtlInsightGraphNodeItem(const RtlInsightGraphElement& graphElement,
                            const QRectF& rect,
                            const QColor& fill,
                            const QColor& stroke,
                            const QFont& font)
        : QGraphicsRectItem(rect),
          element(graphElement)
    {
        setFlag(QGraphicsItem::ItemIsSelectable, true);
        setAcceptHoverEvents(true);
        setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
        setBrush(fill);
        setPen(QPen(stroke, 1.6));
        setZValue(10);
        applyGraphElementData(this, element);

        QFont titleFont = font;
        titleFont.setBold(true);
        titleFont.setPointSize(qMax(8, titleFont.pointSize() + 1));
        QFont detailFont = font;
        detailFont.setPointSize(qMax(8, detailFont.pointSize() - 1));

        auto* title = new QGraphicsSimpleTextItem(
            graphElidedText(element.primary,
                            titleFont,
                            static_cast<int>(rect.width() - 18)),
            this);
        title->setAcceptedMouseButtons(Qt::NoButton);
        title->setFont(titleFont);
        title->setBrush(QBrush(QColor(QStringLiteral("#0f172a"))));
        title->setPos(rect.left() + 10, rect.top() + 8);

        QString detail = element.secondary;
        if (detail.isEmpty())
            detail = element.detail;
        auto* detailItem = new QGraphicsSimpleTextItem(
            graphElidedText(detail,
                            detailFont,
                            static_cast<int>(rect.width() - 18)),
            this);
        detailItem->setAcceptedMouseButtons(Qt::NoButton);
        detailItem->setFont(detailFont);
        detailItem->setBrush(QBrush(QColor(QStringLiteral("#475569"))));
        detailItem->setPos(rect.left() + 10, rect.top() + 34);
    }

    NavigateHandler navigateHandler;
    SelectHandler selectHandler;

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override
    {
        setSelected(true);
        if (selectHandler)
            selectHandler(element);
        QGraphicsRectItem::mousePressEvent(event);
    }

    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override
    {
        if (event && event->button() == Qt::LeftButton && navigateHandler
            && !element.codeLink.fileName.isEmpty()) {
            navigateHandler(element);
            event->accept();
            return;
        }
        QGraphicsRectItem::mouseDoubleClickEvent(event);
    }

private:
    RtlInsightGraphElement element;
};

class RtlInsightGraphStateNodeItem : public QGraphicsEllipseItem
{
public:
    using NavigateHandler = std::function<void(const RtlInsightGraphElement&)>;
    using SelectHandler = std::function<void(const RtlInsightGraphElement&)>;

    RtlInsightGraphStateNodeItem(const RtlInsightGraphElement& graphElement,
                                 const QRectF& rect,
                                 const QColor& fill,
                                 const QColor& stroke,
                                 const QFont& font,
                                 bool dashed = false)
        : QGraphicsEllipseItem(rect),
          element(graphElement)
    {
        setFlag(QGraphicsItem::ItemIsSelectable, true);
        setAcceptHoverEvents(true);
        setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
        setBrush(fill);
        QPen nodePen(stroke, dashed ? 1.6 : 2.0);
        if (dashed)
            nodePen.setStyle(Qt::DashLine);
        setPen(nodePen);
        setZValue(10);
        applyGraphElementData(this, element);

        QFont titleFont = font;
        titleFont.setBold(true);
        titleFont.setPointSize(qMax(8, titleFont.pointSize() + 1));

        auto* title = new QGraphicsSimpleTextItem(
            element.primary,
            this);
        title->setAcceptedMouseButtons(Qt::NoButton);
        title->setFont(titleFont);
        title->setBrush(QBrush(QColor(QStringLiteral("#111827"))));
        const QRectF titleBounds = title->boundingRect();
        title->setPos(rect.center().x() - titleBounds.width() / 2.0,
                      rect.center().y() - titleBounds.height() / 2.0);
    }

    NavigateHandler navigateHandler;
    SelectHandler selectHandler;

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override
    {
        setSelected(true);
        if (selectHandler)
            selectHandler(element);
        QGraphicsEllipseItem::mousePressEvent(event);
    }

    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override
    {
        if (event && event->button() == Qt::LeftButton && navigateHandler
            && !element.codeLink.fileName.isEmpty()) {
            navigateHandler(element);
            event->accept();
            return;
        }
        QGraphicsEllipseItem::mouseDoubleClickEvent(event);
    }

private:
    RtlInsightGraphElement element;
};

class RtlInsightGraphEdgeItem : public QGraphicsPathItem
{
public:
    using NavigateHandler = std::function<void(const RtlInsightGraphElement&)>;
    using SelectHandler = std::function<void(const RtlInsightGraphElement&)>;

    RtlInsightGraphEdgeItem(const RtlInsightGraphElement& graphElement,
                            const QPainterPath& path,
                            const QPointF& arrowTip,
                            qreal arrowAngle,
                            const QString& label,
                            const QFont& font,
                            const QColor& color,
                            qreal labelOffset = 0.0)
        : QGraphicsPathItem(path),
          element(graphElement)
    {
        setFlag(QGraphicsItem::ItemIsSelectable, true);
        setAcceptHoverEvents(true);
        setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
        setPen(QPen(color, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        setZValue(2);
        applyGraphElementData(this, element);

        constexpr qreal arrowSize = 10.0;
        const QPointF p1 = arrowTip
            - QPointF(std::cos(arrowAngle - kPi / 6.0) * arrowSize,
                      std::sin(arrowAngle - kPi / 6.0) * arrowSize);
        const QPointF p2 = arrowTip
            - QPointF(std::cos(arrowAngle + kPi / 6.0) * arrowSize,
                      std::sin(arrowAngle + kPi / 6.0) * arrowSize);
        QPolygonF arrow;
        arrow << arrowTip << p1 << p2;
        auto* arrowItem = new QGraphicsPolygonItem(arrow, this);
        arrowItem->setAcceptedMouseButtons(Qt::NoButton);
        arrowItem->setPen(QPen(color, 1.0));
        arrowItem->setBrush(QBrush(color));

        if (!label.isEmpty()) {
            QFont labelFont = font;
            labelFont.setPointSize(qMax(8, labelFont.pointSize() - 1));
            auto* labelItem = new QGraphicsTextItem(label, this);
            labelItem->setAcceptedMouseButtons(Qt::NoButton);
            labelItem->setFont(labelFont);
            labelItem->setTextWidth(460.0);
            labelItem->setDefaultTextColor(QColor(QStringLiteral("#334155")));
            const QRectF pathBounds = path.boundingRect();
            const QRectF labelBounds = labelItem->boundingRect();
            auto* labelBackground = new QGraphicsRectItem(
                labelBounds.adjusted(-4.0, -2.0, 4.0, 2.0),
                labelItem);
            labelBackground->setAcceptedMouseButtons(Qt::NoButton);
            labelBackground->setPen(Qt::NoPen);
            labelBackground->setBrush(QColor(248, 250, 252, 220));
            labelBackground->setZValue(-1);
            labelItem->setPos(pathBounds.center().x() - labelBounds.width() / 2.0,
                              pathBounds.center().y() - labelBounds.height() / 2.0
                                  + labelOffset);
        }
    }

    NavigateHandler navigateHandler;
    SelectHandler selectHandler;

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override
    {
        setSelected(true);
        if (selectHandler)
            selectHandler(element);
        QGraphicsPathItem::mousePressEvent(event);
    }

    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override
    {
        if (event && event->button() == Qt::LeftButton && navigateHandler
            && !element.codeLink.fileName.isEmpty()) {
            navigateHandler(element);
            event->accept();
            return;
        }
        QGraphicsPathItem::mouseDoubleClickEvent(event);
    }

private:
    RtlInsightGraphElement element;
};

QPainterPath straightArrowPath(const QPointF& start, const QPointF& end)
{
    QPainterPath path(start);
    path.lineTo(end);
    return path;
}

QPainterPath curvedArrowPath(const QPointF& start,
                             const QPointF& end,
                             qreal bend)
{
    QPainterPath path(start);
    const QPointF mid = (start + end) / 2.0;
    const QPointF normal(-(end.y() - start.y()), end.x() - start.x());
    const qreal length = std::hypot(normal.x(), normal.y());
    const QPointF offset = length > 0.1
        ? QPointF(normal.x() / length * bend, normal.y() / length * bend)
        : QPointF(0, bend);
    path.quadTo(mid + offset, end);
    return path;
}

QRectF insightNodeRectAt(qreal centerX, qreal centerY)
{
    return QRectF(centerX - kInsightNodeWidth / 2.0,
                  centerY - kInsightNodeHeight / 2.0,
                  kInsightNodeWidth,
                  kInsightNodeHeight);
}

qreal stateNodeWidthForText(const QString& text, const QFont& font)
{
    QFont titleFont = font;
    titleFont.setBold(true);
    titleFont.setPointSize(qMax(8, titleFont.pointSize() + 1));
    const qreal textWidth = QFontMetrics(titleFont).horizontalAdvance(text);
    return qMax(kStateNodeWidth, textWidth + 70.0);
}

QRectF stateNodeRectAt(qreal centerX, qreal centerY, qreal width = kStateNodeWidth)
{
    return QRectF(centerX - width / 2.0,
                  centerY - kStateNodeHeight / 2.0,
                  width,
                  kStateNodeHeight);
}

QPointF rectAnchorToward(const QRectF& rect, const QPointF& target)
{
    const QPointF center = rect.center();
    const qreal dx = target.x() - center.x();
    const qreal dy = target.y() - center.y();
    if (std::abs(dx) > std::abs(dy)) {
        return QPointF(dx >= 0 ? rect.right() : rect.left(),
                       center.y() + dy / std::max<qreal>(1.0, std::abs(dx))
                                      * rect.width() / 2.0);
    }
    return QPointF(center.x() + dx / std::max<qreal>(1.0, std::abs(dy))
                              * rect.height() / 2.0,
                   dy >= 0 ? rect.bottom() : rect.top());
}

QPointF ellipseAnchorToward(const QRectF& rect, const QPointF& target)
{
    const QPointF center = rect.center();
    const qreal dx = target.x() - center.x();
    const qreal dy = target.y() - center.y();
    if (std::abs(dx) < 0.1 && std::abs(dy) < 0.1)
        return center;
    const qreal rx = rect.width() / 2.0;
    const qreal ry = rect.height() / 2.0;
    const qreal scale =
        1.0 / std::sqrt((dx * dx) / std::max<qreal>(1.0, rx * rx)
                        + (dy * dy) / std::max<qreal>(1.0, ry * ry));
    return QPointF(center.x() + dx * scale,
                   center.y() + dy * scale);
}

QPointF circularPosition(int index, int count, qreal radius)
{
    if (count <= 1)
        return QPointF(0, 100);
    const qreal angle = -kPi / 2.0 + 2.0 * kPi * index / count;
    return QPointF(std::cos(angle) * radius,
                   120 + std::sin(angle) * radius);
}

QGraphicsItem* graphItemByData(QGraphicsScene* scene,
                               const QString& kind,
                               const QString& primary,
                               const QString& secondary)
{
    if (!scene)
        return nullptr;
    for (QGraphicsItem* item : scene->items()) {
        if (item->data(kGraphKindRole).toString() != kind)
            continue;
        if (item->data(kGraphPrimaryRole).toString() != primary)
            continue;
        if (!secondary.isEmpty()
            && item->data(kGraphSecondaryRole).toString() != secondary) {
            continue;
        }
        return item;
    }
    return nullptr;
}

int indexInList(const QList<QString>& values, const QString& value)
{
    for (int i = 0; i < values.size(); ++i) {
        if (values.at(i) == value)
            return i;
    }
    return -1;
}

QList<QString> orderedFsmStateNames(const FsmGraph& graph)
{
    QList<QString> originalOrder;
    QSet<QString> knownStates;
    for (const FsmStateRow& row : graph.stateRows) {
        if (row.stateDisplayName.isEmpty()
            || knownStates.contains(row.stateDisplayName)) {
            continue;
        }
        knownStates.insert(row.stateDisplayName);
        originalOrder.append(row.stateDisplayName);
    }
    if (originalOrder.size() <= 2)
        return originalOrder;

    QHash<QString, QList<QString>> outgoing;
    QHash<QString, int> incomingCount;
    QSet<QString> seenEdges;
    for (const FsmTransitionRow& row : graph.transitionRows) {
        if (!knownStates.contains(row.fromStateDisplayName)
            || !knownStates.contains(row.toStateDisplayName)
            || row.fromStateDisplayName == row.toStateDisplayName) {
            continue;
        }
        const QString edgeKey =
            row.fromStateDisplayName + QLatin1Char('\n') + row.toStateDisplayName;
        if (seenEdges.contains(edgeKey))
            continue;
        seenEdges.insert(edgeKey);
        outgoing[row.fromStateDisplayName].append(row.toStateDisplayName);
        incomingCount[row.toStateDisplayName] =
            incomingCount.value(row.toStateDisplayName) + 1;
    }

    auto preferredState = [&](const QList<QString>& candidates) {
        QString best;
        int bestOriginalIndex = std::numeric_limits<int>::max();
        for (const QString& candidate : candidates) {
            const int originalIndex = indexInList(originalOrder, candidate);
            if (originalIndex >= 0 && originalIndex < bestOriginalIndex) {
                best = candidate;
                bestOriginalIndex = originalIndex;
            }
        }
        return best;
    };

    QString start;
    for (const QString& state : originalOrder) {
        if (state.contains(QStringLiteral("idle"), Qt::CaseInsensitive)) {
            start = state;
            break;
        }
    }
    if (start.isEmpty()) {
        QList<QString> roots;
        for (const QString& state : originalOrder) {
            if (incomingCount.value(state) == 0)
                roots.append(state);
        }
        start = preferredState(roots);
    }
    if (start.isEmpty())
        start = originalOrder.first();

    QList<QString> ordered;
    QSet<QString> visited;
    QString current = start;
    while (ordered.size() < originalOrder.size()) {
        if (!current.isEmpty() && !visited.contains(current)) {
            ordered.append(current);
            visited.insert(current);
        }

        QList<QString> nextCandidates;
        for (const QString& target : outgoing.value(current)) {
            if (!visited.contains(target))
                nextCandidates.append(target);
        }
        QString next = preferredState(nextCandidates);
        if (next.isEmpty()) {
            QList<QString> rootCandidates;
            for (const QString& state : originalOrder) {
                if (!visited.contains(state) && incomingCount.value(state) == 0)
                    rootCandidates.append(state);
            }
            next = preferredState(rootCandidates);
        }
        if (next.isEmpty()) {
            for (const QString& state : originalOrder) {
                if (!visited.contains(state)) {
                    next = state;
                    break;
                }
            }
        }
        if (next.isEmpty())
            break;
        current = next;
    }
    return ordered;
}

struct FsmGraphLayout {
    QList<QString> orderedStates;
    QHash<QString, QRectF> stateRects;
    QHash<QString, int> stateIndexes;
    QHash<QString, FsmStateRow> stateRowsByName;
    QRectF bounds;
};

FsmGraphLayout buildFsmGraphLayout(const FsmGraph& graph,
                                   const QPointF& origin,
                                   const QFont& font)
{
    FsmGraphLayout layout;
    layout.orderedStates = orderedFsmStateNames(graph);
    QSet<QString> knownStates;
    QHash<QString, qreal> stateWidths;
    qreal maxStateWidth = kStateNodeWidth;
    for (const FsmStateRow& row : graph.stateRows) {
        layout.stateRowsByName.insert(row.stateDisplayName, row);
        knownStates.insert(row.stateDisplayName);
        const qreal width = stateNodeWidthForText(row.stateDisplayName, font);
        stateWidths.insert(row.stateDisplayName, width);
        maxStateWidth = qMax(maxStateWidth, width);
    }

    const int stateCount = layout.orderedStates.size();
    if (stateCount <= 0)
        return layout;

    auto uniqueOutgoingTargets = [&](const QString& source) {
        QList<QString> targets;
        QSet<QString> seen;
        for (const FsmTransitionRow& row : graph.transitionRows) {
            if (row.fromStateDisplayName != source
                || row.toStateDisplayName == source
                || !knownStates.contains(row.toStateDisplayName)
                || seen.contains(row.toStateDisplayName)) {
                continue;
            }
            seen.insert(row.toStateDisplayName);
            targets.append(row.toStateDisplayName);
        }
        return targets;
    };

    QString hubState;
    QList<QString> hubTargets;
    for (const QString& stateName : layout.orderedStates) {
        const QList<QString> targets = uniqueOutgoingTargets(stateName);
        const bool betterCount = targets.size() > hubTargets.size();
        const bool betterIdleTie =
            targets.size() == hubTargets.size()
            && stateName.contains(QStringLiteral("idle"), Qt::CaseInsensitive)
            && !hubState.contains(QStringLiteral("idle"), Qt::CaseInsensitive);
        if (betterCount || betterIdleTie) {
            hubState = stateName;
            hubTargets = targets;
        }
    }

    auto placeState = [&](const QString& stateName, const QPointF& center) {
        const QRectF rect = stateNodeRectAt(
            center.x(),
            center.y(),
            stateWidths.value(stateName, kStateNodeWidth));
        layout.stateRects.insert(stateName, rect);
        layout.bounds = layout.bounds.isNull() ? rect : layout.bounds.united(rect);
    };

    if (stateCount == 1) {
        placeState(layout.orderedStates.constFirst(), origin);
    } else if (!hubState.isEmpty() && !hubTargets.isEmpty()) {
        placeState(hubState, origin);
        QSet<QString> placed;
        placed.insert(hubState);

        const qreal horizontalGap = maxStateWidth + 210.0;
        const qreal verticalGap = kStateNodeHeight + 185.0;
        if (hubTargets.size() <= 4) {
            const QList<QPointF> offsets = {
                QPointF(0.0, -verticalGap),
                QPointF(horizontalGap, 0.0),
                QPointF(0.0, verticalGap),
                QPointF(-horizontalGap, 0.0),
            };
            for (int i = 0; i < hubTargets.size(); ++i) {
                placeState(hubTargets.at(i), origin + offsets.at(i));
                placed.insert(hubTargets.at(i));
            }
        } else {
            const qreal radius =
                qMax<qreal>(360.0,
                            hubTargets.size() * (maxStateWidth + 120.0)
                                / (2.0 * kPi));
            for (int i = 0; i < hubTargets.size(); ++i) {
                placeState(hubTargets.at(i),
                           origin + circularPosition(i, hubTargets.size(), radius));
                placed.insert(hubTargets.at(i));
            }
        }

        QList<QString> remainingStates;
        for (const QString& stateName : layout.orderedStates) {
            if (!placed.contains(stateName))
                remainingStates.append(stateName);
        }
        if (!remainingStates.isEmpty()) {
            const int columns = qMin(4, qMax(1, remainingStates.size()));
            const qreal startX = origin.x()
                - (columns - 1) * horizontalGap / 2.0;
            const qreal startY = origin.y()
                + (hubTargets.size() <= 4 ? verticalGap * 2.2
                                           : verticalGap * 2.8);
            for (int i = 0; i < remainingStates.size(); ++i) {
                const int row = i / columns;
                const int column = i % columns;
                placeState(remainingStates.at(i),
                           QPointF(startX + column * horizontalGap,
                                   startY + row * verticalGap));
            }
        }
    } else {
        const qreal radius =
            qMax<qreal>(360.0,
                        stateCount * (maxStateWidth + 120.0)
                            / (2.0 * kPi));
        for (int i = 0; i < stateCount; ++i) {
            placeState(layout.orderedStates.at(i),
                       origin + circularPosition(i, stateCount, radius));
        }
    }

    for (int i = 0; i < layout.orderedStates.size(); ++i) {
        layout.stateIndexes.insert(layout.orderedStates.at(i), i);
    }
    return layout;
}

QString fsmTransitionLabel(const QString& condition)
{
    if (condition.isEmpty()
        || condition == QStringLiteral("unconditional")) {
        return QString();
    }
    return condition;
}

RtlInsightCodeLink fsmStateTransitionCodeLink(const FsmGraph& graph,
                                              const QString& stateName,
                                              const RtlInsightCodeLink& fallback)
{
    for (const FsmTransitionRow& row : graph.transitionRows) {
        if (row.fromStateDisplayName == stateName
            && row.toStateDisplayName != stateName
            && !row.codeLink.fileName.isEmpty()) {
            return row.codeLink;
        }
    }
    for (const FsmTransitionRow& row : graph.transitionRows) {
        if (row.fromStateDisplayName == stateName
            && !row.codeLink.fileName.isEmpty()) {
            return row.codeLink;
        }
    }
    for (const FsmTransitionRow& row : graph.transitionRows) {
        if (row.toStateDisplayName == stateName
            && !row.codeLink.fileName.isEmpty()) {
            return row.codeLink;
        }
    }
    return fallback;
}

QPainterPath fsmTransitionPath(const QRectF& fromRect,
                               const QRectF& toRect,
                               QPointF* startOut,
                               QPointF* endOut,
                               int lane = 0)
{
    const QPointF start = ellipseAnchorToward(fromRect, toRect.center());
    const QPointF end = ellipseAnchorToward(toRect, fromRect.center());
    if (startOut)
        *startOut = start;
    if (endOut)
        *endOut = end;

    QPainterPath path(start);
    const bool sameRow = std::abs(fromRect.center().y() - toRect.center().y()) < 1.0;
    const bool sameColumn = std::abs(fromRect.center().x() - toRect.center().x()) < 1.0;
    if (sameColumn || sameRow) {
        if (lane == 0)
            return straightArrowPath(start, end);
        return curvedArrowPath(start, end, lane * 42.0);
    }
    const qreal laneOffset = lane * 34.0;
    const qreal direction = fromRect.center().x() <= toRect.center().x()
        ? 1.0
        : -1.0;
    path.cubicTo(start + QPointF(105.0 * direction, laneOffset),
                 end - QPointF(105.0 * direction, -laneOffset),
                 end);
    return path;
}

QRectF expandedToMinimum(const QRectF& rect, qreal minWidth, qreal minHeight)
{
    QRectF result = rect;
    if (result.width() < minWidth) {
        const qreal extra = (minWidth - result.width()) / 2.0;
        result.adjust(-extra, 0, extra, 0);
    }
    if (result.height() < minHeight) {
        const qreal extra = (minHeight - result.height()) / 2.0;
        result.adjust(0, -extra, 0, extra);
    }
    return result;
}

QRectF renderFsmStateMachineGraph(
    QGraphicsScene* scene,
    const FsmGraph& graph,
    const QString& title,
    const QPointF& origin,
    const QFont& font,
    const RtlInsightGraphNodeItem::NavigateHandler& navigate,
    const RtlInsightGraphNodeItem::SelectHandler& select)
{
    if (!scene)
        return {};

    FsmGraphLayout layout = buildFsmGraphLayout(graph, origin, font);
    QRectF graphBounds = layout.bounds;
    if (layout.orderedStates.isEmpty())
        return graphBounds;

    QFont titleFont = font;
    titleFont.setBold(true);
    titleFont.setPointSize(qMax(10, titleFont.pointSize() + 1));
    auto* titleItem = scene->addSimpleText(title, titleFont);
    titleItem->setBrush(QBrush(QColor(QStringLiteral("#111827"))));
    titleItem->setPos(layout.bounds.left(), layout.bounds.top() - 86.0);
    graphBounds = graphBounds.united(titleItem->sceneBoundingRect());

    QFont captionFont = font;
    captionFont.setPointSize(qMax(8, captionFont.pointSize() - 1));
    const QString caption = graph.nextStateSignalDisplayName.isEmpty()
        ? graph.stateRegisterDisplayName
        : QStringLiteral("%1  ->  %2")
              .arg(graph.stateRegisterDisplayName,
                   graph.nextStateSignalDisplayName);
    auto* captionItem = scene->addSimpleText(caption, captionFont);
    captionItem->setBrush(QBrush(QColor(QStringLiteral("#475569"))));
    captionItem->setPos(layout.bounds.left(), layout.bounds.top() - 56.0);
    graphBounds = graphBounds.united(captionItem->sceneBoundingRect());

    auto addStateNode = [&](const FsmStateRow& row,
                            const QRectF& rect,
                            bool highlighted,
                            bool dashed) {
        RtlInsightGraphElement state;
        state.kind = QStringLiteral("state");
        state.primary = row.stateDisplayName;
        state.detail = row.detailDisplayName;
        state.codeLink =
            fsmStateTransitionCodeLink(graph, row.stateDisplayName, row.codeLink);
        auto* item = new RtlInsightGraphStateNodeItem(
            state,
            rect,
            highlighted ? QColor(QStringLiteral("#fff7d6"))
                        : QColor(QStringLiteral("#ffffff")),
            QColor(QStringLiteral("#111827")),
            font,
            dashed);
        item->navigateHandler = navigate;
        item->selectHandler = select;
        scene->addItem(item);
        return item;
    };

    for (int i = 0; i < layout.orderedStates.size(); ++i) {
        const QString stateName = layout.orderedStates.at(i);
        const FsmStateRow row = layout.stateRowsByName.value(stateName);
        const bool highlighted =
            i == 0 || stateName.contains(QStringLiteral("idle"),
                                         Qt::CaseInsensitive);
        addStateNode(row, layout.stateRects.value(stateName), highlighted, false);
    }

    auto addTransition = [&](const FsmTransitionRow& row,
                             const QRectF& fromRect,
                             const QRectF& toRect,
                             int lane) {
        QPointF start;
        QPointF end;
        QPainterPath path;
        qreal angle = 0.0;
        if (row.fromStateDisplayName == row.toStateDisplayName) {
            const qreal loopShift = lane * 18.0;
            start = QPointF(fromRect.center().x() + fromRect.width() * 0.24,
                            fromRect.top() + 6.0 + loopShift);
            end = QPointF(fromRect.center().x() - fromRect.width() * 0.24,
                          fromRect.top() + 6.0 + loopShift);
            path.moveTo(start);
            path.cubicTo(start + QPointF(48.0, -96.0 - lane * 22.0),
                         end + QPointF(-48.0, -96.0 - lane * 22.0),
                         end);
            angle = kPi;
        } else {
            path = fsmTransitionPath(fromRect, toRect, &start, &end, lane);
            angle = std::atan2(end.y() - start.y(), end.x() - start.x());
        }

        RtlInsightGraphElement transition;
        transition.kind = QStringLiteral("transition");
        transition.primary = row.fromStateDisplayName;
        transition.secondary = row.toStateDisplayName;
        transition.detail = row.conditionDisplayName;
        transition.codeLink = row.codeLink;
        auto* item = new RtlInsightGraphEdgeItem(
            transition,
            path,
            end,
            angle,
            fsmTransitionLabel(row.conditionDisplayName),
            font,
            QColor(QStringLiteral("#111827")),
            lane * 26.0);
        item->navigateHandler = navigate;
        item->selectHandler = select;
        scene->addItem(item);
        graphBounds = graphBounds.united(item->sceneBoundingRect());
    };

    QHash<QString, int> edgeLaneCounts;
    for (const FsmTransitionRow& row : graph.transitionRows) {
        if (!layout.stateRects.contains(row.fromStateDisplayName)
            || !layout.stateRects.contains(row.toStateDisplayName)) {
            continue;
        }
        const QRectF fromRect = layout.stateRects.value(row.fromStateDisplayName);
        const QRectF toRect = layout.stateRects.value(row.toStateDisplayName);
        const QString edgeKey =
            row.fromStateDisplayName + QLatin1Char('\n') + row.toStateDisplayName;
        const int ordinal = edgeLaneCounts.value(edgeKey);
        edgeLaneCounts.insert(edgeKey, ordinal + 1);
        const int lane = ordinal == 0 ? 0 : ((ordinal + 1) / 2)
            * (ordinal % 2 == 0 ? -1 : 1);
        addTransition(row, fromRect, toRect, lane);
    }

    return graphBounds.adjusted(-90.0, -110.0, 90.0, 90.0);
}

QTreeWidgetItem* createGroupItem(QTreeWidget* tree,
                                 const QString& title,
                                 int count)
{
    auto* item = new QTreeWidgetItem(tree);
    item->setText(0, SemanticPanelUtils::countLabel(title, count));
    return item;
}

QTreeWidgetItem* createChildItem(QTreeWidgetItem* parent,
                                 const QString& section,
                                 const QString& name,
                                 const QString& detail,
                                 const QString& fileName,
                                 int line,
                                 int column,
                                 const QString& fileDisplayName = QString(),
                                 const QString& lineDisplayName = QString())
{
    auto* item = new QTreeWidgetItem(parent);
    item->setText(0, section);
    item->setText(1, name);
    item->setText(2, detail);
    item->setText(3, fileDisplayName.isEmpty()
                         ? QFileInfo(fileName).fileName()
                         : fileDisplayName);
    item->setText(4, lineDisplayName.isEmpty()
                         ? (line > 0 ? QString::number(line) : QString())
                         : lineDisplayName);
    item->setToolTip(1, name);
    item->setToolTip(2, detail);
    item->setToolTip(3, fileName);
    item->setData(0, Qt::UserRole, fileName);
    item->setData(0, Qt::UserRole + 1, line);
    item->setData(0, Qt::UserRole + 2, column);
    return item;
}

void appendSymbolGroup(QTreeWidget* tree,
                       const QString& title,
                       const QList<ModuleBriefSymbolRow>& rows)
{
    QTreeWidgetItem* group = createGroupItem(tree, title, rows.size());
    for (const ModuleBriefSymbolRow& row : rows) {
        createChildItem(group,
                        row.sectionDisplayName,
                        row.symbolDisplayName,
                        row.detailDisplayName,
                        row.codeLink.fileName,
                        row.codeLink.line,
                        row.codeLink.column,
                        row.codeLink.fileDisplayName,
                        row.codeLink.lineDisplayName);
    }
}

void appendDiagnostics(QTreeWidget* tree,
                       const QList<ModuleBriefDiagnosticRow>& diagnostics)
{
    QTreeWidgetItem* group = createGroupItem(tree,
                                            QStringLiteral("Diagnostics"),
                                            diagnostics.size());
    for (const ModuleBriefDiagnosticRow& row : diagnostics) {
        const SemanticDiagnostic& diagnostic = row.diagnostic;
        QTreeWidgetItem* diagnosticItem =
            createChildItem(group,
                            row.severityDisplayName,
                            diagnostic.message,
                            row.detailDisplayName,
                            row.codeLink.fileName,
                            row.codeLink.line,
                            row.codeLink.column,
                            row.codeLink.fileDisplayName,
                            row.codeLink.lineDisplayName);
        createChildItem(diagnosticItem,
                        QStringLiteral("Source Role"),
                        row.sourceRoleDisplayName,
                        row.severityDisplayName,
                        row.codeLink.fileName,
                        row.codeLink.line,
                        row.codeLink.column,
                        row.codeLink.fileDisplayName,
                        row.codeLink.lineDisplayName);
    }
}

void appendContextRows(QTreeWidget* tree,
                       const QList<ModuleBriefContextRow>& rows)
{
    QTreeWidgetItem* group = createGroupItem(tree,
                                            QStringLiteral("Context"),
                                            rows.size());
    for (const ModuleBriefContextRow& row : rows) {
        QTreeWidgetItem* context =
            createChildItem(group,
                            row.sectionDisplayName,
                            row.symbolDisplayName,
                            row.detailDisplayName,
                            row.codeLink.fileName,
                            row.codeLink.line,
                            row.codeLink.column,
                            row.codeLink.fileDisplayName,
                            row.codeLink.lineDisplayName);
        createChildItem(context,
                        QStringLiteral("Kind"),
                        row.contextKindDisplayName,
                        row.sectionDisplayName,
                        row.codeLink.fileName,
                        row.codeLink.line,
                        row.codeLink.column,
                        row.codeLink.fileDisplayName,
                        row.codeLink.lineDisplayName);
        createChildItem(context,
                        QStringLiteral("Type"),
                        row.symbolTypeDisplayName,
                        row.detailDisplayName,
                        row.codeLink.fileName,
                        row.codeLink.line,
                        row.codeLink.column,
                        row.codeLink.fileDisplayName,
                        row.codeLink.lineDisplayName);
        createChildItem(context,
                        QStringLiteral("Source Role"),
                        row.sourceRoleDisplayName,
                        row.sectionDisplayName,
                        row.codeLink.fileName,
                        row.codeLink.line,
                        row.codeLink.column,
                        row.codeLink.fileDisplayName,
                        row.codeLink.lineDisplayName);
    }
}

void appendRelationshipSummary(
    QTreeWidget* tree,
    const ModuleBriefRelationshipSummary& summary,
    const QList<ModuleBriefRelationshipEvidenceRow>& evidenceRows)
{
    QTreeWidgetItem* group = createGroupItem(tree,
                                            QStringLiteral("Relationships"),
                                            summary.totalCount);
    for (const ModuleBriefRelationshipEvidenceRow& row : evidenceRows) {
        QTreeWidgetItem* relationship =
            createChildItem(group,
                            row.directionDisplayName,
                            row.peerDisplayName,
                            row.detailDisplayName,
                            row.peerCodeLink.fileName,
                            row.peerCodeLink.line,
                            row.peerCodeLink.column,
                            row.peerCodeLink.fileDisplayName,
                            row.peerCodeLink.lineDisplayName);
        createChildItem(relationship,
                        QStringLiteral("From"),
                        row.fromSymbolDisplayName,
                        row.typeDisplayName,
                        row.fromCodeLink.fileName,
                        row.fromCodeLink.line,
                        row.fromCodeLink.column,
                        row.fromCodeLink.fileDisplayName,
                        row.fromCodeLink.lineDisplayName);
        createChildItem(relationship,
                        QStringLiteral("To"),
                        row.toSymbolDisplayName,
                        row.typeDisplayName,
                        row.toCodeLink.fileName,
                        row.toCodeLink.line,
                        row.toCodeLink.column,
                        row.toCodeLink.fileDisplayName,
                        row.toCodeLink.lineDisplayName);
    }
    for (const ModuleBriefRelationshipRow& row : summary.rows) {
        createChildItem(group,
                        row.directionDisplayName,
                        row.typeDisplayName,
                        row.detailDisplayName,
                        QString(),
                        0,
                        0);
    }
}

void appendClockResetDomains(QTreeWidget* tree,
                             const ClockResetDomainReport& report)
{
    QTreeWidgetItem* clocks = createGroupItem(tree,
                                             report.clockGroupDisplayName.isEmpty()
                                                 ? QStringLiteral("Clock Domains")
                                                 : report.clockGroupDisplayName,
                                             report.clockRelationshipCount);
    for (const ClockResetDomainEntry& domain : report.clockDomains) {
        QTreeWidgetItem* signal = createChildItem(clocks,
                                                  domain.sectionDisplayName.isEmpty()
                                                      ? QStringLiteral("Clock")
                                                      : domain.sectionDisplayName,
                                                  domain.domainSignalDisplayName,
                                                  domain.detailDisplayName.isEmpty()
                                                      ? QStringLiteral("drives %1 modules")
                                                            .arg(domain.modules.size())
                                                      : domain.detailDisplayName,
                                                  domain.domainSignalCodeLink.fileName,
                                                  domain.domainSignalCodeLink.line,
                                                  domain.domainSignalCodeLink.column,
                                                  domain.domainSignalCodeLink.fileDisplayName,
                                                  domain.domainSignalCodeLink.lineDisplayName);
        for (const ClockResetDomainMember& member : domain.modules) {
            QTreeWidgetItem* module =
                createChildItem(signal,
                                member.sectionDisplayName.isEmpty()
                                    ? QStringLiteral("Module")
                                    : member.sectionDisplayName,
                                member.moduleDisplayName.isEmpty()
                                    ? QStringLiteral("<unnamed>")
                                    : member.moduleDisplayName,
                                member.detailDisplayName.isEmpty()
                                    ? QStringLiteral("clocked")
                                    : member.detailDisplayName,
                                member.moduleCodeLink.fileName,
                                member.moduleCodeLink.line,
                                member.moduleCodeLink.column,
                                member.moduleCodeLink.fileDisplayName,
                                member.moduleCodeLink.lineDisplayName);
            createChildItem(module,
                            QStringLiteral("Relationship Type"),
                            member.relationshipTypeDisplayName,
                            member.moduleDisplayName,
                            member.moduleCodeLink.fileName,
                            member.moduleCodeLink.line,
                            member.moduleCodeLink.column,
                            member.moduleCodeLink.fileDisplayName,
                            member.moduleCodeLink.lineDisplayName);
            createChildItem(module,
                            QStringLiteral("Source Role"),
                            member.sourceRoleDisplayName,
                            member.moduleDisplayName,
                            member.moduleCodeLink.fileName,
                            member.moduleCodeLink.line,
                            member.moduleCodeLink.column,
                            member.moduleCodeLink.fileDisplayName,
                            member.moduleCodeLink.lineDisplayName);
            createChildItem(module,
                            QStringLiteral("Domain Signal"),
                            domain.domainSignalDisplayName,
                            domain.sectionDisplayName,
                            domain.domainSignalCodeLink.fileName,
                            domain.domainSignalCodeLink.line,
                            domain.domainSignalCodeLink.column,
                            domain.domainSignalCodeLink.fileDisplayName,
                            domain.domainSignalCodeLink.lineDisplayName);
        }
    }

    QTreeWidgetItem* resets = createGroupItem(tree,
                                             report.resetGroupDisplayName.isEmpty()
                                                 ? QStringLiteral("Reset Domains")
                                                 : report.resetGroupDisplayName,
                                             report.resetRelationshipCount);
    for (const ClockResetDomainEntry& domain : report.resetDomains) {
        QTreeWidgetItem* signal = createChildItem(resets,
                                                  domain.sectionDisplayName.isEmpty()
                                                      ? QStringLiteral("Reset")
                                                      : domain.sectionDisplayName,
                                                  domain.domainSignalDisplayName,
                                                  domain.detailDisplayName.isEmpty()
                                                      ? QStringLiteral("resets %1 modules")
                                                            .arg(domain.modules.size())
                                                      : domain.detailDisplayName,
                                                  domain.domainSignalCodeLink.fileName,
                                                  domain.domainSignalCodeLink.line,
                                                  domain.domainSignalCodeLink.column,
                                                  domain.domainSignalCodeLink.fileDisplayName,
                                                  domain.domainSignalCodeLink.lineDisplayName);
        for (const ClockResetDomainMember& member : domain.modules) {
            QTreeWidgetItem* module =
                createChildItem(signal,
                                member.sectionDisplayName.isEmpty()
                                    ? QStringLiteral("Module")
                                    : member.sectionDisplayName,
                                member.moduleDisplayName.isEmpty()
                                    ? QStringLiteral("<unnamed>")
                                    : member.moduleDisplayName,
                                member.detailDisplayName.isEmpty()
                                    ? QStringLiteral("reset")
                                    : member.detailDisplayName,
                                member.moduleCodeLink.fileName,
                                member.moduleCodeLink.line,
                                member.moduleCodeLink.column,
                                member.moduleCodeLink.fileDisplayName,
                                member.moduleCodeLink.lineDisplayName);
            createChildItem(module,
                            QStringLiteral("Relationship Type"),
                            member.relationshipTypeDisplayName,
                            member.moduleDisplayName,
                            member.moduleCodeLink.fileName,
                            member.moduleCodeLink.line,
                            member.moduleCodeLink.column,
                            member.moduleCodeLink.fileDisplayName,
                            member.moduleCodeLink.lineDisplayName);
            createChildItem(module,
                            QStringLiteral("Source Role"),
                            member.sourceRoleDisplayName,
                            member.moduleDisplayName,
                            member.moduleCodeLink.fileName,
                            member.moduleCodeLink.line,
                            member.moduleCodeLink.column,
                            member.moduleCodeLink.fileDisplayName,
                            member.moduleCodeLink.lineDisplayName);
            createChildItem(module,
                            QStringLiteral("Domain Signal"),
                            domain.domainSignalDisplayName,
                            domain.sectionDisplayName,
                            domain.domainSignalCodeLink.fileName,
                            domain.domainSignalCodeLink.line,
                            domain.domainSignalCodeLink.column,
                            domain.domainSignalCodeLink.fileDisplayName,
                            domain.domainSignalCodeLink.lineDisplayName);
        }
    }
}

void appendClockResetEvidenceRows(
    QTreeWidget* tree,
    const QString& groupDisplayName,
    const QList<ClockResetDomainEvidenceRow>& rows)
{
    QTreeWidgetItem* group = createGroupItem(tree,
                                            groupDisplayName,
                                            rows.size());
    for (const ClockResetDomainEvidenceRow& row : rows) {
        QTreeWidgetItem* evidence =
            createChildItem(group,
                            row.sectionDisplayName,
                            row.signalDisplayName,
                            row.detailDisplayName,
                            row.signalCodeLink.fileName,
                            row.signalCodeLink.line,
                            row.signalCodeLink.column,
                            row.signalCodeLink.fileDisplayName,
                            row.signalCodeLink.lineDisplayName);
        createChildItem(evidence,
                        QStringLiteral("Signal"),
                        row.signalDisplayName,
                        row.sectionDisplayName,
                        row.signalCodeLink.fileName,
                        row.signalCodeLink.line,
                        row.signalCodeLink.column,
                        row.signalCodeLink.fileDisplayName,
                        row.signalCodeLink.lineDisplayName);
        createChildItem(evidence,
                        QStringLiteral("Module"),
                        row.moduleDisplayName,
                        row.sectionDisplayName,
                        row.moduleCodeLink.fileName,
                        row.moduleCodeLink.line,
                        row.moduleCodeLink.column,
                        row.moduleCodeLink.fileDisplayName,
                        row.moduleCodeLink.lineDisplayName);
        createChildItem(evidence,
                        QStringLiteral("Relationship Type"),
                        row.relationshipTypeDisplayName,
                        row.sectionDisplayName,
                        row.signalCodeLink.fileName,
                        row.signalCodeLink.line,
                        row.signalCodeLink.column,
                        row.signalCodeLink.fileDisplayName,
                        row.signalCodeLink.lineDisplayName);
        createChildItem(evidence,
                        QStringLiteral("Category"),
                        row.categoryDisplayName,
                        row.sectionDisplayName,
                        row.signalCodeLink.fileName,
                        row.signalCodeLink.line,
                        row.signalCodeLink.column,
                        row.signalCodeLink.fileDisplayName,
                        row.signalCodeLink.lineDisplayName);
        createChildItem(evidence,
                        QStringLiteral("Reason"),
                        row.evidenceReasonDisplayName,
                        row.detailDisplayName,
                        row.signalCodeLink.fileName,
                        row.signalCodeLink.line,
                        row.signalCodeLink.column,
                        row.signalCodeLink.fileDisplayName,
                        row.signalCodeLink.lineDisplayName);
        createChildItem(evidence,
                        QStringLiteral("Source Role"),
                        row.sourceRoleDisplayName,
                        row.sectionDisplayName,
                        row.signalCodeLink.fileName,
                        row.signalCodeLink.line,
                        row.signalCodeLink.column,
                        row.signalCodeLink.fileDisplayName,
                        row.signalCodeLink.lineDisplayName);
    }
}

void appendSignalJourneyItems(QTreeWidgetItem* parent,
                              const QString& section,
                              const QList<SignalJourneyItem>& items)
{
    QTreeWidgetItem* group = new QTreeWidgetItem(parent);
    group->setText(0, SemanticPanelUtils::countLabel(section, items.size()));
    for (const SignalJourneyItem& item : items) {
        QTreeWidgetItem* relationship =
            createChildItem(group,
                            section,
                            item.peerSymbolDisplayName,
                            item.detailDisplayName,
                            item.peerCodeLink.fileName,
                            item.peerCodeLink.line,
                            item.peerCodeLink.column,
                            item.peerCodeLink.fileDisplayName,
                            item.peerCodeLink.lineDisplayName);
        QTreeWidgetItem* fromItem =
            createChildItem(relationship,
                            QStringLiteral("From"),
                            item.fromSymbolDisplayName,
                            item.relationshipTypeDisplayName,
                            item.fromCodeLink.fileName,
                            item.fromCodeLink.line,
                            item.fromCodeLink.column,
                            item.fromCodeLink.fileDisplayName,
                            item.fromCodeLink.lineDisplayName);
        createChildItem(fromItem,
                        QStringLiteral("Type"),
                        item.fromTypeDisplayName,
                        item.fromSymbolDisplayName,
                        item.fromCodeLink.fileName,
                        item.fromCodeLink.line,
                        item.fromCodeLink.column,
                        item.fromCodeLink.fileDisplayName,
                        item.fromCodeLink.lineDisplayName);
        createChildItem(fromItem,
                        QStringLiteral("Source Role"),
                        item.fromSourceRoleDisplayName,
                        item.fromSymbolDisplayName,
                        item.fromCodeLink.fileName,
                        item.fromCodeLink.line,
                        item.fromCodeLink.column,
                        item.fromCodeLink.fileDisplayName,
                        item.fromCodeLink.lineDisplayName);
        QTreeWidgetItem* toItem =
            createChildItem(relationship,
                            QStringLiteral("To"),
                            item.toSymbolDisplayName,
                            item.relationshipTypeDisplayName,
                            item.toCodeLink.fileName,
                            item.toCodeLink.line,
                            item.toCodeLink.column,
                            item.toCodeLink.fileDisplayName,
                            item.toCodeLink.lineDisplayName);
        createChildItem(toItem,
                        QStringLiteral("Type"),
                        item.toTypeDisplayName,
                        item.toSymbolDisplayName,
                        item.toCodeLink.fileName,
                        item.toCodeLink.line,
                        item.toCodeLink.column,
                        item.toCodeLink.fileDisplayName,
                        item.toCodeLink.lineDisplayName);
        createChildItem(toItem,
                        QStringLiteral("Source Role"),
                        item.toSourceRoleDisplayName,
                        item.toSymbolDisplayName,
                        item.toCodeLink.fileName,
                        item.toCodeLink.line,
                        item.toCodeLink.column,
                        item.toCodeLink.fileDisplayName,
                        item.toCodeLink.lineDisplayName);
        createChildItem(relationship,
                        QStringLiteral("Connection"),
                        item.connectionKindDisplayName,
                        item.detailDisplayName,
                        item.peerCodeLink.fileName,
                        item.peerCodeLink.line,
                        item.peerCodeLink.column,
                        item.peerCodeLink.fileDisplayName,
                        item.peerCodeLink.lineDisplayName);
        createChildItem(relationship,
                        QStringLiteral("Peer Type"),
                        item.peerTypeDisplayName,
                        item.relationshipTypeDisplayName,
                        item.peerCodeLink.fileName,
                        item.peerCodeLink.line,
                        item.peerCodeLink.column,
                        item.peerCodeLink.fileDisplayName,
                        item.peerCodeLink.lineDisplayName);
        if (!item.interfaceBaseDisplayName.isEmpty()) {
            createChildItem(relationship,
                            QStringLiteral("Interface"),
                            item.interfaceBaseDisplayName,
                            item.connectionKindDisplayName,
                            item.peerCodeLink.fileName,
                            item.peerCodeLink.line,
                            item.peerCodeLink.column,
                            item.peerCodeLink.fileDisplayName,
                            item.peerCodeLink.lineDisplayName);
        }
        createChildItem(relationship,
                        QStringLiteral("Source Role"),
                        item.peerSourceRoleDisplayName,
                        item.peerSymbolDisplayName,
                        item.peerCodeLink.fileName,
                        item.peerCodeLink.line,
                        item.peerCodeLink.column,
                        item.peerCodeLink.fileDisplayName,
                        item.peerCodeLink.lineDisplayName);
    }
}

void appendSignalJourney(QTreeWidget* tree,
                         const QString& fileName,
                         const QString& moduleName,
                         const QString& signalName)
{
    if (signalName.isEmpty())
        return;

    SignalJourneyQuery query;
    query.fileName = fileName;
    query.moduleName = moduleName;
    query.signalName = signalName;
    const SignalJourneyReport report =
        SignalJourneyService::getInstance()->buildSignalJourney(query);
    if (!report.found)
        return;

    const int totalItems = 1
        + report.assignments.size()
        + report.reads.size()
        + report.portConnections.size()
        + report.interfaceConnections.size()
        + report.timingConnections.size();
    QTreeWidgetItem* group = createGroupItem(tree,
                                            QStringLiteral("Signal Journey: %1")
                                                .arg(report.declarationDisplayName),
                                            totalItems);
    QTreeWidgetItem* declaration =
        createChildItem(group,
                        QStringLiteral("Declaration"),
                        report.declarationDisplayName,
                        report.declarationTypeDisplayName,
                        report.declarationCodeLink.fileName,
                        report.declarationCodeLink.line,
                        report.declarationCodeLink.column,
                        report.declarationCodeLink.fileDisplayName,
                        report.declarationCodeLink.lineDisplayName);
    createChildItem(declaration,
                    QStringLiteral("Source Role"),
                    report.declarationSourceRoleDisplayName,
                    report.declarationDisplayName,
                    report.declarationCodeLink.fileName,
                    report.declarationCodeLink.line,
                    report.declarationCodeLink.column,
                    report.declarationCodeLink.fileDisplayName,
                    report.declarationCodeLink.lineDisplayName);
    appendSignalJourneyItems(group, QStringLiteral("Assignments"), report.assignments);
    appendSignalJourneyItems(group, QStringLiteral("Reads"), report.reads);
    appendSignalJourneyItems(group,
                             QStringLiteral("Port Connections"),
                             report.portConnections);
    appendSignalJourneyItems(group,
                             QStringLiteral("Interface Connections"),
                             report.interfaceConnections);
    appendSignalJourneyItems(group,
                             QStringLiteral("Timing Connections"),
                             report.timingConnections);
}

void appendSemanticDiff(QTreeWidget* tree, const SemanticDiffReport& report)
{
    QTreeWidgetItem* symbols = createGroupItem(tree,
                                              report.symbolGroupDisplayName.isEmpty()
                                                  ? QStringLiteral("Semantic Diff Symbols")
                                                  : report.symbolGroupDisplayName,
                                              report.symbolChangeCount);
    for (const SemanticDiffSymbolChange& change : report.symbolChanges) {
        QTreeWidgetItem* symbolChange =
            createChildItem(symbols,
                            QStringLiteral("%1 %2")
                                .arg(change.kindDisplayName,
                                     change.categoryGroupDisplayName),
                            change.symbolDisplayName,
                            change.detailDisplayName,
                            change.codeLink.fileName,
                            change.codeLink.line,
                            change.codeLink.column,
                            change.codeLink.fileDisplayName,
                            change.codeLink.lineDisplayName);
        if (!change.beforeSymbolTypeDisplayName.isEmpty()) {
            createChildItem(symbolChange,
                            QStringLiteral("Before"),
                            change.beforeSymbolTypeDisplayName,
                            change.beforeDataTypeDisplayName.isEmpty()
                                ? change.beforeScopeDisplayName
                                : QStringLiteral("%1, %2")
                                      .arg(change.beforeDataTypeDisplayName,
                                           change.beforeScopeDisplayName),
                            change.beforeCodeLink.fileName,
                            change.beforeCodeLink.line,
                            change.beforeCodeLink.column,
                            change.beforeCodeLink.fileDisplayName,
                            change.beforeCodeLink.lineDisplayName);
        }
        if (!change.afterSymbolTypeDisplayName.isEmpty()) {
            createChildItem(symbolChange,
                            QStringLiteral("After"),
                            change.afterSymbolTypeDisplayName,
                            change.afterDataTypeDisplayName.isEmpty()
                                ? change.afterScopeDisplayName
                                : QStringLiteral("%1, %2")
                                      .arg(change.afterDataTypeDisplayName,
                                           change.afterScopeDisplayName),
                            change.afterCodeLink.fileName,
                            change.afterCodeLink.line,
                            change.afterCodeLink.column,
                            change.afterCodeLink.fileDisplayName,
                            change.afterCodeLink.lineDisplayName);
        }
        createChildItem(symbolChange,
                        QStringLiteral("Source Role"),
                        change.sourceRoleDisplayName,
                        change.categoryDisplayName,
                        change.codeLink.fileName,
                        change.codeLink.line,
                        change.codeLink.column,
                        change.codeLink.fileDisplayName,
                        change.codeLink.lineDisplayName);
    }

    QTreeWidgetItem* relationships =
        createGroupItem(tree,
                        report.relationshipGroupDisplayName.isEmpty()
                            ? QStringLiteral("Semantic Diff Relationships")
                            : report.relationshipGroupDisplayName,
                        report.relationshipChangeCount);
    for (const SemanticDiffRelationshipChange& change : report.relationshipChanges) {
        QTreeWidgetItem* relationship =
            createChildItem(relationships,
                            change.kindDisplayName,
                            change.relationshipTypeDisplayName,
                            change.detailDisplayName,
                            change.codeLink.fileName,
                            change.codeLink.line,
                            change.codeLink.column,
                            change.codeLink.fileDisplayName,
                            change.codeLink.lineDisplayName);
        createChildItem(relationship,
                        QStringLiteral("From"),
                        change.fromSymbolDisplayName,
                        change.relationshipTypeDisplayName,
                        change.fromCodeLink.fileName,
                        change.fromCodeLink.line,
                        change.fromCodeLink.column,
                        change.fromCodeLink.fileDisplayName,
                        change.fromCodeLink.lineDisplayName);
        createChildItem(relationship,
                        QStringLiteral("To"),
                        change.toSymbolDisplayName,
                        change.relationshipTypeDisplayName,
                        change.toCodeLink.fileName,
                        change.toCodeLink.line,
                        change.toCodeLink.column,
                        change.toCodeLink.fileDisplayName,
                        change.toCodeLink.lineDisplayName);
        createChildItem(relationship,
                        QStringLiteral("Source Role"),
                        change.sourceRoleDisplayName,
                        change.relationshipTypeDisplayName,
                        change.codeLink.fileName,
                        change.codeLink.line,
                        change.codeLink.column,
                        change.codeLink.fileDisplayName,
                        change.codeLink.lineDisplayName);
    }

    QTreeWidgetItem* diagnostics =
        createGroupItem(tree,
                        report.diagnosticGroupDisplayName.isEmpty()
                            ? QStringLiteral("Semantic Diff Diagnostics")
                            : report.diagnosticGroupDisplayName,
                        report.diagnosticChangeCount);
    for (const SemanticDiffDiagnosticChange& change : report.diagnosticChanges) {
        const SemanticDiagnostic& diagnostic = change.displayDiagnostic;
        QTreeWidgetItem* diagnosticItem =
            createChildItem(diagnostics,
                            change.kindDisplayName,
                            diagnostic.message,
                            change.detailDisplayName.isEmpty()
                                ? change.severityDisplayName
                                : change.detailDisplayName,
                            change.codeLink.fileName,
                            change.codeLink.line,
                            change.codeLink.column,
                            change.codeLink.fileDisplayName,
                            change.codeLink.lineDisplayName);
        createChildItem(diagnosticItem,
                        QStringLiteral("Source Role"),
                        change.sourceRoleDisplayName,
                        change.severityDisplayName,
                        change.codeLink.fileName,
                        change.codeLink.line,
                        change.codeLink.column,
                        change.codeLink.fileDisplayName,
                        change.codeLink.lineDisplayName);
    }
}

} // namespace

RtlInsightsPanelCoordinator::RtlInsightsPanelCoordinator(QWidget* parent)
{
    auto* panel = new QWidget(parent);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto* actionLayout = new QHBoxLayout;
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(4);
    moduleBriefButton = new QPushButton(QStringLiteral("Module Brief"), panel);
    moduleBriefButton->setObjectName(QStringLiteral("rtlModuleBriefButton"));
    signalJourneyButton = new QPushButton(QStringLiteral("Signal Journey"), panel);
    signalJourneyButton->setObjectName(QStringLiteral("rtlSignalJourneyButton"));
    clockResetButton = new QPushButton(QStringLiteral("Clock/Reset Map"), panel);
    clockResetButton->setObjectName(QStringLiteral("rtlClockResetButton"));
    fsmGraphButton = new QPushButton(QStringLiteral("FSM Graph"), panel);
    fsmGraphButton->setObjectName(QStringLiteral("rtlFsmGraphButton"));
    moduleBlockDiagramButton =
        new QPushButton(QStringLiteral("Module Block Diagram"), panel);
    moduleBlockDiagramButton->setObjectName(
        QStringLiteral("rtlModuleBlockDiagramButton"));
    graphZoomOutButton = new QPushButton(QStringLiteral("-"), panel);
    graphZoomOutButton->setObjectName(QStringLiteral("rtlGraphZoomOutButton"));
    graphZoomOutButton->setFixedWidth(30);
    graphFitButton = new QPushButton(QStringLiteral("Fit"), panel);
    graphFitButton->setObjectName(QStringLiteral("rtlGraphFitButton"));
    graphFitButton->setFixedWidth(42);
    graphZoomInButton = new QPushButton(QStringLiteral("+"), panel);
    graphZoomInButton->setObjectName(QStringLiteral("rtlGraphZoomInButton"));
    graphZoomInButton->setFixedWidth(30);
    actionLayout->addWidget(moduleBriefButton);
    actionLayout->addWidget(signalJourneyButton);
    actionLayout->addWidget(clockResetButton);
    actionLayout->addWidget(fsmGraphButton);
    actionLayout->addWidget(moduleBlockDiagramButton);
    actionLayout->addStretch(1);
    actionLayout->addWidget(graphZoomOutButton);
    actionLayout->addWidget(graphFitButton);
    actionLayout->addWidget(graphZoomInButton);
    layout->addLayout(actionLayout);

    insightsTree = new QTreeWidget(panel);
    insightsTree->setObjectName(QStringLiteral("rtlInsightsTree"));
    insightsTree->setColumnCount(5);
    insightsTree->setHeaderLabels({"Section", "Symbol", "Detail", "File", "Line"});
    insightsTree->setRootIsDecorated(true);
    insightsTree->setAlternatingRowColors(true);
    insightsTree->setSelectionMode(QAbstractItemView::SingleSelection);
    insightsTree->header()->setStretchLastSection(true);
    insightsTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    insightsTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    insightsTree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    insightsTree->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);

    insightsGraphScene = new QGraphicsScene(panel);
    insightsGraphView = new RtlInsightsGraphView(insightsGraphScene, panel);
    insightsGraphView->setObjectName(QStringLiteral("rtlInsightsGraphView"));
    insightsGraphView->setRenderHint(QPainter::Antialiasing, true);
    insightsGraphView->setDragMode(QGraphicsView::ScrollHandDrag);
    insightsGraphView->setFocusPolicy(Qt::StrongFocus);
    insightsGraphView->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    insightsGraphView->setResizeAnchor(QGraphicsView::AnchorViewCenter);
    insightsGraphView->setBackgroundBrush(
        QBrush(QColor(QStringLiteral("#f8fafc"))));

    insightsStack = new QStackedWidget(panel);
    insightsStack->addWidget(insightsTree);
    insightsStack->addWidget(insightsGraphView);
    layout->addWidget(insightsStack, 1);

    insightsDock = new QDockWidget(QStringLiteral("RTL Insights"), parent);
    insightsDock->setObjectName(QStringLiteral("rtlInsightsDock"));
    insightsDock->setWidget(panel);
    insightsDock->setFeatures(QDockWidget::DockWidgetMovable |
                              QDockWidget::DockWidgetFloatable |
                              QDockWidget::DockWidgetClosable);

    QObject::connect(insightsTree, &QTreeWidget::itemDoubleClicked,
                     insightsDock, [this](QTreeWidgetItem* item, int) {
                         if (!item || !navigationHandler)
                             return;
                         const QString fileName = item->data(0, Qt::UserRole).toString();
                         if (fileName.isEmpty())
                             return;
                         const int line = item->data(0, Qt::UserRole + 1).toInt();
                         const int column = item->data(0, Qt::UserRole + 2).toInt();
                         navigationHandler(fileName, line, column);
                     });
    QObject::connect(moduleBriefButton, &QPushButton::clicked,
                     insightsDock, [this]() { showModuleBrief(); });
    QObject::connect(signalJourneyButton, &QPushButton::clicked,
                     insightsDock, [this]() { showSignalJourney(); });
    QObject::connect(clockResetButton, &QPushButton::clicked,
                     insightsDock, [this]() { showClockResetDomainMap(); });
    QObject::connect(fsmGraphButton, &QPushButton::clicked,
                     insightsDock, [this]() { showFsmGraph(); });
    QObject::connect(moduleBlockDiagramButton, &QPushButton::clicked,
                     insightsDock, [this]() { showModuleBlockDiagram(); });
    QObject::connect(graphZoomOutButton, &QPushButton::clicked,
                     insightsDock, [this]() {
                         if (insightsGraphView)
                             insightsGraphView->scale(1.0 / 1.15, 1.0 / 1.15);
                     });
    QObject::connect(graphZoomInButton, &QPushButton::clicked,
                     insightsDock, [this]() {
                         if (insightsGraphView)
                             insightsGraphView->scale(1.15, 1.15);
                     });
    QObject::connect(graphFitButton, &QPushButton::clicked,
                     insightsDock, [this]() {
                         if (insightsGraphView && insightsGraphScene) {
                             const QRectF rect = insightsGraphScene->sceneRect();
                             if (!rect.isEmpty())
                                 insightsGraphView->fitInView(rect,
                                                              Qt::KeepAspectRatio);
                         }
                     });

    renderNoContext();
}

void RtlInsightsPanelCoordinator::setNavigationHandler(
    std::function<void(const QString&, int, int)> handler)
{
    navigationHandler = std::move(handler);
}

void RtlInsightsPanelCoordinator::setStatusMessageHandler(
    std::function<void(const QString&, int)> handler)
{
    statusMessageHandler = std::move(handler);
}

int RtlInsightsPanelCoordinator::graphNodeItemCountForTest() const
{
    if (!insightsGraphScene)
        return 0;
    int count = 0;
    for (QGraphicsItem* item : insightsGraphScene->items()) {
        if (dynamic_cast<RtlInsightGraphNodeItem*>(item)
            || dynamic_cast<RtlInsightGraphStateNodeItem*>(item))
            ++count;
    }
    return count;
}

int RtlInsightsPanelCoordinator::graphEdgeItemCountForTest() const
{
    if (!insightsGraphScene)
        return 0;
    int count = 0;
    for (QGraphicsItem* item : insightsGraphScene->items()) {
        if (dynamic_cast<RtlInsightGraphEdgeItem*>(item))
            ++count;
    }
    return count;
}

QStringList RtlInsightsPanelCoordinator::graphTextItemsForTest() const
{
    QStringList texts;
    if (!insightsGraphScene)
        return texts;
    for (QGraphicsItem* item : insightsGraphScene->items()) {
        if (auto* simpleText = dynamic_cast<QGraphicsSimpleTextItem*>(item)) {
            texts.append(simpleText->text());
        } else if (auto* text = dynamic_cast<QGraphicsTextItem*>(item)) {
            texts.append(text->toPlainText());
        }
    }
    return texts;
}

bool RtlInsightsPanelCoordinator::graphNodeRectsOverlapForTest() const
{
    if (!insightsGraphScene)
        return false;
    QList<QRectF> rects;
    for (QGraphicsItem* item : insightsGraphScene->items()) {
        if (dynamic_cast<RtlInsightGraphNodeItem*>(item)
            || dynamic_cast<RtlInsightGraphStateNodeItem*>(item)) {
            rects.append(item->sceneBoundingRect());
        }
    }
    for (int i = 0; i < rects.size(); ++i) {
        for (int j = i + 1; j < rects.size(); ++j) {
            if (rects.at(i).adjusted(1.0, 1.0, -1.0, -1.0)
                    .intersects(rects.at(j).adjusted(1.0, 1.0, -1.0, -1.0))) {
                return true;
            }
        }
    }
    return false;
}

int RtlInsightsPanelCoordinator::graphElementLineForTest(
    const QString& elementKind,
    const QString& primaryText,
    const QString& secondaryText) const
{
    QGraphicsItem* item = graphItemByData(insightsGraphScene,
                                          elementKind,
                                          primaryText,
                                          secondaryText);
    return item ? item->data(kGraphLineRole).toInt() : -1;
}

bool RtlInsightsPanelCoordinator::triggerGraphNavigationForTest(
    const QString& elementKind,
    const QString& primaryText,
    const QString& secondaryText)
{
    QGraphicsItem* item = graphItemByData(insightsGraphScene,
                                          elementKind,
                                          primaryText,
                                          secondaryText);
    if (!item || !navigationHandler)
        return false;
    item->setSelected(true);
    const QString fileName = item->data(kGraphFileRole).toString();
    if (fileName.isEmpty())
        return false;
    navigationHandler(fileName,
                      item->data(kGraphLineRole).toInt(),
                      item->data(kGraphColumnRole).toInt());
    const QString drillModuleName =
        item->data(kGraphDrillModuleRole).toString();
    if (!drillModuleName.isEmpty()) {
        const QString drillFileName =
            item->data(kGraphDrillFileRole).toString().isEmpty()
                ? fileName
                : item->data(kGraphDrillFileRole).toString();
        showModuleBlockDiagramForModule(drillFileName, drillModuleName);
    }
    return true;
}

void RtlInsightsPanelCoordinator::showTreeSurface()
{
    if (insightsStack && insightsTree)
        insightsStack->setCurrentWidget(insightsTree);
}

void RtlInsightsPanelCoordinator::showGraphSurface()
{
    if (insightsStack && insightsGraphView)
        insightsStack->setCurrentWidget(insightsGraphView);
}

void RtlInsightsPanelCoordinator::renderGraphUnavailable(
    const QString& title,
    const QString& message)
{
    showGraphSurface();
    if (!insightsGraphScene || !insightsGraphView)
        return;
    insightsGraphScene->clear();
    insightsGraphView->resetTransform();

    QFont titleFont = insightsGraphView->font();
    titleFont.setBold(true);
    titleFont.setPointSize(qMax(10, titleFont.pointSize() + 2));
    auto* titleItem = insightsGraphScene->addSimpleText(title, titleFont);
    titleItem->setBrush(QBrush(QColor(QStringLiteral("#0f172a"))));
    titleItem->setPos(-160, -34);

    QFont detailFont = insightsGraphView->font();
    auto* detailItem = insightsGraphScene->addSimpleText(message, detailFont);
    detailItem->setBrush(QBrush(QColor(QStringLiteral("#475569"))));
    detailItem->setPos(-160, 0);

    insightsGraphScene->setSceneRect(-220, -90, 440, 180);
    insightsGraphView->fitInView(insightsGraphScene->sceneRect(),
                                 Qt::KeepAspectRatio);
}

void RtlInsightsPanelCoordinator::renderStateTransitionGraphScene(
    const StateTransitionGraphReport& report)
{
    showGraphSurface();
    if (!insightsGraphScene || !insightsGraphView)
        return;
    insightsGraphScene->clear();
    insightsGraphView->resetTransform();

    if (!report.found) {
        renderGraphUnavailable(
            report.groupDisplayName.isEmpty()
                ? QStringLiteral("State Transition Graph")
                : report.groupDisplayName,
            report.notFoundReasonDisplayName);
        return;
    }

    const QFont font = insightsGraphView->font();
    const auto navigate = [this](const RtlInsightGraphElement& element) {
        const RtlInsightCodeLink& link = element.codeLink;
        if (navigationHandler && !link.fileName.isEmpty())
            navigationHandler(link.fileName, link.line, link.column);
    };
    const auto select = [this](const RtlInsightGraphElement& element) {
        if (statusMessageHandler) {
            statusMessageHandler(QStringLiteral("%1: %2")
                                     .arg(element.kind, element.primary),
                                 1200);
        }
    };

    const QString graphTitle = report.selectedSignalDisplayName.isEmpty()
        ? QStringLiteral("State Transition Graph")
        : QStringLiteral("State Transition Graph: %1")
              .arg(report.selectedSignalDisplayName);
    const QRectF bounds = renderFsmStateMachineGraph(insightsGraphScene,
                                                     report.graph,
                                                     graphTitle,
                                                     QPointF(0.0, 0.0),
                                                     font,
                                                     navigate,
                                                     select);
    insightsGraphScene->setSceneRect(bounds);
    insightsGraphView->fitInView(bounds, Qt::KeepAspectRatio);
}

void RtlInsightsPanelCoordinator::renderFsmGraphScene(
    const FsmGraphReport& report,
    const QString& title)
{
    showGraphSurface();
    if (!insightsGraphScene || !insightsGraphView)
        return;
    insightsGraphScene->clear();
    insightsGraphView->resetTransform();

    if (!report.found || report.graphs.isEmpty()) {
        renderGraphUnavailable(
            title.isEmpty() ? QStringLiteral("FSM Graph") : title,
            report.notFoundReasonDisplayName.isEmpty()
                ? QStringLiteral("No FSM graph")
                : report.notFoundReasonDisplayName);
        return;
    }

    const QFont font = insightsGraphView->font();
    const auto navigate = [this](const RtlInsightGraphElement& element) {
        const RtlInsightCodeLink& link = element.codeLink;
        if (navigationHandler && !link.fileName.isEmpty())
            navigationHandler(link.fileName, link.line, link.column);
    };
    const auto select = [this](const RtlInsightGraphElement& element) {
        if (statusMessageHandler) {
            statusMessageHandler(QStringLiteral("%1: %2")
                                     .arg(element.kind, element.primary),
                                 1200);
        }
    };

    QRectF bounds;
    qreal nextOriginY = 0.0;
    for (int graphIndex = 0; graphIndex < report.graphs.size(); ++graphIndex) {
        const FsmGraph& graph = report.graphs.at(graphIndex);
        const QString graphTitle = report.graphs.size() == 1
            ? (title.isEmpty() ? QStringLiteral("FSM Graph") : title)
            : QStringLiteral("FSM Graph: %1")
                  .arg(graph.nextStateSignalDisplayName.isEmpty()
                           ? graph.stateRegisterDisplayName
                           : graph.nextStateSignalDisplayName);
        const QRectF graphBounds =
            renderFsmStateMachineGraph(insightsGraphScene,
                                       graph,
                                       graphTitle,
                                       QPointF(0.0, nextOriginY),
                                       font,
                                       navigate,
                                       select);
        bounds = bounds.isNull() ? graphBounds : bounds.united(graphBounds);
        nextOriginY = graphBounds.bottom() + 190.0;
    }
    insightsGraphScene->setSceneRect(bounds);
    insightsGraphView->fitInView(bounds, Qt::KeepAspectRatio);
}

void RtlInsightsPanelCoordinator::renderModuleBlockDiagramScene(
    const ModuleBlockDiagramReport& report)
{
    showGraphSurface();
    if (!insightsGraphScene || !insightsGraphView)
        return;
    insightsGraphScene->clear();
    insightsGraphView->resetTransform();

    if (!report.found) {
        renderGraphUnavailable(
            report.groupDisplayName.isEmpty()
                ? QStringLiteral("Module Block Diagram")
                : report.groupDisplayName,
            report.notFoundReasonDisplayName);
        return;
    }

    const QFont font = insightsGraphView->font();
    const auto navigate = [this](const RtlInsightGraphElement& element) {
        const RtlInsightCodeLink& link = element.codeLink;
        if (navigationHandler && !link.fileName.isEmpty())
            navigationHandler(link.fileName, link.line, link.column);
        if (!element.drillModuleName.isEmpty()) {
            const QString drillFileName = element.drillFileName.isEmpty()
                ? link.fileName
                : element.drillFileName;
            const QString drillModuleName = element.drillModuleName;
            QTimer::singleShot(0, insightsDock, [this,
                                                 drillFileName,
                                                 drillModuleName]() {
                showModuleBlockDiagramForModule(drillFileName,
                                                drillModuleName);
            });
        }
    };
    const auto select = [this](const RtlInsightGraphElement& element) {
        if (statusMessageHandler) {
            statusMessageHandler(QStringLiteral("%1: %2")
                                     .arg(element.kind, element.primary),
                                 1200);
        }
    };

    auto addNode = [&](const ModuleBlockDiagramNode& node,
                       const QRectF& rect,
                       bool root) {
        RtlInsightGraphElement element;
        element.kind = QStringLiteral("module");
        element.primary = node.moduleDisplayName;
        element.secondary = node.moduleTypeDisplayName;
        element.detail = node.sourceRoleDisplayName;
        element.codeLink = node.definitionCodeLink;
        element.drillModuleName = node.moduleDisplayName;
        element.drillFileName = node.definitionCodeLink.fileName;
        auto* item = new RtlInsightGraphNodeItem(
            element,
            rect,
            root ? QColor(QStringLiteral("#eef6e6"))
                 : QColor(QStringLiteral("#dff3f8")),
            root ? QColor(QStringLiteral("#111827"))
                 : QColor(QStringLiteral("#0f172a")),
            font);
        item->setZValue(root ? 0.0 : 10.0);
        item->navigateHandler = navigate;
        item->selectHandler = select;
        insightsGraphScene->addItem(item);
        return item;
    };
    auto addEdge = [&](const ModuleBlockDiagramEdge& edge,
                       const QRectF& fromRect,
                       const QRectF& toRect,
                       bool fromRoot) {
        const QPointF start = fromRoot
            ? QPointF(fromRect.left() + 62.0, toRect.center().y())
            : QPointF(fromRect.right(), fromRect.center().y());
        const QPointF end = QPointF(toRect.left(), toRect.center().y());
        QPainterPath path(start);
        if (std::abs(start.y() - end.y()) < 1.0) {
            path.lineTo(end);
        } else {
            const qreal midX = (start.x() + end.x()) / 2.0;
            path.cubicTo(QPointF(midX, start.y()),
                         QPointF(midX, end.y()),
                         end);
        }
        RtlInsightGraphElement element;
        element.kind = QStringLiteral("module-edge");
        element.primary = edge.parentModuleDisplayName;
        element.secondary = edge.childModuleDisplayName;
        element.detail = edge.relationshipDisplayName;
        element.codeLink = edge.childDefinitionCodeLink;
        element.drillModuleName = edge.childModuleDisplayName;
        element.drillFileName = edge.childDefinitionCodeLink.fileName;
        auto* item = new RtlInsightGraphEdgeItem(
            element,
            path,
            end,
            std::atan2(end.y() - start.y(), end.x() - start.x()),
            QString(),
            font,
            QColor(QStringLiteral("#334155")));
        item->navigateHandler = navigate;
        item->selectHandler = select;
        insightsGraphScene->addItem(item);
    };

    QHash<int, ModuleBlockDiagramNode> nodeById;
    QHash<int, QList<ModuleBlockDiagramNode>> childrenByParent;
    int maxDepth = 0;
    for (const ModuleBlockDiagramNode& node : report.nodes) {
        nodeById.insert(node.nodeId, node);
        maxDepth = qMax(maxDepth, node.depth);
        if (node.parentNodeId >= 0)
            childrenByParent[node.parentNodeId].append(node);
    }
    for (auto it = childrenByParent.begin(); it != childrenByParent.end(); ++it) {
        std::sort(it.value().begin(),
                  it.value().end(),
                  [](const ModuleBlockDiagramNode& lhs,
                     const ModuleBlockDiagramNode& rhs) {
                      if (lhs.depth != rhs.depth)
                          return lhs.depth < rhs.depth;
                      return lhs.nodeId < rhs.nodeId;
                  });
    }

    QHash<int, qreal> centerYByNodeId;
    constexpr qreal columnSpacing = 300.0;
    constexpr qreal rowSpacing = 122.0;
    qreal nextLeafY = 0.0;
    std::function<qreal(int)> assignTreeY = [&](int nodeId) -> qreal {
        const QList<ModuleBlockDiagramNode> children =
            childrenByParent.value(nodeId);
        if (children.isEmpty()) {
            const qreal y = nextLeafY;
            nextLeafY += rowSpacing;
            centerYByNodeId.insert(nodeId, y);
            return y;
        }

        qreal firstChildY = 0.0;
        qreal lastChildY = 0.0;
        bool haveChild = false;
        for (const ModuleBlockDiagramNode& child : children) {
            const qreal childY = assignTreeY(child.nodeId);
            if (!haveChild) {
                firstChildY = childY;
                haveChild = true;
            }
            lastChildY = childY;
        }
        const qreal y = (firstChildY + lastChildY) / 2.0;
        centerYByNodeId.insert(nodeId, y);
        return y;
    };
    assignTreeY(report.root.nodeId);

    QHash<int, QRectF> rectByNodeId;
    QRectF childBounds;
    for (const ModuleBlockDiagramNode& node : report.nodes) {
        if (node.nodeId == report.root.nodeId)
            continue;
        const qreal x = (node.depth - 1) * columnSpacing;
        const QRectF rect = insightNodeRectAt(x,
                                              centerYByNodeId.value(node.nodeId));
        rectByNodeId.insert(node.nodeId, rect);
        childBounds = childBounds.isNull() ? rect : childBounds.united(rect);
    }

    QRectF rootRect;
    if (childBounds.isNull()) {
        rootRect = QRectF(-320.0, -180.0, 640.0, 360.0);
    } else {
        rootRect = childBounds.adjusted(-130.0, -120.0, 130.0, 95.0);
        rootRect = expandedToMinimum(rootRect,
                                     qMax<qreal>(680.0,
                                                 maxDepth * columnSpacing + 260.0),
                                     360.0);
    }
    rectByNodeId.insert(report.root.nodeId, rootRect);

    addNode(report.root, rootRect, true);

    for (const ModuleBlockDiagramEdge& edge : report.edges) {
        if (!rectByNodeId.contains(edge.fromNodeId)
            || !rectByNodeId.contains(edge.toNodeId)) {
            continue;
        }
        const bool fromRoot = edge.fromNodeId == report.root.nodeId;
        addEdge(edge,
                rectByNodeId.value(edge.fromNodeId),
                rectByNodeId.value(edge.toNodeId),
                fromRoot);
    }

    for (const ModuleBlockDiagramNode& node : report.nodes) {
        if (!rectByNodeId.contains(node.nodeId))
            continue;
        if (node.nodeId == report.root.nodeId)
            continue;
        addNode(node,
                rectByNodeId.value(node.nodeId),
                false);
    }

    if (report.edgeCount == 0) {
        auto* label = insightsGraphScene->addSimpleText(
            QStringLiteral("No child modules"),
            font);
        label->setBrush(QBrush(QColor(QStringLiteral("#475569"))));
        label->setPos(rootRect.center().x() - 70,
                      rootRect.center().y() - 10);
    }

    const QRectF bounds =
        insightsGraphScene->itemsBoundingRect().adjusted(-80, -80, 80, 80);
    insightsGraphScene->setSceneRect(bounds);
    insightsGraphView->fitInView(bounds, Qt::KeepAspectRatio);
}

void RtlInsightsPanelCoordinator::updateModuleContext(
    const QString& fileName,
    const QString& moduleName,
    const QString& signalName)
{
    currentFileName = fileName;
    currentModuleName = moduleName;
    currentSignalName = signalName;
    renderActionList();
}

void RtlInsightsPanelCoordinator::showModuleInsights(
    const QString& fileName,
    const QString& moduleName,
    const QString& signalName)
{
    updateModuleContext(fileName, moduleName, signalName);
    if (insightsDock) {
        insightsDock->show();
        insightsDock->raise();
    }
}

void RtlInsightsPanelCoordinator::showStateTransitionGraphForSignal(
    const QString& fileName,
    const QString& moduleName,
    const QString& signalName)
{
    updateModuleContext(fileName, moduleName, signalName);
    if (!insightsGraphScene)
        return;

    if (currentFileName.isEmpty() || currentModuleName.isEmpty()) {
        renderNoContext();
        return;
    }

    QElapsedTimer timer;
    timer.start();
    logReportStart(QStringLiteral("State Transition Graph"));
    StateTransitionGraphReport report;
    try {
        StateTransitionGraphQuery query;
        query.fileName = currentFileName;
        query.moduleName = currentModuleName;
        query.symbolName = currentSignalName;
        report = StateTransitionGraphService::getInstance()
            ->buildStateTransitionGraph(query);
    } catch (const std::exception& error) {
        logReportError(QStringLiteral("State Transition Graph"),
                       QString::fromLocal8Bit(error.what()));
        return;
    } catch (...) {
        logReportError(QStringLiteral("State Transition Graph"),
                       QStringLiteral("unknown error"));
        return;
    }

    renderStateTransitionGraphScene(report);
    if (insightsDock) {
        insightsDock->setWindowTitle(
            QStringLiteral("RTL Insights: State Transition Graph %1")
                .arg(signalName.isEmpty() ? moduleName : signalName));
        insightsDock->show();
        insightsDock->raise();
    }
    if (statusMessageHandler) {
        statusMessageHandler(
            report.found
                ? QStringLiteral("Rendered state transition graph for %1")
                      .arg(report.selectedSignalDisplayName)
                : report.notFoundReasonDisplayName,
            1500);
    }
    logReportDone(QStringLiteral("State Transition Graph"),
                  static_cast<int>(timer.elapsed()));
}

void RtlInsightsPanelCoordinator::showModuleBlockDiagramForModule(
    const QString& fileName,
    const QString& moduleName)
{
    updateModuleContext(fileName, moduleName);
    showModuleBlockDiagram();
}

void RtlInsightsPanelCoordinator::showSemanticDiff(
    std::shared_ptr<const SemanticIndexSnapshot> beforeSnapshot,
    std::shared_ptr<const SemanticIndexSnapshot> afterSnapshot,
    const QString& moduleName,
    const QString& beforeFileName,
    const QString& afterFileName)
{
    if (!insightsTree)
        return;
    showTreeSurface();

    QElapsedTimer timer;
    timer.start();
    logReportStart(QStringLiteral("Semantic Diff"));

    const bool hadExpandableItems =
        SemanticPanelUtils::treeHasExpandableItems(insightsTree);
    const QSet<QString> expandedKeys =
        SemanticPanelUtils::collectExpandedKeys(insightsTree);
    insightsTree->clear();

    SemanticDiffQuery query;
    query.beforeSnapshot = std::move(beforeSnapshot);
    query.afterSnapshot = std::move(afterSnapshot);
    query.moduleName = moduleName;
    query.beforeFileName = beforeFileName;
    query.afterFileName = afterFileName;
    SemanticDiffReport report;
    try {
        report = SemanticDiffService::getInstance()->buildSemanticDiff(query);
    } catch (const std::exception& error) {
        logReportError(QStringLiteral("Semantic Diff"),
                       QString::fromLocal8Bit(error.what()));
        return;
    } catch (...) {
        logReportError(QStringLiteral("Semantic Diff"),
                       QStringLiteral("unknown error"));
        return;
    }

    appendSemanticDiff(insightsTree, report);
    SemanticPanelUtils::restoreTreeExpansion(insightsTree,
                                             hadExpandableItems,
                                             expandedKeys);

    const int totalChanges = report.symbolChanges.size()
        + report.relationshipChanges.size()
        + report.diagnosticChanges.size();
    if (insightsDock) {
        const QString title = moduleName.isEmpty()
            ? QStringLiteral("RTL Insights: Semantic Diff")
            : QStringLiteral("RTL Insights: Semantic Diff %1").arg(moduleName);
        insightsDock->setWindowTitle(title);
        insightsDock->show();
        insightsDock->raise();
    }
    if (statusMessageHandler) {
        statusMessageHandler(QStringLiteral("Rendered semantic diff (%1 changes)")
                                 .arg(totalChanges),
                             1500);
    }
    logReportDone(QStringLiteral("Semantic Diff"),
                  static_cast<int>(timer.elapsed()));
}

void RtlInsightsPanelCoordinator::refresh()
{
    showModuleBrief();
}

void RtlInsightsPanelCoordinator::renderNoContext()
{
    if (!insightsTree)
        return;
    showTreeSurface();

    insightsTree->clear();
    createGroupItem(insightsTree, QStringLiteral("No module context"), 0);
    if (insightsDock)
        insightsDock->setWindowTitle(QStringLiteral("RTL Insights"));
    updateActionState();
}

void RtlInsightsPanelCoordinator::renderActionList()
{
    if (!insightsTree)
        return;
    showTreeSurface();

    insightsTree->clear();
    if (currentFileName.isEmpty() || currentModuleName.isEmpty()) {
        renderNoContext();
        return;
    }

    createGroupItem(
        insightsTree,
        QStringLiteral("Ready: %1").arg(currentModuleName),
        5);
    createGroupItem(
        insightsTree,
        currentSignalName.isEmpty()
            ? QStringLiteral("Select a signal or click Module Brief / Clock/Reset / FSM / Module Block Diagram")
            : QStringLiteral("Current signal: %1").arg(currentSignalName),
        0);
    if (insightsDock)
        insightsDock->setWindowTitle(QStringLiteral("RTL Insights: %1")
                                         .arg(currentModuleName));
    updateActionState();
}

void RtlInsightsPanelCoordinator::showModuleBrief()
{
    if (!insightsTree)
        return;
    showTreeSurface();

    const bool hadExpandableItems =
        SemanticPanelUtils::treeHasExpandableItems(insightsTree);
    const QSet<QString> expandedKeys =
        SemanticPanelUtils::collectExpandedKeys(insightsTree);
    insightsTree->clear();

    if (currentFileName.isEmpty() || currentModuleName.isEmpty()) {
        renderNoContext();
        return;
    }

    QElapsedTimer timer;
    timer.start();
    logReportStart(QStringLiteral("Module Brief"));

    ModuleBriefQuery moduleQuery;
    moduleQuery.fileName = currentFileName;
    moduleQuery.moduleName = currentModuleName;
    ModuleBriefReport moduleReport;
    try {
        moduleReport =
            ModuleBriefService::getInstance()->buildModuleBrief(moduleQuery);
    } catch (const std::exception& error) {
        logReportError(QStringLiteral("Module Brief"),
                       QString::fromLocal8Bit(error.what()));
        return;
    } catch (...) {
        logReportError(QStringLiteral("Module Brief"),
                       QStringLiteral("unknown error"));
        return;
    }

    if (!moduleReport.found) {
        createGroupItem(insightsTree,
                        QStringLiteral("Module not found: %1").arg(currentModuleName),
                        0);
        if (insightsDock)
            insightsDock->setWindowTitle(QStringLiteral("RTL Insights: %1")
                                             .arg(currentModuleName));
        logReportDone(QStringLiteral("Module Brief"),
                      static_cast<int>(timer.elapsed()));
        return;
    }

    appendSymbolGroup(insightsTree,
                      QStringLiteral("Ports"),
                      moduleReport.portRows);
    appendSymbolGroup(insightsTree,
                      QStringLiteral("Parameters"),
                      moduleReport.parameterRows);
    appendSymbolGroup(insightsTree,
                      QStringLiteral("Instances"),
                      moduleReport.instanceRows);
    appendSymbolGroup(insightsTree,
                      QStringLiteral("Imports"),
                      moduleReport.importRows);
    appendContextRows(insightsTree, moduleReport.contextRows);
    appendDiagnostics(insightsTree, moduleReport.diagnosticRows);
    appendRelationshipSummary(insightsTree,
                              moduleReport.relationshipSummary,
                              moduleReport.relationshipEvidenceRows);
    SemanticPanelUtils::restoreTreeExpansion(insightsTree,
                                             hadExpandableItems,
                                             expandedKeys);

    if (insightsDock) {
        insightsDock->setWindowTitle(QStringLiteral("RTL Insights: %1")
                                         .arg(moduleReport.moduleDisplayName));
    }
    if (statusMessageHandler) {
        statusMessageHandler(QStringLiteral("Updated RTL insights for %1")
                                 .arg(moduleReport.moduleDisplayName),
                             1500);
    }
    logReportDone(QStringLiteral("Module Brief"),
                  static_cast<int>(timer.elapsed()));
}

void RtlInsightsPanelCoordinator::showSignalJourney()
{
    if (!insightsTree)
        return;
    showTreeSurface();

    insightsTree->clear();
    if (currentFileName.isEmpty() || currentModuleName.isEmpty()) {
        renderNoContext();
        return;
    }

    QElapsedTimer timer;
    timer.start();
    logReportStart(QStringLiteral("Signal Journey"));
    try {
        appendSignalJourney(insightsTree,
                            currentFileName,
                            currentModuleName,
                            currentSignalName);
    } catch (const std::exception& error) {
        logReportError(QStringLiteral("Signal Journey"),
                       QString::fromLocal8Bit(error.what()));
        return;
    } catch (...) {
        logReportError(QStringLiteral("Signal Journey"),
                       QStringLiteral("unknown error"));
        return;
    }
    if (insightsDock)
        insightsDock->setWindowTitle(QStringLiteral("RTL Insights: Signal Journey %1")
                                         .arg(currentSignalName));
    if (statusMessageHandler)
        statusMessageHandler(QStringLiteral("Rendered signal journey"), 1500);
    logReportDone(QStringLiteral("Signal Journey"),
                  static_cast<int>(timer.elapsed()));
}

void RtlInsightsPanelCoordinator::showClockResetDomainMap()
{
    if (!insightsTree)
        return;
    showTreeSurface();

    insightsTree->clear();
    if (currentFileName.isEmpty() || currentModuleName.isEmpty()) {
        renderNoContext();
        return;
    }

    QElapsedTimer timer;
    timer.start();
    logReportStart(QStringLiteral("Clock/Reset Domain Map"));
    ClockResetDomainReport report;
    try {
        ClockResetDomainQuery query;
        query.fileName = currentFileName;
        query.moduleName = currentModuleName;
        report = ClockResetDomainService::getInstance()->buildClockResetDomainMap(query);
    } catch (const std::exception& error) {
        logReportError(QStringLiteral("Clock/Reset Domain Map"),
                       QString::fromLocal8Bit(error.what()));
        return;
    } catch (...) {
        logReportError(QStringLiteral("Clock/Reset Domain Map"),
                       QStringLiteral("unknown error"));
        return;
    }
    appendClockResetDomains(insightsTree, report);
    appendClockResetEvidenceRows(
        insightsTree,
        report.evidenceGroupDisplayName.isEmpty()
            ? QStringLiteral("Domain Evidence")
            : report.evidenceGroupDisplayName,
        report.evidenceRows);
    appendClockResetEvidenceRows(
        insightsTree,
        report.ambiguityGroupDisplayName.isEmpty()
            ? QStringLiteral("Ambiguity")
            : report.ambiguityGroupDisplayName,
        report.ambiguityRows);
    appendClockResetEvidenceRows(
        insightsTree,
        report.unmappedGroupDisplayName.isEmpty()
            ? QStringLiteral("Unmapped Timing Signals")
            : report.unmappedGroupDisplayName,
        report.unmappedRows);
    if (insightsDock)
        insightsDock->setWindowTitle(QStringLiteral("RTL Insights: Clock/Reset Map %1")
                                         .arg(currentModuleName));
    if (statusMessageHandler)
        statusMessageHandler(QStringLiteral("Rendered clock/reset domain map"), 1500);
    logReportDone(QStringLiteral("Clock/Reset Domain Map"),
                  static_cast<int>(timer.elapsed()));
}

void RtlInsightsPanelCoordinator::showFsmGraph()
{
    if (!insightsGraphScene)
        return;

    if (currentFileName.isEmpty() || currentModuleName.isEmpty()) {
        renderNoContext();
        return;
    }

    QElapsedTimer timer;
    timer.start();
    logReportStart(QStringLiteral("FSM Graph"));
    FsmGraphReport report;
    try {
        FsmGraphQuery query;
        query.fileName = currentFileName;
        query.moduleName = currentModuleName;
        report = FsmGraphService::getInstance()->buildFsmGraph(query);
    } catch (const std::exception& error) {
        logReportError(QStringLiteral("FSM Graph"),
                       QString::fromLocal8Bit(error.what()));
        return;
    } catch (...) {
        logReportError(QStringLiteral("FSM Graph"),
                       QStringLiteral("unknown error"));
        return;
    }
    renderFsmGraphScene(
        report,
        QStringLiteral("FSM Graph %1").arg(currentModuleName));
    if (insightsDock)
        insightsDock->setWindowTitle(QStringLiteral("RTL Insights: FSM Graph %1")
                                         .arg(currentModuleName));
    if (statusMessageHandler)
        statusMessageHandler(QStringLiteral("Rendered FSM graph"), 1500);
    logReportDone(QStringLiteral("FSM Graph"),
                  static_cast<int>(timer.elapsed()));
}

void RtlInsightsPanelCoordinator::showModuleBlockDiagram()
{
    if (!insightsGraphScene)
        return;

    if (currentFileName.isEmpty() || currentModuleName.isEmpty()) {
        renderNoContext();
        return;
    }

    QElapsedTimer timer;
    timer.start();
    logReportStart(QStringLiteral("Module Block Diagram"));
    ModuleBlockDiagramReport report;
    try {
        ModuleBlockDiagramQuery query;
        query.fileName = currentFileName;
        query.moduleName = currentModuleName;
        report = ModuleBlockDiagramService::getInstance()
            ->buildModuleBlockDiagram(query);
    } catch (const std::exception& error) {
        logReportError(QStringLiteral("Module Block Diagram"),
                       QString::fromLocal8Bit(error.what()));
        return;
    } catch (...) {
        logReportError(QStringLiteral("Module Block Diagram"),
                       QStringLiteral("unknown error"));
        return;
    }

    renderModuleBlockDiagramScene(report);
    if (insightsDock) {
        insightsDock->setWindowTitle(
            QStringLiteral("RTL Insights: Module Block Diagram %1")
                .arg(currentModuleName));
        insightsDock->show();
        insightsDock->raise();
    }
    if (statusMessageHandler) {
        statusMessageHandler(
            report.found
                ? QStringLiteral("Rendered module block diagram for %1")
                      .arg(report.root.moduleDisplayName)
                : report.notFoundReasonDisplayName,
            1500);
    }
    logReportDone(QStringLiteral("Module Block Diagram"),
                  static_cast<int>(timer.elapsed()));
}

void RtlInsightsPanelCoordinator::updateActionState()
{
    const bool hasModule = !currentFileName.isEmpty() && !currentModuleName.isEmpty();
    if (moduleBriefButton)
        moduleBriefButton->setEnabled(hasModule);
    if (signalJourneyButton)
        signalJourneyButton->setEnabled(hasModule);
    if (clockResetButton)
        clockResetButton->setEnabled(hasModule);
    if (fsmGraphButton)
        fsmGraphButton->setEnabled(hasModule);
    if (moduleBlockDiagramButton)
        moduleBlockDiagramButton->setEnabled(hasModule);
}

void RtlInsightsPanelCoordinator::logReportStart(const QString& reportName) const
{
    ActivityLogService::getInstance()->append(
        QStringLiteral("RTL Insights"),
        ActivityLogLevel::Info,
        QStringLiteral("%1 start for %2")
            .arg(reportName,
                 currentModuleName.isEmpty()
                     ? QStringLiteral("<no module>")
                     : currentModuleName));
}

void RtlInsightsPanelCoordinator::logReportDone(
    const QString& reportName,
    int durationMs) const
{
    ActivityLogService::getInstance()->append(
        QStringLiteral("RTL Insights"),
        ActivityLogLevel::Info,
        QStringLiteral("%1 done for %2")
            .arg(reportName,
                 currentModuleName.isEmpty()
                     ? QStringLiteral("<no module>")
                     : currentModuleName),
        durationMs);
}

void RtlInsightsPanelCoordinator::logReportError(
    const QString& reportName,
    const QString& message) const
{
    ActivityLogService::getInstance()->append(
        QStringLiteral("RTL Insights"),
        ActivityLogLevel::Error,
        QStringLiteral("%1 failed: %2").arg(reportName, message));
}
