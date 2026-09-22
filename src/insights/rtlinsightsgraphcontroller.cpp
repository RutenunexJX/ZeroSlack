#include "rtlinsightsgraphcontroller.h"

#include "rtlinsightsgraphscenemapper.h"
#include "insightgraphview.h"
#include "insightvisualstyle.h"
#include "moduleblockdiagramtoolbar.h"
#include "rtlinsightsgraphconstants.h"
#include "rtlinsightspanelviewstate.h"
#include "signalusagehotspotpanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGraphicsItem>
#include <QLineF>
#include <QLineEdit>
#include <QList>
#include <QPointer>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSize>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVariant>
#include <QWidget>

#include <memory>

namespace {

class RtlThemeViewportInteractionGuard final : public QObject
{
public:
    explicit RtlThemeViewportInteractionGuard(
        std::shared_ptr<quint64> interactionEpoch)
        : epoch(std::move(interactionEpoch))
    {
    }

    void watch(QObject* object)
    {
        if (!object || watchedObjects.contains(object))
            return;
        watchedObjects.append(object);
        object->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        Q_UNUSED(watched)
        if (!event)
            return false;
        switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonDblClick:
        case QEvent::Wheel:
        case QEvent::KeyPress:
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::NativeGesture:
            ++(*epoch);
            break;
        default:
            break;
        }
        return false;
    }

private:
    std::shared_ptr<quint64> epoch;
    QList<QObject*> watchedObjects;
};

void restoreThemeViewport(
    InsightGraphView* graphView,
    const QTransform& transform,
    const QPointF& center,
    const QSize& viewportSize,
    int horizontalValue,
    int verticalValue)
{
    if (!graphView || !graphView->viewport())
        return;

    graphView->setTransform(transform);
    if (graphView->viewport()->size() == viewportSize) {
        graphView->horizontalScrollBar()->setValue(
            horizontalValue);
        graphView->verticalScrollBar()->setValue(
            verticalValue);
    } else {
        graphView->centerOn(center);
    }

    // QGraphicsView represents its center through integer scroll-bar values.
    // centerOn() can round both axes in the same direction; at fractional zoom
    // that makes the combined two-axis error larger than either pixel step.
    // Search the small set of realizable values around Qt's candidate and keep
    // the closest scene-space center.
    QScrollBar* horizontal =
        graphView->horizontalScrollBar();
    QScrollBar* vertical =
        graphView->verticalScrollBar();
    const int baseHorizontal = horizontal->value();
    const int baseVertical = vertical->value();
    int bestHorizontal = baseHorizontal;
    int bestVertical = baseVertical;
    qreal bestDistance = QLineF(
        graphView->mapToScene(
            graphView->viewport()->rect().center()),
        center).length();

    constexpr int kScrollCandidateRadius = 2;
    for (int horizontalDelta = -kScrollCandidateRadius;
         horizontalDelta <= kScrollCandidateRadius;
         ++horizontalDelta) {
        horizontal->setValue(
            baseHorizontal + horizontalDelta);
        for (int verticalDelta = -kScrollCandidateRadius;
             verticalDelta <= kScrollCandidateRadius;
             ++verticalDelta) {
            vertical->setValue(
                baseVertical + verticalDelta);
            const qreal candidateDistance = QLineF(
                graphView->mapToScene(
                    graphView->viewport()->rect().center()),
                center).length();
            if (candidateDistance < bestDistance) {
                bestDistance = candidateDistance;
                bestHorizontal = horizontal->value();
                bestVertical = vertical->value();
            }
        }
    }
    horizontal->setValue(bestHorizontal);
    vertical->setValue(bestVertical);
}

} // namespace

struct RtlInsightsThemeViewportState
{
    QTransform transform;
    QPointF center;
    QSize viewportSize;
    int horizontalValue = 0;
    int verticalValue = 0;
    quint64 graphGeneration = 0;
    quint64 captureEpoch = 0;
    quint64 interactionEpochAtCapture = 0;
    bool valid = false;
    QPointer<QGraphicsScene> scene;
    std::shared_ptr<quint64> restoreEpoch =
        std::make_shared<quint64>(0);
    std::shared_ptr<quint64> interactionEpoch =
        std::make_shared<quint64>(0);
    std::unique_ptr<RtlThemeViewportInteractionGuard> interactionGuard =
        std::make_unique<RtlThemeViewportInteractionGuard>(
            interactionEpoch);
};

namespace {

constexpr auto kRtlGraphBuildGenerationProperty =
    "rtlGraphBuildGeneration";

void publishGraphGeneration(
    const RtlInsightsPanelViewState& state)
{
    if (state.insightsGraphScene) {
        state.insightsGraphScene->setProperty(
            kRtlGraphBuildGenerationProperty,
            QVariant::fromValue<qulonglong>(
                state.graphBuildGeneration));
    }
}

} // namespace

RtlInsightsGraphController::RtlInsightsGraphController(
    RtlInsightsPanelViewState& viewState)
    : state(viewState),
      sceneMapper(
          std::make_unique<
              RtlInsightsGraphSceneMapper>(
              state,
              *this)),
      themeViewportState(
          std::make_unique<RtlInsightsThemeViewportState>())
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

void RtlInsightsGraphController::
    populateFsmTransitionsTable(const FsmGraph& graph)
{
    sceneMapper->populateFsmTransitionsTable(graph);
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

RtlInsightSourceLocation
RtlInsightsGraphController::selectedSourceLocation() const
{
    return sceneMapper->selectedSourceLocation();
}

bool RtlInsightsGraphController::
    setModuleBlockTopFromSelected()
{
    return sceneMapper
        ->setModuleBlockTopFromSelected();
}

bool RtlInsightsGraphController::enterModuleBlockNode(int nodeId)
{ return sceneMapper->enterModuleBlockNode(nodeId); }
bool RtlInsightsGraphController::navigateModuleBlockBreadcrumb(int index)
{ return sceneMapper->navigateModuleBlockBreadcrumb(index); }
bool RtlInsightsGraphController::toggleModuleBlockNode(int nodeId)
{ return sceneMapper->toggleModuleBlockNode(nodeId); }
void RtlInsightsGraphController::refreshModuleBlockSelectionActions()
{ sceneMapper->refreshModuleBlockSelectionActions(); }

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
    publishGraphGeneration(state);
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
    publishGraphGeneration(state);
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
    publishGraphGeneration(state);
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
    publishGraphGeneration(state);
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
        if (state.moduleBlockToolbar) state.moduleBlockToolbar->setSearchText(text);
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
               && state.insightsGraphView) {
        if (state.currentGraphMode == QStringLiteral("module-block"))
            state.insightsGraphView->setFocus();
        else if (state.graphInspector)
            state.graphInspector->setFocus();
    } else if (state.insightsTree) {
        state.insightsTree->setFocus();
    }
}

void RtlInsightsGraphController::captureThemeViewportState()
{
    RtlInsightsThemeViewportState& snapshot =
        *themeViewportState;
    ++(*snapshot.restoreEpoch);
    snapshot.valid = false;

    InsightGraphView* graphView =
        state.insightsGraphView;
    if (!graphView || !graphView->viewport()
        || !state.insightsGraphScene) {
        return;
    }

    for (QObject* object :
         {static_cast<QObject*>(graphView),
          static_cast<QObject*>(graphView->viewport()),
          static_cast<QObject*>(graphView->horizontalScrollBar()),
          static_cast<QObject*>(graphView->verticalScrollBar()),
          static_cast<QObject*>(state.graphZoomOutButton),
          static_cast<QObject*>(state.graphFitButton),
          static_cast<QObject*>(state.graphZoomInButton),
          static_cast<QObject*>(state.graphSearchEdit)}) {
        snapshot.interactionGuard->watch(object);
    }

    snapshot.transform = graphView->transform();
    snapshot.center = graphView->mapToScene(
        graphView->viewport()->rect().center());
    snapshot.viewportSize =
        graphView->viewport()->size();
    snapshot.horizontalValue =
        graphView->horizontalScrollBar()->value();
    snapshot.verticalValue =
        graphView->verticalScrollBar()->value();
    snapshot.graphGeneration =
        state.graphBuildGeneration;
    snapshot.captureEpoch =
        *snapshot.restoreEpoch;
    snapshot.interactionEpochAtCapture =
        *snapshot.interactionEpoch;
    snapshot.scene = state.insightsGraphScene;
    snapshot.valid = true;
}

void RtlInsightsGraphController::refreshThemePresentation()
{
    if (state.signalUsageHotspotPanel)
        state.signalUsageHotspotPanel->refreshThemePresentation();

    if (state.insightsTree && state.insightsTree->viewport())
        state.insightsTree->viewport()->update();
    if (!state.insightsGraphView
        || !state.insightsGraphScene) {
        return;
    }
    sceneMapper->refreshThemePresentation();

    RtlInsightsThemeViewportState& snapshot =
        *themeViewportState;
    if (!snapshot.valid)
        return;
    snapshot.valid = false;
    if (snapshot.scene != state.insightsGraphScene
        || snapshot.graphGeneration
               != state.graphBuildGeneration
        || snapshot.interactionEpochAtCapture
               != *snapshot.interactionEpoch) {
        return;
    }

    InsightGraphView* graphView =
        state.insightsGraphView;
    const QTransform transform = snapshot.transform;
    const QPointF center = snapshot.center;
    const QSize viewportSize = snapshot.viewportSize;
    const int horizontalValue =
        snapshot.horizontalValue;
    const int verticalValue =
        snapshot.verticalValue;
    const quint64 captureEpoch =
        snapshot.captureEpoch;
    const quint64 graphGeneration =
        snapshot.graphGeneration;
    const auto restoreEpoch = snapshot.restoreEpoch;
    const auto interactionEpoch =
        snapshot.interactionEpoch;

    restoreThemeViewport(graphView,
                         transform,
                         center,
                         viewportSize,
                         horizontalValue,
                         verticalValue);
    const quint64 interactionEpochAfterRestore =
        *interactionEpoch;
    QPointer<InsightGraphView> guardedView = graphView;
    QPointer<QGraphicsScene> guardedScene =
        state.insightsGraphScene;
    QTimer::singleShot(
        0,
        graphView,
        [guardedView,
         guardedScene,
         transform,
         center,
         viewportSize,
         horizontalValue,
         verticalValue,
         graphGeneration,
         captureEpoch,
         restoreEpoch,
         interactionEpoch,
         interactionEpochAfterRestore]() {
            if (!guardedView || !guardedScene
                || guardedView->scene() != guardedScene
                || *restoreEpoch != captureEpoch
                || *interactionEpoch
                       != interactionEpochAfterRestore
                || guardedScene
                       ->property(
                           kRtlGraphBuildGenerationProperty)
                       .toULongLong()
                       != graphGeneration) {
                return;
            }
            restoreThemeViewport(
                guardedView,
                transform,
                center,
                viewportSize,
                horizontalValue,
                verticalValue);
            if (guardedView->viewport())
                guardedView->viewport()->update();
        });
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
    state.currentGraphMode.clear();
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
    card->setData(kGraphThemeVisualRole,
                  kGraphThemePanel);

    auto* titleItem = state.insightsGraphScene->addSimpleText(title, titleFont);
    titleItem->setBrush(QBrush(t.textPrimary));
    titleItem->setData(kGraphThemeVisualRole,
                       kGraphThemeTextPrimary);
    titleItem->setPos(-188, -48);

    QFont detailFont = InsightVisualStyle::compactFont(state.insightsGraphView->font());
    auto* detailItem = state.insightsGraphScene->addText(
        message.isEmpty()
            ? QStringLiteral("No graph is available for the current selection.")
            : message,
        detailFont);
    detailItem->setTextWidth(376.0);
    detailItem->setDefaultTextColor(t.warning);
    detailItem->setData(kGraphThemeVisualRole,
                        kGraphThemeWarning);
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
    if (state.moduleBlockToolbar) state.moduleBlockToolbar->setVisible(moduleMode);
    if (state.graphToolbar) state.graphToolbar->setVisible(stateMode);
    if (state.insightsGraphView) {
        state.insightsGraphView->setFitUpscalingEnabled(!moduleMode);
        state.insightsGraphView->setGridVisible(!moduleMode);
    }

    for (QWidget* widget :
         {static_cast<QWidget*>(state.moduleBlockDepthSpin),
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
        state.graphTable->setVisible(stateMode);
    if (state.graphInspectorPanel)
        state.graphInspectorPanel->setVisible(stateMode);
}
