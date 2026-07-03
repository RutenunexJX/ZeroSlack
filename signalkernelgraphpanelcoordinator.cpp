#include "signalkernelgraphpanelcoordinator.h"

#include "codepreviewservice.h"
#include "documentmodel.h"
#include "editorhoverpopup.h"
#include "insightvisualstyle.h"

#include <QBrush>
#include <QCheckBox>
#include <QColor>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
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
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPoint>
#include <QPolygonF>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidget>

#include <cmath>
#include <utility>

namespace {
constexpr qreal kNodeWidth = 214.0;
constexpr qreal kNodeHeight = 64.0;
constexpr qreal kColumnOffset = 430.0;
constexpr qreal kVerticalSpacing = 92.0;
constexpr qreal kFanoutGroupCardWidth = 244.0;
constexpr qreal kFanoutGroupCardHeight = 58.0;
constexpr double kPi = 3.14159265358979323846;

class SignalKernelGraphView : public QGraphicsView
{
public:
    using QGraphicsView::QGraphicsView;
    std::function<bool(const QPoint&,
                       Qt::MouseButton,
                       Qt::KeyboardModifiers)> pressHandler;
    std::function<bool(const QPoint&)> doubleClickHandler;

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event
            && pressHandler
            && pressHandler(event->pos(), event->button(), event->modifiers())) {
            event->accept();
            return;
        }
        QGraphicsView::mousePressEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (event
            && event->button() == Qt::LeftButton
            && doubleClickHandler
            && doubleClickHandler(event->pos())) {
            event->accept();
            return;
        }
        QGraphicsView::mouseDoubleClickEvent(event);
    }

    void wheelEvent(QWheelEvent* event) override
    {
        if (!event)
            return;
        const qreal currentScale = transform().m11();
        const qreal factor = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
        const qreal nextScale = currentScale * factor;
        if (nextScale < 0.18 || nextScale > 4.5) {
            event->accept();
            return;
        }
        scale(factor, factor);
        event->accept();
    }
};

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

QList<SignalKernelGraphInputLane> inputLaneOrder()
{
    return {SignalKernelGraphInputLane::Data,
            SignalKernelGraphInputLane::Control,
            SignalKernelGraphInputLane::Timing};
}

QColor inputLaneColor(SignalKernelGraphInputLane lane)
{
    switch (lane) {
    case SignalKernelGraphInputLane::Data:
        return InsightVisualStyle::roleFillColor(InsightVisualRole::Data);
    case SignalKernelGraphInputLane::Control:
        return InsightVisualStyle::roleFillColor(InsightVisualRole::Control);
    case SignalKernelGraphInputLane::Timing:
        return InsightVisualStyle::roleFillColor(InsightVisualRole::Timing);
    }
    return InsightVisualStyle::roleFillColor(InsightVisualRole::Unknown);
}

InsightVisualRole visualRoleForGraphRole(SignalKernelGraphNodeRole role)
{
    switch (role) {
    case SignalKernelGraphNodeRole::Kernel:
        return InsightVisualRole::Kernel;
    case SignalKernelGraphNodeRole::Input:
        return InsightVisualRole::Read;
    case SignalKernelGraphNodeRole::Output:
        return InsightVisualRole::Write;
    }
    return InsightVisualRole::Unknown;
}

QColor fillColorForRole(SignalKernelGraphNodeRole role)
{
    return InsightVisualStyle::roleFillColor(visualRoleForGraphRole(role));
}

QColor strokeColorForRole(SignalKernelGraphNodeRole role)
{
    return InsightVisualStyle::roleColor(visualRoleForGraphRole(role));
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

QString moduleDisplayNameForNode(const SignalKernelGraphNode& node)
{
    if (!node.moduleDisplayName.isEmpty())
        return node.moduleDisplayName;
    if (!node.symbolRecord.owner.name.isEmpty())
        return node.symbolRecord.owner.name;
    return QFileInfo(node.navigateCodeLink.fileName).fileName();
}

QString hoverDetailForNode(const SignalKernelGraphNode& node)
{
    QStringList parts;
    if (node.role == SignalKernelGraphNodeRole::Input) {
        parts.append(QStringLiteral("%1 input")
                         .arg(SignalKernelGraphService::inputLaneDisplayName(
                             node.inputLane)));
    } else {
        parts.append(roleTitle(node.role));
    }
    if (!node.typeDisplayName.isEmpty())
        parts.append(node.typeDisplayName);
    const QString moduleName = moduleDisplayNameForNode(node);
    if (!moduleName.isEmpty())
        parts.append(QStringLiteral("module %1").arg(moduleName));
    if (!node.detailDisplayName.isEmpty())
        parts.append(node.detailDisplayName);
    return parts.join(QStringLiteral(" | "));
}

bool textMatchesSearch(const QString& text, const QString& searchText)
{
    return !searchText.isEmpty()
        && text.contains(searchText, Qt::CaseInsensitive);
}

bool nodeMatchesSearchText(const SignalKernelGraphNode& node,
                           const QString& searchText)
{
    if (searchText.isEmpty())
        return false;
    return textMatchesSearch(node.displayName, searchText)
        || textMatchesSearch(node.detailDisplayName, searchText)
        || textMatchesSearch(moduleDisplayNameForNode(node), searchText)
        || textMatchesSearch(node.typeDisplayName, searchText)
        || textMatchesSearch(node.sourceRoleDisplayName, searchText)
        || textMatchesSearch(node.navigateCodeLink.fileName, searchText)
        || textMatchesSearch(QFileInfo(node.navigateCodeLink.fileName)
                                 .fileName(),
                             searchText);
}

int firstMatchingFanoutGroupNodeId(
    const SignalKernelGraphFanoutGroup& group,
    const QHash<int, SignalKernelGraphNode>& nodesById,
    const QString& searchText)
{
    if (searchText.isEmpty())
        return -1;
    if (textMatchesSearch(group.displayName, searchText)
        || textMatchesSearch(group.moduleName, searchText)) {
        return group.nodeIds.isEmpty() ? -1 : group.nodeIds.first();
    }
    for (int nodeId : group.nodeIds) {
        if (nodesById.contains(nodeId)
            && nodeMatchesSearchText(nodesById.value(nodeId), searchText)) {
            return nodeId;
        }
    }
    return -1;
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
    using PreviewHandler =
        std::function<void(const SignalKernelGraphNode&, const QRectF&)>;
    using NodeHandler = std::function<void(const SignalKernelGraphNode&)>;

    SignalKernelGraphNodeItem(const SignalKernelGraphNode& graphNode,
                              const QRectF& rect,
                              const QFont& font,
                              bool searchMatch)
        : QGraphicsRectItem(rect),
          node(graphNode)
    {
        setAcceptHoverEvents(true);
        setFlag(QGraphicsItem::ItemIsSelectable, true);
        setTransformOriginPoint(rect.center());
        setBrush(fillColorForRole(node.role));
        normalPen =
            searchMatch
                ? InsightVisualStyle::selectedPen(2.8)
                : QPen(strokeColorForRole(node.role),
                       node.role == SignalKernelGraphNodeRole::Kernel ? 2.0
                                                                       : 1.4);
        hoverPen = searchMatch ? normalPen : InsightVisualStyle::hoverPen();
        setPen(normalPen);

        QFont titleFont = InsightVisualStyle::titleFont(font);
        titleFont.setPointSize(qMax(8, titleFont.pointSize() + 1));
        QFont detailFont = InsightVisualStyle::compactFont(font);

        auto* title = new QGraphicsSimpleTextItem(
            elidedText(node.displayName, titleFont,
                       static_cast<int>(rect.width() - 18)),
            this);
        title->setFont(titleFont);
        title->setBrush(QBrush(InsightVisualStyle::theme().textPrimary));
        title->setPos(rect.left() + 10, rect.top() + 8);

        QString detail = node.typeDisplayName;
        if (detail.isEmpty())
            detail = node.sourceRoleDisplayName;
        auto* detailItem = new QGraphicsSimpleTextItem(
            elidedText(detail, detailFont,
                       static_cast<int>(rect.width() - 18)),
            this);
        detailItem->setFont(detailFont);
        detailItem->setBrush(QBrush(InsightVisualStyle::theme().textSecondary));
        detailItem->setPos(rect.left() + 10, rect.top() + 34);
    }

    const SignalKernelGraphNode& graphNode() const { return node; }

    PreviewHandler previewHandler;
    NodeHandler navigateHandler;
    NodeHandler rebaseHandler;

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override
    {
        setZValue(40);
        setPen(hoverPen);
        QGraphicsRectItem::hoverEnterEvent(event);
    }

    void hoverMoveEvent(QGraphicsSceneHoverEvent* event) override
    {
        QGraphicsRectItem::hoverMoveEvent(event);
    }

    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override
    {
        setZValue(10);
        setPen(isSelected() ? InsightVisualStyle::selectedPen() : normalPen);
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
        if (event->button() == Qt::RightButton && previewHandler) {
            setSelected(true);
            setPen(InsightVisualStyle::selectedPen());
            previewHandler(node, sceneBoundingRect());
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
    QPen normalPen;
    QPen hoverPen;
};

class SignalKernelGraphFanoutGroupItem : public QGraphicsRectItem
{
public:
    using ToggleHandler = std::function<void(const QString&)>;

    SignalKernelGraphFanoutGroupItem(
        const SignalKernelGraphFanoutGroup& fanoutGroup,
        const QString& uiKey,
        bool collapsedState,
        const QRectF& rect,
        const QFont& font,
        bool searchMatch)
        : QGraphicsRectItem(rect),
          group(fanoutGroup),
          key(uiKey),
          collapsed(collapsedState)
    {
        setAcceptHoverEvents(true);
        setFlag(QGraphicsItem::ItemIsSelectable, true);
        setBrush(collapsed ? InsightVisualStyle::panelBrush()
                           : QBrush(InsightVisualStyle::theme().panelSubtle));
        normalPen =
            searchMatch
                ? InsightVisualStyle::selectedPen(2.6)
                : QPen(InsightVisualStyle::theme().borderStrong,
                       collapsed ? 1.5 : 1.1,
                       collapsed ? Qt::SolidLine : Qt::DashLine);
        hoverPen = searchMatch ? normalPen : InsightVisualStyle::hoverPen();
        setPen(normalPen);

        QFont titleFont = InsightVisualStyle::titleFont(font);
        QFont detailFont = InsightVisualStyle::compactFont(font);

        const QString marker = collapsed
            ? QStringLiteral("[+] ")
            : QStringLiteral("[-] ");
        auto* title = new QGraphicsSimpleTextItem(
            elidedText(marker + group.displayName,
                       titleFont,
                       static_cast<int>(rect.width() - 18)),
            this);
        title->setFont(titleFont);
        title->setBrush(QBrush(InsightVisualStyle::theme().textPrimary));
        title->setPos(rect.left() + 10, rect.top() + 7);

        const QString detail =
            QStringLiteral("%1 signal(s), %2 total")
                .arg(group.nodeCount)
                .arg(group.totalRoleNodeCount);
        auto* detailItem = new QGraphicsSimpleTextItem(
            elidedText(detail,
                       detailFont,
                       static_cast<int>(rect.width() - 18)),
            this);
        detailItem->setFont(detailFont);
        detailItem->setBrush(QBrush(InsightVisualStyle::theme().textSecondary));
        detailItem->setPos(rect.left() + 10, rect.top() + 32);
    }

    ToggleHandler toggleHandler;

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override
    {
        setZValue(45);
        setPen(hoverPen);
        QGraphicsRectItem::hoverEnterEvent(event);
    }

    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override
    {
        setZValue(collapsed ? 12 : 14);
        setPen(isSelected() ? InsightVisualStyle::selectedPen() : normalPen);
        QGraphicsRectItem::hoverLeaveEvent(event);
    }

    void mousePressEvent(QGraphicsSceneMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && toggleHandler) {
            setSelected(true);
            setPen(InsightVisualStyle::selectedPen());
            toggleHandler(key);
            event->accept();
            return;
        }
        QGraphicsRectItem::mousePressEvent(event);
    }

private:
    SignalKernelGraphFanoutGroup group;
    QString key;
    bool collapsed = false;
    QPen normalPen;
    QPen hoverPen;
};

QString hoverKeyForNode(const SignalKernelGraphNode& node)
{
    const QString stable = symbolStableKeyText(node.stableKey);
    if (!stable.isEmpty())
        return stable;
    return QStringLiteral("%1|%2|%3|%4")
        .arg(QString::number(node.id),
             QString::number(static_cast<int>(node.role)),
             node.displayName,
             node.navigateCodeLink.fileName);
}

SignalKernelGraphNodeItem* nodeItemFromGraphicsItem(QGraphicsItem* item)
{
    while (item) {
        if (auto* nodeItem =
                dynamic_cast<SignalKernelGraphNodeItem*>(item)) {
            return nodeItem;
        }
        item = item->parentItem();
    }
    return nullptr;
}

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

QRectF fixedFanoutCardRect(const QRectF& groupBounds,
                           SignalKernelGraphNodeRole role)
{
    if (!groupBounds.isValid())
        return {};
    const qreal centerX =
        role == SignalKernelGraphNodeRole::Input ? -kColumnOffset
                                                 : kColumnOffset;
    return QRectF(centerX - kFanoutGroupCardWidth / 2.0,
                  groupBounds.center().y() - kFanoutGroupCardHeight / 2.0,
                  kFanoutGroupCardWidth,
                  kFanoutGroupCardHeight);
}

struct InputLaneBand {
    SignalKernelGraphInputLane lane = SignalKernelGraphInputLane::Data;
    QRectF rect;
};

struct InputLaneLayout {
    QHash<int, QRectF> nodeRects;
    QList<InputLaneBand> bands;
    QRectF bounds;
};

InputLaneLayout layoutInputLanes(const QList<SignalKernelGraphNode>& inputs)
{
    InputLaneLayout layout;
    if (inputs.isEmpty())
        return layout;

    QList<QList<SignalKernelGraphNode>> lanes;
    int nonEmptyLaneCount = 0;
    for (SignalKernelGraphInputLane lane : inputLaneOrder()) {
        QList<SignalKernelGraphNode> laneNodes;
        for (const SignalKernelGraphNode& node : inputs) {
            if (node.inputLane == lane)
                laneNodes.append(node);
        }
        if (!laneNodes.isEmpty())
            ++nonEmptyLaneCount;
        lanes.append(laneNodes);
    }

    const qreal totalHeight =
        qMax<qreal>(0.0,
                    (inputs.size() - 1) * kVerticalSpacing
                        + qMax(0, nonEmptyLaneCount - 1) * 44.0);
    qreal y = -totalHeight / 2.0;
    bool firstLane = true;
    for (int laneIndex = 0; laneIndex < lanes.size(); ++laneIndex) {
        const QList<SignalKernelGraphNode>& laneNodes = lanes.at(laneIndex);
        if (laneNodes.isEmpty())
            continue;
        if (!firstLane)
            y += 44.0;
        firstLane = false;

        QRectF laneRect;
        bool firstNode = true;
        for (const SignalKernelGraphNode& node : laneNodes) {
            const QRectF rect = nodeRectAt(-kColumnOffset, y);
            layout.nodeRects.insert(node.id, rect);
            laneRect = firstNode ? rect : laneRect.united(rect);
            layout.bounds = layout.bounds.isValid()
                ? layout.bounds.united(rect)
                : rect;
            firstNode = false;
            y += kVerticalSpacing;
        }

        InputLaneBand band;
        band.lane = inputLaneOrder().at(laneIndex);
        band.rect = laneRect.adjusted(-32, -48, 32, 32);
        layout.bands.append(band);
    }

    return layout;
}

void addInputLaneBand(QGraphicsScene* scene,
                      const InputLaneBand& band,
                      const QFont& font)
{
    if (!scene || !band.rect.isValid())
        return;
    QColor fill = inputLaneColor(band.lane);
    fill.setAlpha(74);
    QPen pen(inputLaneColor(band.lane).darker(140), 1.0);
    auto* rectItem = scene->addRect(band.rect, pen, QBrush(fill));
    rectItem->setZValue(-35);

    QFont labelFont = font;
    labelFont = InsightVisualStyle::labelFont(font);
    auto* label = scene->addSimpleText(
        SignalKernelGraphService::inputLaneDisplayName(band.lane),
        labelFont);
    label->setBrush(QBrush(InsightVisualStyle::theme().textSecondary));
    label->setPos(band.rect.left() + 10, band.rect.top() + 8);
    label->setZValue(-13);
}

QList<SignalKernelGraphModuleGroup> moduleGroupsForReport(
    const SignalKernelGraphReport& report)
{
    QList<SignalKernelGraphModuleGroup> groups;
    QHash<QString, int> groupIndexByModule;

    auto appendNode = [&](const SignalKernelGraphNode& node) {
        QString moduleName = moduleDisplayNameForNode(node);
        if (moduleName.isEmpty())
            moduleName = QStringLiteral("<unknown>");
        if (!groupIndexByModule.contains(moduleName)) {
            SignalKernelGraphModuleGroup group;
            group.moduleName = moduleName;
            group.crossModule = !report.kernelModuleName.isEmpty()
                && moduleName != report.kernelModuleName;
            groupIndexByModule.insert(moduleName, groups.size());
            groups.append(group);
        }
        groups[groupIndexByModule.value(moduleName)].nodeIds.append(node.id);
    };

    for (const SignalKernelGraphNode& node : report.inputs)
        appendNode(node);
    appendNode(report.kernel);
    for (const SignalKernelGraphNode& node : report.outputs)
        appendNode(node);
    return groups;
}

void addModuleWrapper(QGraphicsScene* scene,
                      const SignalKernelGraphModuleGroup& group,
                      const QHash<int, QRectF>& nodeRects,
                      const QFont& font)
{
    if (!scene)
        return;

    const QRectF groupRect =
        unitedRectForGroup(nodeRects, group.nodeIds).adjusted(-24, -50, 24, 26);
    if (!groupRect.isValid())
        return;

    QPen pen(group.crossModule
                 ? InsightVisualStyle::theme().borderStrong
                 : InsightVisualStyle::roleColor(InsightVisualRole::Kernel),
             group.crossModule ? 1.0 : 1.3,
             group.crossModule ? Qt::DashLine : Qt::SolidLine);
    QColor fill = group.crossModule
        ? InsightVisualStyle::theme().panelSubtle
        : InsightVisualStyle::roleFillColor(InsightVisualRole::Kernel);
    fill.setAlpha(group.crossModule ? 34 : 48);
    auto* wrapper = scene->addRect(groupRect, pen, QBrush(fill));
    wrapper->setZValue(-25);

    QFont labelFont = font;
    labelFont = InsightVisualStyle::labelFont(font);
    auto* label = scene->addSimpleText(group.moduleName, labelFont);
    label->setBrush(QBrush(InsightVisualStyle::theme().textSecondary));
    label->setPos(groupRect.right() - label->boundingRect().width() - 10,
                  groupRect.top() + 8);
    label->setZValue(-14);
}

void addSectionLabel(QGraphicsScene* scene,
                     const QString& text,
                     qreal x,
                     qreal y,
                     const QFont& font)
{
    if (!scene)
        return;
    QFont labelFont = InsightVisualStyle::labelFont(font);
    auto* label = scene->addSimpleText(text, labelFont);
    label->setBrush(QBrush(InsightVisualStyle::theme().textSecondary));
    label->setPos(x - label->boundingRect().width() / 2.0, y);
    label->setZValue(20);
}
}

SignalKernelGraphPanelCoordinator::SignalKernelGraphPanelCoordinator(
    QWidget* parent)
{
    auto* panel = new QWidget(parent);
    panel->setObjectName(QStringLiteral("signalKernelGraphPanel"));
    InsightVisualStyle::applyPanel(panel);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    titleLabel = new QLabel(QStringLiteral("Signal Kernel Graph"), panel);
    titleLabel->setObjectName(QStringLiteral("signalKernelGraphTitle"));
    InsightVisualStyle::applyTitleLabel(titleLabel);
    layout->addWidget(titleLabel);

    auto* controlsLayout = new QHBoxLayout();
    controlsLayout->setContentsMargins(0, 0, 0, 0);
    controlsLayout->setSpacing(6);
    graphSearchEdit = new QLineEdit(panel);
    graphSearchEdit->setObjectName(
        QStringLiteral("signalKernelGraphSearchEdit"));
    graphSearchEdit->setPlaceholderText(QStringLiteral("Search graph"));
    graphSearchEdit->setClearButtonEnabled(true);
    InsightVisualStyle::applySearchField(graphSearchEdit);
    controlsLayout->addWidget(graphSearchEdit, 1);

    showInputsCheck = new QCheckBox(QStringLiteral("Inputs"), panel);
    showInputsCheck->setObjectName(
        QStringLiteral("signalKernelGraphShowInputsCheck"));
    showInputsCheck->setChecked(graphShowInputs);
    InsightVisualStyle::applySegmentedCheckBox(showInputsCheck);
    controlsLayout->addWidget(showInputsCheck);

    showOutputsCheck = new QCheckBox(QStringLiteral("Outputs"), panel);
    showOutputsCheck->setObjectName(
        QStringLiteral("signalKernelGraphShowOutputsCheck"));
    showOutputsCheck->setChecked(graphShowOutputs);
    InsightVisualStyle::applySegmentedCheckBox(showOutputsCheck);
    controlsLayout->addWidget(showOutputsCheck);

    crossModuleOnlyCheck =
        new QCheckBox(QStringLiteral("Cross-module"), panel);
    crossModuleOnlyCheck->setObjectName(
        QStringLiteral("signalKernelGraphCrossModuleOnlyCheck"));
    crossModuleOnlyCheck->setChecked(graphCrossModuleOnly);
    InsightVisualStyle::applySegmentedCheckBox(crossModuleOnlyCheck);
    controlsLayout->addWidget(crossModuleOnlyCheck);
    layout->addLayout(controlsLayout);

    QObject::connect(graphSearchEdit,
                     &QLineEdit::textChanged,
                     graphSearchEdit,
                     [this](const QString& text) {
                         graphSearchText = text;
                         renderReport(currentReport);
                     });
    QObject::connect(showInputsCheck,
                     &QCheckBox::toggled,
                     showInputsCheck,
                     [this](bool checked) {
                         graphShowInputs = checked;
                         renderReport(currentReport);
                     });
    QObject::connect(showOutputsCheck,
                     &QCheckBox::toggled,
                     showOutputsCheck,
                     [this](bool checked) {
                         graphShowOutputs = checked;
                         renderReport(currentReport);
                     });
    QObject::connect(crossModuleOnlyCheck,
                     &QCheckBox::toggled,
                     crossModuleOnlyCheck,
                     [this](bool checked) {
                         graphCrossModuleOnly = checked;
                         renderReport(currentReport);
                     });

    graphScene = new QGraphicsScene(panel);
    graphView = new SignalKernelGraphView(graphScene, panel);
    graphView->setObjectName(QStringLiteral("signalKernelGraphView"));
    graphView->setRenderHint(QPainter::Antialiasing, true);
    graphView->setDragMode(QGraphicsView::ScrollHandDrag);
    graphView->setFocusPolicy(Qt::StrongFocus);
    graphView->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    graphView->setResizeAnchor(QGraphicsView::AnchorViewCenter);
    graphView->setBackgroundBrush(InsightVisualStyle::canvasBrush());
    graphView->setFrameShape(QFrame::StyledPanel);
    graphView->setStyleSheet(QStringLiteral(
        "QGraphicsView#signalKernelGraphView {"
        "  background: %1;"
        "  border: 1px solid %2;"
        "  border-radius: 6px;"
        "}")
                                 .arg(InsightVisualStyle::theme()
                                          .canvasBackground.name(),
                                      InsightVisualStyle::theme().border.name()));
    if (auto* signalGraphView =
            dynamic_cast<SignalKernelGraphView*>(graphView)) {
        signalGraphView->pressHandler =
            [this](const QPoint& viewPosition,
                   Qt::MouseButton button,
                   Qt::KeyboardModifiers modifiers) {
                if (!graphView)
                    return false;
                const QList<QGraphicsItem*> items =
                    graphView->items(viewPosition);
                for (QGraphicsItem* item : items) {
                    SignalKernelGraphNodeItem* nodeItem =
                        nodeItemFromGraphicsItem(item);
                    if (!nodeItem)
                        continue;
                    if (button != Qt::RightButton || modifiers != Qt::NoModifier)
                        return false;
                    showNodePreview(nodeItem->graphNode(),
                                    nodeItem->sceneBoundingRect());
                    return true;
                }
                if ((button == Qt::LeftButton || button == Qt::RightButton)
                    && hoverPopup && hoverPopup->isVisible()) {
                    closeNodePreviewNow();
                    return true;
                }
                return false;
            };
        signalGraphView->doubleClickHandler =
            [this](const QPoint& viewPosition) {
                if (!graphView)
                    return false;
                const QList<QGraphicsItem*> items =
                    graphView->items(viewPosition);
                for (QGraphicsItem* item : items) {
                    SignalKernelGraphNodeItem* nodeItem =
                        nodeItemFromGraphicsItem(item);
                    if (!nodeItem)
                        continue;
                    closeNodePreviewNow();
                    navigateNode(nodeItem->graphNode());
                    return true;
                }
                return false;
            };
    }
    layout->addWidget(graphView, 1);

    graphDock = new QDockWidget(QStringLiteral("Signal Kernel Graph"), parent);
    graphDock->setObjectName(QStringLiteral("signalKernelGraphDock"));
    graphDock->setWidget(panel);
    graphDock->setFeatures(QDockWidget::DockWidgetMovable |
                           QDockWidget::DockWidgetFloatable |
                           QDockWidget::DockWidgetClosable);
    graphDock->hide();

    hoverPopup = new EditorHoverPopup(graphDock);
    hoverCloseTimer = new QTimer(graphDock);
    hoverCloseTimer->setSingleShot(true);
    QObject::connect(hoverCloseTimer,
                     &QTimer::timeout,
                     graphDock,
                     [this]() {
                         closeNodePreviewNow();
                     });

    renderUnavailable(QStringLiteral("No signal selected."));
}

SignalKernelGraphPanelCoordinator::~SignalKernelGraphPanelCoordinator() = default;

void SignalKernelGraphPanelCoordinator::renderReportForTest(
    const SignalKernelGraphReport& report)
{
    renderReport(report);
}

int SignalKernelGraphPanelCoordinator::collapsedFanoutGroupCountForTest() const
{
    int count = 0;
    const auto countGroup = [&](const SignalKernelGraphFanoutGroup& group) {
        if (collapsedFanoutGroupKeys.contains(fanoutGroupUiKey(group)))
            ++count;
    };
    for (const SignalKernelGraphFanoutGroup& group :
         currentReport.inputFanoutGroups) {
        countGroup(group);
    }
    for (const SignalKernelGraphFanoutGroup& group :
         currentReport.outputFanoutGroups) {
        countGroup(group);
    }
    return count;
}

int SignalKernelGraphPanelCoordinator::visibleGraphNodeCountForTest() const
{
    return lastVisibleGraphNodeCount;
}

int SignalKernelGraphPanelCoordinator::renderedFanoutGroupItemCountForTest() const
{
    return lastRenderedFanoutGroupItemCount;
}

QRectF SignalKernelGraphPanelCoordinator::lastRenderedFanoutGroupRectForTest() const
{
    return lastRenderedFanoutGroupRect;
}

bool SignalKernelGraphPanelCoordinator::toggleFanoutGroupForTest(
    const QString& groupKey)
{
    const bool known = knownFanoutGroupKeys.contains(groupKey);
    if (!known)
        return false;
    toggleFanoutGroup(groupKey);
    return true;
}

void SignalKernelGraphPanelCoordinator::setGraphSearchTextForTest(
    const QString& text)
{
    graphSearchText = text;
    if (graphSearchEdit) {
        const QSignalBlocker blocker(graphSearchEdit);
        graphSearchEdit->setText(text);
    }
    renderReport(currentReport);
}

void SignalKernelGraphPanelCoordinator::setGraphFilterForTest(
    bool showInputs,
    bool showOutputs,
    bool crossModuleOnly)
{
    graphShowInputs = showInputs;
    graphShowOutputs = showOutputs;
    graphCrossModuleOnly = crossModuleOnly;
    if (showInputsCheck) {
        const QSignalBlocker blocker(showInputsCheck);
        showInputsCheck->setChecked(showInputs);
    }
    if (showOutputsCheck) {
        const QSignalBlocker blocker(showOutputsCheck);
        showOutputsCheck->setChecked(showOutputs);
    }
    if (crossModuleOnlyCheck) {
        const QSignalBlocker blocker(crossModuleOnlyCheck);
        crossModuleOnlyCheck->setChecked(crossModuleOnly);
    }
    renderReport(currentReport);
}

int SignalKernelGraphPanelCoordinator::searchMatchCountForTest() const
{
    return lastSearchMatchCount;
}

int SignalKernelGraphPanelCoordinator::focusedSearchNodeIdForTest() const
{
    return lastFocusedSearchNodeId;
}

void SignalKernelGraphPanelCoordinator::setNavigationHandler(
    std::function<bool(const QString&, int, int)> handler)
{
    navigationHandler = std::move(handler);
    if (hoverPopup) {
        hoverPopup->setNavigationHandler(
            [this](const QString& fileName, int line, int column) {
                if (navigationHandler
                    && !navigationHandler(fileName, line, column)) {
                    showStatusMessage(
                        QStringLiteral("Signal kernel graph jump failed"),
                        4000);
                }
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
    const QString& moduleName,
    const QString& signalAccessPath)
{
    currentQuery = {};
    currentQuery.signalName = symbolName;
    currentQuery.signalAccessPath = signalAccessPath;
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

    closeNodePreviewNow();
    graphScene->clear();
    currentReport = report;
    lastVisibleGraphNodeCount = 0;
    lastRenderedFanoutGroupItemCount = 0;
    lastRenderedFanoutGroupRect = {};
    lastSearchMatchCount = 0;
    lastFocusedSearchNodeId = -1;
    if (!report.found) {
        renderUnavailable(report.notFoundReasonDisplayName);
        return;
    }
    initializeFanoutCollapseState(report);

    QList<SignalKernelGraphNode> visibleInputs;
    QList<SignalKernelGraphNode> visibleOutputs;
    QHash<int, SignalKernelGraphNode> visibleNodesById;
    visibleNodesById.insert(report.kernel.id, report.kernel);
    for (const SignalKernelGraphNode& node : report.inputs) {
        if (!nodePassesGraphFilter(node))
            continue;
        visibleInputs.append(node);
        visibleNodesById.insert(node.id, node);
    }
    for (const SignalKernelGraphNode& node : report.outputs) {
        if (!nodePassesGraphFilter(node))
            continue;
        visibleOutputs.append(node);
        visibleNodesById.insert(node.id, node);
    }
    const QString searchText = graphSearchText.trimmed();
    bool hasSearchFocusRect = false;
    QRectF firstSearchFocusRect;

    if (titleLabel) {
        titleLabel->setText(QStringLiteral("%1   Inputs %2/%3   Outputs %4/%5")
                                .arg(report.kernel.displayName)
                                .arg(visibleInputs.size())
                                .arg(report.inputs.size())
                                .arg(visibleOutputs.size())
                                .arg(report.outputs.size()));
    }
    if (graphDock) {
        graphDock->setWindowTitle(QStringLiteral("Signal Kernel Graph: %1")
                                      .arg(report.kernel.displayName));
    }

    const QFont baseFont = graphView ? graphView->font() : QFont();
    QHash<int, QRectF> rawNodeRects;
    QHash<int, QRectF> moduleWrapperRects;
    QHash<int, QRectF> visibleRects;
    QHash<int, int> collapsedNodeToGroupRectId;
    QSet<int> hiddenNodeIds;
    QHash<int, SignalKernelGraphNodeItem*> nodeItems;
    const InputLaneLayout inputLayout = layoutInputLanes(visibleInputs);
    for (auto it = inputLayout.nodeRects.constBegin();
         it != inputLayout.nodeRects.constEnd();
         ++it) {
        rawNodeRects.insert(it.key(), it.value());
        moduleWrapperRects.insert(it.key(), it.value());
    }
    const QRectF outputBounds =
        visibleOutputs.isEmpty()
            ? QRectF()
            : QRectF(kColumnOffset - kNodeWidth / 2.0,
                     rowY(0, visibleOutputs.size()) - kNodeHeight / 2.0,
                     kNodeWidth,
                     (visibleOutputs.size() - 1) * kVerticalSpacing
                        + kNodeHeight);
    for (int i = 0; i < visibleOutputs.size(); ++i) {
        rawNodeRects.insert(
            visibleOutputs.at(i).id,
            nodeRectAt(kColumnOffset, rowY(i, visibleOutputs.size())));
        moduleWrapperRects.insert(visibleOutputs.at(i).id,
                                  rawNodeRects.value(visibleOutputs.at(i).id));
    }
    rawNodeRects.insert(report.kernel.id, nodeRectAt(0, 0));
    moduleWrapperRects.insert(report.kernel.id,
                              rawNodeRects.value(report.kernel.id));

    addSectionLabel(graphScene,
                    QStringLiteral("Inputs"),
                    -kColumnOffset,
                    (inputLayout.bounds.isValid()
                         ? inputLayout.bounds.top()
                         : -kNodeHeight / 2.0)
                        - 66,
                    baseFont);
    addSectionLabel(graphScene,
                    QStringLiteral("Kernel"),
                    0,
                    -86,
                    baseFont);
    addSectionLabel(graphScene,
                    QStringLiteral("Outputs"),
                    kColumnOffset,
                    (outputBounds.isValid()
                         ? outputBounds.top()
                         : -kNodeHeight / 2.0)
                        - 58,
                    baseFont);

    for (const InputLaneBand& band : inputLayout.bands)
        addInputLaneBand(graphScene, band, baseFont);

    int nextFanoutGroupRectId = -1000;
    auto addFanoutGroupItem =
        [&](const SignalKernelGraphFanoutGroup& sourceGroup) {
            SignalKernelGraphFanoutGroup group = sourceGroup;
            group.nodeIds.clear();
            for (int nodeId : sourceGroup.nodeIds) {
                if (rawNodeRects.contains(nodeId))
                    group.nodeIds.append(nodeId);
            }
            group.nodeCount = group.nodeIds.size();
            if (group.nodeIds.isEmpty())
                return;

            const QRectF groupBounds =
                unitedRectForGroup(rawNodeRects, group.nodeIds)
                    .adjusted(-18, -34, 18, 28);
            if (!groupBounds.isValid())
                return;

            const bool collapsed = isFanoutGroupCollapsed(group);
            QRectF itemRect = groupBounds;
            if (collapsed) {
                itemRect = fixedFanoutCardRect(groupBounds, group.role);
            } else {
                itemRect = QRectF(groupBounds.left(),
                                  groupBounds.top(),
                                  groupBounds.width(),
                                  qMin<qreal>(52.0, groupBounds.height()));
            }

            const int matchingGroupNodeId =
                collapsed ? firstMatchingFanoutGroupNodeId(group,
                                                           visibleNodesById,
                                                           searchText)
                          : -1;
            const bool searchMatch = matchingGroupNodeId >= 0;
            const QString key = fanoutGroupUiKey(group);
            auto* item = new SignalKernelGraphFanoutGroupItem(
                group,
                key,
                collapsed,
                itemRect,
                baseFont,
                searchMatch);
            item->setZValue(searchMatch ? 18 : (collapsed ? 12 : 14));
            item->toggleHandler = [this](const QString& groupKey) {
                toggleFanoutGroup(groupKey);
            };
            graphScene->addItem(item);
            ++lastRenderedFanoutGroupItemCount;
            lastRenderedFanoutGroupRect = itemRect;
            if (searchMatch) {
                ++lastSearchMatchCount;
                if (!hasSearchFocusRect) {
                    hasSearchFocusRect = true;
                    firstSearchFocusRect = itemRect;
                    lastFocusedSearchNodeId = matchingGroupNodeId;
                }
            }

            if (!collapsed)
                return;

            const int groupRectId = nextFanoutGroupRectId--;
            visibleRects.insert(groupRectId, itemRect);
            for (int nodeId : group.nodeIds) {
                hiddenNodeIds.insert(nodeId);
                collapsedNodeToGroupRectId.insert(nodeId, groupRectId);
                moduleWrapperRects.insert(nodeId, itemRect);
            }
        };

    for (const SignalKernelGraphFanoutGroup& group : report.inputFanoutGroups)
        addFanoutGroupItem(group);
    for (const SignalKernelGraphFanoutGroup& group : report.outputFanoutGroups)
        addFanoutGroupItem(group);

    auto addNode = [&](const SignalKernelGraphNode& node,
                       const QRectF& rect) {
        if (hiddenNodeIds.contains(node.id))
            return;
        visibleRects.insert(node.id, rect);
        const bool searchMatch = nodeMatchesGraphSearch(node);
        auto* item =
            new SignalKernelGraphNodeItem(node, rect, baseFont, searchMatch);
        item->setZValue(searchMatch ? 18 : 10);
        item->previewHandler =
            [this](const SignalKernelGraphNode& clickedNode,
                   const QRectF& nodeSceneRect) {
                showNodePreview(clickedNode, nodeSceneRect);
            };
        item->navigateHandler =
            [this](const SignalKernelGraphNode& clickedNode) {
                closeNodePreviewNow();
                navigateNode(clickedNode);
            };
        item->rebaseHandler =
            [this](const SignalKernelGraphNode& clickedNode) {
                rebaseToNode(clickedNode);
        };
        graphScene->addItem(item);
        nodeItems.insert(node.id, item);
        ++lastVisibleGraphNodeCount;
        if (searchMatch) {
            ++lastSearchMatchCount;
            if (!hasSearchFocusRect) {
                hasSearchFocusRect = true;
                firstSearchFocusRect = rect;
                lastFocusedSearchNodeId = node.id;
            }
        }
    };

    for (const SignalKernelGraphNode& node : visibleInputs) {
        addNode(node,
                inputLayout.nodeRects.value(
                    node.id,
                    nodeRectAt(-kColumnOffset, 0)));
    }
    addNode(report.kernel, nodeRectAt(0, 0));
    for (int i = 0; i < visibleOutputs.size(); ++i) {
        addNode(visibleOutputs.at(i),
                rawNodeRects.value(visibleOutputs.at(i).id,
                                   nodeRectAt(kColumnOffset,
                                              rowY(i, visibleOutputs.size()))));
    }

    SignalKernelGraphReport visibleReport = report;
    visibleReport.inputs = visibleInputs;
    visibleReport.outputs = visibleOutputs;
    for (const SignalKernelGraphModuleGroup& group :
         moduleGroupsForReport(visibleReport)) {
        addModuleWrapper(graphScene, group, moduleWrapperRects, baseFont);
    }

    QPen edgePen(InsightVisualStyle::theme().borderStrong, 1.5);
    QSet<QString> renderedEdgeKeys;
    for (const SignalKernelGraphEdge& edge : report.edges) {
        const int fromRectId =
            collapsedNodeToGroupRectId.value(edge.fromNodeId, edge.fromNodeId);
        const int toRectId =
            collapsedNodeToGroupRectId.value(edge.toNodeId, edge.toNodeId);
        if (fromRectId == toRectId)
            continue;
        const bool collapsedEdge =
            fromRectId != edge.fromNodeId || toRectId != edge.toNodeId;
        if (collapsedEdge) {
            const QString edgeKey =
                QStringLiteral("%1:%2").arg(fromRectId).arg(toRectId);
            if (renderedEdgeKeys.contains(edgeKey))
                continue;
            renderedEdgeKeys.insert(edgeKey);
        }

        if (!visibleRects.contains(fromRectId)
            || !visibleRects.contains(toRectId)) {
            continue;
        }
        const QRectF fromRect = visibleRects.value(fromRectId);
        const QRectF toRect = visibleRects.value(toRectId);
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
    if (graphView) {
        graphView->fitInView(bounds, Qt::KeepAspectRatio);
        if (hasSearchFocusRect)
            graphView->centerOn(firstSearchFocusRect.center());
    }

    showStatusMessage(searchText.isEmpty()
                          ? QStringLiteral("Rendered signal kernel graph")
                          : QStringLiteral("%1 graph match(es)")
                                .arg(lastSearchMatchCount),
                      1500);
}

void SignalKernelGraphPanelCoordinator::renderUnavailable(
    const QString& message)
{
    if (!graphScene)
        return;

    closeNodePreviewNow();
    graphScene->clear();
    lastVisibleGraphNodeCount = 0;
    lastRenderedFanoutGroupItemCount = 0;
    lastRenderedFanoutGroupRect = {};
    lastSearchMatchCount = 0;
    lastFocusedSearchNodeId = -1;
    const QString text = message.isEmpty()
        ? QStringLiteral("Signal kernel graph unavailable.")
        : message;
    auto* item = graphScene->addText(text);
    item->setDefaultTextColor(InsightVisualStyle::theme().textMuted);
    item->setPos(0, 0);
    graphScene->setSceneRect(item->boundingRect().adjusted(-40, -40, 40, 40));
    if (titleLabel)
        titleLabel->setText(QStringLiteral("Signal Kernel Graph"));
    if (graphDock)
        graphDock->setWindowTitle(QStringLiteral("Signal Kernel Graph"));
}

bool SignalKernelGraphPanelCoordinator::nodePassesGraphFilter(
    const SignalKernelGraphNode& node) const
{
    if (node.role == SignalKernelGraphNodeRole::Kernel)
        return true;
    if (node.role == SignalKernelGraphNodeRole::Input && !graphShowInputs)
        return false;
    if (node.role == SignalKernelGraphNodeRole::Output && !graphShowOutputs)
        return false;
    if (graphCrossModuleOnly && !node.crossModule)
        return false;
    return true;
}

bool SignalKernelGraphPanelCoordinator::nodeMatchesGraphSearch(
    const SignalKernelGraphNode& node) const
{
    return nodeMatchesSearchText(node, graphSearchText.trimmed());
}

QString SignalKernelGraphPanelCoordinator::fanoutGroupUiKey(
    const SignalKernelGraphFanoutGroup& group) const
{
    if (!group.groupKey.isEmpty())
        return group.groupKey;
    return QStringLiteral("%1:%2:%3")
        .arg(static_cast<int>(group.role))
        .arg(static_cast<int>(group.inputLane))
        .arg(group.moduleName);
}

void SignalKernelGraphPanelCoordinator::initializeFanoutCollapseState(
    const SignalKernelGraphReport& report)
{
    auto rememberGroup = [&](const SignalKernelGraphFanoutGroup& group) {
        if (!group.highFanout)
            return;
        const QString key = fanoutGroupUiKey(group);
        if (key.isEmpty() || knownFanoutGroupKeys.contains(key))
            return;
        knownFanoutGroupKeys.insert(key);
        collapsedFanoutGroupKeys.insert(key);
    };

    for (const SignalKernelGraphFanoutGroup& group : report.inputFanoutGroups)
        rememberGroup(group);
    for (const SignalKernelGraphFanoutGroup& group : report.outputFanoutGroups)
        rememberGroup(group);
}

bool SignalKernelGraphPanelCoordinator::isFanoutGroupCollapsed(
    const SignalKernelGraphFanoutGroup& group) const
{
    return collapsedFanoutGroupKeys.contains(fanoutGroupUiKey(group));
}

void SignalKernelGraphPanelCoordinator::toggleFanoutGroup(
    const QString& groupKey)
{
    if (groupKey.isEmpty())
        return;
    if (collapsedFanoutGroupKeys.contains(groupKey))
        collapsedFanoutGroupKeys.remove(groupKey);
    else
        collapsedFanoutGroupKeys.insert(groupKey);
    renderReport(currentReport);
}

void SignalKernelGraphPanelCoordinator::showNodePreview(
    const SignalKernelGraphNode& node,
    const QRectF& nodeSceneRect)
{
    if (!hoverPopup || !graphView)
        return;

    if (hoverCloseTimer)
        hoverCloseTimer->stop();

    const QString hoverKey = hoverKeyForNode(node);
    if (hoverPopup->isVisible() && currentHoverNodeKey == hoverKey)
        return;
    currentHoverNodeKey = hoverKey;
    graphView->setFocus(Qt::MouseFocusReason);
    hoverPopup->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);

    CodePreviewQuery query;
    query.codeLink = node.previewCodeLink;
    query.sourceRange = node.evidenceRange;
    query.title = QStringLiteral("%1: %2")
                      .arg(roleTitle(node.role), node.displayName);
    query.detail = hoverDetailForNode(node);
    const CodePreviewReport report =
        CodePreviewService::getInstance()->previewForCodeLink(query);
    QWidget* viewport = graphView->viewport();
    if (!viewport)
        return;

    const QRect nodeViewRect =
        graphView->mapFromScene(nodeSceneRect).boundingRect();
    const QRect nodeGlobalRect(
        viewport->mapToGlobal(nodeViewRect.topLeft()),
        viewport->mapToGlobal(nodeViewRect.bottomRight()));
    hoverPopup->showCodePreview(
        report,
        nodeGlobalRect.topRight() + QPoint(14, 0),
        graphView->font());
    placeHoverPopupAvoidingNode(nodeGlobalRect.normalized());
}

void SignalKernelGraphPanelCoordinator::closeNodePreviewDelayed()
{
    if (hoverCloseTimer)
        hoverCloseTimer->start(180);
}

void SignalKernelGraphPanelCoordinator::closeNodePreviewNow()
{
    if (hoverCloseTimer)
        hoverCloseTimer->stop();
    currentHoverNodeKey.clear();
    if (hoverPopup)
        hoverPopup->closePopup();
}

void SignalKernelGraphPanelCoordinator::placeHoverPopupAvoidingNode(
    const QRect& nodeGlobalRect) const
{
    if (!hoverPopup || !hoverPopup->isVisible() || !graphView)
        return;

    QWidget* viewport = graphView->viewport();
    if (!viewport)
        return;

    const QRect rawViewportRect(
        viewport->mapToGlobal(viewport->rect().topLeft()),
        viewport->mapToGlobal(viewport->rect().bottomRight()));
    const int margin = 6;
    const int gap = 12;
    const QRect viewportRect =
        rawViewportRect.normalized().adjusted(margin, margin, -margin, -margin);
    if (viewportRect.isEmpty())
        return;

    const QRect avoidRect =
        nodeGlobalRect.normalized().adjusted(-gap, -gap, gap, gap)
            .intersected(viewportRect);

    QList<QRect> candidates;
    const int rightX = avoidRect.right() + 1;
    if (rightX <= viewportRect.right()) {
        candidates.append(QRect(rightX,
                                viewportRect.top(),
                                viewportRect.right() - rightX + 1,
                                viewportRect.height()));
    }
    const int leftW = avoidRect.left() - viewportRect.left();
    if (leftW > 0) {
        candidates.append(QRect(viewportRect.left(),
                                viewportRect.top(),
                                leftW,
                                viewportRect.height()));
    }
    const int belowY = avoidRect.bottom() + 1;
    if (belowY <= viewportRect.bottom()) {
        candidates.append(QRect(viewportRect.left(),
                                belowY,
                                viewportRect.width(),
                                viewportRect.bottom() - belowY + 1));
    }
    const int aboveH = avoidRect.top() - viewportRect.top();
    if (aboveH > 0) {
        candidates.append(QRect(viewportRect.left(),
                                viewportRect.top(),
                                viewportRect.width(),
                                aboveH));
    }

    if (candidates.isEmpty()) {
        hoverPopup->move(viewportRect.topLeft());
        return;
    }

    QSize popupSize = hoverPopup->size();
    int bestIndex = -1;
    for (int i = 0; i < candidates.size(); ++i) {
        const QRect candidate = candidates.at(i);
        if (candidate.width() >= popupSize.width()
            && candidate.height() >= popupSize.height()) {
            bestIndex = i;
            break;
        }
    }
    if (bestIndex < 0) {
        int bestArea = -1;
        for (int i = 0; i < candidates.size(); ++i) {
            const QRect candidate = candidates.at(i);
            const int area = candidate.width() * candidate.height();
            if (area > bestArea) {
                bestArea = area;
                bestIndex = i;
            }
        }
    }

    const QRect available = candidates.at(bestIndex);
    popupSize.setWidth(qMax(1, qMin(popupSize.width(), available.width())));
    popupSize.setHeight(qMax(1, qMin(popupSize.height(), available.height())));
    hoverPopup->setMaximumSize(available.size());
    hoverPopup->resize(popupSize);

    int x = available.left();
    int y = available.top();
    if (bestIndex == 0) {
        x = available.left();
        y = qBound(available.top(),
                   nodeGlobalRect.top(),
                   available.bottom() - popupSize.height() + 1);
    } else if (bestIndex == 1) {
        x = available.right() - popupSize.width() + 1;
        y = qBound(available.top(),
                   nodeGlobalRect.top(),
                   available.bottom() - popupSize.height() + 1);
    } else if (bestIndex == 2) {
        x = qBound(available.left(),
                   nodeGlobalRect.left(),
                   available.right() - popupSize.width() + 1);
        y = available.top();
    } else {
        x = qBound(available.left(),
                   nodeGlobalRect.left(),
                   available.right() - popupSize.width() + 1);
        y = available.bottom() - popupSize.height() + 1;
    }

    hoverPopup->move(x, y);
}

void SignalKernelGraphPanelCoordinator::navigateNode(
    const SignalKernelGraphNode& node) const
{
    if (!navigationHandler)
        return;
    if (node.navigateCodeLink.fileName.isEmpty()
        || node.navigateCodeLink.line <= 0) {
        showStatusMessage(QStringLiteral("Signal kernel graph node has no source location"),
                          3000);
        return;
    }
    if (!navigationHandler(node.navigateCodeLink.fileName,
                           node.navigateCodeLink.line,
                           node.navigateCodeLink.column)) {
        showStatusMessage(QStringLiteral("Signal kernel graph jump failed"),
                          4000);
    }
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
