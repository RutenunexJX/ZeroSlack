#include "rtlinsightsgraphscenemapper.h"

#include "fsmgraphlayout.h"
#include "fsmgraphservice.h"
#include "insightgraphview.h"
#include "insightvisualstyle.h"
#include "moduleblockdiagramservice.h"
#include "rtlinsightsgraphcontroller.h"
#include "rtlinsightsgraphconstants.h"
#include "rtlinsightspanelviewstate.h"
#include "statetransitiongraphservice.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsTextItem>
#include <QHeaderView>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStyle>
#include <QStyleOptionGraphicsItem>
#include <QTableWidget>
#include <QTimer>
#include <QTreeWidget>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace {


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

QSet<QString> deadFsmStateNames(const FsmGraph& graph)
{
    QSet<QString> names;
    for (const FsmStateRow& row : graph.stateRows) {
        if (row.deadEndState)
            names.insert(row.stateDisplayName);
    }
    return names;
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

QString fsmTransitionConditionId(int transitionIndex)
{
    return QStringLiteral("C%1").arg(transitionIndex);
}

} // namespace

RtlInsightsGraphSceneMapper::RtlInsightsGraphSceneMapper(
    RtlInsightsPanelViewState& viewState,
    RtlInsightsGraphController& graphController)
    : state(viewState),
      controller(graphController)
{
}

QGraphicsItem*
RtlInsightsGraphSceneMapper::itemAtScenePoint(
    const QPointF& scenePoint) const
{
    return graphItemAtScenePoint(
        state.insightsGraphScene,
        scenePoint);
}
void RtlInsightsGraphSceneMapper::renderFsmGraphLayoutScene(
    const FsmGraph& graph,
    const QString& title,
    const QString& mode)
{
    controller.showGraphSurface();
    if (!state.insightsGraphScene || !state.insightsGraphView)
        return;
    controller.configureToolbarForMode(mode);
    clearDetails();
    state.insightsGraphScene->clear();
    state.insightsGraphView->resetView();
    state.lastGraphFitRect = QRectF();

    const InsightTheme theme = InsightVisualStyle::theme();
    const QFont font = readableGraphFont(state.insightsGraphView->font());
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
        controller.renderUnavailable(title,
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
        if (state.navigationHandler && !link.fileName.isEmpty()
            && !state.navigationHandler(link.fileName, link.line, link.column)
            && state.statusMessageHandler) {
            state.statusMessageHandler(QStringLiteral("RTL graph jump failed"),
                                 4000);
        }
    };
    const auto select = [this](const RtlInsightGraphElement& element) {
        renderGenericInspector(
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
        if (state.statusMessageHandler)
            state.statusMessageHandler(QStringLiteral("%1: %2")
                                     .arg(element.kind, element.primary),
                                 1200);
    };
    const auto hover = [scene = state.insightsGraphScene](QGraphicsItem* item,
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
        state.insightsGraphScene->addItem(item);
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
        state.insightsGraphScene->addItem(item);
    }

    state.insightsGraphScene->setSceneRect(layout.sceneBounds);
    state.lastGraphFitRect = layout.sceneBounds;
    state.insightsGraphView->fitRect(state.lastGraphFitRect, Qt::KeepAspectRatio);
    populateFsmTransitionsTable(graph);
    applySearchHighlight();
}

void RtlInsightsGraphSceneMapper::renderStateTransitionGraphScene(
    const StateTransitionGraphReport& report)
{
    controller.showGraphSurface();
    if (!state.insightsGraphScene || !state.insightsGraphView)
        return;
    controller.configureToolbarForMode(QStringLiteral("state-transition"));
    clearDetails();
    state.insightsGraphScene->clear();
    state.insightsGraphView->resetView();
    state.lastGraphFitRect = QRectF();

    if (!report.found) {
        controller.renderUnavailable(
            report.groupDisplayName.isEmpty()
                ? QStringLiteral("State Transition Graph")
                : report.groupDisplayName,
            report.notFoundReasonDisplayName);
        return;
    }
    if (state.stateTransitionSignalCombo) {
        QSignalBlocker blocker(state.stateTransitionSignalCombo);
        state.stateTransitionSignalCombo->clear();
        state.stateTransitionSignalCombo->addItem(report.selectedSignalDisplayName);
    }
    if (state.stateTransitionCurrentCombo) {
        QSignalBlocker blocker(state.stateTransitionCurrentCombo);
        state.stateTransitionCurrentCombo->clear();
        state.stateTransitionCurrentCombo->addItem(
            report.graph.stateRegisterDisplayName);
    }
    if (state.stateTransitionNextCombo) {
        QSignalBlocker blocker(state.stateTransitionNextCombo);
        state.stateTransitionNextCombo->clear();
        state.stateTransitionNextCombo->addItem(
            report.graph.nextStateSignalDisplayName);
    }

    renderFsmGraphLayoutScene(
        report.graph,
        report.groupDisplayName.isEmpty()
            ? QStringLiteral("State Transition Graph")
            : report.groupDisplayName,
        QStringLiteral("state-transition"));
}

void RtlInsightsGraphSceneMapper::renderFsmGraphScene(
    const FsmGraphReport& report,
    const QString& title)
{
    controller.showGraphSurface();
    if (!state.insightsGraphScene || !state.insightsGraphView)
        return;
    controller.configureToolbarForMode(QStringLiteral("fsm"));
    clearDetails();
    state.insightsGraphScene->clear();
    state.insightsGraphView->resetView();
    state.lastGraphFitRect = QRectF();

    if (!report.found || report.graphs.isEmpty()) {
        controller.renderUnavailable(
            title.isEmpty() ? QStringLiteral("FSM Graph") : title,
            report.notFoundReasonDisplayName.isEmpty()
                ? QStringLiteral("No FSM graph")
                : report.notFoundReasonDisplayName);
        return;
    }

    if (!report.graphs.isEmpty()) {
        const FsmGraph& firstGraph = report.graphs.first();
        if (state.stateTransitionSignalCombo) {
            QSignalBlocker blocker(state.stateTransitionSignalCombo);
            state.stateTransitionSignalCombo->clear();
            state.stateTransitionSignalCombo->addItem(
                firstGraph.nextStateSignalDisplayName);
        }
        if (state.stateTransitionCurrentCombo) {
            QSignalBlocker blocker(state.stateTransitionCurrentCombo);
            state.stateTransitionCurrentCombo->clear();
            state.stateTransitionCurrentCombo->addItem(
                firstGraph.stateRegisterDisplayName);
        }
        if (state.stateTransitionNextCombo) {
            QSignalBlocker blocker(state.stateTransitionNextCombo);
            state.stateTransitionNextCombo->clear();
            state.stateTransitionNextCombo->addItem(
                firstGraph.nextStateSignalDisplayName);
        }
        renderFsmGraphLayoutScene(
            firstGraph,
            title.isEmpty() ? QStringLiteral("FSM Graph") : title,
            QStringLiteral("fsm"));
    }
}

void RtlInsightsGraphSceneMapper::renderModuleBlockDiagramScene(
    const ModuleBlockDiagramReport& report)
{
    controller.showGraphSurface();
    if (!state.insightsGraphScene || !state.insightsGraphView)
        return;
    controller.configureToolbarForMode(QStringLiteral("module-block"));
    clearDetails();
    state.currentModuleBlockReport = report;
    state.insightsGraphScene->clear();
    state.insightsGraphView->resetView();

    if (!report.found) {
        controller.renderUnavailable(
            report.groupDisplayName.isEmpty()
                ? QStringLiteral("Module Block Diagram")
                : report.groupDisplayName,
            report.notFoundReasonDisplayName);
        return;
    }
    if (state.moduleBlockTopCombo) {
        QSignalBlocker blocker(state.moduleBlockTopCombo);
        state.moduleBlockTopCombo->clear();
        for (const ModuleBlockDiagramNode& node : report.nodes) {
            if (node.unresolved)
                continue;
            state.moduleBlockTopCombo->addItem(node.moduleDisplayName,
                                         node.nodeId);
        }
        state.moduleBlockTopCombo->setCurrentText(report.root.moduleDisplayName);
    }

    const QFont font = state.insightsGraphView->font();
    const auto navigate = [this](const RtlInsightGraphElement& element) {
        const RtlInsightCodeLink& link = element.codeLink;
        if (state.navigationHandler && !link.fileName.isEmpty()
            && !state.navigationHandler(link.fileName, link.line, link.column)
            && state.statusMessageHandler) {
            state.statusMessageHandler(QStringLiteral("RTL graph jump failed"),
                                 4000);
        }
        if (!element.drillModuleName.isEmpty()) {
            const QString drillFileName = element.drillFileName.isEmpty()
                ? link.fileName
                : element.drillFileName;
            const QString drillModuleName = element.drillModuleName;
            QTimer::singleShot(0, state.insightsDock, [this,
                                                 drillFileName,
                                                 drillModuleName]() {
                if (state.moduleBlockDiagramRequestHandler) {
                    state.moduleBlockDiagramRequestHandler(
                        drillFileName,
                        drillModuleName);
                }
            });
        }
    };
    const auto select = [this](const RtlInsightGraphElement& element) {
        renderGenericInspector(
            element.primary,
            {QStringLiteral("Kind=%1").arg(element.kind),
             QStringLiteral("Detail=%1").arg(element.detail),
             QStringLiteral("Location=%1:%2")
                 .arg(element.codeLink.fileDisplayName,
                      element.codeLink.lineDisplayName)});
        if (state.statusMessageHandler)
            state.statusMessageHandler(QStringLiteral("%1: %2")
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
        state.insightsGraphScene->addItem(item);
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
        state.insightsGraphScene->addItem(item);
    };

    QHash<int, ModuleBlockDiagramNode> nodeById;
    QHash<int, QList<ModuleBlockDiagramNode>> childrenByParent;
    QSet<int> visibleNodeIds;
    visibleNodeIds.insert(report.root.nodeId);
    const bool showUnresolved =
        !state.moduleBlockShowUnresolvedCheck
        || state.moduleBlockShowUnresolvedCheck->isChecked();
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
    auto* titleItem = state.insightsGraphScene->addSimpleText(
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
    auto* summaryItem = state.insightsGraphScene->addSimpleText(
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
            state.insightsGraphScene->addSimpleText(noChildText,
                                              InsightVisualStyle::compactFont(font));
        label->setBrush(QBrush(InsightVisualStyle::theme().warning));
        label->setPos(rootRect.center().x() - 70,
                      rootRect.center().y() - 10);
    }

    const QRectF bounds =
        state.insightsGraphScene->itemsBoundingRect().adjusted(-48, -48, 48, 44);
    state.insightsGraphScene->setSceneRect(bounds);
    state.lastGraphFitRect = bounds;
    state.insightsGraphView->fitRect(bounds, Qt::KeepAspectRatio);
    populateModuleBlockInstancesTable(report);
    selectModuleBlockNode(report.root.nodeId, false, false);
    applySearchHighlight();
}
void RtlInsightsGraphSceneMapper::applySearchHighlight()
{
    if (!state.insightsGraphScene)
        return;

    const QString needle = state.graphSearchText.trimmed();
    const bool active = !needle.isEmpty();
    int firstMatchCount = 0;
    QPointF firstMatchCenter;
    for (QGraphicsItem* item : state.insightsGraphScene->items()) {
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
    if (firstMatchCount > 0
        && state.insightsGraphView) {
        state.insightsGraphView->centerOnPoint(
            firstMatchCenter);
    }
    if (state.statusMessageHandler && active) {
        state.statusMessageHandler(
            QStringLiteral("%1 graph match(es)")
                .arg(firstMatchCount),
            1200);
    }
}
void RtlInsightsGraphSceneMapper::clearDetails()
{
    if (state.graphInspector)
        state.graphInspector->clear();
    if (state.graphTable) {
        state.graphTable->clear();
        state.graphTable->setRowCount(0);
        state.graphTable->setColumnCount(0);
    }
    state.currentModuleBlockSelectedNodeId = -1;
    if (state.graphInspectorJumpButton)
        state.graphInspectorJumpButton->setEnabled(false);
    if (state.graphInspectorFocusButton)
        state.graphInspectorFocusButton->setEnabled(false);
    if (state.graphInspectorSetTopButton)
        state.graphInspectorSetTopButton->setEnabled(false);
    if (state.graphInspectorRevealButton)
        state.graphInspectorRevealButton->setEnabled(false);
}

void RtlInsightsGraphSceneMapper::renderGenericInspector(
    const QString& title,
    const QStringList& rows)
{
    if (!state.graphInspector)
        return;
    state.graphInspector->clear();
    auto* titleItem = new QTreeWidgetItem(state.graphInspector);
    titleItem->setText(0, QStringLiteral("Selected"));
    titleItem->setText(1, title);
    QFont font = titleItem->font(0);
    font.setBold(true);
    titleItem->setFont(0, font);
    titleItem->setFont(1, font);
    for (const QString& row : rows) {
        const int split = row.indexOf(QLatin1Char('='));
        auto* item = new QTreeWidgetItem(state.graphInspector);
        item->setText(0, split > 0 ? row.left(split) : row);
        item->setText(1, split > 0 ? row.mid(split + 1) : QString());
    }
    if (state.graphInspectorJumpButton)
        state.graphInspectorJumpButton->setEnabled(true);
    if (state.graphInspectorFocusButton)
        state.graphInspectorFocusButton->setEnabled(true);
    if (state.graphInspectorSetTopButton)
        state.graphInspectorSetTopButton->setEnabled(false);
}

void RtlInsightsGraphSceneMapper::renderModuleBlockInspector(
    const ModuleBlockDiagramReport& report,
    const ModuleBlockDiagramNode& node)
{
    if (!state.graphInspector)
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
        auto* item = new QTreeWidgetItem(state.graphInspector);
        item->setText(0, field);
        item->setText(1, value.isEmpty() ? QStringLiteral("-") : value);
        item->setToolTip(1, value);
    };

    state.graphInspector->clear();
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
           QFileInfo(state.currentFileName).dir().dirName());
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

    if (state.graphInspectorJumpButton)
        state.graphInspectorJumpButton->setEnabled(!link.fileName.isEmpty());
    if (state.graphInspectorFocusButton)
        state.graphInspectorFocusButton->setEnabled(true);
    if (state.graphInspectorSetTopButton) {
        state.graphInspectorSetTopButton->setEnabled(
            !node.unresolved && !node.definitionCodeLink.fileName.isEmpty());
    }
}

void RtlInsightsGraphSceneMapper::populateModuleBlockInstancesTable(
    const ModuleBlockDiagramReport& report)
{
    if (!state.graphTable)
        return;
    state.graphTable->clear();
    state.graphTable->setColumnCount(5);
    state.graphTable->setHorizontalHeaderLabels({QStringLiteral("Instance"),
                                           QStringLiteral("Module"),
                                           QStringLiteral("Parent"),
                                           QStringLiteral("File"),
                                           QStringLiteral("Status")});
    state.graphTable->setRowCount(0);

    QHash<int, ModuleBlockDiagramNode> nodesById;
    for (const ModuleBlockDiagramNode& node : report.nodes)
        nodesById.insert(node.nodeId, node);

    const bool showUnresolved =
        !state.moduleBlockShowUnresolvedCheck || state.moduleBlockShowUnresolvedCheck->isChecked();
    for (const ModuleBlockDiagramNode& node : report.nodes) {
        if (node.nodeId == report.root.nodeId)
            continue;
        if (node.unresolved && !showUnresolved)
            continue;
        const int row = state.graphTable->rowCount();
        state.graphTable->insertRow(row);
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
            state.graphTable->setItem(row, column, item);
        }
    }
    state.graphTable->resizeColumnsToContents();
    state.graphTable->horizontalHeader()->setStretchLastSection(true);
}

void RtlInsightsGraphSceneMapper::populateFsmTransitionsTable(
    const FsmGraph& graph)
{
    if (!state.graphTable)
        return;
    state.graphTable->clear();
    state.graphTable->setColumnCount(7);
    state.graphTable->setHorizontalHeaderLabels({QStringLiteral("ID"),
                                           QStringLiteral("From"),
                                           QStringLiteral("To"),
                                           QStringLiteral("Condition"),
                                           QStringLiteral("Type"),
                                           QStringLiteral("Source"),
                                           QStringLiteral("Notes")});
    const QSet<QString> deadStates = deadFsmStateNames(graph);
    state.graphTable->setRowCount(graph.transitionRows.size());
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
            state.graphTable->setItem(row, column, item);
        }
    }
    state.graphTable->resizeColumnsToContents();
    state.graphTable->horizontalHeader()->setStretchLastSection(true);
}

void RtlInsightsGraphSceneMapper::selectModuleBlockNode(
    int nodeId,
    bool centerGraph,
    bool syncTable)
{
    if (nodeId < 0)
        return;
    state.currentModuleBlockSelectedNodeId = nodeId;
    const ModuleBlockDiagramNode* selectedNode = nullptr;
    for (const ModuleBlockDiagramNode& node : state.currentModuleBlockReport.nodes) {
        if (node.nodeId == nodeId) {
            selectedNode = &node;
            break;
        }
    }
    if (!selectedNode)
        return;

    if (state.insightsGraphScene) {
        for (QGraphicsItem* item : state.insightsGraphScene->items()) {
            if (!dynamic_cast<RtlInsightGraphNodeItem*>(item))
                continue;
            const bool match = item->data(kGraphNodeIdRole).toInt() == nodeId;
            item->setSelected(match);
            if (match && centerGraph && state.insightsGraphView)
                state.insightsGraphView->centerOnRect(item->sceneBoundingRect());
        }
    }
    if (syncTable && state.graphTable) {
        for (int row = 0; row < state.graphTable->rowCount(); ++row) {
            const QTableWidgetItem* item = state.graphTable->item(row, 0);
            if (item && item->data(kGraphNodeIdRole).toInt() == nodeId) {
                const QSignalBlocker blocker(state.graphTable);
                state.graphTable->selectRow(row);
                break;
            }
        }
    }
    renderModuleBlockInspector(state.currentModuleBlockReport, *selectedNode);
}

bool RtlInsightsGraphSceneMapper::navigateItem(QGraphicsItem* item)
{
    if (!item || !state.navigationHandler)
        return false;
    const QString fileName = item->data(kGraphFileRole).toString();
    if (fileName.isEmpty())
        return false;
    if (!state.navigationHandler(fileName,
                           item->data(kGraphLineRole).toInt(),
                           item->data(kGraphColumnRole).toInt())) {
        if (state.statusMessageHandler)
            state.statusMessageHandler(QStringLiteral("RTL graph jump failed"),
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
        if (state.moduleBlockDiagramRequestHandler) {
            state.moduleBlockDiagramRequestHandler(
                drillFileName,
                drillModuleName);
        }
    }
    return true;
}

bool RtlInsightsGraphSceneMapper::navigateSelectedItem()
{
    if (!state.insightsGraphScene)
        return false;
    const QList<QGraphicsItem*> selected = state.insightsGraphScene->selectedItems();
    if (selected.isEmpty())
        return false;
    return navigateItem(selected.first());
}

bool RtlInsightsGraphSceneMapper::setModuleBlockTopFromSelected()
{
    if (!state.insightsGraphScene
        || state.currentGraphMode != QStringLiteral("module-block")) {
        return false;
    }
    const QList<QGraphicsItem*> selected = state.insightsGraphScene->selectedItems();
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
    if (state.moduleBlockDiagramRequestHandler) {
        state.moduleBlockDiagramRequestHandler(
            drillFileName,
            drillModuleName);
    }
    return true;
}
int RtlInsightsGraphSceneMapper::nodeItemCountForTest() const
{
    if (!state.insightsGraphScene)
        return 0;
    int count = 0;
    for (QGraphicsItem* item : state.insightsGraphScene->items()) {
        if (dynamic_cast<RtlInsightGraphNodeItem*>(item))
            ++count;
    }
    return count;
}

int RtlInsightsGraphSceneMapper::edgeItemCountForTest() const
{
    if (!state.insightsGraphScene)
        return 0;
    int count = 0;
    for (QGraphicsItem* item : state.insightsGraphScene->items()) {
        if (dynamic_cast<RtlInsightGraphEdgeItem*>(item))
            ++count;
    }
    return count;
}

QStringList RtlInsightsGraphSceneMapper::textItemsForTest() const
{
    QStringList texts;
    if (!state.insightsGraphScene)
        return texts;
    for (QGraphicsItem* item : state.insightsGraphScene->items()) {
        if (auto* simpleText = dynamic_cast<QGraphicsSimpleTextItem*>(item)) {
            texts.append(simpleText->text());
        } else if (auto* text = dynamic_cast<QGraphicsTextItem*>(item)) {
            texts.append(text->toPlainText());
        }
    }
    return texts;
}

QStringList RtlInsightsGraphSceneMapper::elementSummariesForTest() const
{
    QStringList summaries;
    if (!state.insightsGraphScene)
        return summaries;
    for (QGraphicsItem* item : state.insightsGraphScene->items()) {
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

QStringList RtlInsightsGraphSceneMapper::elementVisualSummariesForTest() const
{
    QStringList summaries;
    if (!state.insightsGraphScene)
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
    for (QGraphicsItem* item : state.insightsGraphScene->items()) {
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

QStringList RtlInsightsGraphSceneMapper::hoveredElementSummariesForTest() const
{
    QStringList summaries;
    if (!state.insightsGraphScene)
        return summaries;
    for (QGraphicsItem* item : state.insightsGraphScene->items()) {
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

QString RtlInsightsGraphSceneMapper::itemToolTipForTest(
    const QString& elementKind,
    const QString& primaryText,
    const QString& secondaryText) const
{
    QGraphicsItem* item = graphItemByData(state.insightsGraphScene,
                                          elementKind,
                                          primaryText,
                                          secondaryText);
    return item ? item->toolTip() : QString();
}

QRectF RtlInsightsGraphSceneMapper::lastFitRectForTest() const
{
    return state.lastGraphFitRect;
}

int RtlInsightsGraphSceneMapper::selectedItemCountForTest() const
{
    if (!state.insightsGraphScene)
        return 0;
    int count = 0;
    for (QGraphicsItem* item : state.insightsGraphScene->items()) {
        if ((dynamic_cast<RtlInsightGraphNodeItem*>(item)
             || dynamic_cast<RtlInsightGraphEdgeItem*>(item))
            && item->isSelected()) {
            ++count;
        }
    }
    return count;
}

QStringList RtlInsightsGraphSceneMapper::inspectorRowsForTest() const
{
    QStringList rows;
    if (!state.graphInspector)
        return rows;
    for (int i = 0; i < state.graphInspector->topLevelItemCount(); ++i) {
        const QTreeWidgetItem* item = state.graphInspector->topLevelItem(i);
        if (!item)
            continue;
        rows.append(QStringLiteral("%1=%2")
                        .arg(item->text(0), item->text(1)));
    }
    return rows;
}

QStringList RtlInsightsGraphSceneMapper::tableRowsForTest() const
{
    QStringList rows;
    if (!state.graphTable)
        return rows;
    for (int row = 0; row < state.graphTable->rowCount(); ++row) {
        QStringList cells;
        for (int column = 0; column < state.graphTable->columnCount(); ++column) {
            const QTableWidgetItem* item = state.graphTable->item(row, column);
            cells.append(item ? item->text() : QString());
        }
        rows.append(cells.join(QLatin1Char('|')));
    }
    return rows;
}

bool RtlInsightsGraphSceneMapper::itemsReadableForTest() const
{
    if (!state.insightsGraphScene)
        return true;
    for (QGraphicsItem* item : state.insightsGraphScene->items()) {
        if (!isGraphShapeItem(item))
            continue;
        if (!graphItemReadable(item))
            return false;
    }
    return true;
}

bool RtlInsightsGraphSceneMapper::nestedNodeStackingReadableForTest() const
{
    if (!state.insightsGraphScene)
        return true;
    QList<QGraphicsItem*> nodes;
    for (QGraphicsItem* item : state.insightsGraphScene->items()) {
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

bool RtlInsightsGraphSceneMapper::setItemHoveredForTest(
    const QString& elementKind,
    const QString& primaryText,
    const QString& secondaryText,
    bool hovered)
{
    QGraphicsItem* item = graphItemByData(state.insightsGraphScene,
                                          elementKind,
                                          primaryText,
                                          secondaryText);
    if (!item)
        return false;
    if (!isGraphShapeItem(item))
        return false;
    applyFsmGraphHover(state.insightsGraphScene, item, hovered);
    return true;
}

bool RtlInsightsGraphSceneMapper::selectItemForTest(
    const QString& elementKind,
    const QString& primaryText,
    const QString& secondaryText)
{
    QGraphicsItem* item = graphItemByData(state.insightsGraphScene,
                                          elementKind,
                                          primaryText,
                                          secondaryText);
    if (!item)
        return false;
    if (state.insightsGraphScene)
        state.insightsGraphScene->clearSelection();
    item->setSelected(true);
    if (dynamic_cast<RtlInsightGraphNodeItem*>(item)) {
        const QVariant canonicalData =
            item->data(kGraphFsmCanonicalNodeIdRole);
        if (canonicalData.isValid() && state.insightsGraphScene) {
            const int canonicalNodeId = canonicalData.toInt();
            for (QGraphicsItem* candidate : state.insightsGraphScene->items()) {
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
    renderGenericInspector(primaryText, rows);
    return true;
}

bool RtlInsightsGraphSceneMapper::selectItemForInspector(QGraphicsItem* item)
{
    if (!item || !state.insightsGraphScene)
        return false;
    state.insightsGraphScene->clearSelection();
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
    renderGenericInspector(primary, rows);
    if (state.statusMessageHandler)
        state.statusMessageHandler(QStringLiteral("%1: %2").arg(kind, primary),
                             1200);
    return true;
}

bool RtlInsightsGraphSceneMapper::selectItemAtScenePoint(
    const QPointF& scenePoint)
{
    return selectItemForInspector(
        graphItemAtScenePoint(state.insightsGraphScene, scenePoint));
}

bool RtlInsightsGraphSceneMapper::selectTableRowForTest(
    const QString& primaryText,
    const QString& secondaryText)
{
    if (!state.graphTable)
        return false;
    for (int row = 0; row < state.graphTable->rowCount(); ++row) {
        const QTableWidgetItem* item = state.graphTable->item(row, 0);
        if (!item)
            continue;
        if (item->data(kGraphTablePrimaryRole).toString() != primaryText)
            continue;
        if (!secondaryText.isEmpty()
            && item->data(kGraphTableSecondaryRole).toString()
                   != secondaryText) {
            continue;
        }
        state.graphTable->selectRow(row);
        if (state.currentGraphMode == QStringLiteral("module-block"))
            selectModuleBlockNode(item->data(kGraphNodeIdRole).toInt());
        return true;
    }
    return false;
}

bool RtlInsightsGraphSceneMapper::triggerNavigationForTest(
    const QString& elementKind,
    const QString& primaryText,
    const QString& secondaryText)
{
    QGraphicsItem* item = graphItemByData(state.insightsGraphScene,
                                          elementKind,
                                          primaryText,
                                          secondaryText);
    if (!item || !state.navigationHandler)
        return false;
    item->setSelected(true);
    const QString fileName = item->data(kGraphFileRole).toString();
    if (fileName.isEmpty())
        return false;
    if (!state.navigationHandler(fileName,
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
        if (state.moduleBlockDiagramRequestHandler) {
            state.moduleBlockDiagramRequestHandler(
                drillFileName,
                drillModuleName);
        }
    }
    return true;
}
