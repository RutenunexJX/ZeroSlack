#include "rtlinsightspanelcoordinator.h"

#include "activitylogservice.h"
#include "clockresetdomainservice.h"
#include "fsmgraphlayout.h"
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
#include <QFontDatabase>
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
#include <QStyle>
#include <QStyleOptionGraphicsItem>
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
constexpr int kGraphEdgeLabelRectRole = Qt::UserRole + 1224;
constexpr int kGraphEdgeHitPriorityRole = Qt::UserRole + 1225;
constexpr int kGraphEdgeBridgeRole = Qt::UserRole + 1226;
constexpr int kGraphEdgeOverlapRole = Qt::UserRole + 1227;
constexpr int kGraphEdgePathLengthRole = Qt::UserRole + 1228;
constexpr int kGraphNodePathClearRectRole = Qt::UserRole + 1229;
constexpr int kGraphEdgeRouteDecisionRole = Qt::UserRole + 1230;
constexpr int kGraphFsmNodeIdRole = Qt::UserRole + 1231;
constexpr int kGraphEdgeFromNodeIdRole = Qt::UserRole + 1232;
constexpr int kGraphEdgeToNodeIdRole = Qt::UserRole + 1233;
constexpr int kGraphFsmCanonicalNodeIdRole = Qt::UserRole + 1234;
constexpr int kGraphHoverActiveRole = Qt::UserRole + 1235;

constexpr qreal kInsightNodeWidth = 170.0;
constexpr qreal kInsightNodeHeight = 56.0;
constexpr qreal kGraphMinScale = 0.05;
constexpr qreal kGraphMaxScale = 6.0;
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
    QString overlapIds;
    QString routeDecision;
};

QString graphElidedText(const QString& text, const QFont& font, int width)
{
    return QFontMetrics(font).elidedText(text, Qt::ElideRight, width);
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

QColor fsmStateFillColor(const QString& stateName,
                         const QSet<int>& aliasCanonicalIds,
                         int canonicalNodeId,
                         bool deadEndState)
{
    if (deadEndState)
        return InsightVisualStyle::theme().statusBar.errorBackground;
    if (!aliasCanonicalIds.contains(canonicalNodeId))
        return InsightVisualStyle::theme().graph.nodeFill;
    const QList<InsightVisualRole> roles{
        InsightVisualRole::Timing,
        InsightVisualRole::Read,
        InsightVisualRole::Condition,
        InsightVisualRole::Write,
        InsightVisualRole::Port,
        InsightVisualRole::Case,
        InsightVisualRole::Data};
    const InsightVisualRole role =
        roles.at(static_cast<int>(stableStringHash(stateName)
                                  % static_cast<uint>(roles.size())));
    return InsightVisualStyle::roleFillColor(role);
}

QColor fsmStateStrokeColor(const QString& stateName,
                           const QSet<int>& aliasCanonicalIds,
                           int canonicalNodeId,
                           bool deadEndState)
{
    if (deadEndState)
        return InsightVisualStyle::theme().statusBar.errorBorder;
    if (!aliasCanonicalIds.contains(canonicalNodeId))
        return InsightVisualStyle::theme().graph.nodeBorder;
    const QList<InsightVisualRole> roles{
        InsightVisualRole::Timing,
        InsightVisualRole::Read,
        InsightVisualRole::Condition,
        InsightVisualRole::Write,
        InsightVisualRole::Port,
        InsightVisualRole::Case,
        InsightVisualRole::Data};
    const InsightVisualRole role =
        roles.at(static_cast<int>(stableStringHash(stateName)
                                  % static_cast<uint>(roles.size())));
    return InsightVisualStyle::roleColor(role);
}

QFont readableGraphFont(const QFont& base)
{
    QFont font = base;
    static QString bundledFamily;
    if (bundledFamily.isEmpty()) {
        for (const QString& resourcePath :
             {QStringLiteral(":/fonts/resources/fonts/maple-mono/MapleMono-Regular.ttf"),
              QStringLiteral(":/fonts/resources/fonts/iosevka/Iosevka-Regular.ttf"),
              QStringLiteral(":/fonts/resources/fonts/geist-mono/GeistMono-Regular.ttf")}) {
            const int fontId = QFontDatabase::addApplicationFont(resourcePath);
            if (fontId < 0)
                continue;
            const QStringList loadedFamilies =
                QFontDatabase::applicationFontFamilies(fontId);
            if (!loadedFamilies.isEmpty()) {
                bundledFamily = loadedFamilies.first();
                break;
            }
        }
    }
    if (!bundledFamily.isEmpty())
        font.setFamily(bundledFamily);
    const QStringList families = QFontDatabase::families();
    if (bundledFamily.isEmpty()) {
        for (const QString& candidate :
             {QStringLiteral("Microsoft YaHei UI"),
              QStringLiteral("Arial"),
              QStringLiteral("Consolas"),
              QStringLiteral("Segoe UI"),
              QStringLiteral("Tahoma")}) {
            if (families.contains(candidate, Qt::CaseInsensitive)) {
                font.setFamily(candidate);
                break;
            }
        }
    }
    font.setPointSize(qMax(10, font.pointSize()));
    font.setStyleStrategy(QFont::PreferAntialias);
    return font;
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

QString wrappedFsmConditionToolTip(const QString& label,
                                   const QString& condition)
{
    QString text = QStringLiteral("%1: %2")
                       .arg(label, condition.simplified());
    constexpr int kMaximumCharacters = 640;
    constexpr int kMaximumLineLength = 88;
    if (text.size() > kMaximumCharacters)
        text = text.left(kMaximumCharacters - 3) + QStringLiteral("...");

    QStringList lines;
    while (!text.isEmpty()) {
        if (text.size() <= kMaximumLineLength) {
            lines.append(text);
            break;
        }
        int split = text.lastIndexOf(QLatin1Char(' '),
                                     kMaximumLineLength);
        if (split < kMaximumLineLength / 2)
            split = kMaximumLineLength;
        lines.append(text.left(split).trimmed());
        text = text.mid(split).trimmed();
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

qreal orientation(const QPointF& a, const QPointF& b, const QPointF& c)
{
    return (b.x() - a.x()) * (c.y() - a.y())
        - (b.y() - a.y()) * (c.x() - a.x());
}

bool segmentBoxesOverlap(const QPointF& a,
                         const QPointF& b,
                         const QPointF& c,
                         const QPointF& d)
{
    return qMax(qMin(a.x(), b.x()), qMin(c.x(), d.x()))
            <= qMin(qMax(a.x(), b.x()), qMax(c.x(), d.x())) + 0.1
        && qMax(qMin(a.y(), b.y()), qMin(c.y(), d.y()))
            <= qMin(qMax(a.y(), b.y()), qMax(c.y(), d.y())) + 0.1;
}

bool pointOnSegment(const QPointF& point,
                    const QPointF& start,
                    const QPointF& end)
{
    return qAbs(orientation(start, end, point)) < 0.1
        && point.x() >= qMin(start.x(), end.x()) - 0.1
        && point.x() <= qMax(start.x(), end.x()) + 0.1
        && point.y() >= qMin(start.y(), end.y()) - 0.1
        && point.y() <= qMax(start.y(), end.y()) + 0.1;
}

bool lineSegmentsIntersect(const QPointF& a,
                           const QPointF& b,
                           const QPointF& c,
                           const QPointF& d)
{
    if (!segmentBoxesOverlap(a, b, c, d))
        return false;

    const qreal o1 = orientation(a, b, c);
    const qreal o2 = orientation(a, b, d);
    const qreal o3 = orientation(c, d, a);
    const qreal o4 = orientation(c, d, b);

    if ((o1 > 0.0 && o2 < 0.0 || o1 < 0.0 && o2 > 0.0)
        && (o3 > 0.0 && o4 < 0.0 || o3 < 0.0 && o4 > 0.0)) {
        return true;
    }

    return pointOnSegment(c, a, b)
        || pointOnSegment(d, a, b)
        || pointOnSegment(a, c, d)
        || pointOnSegment(b, c, d);
}

bool pathSamplesCross(const QPainterPath& lhs, const QPainterPath& rhs)
{
    const QList<QPointF> lhsPoints = sampledPathPoints(lhs, 96);
    const QList<QPointF> rhsPoints = sampledPathPoints(rhs, 96);
    if (lhsPoints.size() < 2 || rhsPoints.size() < 2)
        return false;

    for (int i = 1; i < lhsPoints.size(); ++i) {
        const QPointF a = lhsPoints.at(i - 1);
        const QPointF b = lhsPoints.at(i);
        for (int j = 1; j < rhsPoints.size(); ++j) {
            const QPointF c = rhsPoints.at(j - 1);
            const QPointF d = rhsPoints.at(j);
            const bool nearSharedEndpoint =
                distanceBetweenPoints(a, c) < 5.0
                || distanceBetweenPoints(a, d) < 5.0
                || distanceBetweenPoints(b, c) < 5.0
                || distanceBetweenPoints(b, d) < 5.0;
            if (nearSharedEndpoint)
                continue;
            if (lineSegmentsIntersect(a, b, c, d))
                return true;
        }
    }
    return false;
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
    using HoverHandler = std::function<void(QGraphicsItem*, bool)>;

    RtlInsightGraphNodeItem(const RtlInsightGraphElement& graphElement,
                            const QRectF& rect,
                            const QColor& fill,
                            const QColor& stroke,
                            const QFont& font,
                            bool dashed = false,
                            int canonicalNodeId = -1,
                            bool showDetail = true)
        : QGraphicsRectItem(rect),
          element(graphElement)
    {
        setFlag(QGraphicsItem::ItemIsSelectable, true);
        setAcceptHoverEvents(true);
        setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
        normalBrush = QBrush(fill);
        setBrush(normalBrush);
        normalPen = QPen(stroke, 1.6);
        if (dashed)
            normalPen.setStyle(Qt::DashLine);
        hoverPen = InsightVisualStyle::hoverPen();
        setPen(normalPen);
        setBaseZValues(10.0, 40.0);
        applyGraphElementData(this, element);
        setData(kGraphNodePathClearRectRole, rect);
        setData(kGraphFsmCanonicalNodeIdRole, canonicalNodeId);

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
        if (showDetail) {
            auto* detailItem = new QGraphicsSimpleTextItem(
                graphElidedText(detail,
                                detailFont,
                                static_cast<int>(rect.width() - 18)),
                this);
            detailItem->setAcceptedMouseButtons(Qt::NoButton);
            detailItem->setFont(detailFont);
            detailItem->setBrush(
                QBrush(InsightVisualStyle::theme().textSecondary));
            detailItem->setPos(rect.left() + 10, rect.top() + 32);
        }
    }

    NavigateHandler navigateHandler;
    SelectHandler selectHandler;
    HoverHandler hoverHandler;

    void setBaseZValues(qreal normalZ, qreal hoverZ)
    {
        normalZValue = normalZ;
        hoverZValue = hoverZ;
        setZValue(hovered ? hoverZValue : normalZValue);
    }

    void setHoveredForTest(bool value)
    {
        hovered = value;
        setData(kGraphHoverActiveRole, value);
        refreshVisual();
    }

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override
    {
        if (hoverHandler)
            hoverHandler(this, true);
        else
            setHoveredForTest(true);
        QGraphicsRectItem::hoverEnterEvent(event);
    }

    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override
    {
        if (hoverHandler)
            hoverHandler(this, false);
        else
            setHoveredForTest(false);
        QGraphicsRectItem::hoverLeaveEvent(event);
    }

    void mousePressEvent(QGraphicsSceneMouseEvent* event) override
    {
        setSelected(true);
        const int canonicalNodeId =
            data(kGraphFsmCanonicalNodeIdRole).toInt();
        if (scene() && canonicalNodeId >= 0) {
            for (QGraphicsItem* item : scene()->items()) {
                if (item == this)
                    continue;
                const QVariant itemCanonical =
                    item->data(kGraphFsmCanonicalNodeIdRole);
                if (itemCanonical.isValid()
                    && itemCanonical.toInt() == canonicalNodeId) {
                    item->setSelected(true);
                }
            }
        }
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

class RtlInsightGraphEdgeItem : public QGraphicsPathItem
{
public:
    using NavigateHandler = std::function<void(const RtlInsightGraphElement&)>;
    using SelectHandler = std::function<void(const RtlInsightGraphElement&)>;
    using HoverHandler = std::function<void(QGraphicsItem*, bool)>;

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
        normalColor = color;
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
        normalLabelColor = resolvedLabelColor;
        setData(kGraphEdgeLabelColorRole, resolvedLabelColor.name());
        setData(kGraphEdgeRouteKindRole, presentation.routeKind);
        setData(kGraphEdgeRouteLaneRole, presentation.routeLane);
        setData(kGraphEdgeHitPriorityRole, 200);
        setData(kGraphEdgeBridgeRole, QStringLiteral("none"));
        setData(kGraphEdgeOverlapRole, presentation.overlapIds);
        setData(kGraphEdgePathLengthRole, path.length());
        setData(kGraphEdgeRouteDecisionRole, presentation.routeDecision);
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
        arrowItem = new QGraphicsPolygonItem(arrow, this);
        arrowItem->setAcceptedMouseButtons(Qt::NoButton);
        arrowItem->setPen(QPen(color, 1.0));
        arrowItem->setBrush(QBrush(color));

        if (!label.isEmpty()) {
            QFont labelFont = font;
            labelFont.setPointSize(qMax(8, labelFont.pointSize() - 1));
            labelFont.setBold(labelBold);
            auto* labelItem = new QGraphicsTextItem(label, this);
            labelTextItem = labelItem;
            labelItem->setAcceptedMouseButtons(Qt::NoButton);
            labelItem->setFont(labelFont);
            const qreal labelTextWidth = qBound<qreal>(
                28.0,
                QFontMetrics(labelFont).horizontalAdvance(label) + 12.0,
                220.0);
            labelItem->setTextWidth(labelTextWidth);
            labelItem->setDefaultTextColor(resolvedLabelColor);
            labelItem->setToolTip(toolTip());
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
            labelHitRect =
                labelBounds.translated(labelItem->pos())
                    .adjusted(-8.0, -6.0, 8.0, 6.0);
            setData(kGraphEdgeLabelRectRole, labelHitRect);
            setData(kGraphEdgeLabelXRole, labelCenter.x());
            setData(kGraphEdgeLabelYRole, labelCenter.y());
        }
    }

    NavigateHandler navigateHandler;
    SelectHandler selectHandler;
    HoverHandler hoverHandler;

    void setHoveredForTest(bool value)
    {
        hovered = value;
        setData(kGraphHoverActiveRole, value);
        setZValue(hovered ? 30.0 : 2.0);
        refreshPen();
    }

    void setLinkedToolTip(const QString& text)
    {
        setToolTip(text);
        if (labelTextItem)
            labelTextItem->setToolTip(text);
    }

    QPainterPath shape() const override
    {
        QPainterPath result = QGraphicsPathItem::shape();
        if (!labelHitRect.isNull())
            result.addRoundedRect(labelHitRect, 5.0, 5.0);
        return result;
    }

protected:
    void paint(QPainter* painter,
               const QStyleOptionGraphicsItem* option,
               QWidget* widget = nullptr) override
    {
        QStyleOptionGraphicsItem cleanOption(*option);
        cleanOption.state &= ~QStyle::State_Selected;
        QGraphicsPathItem::paint(painter, &cleanOption, widget);
    }

    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override
    {
        if (hoverHandler)
            hoverHandler(this, true);
        else
            setHoveredForTest(true);
        QGraphicsPathItem::hoverEnterEvent(event);
    }

    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override
    {
        if (hoverHandler)
            hoverHandler(this, false);
        else
            setHoveredForTest(false);
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
        QColor activeColor = normalColor;
        QColor activeLabelColor = normalLabelColor;
        if (isSelected()) {
            const QPen selected = InsightVisualStyle::selectedPen(2.5);
            setPen(selected);
            activeColor = selected.color();
            activeLabelColor = activeColor;
        } else if (hovered) {
            setPen(hoverPen);
            activeColor = hoverPen.color();
            activeLabelColor = activeColor;
        } else {
            setPen(normalPen);
        }
        if (arrowItem) {
            arrowItem->setPen(QPen(activeColor, 1.0));
            arrowItem->setBrush(QBrush(activeColor));
        }
        if (labelTextItem)
            labelTextItem->setDefaultTextColor(activeLabelColor);
    }

    RtlInsightGraphElement element;
    QPen normalPen;
    QPen hoverPen;
    QColor normalColor;
    QColor normalLabelColor;
    QGraphicsPolygonItem* arrowItem = nullptr;
    QGraphicsTextItem* labelTextItem = nullptr;
    QRectF labelHitRect;
    bool hovered = false;
};

void setGraphItemHoverVisual(QGraphicsItem* item, bool hovered)
{
    if (auto* node = dynamic_cast<RtlInsightGraphNodeItem*>(item)) {
        node->setHoveredForTest(hovered);
    } else if (auto* edge = dynamic_cast<RtlInsightGraphEdgeItem*>(item)) {
        edge->setHoveredForTest(hovered);
    }
}

void applyFsmGraphHover(QGraphicsScene* scene,
                        QGraphicsItem* sourceItem,
                        bool hovered)
{
    if (!scene)
        return;

    const QList<QGraphicsItem*> items = scene->items();
    for (QGraphicsItem* item : items)
        setGraphItemHoverVisual(item, false);
    if (!hovered || !sourceItem)
        return;

    if (dynamic_cast<RtlInsightGraphEdgeItem*>(sourceItem)) {
        setGraphItemHoverVisual(sourceItem, true);
        return;
    }

    const auto* sourceNode =
        dynamic_cast<RtlInsightGraphNodeItem*>(sourceItem);
    if (!sourceNode)
        return;
    const int canonicalNodeId =
        sourceNode->data(kGraphFsmCanonicalNodeIdRole).toInt();
    if (canonicalNodeId < 0) {
        setGraphItemHoverVisual(sourceItem, true);
        return;
    }

    QHash<int, int> canonicalByNodeId;
    for (QGraphicsItem* item : items) {
        if (!dynamic_cast<RtlInsightGraphNodeItem*>(item))
            continue;
        canonicalByNodeId.insert(item->data(kGraphFsmNodeIdRole).toInt(),
                                 item->data(kGraphFsmCanonicalNodeIdRole).toInt());
        if (item->data(kGraphFsmCanonicalNodeIdRole).toInt()
            == canonicalNodeId) {
            setGraphItemHoverVisual(item, true);
        }
    }
    for (QGraphicsItem* item : items) {
        if (!dynamic_cast<RtlInsightGraphEdgeItem*>(item))
            continue;
        const int fromCanonical = canonicalByNodeId.value(
            item->data(kGraphEdgeFromNodeIdRole).toInt(), -1);
        const int toCanonical = canonicalByNodeId.value(
            item->data(kGraphEdgeToNodeIdRole).toInt(), -1);
        if (fromCanonical == canonicalNodeId
            || toCanonical == canonicalNodeId) {
            setGraphItemHoverVisual(item, true);
        }
    }
}

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

QPainterPath fsmLayoutEdgePath(const FsmLayoutEdge& edge)
{
    QPainterPath path;
    if (edge.points.isEmpty())
        return path;
    path.moveTo(edge.points.first());
    if (edge.selfLoop && edge.points.size() >= 4) {
        path.cubicTo(edge.points.at(1), edge.points.at(2), edge.points.at(3));
        return path;
    }
    if (edge.curved && edge.points.size() == 7) {
        path.cubicTo(edge.points.at(1),
                     edge.points.at(2),
                     edge.points.at(3));
        path.cubicTo(edge.points.at(4),
                     edge.points.at(5),
                     edge.points.at(6));
        return path;
    }
    if (edge.curved && edge.points.size() == 4) {
        path.cubicTo(edge.points.at(1),
                     edge.points.at(2),
                     edge.points.at(3));
        return path;
    }
    if (edge.curved && edge.points.size() >= 3) {
        path.quadTo(edge.points.at(1), edge.points.at(2));
        return path;
    }
    if (edge.points.size() <= 2) {
        for (int i = 1; i < edge.points.size(); ++i)
            path.lineTo(edge.points.at(i));
        return path;
    }
    constexpr qreal cornerRadius = 12.0;
    for (int i = 1; i < edge.points.size() - 1; ++i) {
        const QPointF previous = edge.points.at(i - 1);
        const QPointF corner = edge.points.at(i);
        const QPointF next = edge.points.at(i + 1);
        const QLineF inLine(corner, previous);
        const QLineF outLine(corner, next);
        const qreal inRadius = qMin(cornerRadius, inLine.length() / 2.0);
        const qreal outRadius = qMin(cornerRadius, outLine.length() / 2.0);
        const QPointF before =
            inLine.pointAt(inLine.length() > 0.1
                               ? inRadius / inLine.length()
                               : 0.0);
        const QPointF after =
            outLine.pointAt(outLine.length() > 0.1
                                ? outRadius / outLine.length()
                                : 0.0);
        path.lineTo(before);
        path.quadTo(corner, after);
    }
    path.lineTo(edge.points.last());
    return path;
}

QRectF insightNodeRectAt(qreal centerX, qreal centerY)
{
    return QRectF(centerX - kInsightNodeWidth / 2.0,
                  centerY - kInsightNodeHeight / 2.0,
                  kInsightNodeWidth,
                  kInsightNodeHeight);
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

bool edgeLabelContainsScenePoint(const QGraphicsItem* item,
                                 const QPointF& scenePoint)
{
    if (!dynamic_cast<const RtlInsightGraphEdgeItem*>(item))
        return false;
    const QRectF labelRect =
        item->data(kGraphEdgeLabelRectRole).toRectF();
    return !labelRect.isNull()
        && labelRect.contains(item->mapFromScene(scenePoint));
}

QGraphicsItem* graphItemAtScenePoint(QGraphicsScene* scene,
                                     const QPointF& scenePoint)
{
    if (!scene)
        return nullptr;

    struct Candidate {
        QGraphicsItem* item = nullptr;
        int priority = -1;
        qreal z = 0.0;
    };

    Candidate best;
    for (QGraphicsItem* item : scene->items()) {
        if (!item)
            continue;
        const QString kind = item->data(kGraphKindRole).toString();
        const bool isEdge = dynamic_cast<RtlInsightGraphEdgeItem*>(item);
        const bool isNode = dynamic_cast<RtlInsightGraphNodeItem*>(item);
        if (!isEdge && !isNode)
            continue;
        const QPointF localPoint = item->mapFromScene(scenePoint);
        if (!item->shape().contains(localPoint))
            continue;

        int priority = isNode ? 100 : 200;
        if (isEdge && edgeLabelContainsScenePoint(item, scenePoint))
            priority = 300;
        if (kind == QStringLiteral("transition"))
            priority += 20;

        if (!best.item
            || priority > best.priority
            || (priority == best.priority && item->zValue() > best.z)) {
            best = Candidate{item, priority, item->zValue()};
        }
    }
    return best.item;
}

bool isGraphShapeItem(const QGraphicsItem* item)
{
    return dynamic_cast<const RtlInsightGraphNodeItem*>(item)
        || dynamic_cast<const RtlInsightGraphEdgeItem*>(item);
}

bool isGraphNodeShapeItem(const QGraphicsItem* item)
{
    return dynamic_cast<const RtlInsightGraphNodeItem*>(item);
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

QString fsmTransitionConditionId(int transitionIndex)
{
    return QStringLiteral("C%1").arg(transitionIndex);
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
    insightsGraphView->setPressHandler(
        [this](const QPoint& viewPoint,
               Qt::MouseButton button,
               Qt::KeyboardModifiers) {
            if (button != Qt::LeftButton
                || !insightsGraphView
                || (currentGraphMode != QStringLiteral("state-transition")
                    && currentGraphMode != QStringLiteral("fsm"))) {
                return false;
            }
            return selectGraphItemAtScenePoint(
                insightsGraphView->mapToScene(viewPoint));
        });

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
        if (dynamic_cast<RtlInsightGraphNodeItem*>(item))
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

QStringList RtlInsightsPanelCoordinator::graphHoveredElementSummariesForTest() const
{
    QStringList summaries;
    if (!insightsGraphScene)
        return summaries;
    for (QGraphicsItem* item : insightsGraphScene->items()) {
        if (!isGraphShapeItem(item)
            || !item->data(kGraphHoverActiveRole).toBool()) {
            continue;
        }
        summaries.append(
            QStringLiteral("%1|%2|%3")
                .arg(item->data(kGraphKindRole).toString(),
                     item->data(kGraphPrimaryRole).toString(),
                     item->data(kGraphSecondaryRole).toString()));
    }
    summaries.sort(Qt::CaseInsensitive);
    return summaries;
}

QString RtlInsightsPanelCoordinator::graphItemToolTipForTest(
    const QString& elementKind,
    const QString& primaryText,
    const QString& secondaryText) const
{
    QGraphicsItem* item = graphItemByData(insightsGraphScene,
                                          elementKind,
                                          primaryText,
                                          secondaryText);
    return item ? item->toolTip() : QString();
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
        if (dynamic_cast<RtlInsightGraphNodeItem*>(item)) {
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
    if (!isGraphShapeItem(item))
        return false;
    applyFsmGraphHover(insightsGraphScene, item, hovered);
    return true;
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
    if (insightsGraphScene)
        insightsGraphScene->clearSelection();
    item->setSelected(true);
    if (dynamic_cast<RtlInsightGraphNodeItem*>(item)) {
        const QVariant canonicalData =
            item->data(kGraphFsmCanonicalNodeIdRole);
        if (canonicalData.isValid() && insightsGraphScene) {
            const int canonicalNodeId = canonicalData.toInt();
            for (QGraphicsItem* candidate : insightsGraphScene->items()) {
                const QVariant candidateCanonicalData =
                    candidate->data(kGraphFsmCanonicalNodeIdRole);
                if (dynamic_cast<RtlInsightGraphNodeItem*>(candidate)
                    && candidateCanonicalData.isValid()
                    && candidateCanonicalData.toInt() == canonicalNodeId) {
                    candidate->setSelected(true);
                }
            }
        }
    }
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

bool RtlInsightsPanelCoordinator::selectGraphItemForInspector(QGraphicsItem* item)
{
    if (!item || !insightsGraphScene)
        return false;
    insightsGraphScene->clearSelection();
    item->setSelected(true);
    const QString kind = item->data(kGraphKindRole).toString();
    const QString primary = item->data(kGraphPrimaryRole).toString();
    QStringList rows{QStringLiteral("Kind=%1").arg(kind)};
    const QString badge = item->data(kGraphBadgeRole).toString();
    if (!badge.isEmpty())
        rows.append(QStringLiteral("ID=%1").arg(badge));
    rows.append(QStringLiteral("To=%1")
                    .arg(item->data(kGraphSecondaryRole).toString()));
    const QString detailLabel =
        kind == QStringLiteral("transition")
            ? QStringLiteral("Condition")
            : QStringLiteral("Detail");
    rows.append(QStringLiteral("%1=%2")
                    .arg(detailLabel,
                         item->data(kGraphDetailRole).toString()));
    rows.append(QStringLiteral("Location=%1:%2")
                    .arg(item->data(kGraphFileRole).toString(),
                         item->data(kGraphLineRole).toString()));
    renderGenericGraphInspector(primary, rows);
    if (statusMessageHandler)
        statusMessageHandler(QStringLiteral("%1: %2").arg(kind, primary),
                             1200);
    return true;
}

bool RtlInsightsPanelCoordinator::selectGraphItemAtScenePoint(
    const QPointF& scenePoint)
{
    return selectGraphItemForInspector(
        graphItemAtScenePoint(insightsGraphScene, scenePoint));
}

bool RtlInsightsPanelCoordinator::selectGraphItemAtScenePointForTest(
    qreal sceneX,
    qreal sceneY)
{
    return selectGraphItemAtScenePoint(QPointF(sceneX, sceneY));
}

QString RtlInsightsPanelCoordinator::graphItemAtScenePointSummaryForTest(
    qreal sceneX,
    qreal sceneY) const
{
    QGraphicsItem* item =
        graphItemAtScenePoint(insightsGraphScene, QPointF(sceneX, sceneY));
    if (!item)
        return QString();
    return QStringLiteral("%1|%2|%3|%4")
        .arg(item->data(kGraphKindRole).toString(),
             item->data(kGraphPrimaryRole).toString(),
             item->data(kGraphSecondaryRole).toString(),
             item->data(kGraphBadgeRole).toString());
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

void RtlInsightsPanelCoordinator::renderFsmGraphLayoutScene(
    const FsmGraph& graph,
    const QString& title,
    const QString& mode)
{
    showGraphSurface();
    if (!insightsGraphScene || !insightsGraphView)
        return;
    configureGraphToolbarForMode(mode);
    clearGraphDetails();
    insightsGraphScene->clear();
    insightsGraphView->resetView();
    lastGraphFitRect = QRectF();

    const InsightTheme theme = InsightVisualStyle::theme();
    const QFont font = readableGraphFont(insightsGraphView->font());
    QFont titleMeasureFont = InsightVisualStyle::titleFont(font);
    titleMeasureFont.setPointSize(qMax(8, titleMeasureFont.pointSize() + 1));

    FsmLayoutOptions options;
    options.direction = FsmLayoutDirection::TopDown;
    options.textWidth = [titleMeasureFont](const QString& text) {
        return static_cast<qreal>(
            QFontMetrics(titleMeasureFont).horizontalAdvance(text));
    };
    const FsmGraphLayout layout = layoutFsmGraph(graph, options);
    if (layout.nodes.isEmpty()) {
        renderGraphUnavailable(title,
                               QStringLiteral("No FSM states are available."));
        return;
    }

    QSet<int> aliasCanonicalIds;
    for (const FsmLayoutNode& node : layout.nodes) {
        if (node.alias)
            aliasCanonicalIds.insert(node.canonicalNodeId);
    }
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
        renderGenericGraphInspector(
            element.primary,
            {QStringLiteral("Kind=%1").arg(element.kind),
             element.badge.isEmpty()
                 ? QString()
                 : QStringLiteral("ID=%1").arg(element.badge),
             element.secondary.isEmpty()
                 ? QString()
                 : QStringLiteral("To=%1").arg(element.secondary),
             QStringLiteral("%1=%2")
                 .arg(element.kind == QStringLiteral("transition")
                          ? QStringLiteral("Condition")
                          : QStringLiteral("Detail"),
                      element.detail),
             QStringLiteral("Location=%1:%2")
                 .arg(element.codeLink.fileDisplayName,
                      element.codeLink.lineDisplayName)});
        if (statusMessageHandler)
            statusMessageHandler(QStringLiteral("%1: %2")
                                     .arg(element.kind, element.primary),
                                 1200);
    };
    const auto hover = [scene = insightsGraphScene](QGraphicsItem* item,
                                                    bool hovered) {
        applyFsmGraphHover(scene, item, hovered);
    };

    for (const FsmLayoutEdge& edge : layout.edges) {
        const QPainterPath path = fsmLayoutEdgePath(edge);
        if (path.isEmpty())
            continue;
        RtlInsightGraphElement element;
        element.kind = QStringLiteral("transition");
        element.primary = edge.fromState;
        element.secondary = edge.toState;
        element.detail = edge.condition;
        element.badge = edge.label;
        element.codeLink = edge.codeLink;

        RtlInsightGraphEdgePresentation presentation;
        presentation.hasLabelCenter = true;
        presentation.labelCenter = edge.labelAnchor;
        presentation.routeKind = edge.selfLoop
            ? QStringLiteral("self-loop")
            : QStringLiteral("sugiyama");
        presentation.routeLane = edge.layoutFromRank;
        presentation.routeDecision = edge.reversedForLayout
            ? QStringLiteral("dfs-back-edge-reversed")
            : QStringLiteral("dag-edge");

        auto* item = new RtlInsightGraphEdgeItem(
            element,
            path,
            edge.arrowTip,
            edge.arrowAngle,
            edge.label,
            font,
            theme.graph.edge,
            0.0,
            true,
            false,
            theme.textPrimary,
            presentation);
        item->navigateHandler = navigate;
        item->selectHandler = select;
        item->hoverHandler = hover;
        item->setLinkedToolTip(
            wrappedFsmConditionToolTip(edge.label, edge.condition));
        item->setData(kGraphFsmNodeIdRole, edge.transitionIndex);
        item->setData(kGraphEdgeFromNodeIdRole, edge.fromNodeId);
        item->setData(kGraphEdgeToNodeIdRole, edge.toNodeId);
        insightsGraphScene->addItem(item);
    }

    for (const FsmLayoutNode& node : layout.nodes) {
        RtlInsightGraphElement element;
        element.kind = QStringLiteral("state");
        element.primary = node.displayName;
        element.secondary = node.deadEndState
            ? QStringLiteral("dead/end state")
            : graph.stateRegisterDisplayName;
        element.detail = node.detail;
        element.badge = node.deadEndState
            ? QStringLiteral("dead/end")
            : QString();
        element.codeLink = node.codeLink;

        const QColor fill = fsmStateFillColor(node.stateName,
                                              aliasCanonicalIds,
                                              node.canonicalNodeId,
                                              node.deadEndState);
        const QColor stroke = fsmStateStrokeColor(node.stateName,
                                                  aliasCanonicalIds,
                                                  node.canonicalNodeId,
                                                  node.deadEndState);
        auto* item = new RtlInsightGraphNodeItem(element,
                                                node.rect,
                                                fill,
                                                stroke,
                                                font,
                                                node.alias || node.implicitState,
                                                node.canonicalNodeId,
                                                node.showDetail);
        item->setData(kGraphFsmNodeIdRole, node.nodeId);
        item->setData(kGraphFsmCanonicalNodeIdRole, node.canonicalNodeId);
        item->navigateHandler = navigate;
        item->selectHandler = select;
        item->hoverHandler = hover;
        insightsGraphScene->addItem(item);
    }

    insightsGraphScene->setSceneRect(layout.sceneBounds);
    lastGraphFitRect = layout.sceneBounds;
    insightsGraphView->fitRect(lastGraphFitRect, Qt::KeepAspectRatio);
    populateFsmTransitionsTable(graph);
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

    renderFsmGraphLayoutScene(
        report.graph,
        report.groupDisplayName.isEmpty()
            ? QStringLiteral("State Transition Graph")
            : report.groupDisplayName,
        QStringLiteral("state-transition"));
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
        renderFsmGraphLayoutScene(
            firstGraph,
            title.isEmpty() ? QStringLiteral("FSM Graph") : title,
            QStringLiteral("fsm"));
    }
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
