#include "signalkernelgraphpanelcoordinator.h"

#include "codepreviewservice.h"
#include "documentmodel.h"
#include "editorhoverpopup.h"

#include <QBrush>
#include <QColor>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QGraphicsLineItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPen>
#include <QPoint>
#include <QPolygonF>
#include <QVBoxLayout>
#include <QWidget>

#include <cmath>
#include <utility>

namespace {
constexpr qreal kNodeWidth = 214.0;
constexpr qreal kNodeHeight = 64.0;
constexpr qreal kColumnOffset = 430.0;
constexpr qreal kVerticalSpacing = 92.0;
constexpr double kPi = 3.14159265358979323846;

QString elidedText(const QString& text, const QFont& font, int width)
{
    QFontMetrics metrics(font);
    return metrics.elidedText(text, Qt::ElideRight, width);
}

QString roleTitle(SignalKernelGraphNodeRole role)
{
    switch (role) {
    case SignalKernelGraphNodeRole::Kernel:
        return QStringLiteral("Kernel");
    case SignalKernelGraphNodeRole::Input:
        return QStringLiteral("Input");
    case SignalKernelGraphNodeRole::Output:
        return QStringLiteral("Output");
    }
    return QStringLiteral("Node");
}

QColor fillColorForRole(SignalKernelGraphNodeRole role)
{
    switch (role) {
    case SignalKernelGraphNodeRole::Kernel:
        return QColor(QStringLiteral("#eaf2ff"));
    case SignalKernelGraphNodeRole::Input:
        return QColor(QStringLiteral("#e8f7ef"));
    case SignalKernelGraphNodeRole::Output:
        return QColor(QStringLiteral("#fff4dd"));
    }
    return QColor(QStringLiteral("#f8fafc"));
}

QColor strokeColorForRole(SignalKernelGraphNodeRole role)
{
    switch (role) {
    case SignalKernelGraphNodeRole::Kernel:
        return QColor(QStringLiteral("#2563eb"));
    case SignalKernelGraphNodeRole::Input:
        return QColor(QStringLiteral("#15803d"));
    case SignalKernelGraphNodeRole::Output:
        return QColor(QStringLiteral("#b45309"));
    }
    return QColor(QStringLiteral("#64748b"));
}

QPointF leftAnchor(const QRectF& rect)
{
    return QPointF(rect.left(), rect.center().y());
}

QPointF rightAnchor(const QRectF& rect)
{
    return QPointF(rect.right(), rect.center().y());
}

QRectF nodeRectAt(qreal centerX, qreal centerY)
{
    return QRectF(centerX - kNodeWidth / 2.0,
                  centerY - kNodeHeight / 2.0,
                  kNodeWidth,
                  kNodeHeight);
}

qreal rowY(int index, int count)
{
    if (count <= 1)
        return 0.0;
    return (index - (count - 1) / 2.0) * kVerticalSpacing;
}

class SignalKernelGraphNodeItem : public QGraphicsRectItem
{
public:
    using HoverHandler =
        std::function<void(const SignalKernelGraphNode&, const QPoint&)>;
    using LeaveHandler = std::function<void()>;
    using NodeHandler = std::function<void(const SignalKernelGraphNode&)>;

    SignalKernelGraphNodeItem(const SignalKernelGraphNode& graphNode,
                              const QRectF& rect,
                              const QFont& font)
        : QGraphicsRectItem(rect),
          node(graphNode)
    {
        setAcceptHoverEvents(true);
        setFlag(QGraphicsItem::ItemIsSelectable, true);
        setTransformOriginPoint(rect.center());
        setBrush(fillColorForRole(node.role));
        setPen(QPen(strokeColorForRole(node.role),
                    node.role == SignalKernelGraphNodeRole::Kernel ? 2.0 : 1.4));
        setToolTip(node.displayName);

        QFont titleFont = font;
        titleFont.setBold(true);
        titleFont.setPointSize(qMax(8, titleFont.pointSize() + 1));
        QFont detailFont = font;
        detailFont.setPointSize(qMax(8, detailFont.pointSize() - 1));

        auto* title = new QGraphicsSimpleTextItem(
            elidedText(node.displayName, titleFont,
                       static_cast<int>(rect.width() - 18)),
            this);
        title->setFont(titleFont);
        title->setBrush(QBrush(QColor(QStringLiteral("#0f172a"))));
        title->setPos(rect.left() + 10, rect.top() + 8);

        QString detail = node.detailDisplayName;
        if (!node.moduleDisplayName.isEmpty()) {
            detail = detail.isEmpty()
                ? node.moduleDisplayName
                : QStringLiteral("%1 | %2").arg(node.moduleDisplayName, detail);
        }
        auto* detailItem = new QGraphicsSimpleTextItem(
            elidedText(detail, detailFont,
                       static_cast<int>(rect.width() - 18)),
            this);
        detailItem->setFont(detailFont);
        detailItem->setBrush(QBrush(QColor(QStringLiteral("#475569"))));
        detailItem->setPos(rect.left() + 10, rect.top() + 34);
    }

    HoverHandler hoverHandler;
    LeaveHandler leaveHandler;
    NodeHandler navigateHandler;
    NodeHandler rebaseHandler;

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override
    {
        setScale(1.08);
        setZValue(40);
        if (hoverHandler)
            hoverHandler(node, event->screenPos());
        QGraphicsRectItem::hoverEnterEvent(event);
    }

    void hoverMoveEvent(QGraphicsSceneHoverEvent* event) override
    {
        if (hoverHandler)
            hoverHandler(node, event->screenPos());
        QGraphicsRectItem::hoverMoveEvent(event);
    }

    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override
    {
        setScale(1.0);
        setZValue(10);
        if (leaveHandler)
            leaveHandler();
        QGraphicsRectItem::hoverLeaveEvent(event);
    }

    void mousePressEvent(QGraphicsSceneMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton
            && event->modifiers().testFlag(Qt::ControlModifier)
            && rebaseHandler) {
            rebaseHandler(node);
            event->accept();
            return;
        }
        QGraphicsRectItem::mousePressEvent(event);
    }

    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && navigateHandler) {
            navigateHandler(node);
            event->accept();
            return;
        }
        QGraphicsRectItem::mouseDoubleClickEvent(event);
    }

private:
    SignalKernelGraphNode node;
};

void addArrow(QGraphicsScene* scene,
              const QLineF& line,
              const QPen& pen)
{
    if (!scene || line.length() < 1.0)
        return;

    auto* lineItem = scene->addLine(line, pen);
    lineItem->setZValue(-8);

    const double angle = std::atan2(line.dy(), line.dx());
    constexpr qreal arrowSize = 10.0;
    const QPointF p1 = line.p2()
        - QPointF(std::cos(angle - kPi / 6.0) * arrowSize,
                  std::sin(angle - kPi / 6.0) * arrowSize);
    const QPointF p2 = line.p2()
        - QPointF(std::cos(angle + kPi / 6.0) * arrowSize,
                  std::sin(angle + kPi / 6.0) * arrowSize);
    QPolygonF arrowHead;
    arrowHead << line.p2() << p1 << p2;
    auto* arrowItem = scene->addPolygon(arrowHead, pen, QBrush(pen.color()));
    arrowItem->setZValue(-7);
}

QRectF unitedRectForGroup(
    const QHash<int, QRectF>& nodeRects,
    const QList<int>& nodeIds)
{
    QRectF groupRect;
    bool first = true;
    for (int nodeId : nodeIds) {
        if (!nodeRects.contains(nodeId))
            continue;
        if (first) {
            groupRect = nodeRects.value(nodeId);
            first = false;
        } else {
            groupRect = groupRect.united(nodeRects.value(nodeId));
        }
    }
    return groupRect;
}

void addModuleWrapper(QGraphicsScene* scene,
                      const SignalKernelGraphModuleGroup& group,
                      const QHash<int, QRectF>& nodeRects,
                      const QFont& font)
{
    if (!scene || !group.crossModule)
        return;

    const QRectF groupRect =
        unitedRectForGroup(nodeRects, group.nodeIds).adjusted(-18, -24, 18, 18);
    if (!groupRect.isValid())
        return;

    QPen pen(QColor(QStringLiteral("#64748b")), 1.0, Qt::DashLine);
    auto* wrapper = scene->addRect(groupRect, pen, Qt::NoBrush);
    wrapper->setZValue(-20);

    QFont labelFont = font;
    labelFont.setBold(true);
    labelFont.setPointSize(qMax(8, labelFont.pointSize() - 1));
    auto* label = scene->addSimpleText(group.moduleName, labelFont);
    label->setBrush(QBrush(QColor(QStringLiteral("#334155"))));
    label->setPos(groupRect.left() + 8, groupRect.top() + 4);
    label->setZValue(-19);
}

void addSectionLabel(QGraphicsScene* scene,
                     const QString& text,
                     qreal x,
                     qreal y,
                     const QFont& font)
{
    if (!scene)
        return;
    QFont labelFont = font;
    labelFont.setBold(true);
    auto* label = scene->addSimpleText(text, labelFont);
    label->setBrush(QBrush(QColor(QStringLiteral("#334155"))));
    label->setPos(x - label->boundingRect().width() / 2.0, y);
    label->setZValue(20);
}
}

SignalKernelGraphPanelCoordinator::SignalKernelGraphPanelCoordinator(
    QWidget* parent)
{
    auto* panel = new QWidget(parent);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    titleLabel = new QLabel(QStringLiteral("Signal Kernel Graph"), panel);
    titleLabel->setObjectName(QStringLiteral("signalKernelGraphTitle"));
    titleLabel->setStyleSheet(QStringLiteral(
        "QLabel#signalKernelGraphTitle {"
        "  color: #0f172a;"
        "  padding: 3px 4px;"
        "  font-weight: 600;"
        "}"));
    layout->addWidget(titleLabel);

    graphScene = new QGraphicsScene(panel);
    graphView = new QGraphicsView(graphScene, panel);
    graphView->setObjectName(QStringLiteral("signalKernelGraphView"));
    graphView->setRenderHint(QPainter::Antialiasing, true);
    graphView->setDragMode(QGraphicsView::ScrollHandDrag);
    graphView->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    graphView->setResizeAnchor(QGraphicsView::AnchorViewCenter);
    graphView->setBackgroundBrush(QBrush(QColor(QStringLiteral("#f8fafc"))));
    layout->addWidget(graphView, 1);

    graphDock = new QDockWidget(QStringLiteral("Signal Kernel Graph"), parent);
    graphDock->setObjectName(QStringLiteral("signalKernelGraphDock"));
    graphDock->setWidget(panel);
    graphDock->setFeatures(QDockWidget::DockWidgetMovable |
                           QDockWidget::DockWidgetFloatable |
                           QDockWidget::DockWidgetClosable);
    graphDock->hide();

    hoverPopup = new EditorHoverPopup(graphDock);

    renderUnavailable(QStringLiteral("No signal selected."));
}

SignalKernelGraphPanelCoordinator::~SignalKernelGraphPanelCoordinator() = default;

void SignalKernelGraphPanelCoordinator::setNavigationHandler(
    std::function<void(const QString&, int, int)> handler)
{
    navigationHandler = std::move(handler);
    if (hoverPopup) {
        hoverPopup->setNavigationHandler(
            [this](const QString& fileName, int line, int column) {
                if (navigationHandler)
                    navigationHandler(fileName, line, column);
            });
    }
}

void SignalKernelGraphPanelCoordinator::setStatusMessageHandler(
    std::function<void(const QString&, int)> handler)
{
    statusMessageHandler = std::move(handler);
}

void SignalKernelGraphPanelCoordinator::setDocumentModel(
    DocumentModel* documentModel)
{
    CodePreviewService::getInstance()->setDocumentModel(documentModel);
}

void SignalKernelGraphPanelCoordinator::showSignalKernelGraphForSymbol(
    const QString& symbolName,
    const QString& fileName,
    const QString& moduleName)
{
    currentQuery = {};
    currentQuery.signalName = symbolName;
    currentQuery.fileName = fileName;
    currentQuery.moduleName = moduleName;
    refresh();
    showDock();
}

void SignalKernelGraphPanelCoordinator::showSignalKernelGraphForStableKey(
    const SymbolStableKey& stableKey)
{
    currentQuery = {};
    currentQuery.signalStableKey = stableKey;
    refresh();
    showDock();
}

void SignalKernelGraphPanelCoordinator::refresh()
{
    if (!currentQuery.signalStableKey.isValid()
        && currentQuery.signalName.isEmpty()) {
        renderUnavailable(QStringLiteral("No signal selected."));
        return;
    }

    const SignalKernelGraphReport report =
        SignalKernelGraphService::getInstance()->buildSignalKernelGraph(
            currentQuery);
    renderReport(report);
}

void SignalKernelGraphPanelCoordinator::renderReport(
    const SignalKernelGraphReport& report)
{
    if (!graphScene)
        return;

    graphScene->clear();
    if (!report.found) {
        renderUnavailable(report.notFoundReasonDisplayName);
        return;
    }

    if (titleLabel) {
        titleLabel->setText(QStringLiteral("%1   Inputs %2   Outputs %3")
                                .arg(report.kernel.displayName)
                                .arg(report.inputs.size())
                                .arg(report.outputs.size()));
    }
    if (graphDock) {
        graphDock->setWindowTitle(QStringLiteral("Signal Kernel Graph: %1")
                                      .arg(report.kernel.displayName));
    }

    const QFont baseFont = graphView ? graphView->font() : QFont();
    QHash<int, QRectF> nodeRects;
    QHash<int, SignalKernelGraphNodeItem*> nodeItems;

    addSectionLabel(graphScene,
                    QStringLiteral("Inputs"),
                    -kColumnOffset,
                    -kVerticalSpacing * qMax(1, report.inputs.size()) / 2.0 - 58,
                    baseFont);
    addSectionLabel(graphScene,
                    QStringLiteral("Kernel"),
                    0,
                    -86,
                    baseFont);
    addSectionLabel(graphScene,
                    QStringLiteral("Outputs"),
                    kColumnOffset,
                    -kVerticalSpacing * qMax(1, report.outputs.size()) / 2.0 - 58,
                    baseFont);

    auto addNode = [&](const SignalKernelGraphNode& node,
                       const QRectF& rect) {
        nodeRects.insert(node.id, rect);
        auto* item = new SignalKernelGraphNodeItem(node, rect, baseFont);
        item->setZValue(10);
        item->hoverHandler =
            [this](const SignalKernelGraphNode& hoveredNode,
                   const QPoint& globalPosition) {
                showNodePreview(hoveredNode, globalPosition);
            };
        item->leaveHandler = [this]() {
            if (hoverPopup)
                hoverPopup->closePopup();
        };
        item->navigateHandler =
            [this](const SignalKernelGraphNode& clickedNode) {
                navigateNode(clickedNode);
            };
        item->rebaseHandler =
            [this](const SignalKernelGraphNode& clickedNode) {
                rebaseToNode(clickedNode);
            };
        graphScene->addItem(item);
        nodeItems.insert(node.id, item);
    };

    for (int i = 0; i < report.inputs.size(); ++i) {
        addNode(report.inputs.at(i),
                nodeRectAt(-kColumnOffset, rowY(i, report.inputs.size())));
    }
    addNode(report.kernel, nodeRectAt(0, 0));
    for (int i = 0; i < report.outputs.size(); ++i) {
        addNode(report.outputs.at(i),
                nodeRectAt(kColumnOffset, rowY(i, report.outputs.size())));
    }

    for (const SignalKernelGraphModuleGroup& group : report.inputModuleGroups)
        addModuleWrapper(graphScene, group, nodeRects, baseFont);
    for (const SignalKernelGraphModuleGroup& group : report.outputModuleGroups)
        addModuleWrapper(graphScene, group, nodeRects, baseFont);

    QPen edgePen(QColor(QStringLiteral("#94a3b8")), 1.5);
    for (const SignalKernelGraphEdge& edge : report.edges) {
        if (!nodeRects.contains(edge.fromNodeId)
            || !nodeRects.contains(edge.toNodeId)) {
            continue;
        }
        const QRectF fromRect = nodeRects.value(edge.fromNodeId);
        const QRectF toRect = nodeRects.value(edge.toNodeId);
        const QPointF fromPoint =
            fromRect.center().x() <= toRect.center().x()
                ? rightAnchor(fromRect)
                : leftAnchor(fromRect);
        const QPointF toPoint =
            fromRect.center().x() <= toRect.center().x()
                ? leftAnchor(toRect)
                : rightAnchor(toRect);
        addArrow(graphScene, QLineF(fromPoint, toPoint), edgePen);
    }

    const QRectF bounds =
        graphScene->itemsBoundingRect().adjusted(-60, -60, 60, 60);
    graphScene->setSceneRect(bounds);
    if (graphView)
        graphView->fitInView(bounds, Qt::KeepAspectRatio);

    showStatusMessage(QStringLiteral("Rendered signal kernel graph"), 1500);
}

void SignalKernelGraphPanelCoordinator::renderUnavailable(
    const QString& message)
{
    if (!graphScene)
        return;

    graphScene->clear();
    const QString text = message.isEmpty()
        ? QStringLiteral("Signal kernel graph unavailable.")
        : message;
    auto* item = graphScene->addText(text);
    item->setDefaultTextColor(QColor(QStringLiteral("#64748b")));
    item->setPos(0, 0);
    graphScene->setSceneRect(item->boundingRect().adjusted(-40, -40, 40, 40));
    if (titleLabel)
        titleLabel->setText(QStringLiteral("Signal Kernel Graph"));
    if (graphDock)
        graphDock->setWindowTitle(QStringLiteral("Signal Kernel Graph"));
}

void SignalKernelGraphPanelCoordinator::showNodePreview(
    const SignalKernelGraphNode& node,
    const QPoint& globalPosition)
{
    if (!hoverPopup)
        return;

    CodePreviewQuery query;
    query.codeLink = node.previewCodeLink;
    query.sourceRange = node.evidenceRange;
    query.title = QStringLiteral("%1: %2")
                      .arg(roleTitle(node.role), node.displayName);
    query.detail = node.detailDisplayName;
    const CodePreviewReport report =
        CodePreviewService::getInstance()->previewForCodeLink(query);
    hoverPopup->showCodePreview(report,
                                globalPosition,
                                graphView ? graphView->font() : QFont());
}

void SignalKernelGraphPanelCoordinator::navigateNode(
    const SignalKernelGraphNode& node) const
{
    if (!navigationHandler)
        return;
    if (node.navigateCodeLink.fileName.isEmpty()
        || node.navigateCodeLink.line <= 0) {
        return;
    }
    navigationHandler(node.navigateCodeLink.fileName,
                      node.navigateCodeLink.line,
                      node.navigateCodeLink.column);
}

void SignalKernelGraphPanelCoordinator::rebaseToNode(
    const SignalKernelGraphNode& node)
{
    if (!node.stableKey.isValid()) {
        showStatusMessage(QStringLiteral("Signal kernel unavailable for node"),
                          3000);
        return;
    }
    currentQuery = {};
    currentQuery.signalStableKey = node.stableKey;
    currentQuery.signalName = node.displayName;
    currentQuery.fileName = node.symbolRecord.location.fileName;
    currentQuery.moduleName = node.symbolRecord.owner.name;
    refresh();
    showStatusMessage(QStringLiteral("Signal kernel: %1").arg(node.displayName),
                      1500);
}

void SignalKernelGraphPanelCoordinator::showDock()
{
    if (!graphDock)
        return;
    graphDock->show();
    graphDock->raise();
    graphDock->activateWindow();
}

void SignalKernelGraphPanelCoordinator::showStatusMessage(
    const QString& message,
    int timeoutMs) const
{
    if (statusMessageHandler)
        statusMessageHandler(message, timeoutMs);
}
