#include "signalusagehotspotpanel.h"

#include "editorfileidentity.h"
#include "graphexportui.h"
#include "insightgraphview.h"
#include "insightvisualstyle.h"
#include "semanticindexsnapshot.h"

#include <QAction>
#include <QBrush>
#include <QCheckBox>
#include <QFileInfo>
#include <QFontMetrics>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsPathItem>
#include <QGraphicsSimpleTextItem>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <utility>

namespace {
constexpr int kItemIndexRole = Qt::UserRole + 6100;
constexpr int kMatrixRoleRole = Qt::UserRole + 6101;
constexpr int kMatrixModuleRole = Qt::UserRole + 6102;
constexpr int kMatrixFileRole = Qt::UserRole + 6103;
constexpr qreal kLaneLabelWidth = 220.0;
constexpr qreal kLaneHeight = 64.0;
constexpr qreal kLaneTop = 96.0;
constexpr qreal kTrackDetailsGap = 16.0;
constexpr qreal kTrackDetailsWidth = 260.0;
constexpr qreal kTrackBottomBarHeight = 52.0;
constexpr qreal kTrackRailDefaultWidth = 620.0;
constexpr qreal kTrackRailMinWidth = 220.0;
constexpr qreal kTrackRailMaxWidth = 860.0;
constexpr double kMinTrackZoom = 0.35;
constexpr double kMaxTrackZoom = 3.0;
constexpr qreal kMatrixRowHeaderWidth = 160.0;
constexpr qreal kMatrixColumnWidth = 70.0;
constexpr qreal kMatrixHeaderHeight = 96.0;
constexpr qreal kMatrixRowHeight = 72.0;
const char* kSettingsGroup = "SignalUsageHotspotPanel";

SignalUsageHotspotReport buildReportFromSnapshot(
    const SignalUsageHotspotQuery& query,
    const std::shared_ptr<const SemanticIndexSnapshot>& snapshot)
{
    if (!snapshot) {
        SignalUsageHotspotReport report;
        report.notFoundReason = SignalUsageHotspotNotFoundReason::NoMatchingSignal;
        report.notFoundReasonDisplayName =
            SignalUsageHotspotService::notFoundReasonDisplayName(
                report.notFoundReason);
        return report;
    }

    SemanticIndex immutableIndex;
    immutableIndex.setSnapshot(snapshot);
    SignalUsageHotspotService service(&immutableIndex);
    return service.buildSignalUsageHotspot(query);
}

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

QString matrixRowKey(const QString& moduleName, const QString& fileName)
{
    const QString file = compactFileName(fileName);
    if (moduleName.isEmpty())
        return file.isEmpty() ? QStringLiteral("<unknown>") : file;
    if (file.isEmpty() || file == moduleName)
        return moduleName;
    return QStringLiteral("%1 / %2").arg(moduleName, file);
}

QString matrixColumnDisplayName(SignalUsageHotspotRole role)
{
    switch (role) {
    case SignalUsageHotspotRole::Write:
        return QStringLiteral("Write");
    case SignalUsageHotspotRole::Read:
        return QStringLiteral("Read");
    case SignalUsageHotspotRole::Port:
        return QStringLiteral("Port");
    case SignalUsageHotspotRole::Condition:
        return QStringLiteral("Cond");
    case SignalUsageHotspotRole::Case:
        return QStringLiteral("Case");
    case SignalUsageHotspotRole::Timing:
        return QStringLiteral("Always");
    case SignalUsageHotspotRole::Unknown:
        return QStringLiteral("Assign");
    }
    return QStringLiteral("Assign");
}

QString roleBadgeText(SignalUsageHotspotRole role)
{
    switch (role) {
    case SignalUsageHotspotRole::Write:
        return QStringLiteral("W");
    case SignalUsageHotspotRole::Read:
        return QStringLiteral("R");
    case SignalUsageHotspotRole::Port:
        return QStringLiteral("P");
    case SignalUsageHotspotRole::Condition:
    case SignalUsageHotspotRole::Case:
        return QStringLiteral("C");
    case SignalUsageHotspotRole::Timing:
        return QStringLiteral("A");
    case SignalUsageHotspotRole::Unknown:
        return QStringLiteral("?");
    }
    return QStringLiteral("?");
}

QString signalSummaryText(const SignalUsageHotspotReport& report)
{
    int writes = 0;
    int reads = 0;
    int ports = 0;
    int control = 0;
    if (!report.roleSummaries.isEmpty()) {
        for (const SignalUsageHotspotRoleSummary& summary : report.roleSummaries) {
            switch (summary.role) {
            case SignalUsageHotspotRole::Write:
                writes += summary.count;
                break;
            case SignalUsageHotspotRole::Read:
                reads += summary.count;
                break;
            case SignalUsageHotspotRole::Port:
                ports += summary.count;
                break;
            case SignalUsageHotspotRole::Condition:
            case SignalUsageHotspotRole::Case:
                control += summary.count;
                break;
            case SignalUsageHotspotRole::Timing:
            case SignalUsageHotspotRole::Unknown:
                break;
            }
        }
    } else {
        for (const SignalUsageHotspotItem& item : report.items) {
            switch (item.role) {
        case SignalUsageHotspotRole::Write:
            ++writes;
            break;
        case SignalUsageHotspotRole::Read:
            ++reads;
            break;
        case SignalUsageHotspotRole::Port:
            ++ports;
            break;
        case SignalUsageHotspotRole::Condition:
        case SignalUsageHotspotRole::Case:
            ++control;
            break;
        case SignalUsageHotspotRole::Timing:
        case SignalUsageHotspotRole::Unknown:
            break;
            }
        }
    }
    return QStringLiteral("Signal: %1   W %2   R %3   P %4   C %5")
        .arg(report.declarationDisplayName.isEmpty()
                 ? QStringLiteral("<unknown>")
                 : report.declarationDisplayName)
        .arg(writes)
        .arg(reads)
        .arg(ports)
        .arg(control);
}

QString elidedForWidth(const QString& text, const QFont& font, int width)
{
    return QFontMetrics(font).elidedText(text.trimmed(),
                                         Qt::ElideRight,
                                         qMax(24, width));
}

void addSimpleSceneText(QGraphicsScene* scene,
                        const QString& text,
                        const QFont& font,
                        const QColor& color,
                        const QPointF& pos,
                        qreal z = 1.0)
{
    auto* item = scene->addSimpleText(text, font);
    item->setBrush(QBrush(color));
    item->setPos(pos);
    item->setZValue(z);
}

void addRoundedSceneRect(QGraphicsScene* scene,
                         const QRectF& rect,
                         qreal radius,
                         const QPen& pen,
                         const QBrush& brush,
                         qreal z = 0.0)
{
    QPainterPath path;
    path.addRoundedRect(rect, radius, radius);
    auto* item = scene->addPath(path, pen, brush);
    item->setZValue(z);
}

void addPanelHeader(QGraphicsScene* scene,
                    const QString& title,
                    const SignalUsageHotspotReport& report,
                    qreal width,
                    const QFont& baseFont)
{
    QFont titleFont = InsightVisualStyle::titleFont(baseFont);
    QFont labelFont = InsightVisualStyle::labelFont(baseFont);
    titleFont.setBold(true);
    QFont summaryFont = labelFont;
    summaryFont.setBold(true);
    scene->addRect(0,
                   0,
                   width,
                   64,
                   InsightVisualStyle::hairlinePen(
                       InsightVisualStyle::theme().border),
                   QBrush(InsightVisualStyle::theme().panelBackground));
    addSimpleSceneText(scene,
                       title,
                       titleFont,
                       InsightVisualStyle::theme().textPrimary,
                       QPointF(12, 10));

    const QString summary = signalSummaryText(report);
    addSimpleSceneText(scene,
                       summary,
                       summaryFont,
                       InsightVisualStyle::theme().textPrimary,
                       QPointF(12, 42));

    qreal x = width - 94.0;
    for (int tool = 0; tool < 3; ++tool) {
        QRectF rect(x, 10, 24, 24);
        addRoundedSceneRect(scene,
                            rect,
                            4,
                            InsightVisualStyle::hairlinePen(
                                InsightVisualStyle::theme().border),
                            QBrush(InsightVisualStyle::theme().panelSubtle));
        const QColor iconColor = InsightVisualStyle::theme().textSecondary;
        QPen iconPen(iconColor, 1.2);
        iconPen.setCosmetic(true);
        if (tool == 0) {
            QPainterPath funnel;
            funnel.moveTo(rect.left() + 6, rect.top() + 7);
            funnel.lineTo(rect.right() - 6, rect.top() + 7);
            funnel.lineTo(rect.left() + 14, rect.top() + 13);
            funnel.lineTo(rect.left() + 14, rect.bottom() - 7);
            funnel.lineTo(rect.left() + 10, rect.bottom() - 7);
            funnel.lineTo(rect.left() + 10, rect.top() + 13);
            funnel.closeSubpath();
            scene->addPath(funnel, iconPen, Qt::NoBrush);
        } else if (tool == 1) {
            for (int i = 0; i < 3; ++i) {
                scene->addEllipse(QRectF(rect.left() + 7 + i * 5,
                                         rect.top() + 11,
                                         2,
                                         2),
                                  Qt::NoPen,
                                  QBrush(iconColor));
            }
        } else {
            scene->addLine(rect.left() + 8, rect.top() + 8,
                           rect.right() - 8, rect.bottom() - 8, iconPen);
            scene->addLine(rect.right() - 8, rect.top() + 8,
                           rect.left() + 8, rect.bottom() - 8, iconPen);
        }
        x += rect.width() + 6;
    }
}

QColor mixedColor(const QColor& from, const QColor& to, double amount)
{
    const double t = std::clamp(amount, 0.0, 1.0);
    return QColor::fromRgbF(from.redF() + (to.redF() - from.redF()) * t,
                            from.greenF() + (to.greenF() - from.greenF()) * t,
                            from.blueF() + (to.blueF() - from.blueF()) * t,
                            from.alphaF() + (to.alphaF() - from.alphaF()) * t);
}

QColor alphaColor(QColor color, int alpha)
{
    color.setAlpha(alpha);
    return color;
}

QColor hotspotRoleColor(SignalUsageHotspotRole role)
{
    switch (role) {
    case SignalUsageHotspotRole::Write:
        return QColor(QStringLiteral("#f97316"));
    case SignalUsageHotspotRole::Read:
        return QColor(QStringLiteral("#2563eb"));
    case SignalUsageHotspotRole::Port:
        return QColor(QStringLiteral("#22c55e"));
    case SignalUsageHotspotRole::Condition:
    case SignalUsageHotspotRole::Case:
        return QColor(QStringLiteral("#a855f7"));
    case SignalUsageHotspotRole::Timing:
        return QColor(QStringLiteral("#60a5fa"));
    case SignalUsageHotspotRole::Unknown:
        return QColor(QStringLiteral("#93c5fd"));
    }
    return QColor(QStringLiteral("#93c5fd"));
}

QColor heatColorForRole(SignalUsageHotspotRole role, double intensity)
{
    const QColor base = hotspotRoleColor(role);
    const QColor low = QColor(QStringLiteral("#f5f7fb"));
    return mixedColor(low, base, std::clamp(0.18 + intensity * 0.48, 0.0, 1.0));
}

QPen hotspotSelectedPen(qreal width = 2.0)
{
    return QPen(InsightVisualStyle::theme().accent, width);
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
        const QColor roleColor = hotspotRoleColor(role);
        normalPen = QPen(roleColor.darker(112), 1.0);
        setPen(normalPen);
        setBrush(QBrush(roleColor));
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
        setPen(isSelected() ? hotspotSelectedPen() : normalPen);
        QGraphicsRectItem::hoverLeaveEvent(event);
    }

    void mousePressEvent(QGraphicsSceneMouseEvent* event) override
    {
        setSelected(true);
        setPen(hotspotSelectedPen());
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

class HotspotUsageCardItem : public QGraphicsRectItem
{
public:
    using ItemHandler = std::function<void(int)>;

    HotspotUsageCardItem(int itemIndex,
                         SignalUsageHotspotRole role,
                         const QRectF& rect,
                         bool selected)
        : QGraphicsRectItem(rect),
          index(itemIndex)
    {
        setAcceptHoverEvents(true);
        setAcceptedMouseButtons(Qt::LeftButton);
        setFlag(QGraphicsItem::ItemIsSelectable, true);
        const QColor roleColor = hotspotRoleColor(role);
        normalPen = selected
            ? QPen(alphaColor(InsightVisualStyle::theme().accent, 18), 0.6)
            : QPen(alphaColor(InsightVisualStyle::theme().border, 95), 0.6);
        setPen(normalPen);
        setBrush(QBrush(InsightVisualStyle::theme().panelBackground));
        setZValue(1.0);
    }

    ItemHandler hoverHandler;
    ItemHandler selectHandler;
    ItemHandler navigateHandler;

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override
    {
        setPen(QPen(alphaColor(InsightVisualStyle::theme().accent, 90), 1.0));
        if (hoverHandler)
            hoverHandler(index);
        QGraphicsRectItem::hoverEnterEvent(event);
    }

    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override
    {
        setPen(isSelected()
                   ? QPen(alphaColor(InsightVisualStyle::theme().accent, 18), 0.6)
                   : normalPen);
        QGraphicsRectItem::hoverLeaveEvent(event);
    }

    void mousePressEvent(QGraphicsSceneMouseEvent* event) override
    {
        setSelected(true);
        setPen(QPen(alphaColor(InsightVisualStyle::theme().accent, 18), 0.6));
        if (selectHandler)
            selectHandler(index);
        if (event)
            event->accept();
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

class HotspotMatrixCellItem : public QGraphicsPathItem
{
public:
    using CellHandler =
        std::function<void(SignalUsageHotspotRole, const QString&, const QString&)>;

    HotspotMatrixCellItem(SignalUsageHotspotRole role,
                          QString moduleName,
                          QString fileName,
                          int count,
                          double intensity,
                          const QRectF& rect,
                          bool selected)
        : QGraphicsPathItem(),
          cellRole(role),
          cellModuleName(std::move(moduleName)),
          cellFileName(std::move(fileName))
    {
        QPainterPath cellPath;
        cellPath.addRoundedRect(rect, 5, 5);
        setPath(cellPath);
        setAcceptHoverEvents(true);
        setAcceptedMouseButtons(Qt::LeftButton);
        setFlag(QGraphicsItem::ItemIsSelectable, true);
        setData(kMatrixRoleRole, static_cast<int>(role));
        setData(kMatrixModuleRole, cellModuleName);
        setData(kMatrixFileRole, cellFileName);
        basePen = selected ? hotspotSelectedPen(1.8)
                           : QPen(alphaColor(InsightVisualStyle::theme().border,
                                             100),
                                  0.8);
        setPen(basePen);
        if (count > 0) {
            setBrush(QBrush(heatColorForRole(role, intensity)));
        } else {
            setPen(QPen(alphaColor(InsightVisualStyle::theme().border, 68), 0.65));
            setBrush(QBrush(QColor(QStringLiteral("#f3f6fa"))));
        }
        setZValue(selected ? 2.0 : 0.0);
    }

    CellHandler selectHandler;

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override
    {
        setPen(InsightVisualStyle::hoverPen(2.0));
        QGraphicsPathItem::hoverEnterEvent(event);
    }

    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override
    {
        setPen(basePen);
        QGraphicsPathItem::hoverLeaveEvent(event);
    }

    void mousePressEvent(QGraphicsSceneMouseEvent* event) override
    {
        setSelected(true);
        if (selectHandler)
            selectHandler(cellRole, cellModuleName, cellFileName);
        if (event)
            event->accept();
    }

private:
    SignalUsageHotspotRole cellRole = SignalUsageHotspotRole::Unknown;
    QString cellModuleName;
    QString cellFileName;
    QPen basePen;
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
    titleLabel->setVisible(false);
    rootLayout->addWidget(titleLabel);

    auto* toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->setSpacing(6);
    trackModeButton = modeButton(QStringLiteral("Track"), this);
    matrixModeButton = modeButton(QStringLiteral("Matrix"), this);
    trackModeButton->setChecked(true);
    trackModeButton->setVisible(false);
    matrixModeButton->setVisible(false);
    searchEdit = new QLineEdit(this);
    searchEdit->setPlaceholderText(QStringLiteral("Search usage"));
    InsightVisualStyle::applySearchField(searchEdit);
    zoomOutButton = new QPushButton(this);
    zoomOutButton->setObjectName(
        QStringLiteral("signalUsageHotspotZoomOutButton"));
    zoomInButton = new QPushButton(this);
    zoomInButton->setObjectName(
        QStringLiteral("signalUsageHotspotZoomInButton"));
    fitButton = new QPushButton(this);
    fitButton->setObjectName(
        QStringLiteral("signalUsageHotspotFitButton"));
    centerCurrentButton = new QPushButton(this);
    centerCurrentButton->setObjectName(
        QStringLiteral("signalUsageHotspotCenterCurrentButton"));
    resetLayoutButton = new QPushButton(this);
    resetLayoutButton->setObjectName(
        QStringLiteral("signalUsageHotspotResetLayoutButton"));
    fitViewAction = createGraphViewAction(
        QString::fromLatin1(
            ActionIds::GraphViewFit));
    zoomInViewAction = createGraphViewAction(
        QString::fromLatin1(
            ActionIds::GraphViewZoomIn));
    zoomOutViewAction = createGraphViewAction(
        QString::fromLatin1(
            ActionIds::GraphViewZoomOut));
    centerCurrentViewAction = createGraphViewAction(
        QString::fromLatin1(
            ActionIds::GraphViewCenterCurrent));
    resetLayoutViewAction = createGraphViewAction(
        QString::fromLatin1(
            ActionIds::GraphViewResetLayout));
    bindGraphViewButton(fitButton, fitViewAction);
    bindGraphViewButton(zoomInButton, zoomInViewAction);
    bindGraphViewButton(zoomOutButton, zoomOutViewAction);
    bindGraphViewButton(
        centerCurrentButton,
        centerCurrentViewAction);
    bindGraphViewButton(
        resetLayoutButton,
        resetLayoutViewAction);
    refreshGraphViewActionAvailability();
    auto* exportButton = new QToolButton(this);
    exportButton->setObjectName(
        QStringLiteral("signalUsageHotspotExportButton"));
    exportButton->setText(QStringLiteral("Export"));
    exportButton->setPopupMode(QToolButton::InstantPopup);
    auto* exportMenu = new QMenu(exportButton);
    exportTrackAction = GraphExportUi::bindRegistryAction(
        this,
        QString::fromLatin1(
            ActionIds::GraphExportUsageHotspotTrack),
        [this]() {
            return currentReport.found
                && !currentReport.items.isEmpty()
                && trackScene
                && !trackScene->items().isEmpty();
        },
        [this](const QString& outputPath,
               const GraphExportOptions& options) {
            return exportGraph(
                SignalUsageHotspotExportSurface::Track,
                outputPath,
                options);
        },
        [this](const QString& message, int timeoutMs) {
            showStatusMessage(message, timeoutMs);
        });
    exportMatrixAction = GraphExportUi::bindRegistryAction(
        this,
        QString::fromLatin1(
            ActionIds::GraphExportUsageHotspotMatrix),
        [this]() {
            return currentReport.found
                && !currentReport.items.isEmpty()
                && matrixScene
                && !matrixScene->items().isEmpty();
        },
        [this](const QString& outputPath,
               const GraphExportOptions& options) {
            return exportGraph(
                SignalUsageHotspotExportSurface::Matrix,
                outputPath,
                options);
        },
        [this](const QString& message, int timeoutMs) {
            showStatusMessage(message, timeoutMs);
        });
    if (exportTrackAction)
        exportMenu->addAction(exportTrackAction);
    if (exportMatrixAction)
        exportMenu->addAction(exportMatrixAction);
    exportButton->setMenu(exportMenu);
    for (QPushButton* button : {zoomOutButton,
                                zoomInButton,
                                fitButton,
                                centerCurrentButton,
                                resetLayoutButton}) {
        InsightVisualStyle::applyToolbarButton(button);
    }
    searchEdit->setVisible(false);
    zoomOutButton->setVisible(false);
    zoomInButton->setVisible(false);
    fitButton->setVisible(false);
    centerCurrentButton->setVisible(false);
    resetLayoutButton->setVisible(false);
    toolbar->addWidget(searchEdit, 1);
    toolbar->addWidget(zoomOutButton);
    toolbar->addWidget(zoomInButton);
    toolbar->addWidget(fitButton);
    toolbar->addWidget(centerCurrentButton);
    toolbar->addWidget(resetLayoutButton);
    toolbar->addWidget(exportButton);
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
        check->setVisible(false);
        roleChecks.append(check);
        roleLayout->addWidget(check);
    }
    roleLayout->addStretch(1);
    rootLayout->addLayout(roleLayout);

    contentSplitter = new QSplitter(Qt::Horizontal, this);
    contentSplitter->setChildrenCollapsible(false);

    modeStack = new QStackedWidget(contentSplitter);
    trackScene = new QGraphicsScene(modeStack);
    trackView = new InsightGraphView(trackScene, modeStack);
    trackView->setObjectName(QStringLiteral("signalUsageHotspotTrackView"));
    trackView->applyInsightGraphStyle();
    trackView->setZoomRange(kMinTrackZoom, kMaxTrackZoom);
    trackView->setZoomChangedHandler([this](qreal zoom) {
        trackZoomFactor = std::clamp(zoom, kMinTrackZoom, kMaxTrackZoom);
        saveLayout();
    });
    trackView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    trackView->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    trackView->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    trackView->setContextMenuPolicy(Qt::CustomContextMenu);
    modeStack->addWidget(trackView);

    auto* matrixPage = new QWidget(contentSplitter);
    auto* matrixLayout = new QHBoxLayout(matrixPage);
    matrixLayout->setContentsMargins(0, 0, 0, 0);
    matrixLayout->setSpacing(0);
    matrixScene = new QGraphicsScene(matrixPage);
    matrixView = new InsightGraphView(matrixScene, matrixPage);
    matrixView->setObjectName(QStringLiteral("signalUsageHotspotMatrixView"));
    matrixView->applyInsightGraphStyle();
    matrixView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    matrixView->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    matrixView->setAlignment(Qt::AlignLeft | Qt::AlignTop);
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
    matrixItemsTree->setVisible(false);

    auto* inspector = new QWidget(matrixPage);
    inspector->setObjectName(QStringLiteral("signalUsageHotspotInspector"));
    inspector->setMinimumWidth(250);
    inspector->setMaximumWidth(300);
    inspector->setStyleSheet(
        QStringLiteral(
            "#signalUsageHotspotInspector { background: transparent; "
            "border-left: 1px solid %1; border-radius: 0; }")
            .arg(alphaColor(InsightVisualStyle::theme().border, 80).name(
                QColor::HexArgb)));
    auto* inspectorLayout = new QVBoxLayout(inspector);
    inspectorLayout->setContentsMargins(10, 90, 8, 10);
    inspectorLayout->setSpacing(8);
    inspectorTitleLabel = new QLabel(QStringLiteral("Cell Details"), inspector);
    inspectorTitleLabel->setFont(
        InsightVisualStyle::titleFont(inspectorTitleLabel->font()));
    inspectorDetailLabel = new QLabel(
        QStringLiteral("Select a matrix cell to see hits."), inspector);
    inspectorDetailLabel->setTextFormat(Qt::RichText);
    inspectorDetailLabel->setWordWrap(true);
    inspectorDetailLabel->setTextInteractionFlags(Qt::TextSelectableByMouse
                                                  | Qt::LinksAccessibleByMouse);
    inspectorDetailLabel->setOpenExternalLinks(false);
    inspectorDetailLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    inspectorLayout->addWidget(inspectorTitleLabel);
    inspectorLayout->addWidget(inspectorDetailLabel, 1);
    matrixLayout->addWidget(matrixView, 1);
    matrixLayout->addWidget(inspector, 0);
    inspector->setVisible(false);
    matrixItemsTree->setParent(matrixPage);
    contentSplitter->addWidget(modeStack);
    contentSplitter->addWidget(matrixPage);
    matrixPage->setVisible(true);
    contentSplitter->setStretchFactor(0, 6);
    contentSplitter->setStretchFactor(1, 7);
    rootLayout->addWidget(contentSplitter, 1);
    restoreLayout();

    connect(trackModeButton, &QPushButton::clicked, this, [this]() {
        setMode(false);
    });
    connect(matrixModeButton, &QPushButton::clicked, this, [this]() {
        setMode(true);
    });
    connect(contentSplitter, &QSplitter::splitterMoved, this, [this]() {
        saveLayout();
    });
    connect(trackView, &QWidget::customContextMenuRequested,
            this, [this](const QPoint& pos) {
                refreshGraphViewActionAvailability();
                QMenu menu(trackView);
                if (fitViewAction)
                    menu.addAction(fitViewAction);
                if (centerCurrentViewAction)
                    menu.addAction(centerCurrentViewAction);
                menu.addSeparator();
                if (zoomInViewAction)
                    menu.addAction(zoomInViewAction);
                if (zoomOutViewAction)
                    menu.addAction(zoomOutViewAction);
                menu.addSeparator();
                if (resetLayoutViewAction)
                    menu.addAction(resetLayoutViewAction);
                menu.exec(
                    trackView->viewport()
                        ->mapToGlobal(pos));
            });
    connect(searchEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        searchText = text.trimmed();
        focusedItemIndexes.clear();
        rebuild();
    });
    for (QCheckBox* check : roleChecks) {
        connect(check, &QCheckBox::toggled, this, [this]() {
            focusedItemIndexes.clear();
            rebuild();
        });
    }
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
    connect(inspectorDetailLabel, &QLabel::linkActivated, this,
            [this](const QString& link) {
                if (!activeMatrixCellValid)
                    return;
                if (link == QStringLiteral("show-all")) {
                    showMatrixCellDetails();
                    showStatusMessage(QStringLiteral("Cell details list is current"),
                                      1500);
                    return;
                }
                if (link != QStringLiteral("jump-first"))
                    return;
                for (int index : filteredItemIndexes(false)) {
                    const SignalUsageHotspotItem& item =
                        currentReport.items.at(index);
                    const QString itemModuleName = item.moduleName.isEmpty()
                        ? QStringLiteral("<unknown>")
                        : item.moduleName;
                    const QString itemFileName = item.fileName.isEmpty()
                        ? QStringLiteral("<unknown>")
                        : item.fileName;
                    if (item.role == activeMatrixRole
                        && itemModuleName == activeMatrixModuleName
                        && itemFileName == activeMatrixFileName) {
                        navigateItem(index);
                        return;
                    }
                }
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

void SignalUsageHotspotPanel::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (!currentReport.found || currentReport.items.isEmpty())
        return;
    QTimer::singleShot(0, this, [this]() {
        if (!currentReport.found || currentReport.items.isEmpty())
            return;
        renderTrack();
        renderMatrix();
        if (activeMatrixCellValid)
            showMatrixCellDetails();
    });
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
    currentEditorFileName = fileName;
    currentEditorLine = 0;
    selectedItemIndex = -1;
    clearMatrixFocus();
    titleLabel->setText(QStringLiteral("Signal Usage Hotspot: analyzing %1")
                            .arg(signalAccessPath.isEmpty()
                                     ? symbolName
                                     : signalAccessPath));
    showInspectorMessage(QStringLiteral("Analyzing"),
                         QStringLiteral("Building usage hotspot report..."));
    currentReport = {};
    refreshGraphViewActionAvailability();

    const SignalUsageHotspotQuery query = currentQuery;
    const std::shared_ptr<const SemanticIndexSnapshot> snapshot =
        SemanticIndex::getInstance()->snapshot();
    const ReportBuilder builder = reportBuilder
        ? reportBuilder
        : ReportBuilder(buildReportFromSnapshot);
    const std::uint64_t generation = ++reportGeneration;
    ++activeReportBuilds;

    auto* watcher = new QFutureWatcher<SignalUsageHotspotReport>(this);
    connect(watcher,
            &QFutureWatcher<SignalUsageHotspotReport>::finished,
            this,
            [this, watcher, generation]() {
                SignalUsageHotspotReport report = watcher->result();
                watcher->deleteLater();
                activeReportBuilds = qMax(0, activeReportBuilds - 1);
                if (generation != reportGeneration)
                    return;
                currentReport = std::move(report);
                rebuild();
            });
    watcher->setFuture(QtConcurrent::run(
        [builder, query, snapshot]() {
            return builder(query, snapshot);
        }));
}

void SignalUsageHotspotPanel::setCurrentEditorLocation(const QString& fileName,
                                                       int line)
{
    currentEditorFileName = fileName;
    currentEditorLine = line;
    renderTrack();
    refreshGraphViewActionAvailability();
}

void SignalUsageHotspotPanel::focusFit()
{
    requestGraphViewAction(
        QString::fromLatin1(
            ActionIds::GraphViewFit));
}

void SignalUsageHotspotPanel::focusZoomIn()
{
    requestGraphViewAction(
        QString::fromLatin1(
            ActionIds::GraphViewZoomIn));
}

void SignalUsageHotspotPanel::focusZoomOut()
{
    requestGraphViewAction(
        QString::fromLatin1(
            ActionIds::GraphViewZoomOut));
}

void SignalUsageHotspotPanel::setFocusSearchText(
    const QString& text)
{
    if (searchEdit)
        searchEdit->setText(text);
}

QString SignalUsageHotspotPanel::focusSearchText() const
{
    return searchEdit ? searchEdit->text() : searchText;
}

GraphExportResult SignalUsageHotspotPanel::exportGraph(
    SignalUsageHotspotExportSurface surface,
    const QString& outputPath,
    const GraphExportOptions& options) const
{
    QGraphicsScene* scene =
        surface == SignalUsageHotspotExportSurface::Matrix
        ? matrixScene
        : trackScene;
    return GraphExportService::exportGraphicsScene(
        scene,
        outputPath,
        options);
}

QAction* SignalUsageHotspotPanel::graphExportAction(
    SignalUsageHotspotExportSurface surface) const
{
    return surface == SignalUsageHotspotExportSurface::Matrix
        ? exportMatrixAction
        : exportTrackAction;
}

void SignalUsageHotspotPanel::focusInspector()
{
    if (selectedItemIndex >= 0)
        showInspectorForItem(selectedItemIndex);
    else if (activeMatrixCellValid)
        showMatrixCellDetails();
    else
        showInspectorMessage(
            QStringLiteral("Inspector"),
            QStringLiteral(
                "Select a usage block or matrix cell to inspect it."));
    if (contentSplitter)
        contentSplitter->setFocus();
}

void SignalUsageHotspotPanel::renderReportForTest(
    const SignalUsageHotspotReport& report)
{
    ++reportGeneration;
    currentReport = report;
    selectedItemIndex = -1;
    clearMatrixFocus();
    rebuild();
}

int SignalUsageHotspotPanel::trackBlockCountForTest() const
{
    return lastTrackBlockCount;
}

int SignalUsageHotspotPanel::trackLaneCountForTest() const
{
    return lastTrackLaneCount;
}

int SignalUsageHotspotPanel::matrixNonEmptyCellCountForTest() const
{
    return lastMatrixNonEmptyCellCount;
}

int SignalUsageHotspotPanel::matrixItemCountForTest() const
{
    return matrixItemsTree ? matrixItemsTree->topLevelItemCount() : 0;
}

QList<qreal> SignalUsageHotspotPanel::trackBlockCenterXsForTest() const
{
    QList<qreal> centers;
    if (!trackScene)
        return centers;
    for (QGraphicsItem* item : trackScene->items()) {
        if (!item || !item->data(kItemIndexRole).isValid())
            continue;
        centers.append(item->sceneBoundingRect().center().x());
    }
    std::sort(centers.begin(), centers.end());
    return centers;
}

qreal SignalUsageHotspotPanel::firstTrackRailWidthForTest() const
{
    return lastTrackRailWidth;
}

qreal SignalUsageHotspotPanel::trackSceneWidthForTest() const
{
    return lastTrackSceneWidth;
}

int SignalUsageHotspotPanel::firstTrackLaneStartLineForTest() const
{
    return currentReport.trackLanes.isEmpty()
        ? 0
        : currentReport.trackLanes.first().startLine;
}

int SignalUsageHotspotPanel::firstTrackLaneEndLineForTest() const
{
    return currentReport.trackLanes.isEmpty()
        ? 0
        : currentReport.trackLanes.first().endLine;
}

bool SignalUsageHotspotPanel::selectMatrixCellForTest(
    SignalUsageHotspotRole role,
    const QString& moduleName,
    const QString& fileName)
{
    activateMatrixCell(role, moduleName, fileName, true);
    return !focusedItemIndexes.isEmpty();
}

bool SignalUsageHotspotPanel::selectUsageForTest(int itemIndex)
{
    if (itemIndex < 0 || itemIndex >= currentReport.items.size())
        return false;
    selectedItemIndex = itemIndex;
    renderTrack();
    showInspectorForItem(itemIndex);
    refreshGraphViewActionAvailability();
    return true;
}

bool SignalUsageHotspotPanel::triggerFirstUsageNavigationForTest()
{
    const QList<int> indexes = filteredItemIndexes();
    if (indexes.isEmpty())
        return false;
    navigateItem(indexes.first());
    return true;
}

void SignalUsageHotspotPanel::setReportBuilderForTest(ReportBuilder builder)
{
    reportBuilder = std::move(builder);
}

bool SignalUsageHotspotPanel::reportBuildInFlightForTest() const
{
    return activeReportBuilds > 0;
}

QString SignalUsageHotspotPanel::currentDeclarationDisplayNameForTest() const
{
    return currentReport.declarationDisplayName;
}

void SignalUsageHotspotPanel::setMode(bool matrixMode)
{
    Q_UNUSED(matrixMode);
    if (trackModeButton)
        trackModeButton->setChecked(true);
    if (matrixModeButton)
        matrixModeButton->setChecked(false);
    if (modeStack)
        modeStack->setCurrentIndex(0);
    saveLayout();
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
    GraphExportUi::updateActionAvailability(
        exportTrackAction,
        trackScene && !trackScene->items().isEmpty());
    GraphExportUi::updateActionAvailability(
        exportMatrixAction,
        matrixScene && !matrixScene->items().isEmpty());
    refreshGraphViewActionAvailability();
    if (activeMatrixCellValid)
        showMatrixCellDetails();
    else if (selectedItemIndex >= 0)
        showInspectorForItem(selectedItemIndex);
    else
        showInspectorMessage(QStringLiteral("Cell Details"),
                             QStringLiteral("Select a matrix cell to see hits."));
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
    if (matrixScene)
        matrixScene->clear();
    if (matrixItemsTree)
        matrixItemsTree->clear();
    showInspectorMessage(QStringLiteral("Unavailable"), message);
    GraphExportUi::updateActionAvailability(
        exportTrackAction,
        false);
    GraphExportUi::updateActionAvailability(
        exportMatrixAction,
        false);
    refreshGraphViewActionAvailability();
}

void SignalUsageHotspotPanel::renderTrack()
{
    if (!trackScene)
        return;

    trackScene->clear();
    lastTrackBlockCount = 0;
    lastTrackLaneCount = 0;
    lastTrackRailWidth = 0.0;
    lastTrackSceneWidth = 0.0;
    const QList<int> visibleList = filteredItemIndexes(false);
    QSet<int> visibleIndexes;
    for (int index : visibleList)
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
    QFont titleFont = InsightVisualStyle::titleFont(baseFont);
    QFont codeFont = detailFont;
    codeFont.setFamily(QStringLiteral("Consolas"));
    qreal y = kLaneTop;
    const qreal trackWidth = targetTrackRailWidth();
    lastTrackRailWidth = trackWidth;
    const qreal detailsX = kLaneLabelWidth + trackWidth + kTrackDetailsGap;
    qreal sceneWidth = detailsX + kTrackDetailsWidth + 12.0;

    addPanelHeader(trackScene,
                   QStringLiteral("Signal Usage Map - Code Track"),
                   currentReport,
                   sceneWidth,
                   baseFont);
    trackScene->addRect(0,
                        64,
                        sceneWidth,
                        kLaneTop - 64,
                        InsightVisualStyle::hairlinePen(
                            InsightVisualStyle::theme().border),
                        QBrush(InsightVisualStyle::theme().panelBackground));
    addSimpleSceneText(trackScene,
                       QStringLiteral("Modules"),
                       labelFont,
                       InsightVisualStyle::theme().textPrimary,
                       QPointF(12, 74));
    addSimpleSceneText(trackScene,
                       QStringLiteral("Code Range (lines)"),
                       labelFont,
                       InsightVisualStyle::theme().textPrimary,
                       QPointF(kLaneLabelWidth, 74));
    addSimpleSceneText(trackScene,
                       QStringLiteral("Usage Details"),
                       labelFont,
                       InsightVisualStyle::theme().textPrimary,
                       QPointF(detailsX, 74));

    int visibleLaneCount = 0;
    for (const SignalUsageHotspotTrackLane& lane : currentReport.trackLanes) {
        for (const SignalUsageHotspotTrackPosition& position : lane.positions) {
            if (visibleIndexes.contains(position.itemIndex)) {
                ++visibleLaneCount;
                break;
            }
        }
    }
    qreal trackFooterTargetY = kLaneTop
        + qMax(1, visibleLaneCount) * (visibleLaneCount <= 3 ? 78.0 : 86.0)
        + 24.0;
    if (trackView && trackView->viewport()
        && trackView->viewport()->height() > 0) {
        const qreal viewportFooterY =
            static_cast<qreal>(trackView->viewport()->height())
                / qMax(0.1, trackZoomFactor)
            - kTrackBottomBarHeight - 8.0;
        trackFooterTargetY = qMax(trackFooterTargetY, viewportFooterY);
    }
    qreal laneStep = visibleLaneCount <= 3 ? 78.0 : 86.0;
    if (visibleLaneCount > 0) {
        laneStep = std::clamp((trackFooterTargetY - kLaneTop - 28.0)
                                  / visibleLaneCount,
                              visibleLaneCount <= 3 ? 78.0 : 86.0,
                              148.0);
    }
    int laneIndex = 0;
    for (const SignalUsageHotspotTrackLane& lane : currentReport.trackLanes) {
        QList<SignalUsageHotspotTrackPosition> positions;
        for (const SignalUsageHotspotTrackPosition& position : lane.positions) {
            if (visibleIndexes.contains(position.itemIndex))
                positions.append(position);
        }
        if (positions.isEmpty())
            continue;
        ++lastTrackLaneCount;

        const int lineSpan = qMax(1, lane.endLine - lane.startLine);
        const qreal laneY = y;
        bool laneSelected = false;
        for (const SignalUsageHotspotTrackPosition& position : positions) {
            laneSelected = laneSelected
                || position.itemIndex == selectedItemIndex
                || focusedItemIndexes.contains(position.itemIndex);
        }
        QPainterPath laneBackground;
        laneBackground.addRoundedRect(QRectF(4,
                                             laneY + 2,
                                             detailsX - 12,
                                             kLaneHeight - 8),
                                      6,
                                      6);
        trackScene->addPath(
            laneBackground,
            Qt::NoPen,
            QBrush(laneSelected
                       ? alphaColor(InsightVisualStyle::theme().accent, 24)
                       : (laneIndex % 2
                                  ? InsightVisualStyle::theme().panelBackground
                                  : InsightVisualStyle::theme().panelSubtle)));
        auto* laneLabel = trackScene->addSimpleText(
            QStringLiteral("%1")
                .arg(lane.moduleName.isEmpty()
                         ? compactFileName(lane.fileName)
                         : lane.moduleName),
            labelFont);
        laneLabel->setBrush(QBrush(InsightVisualStyle::theme().textPrimary));
        laneLabel->setPos(12, laneY + 14);

        auto* countLabel = trackScene->addSimpleText(
            compactFileName(lane.fileName),
            detailFont);
        countLabel->setBrush(QBrush(InsightVisualStyle::theme().textMuted));
        countLabel->setPos(12, laneY + 39);

        QRectF rail(kLaneLabelWidth, laneY + 42, trackWidth, 1);
        QPen railPen(QColor(QStringLiteral("#111827")), 1.4);
        railPen.setCosmetic(true);
        auto* railItem = trackScene->addLine(rail.left(),
                                             rail.center().y(),
                                             rail.right(),
                                             rail.center().y(),
                                             railPen);
        railItem->setZValue(0);
        trackScene->addLine(rail.left(),
                            rail.center().y() - 7,
                            rail.left(),
                            rail.center().y() + 7,
                            railPen);
        trackScene->addLine(rail.right(),
                            rail.center().y() - 7,
                            rail.right(),
                            rail.center().y() + 7,
                            railPen);

        auto* startLabel = trackScene->addSimpleText(
            QString::number(lane.startLine),
            detailFont);
        startLabel->setBrush(QBrush(InsightVisualStyle::theme().textMuted));
        startLabel->setPos(rail.left(), laneY + 15);
        auto* endLabel = trackScene->addSimpleText(
            QString::number(lane.endLine),
            detailFont);
        endLabel->setBrush(QBrush(InsightVisualStyle::theme().textMuted));
        const QRectF endBounds = endLabel->boundingRect();
        endLabel->setPos(rail.right() - endBounds.width(), laneY + 15);

        if (EditorFileIdentity::same(lane.fileName, currentEditorFileName)
            && currentEditorLine >= lane.startLine
            && currentEditorLine <= lane.endLine) {
            const double currentRatio =
                std::clamp(static_cast<double>(
                               currentEditorLine - lane.startLine)
                               / static_cast<double>(lineSpan),
                           0.0,
                           1.0);
            const qreal cursorX = rail.left() + currentRatio * trackWidth;
            QPainterPath markerHalo;
            markerHalo.addRoundedRect(QRectF(cursorX - 14.0,
                                             laneY + 22.0,
                                             28.0,
                                             42.0),
                                      5,
                                      5);
            auto* halo = trackScene->addPath(
                markerHalo,
                QPen(alphaColor(InsightVisualStyle::theme().accent, 110), 1.2),
                QBrush(alphaColor(InsightVisualStyle::theme().accent, 24)));
            halo->setZValue(2.0);
            QPen markerPen(InsightVisualStyle::theme().accent, 1.1);
            markerPen.setCosmetic(true);
            markerPen.setStyle(Qt::DashLine);
            auto* cursorLine = trackScene->addLine(
                cursorX,
                laneY + 8.0,
                cursorX,
                laneY + kLaneHeight - 10.0,
                markerPen);
            cursorLine->setZValue(3.0);
            auto* cursorLabel = trackScene->addSimpleText(
                QStringLiteral("L%1").arg(currentEditorLine),
                detailFont);
            cursorLabel->setBrush(QBrush(InsightVisualStyle::theme().accent));
            cursorLabel->setPos(cursorX + 5.0, laneY + 8.0);
            cursorLabel->setZValue(3.0);
        }

        QList<QPair<qreal, SignalUsageHotspotTrackPosition>> orderedPositions;
        for (const SignalUsageHotspotTrackPosition& position : positions) {
            const double lineRatio =
                std::clamp(static_cast<double>(position.line - lane.startLine)
                               / static_cast<double>(lineSpan),
                           0.0,
                           1.0);
            orderedPositions.append({rail.left() + lineRatio * trackWidth,
                                     position});
        }
        std::sort(orderedPositions.begin(),
                  orderedPositions.end(),
                  [](const auto& lhs, const auto& rhs) {
                      if (!qFuzzyCompare(lhs.first, rhs.first))
                          return lhs.first < rhs.first;
                      return lhs.second.line < rhs.second.line;
                  });

        QList<QList<QPair<qreal, SignalUsageHotspotTrackPosition>>> clusters;
        for (const auto& orderedPosition : orderedPositions) {
            if (clusters.isEmpty()
                || orderedPosition.first - clusters.last().last().first > 18.0) {
                clusters.append({orderedPosition});
            } else {
                clusters.last().append(orderedPosition);
            }
        }

        qreal lastTickX = -100000.0;
        int proximityIndex = 0;
        for (const auto& cluster : clusters) {
            QList<QPair<qreal, SignalUsageHotspotTrackPosition>> representatives;
            for (const auto& orderedPosition : cluster) {
                if (orderedPosition.second.itemIndex == selectedItemIndex) {
                    representatives.prepend(orderedPosition);
                    break;
                }
            }
            if (representatives.isEmpty() && !cluster.isEmpty())
                representatives.append(cluster.first());
            for (const auto& orderedPosition : cluster) {
                if (representatives.size() >= 4)
                    break;
                bool duplicateRole = false;
                for (const auto& representative : representatives) {
                    duplicateRole = duplicateRole
                        || representative.second.role == orderedPosition.second.role;
                }
                if (!duplicateRole)
                    representatives.append(orderedPosition);
            }
            for (const auto& orderedPosition : cluster) {
                if (representatives.size() >= 5)
                    break;
                bool duplicateItem = false;
                for (const auto& representative : representatives) {
                    duplicateItem = duplicateItem
                        || representative.second.itemIndex
                               == orderedPosition.second.itemIndex;
                }
                if (!duplicateItem)
                    representatives.append(orderedPosition);
            }

            std::sort(representatives.begin(),
                      representatives.end(),
                      [](const auto& lhs, const auto& rhs) {
                          if (lhs.second.itemIndex == rhs.second.itemIndex)
                              return lhs.first < rhs.first;
                          return lhs.second.itemIndex < rhs.second.itemIndex;
                      });

            const qreal clusterCenter = cluster.isEmpty()
                ? 0.0
                : (cluster.first().first + cluster.last().first) / 2.0;
            const int representativeCount = representatives.size();
            const qreal tickSpacing = representativeCount >= 5 ? 8.0 : 8.5;
            const qreal spreadWidth =
                tickSpacing * qMax(0, representativeCount - 1);
            const qreal naturalWidth = cluster.isEmpty()
                ? spreadWidth
                : qMax(spreadWidth, cluster.last().first - cluster.first().first);
            const qreal visualWidth = qMin(naturalWidth + 14.0, 48.0);
            const qreal startX = std::clamp(clusterCenter - visualWidth / 2.0,
                                            rail.left(),
                                            qMax(rail.left(),
                                                 rail.right() - visualWidth));
            for (int representativeIndex = 0;
                 representativeIndex < representativeCount;
                 ++representativeIndex) {
            const auto& orderedPosition = representatives.at(representativeIndex);
            const SignalUsageHotspotTrackPosition& position =
                orderedPosition.second;
            qreal x = representativeCount <= 1
                ? clusterCenter
                : startX
                      + (visualWidth
                         * static_cast<qreal>(representativeIndex)
                         / static_cast<qreal>(representativeCount - 1));
            if (x - lastTickX < 6.0) {
                x = qMin(rail.right(), lastTickX + 6.0);
                ++proximityIndex;
            } else {
                proximityIndex = 0;
            }
            lastTickX = x;
            const qreal yOffset =
                (static_cast<qreal>((representativeIndex + 1) % 3) - 1.0)
                * 3.6;
            const bool selectedBlock = position.itemIndex == selectedItemIndex;
            const bool focusedBlock = !selectedBlock
                && focusedItemIndexes.contains(position.itemIndex);
            QRectF blockRect(x - 1.35, laneY + 26.0 + yOffset, 2.7, 22.0);
            auto* block =
                new HotspotUsageBlockItem(position.itemIndex,
                                          position.role,
                                          blockRect);
            block->setToolTip(
                QStringLiteral("%1 line %2\nDouble-click to jump")
                    .arg(position.roleDisplayName)
                    .arg(position.line));
            if (selectedBlock) {
                block->setSelected(true);
                block->setPen(hotspotSelectedPen());
            } else if (focusedBlock) {
                block->setOpacity(0.82);
            }
            block->hoverHandler = [this](int itemIndex) {
                showInspectorForItem(itemIndex);
            };
            block->selectHandler = [this](int itemIndex) {
                selectedItemIndex = itemIndex;
                activateMatrixCellForItem(itemIndex, false);
                showInspectorForItem(itemIndex);
            };
            block->navigateHandler = [this](int itemIndex) {
                navigateItem(itemIndex);
            };
            trackScene->addItem(block);
            }
            lastTrackBlockCount += cluster.size();
        }
        y += laneStep;
        ++laneIndex;
    }

    if (lastTrackBlockCount == 0) {
        auto* text = trackScene->addText(QStringLiteral("No track lanes match filters."));
        text->setDefaultTextColor(InsightVisualStyle::theme().textMuted);
        text->setPos(16, 16);
        trackScene->setSceneRect(text->boundingRect().adjusted(-16, -16, 260, 80));
        return;
    }

    QList<int> detailIndexes;
    if (selectedItemIndex >= 0 && visibleIndexes.contains(selectedItemIndex))
        detailIndexes.append(selectedItemIndex);

    const SignalUsageHotspotItem* selectedItem = nullptr;
    if (selectedItemIndex >= 0 && selectedItemIndex < currentReport.items.size())
        selectedItem = &currentReport.items.at(selectedItemIndex);
    if (selectedItem) {
        QList<int> sameLaneIndexes;
        for (const SignalUsageHotspotTrackLane& lane : currentReport.trackLanes) {
            if (!EditorFileIdentity::same(lane.fileName,
                                          selectedItem->fileName)
                || (!lane.moduleName.isEmpty()
                    && !selectedItem->moduleName.isEmpty()
                    && lane.moduleName != selectedItem->moduleName)) {
                continue;
            }
            for (const SignalUsageHotspotTrackPosition& position : lane.positions) {
                if (visibleIndexes.contains(position.itemIndex)
                    && !sameLaneIndexes.contains(position.itemIndex)) {
                    sameLaneIndexes.append(position.itemIndex);
                }
            }
            break;
        }
        std::sort(sameLaneIndexes.begin(),
                  sameLaneIndexes.end(),
                  [this](int lhs, int rhs) {
                      return currentReport.items.at(lhs).line
                          < currentReport.items.at(rhs).line;
                  });
        for (int index : sameLaneIndexes) {
            if (detailIndexes.size() >= 3)
                break;
            if (detailIndexes.contains(index))
                continue;
            if (currentReport.items.at(index).role == selectedItem->role)
                continue;
            detailIndexes.append(index);
        }
        for (int index : sameLaneIndexes) {
            if (detailIndexes.size() >= 3)
                break;
            if (!detailIndexes.contains(index))
                detailIndexes.append(index);
        }
    }
    for (int index : visibleList) {
        if (!detailIndexes.contains(index))
            detailIndexes.append(index);
        if (detailIndexes.size() >= 3)
            break;
    }

    qreal cardY = kLaneTop + 8.0;
    int renderedCards = 0;
    for (int index : detailIndexes) {
        if (index < 0 || index >= currentReport.items.size())
            continue;
        if (renderedCards >= 3)
            break;
        const SignalUsageHotspotItem& item = currentReport.items.at(index);
        const bool selectedCard = index == selectedItemIndex;
        QRectF cardRect(detailsX,
                        cardY,
                        kTrackDetailsWidth - 8.0,
                        58.0);
        auto* card =
            new HotspotUsageCardItem(index, item.role, cardRect, selectedCard);
        card->hoverHandler = [this](int itemIndex) {
            showInspectorForItem(itemIndex);
        };
        card->selectHandler = [this](int itemIndex) {
            selectedItemIndex = itemIndex;
            activateMatrixCellForItem(itemIndex, false);
            showInspectorForItem(itemIndex);
            renderTrack();
        };
        card->navigateHandler = [this](int itemIndex) {
            navigateItem(itemIndex);
        };
        trackScene->addItem(card);

        const QColor roleColor = hotspotRoleColor(item.role);
        trackScene->addRect(detailsX,
                            cardY + 4,
                            4,
                            50,
                            Qt::NoPen,
                            QBrush(roleColor));
        addRoundedSceneRect(trackScene,
                            QRectF(detailsX + 48, cardY + 8, 24, 16),
                            4,
                            Qt::NoPen,
                            QBrush(alphaColor(roleColor, 34)),
                            3.0);
        addSimpleSceneText(trackScene,
                           roleBadgeText(item.role),
                           labelFont,
                           roleColor.darker(120),
                           QPointF(detailsX + 55, cardY + 8),
                           3.0);
        QFont lineFont = labelFont;
        lineFont.setPointSize(qMax(10, lineFont.pointSize() + 2));
        addSimpleSceneText(trackScene,
                           QString::number(item.line),
                           lineFont,
                           InsightVisualStyle::theme().textPrimary,
                           QPointF(detailsX + 12, cardY + 5),
                           3.0);
        const QString reason = item.roleReasonDisplayName.isEmpty()
            ? SignalUsageHotspotService::roleDisplayName(item.role)
            : item.roleReasonDisplayName;
        addSimpleSceneText(trackScene,
                           elidedForWidth(reason, labelFont, 130),
                           labelFont,
                           roleColor.darker(112),
                           QPointF(detailsX + 82, cardY + 7),
                           3.0);
        addSimpleSceneText(trackScene,
                           elidedForWidth(item.snippet, codeFont, 188),
                           codeFont,
                           alphaColor(InsightVisualStyle::theme().textPrimary, 215),
                           QPointF(detailsX + 12, cardY + 30),
                           3.0);
        addSimpleSceneText(trackScene,
                           compactFileName(item.fileName),
                           detailFont,
                           alphaColor(InsightVisualStyle::theme().textMuted, 185),
                           QPointF(detailsX + 12, cardY + 45),
                           3.0);
        cardY += 64.0;
        ++renderedCards;
    }
    if (visibleList.size() > renderedCards) {
        addSimpleSceneText(
            trackScene,
            QStringLiteral("Show all %1 usages in Matrix")
                .arg(visibleList.size()),
            detailFont,
            InsightVisualStyle::theme().accent,
            QPointF(detailsX + 10, cardY + 4),
            3.0);
    }

    qreal footerY = qMax(qMax(y + 24.0, trackFooterTargetY),
                         kLaneTop + 3 * 64.0 + 20.0);
    trackScene->addRect(0,
                        footerY,
                        sceneWidth,
                        kTrackBottomBarHeight,
                        InsightVisualStyle::hairlinePen(
                            InsightVisualStyle::theme().border),
                        QBrush(InsightVisualStyle::theme().panelBackground));
    QFont footerFont = detailFont;
    footerFont.setPointSize(qMax(7, footerFont.pointSize() - 1));
    qreal legendX = 12.0;
    const QList<QPair<SignalUsageHotspotRole, QString>> legend = {
        {SignalUsageHotspotRole::Write, QStringLiteral("Write (driver)")},
        {SignalUsageHotspotRole::Read, QStringLiteral("Read (consumer)")},
        {SignalUsageHotspotRole::Port, QStringLiteral("Port Connection")},
        {SignalUsageHotspotRole::Condition, QStringLiteral("Condition / Case")}};
    for (const auto& entry : legend) {
        const QColor color = hotspotRoleColor(entry.first);
        trackScene->addRect(legendX,
                            footerY + 14,
                            9,
                            9,
                            Qt::NoPen,
                            QBrush(color));
        addSimpleSceneText(trackScene,
                           entry.second,
                           footerFont,
                           InsightVisualStyle::theme().textSecondary,
                           QPointF(legendX + 14, footerY + 10),
                           2.0);
        legendX += QFontMetrics(footerFont).horizontalAdvance(entry.second) + 32;
    }
    trackScene->addLine(legendX - 12,
                        footerY + 9,
                        legendX - 12,
                        footerY + 43,
                        InsightVisualStyle::hairlinePen(
                            InsightVisualStyle::theme().border));
    addSimpleSceneText(trackScene,
                       QStringLiteral("Click a tick to see details   |   Double-click to jump to code"),
                       footerFont,
                       InsightVisualStyle::theme().textMuted,
                       QPointF(12, footerY + 34),
                       2.0);
    const qreal zoomX = sceneWidth - 178.0;
    addSimpleSceneText(trackScene,
                       QStringLiteral("-"),
                       titleFont,
                       InsightVisualStyle::theme().textSecondary,
                       QPointF(zoomX, footerY + 20),
                       2.0);
    trackScene->addLine(zoomX + 28,
                        footerY + 29,
                        zoomX + 96,
                        footerY + 29,
                        InsightVisualStyle::hairlinePen(
                            InsightVisualStyle::theme().borderStrong));
    trackScene->addEllipse(QRectF(zoomX + 58, footerY + 23, 12, 12),
                           Qt::NoPen,
                           QBrush(InsightVisualStyle::theme().accent));
    addSimpleSceneText(trackScene,
                       QStringLiteral("+"),
                       titleFont,
                       InsightVisualStyle::theme().textSecondary,
                       QPointF(zoomX + 112, footerY + 20),
                       2.0);
    addRoundedSceneRect(trackScene,
                        QRectF(zoomX + 138, footerY + 16, 34, 24),
                        4,
                        InsightVisualStyle::hairlinePen(
                            InsightVisualStyle::theme().border),
                        QBrush(InsightVisualStyle::theme().panelSubtle),
                        2.0);
    addSimpleSceneText(trackScene,
                       QStringLiteral("Fit"),
                       footerFont,
                       InsightVisualStyle::theme().textSecondary,
                       QPointF(zoomX + 148, footerY + 20),
                       2.0);

    lastTrackSceneWidth = sceneWidth;
    trackScene->setSceneRect(0, 0, sceneWidth, footerY + kTrackBottomBarHeight);
    setTrackZoom(trackZoomFactor);
}

void SignalUsageHotspotPanel::renderMatrix()
{
    if (!matrixScene)
        return;

    matrixScene->clear();

    QList<int> visibleIndexes = filteredItemIndexes(false);
    QList<QString> rows;
    QHash<QString, QString> rowModuleByKey;
    QHash<QString, QString> rowFileByKey;
    QHash<QString, int> countsByKey;
    int maxCount = 1;
    for (const SignalUsageHotspotTrackLane& lane : currentReport.trackLanes) {
        const QString moduleName = lane.moduleName.isEmpty()
            ? QStringLiteral("<unknown>")
            : lane.moduleName;
        const QString fileName = lane.fileName.isEmpty()
            ? QStringLiteral("<unknown>")
            : lane.fileName;
        const QString rowKey = matrixRowKey(moduleName, fileName);
        if (!rows.contains(rowKey)) {
            rows.append(rowKey);
            rowModuleByKey.insert(rowKey, moduleName);
            rowFileByKey.insert(rowKey, fileName);
        }
    }
    if (!currentReport.matrixCells.isEmpty()) {
        for (const SignalUsageHotspotMatrixCell& cell : currentReport.matrixCells) {
            const QString moduleName = cell.moduleName.isEmpty()
                ? QStringLiteral("<unknown>")
                : cell.moduleName;
            const QString fileName = cell.fileName.isEmpty()
                ? QStringLiteral("<unknown>")
                : cell.fileName;
            const QString rowKey = matrixRowKey(moduleName, fileName);
            if (!rows.contains(rowKey)) {
                rows.append(rowKey);
                rowModuleByKey.insert(rowKey, moduleName);
                rowFileByKey.insert(rowKey, fileName);
            }
            const QString countKey =
                rowKey + QLatin1Char('\n')
                + QString::number(static_cast<int>(cell.role));
            countsByKey.insert(countKey, cell.count);
            maxCount = qMax(maxCount, cell.count);
        }
    } else {
        for (int index : visibleIndexes) {
        const SignalUsageHotspotItem& usage = currentReport.items.at(index);
        const QString moduleName = usage.moduleName.isEmpty()
            ? QStringLiteral("<unknown>")
            : usage.moduleName;
        const QString fileName = usage.fileName.isEmpty()
            ? QStringLiteral("<unknown>")
            : usage.fileName;
        const QString rowKey = matrixRowKey(moduleName, fileName);
        if (!rows.contains(rowKey)) {
            rows.append(rowKey);
            rowModuleByKey.insert(rowKey, moduleName);
            rowFileByKey.insert(rowKey, fileName);
        }
        const QString countKey =
            rowKey + QLatin1Char('\n')
            + QString::number(static_cast<int>(usage.role));
        const int count = countsByKey.value(countKey) + 1;
        countsByKey.insert(countKey, count);
        maxCount = qMax(maxCount, count);
        }
    }
    lastMatrixNonEmptyCellCount = 0;

    QFont baseFont = font();
    QFont labelFont = InsightVisualStyle::labelFont(baseFont);
    QFont detailFont = InsightVisualStyle::compactFont(baseFont);
    QFont countFont = labelFont;
    countFont.setPointSize(qMax(9, countFont.pointSize() + 1));
    const QPen softGridPen(alphaColor(InsightVisualStyle::theme().border, 52),
                           0.7);
    const qreal tableWidth =
        kMatrixRowHeaderWidth + hotspotRoles().size() * kMatrixColumnWidth;
    constexpr qreal inspectorWidth = 282.0;
    const qreal sceneWidth = tableWidth + inspectorWidth;
    constexpr qreal tableHeaderY = 64.0;
    constexpr qreal matrixFooterHeight = 44.0;

    addPanelHeader(matrixScene,
                   QStringLiteral("Signal Usage Matrix - Module x Role"),
                   currentReport,
                   sceneWidth,
                   baseFont);

    auto* corner = matrixScene->addRect(
        0,
        tableHeaderY,
        kMatrixRowHeaderWidth,
        kMatrixHeaderHeight - tableHeaderY,
        softGridPen,
        QBrush(InsightVisualStyle::theme().panelBackground));
    corner->setZValue(-1);
    auto* cornerText =
        matrixScene->addSimpleText(QStringLiteral("Module / File"), labelFont);
    cornerText->setBrush(QBrush(InsightVisualStyle::theme().textPrimary));
    cornerText->setPos(10, tableHeaderY + 10);

    for (int column = 0; column < hotspotRoles().size(); ++column) {
        const SignalUsageHotspotRole role = hotspotRoles().at(column);
        const qreal x = kMatrixRowHeaderWidth + column * kMatrixColumnWidth;
        matrixScene->addRect(
            x,
            tableHeaderY,
            kMatrixColumnWidth,
            kMatrixHeaderHeight - tableHeaderY,
            softGridPen,
            QBrush(InsightVisualStyle::theme().panelBackground));
        auto* text = matrixScene->addSimpleText(
            matrixColumnDisplayName(role),
            labelFont);
        text->setBrush(QBrush(InsightVisualStyle::theme().textPrimary));
        const QRectF textBounds = text->boundingRect();
        text->setPos(x + (kMatrixColumnWidth - textBounds.width()) / 2.0,
                     tableHeaderY + 10);
    }

    qreal matrixFooterTargetY = kMatrixHeaderHeight
        + qMax(1, rows.size()) * kMatrixRowHeight + 14.0;
    if (matrixView && matrixView->viewport()
        && matrixView->viewport()->height() > 0) {
        matrixFooterTargetY =
            qMax(matrixFooterTargetY,
                 static_cast<qreal>(matrixView->viewport()->height())
                     - matrixFooterHeight - 8.0);
    }
    const qreal matrixRowStep = rows.isEmpty()
        ? kMatrixRowHeight
        : std::clamp((matrixFooterTargetY - kMatrixHeaderHeight - 14.0)
                         / rows.size(),
                     kMatrixRowHeight,
                     116.0);
    qreal footerY = qMax(kMatrixHeaderHeight
                             + qMax(1, rows.size()) * matrixRowStep
                             + 14.0,
                         matrixFooterTargetY);

    for (int row = 0; row < rows.size(); ++row) {
        const qreal y = kMatrixHeaderHeight + row * matrixRowStep;
        const QString rowKey = rows.at(row);
        const QString moduleName = rowModuleByKey.value(rowKey);
        const QString fileName = rowFileByKey.value(rowKey);
        const bool rowSelected = activeMatrixCellValid
            && moduleName == activeMatrixModuleName
            && fileName == activeMatrixFileName;
        matrixScene->addRect(
            0,
            y,
            kMatrixRowHeaderWidth,
            matrixRowStep,
            rowSelected ? hotspotSelectedPen(1.2)
                        : softGridPen,
            QBrush(rowSelected
                       ? alphaColor(InsightVisualStyle::theme().accent, 18)
                       : InsightVisualStyle::theme().panelBackground));
        if (rowSelected) {
            auto* rowFrame = matrixScene->addRect(
                1,
                y + 1,
                tableWidth - 2,
                matrixRowStep - 2,
                hotspotSelectedPen(1.5),
                Qt::NoBrush);
            rowFrame->setZValue(2.5);
        }
        auto* moduleText = matrixScene->addSimpleText(moduleName, labelFont);
        moduleText->setBrush(QBrush(InsightVisualStyle::theme().textPrimary));
        moduleText->setPos(10, y + qMax(15.0, matrixRowStep * 0.28));
        auto* fileText = matrixScene->addSimpleText(compactFileName(fileName),
                                                    detailFont);
        fileText->setBrush(QBrush(InsightVisualStyle::theme().textMuted));
        fileText->setPos(10, y + qMax(38.0, matrixRowStep * 0.52));

        for (int column = 0; column < hotspotRoles().size(); ++column) {
            const SignalUsageHotspotRole role = hotspotRoles().at(column);
            const QString key =
                rowKey + QLatin1Char('\n')
                + QString::number(static_cast<int>(role));
            const int count = countsByKey.value(key, 0);
            const qreal x = kMatrixRowHeaderWidth + column * kMatrixColumnWidth;
            matrixScene->addRect(
                x,
                y,
                kMatrixColumnWidth,
                matrixRowStep,
                softGridPen,
                QBrush(InsightVisualStyle::theme().panelBackground));
            const bool selected = activeMatrixCellValid
                && role == activeMatrixRole
                && moduleName == activeMatrixModuleName
                && fileName == activeMatrixFileName;
            constexpr qreal cellWidth = 54.0;
            constexpr qreal cellHeight = 50.0;
            const QRectF cellRect(x + (kMatrixColumnWidth - cellWidth) / 2.0,
                                  y + (matrixRowStep - cellHeight) / 2.0,
                                  cellWidth,
                                  cellHeight);
            const double intensity =
                count > 0
                    ? static_cast<double>(count) / static_cast<double>(maxCount)
                    : 0.0;
            auto* cell = new HotspotMatrixCellItem(role,
                                                   moduleName,
                                                   fileName,
                                                   count,
                                                   intensity,
                                                   cellRect,
                                                   selected);
            cell->setToolTip(
                QStringLiteral("%1 / %2\n%3: %4 usage(s)\nClick for details")
                    .arg(moduleName,
                         compactFileName(fileName),
                         matrixColumnDisplayName(role))
                    .arg(count));
            cell->selectHandler =
                [this](SignalUsageHotspotRole selectedRole,
                       const QString& selectedModule,
                       const QString& selectedFile) {
                    activateMatrixCell(selectedRole,
                                       selectedModule,
                                       selectedFile,
                                       true);
                };
            matrixScene->addItem(cell);
            auto* countText = matrixScene->addSimpleText(QString::number(count),
                                                         countFont);
            countText->setBrush(QBrush(
                count > 0
                    ? (selected || intensity > 0.7
                           ? QColor(Qt::white)
                           : InsightVisualStyle::theme().textPrimary)
                    : alphaColor(InsightVisualStyle::theme().textMuted, 165)));
            const QRectF countBounds = countText->boundingRect();
            countText->setPos(cellRect.center().x() - countBounds.width() / 2.0,
                              cellRect.center().y() - countBounds.height() / 2.0);
            countText->setZValue(3.0);
            countText->setAcceptedMouseButtons(Qt::NoButton);
            if (count > 0)
                ++lastMatrixNonEmptyCellCount;
        }
    }
    if (rows.isEmpty()) {
        auto* text = matrixScene->addText(QStringLiteral("No matrix cells match filters."));
        text->setDefaultTextColor(InsightVisualStyle::theme().textMuted);
        text->setPos(12, kMatrixHeaderHeight + 14);
    }
    matrixScene->addRect(0,
                         footerY,
                         tableWidth,
                         matrixFooterHeight,
                         InsightVisualStyle::hairlinePen(
                             InsightVisualStyle::theme().border),
                         QBrush(InsightVisualStyle::theme().panelBackground));
    addSimpleSceneText(matrixScene,
                       QStringLiteral("Hits"),
                       detailFont,
                       InsightVisualStyle::theme().textSecondary,
                       QPointF(10, footerY + 15),
                       2.0);
    const QStringList buckets = {QStringLiteral("0"),
                                 QStringLiteral("1"),
                                 QStringLiteral("2-3"),
                                 QStringLiteral("4-7"),
                                 QStringLiteral("8-15"),
                                 QStringLiteral("16+")};
    qreal bucketX = 44.0;
    for (int i = 0; i < buckets.size(); ++i) {
        const double intensity = static_cast<double>(i)
            / static_cast<double>(qMax(1, buckets.size() - 1));
        const QColor fill = i == 0
            ? InsightVisualStyle::theme().panelSubtle
            : mixedColor(InsightVisualStyle::theme().panelSubtle,
                         InsightVisualStyle::theme().accent,
                         0.18 + intensity * 0.72);
        addRoundedSceneRect(matrixScene,
                            QRectF(bucketX, footerY + 10, 36, 24),
                            3,
                            Qt::NoPen,
                            QBrush(fill),
                            1.0);
        addSimpleSceneText(matrixScene,
                           buckets.at(i),
                           detailFont,
                           i > 3 ? QColor(Qt::white)
                                 : InsightVisualStyle::theme().textPrimary,
                           QPointF(bucketX + 9, footerY + 14),
                           2.0);
        bucketX += 40.0;
    }
    addSimpleSceneText(matrixScene,
                       QStringLiteral("Click a cell or row to see details"),
                       detailFont,
                       InsightVisualStyle::theme().textMuted,
                       QPointF(tableWidth - 190.0, footerY + 15),
                       2.0);
    const qreal inspectorX = tableWidth;
    matrixScene->addRect(inspectorX,
                         tableHeaderY,
                         inspectorWidth,
                         footerY + matrixFooterHeight - tableHeaderY,
                         Qt::NoPen,
                         QBrush(InsightVisualStyle::theme().panelBackground));
    matrixScene->addLine(inspectorX,
                         tableHeaderY,
                         inspectorX,
                         footerY + matrixFooterHeight,
                         QPen(alphaColor(InsightVisualStyle::theme().border, 24),
                              0.6));
    addSimpleSceneText(matrixScene,
                       QStringLiteral("Cell Details"),
                       labelFont,
                       InsightVisualStyle::theme().textPrimary,
                       QPointF(inspectorX + 14, tableHeaderY + 10),
                       2.0);
    matrixScene->addLine(inspectorX + 12,
                         tableHeaderY + 32,
                         inspectorX + inspectorWidth - 12,
                         tableHeaderY + 32,
                         QPen(alphaColor(InsightVisualStyle::theme().border, 52),
                              0.6));
    QPen closePen(alphaColor(InsightVisualStyle::theme().textSecondary, 155), 1.1);
    closePen.setCosmetic(true);
    matrixScene->addLine(inspectorX + inspectorWidth - 24,
                         tableHeaderY + 12,
                         inspectorX + inspectorWidth - 14,
                         tableHeaderY + 22,
                         closePen);
    matrixScene->addLine(inspectorX + inspectorWidth - 14,
                         tableHeaderY + 12,
                         inspectorX + inspectorWidth - 24,
                         tableHeaderY + 22,
                         closePen);

    QList<int> inspectorMatches;
    if (activeMatrixCellValid) {
        for (int index : filteredItemIndexes(false)) {
            const SignalUsageHotspotItem& item = currentReport.items.at(index);
            const QString itemModuleName = item.moduleName.isEmpty()
                ? QStringLiteral("<unknown>")
                : item.moduleName;
            const QString itemFileName = item.fileName.isEmpty()
                ? QStringLiteral("<unknown>")
                : item.fileName;
            if (item.role == activeMatrixRole
                && itemModuleName == activeMatrixModuleName
                && itemFileName == activeMatrixFileName) {
                inspectorMatches.append(index);
            }
        }
    }
    const qreal detailX = inspectorX + 14.0;
    qreal detailY = kMatrixHeaderHeight + 14.0;
    if (activeMatrixCellValid) {
        const QColor roleColor = hotspotRoleColor(activeMatrixRole);
        const QString roleLegendText = [this]() {
            switch (activeMatrixRole) {
            case SignalUsageHotspotRole::Write:
                return QStringLiteral("Write (driver)");
            case SignalUsageHotspotRole::Read:
                return QStringLiteral("Read (consumer)");
            case SignalUsageHotspotRole::Port:
                return QStringLiteral("Port Connection");
            case SignalUsageHotspotRole::Condition:
            case SignalUsageHotspotRole::Case:
                return QStringLiteral("Condition / Case");
            case SignalUsageHotspotRole::Timing:
                return QStringLiteral("Always / Timing");
            case SignalUsageHotspotRole::Unknown:
                return QStringLiteral("Assign / Unknown");
            }
            return QStringLiteral("Usage");
        }();
        addRoundedSceneRect(matrixScene,
                            QRectF(detailX - 2,
                                   detailY - 7,
                                   inspectorWidth - 26,
                                   58),
                            5,
                            QPen(alphaColor(InsightVisualStyle::theme().border, 38),
                                 0.6),
                            QBrush(alphaColor(InsightVisualStyle::theme().panelSubtle,
                                               160)),
                            1.0);
        addSimpleSceneText(matrixScene,
                           QStringLiteral("%1 / %2 / %3 hits")
                               .arg(activeMatrixModuleName,
                                    matrixColumnDisplayName(activeMatrixRole))
                               .arg(inspectorMatches.size()),
                           labelFont,
                           InsightVisualStyle::theme().textPrimary,
                           QPointF(detailX, detailY),
                           2.0);
        matrixScene->addRect(detailX,
                             detailY + 26,
                             9,
                             9,
                             Qt::NoPen,
                             QBrush(roleColor));
        addSimpleSceneText(matrixScene,
                           roleLegendText,
                           detailFont,
                           InsightVisualStyle::theme().textSecondary,
                           QPointF(detailX + 15, detailY + 22),
                           3.0);
        detailY += 39.0;
        addSimpleSceneText(matrixScene,
                           compactFileName(activeMatrixFileName),
                           detailFont,
                           InsightVisualStyle::theme().textMuted,
                           QPointF(detailX, detailY),
                           2.0);
        detailY += 24.0;
        const int rowLimit = qMin(inspectorMatches.size(), 8);
        addRoundedSceneRect(matrixScene,
                            QRectF(detailX - 2,
                                   detailY - 5,
                                   inspectorWidth - 26,
                                   21),
                            3,
                            Qt::NoPen,
                            QBrush(alphaColor(InsightVisualStyle::theme().panelSubtle,
                                               185)),
                            1.0);
        addSimpleSceneText(matrixScene,
                           QStringLiteral("Line"),
                           detailFont,
                           InsightVisualStyle::theme().textSecondary,
                           QPointF(detailX, detailY),
                           2.0);
        addSimpleSceneText(matrixScene,
                           QStringLiteral("Code preview"),
                           detailFont,
                           InsightVisualStyle::theme().textSecondary,
                           QPointF(detailX + 48, detailY),
                           2.0);
        matrixScene->addLine(detailX + 40,
                             detailY - 5,
                             detailX + 40,
                             detailY + 21 + rowLimit * 24,
                             QPen(alphaColor(InsightVisualStyle::theme().border, 38),
                                  0.6));
        detailY += 22.0;
        QFont codeFont = detailFont;
        codeFont.setFamily(QStringLiteral("Consolas"));
        for (int i = 0; i < rowLimit; ++i) {
            const SignalUsageHotspotItem& item =
                currentReport.items.at(inspectorMatches.at(i));
            matrixScene->addLine(detailX,
                                 detailY + 18,
                                 inspectorX + inspectorWidth - 14,
                                 detailY + 18,
                                 QPen(alphaColor(InsightVisualStyle::theme().border, 50), 0.6));
            addSimpleSceneText(matrixScene,
                               QString::number(item.line),
                               detailFont,
                               InsightVisualStyle::theme().textPrimary,
                               QPointF(detailX, detailY),
                               2.0);
            addSimpleSceneText(matrixScene,
                               elidedForWidth(item.snippet, codeFont, 190),
                               codeFont,
                               InsightVisualStyle::theme().textPrimary,
                               QPointF(detailX + 48, detailY),
                               2.0);
            detailY += 24.0;
        }
        matrixScene->addLine(detailX,
                             detailY - 4,
                             inspectorX + inspectorWidth - 14,
                             detailY - 4,
                             QPen(alphaColor(InsightVisualStyle::theme().border, 72),
                                  0.7));
        if (inspectorMatches.size() > rowLimit) {
            addSimpleSceneText(matrixScene,
                               QStringLiteral("...    %1 more hits")
                                   .arg(inspectorMatches.size() - rowLimit),
                               detailFont,
                               InsightVisualStyle::theme().textMuted,
                               QPointF(detailX, detailY),
                               2.0);
            detailY += 28.0;
        }
        QPen commandPen(InsightVisualStyle::theme().accent, 1.0);
        commandPen.setCosmetic(true);
        matrixScene->addLine(detailX,
                             detailY + 12,
                             detailX + 6,
                             detailY + 8,
                             commandPen);
        matrixScene->addLine(detailX,
                             detailY + 12,
                             detailX + 6,
                             detailY + 16,
                             commandPen);
        addSimpleSceneText(matrixScene,
                           QStringLiteral("Show all %1 hits in list")
                               .arg(inspectorMatches.size()),
                           detailFont,
                           InsightVisualStyle::theme().accent,
                           QPointF(detailX + 14, detailY + 5),
                           2.0);
        int firstLine = -1;
        if (!inspectorMatches.isEmpty())
            firstLine = currentReport.items.at(inspectorMatches.first()).line;
        matrixScene->addLine(detailX,
                             detailY + 38,
                             detailX + 6,
                             detailY + 34,
                             commandPen);
        matrixScene->addLine(detailX,
                             detailY + 38,
                             detailX + 6,
                             detailY + 42,
                             commandPen);
        addSimpleSceneText(matrixScene,
                           firstLine > 0
                               ? QStringLiteral("Jump to first hit (line %1)")
                                     .arg(firstLine)
                               : QStringLiteral("Jump to first hit"),
                           detailFont,
                           InsightVisualStyle::theme().accent,
                           QPointF(detailX + 14, detailY + 31),
                           2.0);
    } else {
        addRoundedSceneRect(matrixScene,
                            QRectF(detailX - 2,
                                   detailY - 7,
                                   inspectorWidth - 26,
                                   48),
                            5,
                            QPen(alphaColor(InsightVisualStyle::theme().border, 32),
                                 0.6),
                            QBrush(alphaColor(InsightVisualStyle::theme().panelSubtle,
                                               130)),
                            1.0);
        addSimpleSceneText(matrixScene,
                           QStringLiteral("No cell selected"),
                           labelFont,
                           InsightVisualStyle::theme().textMuted,
                           QPointF(detailX, detailY),
                           2.0);
        addSimpleSceneText(matrixScene,
                           QStringLiteral("Cell hits appear here."),
                           detailFont,
                           InsightVisualStyle::theme().textMuted,
                           QPointF(detailX, detailY + 22),
                           2.0);
        detailY += 66.0;
        addRoundedSceneRect(matrixScene,
                            QRectF(detailX - 2,
                                   detailY - 5,
                                   inspectorWidth - 26,
                                   21),
                            3,
                            Qt::NoPen,
                            QBrush(alphaColor(InsightVisualStyle::theme().panelSubtle,
                                               165)),
                            1.0);
        addSimpleSceneText(matrixScene,
                           QStringLiteral("Line"),
                           detailFont,
                           InsightVisualStyle::theme().textSecondary,
                           QPointF(detailX, detailY),
                           2.0);
        addSimpleSceneText(matrixScene,
                           QStringLiteral("Code preview"),
                           detailFont,
                           InsightVisualStyle::theme().textSecondary,
                           QPointF(detailX + 48, detailY),
                           2.0);
        matrixScene->addLine(detailX + 40,
                             detailY - 5,
                             detailX + 40,
                             detailY + 86,
                             QPen(alphaColor(InsightVisualStyle::theme().border, 32),
                                  0.6));
        detailY += 22.0;
        for (int row = 0; row < 3; ++row) {
            matrixScene->addLine(detailX,
                                 detailY + 18,
                                 inspectorX + inspectorWidth - 14,
                                 detailY + 18,
                                 QPen(alphaColor(InsightVisualStyle::theme().border, 34),
                                      0.6));
            addSimpleSceneText(matrixScene,
                               QStringLiteral("-"),
                               detailFont,
                               alphaColor(InsightVisualStyle::theme().textMuted, 100),
                               QPointF(detailX + 4, detailY),
                               2.0);
            addSimpleSceneText(matrixScene,
                               QStringLiteral("Select a cell"),
                               detailFont,
                               alphaColor(InsightVisualStyle::theme().textMuted, 100),
                               QPointF(detailX + 48, detailY),
                               2.0);
            detailY += 24.0;
        }
    }
    matrixScene->setSceneRect(0, 0, sceneWidth, footerY + matrixFooterHeight);
    renderMatrixItems();
}

void SignalUsageHotspotPanel::renderMatrixItems()
{
    if (!matrixItemsTree)
        return;

    matrixItemsTree->clear();
    for (int index : filteredItemIndexes()) {
        const SignalUsageHotspotItem& item = currentReport.items.at(index);
        if (activeMatrixCellValid) {
            if (item.role != activeMatrixRole)
                continue;
            const QString itemModuleName = item.moduleName.isEmpty()
                ? QStringLiteral("<unknown>")
                : item.moduleName;
            const QString itemFileName = item.fileName.isEmpty()
                ? QStringLiteral("<unknown>")
                : item.fileName;
            if (!activeMatrixModuleName.isEmpty()
                && itemModuleName != activeMatrixModuleName) {
                continue;
            }
            if (!activeMatrixFileName.isEmpty()
                && itemFileName != activeMatrixFileName) {
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

void SignalUsageHotspotPanel::activateMatrixCell(SignalUsageHotspotRole role,
                                                 const QString& moduleName,
                                                 const QString& fileName,
                                                 bool focusTrack)
{
    activeMatrixRole = role;
    activeMatrixModuleName = moduleName;
    activeMatrixFileName = fileName;
    activeMatrixCellValid = true;
    focusedItemIndexes.clear();

    if (focusTrack) {
        int firstFocusedIndex = -1;
        for (int index : filteredItemIndexes(false)) {
            const SignalUsageHotspotItem& item = currentReport.items.at(index);
            const QString itemModuleName = item.moduleName.isEmpty()
                ? QStringLiteral("<unknown>")
                : item.moduleName;
            const QString itemFileName = item.fileName.isEmpty()
                ? QStringLiteral("<unknown>")
                : item.fileName;
            if (item.role == activeMatrixRole
                && itemModuleName == activeMatrixModuleName
                && itemFileName == activeMatrixFileName) {
                focusedItemIndexes.insert(index);
                if (firstFocusedIndex < 0)
                    firstFocusedIndex = index;
            }
        }
        selectedItemIndex = firstFocusedIndex;
    }

    if (focusTrack)
        renderTrack();
    renderMatrix();
    renderMatrixItems();
    showMatrixCellDetails();
    refreshGraphViewActionAvailability();
}

void SignalUsageHotspotPanel::activateMatrixCellForItem(int itemIndex,
                                                        bool focusTrack)
{
    if (itemIndex < 0 || itemIndex >= currentReport.items.size())
        return;
    const SignalUsageHotspotItem& item = currentReport.items.at(itemIndex);
    activateMatrixCell(item.role,
                       item.moduleName.isEmpty() ? QStringLiteral("<unknown>")
                                                 : item.moduleName,
                       item.fileName.isEmpty() ? QStringLiteral("<unknown>")
                                               : item.fileName,
                       focusTrack);
}

void SignalUsageHotspotPanel::clearMatrixFocus()
{
    activeMatrixCellValid = false;
    activeMatrixRole = SignalUsageHotspotRole::Unknown;
    activeMatrixModuleName.clear();
    activeMatrixFileName.clear();
    focusedItemIndexes.clear();
}

QRectF SignalUsageHotspotPanel::trackRectForItem(int itemIndex) const
{
    if (!trackScene)
        return {};
    for (QGraphicsItem* item : trackScene->items()) {
        if (!item || !item->data(kItemIndexRole).isValid()
            || item->data(kItemIndexRole).toInt() != itemIndex)
            continue;
        return item->sceneBoundingRect();
    }
    return {};
}

qreal SignalUsageHotspotPanel::targetTrackRailWidth() const
{
    qreal availableWidth = kTrackRailDefaultWidth;
    if (trackView && trackView->viewport()) {
        const qreal viewportWidth = trackView->viewport()->width();
        if (viewportWidth > 0.0)
            availableWidth = viewportWidth - kLaneLabelWidth
                - kTrackDetailsGap - kTrackDetailsWidth - 22.0;
    }
    return std::clamp(availableWidth, kTrackRailMinWidth, kTrackRailMaxWidth);
}

bool SignalUsageHotspotPanel::setTrackZoom(double zoomFactor)
{
    if (!trackView)
        return false;
    trackZoomFactor = std::clamp(zoomFactor, kMinTrackZoom, kMaxTrackZoom);
    trackView->resetView();
    trackView->zoomBy(trackZoomFactor);
    saveLayout();
    return true;
}

bool SignalUsageHotspotPanel::zoomTrack(double factor)
{
    return setTrackZoom(trackZoomFactor * factor);
}

bool SignalUsageHotspotPanel::fitTrackToView()
{
    if (!trackView || !trackScene || trackScene->sceneRect().isEmpty())
        return false;
    trackView->fitScene(Qt::KeepAspectRatio);
    trackZoomFactor = std::clamp(trackView->currentZoom(),
                                 kMinTrackZoom,
                                 kMaxTrackZoom);
    saveLayout();
    return true;
}

bool SignalUsageHotspotPanel::centerCurrentUsage()
{
    if (!trackView || !trackScene)
        return false;
    for (const SignalUsageHotspotTrackLane& lane : currentReport.trackLanes) {
        if (!EditorFileIdentity::same(lane.fileName,
                                      currentEditorFileName)
            || currentEditorLine < lane.startLine
            || currentEditorLine > lane.endLine) {
            continue;
        }
        const QRectF sceneRect = trackScene->sceneRect();
        const int lineSpan = qMax(1, lane.endLine - lane.startLine);
        const qreal trackWidth =
            lastTrackRailWidth > 0.0 ? lastTrackRailWidth : targetTrackRailWidth();
        const double ratio =
            std::clamp(static_cast<double>(currentEditorLine - lane.startLine)
                           / static_cast<double>(lineSpan),
                       0.0,
                       1.0);
        trackView->centerOn(kLaneLabelWidth + ratio * trackWidth,
                            sceneRect.center().y());
        return true;
    }

    const QRectF selectedRect = trackRectForItem(selectedItemIndex);
    if (!selectedRect.isEmpty()) {
        trackView->centerOnRect(selectedRect);
        return true;
    }
    return false;
}

bool SignalUsageHotspotPanel::resetLayout()
{
    clearMatrixFocus();
    selectedItemIndex = -1;
    const bool zoomReset = setTrackZoom(1.0);
    if (contentSplitter)
        contentSplitter->setSizes({820, 980});
    saveLayout();
    rebuild();
    return zoomReset;
}

void SignalUsageHotspotPanel::restoreLayout()
{
    QSettings settings;
    settings.beginGroup(QString::fromLatin1(kSettingsGroup));
    trackZoomFactor = settings.value(QStringLiteral("trackZoom"), 1.0).toDouble();
    trackZoomFactor = std::clamp(trackZoomFactor, kMinTrackZoom, kMaxTrackZoom);
    if (modeStack)
        modeStack->setCurrentIndex(0);
    const QByteArray splitterState =
        settings.value(QStringLiteral("splitterState")).toByteArray();
    settings.endGroup();
    if (contentSplitter && !splitterState.isEmpty())
        contentSplitter->restoreState(splitterState);
    if (contentSplitter) {
        const QList<int> sizes = contentSplitter->sizes();
        if (sizes.size() < 2 || sizes.value(0) < 560 || sizes.value(1) < 720) {
            contentSplitter->setSizes({820, 980});
        }
    }
    if (trackModeButton && matrixModeButton) {
        trackModeButton->setChecked(true);
        matrixModeButton->setChecked(false);
    }
}

void SignalUsageHotspotPanel::saveLayout() const
{
    QSettings settings;
    settings.beginGroup(QString::fromLatin1(kSettingsGroup));
    if (contentSplitter)
        settings.setValue(QStringLiteral("splitterState"),
                          contentSplitter->saveState());
    settings.setValue(QStringLiteral("mode"), 0);
    settings.setValue(QStringLiteral("trackZoom"), trackZoomFactor);
    settings.endGroup();
}

void SignalUsageHotspotPanel::showInspectorForItem(int itemIndex)
{
    if (itemIndex < 0 || itemIndex >= currentReport.items.size())
        return;
    selectedItemIndex = itemIndex;
    const SignalUsageHotspotItem& item = currentReport.items.at(itemIndex);
    const QColor roleColor = hotspotRoleColor(item.role);
    const QString reason = item.roleReasonDisplayName.isEmpty()
        ? SignalUsageHotspotService::roleDisplayName(item.role)
        : item.roleReasonDisplayName;
    const QString html =
        QStringLiteral(
            "<div style='font-family:Segoe UI,Arial,sans-serif;'>"
            "<p><b>Line %1</b> &nbsp; "
            "<span style='color:%2;font-weight:600;'>%3</span></p>"
            "<p style='color:#374151;'>%4</p>"
            "<pre style='font-family:Consolas,monospace;"
            "background:#f8fafc;padding:6px;border:1px solid #e5e7eb;'>%5</pre>"
            "<p style='color:#64748b;'>%6</p>"
            "</div>")
            .arg(item.line)
            .arg(roleColor.name())
            .arg(reason.toHtmlEscaped())
            .arg(item.evidenceKindDisplayName.toHtmlEscaped())
            .arg(item.snippet.trimmed().toHtmlEscaped())
            .arg(compactFileName(item.fileName).toHtmlEscaped());
    showInspectorMessage(QStringLiteral("Usage Details"), html);
    refreshGraphViewActionAvailability();
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

void SignalUsageHotspotPanel::showMatrixCellDetails()
{
    if (!activeMatrixCellValid) {
        showInspectorMessage(QStringLiteral("Cell Details"),
                             QStringLiteral("Select a matrix cell to see hits."));
        return;
    }

    QList<int> matches;
    for (int index : filteredItemIndexes(false)) {
        const SignalUsageHotspotItem& item = currentReport.items.at(index);
        const QString itemModuleName = item.moduleName.isEmpty()
            ? QStringLiteral("<unknown>")
            : item.moduleName;
        const QString itemFileName = item.fileName.isEmpty()
            ? QStringLiteral("<unknown>")
            : item.fileName;
        if (item.role == activeMatrixRole
            && itemModuleName == activeMatrixModuleName
            && itemFileName == activeMatrixFileName) {
            matches.append(index);
        }
    }

    const QColor roleColor = hotspotRoleColor(activeMatrixRole);
    QString rows;
    const int rowLimit = qMin(matches.size(), 8);
    for (int i = 0; i < rowLimit; ++i) {
        const SignalUsageHotspotItem& item = currentReport.items.at(matches.at(i));
        rows += QStringLiteral(
                    "<tr>"
                    "<td style='padding:5px 12px 5px 0;color:#111827;"
                    "border-bottom:1px solid #eef2f7;'>%1</td>"
                    "<td style='padding:5px 0;font-family:Consolas,monospace;"
                    "border-bottom:1px solid #eef2f7;"
                    "color:#374151;'>%2</td></tr>")
                    .arg(item.line)
                    .arg(elidedForWidth(item.snippet, QFont(QStringLiteral("Consolas")), 270)
                             .toHtmlEscaped());
    }
    if (matches.size() > rowLimit) {
        rows += QStringLiteral(
            "<tr><td style='padding:5px 12px 5px 0;color:#64748b;'>...</td>"
            "<td style='padding:5px 0;color:#64748b;'>%1 more hits</td></tr>")
                    .arg(matches.size() - rowLimit);
    }
    if (rows.isEmpty()) {
        rows = QStringLiteral(
            "<tr><td colspan='2' style='color:#64748b;padding:6px 0;'>"
            "No hits in this cell.</td></tr>");
    }

    int firstLine = -1;
    if (!matches.isEmpty())
        firstLine = currentReport.items.at(matches.first()).line;
    const QString firstLineText = firstLine > 0
        ? QStringLiteral("Jump to first hit (line %1)").arg(firstLine)
        : QStringLiteral("Jump to first hit");
    const QString html =
        QStringLiteral(
            "<div style='font-family:Segoe UI,Arial,sans-serif;'>"
            "<p style='margin:0 0 8px 0;'><b>%1</b> / "
            "<span style='background:%8;color:%2;font-weight:600;"
            "padding:2px 6px;border-radius:4px;'>%3</span> / "
            "<b>%4 hits</b></p>"
            "<p style='color:#64748b;margin:0 0 10px 0;'>%5</p>"
            "<table cellspacing='0' cellpadding='0' width='100%'>"
            "<tr><th align='left' style='padding:0 12px 7px 0;color:#64748b;'>Line</th>"
            "<th align='left' style='padding:0 0 7px 0;color:#64748b;'>Code preview</th></tr>"
            "%6</table>"
            "<p style='margin-top:12px;'><a style='color:#2563eb;text-decoration:none;' "
            "href='show-all'>Show all %4 hits in list</a></p>"
            "<p><a style='color:#2563eb;text-decoration:none;' href='jump-first'>%7</a></p>"
            "</div>")
            .arg(activeMatrixModuleName.toHtmlEscaped())
            .arg(roleColor.name())
            .arg(matrixColumnDisplayName(activeMatrixRole).toHtmlEscaped())
            .arg(matches.size())
            .arg(compactFileName(activeMatrixFileName).toHtmlEscaped())
            .arg(rows)
            .arg(firstLineText.toHtmlEscaped())
            .arg(mixedColor(QColor(Qt::white), roleColor, 0.12).name());
    showInspectorMessage(QStringLiteral("Cell Details"), html);
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

QList<int> SignalUsageHotspotPanel::filteredItemIndexes(
    bool includeMatrixFocus) const
{
    QList<int> indexes;
    for (int i = 0; i < currentReport.items.size(); ++i) {
        if (!itemPassesFilters(currentReport.items.at(i)))
            continue;
        if (includeMatrixFocus
            && !focusedItemIndexes.isEmpty()
            && !focusedItemIndexes.contains(i)) {
            continue;
        }
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
