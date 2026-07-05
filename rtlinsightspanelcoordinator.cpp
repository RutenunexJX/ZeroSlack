#include "rtlinsightspanelcoordinator.h"

#include "activitylogservice.h"
#include "clockresetdomainservice.h"
#include "fsmgraphservice.h"
#include "insightgraphview.h"
#include "insightvisualstyle.h"
#include "moduleblockdiagramservice.h"
#include "modulebriefservice.h"
#include "semanticdiffservice.h"
#include "semanticpanelutils.h"
#include "signaljourneyservice.h"
#include "signalusagehotspotpanel.h"
#include "statetransitiongraphservice.h"

#include <QElapsedTimer>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QGraphicsPathItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsRectItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsScene>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsTextItem>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPoint>
#include <QPolygonF>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QTimer>

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
constexpr int kGraphDetailRole = Qt::UserRole + 1208;
constexpr int kGraphNodeIdRole = Qt::UserRole + 1209;
constexpr int kGraphTablePrimaryRole = Qt::UserRole + 1210;
constexpr int kGraphTableSecondaryRole = Qt::UserRole + 1211;
constexpr int kGraphTableKindRole = Qt::UserRole + 1212;
constexpr int kGraphBadgeRole = Qt::UserRole + 1213;
constexpr int kGraphEdgeLabelRole = Qt::UserRole + 1214;
constexpr int kGraphEdgeLabelBoldRole = Qt::UserRole + 1215;
constexpr int kGraphEdgeLabelBackgroundRole = Qt::UserRole + 1216;
constexpr int kGraphEdgeHasCurveRole = Qt::UserRole + 1217;
constexpr int kGraphEdgeArrowAngleRole = Qt::UserRole + 1218;
constexpr int kGraphEdgeLabelColorRole = Qt::UserRole + 1219;
constexpr int kGraphEdgeRouteKindRole = Qt::UserRole + 1220;
constexpr int kGraphEdgeRouteLaneRole = Qt::UserRole + 1221;
constexpr int kGraphEdgeLabelXRole = Qt::UserRole + 1222;
constexpr int kGraphEdgeLabelYRole = Qt::UserRole + 1223;

constexpr qreal kInsightNodeWidth = 170.0;
constexpr qreal kInsightNodeHeight = 56.0;
constexpr qreal kStateNodeWidth = 196.0;
constexpr qreal kStateNodeHeight = 52.0;
constexpr qreal kGraphMinScale = 0.05;
constexpr qreal kGraphMaxScale = 6.0;
constexpr qreal kFsmInitialFitMinScale = 0.12;
constexpr double kPi = 3.14159265358979323846;

struct RtlInsightGraphElement {
    QString kind;
    QString primary;
    QString secondary;
    QString detail;
    QString badge;
    RtlInsightCodeLink codeLink;
    QString drillModuleName;
    QString drillFileName;
};

struct RtlInsightGraphEdgePresentation {
    QPointF labelCenter;
    bool hasLabelCenter = false;
    QString routeKind;
    int routeLane = 0;
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
    if (!element.badge.trimmed().isEmpty())
        appendLine(QStringLiteral("ID: %1").arg(element.badge));
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
    item->setData(kGraphDetailRole, element.detail);
    item->setData(kGraphBadgeRole, element.badge);
    item->setToolTip(graphElementTooltip(element));
}

bool painterPathHasCurve(const QPainterPath& path)
{
    for (int i = 0; i < path.elementCount(); ++i) {
        const QPainterPath::Element element = path.elementAt(i);
        if (element.type == QPainterPath::CurveToElement
            || element.type == QPainterPath::CurveToDataElement) {
            return true;
        }
    }
    return false;
}

qreal distanceBetweenPoints(const QPointF& lhs, const QPointF& rhs)
{
    return std::hypot(lhs.x() - rhs.x(), lhs.y() - rhs.y());
}

qreal distancePointToSegment(const QPointF& point,
                             const QPointF& start,
                             const QPointF& end)
{
    const qreal dx = end.x() - start.x();
    const qreal dy = end.y() - start.y();
    const qreal lengthSquared = dx * dx + dy * dy;
    if (lengthSquared < 0.001)
        return distanceBetweenPoints(point, start);
    const qreal t =
        std::clamp(((point.x() - start.x()) * dx
                    + (point.y() - start.y()) * dy)
                       / lengthSquared,
                   0.0,
                   1.0);
    return distanceBetweenPoints(point,
                                 QPointF(start.x() + t * dx,
                                         start.y() + t * dy));
}

QList<QPointF> sampledPathPoints(const QPainterPath& path, int sampleCount = 80)
{
    QList<QPointF> points;
    if (path.elementCount() <= 0)
        return points;
    const int count = qMax(2, sampleCount);
    points.reserve(count + 1);
    for (int i = 0; i <= count; ++i)
        points.append(path.pointAtPercent(static_cast<qreal>(i) / count));
    return points;
}

qreal distancePointToPath(const QPointF& point, const QPainterPath& path)
{
    const QList<QPointF> points = sampledPathPoints(path);
    if (points.isEmpty())
        return std::numeric_limits<qreal>::max();
    qreal best = std::numeric_limits<qreal>::max();
    for (int i = 1; i < points.size(); ++i)
        best = qMin(best, distancePointToSegment(point,
                                                 points.at(i - 1),
                                                 points.at(i)));
    return best;
}

qreal distanceBetweenPaths(const QPainterPath& lhs, const QPainterPath& rhs)
{
    const QList<QPointF> lhsPoints = sampledPathPoints(lhs, 56);
    const QList<QPointF> rhsPoints = sampledPathPoints(rhs, 56);
    if (lhsPoints.isEmpty() || rhsPoints.isEmpty())
        return -1.0;
    qreal best = std::numeric_limits<qreal>::max();
    for (int i = 10; i + 10 < lhsPoints.size(); ++i) {
        for (int j = 10; j + 10 < rhsPoints.size(); ++j)
            best = qMin(best, distanceBetweenPoints(lhsPoints.at(i),
                                                    rhsPoints.at(j)));
    }
    return best == std::numeric_limits<qreal>::max() ? -1.0 : best;
}

bool pathSamplesIntersectNodeRects(const QPainterPath& path,
                                   const QList<QRectF>& nodeRects)
{
    if (nodeRects.isEmpty())
        return false;
    const int sampleCount = 90;
    for (int i = 11; i <= sampleCount - 11; ++i) {
        const QPointF point =
            path.pointAtPercent(static_cast<qreal>(i) / sampleCount);
        for (const QRectF& rect : nodeRects) {
            if (rect.adjusted(2.0, 2.0, -2.0, -2.0).contains(point))
                return true;
        }
    }
    return false;
}

qreal painterPathTangentAngleAtEnd(const QPainterPath& path)
{
    if (path.elementCount() <= 1)
        return 0.0;
    const QPointF end = path.pointAtPercent(1.0);
    for (qreal percent : {0.98, 0.95, 0.90, 0.80}) {
        const QPointF previous = path.pointAtPercent(percent);
        if (distanceBetweenPoints(previous, end) > 0.5)
            return std::atan2(end.y() - previous.y(), end.x() - previous.x());
    }
    const QPainterPath::Element last = path.elementAt(path.elementCount() - 1);
    const QPainterPath::Element previous =
        path.elementAt(qMax(0, path.elementCount() - 2));
    return std::atan2(last.y - previous.y, last.x - previous.x);
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
        normalBrush = QBrush(fill);
        setBrush(normalBrush);
        normalPen = QPen(stroke, 1.6);
        hoverPen = InsightVisualStyle::hoverPen();
        setPen(normalPen);
        setBaseZValues(10.0, 40.0);
        applyGraphElementData(this, element);

        QFont titleFont = InsightVisualStyle::titleFont(font);
        titleFont.setPointSize(qMax(8, titleFont.pointSize() + 1));
        QFont detailFont = InsightVisualStyle::compactFont(font);

        auto* title = new QGraphicsSimpleTextItem(
            graphElidedText(element.primary,
                            titleFont,
                            static_cast<int>(rect.width() - 18)),
            this);
        title->setAcceptedMouseButtons(Qt::NoButton);
        title->setFont(titleFont);
        title->setBrush(QBrush(InsightVisualStyle::theme().textPrimary));
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
        detailItem->setBrush(QBrush(InsightVisualStyle::theme().textSecondary));
        detailItem->setPos(rect.left() + 10, rect.top() + 34);
    }

    NavigateHandler navigateHandler;
    SelectHandler selectHandler;

    void setBaseZValues(qreal normalZ, qreal hoverZ)
    {
        normalZValue = normalZ;
        hoverZValue = hoverZ;
        setZValue(hovered ? hoverZValue : normalZValue);
    }

    void setHoveredForTest(bool value)
    {
        hovered = value;
        refreshVisual();
    }

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override
    {
        hovered = true;
        refreshVisual();
        QGraphicsRectItem::hoverEnterEvent(event);
    }

    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override
    {
        hovered = false;
        refreshVisual();
        QGraphicsRectItem::hoverLeaveEvent(event);
    }

    void mousePressEvent(QGraphicsSceneMouseEvent* event) override
    {
        setSelected(true);
        refreshVisual();
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

    QVariant itemChange(GraphicsItemChange change,
                        const QVariant& value) override
    {
        const QVariant result = QGraphicsRectItem::itemChange(change, value);
        if (change == QGraphicsItem::ItemSelectedHasChanged)
            refreshVisual();
        return result;
    }

private:
    void refreshVisual()
    {
        setBrush(normalBrush);
        setZValue(hovered ? hoverZValue : normalZValue);
        if (isSelected()) {
            setPen(InsightVisualStyle::selectedPen());
        } else if (hovered) {
            setPen(hoverPen);
        } else {
            setPen(normalPen);
        }
    }

    RtlInsightGraphElement element;
    QBrush normalBrush;
    QPen normalPen;
    QPen hoverPen;
    qreal normalZValue = 10.0;
    qreal hoverZValue = 40.0;
    bool hovered = false;
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
        normalBrush = QBrush(fill);
        setBrush(normalBrush);
        normalPen = QPen(stroke, dashed ? 1.6 : 2.0);
        if (dashed)
            normalPen.setStyle(Qt::DashLine);
        hoverPen = InsightVisualStyle::hoverPen();
        setPen(normalPen);
        setBaseZValues(10.0, 40.0);
        applyGraphElementData(this, element);

        QFont titleFont = InsightVisualStyle::titleFont(font);
        titleFont.setPointSize(qMax(8, titleFont.pointSize() + 1));

        auto* title = new QGraphicsSimpleTextItem(
            element.primary,
            this);
        title->setAcceptedMouseButtons(Qt::NoButton);
        title->setFont(titleFont);
        title->setBrush(QBrush(InsightVisualStyle::theme().textPrimary));
        const QRectF titleBounds = title->boundingRect();
        const bool hasSecondary = !element.secondary.trimmed().isEmpty();
        title->setPos(rect.center().x() - titleBounds.width() / 2.0,
                      rect.center().y() - titleBounds.height() / 2.0
                          - (hasSecondary ? 8.0 : 0.0));

        if (hasSecondary) {
            QFont secondaryFont = InsightVisualStyle::compactFont(font);
            secondaryFont.setPointSize(qMax(7, secondaryFont.pointSize() - 1));
            auto* secondary = new QGraphicsSimpleTextItem(
                graphElidedText(element.secondary,
                                secondaryFont,
                                static_cast<int>(rect.width() - 44)),
                this);
            secondary->setAcceptedMouseButtons(Qt::NoButton);
            secondary->setFont(secondaryFont);
            secondary->setBrush(QBrush(InsightVisualStyle::theme().textMuted));
            const QRectF secondaryBounds = secondary->boundingRect();
            secondary->setPos(rect.center().x() - secondaryBounds.width() / 2.0,
                              rect.center().y() + 8.0);
        }
    }

    NavigateHandler navigateHandler;
    SelectHandler selectHandler;

    void setBaseZValues(qreal normalZ, qreal hoverZ)
    {
        normalZValue = normalZ;
        hoverZValue = hoverZ;
        setZValue(hovered ? hoverZValue : normalZValue);
    }

    void setHoveredForTest(bool value)
    {
        hovered = value;
        refreshVisual();
    }

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override
    {
        hovered = true;
        refreshVisual();
        QGraphicsEllipseItem::hoverEnterEvent(event);
    }

    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override
    {
        hovered = false;
        refreshVisual();
        QGraphicsEllipseItem::hoverLeaveEvent(event);
    }

    void mousePressEvent(QGraphicsSceneMouseEvent* event) override
    {
        setSelected(true);
        refreshVisual();
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

    QVariant itemChange(GraphicsItemChange change,
                        const QVariant& value) override
    {
        const QVariant result = QGraphicsEllipseItem::itemChange(change, value);
        if (change == QGraphicsItem::ItemSelectedHasChanged)
            refreshVisual();
        return result;
    }

private:
    void refreshVisual()
    {
        setBrush(normalBrush);
        setZValue(hovered ? hoverZValue : normalZValue);
        if (isSelected()) {
            setPen(InsightVisualStyle::selectedPen());
        } else if (hovered) {
            setPen(hoverPen);
        } else {
            setPen(normalPen);
        }
    }

    RtlInsightGraphElement element;
    QBrush normalBrush;
    QPen normalPen;
    QPen hoverPen;
    qreal normalZValue = 10.0;
    qreal hoverZValue = 40.0;
    bool hovered = false;
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
                            qreal labelOffset = 0.0,
                            bool labelBold = false,
                            bool labelBackgroundVisible = true,
                            const QColor& labelColor = QColor(),
                            const RtlInsightGraphEdgePresentation& presentation =
                                RtlInsightGraphEdgePresentation())
        : QGraphicsPathItem(path),
          element(graphElement)
    {
        setFlag(QGraphicsItem::ItemIsSelectable, true);
        setAcceptHoverEvents(true);
        setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
        normalPen = QPen(color, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        hoverPen = InsightVisualStyle::hoverPen(2.3);
        setPen(normalPen);
        setZValue(2);
        applyGraphElementData(this, element);
        setData(kGraphEdgeLabelRole, label);
        setData(kGraphEdgeLabelBoldRole, labelBold);
        setData(kGraphEdgeLabelBackgroundRole, labelBackgroundVisible);
        setData(kGraphEdgeHasCurveRole, painterPathHasCurve(path));
        setData(kGraphEdgeArrowAngleRole, arrowAngle);
        const QColor resolvedLabelColor =
            labelColor.isValid() ? labelColor
                                 : InsightVisualStyle::theme().textSecondary;
        setData(kGraphEdgeLabelColorRole, resolvedLabelColor.name());
        setData(kGraphEdgeRouteKindRole, presentation.routeKind);
        setData(kGraphEdgeRouteLaneRole, presentation.routeLane);
        if (presentation.hasLabelCenter) {
            setData(kGraphEdgeLabelXRole, presentation.labelCenter.x());
            setData(kGraphEdgeLabelYRole, presentation.labelCenter.y());
        }

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
            labelFont.setBold(labelBold);
            auto* labelItem = new QGraphicsTextItem(label, this);
            labelItem->setAcceptedMouseButtons(Qt::NoButton);
            labelItem->setFont(labelFont);
            const qreal labelTextWidth = qBound<qreal>(
                28.0,
                QFontMetrics(labelFont).horizontalAdvance(label) + 12.0,
                220.0);
            labelItem->setTextWidth(labelTextWidth);
            labelItem->setDefaultTextColor(resolvedLabelColor);
            const QRectF pathBounds = path.boundingRect();
            const QRectF labelBounds = labelItem->boundingRect();
            if (labelBackgroundVisible) {
                auto* labelBackground = new QGraphicsRectItem(
                    labelBounds.adjusted(-4.0, -2.0, 4.0, 2.0),
                    labelItem);
                labelBackground->setAcceptedMouseButtons(Qt::NoButton);
                labelBackground->setPen(Qt::NoPen);
                QColor labelFill = InsightVisualStyle::theme().canvasBackground;
                labelFill.setAlpha(226);
                labelBackground->setBrush(labelFill);
                labelBackground->setZValue(-1);
            }
            const QPointF labelCenter = presentation.hasLabelCenter
                ? presentation.labelCenter
                : QPointF(pathBounds.center().x(),
                          pathBounds.center().y() + labelOffset);
            labelItem->setPos(labelCenter.x() - labelBounds.width() / 2.0,
                              labelCenter.y() - labelBounds.height() / 2.0);
            setData(kGraphEdgeLabelXRole, labelCenter.x());
            setData(kGraphEdgeLabelYRole, labelCenter.y());
        }
    }

    NavigateHandler navigateHandler;
    SelectHandler selectHandler;

    void setHoveredForTest(bool value)
    {
        hovered = value;
        refreshPen();
    }

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override
    {
        hovered = true;
        setZValue(30);
        refreshPen();
        QGraphicsPathItem::hoverEnterEvent(event);
    }

    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override
    {
        hovered = false;
        setZValue(2);
        refreshPen();
        QGraphicsPathItem::hoverLeaveEvent(event);
    }

    void mousePressEvent(QGraphicsSceneMouseEvent* event) override
    {
        setSelected(true);
        refreshPen();
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

    QVariant itemChange(GraphicsItemChange change,
                        const QVariant& value) override
    {
        const QVariant result = QGraphicsPathItem::itemChange(change, value);
        if (change == QGraphicsItem::ItemSelectedHasChanged)
            refreshPen();
        return result;
    }

private:
    void refreshPen()
    {
        if (isSelected()) {
            setPen(InsightVisualStyle::selectedPen(2.5));
        } else if (hovered) {
            setPen(hoverPen);
        } else {
            setPen(normalPen);
        }
    }

    RtlInsightGraphElement element;
    QPen normalPen;
    QPen hoverPen;
    bool hovered = false;
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
    return qMax(kStateNodeWidth, textWidth + 52.0);
}

QRectF stateNodeRectAt(qreal centerX, qreal centerY, qreal width = kStateNodeWidth)
{
    return QRectF(centerX - width / 2.0,
                  centerY - kStateNodeHeight / 2.0,
                  width,
                  kStateNodeHeight);
}

QList<InsightVisualRole> fsmStatePalette()
{
    return {InsightVisualRole::Timing,
            InsightVisualRole::Read,
            InsightVisualRole::Condition,
            InsightVisualRole::Write,
            InsightVisualRole::Port,
            InsightVisualRole::Case,
            InsightVisualRole::Data};
}

uint stableStringHash(const QString& value)
{
    uint hash = 2166136261u;
    for (QChar ch : value) {
        hash ^= static_cast<uint>(ch.unicode());
        hash *= 16777619u;
    }
    return hash;
}

QColor fsmNeutralStateFillColor()
{
    return InsightVisualStyle::theme().graph.nodeFill;
}

QColor fsmNeutralStateStrokeColor()
{
    return InsightVisualStyle::theme().graph.nodeBorder;
}

InsightVisualRole fsmAliasAccentRole(const QString& stateName)
{
    const QList<InsightVisualRole> palette = fsmStatePalette();
    if (palette.isEmpty())
        return InsightVisualRole::Data;
    return palette.at(static_cast<int>(stableStringHash(stateName)
                                       % static_cast<uint>(palette.size())));
}

QColor fsmAliasStateFillColor(const QString& stateName)
{
    return InsightVisualStyle::roleFillColor(
        fsmAliasAccentRole(stateName));
}

QColor fsmAliasStateStrokeColor(const QString& stateName)
{
    return InsightVisualStyle::roleColor(fsmAliasAccentRole(stateName));
}

QString fsmStateDetailText(const FsmStateRow& row)
{
    if (row.statusDisplayName.isEmpty())
        return row.detailDisplayName;
    if (row.detailDisplayName.isEmpty())
        return row.statusDisplayName;
    return QStringLiteral("%1; %2")
        .arg(row.detailDisplayName, row.statusDisplayName);
}

QSet<QString> deadFsmStateNames(const FsmGraph& graph)
{
    QSet<QString> names;
    for (const FsmStateRow& row : graph.stateRows) {
        if (row.deadEndState)
            names.insert(row.stateDisplayName);
    }
    return names;
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

QPointF ellipsePointAtAngle(const QRectF& rect, qreal angle)
{
    return QPointF(rect.center().x() + std::cos(angle) * rect.width() / 2.0,
                   rect.center().y() + std::sin(angle) * rect.height() / 2.0);
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

bool isGraphShapeItem(const QGraphicsItem* item)
{
    return dynamic_cast<const RtlInsightGraphNodeItem*>(item)
        || dynamic_cast<const RtlInsightGraphStateNodeItem*>(item)
        || dynamic_cast<const RtlInsightGraphEdgeItem*>(item);
}

bool isGraphNodeShapeItem(const QGraphicsItem* item)
{
    return dynamic_cast<const RtlInsightGraphNodeItem*>(item)
        || dynamic_cast<const RtlInsightGraphStateNodeItem*>(item);
}

QColor graphItemFillColor(const QGraphicsItem* item)
{
    if (const auto* rect = dynamic_cast<const QGraphicsRectItem*>(item))
        return rect->brush().color();
    if (const auto* ellipse = dynamic_cast<const QGraphicsEllipseItem*>(item))
        return ellipse->brush().color();
    return InsightVisualStyle::theme().canvasBackground;
}

QColor graphItemPenColor(const QGraphicsItem* item)
{
    if (const auto* rect = dynamic_cast<const QGraphicsRectItem*>(item))
        return rect->pen().color();
    if (const auto* ellipse = dynamic_cast<const QGraphicsEllipseItem*>(item))
        return ellipse->pen().color();
    if (const auto* path = dynamic_cast<const QGraphicsPathItem*>(item))
        return path->pen().color();
    return QColor();
}

bool colorsTooSimilar(const QColor& lhs, const QColor& rhs)
{
    if (!lhs.isValid() || !rhs.isValid())
        return false;
    return std::abs(lhs.red() - rhs.red()) < 8
        && std::abs(lhs.green() - rhs.green()) < 8
        && std::abs(lhs.blue() - rhs.blue()) < 8;
}

bool graphItemReadable(const QGraphicsItem* item)
{
    const QColor fill = graphItemFillColor(item);
    if (fill.isValid() && fill.alpha() == 0)
        return false;
    const QColor pen = graphItemPenColor(item);
    if (pen.isValid() && pen.alpha() == 0)
        return false;
    for (const QGraphicsItem* child : item->childItems()) {
        QColor textColor;
        if (const auto* simpleText =
                dynamic_cast<const QGraphicsSimpleTextItem*>(child)) {
            textColor = simpleText->brush().color();
        } else if (const auto* text =
                       dynamic_cast<const QGraphicsTextItem*>(child)) {
            textColor = text->defaultTextColor();
        } else {
            continue;
        }
        if (!textColor.isValid() || textColor.alpha() == 0
            || colorsTooSimilar(textColor, fill)) {
            return false;
        }
    }
    return true;
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

QString fsmTransitionLayoutKey(const FsmTransitionRow& row)
{
    return QStringLiteral("%1\n%2\n%3\n%4\n%5")
        .arg(row.fromStateDisplayName,
             row.toStateDisplayName,
             row.codeLink.fileName,
             QString::number(row.codeLink.line),
             row.conditionDisplayName);
}

int alternatingLane(int index)
{
    const int lane = index / 2 + 1;
    return index % 2 == 0 ? -lane : lane;
}

struct FsmLayoutNode {
    QString id;
    QString canonicalState;
    QRectF rect;
    bool alias = false;
    QString aliasDetail;
    int column = 0;
    int lane = 0;
};

struct FsmGraphLayout {
    QList<QString> orderedStates;
    QList<FsmLayoutNode> nodes;
    QHash<QString, QRectF> stateRects;
    QHash<QString, QRectF> nodeRects;
    QHash<QString, QString> canonicalNodeIds;
    QHash<QString, QString> aliasNodeIdByTransitionKey;
    QHash<QString, int> stateIndexes;
    QHash<QString, int> stateColumns;
    QHash<QString, int> stateLanes;
    QHash<QString, int> nodeColumns;
    QHash<QString, int> nodeLanes;
    QHash<QString, FsmStateRow> stateRowsByName;
    QRectF bounds;
};

struct FsmGraphRenderResult {
    QRectF sceneBounds;
    QRectF bodyBounds;
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

    QHash<QString, QList<QString>> outgoingByState;
    QHash<QString, int> incomingCountByState;
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
    for (const QString& stateName : layout.orderedStates) {
        QList<QString> targets = uniqueOutgoingTargets(stateName);
        const int sourceIndex = indexInList(layout.orderedStates, stateName);
        std::sort(targets.begin(),
                  targets.end(),
                  [&](const QString& lhs, const QString& rhs) {
                      const int lhsIndex = indexInList(layout.orderedStates, lhs);
                      const int rhsIndex = indexInList(layout.orderedStates, rhs);
                      const bool lhsForward = lhsIndex > sourceIndex;
                      const bool rhsForward = rhsIndex > sourceIndex;
                      if (lhsForward != rhsForward)
                          return lhsForward;
                      return lhsIndex < rhsIndex;
                  });
        outgoingByState.insert(stateName, targets);
        for (const QString& target : std::as_const(targets))
            incomingCountByState[target] = incomingCountByState.value(target) + 1;
    }

    QHash<QString, int> stateColumns;
    QHash<QString, int> stateLanes;
    for (int i = 0; i < layout.orderedStates.size(); ++i) {
        stateColumns.insert(layout.orderedStates.at(i), i);
        stateLanes.insert(layout.orderedStates.at(i), 0);
    }

    QList<QString> mainPath;
    QSet<QString> mainPathStates;
    if (!layout.orderedStates.isEmpty()) {
        QString current = layout.orderedStates.first();
        while (!current.isEmpty() && !mainPathStates.contains(current)) {
            mainPath.append(current);
            mainPathStates.insert(current);

            const int sourceIndex = stateColumns.value(current);
            QString next;
            for (const QString& target : outgoingByState.value(current)) {
                if (!mainPathStates.contains(target)
                    && stateColumns.value(target) > sourceIndex) {
                    next = target;
                    break;
                }
            }
            if (next.isEmpty()) {
                for (const QString& target : outgoingByState.value(current)) {
                    if (!mainPathStates.contains(target)) {
                        next = target;
                        break;
                    }
                }
            }
            current = next;
        }
    }

    int branchOrdinal = 0;
    for (const QString& source : std::as_const(layout.orderedStates)) {
        const QList<QString> targets = outgoingByState.value(source);
        if (targets.size() <= 1)
            continue;

        QString primaryTarget;
        const int sourceMainIndex = indexInList(mainPath, source);
        if (sourceMainIndex >= 0 && sourceMainIndex + 1 < mainPath.size())
            primaryTarget = mainPath.at(sourceMainIndex + 1);
        if (primaryTarget.isEmpty() && !targets.isEmpty())
            primaryTarget = targets.first();

        for (const QString& target : targets) {
            if (target == primaryTarget)
                continue;
            if (stateColumns.value(target) <= stateColumns.value(source))
                continue;
            if (mainPathStates.contains(target))
                continue;
            stateColumns[target] =
                qMax(stateColumns.value(target), stateColumns.value(source) + 1);
            stateLanes[target] = alternatingLane(branchOrdinal++);
        }
    }

    for (const QString& stateName : std::as_const(layout.orderedStates)) {
        if (mainPathStates.contains(stateName))
            continue;
        if (stateLanes.value(stateName) != 0)
            continue;
        if (incomingCountByState.value(stateName) > 1)
            stateLanes[stateName] = alternatingLane(branchOrdinal++);
    }

    QSet<QString> occupiedSlots;
    for (const QString& stateName : std::as_const(layout.orderedStates)) {
        int lane = stateLanes.value(stateName);
        const int column = stateColumns.value(stateName);
        QString slotKey = QStringLiteral("%1:%2").arg(column).arg(lane);
        int collisionOrdinal = 0;
        while (occupiedSlots.contains(slotKey)) {
            lane = lane == 0
                ? alternatingLane(collisionOrdinal++)
                : lane + (lane > 0 ? 1 : -1);
            slotKey = QStringLiteral("%1:%2").arg(column).arg(lane);
        }
        occupiedSlots.insert(slotKey);
        stateLanes[stateName] = lane;
    }

    int maxColumn = 0;
    int maxLane = 0;
    int minLane = 0;
    for (const QString& stateName : std::as_const(layout.orderedStates)) {
        maxColumn = qMax(maxColumn, stateColumns.value(stateName));
        maxLane = qMax(maxLane, stateLanes.value(stateName));
        minLane = qMin(minLane, stateLanes.value(stateName));
    }

    const qreal columnGap = 62.0;
    const qreal verticalGap = kStateNodeHeight + 60.0;
    QHash<int, qreal> columnWidths;
    for (const QString& stateName : std::as_const(layout.orderedStates)) {
        const int column = stateColumns.value(stateName);
        columnWidths[column] =
            qMax(columnWidths.value(column),
                 stateWidths.value(stateName, kStateNodeWidth));
    }
    QList<qreal> columnCenters;
    columnCenters.reserve(maxColumn + 1);
    qreal cursorX = 0.0;
    for (int column = 0; column <= maxColumn; ++column) {
        const qreal width =
            qMax<qreal>(kStateNodeWidth, columnWidths.value(column));
        columnCenters.append(cursorX + width / 2.0);
        cursorX += width + columnGap;
    }
    const qreal totalColumnWidth =
        cursorX > 0.0 ? cursorX - columnGap : kStateNodeWidth;
    const qreal startX = origin.x() - totalColumnWidth / 2.0;
    auto columnCenterX = [&](int column) {
        if (column >= 0 && column < columnCenters.size())
            return startX + columnCenters.at(column);
        return startX + totalColumnWidth / 2.0;
    };

    auto addLayoutNode = [&](FsmLayoutNode node) {
        layout.nodeRects.insert(node.id, node.rect);
        layout.nodeColumns.insert(node.id, node.column);
        layout.nodeLanes.insert(node.id, node.lane);
        if (!node.alias) {
            layout.stateRects.insert(node.canonicalState, node.rect);
            layout.canonicalNodeIds.insert(node.canonicalState, node.id);
            layout.stateColumns.insert(node.canonicalState, node.column);
            layout.stateLanes.insert(node.canonicalState, node.lane);
        }
        layout.nodes.append(node);
        layout.bounds = layout.bounds.isNull()
            ? node.rect
            : layout.bounds.united(node.rect);
    };

    auto placeState = [&](const QString& stateName,
                          int column,
                          int lane,
                          bool alias,
                          const QString& aliasDetail,
                          const QString& aliasId = QString()) {
        const QPointF center(columnCenterX(column),
                             origin.y() + lane * verticalGap);
        const QRectF rect = stateNodeRectAt(
            center.x(),
            center.y(),
            stateWidths.value(stateName, kStateNodeWidth));
        FsmLayoutNode node;
        node.id = alias
            ? aliasId
            : QStringLiteral("state:%1").arg(stateName);
        node.canonicalState = stateName;
        node.rect = rect;
        node.alias = alias;
        node.aliasDetail = aliasDetail;
        node.column = column;
        node.lane = lane;
        addLayoutNode(node);
    };

    for (const QString& stateName : std::as_const(layout.orderedStates)) {
        placeState(stateName,
                   stateColumns.value(stateName),
                   stateLanes.value(stateName),
                   false,
                   QString());
    }

    for (int i = 0; i < layout.orderedStates.size(); ++i) {
        layout.stateIndexes.insert(layout.orderedStates.at(i), i);
    }

    struct AliasCandidate {
        QString canonicalState;
        int farthestSourceColumn = -1;
        int preferredLane = 0;
        QList<QString> transitionKeys;
    };
    QHash<QString, AliasCandidate> aliasCandidatesByState;
    if (stateCount >= 7) {
        for (const FsmTransitionRow& row : graph.transitionRows) {
            if (row.fromStateDisplayName == row.toStateDisplayName)
                continue;
            if (!knownStates.contains(row.fromStateDisplayName)
                || !knownStates.contains(row.toStateDisplayName)) {
                continue;
            }
            const int fromColumn = stateColumns.value(row.fromStateDisplayName);
            const int toColumn = stateColumns.value(row.toStateDisplayName);
            const int span = fromColumn - toColumn;
            const bool longBackEdge = span >= 4;
            const bool crossLaneBackEdge =
                span >= 3
                && stateLanes.value(row.fromStateDisplayName)
                       != stateLanes.value(row.toStateDisplayName);
            if (!longBackEdge && !crossLaneBackEdge)
                continue;

            AliasCandidate candidate =
                aliasCandidatesByState.value(row.toStateDisplayName);
            candidate.canonicalState = row.toStateDisplayName;
            if (fromColumn > candidate.farthestSourceColumn) {
                candidate.farthestSourceColumn = fromColumn;
                candidate.preferredLane =
                    stateLanes.value(row.fromStateDisplayName);
            }
            candidate.transitionKeys.append(fsmTransitionLayoutKey(row));
            aliasCandidatesByState.insert(row.toStateDisplayName, candidate);
        }
    }

    QList<AliasCandidate> aliasCandidates = aliasCandidatesByState.values();
    std::sort(aliasCandidates.begin(),
              aliasCandidates.end(),
              [](const AliasCandidate& lhs, const AliasCandidate& rhs) {
                  if (lhs.farthestSourceColumn != rhs.farthestSourceColumn)
                      return lhs.farthestSourceColumn > rhs.farthestSourceColumn;
                  return lhs.canonicalState < rhs.canonicalState;
              });
    const int aliasLimit = qMin(2, qMax(1, stateCount / 6));
    int aliasOrdinal = 0;
    for (const AliasCandidate& candidate : std::as_const(aliasCandidates)) {
        if (aliasOrdinal >= aliasLimit)
            break;
        if (!layout.stateRowsByName.contains(candidate.canonicalState))
            continue;

        int aliasColumn = candidate.farthestSourceColumn;
        int aliasLane = candidate.preferredLane == 0
            ? (aliasOrdinal % 2 == 0 ? -1 : 1)
            : candidate.preferredLane + (candidate.preferredLane > 0 ? 1 : -1);
        QString slotKey =
            QStringLiteral("%1:%2").arg(aliasColumn).arg(aliasLane);
        while (occupiedSlots.contains(slotKey)) {
            aliasLane += aliasLane >= 0 ? 1 : -1;
            slotKey = QStringLiteral("%1:%2").arg(aliasColumn).arg(aliasLane);
        }
        occupiedSlots.insert(slotKey);

        const QString aliasId =
            QStringLiteral("state-alias:%1:%2")
                .arg(candidate.canonicalState)
                .arg(aliasOrdinal);
        const QString aliasDetail =
            QStringLiteral("duplicate/alias of canonical state %1")
                .arg(candidate.canonicalState);
        placeState(candidate.canonicalState,
                   aliasColumn,
                   aliasLane,
                   true,
                   aliasDetail,
                   aliasId);
        for (const QString& key : candidate.transitionKeys)
            layout.aliasNodeIdByTransitionKey.insert(key, aliasId);
        ++aliasOrdinal;
    }
    return layout;
}

QString fsmTransitionConditionId(int transitionIndex)
{
    return QStringLiteral("C%1").arg(transitionIndex);
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

enum class FsmTransitionRouteKind {
    Normal,
    ReversePairUpper,
    ReversePairLower,
    OuterBackEdge,
    SelfLoop
};

QString fsmTransitionRouteKindName(FsmTransitionRouteKind kind)
{
    switch (kind) {
    case FsmTransitionRouteKind::ReversePairUpper:
        return QStringLiteral("reversePairUpper");
    case FsmTransitionRouteKind::ReversePairLower:
        return QStringLiteral("reversePairLower");
    case FsmTransitionRouteKind::OuterBackEdge:
        return QStringLiteral("outerBackEdge");
    case FsmTransitionRouteKind::SelfLoop:
        return QStringLiteral("selfLoop");
    case FsmTransitionRouteKind::Normal:
    default:
        return QStringLiteral("normal");
    }
}

struct FsmTransitionRoutePlan {
    QPainterPath path;
    QPointF arrowTip;
    qreal arrowAngle = 0.0;
    QPointF labelCenter;
    FsmTransitionRouteKind kind = FsmTransitionRouteKind::Normal;
    int lane = 0;
};

QPointF pathLabelPoint(const QPainterPath& path,
                       qreal percent,
                       qreal distance,
                       int preferredVerticalSign)
{
    const qreal clampedPercent = std::clamp(percent, 0.02, 0.98);
    const QPointF center = path.pointAtPercent(clampedPercent);
    const QPointF before =
        path.pointAtPercent(qMax<qreal>(0.0, clampedPercent - 0.025));
    const QPointF after =
        path.pointAtPercent(qMin<qreal>(1.0, clampedPercent + 0.025));
    QPointF normal(-(after.y() - before.y()), after.x() - before.x());
    qreal length = std::hypot(normal.x(), normal.y());
    if (length < 0.1) {
        normal = QPointF(0.0, preferredVerticalSign < 0 ? -1.0 : 1.0);
        length = 1.0;
    }
    normal = QPointF(normal.x() / length, normal.y() / length);
    if (preferredVerticalSign != 0
        && normal.y() * preferredVerticalSign < 0.0) {
        normal = QPointF(-normal.x(), -normal.y());
    }
    return center + QPointF(normal.x() * distance, normal.y() * distance);
}

FsmTransitionRoutePlan buildFsmTransitionRoutePlan(
    const QRectF& fromRect,
    const QRectF& toRect,
    const QRectF& layoutBounds,
    FsmTransitionRouteKind kind,
    int lane)
{
    FsmTransitionRoutePlan plan;
    plan.kind = kind;
    plan.lane = lane;

    if (kind == FsmTransitionRouteKind::SelfLoop) {
        const int laneMagnitude = qMax(0, std::abs(lane));
        const qreal loopLift = 38.0 + laneMagnitude * 12.0;
        const qreal loopOutset = 28.0 + laneMagnitude * 8.0;
        const QPointF start = ellipsePointAtAngle(fromRect, -3.0 * kPi / 4.0);
        const QPointF end = ellipsePointAtAngle(fromRect, -kPi / 4.0);
        plan.path.moveTo(start);
        const QPointF control1(start.x() - loopOutset,
                               fromRect.top() - loopLift);
        const QPointF control2(end.x() + loopOutset,
                               fromRect.top() - loopLift);
        plan.path.cubicTo(control1, control2, end);
        plan.arrowTip = end;
        plan.arrowAngle = std::atan2(end.y() - control2.y(),
                                     end.x() - control2.x());
        plan.labelCenter = QPointF(fromRect.center().x(),
                                   fromRect.top() - loopLift - 22.0);
        return plan;
    }

    const bool routeAbove = lane >= 0;
    if (kind == FsmTransitionRouteKind::OuterBackEdge) {
        const int laneMagnitude = qMax(1, std::abs(lane));
        const qreal outerY = routeAbove
            ? layoutBounds.top() - 42.0 - laneMagnitude * 22.0
            : layoutBounds.bottom() + 42.0 + laneMagnitude * 22.0;
        const QPointF start(fromRect.center().x(),
                            routeAbove ? fromRect.top() : fromRect.bottom());
        const QPointF end(toRect.center().x(),
                          routeAbove ? toRect.top() : toRect.bottom());
        plan.path.moveTo(start);
        plan.path.lineTo(QPointF(start.x(), outerY));
        plan.path.lineTo(QPointF(end.x(), outerY));
        plan.path.lineTo(end);
        plan.arrowTip = end;
        plan.arrowAngle = painterPathTangentAngleAtEnd(plan.path);
        plan.labelCenter =
            QPointF((start.x() + end.x()) / 2.0,
                    outerY + (routeAbove ? -24.0 : 24.0));
        return plan;
    }

    if (kind == FsmTransitionRouteKind::ReversePairUpper
        || kind == FsmTransitionRouteKind::ReversePairLower) {
        const bool upper = kind == FsmTransitionRouteKind::ReversePairUpper;
        const qreal curveLift = 48.0 + std::abs(lane) * 16.0;
        const QPointF start(fromRect.center().x() <= toRect.center().x()
                                ? fromRect.right()
                                : fromRect.left(),
                            fromRect.center().y());
        const QPointF end(fromRect.center().x() <= toRect.center().x()
                              ? toRect.left()
                              : toRect.right(),
                          toRect.center().y());
        const QPointF mid = (start + end) / 2.0;
        const qreal controlY = upper
            ? qMin(fromRect.top(), toRect.top()) - curveLift
            : qMax(fromRect.bottom(), toRect.bottom()) + curveLift;
        const QPointF control(mid.x(), controlY);
        plan.path.moveTo(start);
        plan.path.quadTo(control, end);
        plan.arrowTip = end;
        plan.arrowAngle = std::atan2(end.y() - control.y(),
                                     end.x() - control.x());
        plan.labelCenter =
            QPointF(mid.x(), controlY + (upper ? -22.0 : 22.0));
        return plan;
    }

    const qreal dx = toRect.center().x() - fromRect.center().x();
    const qreal dy = toRect.center().y() - fromRect.center().y();
    QPointF start;
    QPointF end;
    if (std::abs(dx) >= std::abs(dy)) {
        start = QPointF(dx >= 0 ? fromRect.right() : fromRect.left(),
                        fromRect.center().y());
        end = QPointF(dx >= 0 ? toRect.left() : toRect.right(),
                      toRect.center().y());
    } else {
        start = QPointF(fromRect.center().x(),
                        dy >= 0 ? fromRect.bottom() : fromRect.top());
        end = QPointF(toRect.center().x(),
                      dy >= 0 ? toRect.top() : toRect.bottom());
    }

    plan.path.moveTo(start);
    const bool sameRow =
        std::abs(fromRect.center().y() - toRect.center().y()) < 1.0;
    const bool sameColumn =
        std::abs(fromRect.center().x() - toRect.center().x()) < 1.0;
    if (sameRow && lane == 0) {
        plan.path.lineTo(end);
        plan.labelCenter = pathLabelPoint(plan.path, 0.5, 24.0, -1);
    } else if (sameRow) {
        const int side = lane > 0 ? -1 : 1;
        const qreal bend = side * (32.0 + std::abs(lane) * 16.0);
        const QPointF mid = (start + end) / 2.0;
        plan.path.quadTo(QPointF(mid.x(), mid.y() + bend), end);
        plan.labelCenter =
            QPointF(mid.x(), mid.y() + bend + side * 22.0);
    } else if (sameColumn) {
        const int side = lane >= 0 ? 1 : -1;
        const qreal detourX = start.x() + side * (54.0 + std::abs(lane) * 18.0);
        plan.path.lineTo(QPointF(detourX, start.y()));
        plan.path.lineTo(QPointF(detourX, end.y()));
        plan.path.lineTo(end);
        plan.labelCenter = pathLabelPoint(plan.path, 0.5, 24.0, side);
    } else {
        const qreal midX = (start.x() + end.x()) / 2.0
            + lane * 24.0;
        plan.path.lineTo(QPointF(midX, start.y()));
        plan.path.lineTo(QPointF(midX, end.y()));
        plan.path.lineTo(end);
        plan.labelCenter =
            pathLabelPoint(plan.path, 0.52, 24.0, dy >= 0.0 ? -1 : 1);
    }
    plan.arrowTip = end;
    plan.arrowAngle = painterPathTangentAngleAtEnd(plan.path);
    return plan;
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

void fitFsmBodyRect(InsightGraphView* view, const QRectF& rect)
{
    if (!view || rect.isEmpty())
        return;
    view->fitRect(rect, Qt::KeepAspectRatio);
    if (view->currentZoom() >= kFsmInitialFitMinScale)
        return;
    view->resetView();
    view->zoomBy(kFsmInitialFitMinScale);
    view->centerOnRect(rect);
}

FsmGraphRenderResult renderFsmStateMachineGraph(
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
    QRectF bodyBounds = layout.bounds;
    if (layout.orderedStates.isEmpty())
        return {graphBounds, bodyBounds};

    const qreal titleTop = layout.bounds.top() - 86.0;
    const qreal captionTop = layout.bounds.top() - 56.0;

    QFont titleFont = InsightVisualStyle::titleFont(font);
    titleFont.setPointSize(qMax(10, titleFont.pointSize() + 1));
    auto* titleItem = scene->addSimpleText(title, titleFont);
    titleItem->setBrush(QBrush(InsightVisualStyle::theme().textPrimary));
    titleItem->setPos(layout.bounds.left(), titleTop);
    graphBounds = graphBounds.united(titleItem->sceneBoundingRect());

    QFont captionFont = font;
    captionFont.setPointSize(qMax(8, captionFont.pointSize() - 1));
    const QString caption = graph.nextStateSignalDisplayName.isEmpty()
        ? graph.stateRegisterDisplayName
        : QStringLiteral("%1  ->  %2")
              .arg(graph.stateRegisterDisplayName,
                   graph.nextStateSignalDisplayName);
    auto* captionItem = scene->addSimpleText(caption, captionFont);
    captionItem->setBrush(QBrush(InsightVisualStyle::theme().textSecondary));
    captionItem->setPos(layout.bounds.left(), captionTop);
    graphBounds = graphBounds.united(captionItem->sceneBoundingRect());

    QSet<QString> aliasCanonicalStates;
    for (const FsmLayoutNode& node : std::as_const(layout.nodes)) {
        if (node.alias)
            aliasCanonicalStates.insert(node.canonicalState);
    }

    auto addStateNode = [&](const FsmStateRow& row,
                            const QRectF& rect,
                            bool dashed,
                            bool alias,
                            const QString& aliasDetail) {
        RtlInsightGraphElement state;
        state.kind = alias
            ? QStringLiteral("state-alias")
            : QStringLiteral("state");
        state.primary = row.stateDisplayName;
        if (alias && row.deadEndState) {
            state.secondary = QStringLiteral("alias; dead/end");
        } else if (alias) {
            state.secondary = QStringLiteral("alias");
        } else if (row.deadEndState) {
            state.secondary = QStringLiteral("dead/end");
        }
        state.detail = alias ? aliasDetail : fsmStateDetailText(row);
        if (alias && row.deadEndState)
            state.detail = QStringLiteral("%1; %2")
                .arg(aliasDetail, row.statusDisplayName);
        state.codeLink =
            fsmStateTransitionCodeLink(graph, row.stateDisplayName, row.codeLink);
        const QColor fill = row.deadEndState
            ? InsightVisualStyle::theme().statusBar.errorBackground
            : (aliasCanonicalStates.contains(row.stateDisplayName)
                   ? fsmAliasStateFillColor(row.stateDisplayName)
                   : fsmNeutralStateFillColor());
        const QColor stroke = row.deadEndState
            ? InsightVisualStyle::theme().statusBar.errorText
            : (aliasCanonicalStates.contains(row.stateDisplayName)
                   ? fsmAliasStateStrokeColor(row.stateDisplayName)
                   : fsmNeutralStateStrokeColor());
        auto* item = new RtlInsightGraphStateNodeItem(
            state,
            rect,
            fill,
            stroke,
            font,
            dashed);
        item->navigateHandler = navigate;
        item->selectHandler = select;
        scene->addItem(item);
        return item;
    };

    for (const FsmLayoutNode& node : std::as_const(layout.nodes)) {
        const QString stateName = node.canonicalState;
        const FsmStateRow row = layout.stateRowsByName.value(stateName);
        addStateNode(row,
                     node.rect,
                     node.alias,
                     node.alias,
                     node.aliasDetail);
    }

    auto directedPairKey = [](const QString& from, const QString& to) {
        return from + QLatin1Char('\n') + to;
    };
    QSet<QString> directedPairs;
    for (const FsmTransitionRow& row : graph.transitionRows) {
        if (row.fromStateDisplayName == row.toStateDisplayName)
            continue;
        directedPairs.insert(directedPairKey(row.fromStateDisplayName,
                                             row.toStateDisplayName));
    }
    QHash<int, int> outerLaneCountsBySide;

    auto addTransition = [&](const FsmTransitionRow& row,
                             const QString& conditionId,
                             const QRectF& fromRect,
                             const QRectF& toRect,
                             int lane,
                             bool toAlias,
                             int fromColumn,
                             int fromLane,
                             int toColumn,
                             int toLane) {
        FsmTransitionRouteKind routeKind = FsmTransitionRouteKind::Normal;
        int routeLane = lane;
        if (row.fromStateDisplayName == row.toStateDisplayName) {
            routeKind = FsmTransitionRouteKind::SelfLoop;
        } else {
            const bool mutualPair =
                directedPairs.contains(directedPairKey(row.toStateDisplayName,
                                                       row.fromStateDisplayName));
            const int span = std::abs(toColumn - fromColumn);
            const bool backward = toColumn < fromColumn;
            const bool crossLane = fromLane != toLane;
            if (mutualPair && !toAlias) {
                routeKind = fromColumn > toColumn
                    ? FsmTransitionRouteKind::ReversePairUpper
                    : FsmTransitionRouteKind::ReversePairLower;
                if (routeLane == 0)
                    routeLane = routeKind
                            == FsmTransitionRouteKind::ReversePairUpper
                        ? 1
                        : -1;
            } else if (!toAlias
                       && (backward || span >= 3
                           || (span >= 2 && crossLane))) {
                const int preferredSide =
                    (fromLane < 0 || toLane < 0) ? -1 : 1;
                const int sideCount =
                    outerLaneCountsBySide.value(preferredSide) + 1;
                outerLaneCountsBySide.insert(preferredSide, sideCount);
                routeKind = FsmTransitionRouteKind::OuterBackEdge;
                routeLane = preferredSide * sideCount;
            }
        }
        const FsmTransitionRoutePlan route =
            buildFsmTransitionRoutePlan(fromRect,
                                        toRect,
                                        layout.bounds,
                                        routeKind,
                                        routeLane);

        RtlInsightGraphElement transition;
        transition.kind = QStringLiteral("transition");
        transition.primary = row.fromStateDisplayName;
        transition.secondary = row.toStateDisplayName;
        transition.detail = row.conditionDisplayName;
        transition.badge = conditionId;
        transition.codeLink = row.codeLink;
        auto* item = new RtlInsightGraphEdgeItem(
            transition,
            route.path,
            route.arrowTip,
            route.arrowAngle,
            conditionId,
            font,
            InsightVisualStyle::theme().textPrimary,
            0.0,
            true,
            false,
            InsightVisualStyle::theme().textPrimary,
            RtlInsightGraphEdgePresentation{
                route.labelCenter,
                true,
                fsmTransitionRouteKindName(route.kind),
                route.lane});
        item->navigateHandler = navigate;
        item->selectHandler = select;
        scene->addItem(item);
        graphBounds = graphBounds.united(item->sceneBoundingRect());
        bodyBounds = bodyBounds.united(item->sceneBoundingRect());
    };

    QHash<QString, int> edgeLaneCounts;
    for (int rowIndex = 0; rowIndex < graph.transitionRows.size(); ++rowIndex) {
        const FsmTransitionRow& row = graph.transitionRows.at(rowIndex);
        const QString aliasNodeId =
            layout.aliasNodeIdByTransitionKey.value(
                fsmTransitionLayoutKey(row));
        if (!layout.stateRects.contains(row.fromStateDisplayName)
            || (!aliasNodeId.isEmpty()
                ? !layout.nodeRects.contains(aliasNodeId)
                : !layout.stateRects.contains(row.toStateDisplayName))) {
            continue;
        }
        const QRectF fromRect = layout.stateRects.value(row.fromStateDisplayName);
        const QRectF toRect = aliasNodeId.isEmpty()
            ? layout.stateRects.value(row.toStateDisplayName)
            : layout.nodeRects.value(aliasNodeId);
        const QString edgeKey =
            row.fromStateDisplayName + QLatin1Char('\n')
            + (aliasNodeId.isEmpty() ? row.toStateDisplayName : aliasNodeId);
        const int ordinal = edgeLaneCounts.value(edgeKey);
        edgeLaneCounts.insert(edgeKey, ordinal + 1);
        const int lane = ordinal == 0 ? 0 : ((ordinal + 1) / 2)
            * (ordinal % 2 == 0 ? -1 : 1);
        const QString fromNodeId =
            layout.canonicalNodeIds.value(row.fromStateDisplayName);
        const QString toNodeId =
            aliasNodeId.isEmpty()
            ? layout.canonicalNodeIds.value(row.toStateDisplayName)
            : aliasNodeId;
        addTransition(row,
                      fsmTransitionConditionId(rowIndex),
                      fromRect,
                      toRect,
                      lane,
                      !aliasNodeId.isEmpty(),
                      layout.nodeColumns.value(
                          fromNodeId,
                          layout.stateColumns.value(row.fromStateDisplayName)),
                      layout.nodeLanes.value(
                          fromNodeId,
                          layout.stateLanes.value(row.fromStateDisplayName)),
                      layout.nodeColumns.value(
                          toNodeId,
                          layout.stateColumns.value(row.toStateDisplayName)),
                      layout.nodeLanes.value(
                          toNodeId,
                          layout.stateLanes.value(row.toStateDisplayName)));
    }

    return {graphBounds.adjusted(-38.0, -58.0, 38.0, 44.0),
            expandedToMinimum(bodyBounds.adjusted(-34.0, -12.0, 34.0, 34.0),
                              420.0,
                              240.0)};
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
    panel->setObjectName(QStringLiteral("rtlInsightsPanel"));
    InsightVisualStyle::applyPanel(panel);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto* titleLabel = new QLabel(QStringLiteral("RTL Insights"), panel);
    titleLabel->setObjectName(QStringLiteral("rtlInsightsTitle"));
    InsightVisualStyle::applyTitleLabel(titleLabel);
    layout->addWidget(titleLabel);

    auto* actionLayout = new QHBoxLayout;
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(4);
    moduleBriefButton = new QPushButton(QStringLiteral("Module Brief"), panel);
    moduleBriefButton->setObjectName(QStringLiteral("rtlModuleBriefButton"));
    signalJourneyButton = new QPushButton(QStringLiteral("Signal Journey"), panel);
    signalJourneyButton->setObjectName(QStringLiteral("rtlSignalJourneyButton"));
    signalUsageHotspotButton =
        new QPushButton(QStringLiteral("Usage Hotspot"), panel);
    signalUsageHotspotButton->setObjectName(
        QStringLiteral("rtlSignalUsageHotspotButton"));
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
    graphSearchEdit = new QLineEdit(panel);
    graphSearchEdit->setObjectName(QStringLiteral("rtlGraphSearchEdit"));
    graphSearchEdit->setPlaceholderText(QStringLiteral("Search graph"));
    InsightVisualStyle::applySearchField(graphSearchEdit);
    moduleBlockTopCombo = new QComboBox(panel);
    moduleBlockTopCombo->setObjectName(QStringLiteral("rtlModuleBlockTopCombo"));
    moduleBlockTopCombo->setMinimumWidth(150);
    moduleBlockSetSelectionButton =
        new QPushButton(QStringLiteral("Set from selection"), panel);
    moduleBlockSetSelectionButton->setObjectName(
        QStringLiteral("rtlModuleBlockSetSelectionButton"));
    moduleBlockDepthSpin = new QSpinBox(panel);
    moduleBlockDepthSpin->setObjectName(QStringLiteral("rtlModuleBlockDepthSpin"));
    moduleBlockDepthSpin->setRange(0, 8);
    moduleBlockDepthSpin->setValue(2);
    moduleBlockDepthSpin->setPrefix(QStringLiteral("Depth "));
    moduleBlockCollapsePackagesCheck =
        new QCheckBox(QStringLiteral("Collapse packages"), panel);
    moduleBlockCollapsePackagesCheck->setObjectName(
        QStringLiteral("rtlModuleBlockCollapsePackagesCheck"));
    moduleBlockShowUnresolvedCheck =
        new QCheckBox(QStringLiteral("Show unresolved"), panel);
    moduleBlockShowUnresolvedCheck->setObjectName(
        QStringLiteral("rtlModuleBlockShowUnresolvedCheck"));
    moduleBlockShowUnresolvedCheck->setChecked(true);

    stateTransitionSignalCombo = new QComboBox(panel);
    stateTransitionSignalCombo->setObjectName(
        QStringLiteral("rtlStateTransitionSignalCombo"));
    stateTransitionCurrentCombo = new QComboBox(panel);
    stateTransitionCurrentCombo->setObjectName(
        QStringLiteral("rtlStateTransitionCurrentCombo"));
    stateTransitionNextCombo = new QComboBox(panel);
    stateTransitionNextCombo->setObjectName(
        QStringLiteral("rtlStateTransitionNextCombo"));
    stateTransitionResetCheck =
        new QCheckBox(QStringLiteral("Show reset"), panel);
    stateTransitionResetCheck->setObjectName(
        QStringLiteral("rtlStateTransitionResetCheck"));
    stateTransitionResetCheck->setChecked(true);
    stateTransitionErrorCheck =
        new QCheckBox(QStringLiteral("Show error"), panel);
    stateTransitionErrorCheck->setObjectName(
        QStringLiteral("rtlStateTransitionErrorCheck"));
    stateTransitionErrorCheck->setChecked(true);
    stateTransitionUnreachableCheck =
        new QCheckBox(QStringLiteral("Show unreachable"), panel);
    stateTransitionUnreachableCheck->setObjectName(
        QStringLiteral("rtlStateTransitionUnreachableCheck"));
    stateTransitionUnreachableCheck->setChecked(true);

    graphLayoutCombo = new QComboBox(panel);
    graphLayoutCombo->setObjectName(QStringLiteral("rtlGraphLayoutCombo"));
    graphLayoutCombo->addItems({QStringLiteral("Nested blocks"),
                                QStringLiteral("State flow"),
                                QStringLiteral("Tree")});
    graphMoreButton = new QToolButton(panel);
    graphMoreButton->setObjectName(QStringLiteral("rtlGraphMoreButton"));
    graphMoreButton->setText(QStringLiteral("..."));
    graphMoreButton->setPopupMode(QToolButton::InstantPopup);
    graphMoreButton->setToolTip(QStringLiteral("More graph actions"));
    auto* graphMoreMenu = new QMenu(graphMoreButton);
    graphMoreMenu->addAction(QStringLiteral("Jump"),
                             graphMoreButton,
                             [this]() { navigateSelectedGraphItem(); });
    graphMoreMenu->addAction(QStringLiteral("Focus"),
                             graphMoreButton,
                             [this]() {
                                 if (!insightsGraphScene || !insightsGraphView)
                                     return;
                                 const QList<QGraphicsItem*> selected =
                                     insightsGraphScene->selectedItems();
                                 if (!selected.isEmpty())
                                     insightsGraphView->centerOnRect(
                                         selected.first()->sceneBoundingRect());
                             });
    graphMoreMenu->addAction(QStringLiteral("Set top"),
                             graphMoreButton,
                             [this]() { setModuleBlockTopFromSelected(); });
    graphMoreButton->setMenu(graphMoreMenu);
    for (QPushButton* button :
         {moduleBriefButton,
          signalJourneyButton,
          signalUsageHotspotButton,
          clockResetButton,
          fsmGraphButton,
          moduleBlockDiagramButton,
          moduleBlockSetSelectionButton,
          graphZoomOutButton,
          graphFitButton,
          graphZoomInButton}) {
        InsightVisualStyle::applyToolbarButton(button);
    }
    for (QCheckBox* checkBox :
         {moduleBlockCollapsePackagesCheck,
          moduleBlockShowUnresolvedCheck,
          stateTransitionResetCheck,
          stateTransitionErrorCheck,
          stateTransitionUnreachableCheck}) {
        InsightVisualStyle::applySegmentedCheckBox(checkBox);
    }
    actionLayout->addWidget(moduleBriefButton);
    actionLayout->addWidget(signalJourneyButton);
    actionLayout->addWidget(signalUsageHotspotButton);
    actionLayout->addWidget(clockResetButton);
    actionLayout->addWidget(fsmGraphButton);
    actionLayout->addWidget(moduleBlockDiagramButton);
    actionLayout->addStretch(1);
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
    insightsGraphView = new InsightGraphView(insightsGraphScene, panel);
    insightsGraphView->setObjectName(QStringLiteral("rtlInsightsGraphView"));
    insightsGraphView->applyInsightGraphStyle();
    insightsGraphView->setZoomRange(kGraphMinScale, kGraphMaxScale);
    insightsGraphView->setGridVisible(true);
    insightsGraphView->setClearSelectionOnEmptyLeftClick(true);

    insightsGraphPanel = new QWidget(panel);
    insightsGraphPanel->setObjectName(QStringLiteral("rtlInsightsGraphPanel"));
    auto* graphPanelLayout = new QVBoxLayout(insightsGraphPanel);
    graphPanelLayout->setContentsMargins(0, 0, 0, 0);
    graphPanelLayout->setSpacing(6);

    auto* graphToolbarLayout = new QHBoxLayout;
    graphToolbarLayout->setContentsMargins(0, 0, 0, 0);
    graphToolbarLayout->setSpacing(6);
    auto* moduleBlockTopLabel = new QLabel(QStringLiteral("Top:"), panel);
    moduleBlockTopLabel->setObjectName(QStringLiteral("rtlModuleBlockTopLabel"));
    auto* stateSignalLabel = new QLabel(QStringLiteral("Signal:"), panel);
    stateSignalLabel->setObjectName(QStringLiteral("rtlStateSignalLabel"));
    auto* stateCurrentLabel = new QLabel(QStringLiteral("Current:"), panel);
    stateCurrentLabel->setObjectName(QStringLiteral("rtlStateCurrentLabel"));
    auto* stateNextLabel = new QLabel(QStringLiteral("Next:"), panel);
    stateNextLabel->setObjectName(QStringLiteral("rtlStateNextLabel"));
    graphToolbarLayout->addWidget(moduleBlockTopLabel);
    graphToolbarLayout->addWidget(moduleBlockTopCombo);
    graphToolbarLayout->addWidget(stateSignalLabel);
    graphToolbarLayout->addWidget(stateTransitionSignalCombo);
    graphToolbarLayout->addWidget(stateCurrentLabel);
    graphToolbarLayout->addWidget(stateTransitionCurrentCombo);
    graphToolbarLayout->addWidget(stateNextLabel);
    graphToolbarLayout->addWidget(stateTransitionNextCombo);
    graphToolbarLayout->addWidget(graphSearchEdit, 1);
    graphToolbarLayout->addWidget(moduleBlockSetSelectionButton);
    graphToolbarLayout->addWidget(graphFitButton);
    graphToolbarLayout->addWidget(moduleBlockDepthSpin);
    graphToolbarLayout->addWidget(moduleBlockCollapsePackagesCheck);
    graphToolbarLayout->addWidget(moduleBlockShowUnresolvedCheck);
    graphToolbarLayout->addWidget(stateTransitionResetCheck);
    graphToolbarLayout->addWidget(stateTransitionErrorCheck);
    graphToolbarLayout->addWidget(stateTransitionUnreachableCheck);
    graphToolbarLayout->addWidget(graphLayoutCombo);
    graphToolbarLayout->addWidget(graphZoomOutButton);
    graphToolbarLayout->addWidget(graphZoomInButton);
    graphToolbarLayout->addWidget(graphMoreButton);
    graphPanelLayout->addLayout(graphToolbarLayout);

    graphInspector = new QTreeWidget(panel);
    graphInspector->setObjectName(QStringLiteral("rtlGraphInspector"));
    graphInspector->setColumnCount(2);
    graphInspector->setHeaderLabels({QStringLiteral("Field"),
                                     QStringLiteral("Value")});
    graphInspector->setRootIsDecorated(false);
    graphInspector->setAlternatingRowColors(true);
    graphInspector->setUniformRowHeights(true);
    graphInspector->header()->setSectionResizeMode(0,
                                                   QHeaderView::ResizeToContents);
    graphInspector->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    graphInspector->setMinimumWidth(260);
    graphInspectorJumpButton =
        new QPushButton(QStringLiteral("Jump"), panel);
    graphInspectorJumpButton->setObjectName(
        QStringLiteral("rtlGraphInspectorJumpButton"));
    graphInspectorFocusButton =
        new QPushButton(QStringLiteral("Focus"), panel);
    graphInspectorFocusButton->setObjectName(
        QStringLiteral("rtlGraphInspectorFocusButton"));
    graphInspectorSetTopButton =
        new QPushButton(QStringLiteral("Set Top"), panel);
    graphInspectorSetTopButton->setObjectName(
        QStringLiteral("rtlGraphInspectorSetTopButton"));
    graphInspectorRevealButton =
        new QPushButton(QStringLiteral("Reveal"), panel);
    graphInspectorRevealButton->setObjectName(
        QStringLiteral("rtlGraphInspectorRevealButton"));
    for (QPushButton* button :
         {graphInspectorJumpButton,
          graphInspectorFocusButton,
          graphInspectorSetTopButton,
          graphInspectorRevealButton}) {
        InsightVisualStyle::applyToolbarButton(button);
    }
    graphInspectorRevealButton->setEnabled(false);
    graphInspectorRevealButton->setToolTip(
        QStringLiteral("Hierarchy reveal is not wired for this panel yet."));

    auto* inspectorPanel = new QWidget(panel);
    inspectorPanel->setObjectName(QStringLiteral("rtlGraphInspectorPanel"));
    auto* inspectorLayout = new QVBoxLayout(inspectorPanel);
    inspectorLayout->setContentsMargins(0, 0, 0, 0);
    inspectorLayout->setSpacing(5);
    inspectorLayout->addWidget(graphInspector, 1);
    auto* inspectorActionLayout = new QHBoxLayout;
    inspectorActionLayout->setContentsMargins(0, 0, 0, 0);
    inspectorActionLayout->setSpacing(4);
    inspectorActionLayout->addWidget(graphInspectorJumpButton);
    inspectorActionLayout->addWidget(graphInspectorFocusButton);
    inspectorActionLayout->addWidget(graphInspectorSetTopButton);
    inspectorActionLayout->addWidget(graphInspectorRevealButton);
    inspectorLayout->addLayout(inspectorActionLayout);

    auto* graphBodySplitter = new QSplitter(Qt::Horizontal, panel);
    graphBodySplitter->setObjectName(QStringLiteral("rtlGraphBodySplitter"));
    graphBodySplitter->addWidget(insightsGraphView);
    graphBodySplitter->addWidget(inspectorPanel);
    graphBodySplitter->setStretchFactor(0, 1);
    graphBodySplitter->setStretchFactor(1, 0);
    graphPanelLayout->addWidget(graphBodySplitter, 1);

    graphTable = new QTableWidget(panel);
    graphTable->setObjectName(QStringLiteral("rtlGraphDetailTable"));
    graphTable->setAlternatingRowColors(true);
    graphTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    graphTable->setSelectionMode(QAbstractItemView::SingleSelection);
    graphTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    graphTable->verticalHeader()->setVisible(false);
    graphTable->setMinimumHeight(118);
    graphTable->setMaximumHeight(190);
    graphPanelLayout->addWidget(graphTable, 0);

    signalUsageHotspotPanel = new SignalUsageHotspotPanel(panel);

    insightsStack = new QStackedWidget(panel);
    insightsStack->addWidget(insightsTree);
    insightsStack->addWidget(insightsGraphPanel);
    insightsStack->addWidget(signalUsageHotspotPanel);
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
                         if (!navigationHandler(fileName, line, column)
                             && statusMessageHandler) {
                             statusMessageHandler(
                                 QStringLiteral("RTL Insights jump failed"),
                                 4000);
                         }
                     });
    QObject::connect(moduleBriefButton, &QPushButton::clicked,
                     insightsDock, [this]() { showModuleBrief(); });
    QObject::connect(signalJourneyButton, &QPushButton::clicked,
                     insightsDock, [this]() { showSignalJourney(); });
    QObject::connect(signalUsageHotspotButton, &QPushButton::clicked,
                     insightsDock, [this]() { showSignalUsageHotspot(); });
    QObject::connect(clockResetButton, &QPushButton::clicked,
                     insightsDock, [this]() { showClockResetDomainMap(); });
    QObject::connect(fsmGraphButton, &QPushButton::clicked,
                     insightsDock, [this]() { showFsmGraph(); });
    QObject::connect(moduleBlockDiagramButton, &QPushButton::clicked,
                     insightsDock, [this]() { showModuleBlockDiagram(); });
    QObject::connect(graphZoomOutButton, &QPushButton::clicked,
                     insightsDock, [this]() {
                        if (insightsGraphView)
                             insightsGraphView->zoomOut();
                     });
    QObject::connect(graphZoomInButton, &QPushButton::clicked,
                     insightsDock, [this]() {
                        if (insightsGraphView)
                             insightsGraphView->zoomIn();
                     });
    QObject::connect(graphFitButton, &QPushButton::clicked,
                     insightsDock, [this]() {
                         if (insightsGraphView)
                             insightsGraphView->fitScene(Qt::KeepAspectRatio);
                     });
    QObject::connect(graphSearchEdit,
                     &QLineEdit::textChanged,
                     insightsDock,
                     [this](const QString& text) {
                         graphSearchText = text.trimmed();
                         applyGraphSearchHighlight();
                     });
    QObject::connect(moduleBlockSetSelectionButton,
                     &QPushButton::clicked,
                     insightsDock,
                     [this]() {
                         if (!setModuleBlockTopFromSelected()
                             && statusMessageHandler) {
                             statusMessageHandler(
                                 QStringLiteral("Select a resolved module block first"),
                                 1800);
                             }
                     });
    QObject::connect(moduleBlockTopCombo,
                     qOverload<int>(&QComboBox::activated),
                     insightsDock,
                     [this](int index) {
                         if (!moduleBlockTopCombo
                             || currentGraphMode != QStringLiteral("module-block")) {
                             return;
                         }
                         const int nodeId =
                             moduleBlockTopCombo->itemData(index).toInt();
                         for (const ModuleBlockDiagramNode& node :
                              currentModuleBlockReport.nodes) {
                             if (node.nodeId != nodeId
                                 || node.unresolved
                                 || node.definitionCodeLink.fileName.isEmpty()) {
                                 continue;
                             }
                             showModuleBlockDiagramForModule(
                                 node.definitionCodeLink.fileName,
                                 node.moduleDisplayName);
                             return;
                         }
                     });
    QObject::connect(moduleBlockDepthSpin,
                     qOverload<int>(&QSpinBox::valueChanged),
                     insightsDock,
                     [this](int) {
                         if (currentGraphMode == QStringLiteral("module-block"))
                             showModuleBlockDiagram();
                     });
    QObject::connect(moduleBlockShowUnresolvedCheck,
                     &QCheckBox::toggled,
                     insightsDock,
                     [this](bool) {
                         if (currentGraphMode == QStringLiteral("module-block"))
                             renderModuleBlockDiagramScene(currentModuleBlockReport);
                     });
    QObject::connect(graphLayoutCombo,
                     qOverload<int>(&QComboBox::currentIndexChanged),
                     insightsDock,
                     [this](int) {
                         if (currentGraphMode == QStringLiteral("module-block"))
                             renderModuleBlockDiagramScene(currentModuleBlockReport);
                     });
    QObject::connect(graphTable,
                     &QTableWidget::cellClicked,
                     insightsDock,
                     [this](int row, int) {
                         if (!graphTable || row < 0)
                             return;
                         const QTableWidgetItem* item =
                             graphTable->item(row, 0);
                         if (!item)
                             return;
                         if (currentGraphMode == QStringLiteral("module-block"))
                             selectModuleBlockNode(
                                 item->data(kGraphNodeIdRole).toInt());
                     });
    QObject::connect(graphTable,
                     &QTableWidget::cellDoubleClicked,
                     insightsDock,
                     [this](int row, int) {
                         if (!graphTable || row < 0)
                             return;
                         graphTable->selectRow(row);
                         if (currentGraphMode == QStringLiteral("module-block")) {
                             const QTableWidgetItem* item =
                                 graphTable->item(row, 0);
                             if (item) {
                                 selectModuleBlockNode(
                                     item->data(kGraphNodeIdRole).toInt(),
                                     false,
                                     false);
                             }
                         }
                         navigateSelectedGraphItem();
                     });
    QObject::connect(graphInspectorJumpButton,
                     &QPushButton::clicked,
                     insightsDock,
                     [this]() { navigateSelectedGraphItem(); });
    QObject::connect(graphInspectorFocusButton,
                     &QPushButton::clicked,
                     insightsDock,
                     [this]() {
                         if (!insightsGraphScene || !insightsGraphView)
                             return;
                         const QList<QGraphicsItem*> selected =
                             insightsGraphScene->selectedItems();
                         if (!selected.isEmpty())
                             insightsGraphView->centerOnRect(
                                 selected.first()->sceneBoundingRect());
                     });
    QObject::connect(graphInspectorSetTopButton,
                     &QPushButton::clicked,
                     insightsDock,
                     [this]() { setModuleBlockTopFromSelected(); });

    renderNoContext();
}

void RtlInsightsPanelCoordinator::setNavigationHandler(
    std::function<bool(const QString&, int, int)> handler)
{
    navigationHandler = std::move(handler);
    if (signalUsageHotspotPanel)
        signalUsageHotspotPanel->setNavigationHandler(navigationHandler);
}

void RtlInsightsPanelCoordinator::setStatusMessageHandler(
    std::function<void(const QString&, int)> handler)
{
    statusMessageHandler = std::move(handler);
    if (signalUsageHotspotPanel)
        signalUsageHotspotPanel->setStatusMessageHandler(statusMessageHandler);
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

QStringList RtlInsightsPanelCoordinator::graphElementSummariesForTest() const
{
    QStringList summaries;
    if (!insightsGraphScene)
        return summaries;
    for (QGraphicsItem* item : insightsGraphScene->items()) {
        if (!dynamic_cast<RtlInsightGraphNodeItem*>(item)
            && !dynamic_cast<RtlInsightGraphStateNodeItem*>(item)
            && !dynamic_cast<RtlInsightGraphEdgeItem*>(item)) {
            continue;
        }
        const QRectF rect = item->sceneBoundingRect();
        summaries.append(
            QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8")
                .arg(item->data(kGraphKindRole).toString(),
                     item->data(kGraphPrimaryRole).toString(),
                     item->data(kGraphSecondaryRole).toString(),
                     item->data(kGraphDetailRole).toString(),
                     QString::number(qRound(rect.center().x())),
                     QString::number(qRound(rect.center().y())),
                     QString::number(qRound(rect.width())),
                     QString::number(qRound(rect.height()))));
    }
    summaries.sort(Qt::CaseInsensitive);
    return summaries;
}

QStringList RtlInsightsPanelCoordinator::graphElementVisualSummariesForTest() const
{
    QStringList summaries;
    if (!insightsGraphScene)
        return summaries;
    auto penStyleName = [](Qt::PenStyle style) {
        switch (style) {
        case Qt::NoPen:
            return QStringLiteral("none");
        case Qt::DashLine:
            return QStringLiteral("dash");
        case Qt::DotLine:
            return QStringLiteral("dot");
        default:
            return QStringLiteral("solid");
        }
    };
    for (QGraphicsItem* item : insightsGraphScene->items()) {
        if (!isGraphShapeItem(item))
            continue;
        QColor fill = graphItemFillColor(item);
        QColor stroke = graphItemPenColor(item);
        Qt::PenStyle style = Qt::SolidLine;
        if (const auto* rect = dynamic_cast<const QGraphicsRectItem*>(item))
            style = rect->pen().style();
        else if (const auto* ellipse =
                     dynamic_cast<const QGraphicsEllipseItem*>(item))
            style = ellipse->pen().style();
        else if (const auto* path =
                     dynamic_cast<const QGraphicsPathItem*>(item))
            style = path->pen().style();
        summaries.append(
            QStringLiteral("%1|%2|%3|%4|%5|%6|%7")
                .arg(item->data(kGraphKindRole).toString(),
                     item->data(kGraphPrimaryRole).toString(),
                     item->data(kGraphSecondaryRole).toString(),
                     item->data(kGraphDetailRole).toString(),
                     fill.isValid() ? fill.name() : QString(),
                     stroke.isValid() ? stroke.name() : QString(),
                     penStyleName(style)));
    }
    summaries.sort(Qt::CaseInsensitive);
    return summaries;
}

QStringList RtlInsightsPanelCoordinator::graphEdgeGeometrySummariesForTest() const
{
    QStringList summaries;
    if (!insightsGraphScene)
        return summaries;
    QList<QRectF> nodeRects;
    QList<QGraphicsItem*> edgeItems;
    for (QGraphicsItem* item : insightsGraphScene->items()) {
        if (dynamic_cast<RtlInsightGraphNodeItem*>(item)
            || dynamic_cast<RtlInsightGraphStateNodeItem*>(item)) {
            nodeRects.append(item->sceneBoundingRect());
        } else if (dynamic_cast<RtlInsightGraphEdgeItem*>(item)) {
            edgeItems.append(item);
        }
    }
    for (QGraphicsItem* item : std::as_const(edgeItems)) {
        const auto* edgeItem = dynamic_cast<QGraphicsPathItem*>(item);
        if (!edgeItem)
            continue;
        const QPainterPath path = edgeItem->path();
        const QRectF rect = item->sceneBoundingRect();
        const int angleDegrees =
            qRound(item->data(kGraphEdgeArrowAngleRole).toDouble()
                   * 180.0 / kPi);
        const QVariant labelX = item->data(kGraphEdgeLabelXRole);
        const QVariant labelY = item->data(kGraphEdgeLabelYRole);
        const int labelDistance = labelX.isValid() && labelY.isValid()
            ? qRound(distancePointToPath(QPointF(labelX.toDouble(),
                                                 labelY.toDouble()),
                                         path))
            : -1;
        const bool avoidsNodes =
            !pathSamplesIntersectNodeRects(path, nodeRects);
        qreal siblingSpacing = -1.0;
        for (QGraphicsItem* other : std::as_const(edgeItems)) {
            if (other == item)
                continue;
            const QString primary = item->data(kGraphPrimaryRole).toString();
            const QString secondary =
                item->data(kGraphSecondaryRole).toString();
            const QString otherPrimary =
                other->data(kGraphPrimaryRole).toString();
            const QString otherSecondary =
                other->data(kGraphSecondaryRole).toString();
            const bool samePair =
                (primary == otherPrimary && secondary == otherSecondary)
                || (primary == otherSecondary && secondary == otherPrimary);
            if (!samePair)
                continue;
            const auto* otherEdge =
                dynamic_cast<QGraphicsPathItem*>(other);
            if (!otherEdge)
                continue;
            const qreal spacing =
                distanceBetweenPaths(path, otherEdge->path());
            if (spacing >= 0.0) {
                siblingSpacing = siblingSpacing < 0.0
                    ? spacing
                    : qMin(siblingSpacing, spacing);
            }
        }
        summaries.append(
            QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9|%10|%11|%12|%13|%14|%15|%16|%17")
                .arg(item->data(kGraphKindRole).toString(),
                     item->data(kGraphPrimaryRole).toString(),
                     item->data(kGraphSecondaryRole).toString(),
                     item->data(kGraphBadgeRole).toString(),
                     item->data(kGraphEdgeLabelRole).toString(),
                     item->data(kGraphEdgeLabelBoldRole).toBool()
                         ? QStringLiteral("bold")
                         : QStringLiteral("normal"),
                     item->data(kGraphEdgeLabelBackgroundRole).toBool()
                         ? QStringLiteral("bg")
                         : QStringLiteral("no-bg"),
                     item->data(kGraphEdgeLabelColorRole).toString(),
                     item->data(kGraphEdgeHasCurveRole).toBool()
                         ? QStringLiteral("curve")
                         : QStringLiteral("line"),
                     QString::number(angleDegrees),
                     QString::number(qRound(rect.width())),
                     QString::number(qRound(rect.height())),
                     item->data(kGraphEdgeRouteKindRole).toString(),
                     QString::number(item->data(kGraphEdgeRouteLaneRole).toInt()),
                     QString::number(labelDistance),
                     avoidsNodes ? QStringLiteral("clear")
                                 : QStringLiteral("node-hit"),
                     QString::number(qRound(siblingSpacing))));
    }
    summaries.sort(Qt::CaseInsensitive);
    return summaries;
}

QRectF RtlInsightsPanelCoordinator::graphLastFitRectForTest() const
{
    return lastGraphFitRect;
}

qreal RtlInsightsPanelCoordinator::graphCurrentZoomForTest() const
{
    return insightsGraphView ? insightsGraphView->currentZoom() : 0.0;
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

int RtlInsightsPanelCoordinator::graphSelectedItemCountForTest() const
{
    if (!insightsGraphScene)
        return 0;
    int count = 0;
    for (QGraphicsItem* item : insightsGraphScene->items()) {
        if ((dynamic_cast<RtlInsightGraphNodeItem*>(item)
             || dynamic_cast<RtlInsightGraphStateNodeItem*>(item)
             || dynamic_cast<RtlInsightGraphEdgeItem*>(item))
            && item->isSelected()) {
            ++count;
        }
    }
    return count;
}

QStringList RtlInsightsPanelCoordinator::graphInspectorRowsForTest() const
{
    QStringList rows;
    if (!graphInspector)
        return rows;
    for (int i = 0; i < graphInspector->topLevelItemCount(); ++i) {
        const QTreeWidgetItem* item = graphInspector->topLevelItem(i);
        if (!item)
            continue;
        rows.append(QStringLiteral("%1=%2")
                        .arg(item->text(0), item->text(1)));
    }
    return rows;
}

QStringList RtlInsightsPanelCoordinator::graphTableRowsForTest() const
{
    QStringList rows;
    if (!graphTable)
        return rows;
    for (int row = 0; row < graphTable->rowCount(); ++row) {
        QStringList cells;
        for (int column = 0; column < graphTable->columnCount(); ++column) {
            const QTableWidgetItem* item = graphTable->item(row, column);
            cells.append(item ? item->text() : QString());
        }
        rows.append(cells.join(QLatin1Char('|')));
    }
    return rows;
}

bool RtlInsightsPanelCoordinator::graphItemsReadableForTest() const
{
    if (!insightsGraphScene)
        return true;
    for (QGraphicsItem* item : insightsGraphScene->items()) {
        if (!isGraphShapeItem(item))
            continue;
        if (!graphItemReadable(item))
            return false;
    }
    return true;
}

bool RtlInsightsPanelCoordinator::graphNestedNodeStackingReadableForTest() const
{
    if (!insightsGraphScene)
        return true;
    QList<QGraphicsItem*> nodes;
    for (QGraphicsItem* item : insightsGraphScene->items()) {
        if (isGraphNodeShapeItem(item))
            nodes.append(item);
    }
    for (QGraphicsItem* outer : std::as_const(nodes)) {
        const QRectF outerRect = outer->sceneBoundingRect();
        for (QGraphicsItem* inner : std::as_const(nodes)) {
            if (outer == inner)
                continue;
            const QRectF innerRect = inner->sceneBoundingRect();
            if (outerRect.width() <= innerRect.width() + 8.0
                || outerRect.height() <= innerRect.height() + 8.0) {
                continue;
            }
            if (!outerRect.adjusted(4.0, 4.0, -4.0, -4.0)
                     .contains(innerRect.center())) {
                continue;
            }
            if (outer->zValue() >= inner->zValue())
                return false;
        }
    }
    return true;
}

bool RtlInsightsPanelCoordinator::setGraphItemHoveredForTest(
    const QString& elementKind,
    const QString& primaryText,
    const QString& secondaryText,
    bool hovered)
{
    QGraphicsItem* item = graphItemByData(insightsGraphScene,
                                          elementKind,
                                          primaryText,
                                          secondaryText);
    if (!item)
        return false;
    if (auto* node = dynamic_cast<RtlInsightGraphNodeItem*>(item)) {
        node->setHoveredForTest(hovered);
        return true;
    }
    if (auto* state = dynamic_cast<RtlInsightGraphStateNodeItem*>(item)) {
        state->setHoveredForTest(hovered);
        return true;
    }
    if (auto* edge = dynamic_cast<RtlInsightGraphEdgeItem*>(item)) {
        edge->setHoveredForTest(hovered);
        return true;
    }
    return false;
}

bool RtlInsightsPanelCoordinator::selectGraphItemForTest(
    const QString& elementKind,
    const QString& primaryText,
    const QString& secondaryText)
{
    QGraphicsItem* item = graphItemByData(insightsGraphScene,
                                          elementKind,
                                          primaryText,
                                          secondaryText);
    if (!item)
        return false;
    item->setSelected(true);
    QStringList rows{
        QStringLiteral("Kind=%1").arg(item->data(kGraphKindRole).toString())};
    const QString badge = item->data(kGraphBadgeRole).toString();
    if (!badge.isEmpty())
        rows.append(QStringLiteral("ID=%1").arg(badge));
    rows.append(QStringLiteral("To=%1")
                    .arg(item->data(kGraphSecondaryRole).toString()));
    const QString detailLabel =
        item->data(kGraphKindRole).toString() == QStringLiteral("transition")
            ? QStringLiteral("Condition")
            : QStringLiteral("Detail");
    rows.append(QStringLiteral("%1=%2")
                    .arg(detailLabel,
                         item->data(kGraphDetailRole).toString()));
    rows.append(QStringLiteral("Location=%1:%2")
                    .arg(item->data(kGraphFileRole).toString(),
                         item->data(kGraphLineRole).toString()));
    renderGenericGraphInspector(primaryText, rows);
    return true;
}

bool RtlInsightsPanelCoordinator::selectGraphTableRowForTest(
    const QString& primaryText,
    const QString& secondaryText)
{
    if (!graphTable)
        return false;
    for (int row = 0; row < graphTable->rowCount(); ++row) {
        const QTableWidgetItem* item = graphTable->item(row, 0);
        if (!item)
            continue;
        if (item->data(kGraphTablePrimaryRole).toString() != primaryText)
            continue;
        if (!secondaryText.isEmpty()
            && item->data(kGraphTableSecondaryRole).toString()
                   != secondaryText) {
            continue;
        }
        graphTable->selectRow(row);
        if (currentGraphMode == QStringLiteral("module-block"))
            selectModuleBlockNode(item->data(kGraphNodeIdRole).toInt());
        return true;
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
    if (!navigationHandler(fileName,
                           item->data(kGraphLineRole).toInt(),
                           item->data(kGraphColumnRole).toInt())) {
        return false;
    }
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

void RtlInsightsPanelCoordinator::configureGraphToolbarForMode(
    const QString& mode)
{
    currentGraphMode = mode;
    const bool moduleMode = mode == QStringLiteral("module-block");
    const bool stateMode =
        mode == QStringLiteral("state-transition")
        || mode == QStringLiteral("fsm");
    const bool graphMode = moduleMode || stateMode;

    for (QWidget* widget :
         {static_cast<QWidget*>(moduleBlockTopCombo),
          insightsGraphPanel
              ? insightsGraphPanel->findChild<QWidget*>(
                    QStringLiteral("rtlModuleBlockTopLabel"))
              : nullptr,
          static_cast<QWidget*>(moduleBlockSetSelectionButton),
          static_cast<QWidget*>(moduleBlockDepthSpin),
          static_cast<QWidget*>(moduleBlockCollapsePackagesCheck),
          static_cast<QWidget*>(moduleBlockShowUnresolvedCheck)}) {
        if (widget)
            widget->setVisible(moduleMode);
    }
    for (QWidget* widget :
         {insightsGraphPanel
              ? insightsGraphPanel->findChild<QWidget*>(
                    QStringLiteral("rtlStateSignalLabel"))
              : nullptr,
          static_cast<QWidget*>(stateTransitionSignalCombo),
          insightsGraphPanel
              ? insightsGraphPanel->findChild<QWidget*>(
                    QStringLiteral("rtlStateCurrentLabel"))
              : nullptr,
          static_cast<QWidget*>(stateTransitionCurrentCombo),
          insightsGraphPanel
              ? insightsGraphPanel->findChild<QWidget*>(
                    QStringLiteral("rtlStateNextLabel"))
              : nullptr,
          static_cast<QWidget*>(stateTransitionNextCombo),
          static_cast<QWidget*>(stateTransitionResetCheck),
          static_cast<QWidget*>(stateTransitionErrorCheck),
          static_cast<QWidget*>(stateTransitionUnreachableCheck)}) {
        if (widget)
            widget->setVisible(stateMode);
    }
    if (graphLayoutCombo) {
        graphLayoutCombo->setVisible(graphMode);
        const QSignalBlocker blocker(graphLayoutCombo);
        if (moduleMode)
            graphLayoutCombo->setCurrentText(QStringLiteral("Nested blocks"));
        else if (stateMode)
            graphLayoutCombo->setCurrentText(QStringLiteral("State flow"));
    }
    if (graphTable)
        graphTable->setVisible(graphMode);
    if (graphInspector)
        graphInspector->setVisible(graphMode);
}

void RtlInsightsPanelCoordinator::clearGraphDetails()
{
    if (graphInspector)
        graphInspector->clear();
    if (graphTable) {
        graphTable->clear();
        graphTable->setRowCount(0);
        graphTable->setColumnCount(0);
    }
    currentModuleBlockSelectedNodeId = -1;
    if (graphInspectorJumpButton)
        graphInspectorJumpButton->setEnabled(false);
    if (graphInspectorFocusButton)
        graphInspectorFocusButton->setEnabled(false);
    if (graphInspectorSetTopButton)
        graphInspectorSetTopButton->setEnabled(false);
    if (graphInspectorRevealButton)
        graphInspectorRevealButton->setEnabled(false);
}

void RtlInsightsPanelCoordinator::renderGenericGraphInspector(
    const QString& title,
    const QStringList& rows)
{
    if (!graphInspector)
        return;
    graphInspector->clear();
    auto* titleItem = new QTreeWidgetItem(graphInspector);
    titleItem->setText(0, QStringLiteral("Selected"));
    titleItem->setText(1, title);
    QFont font = titleItem->font(0);
    font.setBold(true);
    titleItem->setFont(0, font);
    titleItem->setFont(1, font);
    for (const QString& row : rows) {
        const int split = row.indexOf(QLatin1Char('='));
        auto* item = new QTreeWidgetItem(graphInspector);
        item->setText(0, split > 0 ? row.left(split) : row);
        item->setText(1, split > 0 ? row.mid(split + 1) : QString());
    }
    if (graphInspectorJumpButton)
        graphInspectorJumpButton->setEnabled(true);
    if (graphInspectorFocusButton)
        graphInspectorFocusButton->setEnabled(true);
    if (graphInspectorSetTopButton)
        graphInspectorSetTopButton->setEnabled(false);
}

void RtlInsightsPanelCoordinator::renderModuleBlockInspector(
    const ModuleBlockDiagramReport& report,
    const ModuleBlockDiagramNode& node)
{
    if (!graphInspector)
        return;

    QHash<int, ModuleBlockDiagramNode> nodesById;
    int childCount = 0;
    int unresolvedChildCount = 0;
    for (const ModuleBlockDiagramNode& current : report.nodes) {
        nodesById.insert(current.nodeId, current);
        if (current.parentNodeId == node.nodeId) {
            ++childCount;
            if (current.unresolved)
                ++unresolvedChildCount;
        }
    }

    QStringList pathParts;
    int cursor = node.nodeId;
    QSet<int> seen;
    while (nodesById.contains(cursor) && !seen.contains(cursor)) {
        const ModuleBlockDiagramNode current = nodesById.value(cursor);
        seen.insert(cursor);
        pathParts.prepend(current.instanceDisplayName.isEmpty()
                              ? current.moduleDisplayName
                              : current.instanceDisplayName);
        cursor = current.parentNodeId;
    }

    auto addRow = [this](const QString& field, const QString& value) {
        auto* item = new QTreeWidgetItem(graphInspector);
        item->setText(0, field);
        item->setText(1, value.isEmpty() ? QStringLiteral("-") : value);
        item->setToolTip(1, value);
    };

    graphInspector->clear();
    const QString selectedText =
        node.instanceDisplayName.isEmpty()
            ? node.moduleDisplayName
            : QStringLiteral("%1 : %2")
                  .arg(node.instanceDisplayName, node.moduleDisplayName);
    addRow(QStringLiteral("Selected"), selectedText);
    addRow(QStringLiteral("Type"),
           node.unresolved ? QStringLiteral("unresolved module")
                           : node.moduleTypeDisplayName);
    addRow(QStringLiteral("Definition"),
           node.definitionCodeLink.fileName.isEmpty()
               ? QStringLiteral("missing")
               : QStringLiteral("%1:%2")
                     .arg(node.definitionCodeLink.fileDisplayName,
                          node.definitionCodeLink.lineDisplayName));
    const ModuleBlockDiagramNode parentNode =
        nodesById.value(node.parentNodeId);
    addRow(QStringLiteral("Parent"),
           parentNode.nodeId >= 0 ? parentNode.moduleDisplayName
                                  : QStringLiteral("-"));
    addRow(QStringLiteral("Children"), QString::number(childCount));
    addRow(QStringLiteral("Unresolved children"),
           QString::number(unresolvedChildCount));
    addRow(QStringLiteral("Status"),
           node.unresolved
               ? QStringLiteral("unresolved - %1")
                     .arg(node.unresolvedReason)
               : QStringLiteral("resolved"));
    addRow(QStringLiteral("Workspace"),
           QFileInfo(currentFileName).dir().dirName());
    addRow(QStringLiteral("Path"), pathParts.join(QLatin1Char('.')));
    addRow(QStringLiteral("Depth"), QString::number(node.depth));
    const RtlInsightCodeLink link =
        node.instanceCodeLink.fileName.isEmpty()
            ? node.definitionCodeLink
            : node.instanceCodeLink;
    addRow(QStringLiteral("Location"),
           link.fileName.isEmpty()
               ? QStringLiteral("-")
               : QStringLiteral("%1:%2")
                     .arg(link.fileDisplayName, link.lineDisplayName));

    if (graphInspectorJumpButton)
        graphInspectorJumpButton->setEnabled(!link.fileName.isEmpty());
    if (graphInspectorFocusButton)
        graphInspectorFocusButton->setEnabled(true);
    if (graphInspectorSetTopButton) {
        graphInspectorSetTopButton->setEnabled(
            !node.unresolved && !node.definitionCodeLink.fileName.isEmpty());
    }
}

void RtlInsightsPanelCoordinator::populateModuleBlockInstancesTable(
    const ModuleBlockDiagramReport& report)
{
    if (!graphTable)
        return;
    graphTable->clear();
    graphTable->setColumnCount(5);
    graphTable->setHorizontalHeaderLabels({QStringLiteral("Instance"),
                                           QStringLiteral("Module"),
                                           QStringLiteral("Parent"),
                                           QStringLiteral("File"),
                                           QStringLiteral("Status")});
    graphTable->setRowCount(0);

    QHash<int, ModuleBlockDiagramNode> nodesById;
    for (const ModuleBlockDiagramNode& node : report.nodes)
        nodesById.insert(node.nodeId, node);

    const bool showUnresolved =
        !moduleBlockShowUnresolvedCheck || moduleBlockShowUnresolvedCheck->isChecked();
    for (const ModuleBlockDiagramNode& node : report.nodes) {
        if (node.nodeId == report.root.nodeId)
            continue;
        if (node.unresolved && !showUnresolved)
            continue;
        const int row = graphTable->rowCount();
        graphTable->insertRow(row);
        const ModuleBlockDiagramNode parent =
            nodesById.value(node.parentNodeId);
        const RtlInsightCodeLink link =
            node.instanceCodeLink.fileName.isEmpty()
                ? node.definitionCodeLink
                : node.instanceCodeLink;
        const QString status =
            node.unresolved ? QStringLiteral("unresolved")
                            : QStringLiteral("resolved");
        const QString instance =
            node.instanceDisplayName.isEmpty()
                ? node.moduleDisplayName
                : node.instanceDisplayName;
        const QStringList values{
            instance,
            node.moduleDisplayName,
            parent.nodeId >= 0 ? parent.moduleDisplayName : QStringLiteral("-"),
            link.fileDisplayName.isEmpty()
                ? QFileInfo(link.fileName).fileName()
                : link.fileDisplayName,
            status
        };
        for (int column = 0; column < values.size(); ++column) {
            auto* item = new QTableWidgetItem(values.at(column));
            item->setData(kGraphNodeIdRole, node.nodeId);
            item->setData(kGraphTablePrimaryRole, node.moduleDisplayName);
            item->setData(kGraphTableSecondaryRole, node.instanceDisplayName);
            item->setData(kGraphTableKindRole, QStringLiteral("module"));
            if (column == 4) {
                item->setForeground(node.unresolved
                                        ? QBrush(InsightVisualStyle::theme().warning)
                                        : QBrush(InsightVisualStyle::theme().hover));
            }
            graphTable->setItem(row, column, item);
        }
    }
    graphTable->resizeColumnsToContents();
    graphTable->horizontalHeader()->setStretchLastSection(true);
}

void RtlInsightsPanelCoordinator::populateFsmTransitionsTable(
    const FsmGraph& graph)
{
    if (!graphTable)
        return;
    graphTable->clear();
    graphTable->setColumnCount(7);
    graphTable->setHorizontalHeaderLabels({QStringLiteral("ID"),
                                           QStringLiteral("From"),
                                           QStringLiteral("To"),
                                           QStringLiteral("Condition"),
                                           QStringLiteral("Type"),
                                           QStringLiteral("Source"),
                                           QStringLiteral("Notes")});
    const QSet<QString> deadStates = deadFsmStateNames(graph);
    graphTable->setRowCount(graph.transitionRows.size());
    for (int row = 0; row < graph.transitionRows.size(); ++row) {
        const FsmTransitionRow& transition = graph.transitionRows.at(row);
        const bool deadSelfLoop =
            transition.fromStateDisplayName == transition.toStateDisplayName
            && deadStates.contains(transition.fromStateDisplayName);
        const QStringList values{
            fsmTransitionConditionId(row),
            transition.fromStateDisplayName,
            transition.toStateDisplayName,
            transition.conditionDisplayName,
            transition.assignmentTargetDisplayName,
            transition.sourceLineDisplayName,
            deadSelfLoop ? QStringLiteral("dead/end state") : QString()
        };
        for (int column = 0; column < values.size(); ++column) {
            auto* item = new QTableWidgetItem(values.at(column));
            item->setData(kGraphTablePrimaryRole,
                          transition.fromStateDisplayName);
            item->setData(kGraphTableSecondaryRole,
                          transition.toStateDisplayName);
            item->setData(kGraphTableKindRole, QStringLiteral("transition"));
            graphTable->setItem(row, column, item);
        }
    }
    graphTable->resizeColumnsToContents();
    graphTable->horizontalHeader()->setStretchLastSection(true);
}

void RtlInsightsPanelCoordinator::selectModuleBlockNode(
    int nodeId,
    bool centerGraph,
    bool syncTable)
{
    if (nodeId < 0)
        return;
    currentModuleBlockSelectedNodeId = nodeId;
    const ModuleBlockDiagramNode* selectedNode = nullptr;
    for (const ModuleBlockDiagramNode& node : currentModuleBlockReport.nodes) {
        if (node.nodeId == nodeId) {
            selectedNode = &node;
            break;
        }
    }
    if (!selectedNode)
        return;

    if (insightsGraphScene) {
        for (QGraphicsItem* item : insightsGraphScene->items()) {
            if (!dynamic_cast<RtlInsightGraphNodeItem*>(item))
                continue;
            const bool match = item->data(kGraphNodeIdRole).toInt() == nodeId;
            item->setSelected(match);
            if (match && centerGraph && insightsGraphView)
                insightsGraphView->centerOnRect(item->sceneBoundingRect());
        }
    }
    if (syncTable && graphTable) {
        for (int row = 0; row < graphTable->rowCount(); ++row) {
            const QTableWidgetItem* item = graphTable->item(row, 0);
            if (item && item->data(kGraphNodeIdRole).toInt() == nodeId) {
                const QSignalBlocker blocker(graphTable);
                graphTable->selectRow(row);
                break;
            }
        }
    }
    renderModuleBlockInspector(currentModuleBlockReport, *selectedNode);
}

bool RtlInsightsPanelCoordinator::navigateGraphItem(QGraphicsItem* item)
{
    if (!item || !navigationHandler)
        return false;
    const QString fileName = item->data(kGraphFileRole).toString();
    if (fileName.isEmpty())
        return false;
    if (!navigationHandler(fileName,
                           item->data(kGraphLineRole).toInt(),
                           item->data(kGraphColumnRole).toInt())) {
        if (statusMessageHandler)
            statusMessageHandler(QStringLiteral("RTL graph jump failed"),
                                 4000);
        return false;
    }
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

bool RtlInsightsPanelCoordinator::navigateSelectedGraphItem()
{
    if (!insightsGraphScene)
        return false;
    const QList<QGraphicsItem*> selected = insightsGraphScene->selectedItems();
    if (selected.isEmpty())
        return false;
    return navigateGraphItem(selected.first());
}

bool RtlInsightsPanelCoordinator::setModuleBlockTopFromSelected()
{
    if (!insightsGraphScene
        || currentGraphMode != QStringLiteral("module-block")) {
        return false;
    }
    const QList<QGraphicsItem*> selected = insightsGraphScene->selectedItems();
    if (selected.isEmpty())
        return false;
    QGraphicsItem* item = selected.first();
    const QString drillModuleName =
        item->data(kGraphDrillModuleRole).toString();
    if (drillModuleName.isEmpty())
        return false;
    const QString drillFileName =
        item->data(kGraphDrillFileRole).toString().isEmpty()
            ? item->data(kGraphFileRole).toString()
            : item->data(kGraphDrillFileRole).toString();
    if (drillFileName.isEmpty())
        return false;
    showModuleBlockDiagramForModule(drillFileName, drillModuleName);
    return true;
}

void RtlInsightsPanelCoordinator::showTreeSurface()
{
    if (insightsStack && insightsTree)
        insightsStack->setCurrentWidget(insightsTree);
    currentGraphMode.clear();
}

void RtlInsightsPanelCoordinator::showGraphSurface()
{
    if (insightsStack && insightsGraphPanel)
        insightsStack->setCurrentWidget(insightsGraphPanel);
}

void RtlInsightsPanelCoordinator::applyGraphSearchHighlight()
{
    if (!insightsGraphScene)
        return;

    const QString needle = graphSearchText.trimmed();
    const bool active = !needle.isEmpty();
    int firstMatchCount = 0;
    QPointF firstMatchCenter;
    for (QGraphicsItem* item : insightsGraphScene->items()) {
        if (!dynamic_cast<RtlInsightGraphNodeItem*>(item)
            && !dynamic_cast<RtlInsightGraphStateNodeItem*>(item)
            && !dynamic_cast<RtlInsightGraphEdgeItem*>(item)) {
            continue;
        }
        const QString haystack =
            QStringList{item->data(kGraphKindRole).toString(),
                        item->data(kGraphPrimaryRole).toString(),
                        item->data(kGraphSecondaryRole).toString(),
                        item->data(kGraphDetailRole).toString(),
                        item->data(kGraphBadgeRole).toString()}
                .join(QLatin1Char(' '));
        const bool match =
            active && haystack.contains(needle, Qt::CaseInsensitive);
        item->setSelected(match);
        if (match) {
            ++firstMatchCount;
            if (firstMatchCount == 1)
                firstMatchCenter = item->sceneBoundingRect().center();
        }
    }
    if (firstMatchCount > 0 && insightsGraphView)
        insightsGraphView->centerOnPoint(firstMatchCenter);
    if (statusMessageHandler && active) {
        statusMessageHandler(QStringLiteral("%1 graph match(es)")
                                 .arg(firstMatchCount),
                             1200);
    }
}

void RtlInsightsPanelCoordinator::showHotspotSurface()
{
    if (insightsStack && signalUsageHotspotPanel)
        insightsStack->setCurrentWidget(signalUsageHotspotPanel);
}

void RtlInsightsPanelCoordinator::renderGraphUnavailable(
    const QString& title,
    const QString& message)
{
    showGraphSurface();
    if (!insightsGraphScene || !insightsGraphView)
        return;
    configureGraphToolbarForMode(QString());
    clearGraphDetails();
    insightsGraphScene->clear();
    insightsGraphView->resetView();
    lastGraphFitRect = QRectF();

    const InsightTheme t = InsightVisualStyle::theme();
    QFont titleFont = InsightVisualStyle::titleFont(insightsGraphView->font());
    titleFont.setPointSize(qMax(10, titleFont.pointSize() + 2));
    auto* card = insightsGraphScene->addRect(
        QRectF(-230, -86, 460, 172),
        InsightVisualStyle::panelBorderPen(),
        InsightVisualStyle::panelBrush());
    card->setZValue(-1);

    auto* titleItem = insightsGraphScene->addSimpleText(title, titleFont);
    titleItem->setBrush(QBrush(t.textPrimary));
    titleItem->setPos(-188, -48);

    QFont detailFont = InsightVisualStyle::compactFont(insightsGraphView->font());
    auto* detailItem = insightsGraphScene->addText(
        message.isEmpty()
            ? QStringLiteral("No graph is available for the current selection.")
            : message,
        detailFont);
    detailItem->setTextWidth(376.0);
    detailItem->setDefaultTextColor(t.warning);
    detailItem->setPos(-188, -8);

    insightsGraphScene->setSceneRect(-220, -90, 440, 180);
    lastGraphFitRect = insightsGraphScene->sceneRect();
    insightsGraphView->fitScene(Qt::KeepAspectRatio);
    applyGraphSearchHighlight();
}

void RtlInsightsPanelCoordinator::renderStateTransitionGraphScene(
    const StateTransitionGraphReport& report)
{
    showGraphSurface();
    if (!insightsGraphScene || !insightsGraphView)
        return;
    configureGraphToolbarForMode(QStringLiteral("state-transition"));
    clearGraphDetails();
    insightsGraphScene->clear();
    insightsGraphView->resetView();
    lastGraphFitRect = QRectF();

    if (!report.found) {
        renderGraphUnavailable(
            report.groupDisplayName.isEmpty()
                ? QStringLiteral("State Transition Graph")
                : report.groupDisplayName,
            report.notFoundReasonDisplayName);
        return;
    }
    if (stateTransitionSignalCombo) {
        QSignalBlocker blocker(stateTransitionSignalCombo);
        stateTransitionSignalCombo->clear();
        stateTransitionSignalCombo->addItem(report.selectedSignalDisplayName);
    }
    if (stateTransitionCurrentCombo) {
        QSignalBlocker blocker(stateTransitionCurrentCombo);
        stateTransitionCurrentCombo->clear();
        stateTransitionCurrentCombo->addItem(
            report.graph.stateRegisterDisplayName);
    }
    if (stateTransitionNextCombo) {
        QSignalBlocker blocker(stateTransitionNextCombo);
        stateTransitionNextCombo->clear();
        stateTransitionNextCombo->addItem(
            report.graph.nextStateSignalDisplayName);
    }

    const QFont font = insightsGraphView->font();
    const auto navigate = [this](const RtlInsightGraphElement& element) {
        const RtlInsightCodeLink& link = element.codeLink;
        if (navigationHandler && !link.fileName.isEmpty()
            && !navigationHandler(link.fileName, link.line, link.column)
            && statusMessageHandler) {
            statusMessageHandler(QStringLiteral("RTL graph jump failed"),
                                 4000);
        }
    };
    const auto select = [this](const RtlInsightGraphElement& element) {
        QStringList rows{QStringLiteral("Kind=%1").arg(element.kind)};
        if (!element.badge.isEmpty())
            rows.append(QStringLiteral("ID=%1").arg(element.badge));
        rows.append(QStringLiteral("To=%1").arg(element.secondary));
        rows.append(QStringLiteral("Condition=%1").arg(element.detail));
        rows.append(QStringLiteral("Location=%1:%2")
                        .arg(element.codeLink.fileDisplayName,
                             element.codeLink.lineDisplayName));
        renderGenericGraphInspector(
            element.primary,
            rows);
        if (statusMessageHandler)
            statusMessageHandler(QStringLiteral("%1: %2")
                                     .arg(element.kind, element.primary),
                                 1200);
    };

    const QString graphTitle = report.selectedSignalDisplayName.isEmpty()
        ? QStringLiteral("State Transition Graph")
        : QStringLiteral("State Transition Graph: %1")
              .arg(report.selectedSignalDisplayName);
    const FsmGraphRenderResult renderResult =
        renderFsmStateMachineGraph(insightsGraphScene,
                                   report.graph,
                                   graphTitle,
                                   QPointF(0.0, 0.0),
                                   font,
                                   navigate,
                                   select);
    insightsGraphScene->setSceneRect(renderResult.sceneBounds);
    lastGraphFitRect = renderResult.bodyBounds.isEmpty()
        ? renderResult.sceneBounds
        : renderResult.bodyBounds;
    fitFsmBodyRect(insightsGraphView, lastGraphFitRect);
    populateFsmTransitionsTable(report.graph);
    applyGraphSearchHighlight();
}

void RtlInsightsPanelCoordinator::renderFsmGraphScene(
    const FsmGraphReport& report,
    const QString& title)
{
    showGraphSurface();
    if (!insightsGraphScene || !insightsGraphView)
        return;
    configureGraphToolbarForMode(QStringLiteral("fsm"));
    clearGraphDetails();
    insightsGraphScene->clear();
    insightsGraphView->resetView();
    lastGraphFitRect = QRectF();

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
        if (navigationHandler && !link.fileName.isEmpty()
            && !navigationHandler(link.fileName, link.line, link.column)
            && statusMessageHandler) {
            statusMessageHandler(QStringLiteral("RTL graph jump failed"),
                                 4000);
        }
    };
    const auto select = [this](const RtlInsightGraphElement& element) {
        QStringList rows{QStringLiteral("Kind=%1").arg(element.kind)};
        if (!element.badge.isEmpty())
            rows.append(QStringLiteral("ID=%1").arg(element.badge));
        rows.append(QStringLiteral("To=%1").arg(element.secondary));
        rows.append(QStringLiteral("Detail=%1").arg(element.detail));
        rows.append(QStringLiteral("Location=%1:%2")
                        .arg(element.codeLink.fileDisplayName,
                             element.codeLink.lineDisplayName));
        renderGenericGraphInspector(
            element.primary,
            rows);
        if (statusMessageHandler)
            statusMessageHandler(QStringLiteral("%1: %2")
                                     .arg(element.kind, element.primary),
                                 1200);
    };

    QRectF bounds;
    QRectF bodyBounds;
    qreal nextOriginY = 0.0;
    for (int graphIndex = 0; graphIndex < report.graphs.size(); ++graphIndex) {
        const FsmGraph& graph = report.graphs.at(graphIndex);
        const QString graphTitle = report.graphs.size() == 1
            ? (title.isEmpty() ? QStringLiteral("FSM Graph") : title)
            : QStringLiteral("FSM Graph: %1")
                  .arg(graph.nextStateSignalDisplayName.isEmpty()
                           ? graph.stateRegisterDisplayName
                           : graph.nextStateSignalDisplayName);
        const FsmGraphRenderResult graphRender =
            renderFsmStateMachineGraph(insightsGraphScene,
                                       graph,
                                       graphTitle,
                                       QPointF(0.0, nextOriginY),
                                       font,
                                       navigate,
                                       select);
        bounds = bounds.isNull()
            ? graphRender.sceneBounds
            : bounds.united(graphRender.sceneBounds);
        bodyBounds = bodyBounds.isNull()
            ? graphRender.bodyBounds
            : bodyBounds.united(graphRender.bodyBounds);
        nextOriginY = graphRender.sceneBounds.bottom() + 94.0;
    }
    insightsGraphScene->setSceneRect(bounds);
    lastGraphFitRect = bodyBounds.isEmpty() ? bounds : bodyBounds;
    fitFsmBodyRect(insightsGraphView, lastGraphFitRect);
    if (!report.graphs.isEmpty()) {
        const FsmGraph& firstGraph = report.graphs.first();
        if (stateTransitionSignalCombo) {
            QSignalBlocker blocker(stateTransitionSignalCombo);
            stateTransitionSignalCombo->clear();
            stateTransitionSignalCombo->addItem(
                firstGraph.nextStateSignalDisplayName);
        }
        if (stateTransitionCurrentCombo) {
            QSignalBlocker blocker(stateTransitionCurrentCombo);
            stateTransitionCurrentCombo->clear();
            stateTransitionCurrentCombo->addItem(
                firstGraph.stateRegisterDisplayName);
        }
        if (stateTransitionNextCombo) {
            QSignalBlocker blocker(stateTransitionNextCombo);
            stateTransitionNextCombo->clear();
            stateTransitionNextCombo->addItem(
                firstGraph.nextStateSignalDisplayName);
        }
        populateFsmTransitionsTable(firstGraph);
    }
    applyGraphSearchHighlight();
}

void RtlInsightsPanelCoordinator::renderModuleBlockDiagramScene(
    const ModuleBlockDiagramReport& report)
{
    showGraphSurface();
    if (!insightsGraphScene || !insightsGraphView)
        return;
    configureGraphToolbarForMode(QStringLiteral("module-block"));
    clearGraphDetails();
    currentModuleBlockReport = report;
    insightsGraphScene->clear();
    insightsGraphView->resetView();

    if (!report.found) {
        renderGraphUnavailable(
            report.groupDisplayName.isEmpty()
                ? QStringLiteral("Module Block Diagram")
                : report.groupDisplayName,
            report.notFoundReasonDisplayName);
        return;
    }
    if (moduleBlockTopCombo) {
        QSignalBlocker blocker(moduleBlockTopCombo);
        moduleBlockTopCombo->clear();
        for (const ModuleBlockDiagramNode& node : report.nodes) {
            if (node.unresolved)
                continue;
            moduleBlockTopCombo->addItem(node.moduleDisplayName,
                                         node.nodeId);
        }
        moduleBlockTopCombo->setCurrentText(report.root.moduleDisplayName);
    }

    const QFont font = insightsGraphView->font();
    const auto navigate = [this](const RtlInsightGraphElement& element) {
        const RtlInsightCodeLink& link = element.codeLink;
        if (navigationHandler && !link.fileName.isEmpty()
            && !navigationHandler(link.fileName, link.line, link.column)
            && statusMessageHandler) {
            statusMessageHandler(QStringLiteral("RTL graph jump failed"),
                                 4000);
        }
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
        renderGenericGraphInspector(
            element.primary,
            {QStringLiteral("Kind=%1").arg(element.kind),
             QStringLiteral("Detail=%1").arg(element.detail),
             QStringLiteral("Location=%1:%2")
                 .arg(element.codeLink.fileDisplayName,
                      element.codeLink.lineDisplayName)});
        if (statusMessageHandler)
            statusMessageHandler(QStringLiteral("%1: %2")
                                     .arg(element.kind, element.primary),
                                 1200);
    };

    auto addNode = [&](const ModuleBlockDiagramNode& node,
                       const QRectF& rect,
                       bool root) {
        RtlInsightGraphElement element;
        element.kind = QStringLiteral("module");
        element.primary = node.moduleDisplayName;
        element.secondary = !node.instanceDisplayName.isEmpty()
            ? QStringLiteral("instance: %1").arg(node.instanceDisplayName)
            : node.moduleTypeDisplayName;
        element.detail = node.unresolved
            ? node.unresolvedReason
            : node.sourceRoleDisplayName;
        element.codeLink = root || node.instanceCodeLink.fileName.isEmpty()
            ? node.definitionCodeLink
            : node.instanceCodeLink;
        if (!node.unresolved && !node.definitionCodeLink.fileName.isEmpty()) {
            element.drillModuleName = node.moduleDisplayName;
            element.drillFileName = node.definitionCodeLink.fileName;
        }
        auto* item = new RtlInsightGraphNodeItem(
            element,
            rect,
            root ? InsightVisualStyle::roleFillColor(InsightVisualRole::Kernel)
                 : (node.unresolved
                        ? InsightVisualStyle::roleFillColor(
                              InsightVisualRole::Unknown)
                        : InsightVisualStyle::roleFillColor(
                              InsightVisualRole::Port)),
            root ? InsightVisualStyle::roleColor(InsightVisualRole::Kernel)
                 : (node.unresolved
                        ? InsightVisualStyle::roleColor(
                              InsightVisualRole::Unknown)
                        : InsightVisualStyle::roleColor(
                              InsightVisualRole::Port)),
            font);
        const qreal baseZ = root ? 0.0 : 6.0 + node.depth * 8.0;
        item->setBaseZValues(baseZ, root ? 1.0 : baseZ + 3.0);
        item->setData(kGraphNodeIdRole, node.nodeId);
        item->navigateHandler = navigate;
        item->selectHandler = [this, node](const RtlInsightGraphElement&) {
            selectModuleBlockNode(node.nodeId, false, true);
        };
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
        element.detail = edge.childInstanceDisplayName.isEmpty()
            ? edge.relationshipDisplayName
            : QStringLiteral("%1: %2")
                  .arg(edge.relationshipDisplayName,
                       edge.childInstanceDisplayName);
        element.codeLink = edge.childInstanceCodeLink.fileName.isEmpty()
            ? edge.childDefinitionCodeLink
            : edge.childInstanceCodeLink;
        if (!edge.unresolved && !edge.childDefinitionCodeLink.fileName.isEmpty()) {
            element.drillModuleName = edge.childModuleDisplayName;
            element.drillFileName = edge.childDefinitionCodeLink.fileName;
        }
        auto* item = new RtlInsightGraphEdgeItem(
            element,
            path,
            end,
            std::atan2(end.y() - start.y(), end.x() - start.x()),
            QString(),
            font,
            edge.unresolved ? InsightVisualStyle::theme().textMuted
                            : InsightVisualStyle::theme().borderStrong);
        item->navigateHandler = navigate;
        item->selectHandler = select;
        insightsGraphScene->addItem(item);
    };

    QHash<int, ModuleBlockDiagramNode> nodeById;
    QHash<int, QList<ModuleBlockDiagramNode>> childrenByParent;
    QSet<int> visibleNodeIds;
    visibleNodeIds.insert(report.root.nodeId);
    const bool showUnresolved =
        !moduleBlockShowUnresolvedCheck
        || moduleBlockShowUnresolvedCheck->isChecked();
    for (const ModuleBlockDiagramNode& node : report.nodes) {
        nodeById.insert(node.nodeId, node);
        if (!node.unresolved || showUnresolved)
            visibleNodeIds.insert(node.nodeId);
    }
    for (const ModuleBlockDiagramNode& node : report.nodes) {
        if (node.parentNodeId >= 0
            && visibleNodeIds.contains(node.nodeId)
            && visibleNodeIds.contains(node.parentNodeId)) {
            childrenByParent[node.parentNodeId].append(node);
        }
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

    QHash<int, QRectF> rectByNodeId;
    QHash<int, QSizeF> measuredSizeByNodeId;
    constexpr qreal childGap = 12.0;
    constexpr qreal childInset = 18.0;
    constexpr qreal titleBand = 46.0;
    auto wrappedColumnCount = [](int childCount) {
        if (childCount <= 1)
            return 1;
        return qMin(4,
                    qMax(1,
                         static_cast<int>(
                             std::ceil(std::sqrt(static_cast<double>(childCount))))));
    };
    auto zeroReals = [](int count) {
        QList<qreal> values;
        values.reserve(count);
        for (int i = 0; i < count; ++i)
            values.append(0.0);
        return values;
    };
    std::function<QSizeF(int)> measureNode = [&](int nodeId) -> QSizeF {
        if (measuredSizeByNodeId.contains(nodeId))
            return measuredSizeByNodeId.value(nodeId);
        const QList<ModuleBlockDiagramNode> children =
            childrenByParent.value(nodeId);
        QSizeF size(kInsightNodeWidth + 20.0, kInsightNodeHeight + 10.0);
        if (!children.isEmpty()) {
            const int columns = wrappedColumnCount(children.size());
            const int rows = (children.size() + columns - 1) / columns;
            QList<qreal> columnWidths = zeroReals(columns);
            QList<qreal> rowHeights = zeroReals(rows);
            for (int i = 0; i < children.size(); ++i) {
                const ModuleBlockDiagramNode& child = children.at(i);
                const QSizeF measured = measureNode(child.nodeId);
                const int row = i / columns;
                const int column = i % columns;
                columnWidths[column] =
                    qMax(columnWidths.at(column), measured.width());
                rowHeights[row] =
                    qMax(rowHeights.at(row), measured.height());
            }
            qreal childWidth = 0.0;
            for (int column = 0; column < columnWidths.size(); ++column) {
                childWidth += columnWidths.at(column);
                if (column + 1 < columnWidths.size())
                    childWidth += childGap;
            }
            qreal childHeight = 0.0;
            for (int row = 0; row < rowHeights.size(); ++row) {
                childHeight += rowHeights.at(row);
                if (row + 1 < rowHeights.size())
                    childHeight += childGap;
            }
            size.setWidth(qMax<qreal>(kInsightNodeWidth + 72.0,
                                      childWidth + childInset * 2.0));
            size.setHeight(qMax<qreal>(kInsightNodeHeight + 62.0,
                                       titleBand + childHeight + childInset));
        }
        if (nodeId == report.root.nodeId) {
            size.setWidth(qMax<qreal>(size.width(), 540.0));
            size.setHeight(qMax<qreal>(size.height(), 240.0));
        }
        measuredSizeByNodeId.insert(nodeId, size);
        return size;
    };
    const QSizeF rootSize = measureNode(report.root.nodeId);
    std::function<void(int, QPointF)> placeNode =
        [&](int nodeId, QPointF topLeft) {
            const QSizeF size = measuredSizeByNodeId.value(nodeId);
            const QRectF rect(topLeft, size);
            rectByNodeId.insert(nodeId, rect);
            const QList<ModuleBlockDiagramNode> children =
                childrenByParent.value(nodeId);
            if (children.isEmpty())
                return;

            const int columns = wrappedColumnCount(children.size());
            const int rows = (children.size() + columns - 1) / columns;
            QList<qreal> columnWidths = zeroReals(columns);
            QList<qreal> rowHeights = zeroReals(rows);
            for (int i = 0; i < children.size(); ++i) {
                const QSizeF childSize =
                    measuredSizeByNodeId.value(children.at(i).nodeId);
                const int row = i / columns;
                const int column = i % columns;
                columnWidths[column] =
                    qMax(columnWidths.at(column), childSize.width());
                rowHeights[row] =
                    qMax(rowHeights.at(row), childSize.height());
            }
            qreal gridWidth = 0.0;
            for (int column = 0; column < columnWidths.size(); ++column) {
                gridWidth += columnWidths.at(column);
                if (column + 1 < columnWidths.size())
                    gridWidth += childGap;
            }
            const qreal gridLeft =
                rect.left() + childInset
                + qMax<qreal>(0.0,
                              (rect.width() - childInset * 2.0 - gridWidth)
                                  / 2.0);
            QList<qreal> columnLefts;
            columnLefts.reserve(columns);
            qreal cursorX = gridLeft;
            for (int column = 0; column < columns; ++column) {
                columnLefts.append(cursorX);
                cursorX += columnWidths.at(column) + childGap;
            }
            QList<qreal> rowTops;
            rowTops.reserve(rows);
            qreal cursorY = rect.top() + titleBand;
            for (int row = 0; row < rows; ++row) {
                rowTops.append(cursorY);
                cursorY += rowHeights.at(row) + childGap;
            }

            for (int i = 0; i < children.size(); ++i) {
                const ModuleBlockDiagramNode& child = children.at(i);
                const QSizeF childSize =
                    measuredSizeByNodeId.value(child.nodeId);
                const int row = i / columns;
                const int column = i % columns;
                const QPointF childTopLeft(
                    columnLefts.at(column)
                        + (columnWidths.at(column) - childSize.width()) / 2.0,
                    rowTops.at(row)
                        + (rowHeights.at(row) - childSize.height()) / 2.0);
                placeNode(child.nodeId, childTopLeft);
            }
        };
    const QPointF rootTopLeft(-rootSize.width() / 2.0,
                              -rootSize.height() / 2.0);
    placeNode(report.root.nodeId, rootTopLeft);

    const QRectF rootRect = rectByNodeId.value(report.root.nodeId);

    QFont titleFont = InsightVisualStyle::titleFont(font);
    titleFont.setPointSize(qMax(10, titleFont.pointSize() + 1));
    auto* titleItem = insightsGraphScene->addSimpleText(
        QStringLiteral("Module Block Diagram: %1")
            .arg(report.root.moduleDisplayName),
        titleFont);
    titleItem->setBrush(QBrush(InsightVisualStyle::theme().textPrimary));
    titleItem->setPos(rootRect.left(), rootRect.top() - 54.0);

    const QString summaryText =
        QStringLiteral("%1 module(s), %2 containment edge(s), %3 blackbox")
            .arg(report.moduleCount)
            .arg(report.edgeCount)
            .arg(report.unresolvedInstanceCount);
    auto* summaryItem = insightsGraphScene->addSimpleText(
        summaryText,
        InsightVisualStyle::compactFont(font));
    summaryItem->setBrush(QBrush(InsightVisualStyle::theme().textSecondary));
    summaryItem->setPos(rootRect.left(), rootRect.top() - 30.0);

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

    const bool hasVisibleChildren =
        !childrenByParent.value(report.root.nodeId).isEmpty();
    if (!hasVisibleChildren) {
        const QString noChildText =
            report.notFoundReasonDisplayName.isEmpty()
                ? QStringLiteral("No child modules")
                : QStringLiteral("No child modules: %1")
                      .arg(report.notFoundReasonDisplayName);
        auto* label =
            insightsGraphScene->addSimpleText(noChildText,
                                              InsightVisualStyle::compactFont(font));
        label->setBrush(QBrush(InsightVisualStyle::theme().warning));
        label->setPos(rootRect.center().x() - 70,
                      rootRect.center().y() - 10);
    }

    const QRectF bounds =
        insightsGraphScene->itemsBoundingRect().adjusted(-48, -48, 48, 44);
    insightsGraphScene->setSceneRect(bounds);
    lastGraphFitRect = bounds;
    insightsGraphView->fitRect(bounds, Qt::KeepAspectRatio);
    populateModuleBlockInstancesTable(report);
    selectModuleBlockNode(report.root.nodeId, false, false);
    applyGraphSearchHighlight();
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

void RtlInsightsPanelCoordinator::showSignalUsageHotspotForSignal(
    const QString& fileName,
    const QString& moduleName,
    const QString& signalName,
    const QString& signalAccessPath)
{
    updateModuleContext(fileName, moduleName, signalName);
    showHotspotSurface();
    if (!signalUsageHotspotPanel)
        return;

    if (insightsDock) {
        insightsDock->setWindowTitle(
            QStringLiteral("RTL Insights: Signal Usage Hotspot %1")
                .arg(signalAccessPath.isEmpty() ? signalName
                                                : signalAccessPath));
        insightsDock->show();
        insightsDock->raise();
    }
    signalUsageHotspotPanel->showHotspotForSymbol(signalName,
                                                  fileName,
                                                  moduleName,
                                                  signalAccessPath);
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

void RtlInsightsPanelCoordinator::showSignalUsageHotspot()
{
    showSignalUsageHotspotForSignal(currentFileName,
                                    currentModuleName,
                                    currentSignalName);
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
        if (moduleBlockDepthSpin)
            query.maxDepth = moduleBlockDepthSpin->value();
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
    if (signalUsageHotspotButton)
        signalUsageHotspotButton->setEnabled(hasModule);
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
