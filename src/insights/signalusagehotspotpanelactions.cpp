#include "signalusagehotspotpanel.h"

#include "editorfileidentity.h"
#include "graphexportui.h"

#include <QAction>
#include <QGraphicsScene>
#include <QPushButton>

QAction* SignalUsageHotspotPanel::createGraphViewAction(
    const QString& actionId)
{
    const ActionDescriptor* descriptor =
        findActionById(actionId);
    if (!descriptor
        || !descriptor->hasSurface(
            ActionSurface::GraphPanel)
        || !descriptor->executionRoute.startsWith(
            QStringLiteral("insight.graphView."))) {
        return nullptr;
    }

    const ActionAliasDescriptor graphAlias =
        descriptor->aliasForSurface(
            ActionSurface::GraphPanel);
    auto* action = new QAction(
        graphAlias.label.trimmed().isEmpty()
            ? descriptor->canonicalName
            : graphAlias.label.trimmed(),
        this);
    action->setObjectName(graphAlias.adapterKey);
    action->setProperty(
        GraphExportUi::kActionIdProperty,
        descriptor->id);
    action->setProperty(
        GraphExportUi::kExecutionRouteProperty,
        descriptor->executionRoute);
    action->setToolTip(descriptor->description);
    connect(action,
            &QAction::triggered,
            this,
            [this, actionId]() {
                requestGraphViewAction(actionId);
            });
    return action;
}

void SignalUsageHotspotPanel::bindGraphViewButton(
    QPushButton* button,
    QAction* action)
{
    if (!button || !action)
        return;
    const auto sync = [button, action]() {
        button->setText(action->text());
        button->setToolTip(action->toolTip());
        button->setEnabled(action->isEnabled());
        button->setProperty(
            GraphExportUi::kActionIdProperty,
            action->property(
                GraphExportUi::kActionIdProperty));
        button->setProperty(
            GraphExportUi::kExecutionRouteProperty,
            action->property(
                GraphExportUi::kExecutionRouteProperty));
    };
    sync();
    connect(action,
            &QAction::changed,
            button,
            sync);
    const QString actionId = action->property(
        GraphExportUi::kActionIdProperty).toString();
    connect(button,
            &QPushButton::clicked,
            this,
            [this, actionId]() {
                requestGraphViewAction(actionId);
            });
}

QAction* SignalUsageHotspotPanel::graphViewAction(
    const QString& actionId) const
{
    if (actionId
        == QString::fromLatin1(
            ActionIds::GraphViewFit)) {
        return fitViewAction;
    }
    if (actionId
        == QString::fromLatin1(
            ActionIds::GraphViewZoomIn)) {
        return zoomInViewAction;
    }
    if (actionId
        == QString::fromLatin1(
            ActionIds::GraphViewZoomOut)) {
        return zoomOutViewAction;
    }
    if (actionId
        == QString::fromLatin1(
            ActionIds::GraphViewCenterCurrent)) {
        return centerCurrentViewAction;
    }
    if (actionId
        == QString::fromLatin1(
            ActionIds::GraphViewResetLayout)) {
        return resetLayoutViewAction;
    }
    return nullptr;
}

QList<QAction*>
SignalUsageHotspotPanel::graphViewActions() const
{
    return {
        fitViewAction,
        centerCurrentViewAction,
        zoomInViewAction,
        zoomOutViewAction,
        resetLayoutViewAction,
    };
}

bool SignalUsageHotspotPanel::hasGraphViewContent() const
{
    return currentReport.found
        && !currentReport.items.isEmpty()
        && trackScene
        && lastTrackBlockCount > 0;
}

void SignalUsageHotspotPanel::
    refreshGraphViewActionAvailability() const
{
    const bool graphContent = hasGraphViewContent();
    for (QAction* action : graphViewActions()) {
        GraphExportUi::updateActionAvailability(
            action,
            graphContent);
    }

    bool centerAvailable = false;
    if (graphContent) {
        for (const SignalUsageHotspotTrackLane& lane :
             currentReport.trackLanes) {
            if (EditorFileIdentity::same(
                    lane.fileName,
                    currentEditorFileName)
                && currentEditorLine >= lane.startLine
                && currentEditorLine <= lane.endLine) {
                centerAvailable = true;
                break;
            }
        }
        centerAvailable = centerAvailable
            || !trackRectForItem(
                    selectedItemIndex).isEmpty();
    }
    if (centerCurrentViewAction
        && !centerAvailable) {
        const ActionDescriptor* descriptor =
            findActionById(
                QString::fromLatin1(
                    ActionIds::GraphViewCenterCurrent));
        centerCurrentViewAction->setEnabled(false);
        if (descriptor) {
            centerCurrentViewAction->setToolTip(
                QStringLiteral("%1\n%2")
                    .arg(descriptor->description,
                         descriptor->unavailableReason));
        }
    }
}

ActionExecutionResult
SignalUsageHotspotPanel::requestGraphViewAction(
    const QString& actionId)
{
    refreshGraphViewActionAvailability();
    const ActionDescriptor* descriptor =
        findActionById(actionId);
    QAction* action = graphViewAction(actionId);
    ActionExecutionResult result;
    result.handled = true;
    if (!descriptor || !action) {
        result.failureReason = QStringLiteral(
            "Graph view Action is unavailable.");
    } else if (!action->isEnabled()) {
        result.failureReason =
            descriptor->unavailableReason;
    } else {
        result = executeAction(*descriptor, *this);
    }

    if (!result.succeeded)
        showStatusMessage(result.failureReason, 4000);
    return result;
}

ActionExecutionResult
SignalUsageHotspotPanel::executeActionRoute(
    const ActionDescriptor& descriptor,
    const ActionInvocation& invocation)
{
    Q_UNUSED(invocation);
    ActionExecutionResult result;
    result.handled = true;
    const QString& route = descriptor.executionRoute;
    if (route
        == QStringLiteral("insight.graphView.fit")) {
        result.succeeded = fitTrackToView();
    } else if (route
               == QStringLiteral(
                   "insight.graphView.zoomIn")) {
        result.succeeded = zoomTrack(1.25);
    } else if (route
               == QStringLiteral(
                   "insight.graphView.zoomOut")) {
        result.succeeded = zoomTrack(0.8);
    } else if (route
               == QStringLiteral(
                   "insight.graphView.centerCurrent")) {
        result.succeeded = centerCurrentUsage();
    } else if (route
               == QStringLiteral(
                   "insight.graphView.resetLayout")) {
        result.succeeded = resetLayout();
    } else {
        result.failureReason = QStringLiteral(
            "Action is not a graph view route.");
        return result;
    }

    if (!result.succeeded) {
        result.failureReason =
            descriptor.unavailableReason;
    }
    return result;
}

QAction* SignalUsageHotspotPanel::graphViewActionForTest(
    const QString& actionId) const
{
    refreshGraphViewActionAvailability();
    return graphViewAction(actionId);
}

QList<QAction*>
SignalUsageHotspotPanel::graphViewActionsForTest() const
{
    refreshGraphViewActionAvailability();
    return graphViewActions();
}

ActionExecutionResult
SignalUsageHotspotPanel::triggerGraphViewActionForTest(
    const QString& actionId)
{
    return requestGraphViewAction(actionId);
}

qreal SignalUsageHotspotPanel::trackZoomFactorForTest() const
{
    return trackZoomFactor;
}
