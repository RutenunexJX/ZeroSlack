#include "rtlinsightsgraphcontroller.h"

#include "rtlinsightsgraphscenemapper.h"
#include "insightgraphview.h"
#include "insightvisualstyle.h"
#include "rtlinsightspanelviewstate.h"
#include "signalusagehotspotpanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGraphicsItem>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QWidget>

RtlInsightsGraphController::RtlInsightsGraphController(
    RtlInsightsPanelViewState& viewState)
    : state(viewState),
      sceneMapper(
          std::make_unique<
              RtlInsightsGraphSceneMapper>(
              state,
              *this))
{
}

RtlInsightsGraphController::~RtlInsightsGraphController() =
    default;

int RtlInsightsGraphController::nodeItemCountForTest() const
{
    return sceneMapper->nodeItemCountForTest();
}

int RtlInsightsGraphController::edgeItemCountForTest() const
{
    return sceneMapper->edgeItemCountForTest();
}

QStringList RtlInsightsGraphController::textItemsForTest() const
{
    return sceneMapper->textItemsForTest();
}

QStringList
RtlInsightsGraphController::elementSummariesForTest() const
{
    return sceneMapper->elementSummariesForTest();
}

QStringList RtlInsightsGraphController::
    elementVisualSummariesForTest() const
{
    return sceneMapper->elementVisualSummariesForTest();
}

QStringList RtlInsightsGraphController::
    hoveredElementSummariesForTest() const
{
    return sceneMapper->hoveredElementSummariesForTest();
}

QString RtlInsightsGraphController::itemToolTipForTest(
    const QString& elementKind,
    const QString& primaryText,
    const QString& secondaryText) const
{
    return sceneMapper->itemToolTipForTest(
        elementKind,
        primaryText,
        secondaryText);
}

QRectF RtlInsightsGraphController::lastFitRectForTest() const
{
    return sceneMapper->lastFitRectForTest();
}

int RtlInsightsGraphController::selectedItemCountForTest() const
{
    return sceneMapper->selectedItemCountForTest();
}

QStringList RtlInsightsGraphController::
    selectedElementSummariesForTest() const
{
    return sceneMapper
        ->selectedElementSummariesForTest();
}

QStringList RtlInsightsGraphController::inspectorRowsForTest() const
{
    return sceneMapper->inspectorRowsForTest();
}

QStringList RtlInsightsGraphController::tableRowsForTest() const
{
    return sceneMapper->tableRowsForTest();
}

bool RtlInsightsGraphController::itemsReadableForTest() const
{
    return sceneMapper->itemsReadableForTest();
}

bool RtlInsightsGraphController::
    nestedNodeStackingReadableForTest() const
{
    return sceneMapper
        ->nestedNodeStackingReadableForTest();
}

bool RtlInsightsGraphController::setItemHoveredForTest(
    const QString& elementKind,
    const QString& primaryText,
    const QString& secondaryText,
    bool hovered)
{
    return sceneMapper->setItemHoveredForTest(
        elementKind,
        primaryText,
        secondaryText,
        hovered);
}

bool RtlInsightsGraphController::selectItemForTest(
    const QString& elementKind,
    const QString& primaryText,
    const QString& secondaryText)
{
    return sceneMapper->selectItemForTest(
        elementKind,
        primaryText,
        secondaryText);
}

bool RtlInsightsGraphController::selectTableRowForTest(
    const QString& primaryText,
    const QString& secondaryText)
{
    return sceneMapper->selectTableRowForTest(
        primaryText,
        secondaryText);
}

bool RtlInsightsGraphController::triggerNavigationForTest(
    const QString& elementKind,
    const QString& primaryText,
    const QString& secondaryText)
{
    return sceneMapper->triggerNavigationForTest(
        elementKind,
        primaryText,
        secondaryText);
}

void RtlInsightsGraphController::clearDetails()
{
    sceneMapper->clearDetails();
}

void RtlInsightsGraphController::renderGenericInspector(
    const QString& title,
    const QStringList& rows)
{
    sceneMapper->renderGenericInspector(title, rows);
}

void RtlInsightsGraphController::renderModuleBlockInspector(
    const ModuleBlockDiagramReport& report,
    const ModuleBlockDiagramNode& node)
{
    sceneMapper->renderModuleBlockInspector(
        report,
        node);
}

void RtlInsightsGraphController::
    populateModuleBlockInstancesTable(
        const ModuleBlockDiagramReport& report)
{
    sceneMapper->populateModuleBlockInstancesTable(
        report);
}

void RtlInsightsGraphController::
    populateFsmTransitionsTable(const FsmGraph& graph)
{
    sceneMapper->populateFsmTransitionsTable(graph);
}

void RtlInsightsGraphController::selectModuleBlockNode(
    int nodeId,
    bool centerGraph,
    bool syncTable)
{
    sceneMapper->selectModuleBlockNode(
        nodeId,
        centerGraph,
        syncTable);
}

bool RtlInsightsGraphController::selectItemForInspector(
    QGraphicsItem* item)
{
    return sceneMapper->selectItemForInspector(item);
}

bool RtlInsightsGraphController::selectItemAtScenePoint(
    const QPointF& scenePoint)
{
    return sceneMapper->selectItemAtScenePoint(
        scenePoint);
}

bool RtlInsightsGraphController::navigateItem(
    QGraphicsItem* item)
{
    return sceneMapper->navigateItem(item);
}

bool RtlInsightsGraphController::navigateSelectedItem()
{
    return sceneMapper->navigateSelectedItem();
}

bool RtlInsightsGraphController::
    setModuleBlockTopFromSelected()
{
    return sceneMapper
        ->setModuleBlockTopFromSelected();
}

bool RtlInsightsGraphController::selectSourceLocation(
    const RtlInsightSourceLocation& location,
    bool centerGraph)
{
    return sceneMapper->selectSourceLocation(
        location,
        centerGraph);
}

void RtlInsightsGraphController::applySearchHighlight()
{
    sceneMapper->applySearchHighlight();
}

void RtlInsightsGraphController::
    renderStateTransitionGraphScene(
        const StateTransitionGraphReport& report)
{
    ++state.graphBuildGeneration;
    state.graphDocumentRevision =
        state.currentSourceLocation.documentRevision;
    sceneMapper->renderStateTransitionGraphScene(
        report);
}

void RtlInsightsGraphController::renderFsmGraphScene(
    const FsmGraphReport& report,
    const QString& title)
{
    ++state.graphBuildGeneration;
    state.graphDocumentRevision =
        state.currentSourceLocation.documentRevision;
    sceneMapper->renderFsmGraphScene(report, title);
}

void RtlInsightsGraphController::
    renderFsmGraphLayoutScene(
        const FsmGraph& graph,
        const QString& title,
        const QString& mode)
{
    ++state.graphBuildGeneration;
    state.graphDocumentRevision =
        state.currentSourceLocation.documentRevision;
    sceneMapper->renderFsmGraphLayoutScene(
        graph,
        title,
        mode);
}

void RtlInsightsGraphController::
    mapModuleBlockDiagram(
        const ModuleBlockDiagramReport& report)
{
    ++state.graphBuildGeneration;
    state.graphDocumentRevision =
        state.currentSourceLocation.documentRevision;
    sceneMapper->renderModuleBlockDiagramScene(
        report);
}
void RtlInsightsGraphController::focusFit()
{
    if (state.insightsStack
        && state.insightsStack->currentWidget()
               == state.signalUsageHotspotPanel) {
        state.signalUsageHotspotPanel->focusFit();
    } else if (state.insightsGraphView
               && state.insightsStack
               && state.insightsStack->currentWidget()
                      == state.insightsGraphPanel) {
        state.insightsGraphView->fitScene(Qt::KeepAspectRatio);
    } else if (state.insightsTree) {
        for (int column = 0;
             column < state.insightsTree->columnCount();
             ++column) {
            state.insightsTree->resizeColumnToContents(column);
        }
    }
}

void RtlInsightsGraphController::focusZoomIn()
{
    if (state.insightsStack
        && state.insightsStack->currentWidget()
               == state.signalUsageHotspotPanel) {
        state.signalUsageHotspotPanel->focusZoomIn();
    } else if (state.insightsGraphView) {
        state.insightsGraphView->zoomIn();
    }
}

void RtlInsightsGraphController::focusZoomOut()
{
    if (state.insightsStack
        && state.insightsStack->currentWidget()
               == state.signalUsageHotspotPanel) {
        state.signalUsageHotspotPanel->focusZoomOut();
    } else if (state.insightsGraphView) {
        state.insightsGraphView->zoomOut();
    }
}

void RtlInsightsGraphController::setFocusSearchText(
    const QString& text)
{
    if (state.insightsStack
        && state.insightsStack->currentWidget()
               == state.signalUsageHotspotPanel) {
        state.signalUsageHotspotPanel->setFocusSearchText(text);
    } else if (state.graphSearchEdit) {
        state.graphSearchEdit->setText(text);
    }
}

QString RtlInsightsGraphController::focusSearchText() const
{
    if (state.insightsStack
        && state.insightsStack->currentWidget()
               == state.signalUsageHotspotPanel) {
        return state.signalUsageHotspotPanel->focusSearchText();
    }
    return state.graphSearchEdit
        ? state.graphSearchEdit->text() : state.graphSearchText;
}

void RtlInsightsGraphController::focusInspector()
{
    if (state.insightsStack
        && state.insightsStack->currentWidget()
               == state.signalUsageHotspotPanel) {
        state.signalUsageHotspotPanel->focusInspector();
    } else if (state.insightsStack
               && state.insightsStack->currentWidget()
                      == state.insightsGraphPanel
               && state.graphInspector) {
        state.graphInspector->setFocus();
    } else if (state.insightsTree) {
        state.insightsTree->setFocus();
    }
}

quint64 RtlInsightsGraphController::graphBuildGeneration() const
{
    return state.graphBuildGeneration;
}
void RtlInsightsGraphController::showTreeSurface()
{
    if (state.insightsStack && state.insightsTree)
        state.insightsStack->setCurrentWidget(state.insightsTree);
    state.currentGraphMode.clear();
}

void RtlInsightsGraphController::showGraphSurface()
{
    if (state.insightsStack && state.insightsGraphPanel)
        state.insightsStack->setCurrentWidget(state.insightsGraphPanel);
}

void RtlInsightsGraphController::showHotspotSurface()
{
    if (state.insightsStack && state.signalUsageHotspotPanel)
        state.insightsStack->setCurrentWidget(state.signalUsageHotspotPanel);
}

void RtlInsightsGraphController::renderUnavailable(
    const QString& title,
    const QString& message)
{
    showGraphSurface();
    if (!state.insightsGraphScene || !state.insightsGraphView)
        return;
    configureToolbarForMode(QString());
    clearDetails();
    state.insightsGraphScene->clear();
    state.insightsGraphView->resetView();
    state.lastGraphFitRect = QRectF();

    const InsightTheme t = InsightVisualStyle::theme();
    QFont titleFont = InsightVisualStyle::titleFont(state.insightsGraphView->font());
    titleFont.setPointSize(qMax(10, titleFont.pointSize() + 2));
    auto* card = state.insightsGraphScene->addRect(
        QRectF(-230, -86, 460, 172),
        InsightVisualStyle::panelBorderPen(),
        InsightVisualStyle::panelBrush());
    card->setZValue(-1);

    auto* titleItem = state.insightsGraphScene->addSimpleText(title, titleFont);
    titleItem->setBrush(QBrush(t.textPrimary));
    titleItem->setPos(-188, -48);

    QFont detailFont = InsightVisualStyle::compactFont(state.insightsGraphView->font());
    auto* detailItem = state.insightsGraphScene->addText(
        message.isEmpty()
            ? QStringLiteral("No graph is available for the current selection.")
            : message,
        detailFont);
    detailItem->setTextWidth(376.0);
    detailItem->setDefaultTextColor(t.warning);
    detailItem->setPos(-188, -8);

    state.insightsGraphScene->setSceneRect(-220, -90, 440, 180);
    state.lastGraphFitRect = state.insightsGraphScene->sceneRect();
    state.insightsGraphView->fitScene(Qt::KeepAspectRatio);
    applySearchHighlight();
}
void RtlInsightsGraphController::configureToolbarForMode(
    const QString& mode)
{
    state.currentGraphMode = mode;
    const bool moduleMode = mode == QStringLiteral("module-block");
    const bool stateMode =
        mode == QStringLiteral("state-transition")
        || mode == QStringLiteral("fsm");
    const bool graphMode = moduleMode || stateMode;

    for (QWidget* widget :
         {static_cast<QWidget*>(state.moduleBlockTopCombo),
          state.insightsGraphPanel
              ? state.insightsGraphPanel->findChild<QWidget*>(
                    QStringLiteral("rtlModuleBlockTopLabel"))
              : nullptr,
          static_cast<QWidget*>(state.moduleBlockSetSelectionButton),
          static_cast<QWidget*>(state.moduleBlockDepthSpin),
          static_cast<QWidget*>(state.moduleBlockCollapsePackagesCheck),
          static_cast<QWidget*>(state.moduleBlockShowUnresolvedCheck)}) {
        if (widget)
            widget->setVisible(moduleMode);
    }
    for (QWidget* widget :
         {state.insightsGraphPanel
              ? state.insightsGraphPanel->findChild<QWidget*>(
                    QStringLiteral("rtlStateSignalLabel"))
              : nullptr,
          static_cast<QWidget*>(state.stateTransitionSignalCombo),
          state.insightsGraphPanel
              ? state.insightsGraphPanel->findChild<QWidget*>(
                    QStringLiteral("rtlStateCurrentLabel"))
              : nullptr,
          static_cast<QWidget*>(state.stateTransitionCurrentCombo),
          state.insightsGraphPanel
              ? state.insightsGraphPanel->findChild<QWidget*>(
                    QStringLiteral("rtlStateNextLabel"))
              : nullptr,
          static_cast<QWidget*>(state.stateTransitionNextCombo),
          static_cast<QWidget*>(state.stateTransitionResetCheck),
          static_cast<QWidget*>(state.stateTransitionErrorCheck),
          static_cast<QWidget*>(state.stateTransitionUnreachableCheck)}) {
        if (widget)
            widget->setVisible(stateMode);
    }
    if (state.graphLayoutCombo) {
        state.graphLayoutCombo->setVisible(graphMode);
        const QSignalBlocker blocker(state.graphLayoutCombo);
        if (moduleMode)
            state.graphLayoutCombo->setCurrentText(QStringLiteral("Nested blocks"));
        else if (stateMode)
            state.graphLayoutCombo->setCurrentText(QStringLiteral("State flow"));
    }
    if (state.graphTable)
        state.graphTable->setVisible(graphMode);
    if (state.graphInspector)
        state.graphInspector->setVisible(graphMode);
}
