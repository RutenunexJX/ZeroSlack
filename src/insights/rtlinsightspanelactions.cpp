#include "rtlinsightspanelcoordinator.h"

#include "editorlocation.h"
#include "graphexportservice.h"
#include "graphexportui.h"
#include "insightgraphview.h"
#include "rtlinsightsgraphcontroller.h"
#include "rtlinsightspanelviewstate.h"

#include <QAction>
#include <QDockWidget>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QPushButton>
#include <QTreeWidget>
#include <QWidget>

QAction* RtlInsightsPanelCoordinator::createGraphAction(
    QWidget* owner,
    const QString& actionId)
{
    if (!owner)
        return nullptr;
    const ActionDescriptor* descriptor =
        findActionById(actionId);
    if (!descriptor
        || !descriptor->hasSurface(
            ActionSurface::GraphPanel)
        || !descriptor->executionRoute.startsWith(
            QStringLiteral("insight.graph."))) {
        return nullptr;
    }

    const ActionAliasDescriptor graphAlias =
        descriptor->aliasForSurface(
            ActionSurface::GraphPanel);
    auto* action = new QAction(
        graphAlias.label.trimmed().isEmpty()
            ? descriptor->canonicalName
            : graphAlias.label.trimmed(),
        owner);
    action->setObjectName(graphAlias.adapterKey);
    action->setProperty(
        GraphExportUi::kActionIdProperty,
        descriptor->id);
    action->setProperty(
        GraphExportUi::kExecutionRouteProperty,
        descriptor->executionRoute);
    action->setToolTip(descriptor->description);
    QObject::connect(
        action,
        &QAction::triggered,
        owner,
        [this, actionId]() {
            requestGraphAction(actionId);
        });
    return action;
}

QAction* RtlInsightsPanelCoordinator::
    createSelectedSourceAction(
        QWidget* owner,
        const QString& actionId)
{
    if (!owner)
        return nullptr;
    const ActionDescriptor* descriptor =
        findActionById(actionId);
    if (!descriptor
        || !descriptor->hasSurface(
            ActionSurface::ContextMenu)) {
        return nullptr;
    }
    const ActionAliasDescriptor contextAlias =
        descriptor->aliasForSurface(
            ActionSurface::ContextMenu);
    auto* action = new QAction(
        contextAlias.label.trimmed().isEmpty()
            ? descriptor->canonicalName
            : contextAlias.label.trimmed(),
        owner);
    action->setObjectName(
        QStringLiteral(
            "rtlGraphTemporaryEditorAction"));
    action->setProperty(
        GraphExportUi::kActionIdProperty,
        descriptor->id);
    action->setProperty(
        GraphExportUi::kExecutionRouteProperty,
        descriptor->executionRoute);
    action->setToolTip(descriptor->description);
    action->setStatusTip(descriptor->description);
    action->setEnabled(false);
    QObject::connect(
        action,
        &QAction::triggered,
        owner,
        [this, actionId]() {
            requestGraphAction(actionId);
        });
    return action;
}

void RtlInsightsPanelCoordinator::bindGraphActionButton(
    QPushButton* button,
    QAction* action)
{
    if (!button || !action)
        return;
    button->setText(action->text());
    button->setToolTip(action->toolTip());
    button->setProperty(
        GraphExportUi::kActionIdProperty,
        action->property(
            GraphExportUi::kActionIdProperty));
    button->setProperty(
        GraphExportUi::kExecutionRouteProperty,
        action->property(
            GraphExportUi::kExecutionRouteProperty));
    const QString actionId = action->property(
        GraphExportUi::kActionIdProperty).toString();
    QObject::connect(
        button,
        &QPushButton::clicked,
        button,
        [this, actionId]() {
            requestGraphAction(actionId);
        });
}

QAction* RtlInsightsPanelCoordinator::graphAction(
    const QString& actionId) const
{
    if (!viewState)
        return nullptr;
    if (actionId
        == QString::fromLatin1(
            ActionIds::GraphJumpSelected)) {
        return viewState->graphJumpAction;
    }
    if (actionId
        == QString::fromLatin1(
            ActionIds::GraphFocusSelected)) {
        return viewState->graphFocusAction;
    }
    if (actionId
        == QString::fromLatin1(
            ActionIds::GraphSetTopSelected)) {
        return viewState->graphSetTopAction;
    }
    if (actionId
        == QString::fromLatin1(
            ActionIds::ViewTemporaryEditorOpen)) {
        return viewState->graphTemporaryEditorAction;
    }
    return nullptr;
}

void RtlInsightsPanelCoordinator::
    refreshGraphActionAvailability() const
{
    if (!viewState)
        return;
    const auto update =
        [this](QAction* action,
               const QPushButton* availabilityButton) {
            if (!action)
                return;
            const ActionDescriptor* descriptor =
                findActionById(
                    action->property(
                        GraphExportUi::kActionIdProperty)
                        .toString());
            if (!descriptor)
                return;
            ActionAvailabilityContext context;
            context.graphContentAvailable =
                hasExportableGraph();
            const ActionAvailabilityState availability =
                evaluateActionAvailability(
                    *descriptor,
                    context);
            const bool enabled =
                availability.executable
                && availabilityButton
                && availabilityButton->isEnabled();
            action->setEnabled(enabled);
            action->setToolTip(
                enabled
                    ? descriptor->description
                    : QStringLiteral("%1\n%2")
                          .arg(
                              descriptor->description,
                              availability.reason.isEmpty()
                                  ? descriptor->unavailableReason
                                  : availability.reason));
        };
    update(viewState->graphJumpAction,
           viewState->graphInspectorJumpButton);
    update(viewState->graphFocusAction,
           viewState->graphInspectorFocusButton);
    update(viewState->graphSetTopAction,
           viewState->graphInspectorSetTopButton);
}

ActionExecutionResult
RtlInsightsPanelCoordinator::requestGraphAction(
    const QString& actionId)
{
    refreshGraphActionAvailability();
    const ActionDescriptor* descriptor =
        findActionById(actionId);
    QAction* action = graphAction(actionId);
    ActionExecutionResult result;
    result.handled = true;
    if (!descriptor || !action) {
        result.failureReason =
            QStringLiteral(
                "RTL Insights graph Action is unavailable.");
    } else if (!action->isEnabled()) {
        result.failureReason =
            descriptor->unavailableReason;
    } else if (actionId
               == QString::fromLatin1(
                   ActionIds::ViewTemporaryEditorOpen)) {
        const RtlInsightSourceLocation source =
            graphController
            ? graphController->selectedSourceLocation()
            : RtlInsightSourceLocation();
        if (!source.hasSourcePosition()) {
            result.failureReason = QStringLiteral(
                "Select a graph item with a source location first.");
        } else if (!registeredActionRequestHandler) {
            result.failureReason = QStringLiteral(
                "The temporary-editor Action is unavailable.");
        } else {
            EditorLocation location;
            location.filePath = source.fileName;
            location.line = source.line;
            location.column = qMax(1, source.column);
            location.symbolKey = source.symbolName;
            location.sourceLinkId = QStringLiteral(
                "rtl-insight:%1:%2:%3:%4:%5")
                .arg(
                    source.elementKind,
                    source.moduleName,
                    source.symbolName,
                    source.secondarySymbolName)
                .arg(source.line);
            result = registeredActionRequestHandler(
                descriptor->id,
                editorLocationActionParameters(location));
        }
    } else {
        result = executeAction(*descriptor, *this);
    }

    if (!result.succeeded
        && viewState
        && viewState->statusMessageHandler) {
        viewState->statusMessageHandler(
            result.failureReason,
            4000);
    }
    return result;
}

ActionExecutionResult
RtlInsightsPanelCoordinator::executeActionRoute(
    const ActionDescriptor& descriptor,
    const ActionInvocation& invocation)
{
    Q_UNUSED(invocation);
    ActionExecutionResult result;
    result.handled = true;
    const QString& route = descriptor.executionRoute;
    if (route
        == QStringLiteral(
            "insight.graph.jumpSelected")) {
        result.succeeded =
            graphController
            && graphController->navigateSelectedItem();
        if (!result.succeeded) {
            result.failureReason = QStringLiteral(
                "Select a graph item with a source location first.");
        }
        return result;
    }
    if (route
        == QStringLiteral(
            "insight.graph.focusSelected")) {
        if (viewState
            && viewState->insightsGraphScene
            && viewState->insightsGraphView) {
            const QList<QGraphicsItem*> selected =
                viewState->insightsGraphScene
                    ->selectedItems();
            if (!selected.isEmpty()) {
                viewState->insightsGraphView
                    ->centerOnRect(
                        selected.first()
                            ->sceneBoundingRect());
                result.succeeded = true;
            }
        }
        if (!result.succeeded) {
            result.failureReason =
                QStringLiteral(
                    "Select a graph item to focus first.");
        }
        return result;
    }
    if (route
        == QStringLiteral(
            "insight.graph.setTopSelected")) {
        result.succeeded =
            graphController
            && graphController
                   ->setModuleBlockTopFromSelected();
        if (!result.succeeded) {
            result.failureReason = QStringLiteral(
                "Select a resolved module block first.");
        }
        return result;
    }

    result.failureReason = QStringLiteral(
        "Action is not an RTL Insights graph-selection route.");
    return result;
}

GraphExportResult RtlInsightsPanelCoordinator::exportCurrentGraph(
    const QString& outputPath,
    const GraphExportOptions& options) const
{
    return GraphExportService::exportGraphicsScene(
        viewState ? viewState->insightsGraphScene : nullptr,
        outputPath,
        options);
}

bool RtlInsightsPanelCoordinator::hasExportableGraph() const
{
    if (!viewState || !viewState->insightsGraphScene)
        return false;
    const QString mode = viewState->currentGraphMode;
    if (mode != QStringLiteral("fsm")
        && mode != QStringLiteral("state-transition")
        && mode != QStringLiteral("module-block")) {
        return false;
    }
    for (QGraphicsItem* item :
         viewState->insightsGraphScene->items()) {
        if (item && item->isVisible())
            return true;
    }
    return false;
}

void RtlInsightsPanelCoordinator::
    refreshGraphExportActionAvailability() const
{
    GraphExportUi::updateActionAvailability(
        viewState ? viewState->graphExportAction : nullptr,
        hasExportableGraph());
}

QAction* RtlInsightsPanelCoordinator::graphExportAction() const
{
    refreshGraphExportActionAvailability();
    return viewState ? viewState->graphExportAction : nullptr;
}

QDockWidget* RtlInsightsPanelCoordinator::dock() const
{
    return viewState->insightsDock;
}

QTreeWidget* RtlInsightsPanelCoordinator::tree() const
{
    return viewState->insightsTree;
}

QGraphicsView* RtlInsightsPanelCoordinator::graphView() const
{
    return viewState->insightsGraphView;
}
