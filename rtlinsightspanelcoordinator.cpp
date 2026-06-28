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
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>

namespace {

constexpr int kGraphKindRole = Qt::UserRole + 1200;
constexpr int kGraphPrimaryRole = Qt::UserRole + 1201;
constexpr int kGraphSecondaryRole = Qt::UserRole + 1202;
constexpr int kGraphFileRole = Qt::UserRole + 1203;
constexpr int kGraphLineRole = Qt::UserRole + 1204;
constexpr int kGraphColumnRole = Qt::UserRole + 1205;

constexpr qreal kInsightNodeWidth = 190.0;
constexpr qreal kInsightNodeHeight = 62.0;
constexpr double kPi = 3.14159265358979323846;

struct RtlInsightGraphElement {
    QString kind;
    QString primary;
    QString secondary;
    QString detail;
    RtlInsightCodeLink codeLink;
};

class RtlInsightsGraphView : public QGraphicsView
{
public:
    using QGraphicsView::QGraphicsView;

protected:
    void wheelEvent(QWheelEvent* event) override
    {
        if (!event)
            return;
        const qreal currentScale = transform().m11();
        const qreal factor = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
        const qreal nextScale = currentScale * factor;
        if (nextScale < 0.2 || nextScale > 4.0) {
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
    item->setToolTip(graphElementTooltip(element));
}

class RtlInsightGraphNodeItem : public QGraphicsRectItem
{
public:
    using NavigateHandler = std::function<void(const RtlInsightCodeLink&)>;
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
            navigateHandler(element.codeLink);
            event->accept();
            return;
        }
        QGraphicsRectItem::mouseDoubleClickEvent(event);
    }

private:
    RtlInsightGraphElement element;
};

class RtlInsightGraphEdgeItem : public QGraphicsPathItem
{
public:
    using NavigateHandler = std::function<void(const RtlInsightCodeLink&)>;
    using SelectHandler = std::function<void(const RtlInsightGraphElement&)>;

    RtlInsightGraphEdgeItem(const RtlInsightGraphElement& graphElement,
                            const QPainterPath& path,
                            const QPointF& arrowTip,
                            qreal arrowAngle,
                            const QString& label,
                            const QFont& font,
                            const QColor& color)
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
            auto* labelItem = new QGraphicsTextItem(
                graphElidedText(label, labelFont, 210),
                this);
            labelItem->setAcceptedMouseButtons(Qt::NoButton);
            labelItem->setFont(labelFont);
            labelItem->setDefaultTextColor(QColor(QStringLiteral("#334155")));
            const QRectF pathBounds = path.boundingRect();
            labelItem->setPos(pathBounds.center().x() - 70,
                              pathBounds.center().y() - 26);
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
            navigateHandler(element.codeLink);
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
    actionLayout->addWidget(moduleBriefButton);
    actionLayout->addWidget(signalJourneyButton);
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

bool RtlInsightsPanelCoordinator::triggerGraphNavigationForTest(
    const QString& elementKind,
    const QString& primaryText,
    const QString& secondaryText) const
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
    const auto navigate = [this](const RtlInsightCodeLink& link) {
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

    auto addNode = [&](const RtlInsightGraphElement& element,
                       const QRectF& rect,
                       const QColor& fill,
                       const QColor& stroke) {
        auto* item = new RtlInsightGraphNodeItem(element,
                                                rect,
                                                fill,
                                                stroke,
                                                font);
        item->navigateHandler = navigate;
        item->selectHandler = select;
        insightsGraphScene->addItem(item);
        return item;
    };
    auto addEdge = [&](const RtlInsightGraphElement& element,
                       const QPainterPath& path,
                       const QPointF& tip,
                       qreal angle,
                       const QString& label,
                       const QColor& color) {
        auto* item = new RtlInsightGraphEdgeItem(element,
                                                path,
                                                tip,
                                                angle,
                                                label,
                                                font,
                                                color);
        item->navigateHandler = navigate;
        item->selectHandler = select;
        insightsGraphScene->addItem(item);
        return item;
    };

    const QRectF stateRegisterRect = insightNodeRectAt(-140, -130);
    RtlInsightGraphElement stateRegister;
    stateRegister.kind = QStringLiteral("state-register");
    stateRegister.primary = report.graph.stateRegisterDisplayName;
    stateRegister.secondary = report.graph.stateRegisterTypeDisplayName;
    stateRegister.detail = report.graph.stateRegisterDetailDisplayName;
    stateRegister.codeLink = report.graph.stateRegisterCodeLink;
    addNode(stateRegister,
            stateRegisterRect,
            QColor(QStringLiteral("#eff6ff")),
            QColor(QStringLiteral("#2563eb")));

    const QRectF nextStateRect = insightNodeRectAt(140, -130);
    RtlInsightGraphElement nextState;
    nextState.kind = QStringLiteral("next-state-signal");
    nextState.primary = report.graph.nextStateSignalDisplayName;
    nextState.secondary = report.graph.nextStateSignalTypeDisplayName;
    nextState.detail = report.graph.nextStateSignalSourceRoleDisplayName;
    nextState.codeLink = report.graph.nextStateSignalCodeLink;
    addNode(nextState,
            nextStateRect,
            QColor(QStringLiteral("#f0fdf4")),
            QColor(QStringLiteral("#16a34a")));

    const QPointF registerEnd =
        rectAnchorToward(stateRegisterRect, nextStateRect.center());
    const QPointF nextStart =
        rectAnchorToward(nextStateRect, stateRegisterRect.center());
    RtlInsightGraphElement signalEdge;
    signalEdge.kind = QStringLiteral("state-signal-edge");
    signalEdge.primary = stateRegister.primary;
    signalEdge.secondary = nextState.primary;
    signalEdge.detail = QStringLiteral("paired next-state signal");
    signalEdge.codeLink = nextState.codeLink;
    addEdge(signalEdge,
            straightArrowPath(registerEnd, nextStart),
            nextStart,
            std::atan2(nextStart.y() - registerEnd.y(),
                       nextStart.x() - registerEnd.x()),
            QStringLiteral("next"),
            QColor(QStringLiteral("#64748b")));

    QHash<QString, QRectF> stateRects;
    const int stateCount = report.graph.stateRows.size();
    const qreal radius = qMax<qreal>(170.0, stateCount * 34.0);
    for (int i = 0; i < stateCount; ++i) {
        const FsmStateRow& row = report.graph.stateRows.at(i);
        const QPointF position = circularPosition(i, stateCount, radius);
        const QRectF rect = insightNodeRectAt(position.x(), position.y());
        stateRects.insert(row.stateDisplayName, rect);
        RtlInsightGraphElement state;
        state.kind = QStringLiteral("state");
        state.primary = row.stateDisplayName;
        state.secondary = row.typeDisplayName;
        state.detail = row.detailDisplayName;
        state.codeLink = row.codeLink;
        addNode(state,
                rect,
                QColor(QStringLiteral("#fff7ed")),
                QColor(QStringLiteral("#ea580c")));
    }

    for (const FsmTransitionRow& row : report.graph.transitionRows) {
        if (!stateRects.contains(row.fromStateDisplayName)
            || !stateRects.contains(row.toStateDisplayName)) {
            continue;
        }
        const QRectF fromRect = stateRects.value(row.fromStateDisplayName);
        const QRectF toRect = stateRects.value(row.toStateDisplayName);
        QPointF start;
        QPointF end;
        QPainterPath path;
        qreal angle = 0.0;
        if (row.fromStateDisplayName == row.toStateDisplayName) {
            start = QPointF(fromRect.right(), fromRect.center().y() - 10);
            end = QPointF(fromRect.right(), fromRect.center().y() + 10);
            path.moveTo(start);
            path.cubicTo(start + QPointF(70, -70),
                         end + QPointF(70, 70),
                         end);
            angle = kPi / 2.0;
        } else {
            start = rectAnchorToward(fromRect, toRect.center());
            end = rectAnchorToward(toRect, fromRect.center());
            path = curvedArrowPath(start, end, 22.0);
            angle = std::atan2(end.y() - start.y(), end.x() - start.x());
        }

        RtlInsightGraphElement transition;
        transition.kind = QStringLiteral("transition");
        transition.primary = row.fromStateDisplayName;
        transition.secondary = row.toStateDisplayName;
        transition.detail = row.conditionDisplayName;
        transition.codeLink = row.codeLink;
        addEdge(transition,
                path,
                end,
                angle,
                row.conditionDisplayName,
                QColor(QStringLiteral("#334155")));
    }

    const QRectF bounds =
        insightsGraphScene->itemsBoundingRect().adjusted(-60, -60, 60, 60);
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
    const auto navigate = [this](const RtlInsightCodeLink& link) {
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

    auto addNode = [&](const RtlInsightGraphElement& element,
                       const QRectF& rect,
                       const QColor& fill,
                       const QColor& stroke) {
        auto* item = new RtlInsightGraphNodeItem(element,
                                                rect,
                                                fill,
                                                stroke,
                                                font);
        item->navigateHandler = navigate;
        item->selectHandler = select;
        insightsGraphScene->addItem(item);
        return item;
    };
    auto addEdge = [&](const RtlInsightGraphElement& element,
                       const QPainterPath& path,
                       const QPointF& tip,
                       qreal angle,
                       const QString& label,
                       const QColor& color) {
        auto* item = new RtlInsightGraphEdgeItem(element,
                                                path,
                                                tip,
                                                angle,
                                                label,
                                                font,
                                                color);
        item->navigateHandler = navigate;
        item->selectHandler = select;
        insightsGraphScene->addItem(item);
        return item;
    };

    QFont titleFont = font;
    titleFont.setBold(true);
    titleFont.setPointSize(qMax(10, titleFont.pointSize() + 1));
    auto* titleItem = insightsGraphScene->addSimpleText(
        title.isEmpty() ? QStringLiteral("FSM Graph") : title,
        titleFont);
    titleItem->setBrush(QBrush(QColor(QStringLiteral("#0f172a"))));
    titleItem->setPos(-260, -250);

    constexpr qreal graphSpacing = 470.0;
    for (int graphIndex = 0; graphIndex < report.graphs.size(); ++graphIndex) {
        const FsmGraph& graph = report.graphs.at(graphIndex);
        const qreal baseX = graphIndex * graphSpacing;
        const qreal baseY = 0.0;

        QFont labelFont = font;
        labelFont.setBold(true);
        auto* label = insightsGraphScene->addSimpleText(
            graph.stateRegisterDisplayName,
            labelFont);
        label->setBrush(QBrush(QColor(QStringLiteral("#334155"))));
        label->setPos(baseX - 240, baseY - 214);

        const QRectF stateRegisterRect =
            insightNodeRectAt(baseX - 140, baseY - 130);
        RtlInsightGraphElement stateRegister;
        stateRegister.kind = QStringLiteral("state-register");
        stateRegister.primary = graph.stateRegisterDisplayName;
        stateRegister.secondary = graph.stateRegisterTypeDisplayName;
        stateRegister.detail = graph.stateRegisterDetailDisplayName;
        stateRegister.codeLink = graph.stateRegisterCodeLink;
        addNode(stateRegister,
                stateRegisterRect,
                QColor(QStringLiteral("#eff6ff")),
                QColor(QStringLiteral("#2563eb")));

        const QRectF nextStateRect = insightNodeRectAt(baseX + 140, baseY - 130);
        RtlInsightGraphElement nextState;
        nextState.kind = QStringLiteral("next-state-signal");
        nextState.primary = graph.nextStateSignalDisplayName;
        nextState.secondary = graph.nextStateSignalTypeDisplayName;
        nextState.detail = graph.nextStateSignalSourceRoleDisplayName;
        nextState.codeLink = graph.nextStateSignalCodeLink;
        addNode(nextState,
                nextStateRect,
                QColor(QStringLiteral("#f0fdf4")),
                QColor(QStringLiteral("#16a34a")));

        const QPointF registerEnd =
            rectAnchorToward(stateRegisterRect, nextStateRect.center());
        const QPointF nextStart =
            rectAnchorToward(nextStateRect, stateRegisterRect.center());
        RtlInsightGraphElement signalEdge;
        signalEdge.kind = QStringLiteral("state-signal-edge");
        signalEdge.primary = stateRegister.primary;
        signalEdge.secondary = nextState.primary;
        signalEdge.detail = QStringLiteral("paired next-state signal");
        signalEdge.codeLink = nextState.codeLink;
        addEdge(signalEdge,
                straightArrowPath(registerEnd, nextStart),
                nextStart,
                std::atan2(nextStart.y() - registerEnd.y(),
                           nextStart.x() - registerEnd.x()),
                QStringLiteral("next"),
                QColor(QStringLiteral("#64748b")));

        QHash<QString, QRectF> stateRects;
        const int stateCount = graph.stateRows.size();
        const qreal radius = qMax<qreal>(150.0, stateCount * 28.0);
        for (int i = 0; i < stateCount; ++i) {
            const FsmStateRow& row = graph.stateRows.at(i);
            const QPointF position = circularPosition(i, stateCount, radius);
            const QRectF rect = insightNodeRectAt(baseX + position.x(),
                                                  baseY + position.y());
            stateRects.insert(row.stateDisplayName, rect);
            RtlInsightGraphElement state;
            state.kind = QStringLiteral("state");
            state.primary = row.stateDisplayName;
            state.secondary = row.typeDisplayName;
            state.detail = row.detailDisplayName;
            state.codeLink = row.codeLink;
            addNode(state,
                    rect,
                    QColor(QStringLiteral("#fff7ed")),
                    QColor(QStringLiteral("#ea580c")));
        }

        for (const FsmTransitionRow& row : graph.transitionRows) {
            if (!stateRects.contains(row.fromStateDisplayName)
                || !stateRects.contains(row.toStateDisplayName)) {
                continue;
            }
            const QRectF fromRect = stateRects.value(row.fromStateDisplayName);
            const QRectF toRect = stateRects.value(row.toStateDisplayName);
            QPointF start;
            QPointF end;
            QPainterPath path;
            qreal angle = 0.0;
            if (row.fromStateDisplayName == row.toStateDisplayName) {
                start = QPointF(fromRect.right(), fromRect.center().y() - 10);
                end = QPointF(fromRect.right(), fromRect.center().y() + 10);
                path.moveTo(start);
                path.cubicTo(start + QPointF(70, -70),
                             end + QPointF(70, 70),
                             end);
                angle = kPi / 2.0;
            } else {
                start = rectAnchorToward(fromRect, toRect.center());
                end = rectAnchorToward(toRect, fromRect.center());
                path = curvedArrowPath(start, end, 22.0);
                angle = std::atan2(end.y() - start.y(),
                                   end.x() - start.x());
            }

            RtlInsightGraphElement transition;
            transition.kind = QStringLiteral("transition");
            transition.primary = row.fromStateDisplayName;
            transition.secondary = row.toStateDisplayName;
            transition.detail = row.conditionDisplayName;
            transition.codeLink = row.codeLink;
            addEdge(transition,
                    path,
                    end,
                    angle,
                    row.conditionDisplayName,
                    QColor(QStringLiteral("#334155")));
        }
    }

    const QRectF bounds =
        insightsGraphScene->itemsBoundingRect().adjusted(-80, -80, 80, 80);
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
    const auto navigate = [this](const RtlInsightCodeLink& link) {
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

    auto addNode = [&](const ModuleBlockDiagramNode& node,
                       const QRectF& rect,
                       bool root) {
        RtlInsightGraphElement element;
        element.kind = QStringLiteral("module");
        element.primary = node.moduleDisplayName;
        element.secondary = node.moduleTypeDisplayName;
        element.detail = node.sourceRoleDisplayName;
        element.codeLink = node.definitionCodeLink;
        auto* item = new RtlInsightGraphNodeItem(
            element,
            rect,
            root ? QColor(QStringLiteral("#eff6ff"))
                 : QColor(QStringLiteral("#f0fdf4")),
            root ? QColor(QStringLiteral("#2563eb"))
                 : QColor(QStringLiteral("#16a34a")),
            font);
        item->navigateHandler = navigate;
        item->selectHandler = select;
        insightsGraphScene->addItem(item);
    };
    auto addEdge = [&](const ModuleBlockDiagramEdge& edge,
                       const QRectF& fromRect,
                       const QRectF& toRect) {
        const QPointF start = rectAnchorToward(fromRect, toRect.center());
        const QPointF end = rectAnchorToward(toRect, fromRect.center());
        RtlInsightGraphElement element;
        element.kind = QStringLiteral("module-edge");
        element.primary = edge.parentModuleDisplayName;
        element.secondary = edge.childModuleDisplayName;
        element.detail = edge.relationshipDisplayName;
        element.codeLink = edge.childDefinitionCodeLink;
        auto* item = new RtlInsightGraphEdgeItem(
            element,
            straightArrowPath(start, end),
            end,
            std::atan2(end.y() - start.y(), end.x() - start.x()),
            edge.relationshipDisplayName.isEmpty()
                ? QStringLiteral("instantiates")
                : edge.relationshipDisplayName,
            font,
            QColor(QStringLiteral("#334155")));
        item->navigateHandler = navigate;
        item->selectHandler = select;
        insightsGraphScene->addItem(item);
    };

    QHash<int, QList<ModuleBlockDiagramNode>> nodesByDepth;
    int maxDepth = 0;
    for (const ModuleBlockDiagramNode& node : report.nodes) {
        nodesByDepth[node.depth].append(node);
        maxDepth = qMax(maxDepth, node.depth);
    }
    Q_UNUSED(maxDepth)

    QHash<int, QRectF> rectByNodeId;
    constexpr qreal columnSpacing = 300.0;
    constexpr qreal rowSpacing = 110.0;
    for (auto it = nodesByDepth.begin(); it != nodesByDepth.end(); ++it) {
        QList<ModuleBlockDiagramNode> nodes = it.value();
        std::sort(nodes.begin(), nodes.end(),
                  [](const ModuleBlockDiagramNode& lhs,
                     const ModuleBlockDiagramNode& rhs) {
                      return lhs.nodeId < rhs.nodeId;
                  });
        const int depth = it.key();
        const qreal x = depth * columnSpacing;
        for (int i = 0; i < nodes.size(); ++i) {
            const qreal y = (i - (nodes.size() - 1) / 2.0) * rowSpacing;
            const QRectF rect = insightNodeRectAt(x, y);
            rectByNodeId.insert(nodes.at(i).nodeId, rect);
        }
    }

    for (const ModuleBlockDiagramEdge& edge : report.edges) {
        if (!rectByNodeId.contains(edge.fromNodeId)
            || !rectByNodeId.contains(edge.toNodeId)) {
            continue;
        }
        addEdge(edge,
                rectByNodeId.value(edge.fromNodeId),
                rectByNodeId.value(edge.toNodeId));
    }

    for (const ModuleBlockDiagramNode& node : report.nodes) {
        if (!rectByNodeId.contains(node.nodeId))
            continue;
        addNode(node,
                rectByNodeId.value(node.nodeId),
                node.nodeId == report.root.nodeId);
    }

    if (report.edgeCount == 0) {
        auto* label = insightsGraphScene->addSimpleText(
            QStringLiteral("No child modules"),
            font);
        label->setBrush(QBrush(QColor(QStringLiteral("#475569"))));
        label->setPos(-70, 68);
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
