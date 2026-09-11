#include "insightfocuscontroller.h"

#include "graphexportui.h"

#include <QAction>
#include <QPushButton>
#include <QWidget>

QAction* InsightFocusController::createGraphViewAction(
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
        page);
    action->setObjectName(
        QStringLiteral("insightFocus.%1")
            .arg(graphAlias.adapterKey));
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

void InsightFocusController::bindGraphViewButton(
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

QAction* InsightFocusController::graphViewAction(
    const QString& actionId) const
{
    if (actionId
        == QString::fromLatin1(
            ActionIds::GraphViewFit)) {
        return fitAction;
    }
    if (actionId
        == QString::fromLatin1(
            ActionIds::GraphViewZoomOut)) {
        return zoomOutAction;
    }
    if (actionId
        == QString::fromLatin1(
            ActionIds::GraphViewZoomIn)) {
        return zoomInAction;
    }
    return nullptr;
}

void InsightFocusController::
    refreshGraphViewActionAvailability(
        const PanelEntry* entry)
{
    const auto update =
        [](QAction* action, bool enabled) {
            if (!action)
                return;
            const ActionDescriptor* descriptor =
                findActionById(
                    action->property(
                        GraphExportUi::kActionIdProperty)
                        .toString());
            action->setEnabled(enabled);
            if (descriptor) {
                action->setToolTip(
                    enabled
                        ? descriptor->description
                        : QStringLiteral("%1\n%2")
                              .arg(
                                  descriptor->description,
                                  descriptor->unavailableReason));
            }
        };
    update(fitAction,
           entry
               && static_cast<bool>(
                   entry->registration.fit));
    update(zoomOutAction,
           entry
               && static_cast<bool>(
                   entry->registration.zoomOut));
    update(zoomInAction,
           entry
               && static_cast<bool>(
                   entry->registration.zoomIn));
}

ActionExecutionResult
InsightFocusController::requestGraphViewAction(
    const QString& actionId)
{
    PanelEntry* entry = activeEntry();
    refreshGraphViewActionAvailability(entry);
    const ActionDescriptor* descriptor =
        findActionById(actionId);
    QAction* action = graphViewAction(actionId);
    ActionExecutionResult result;
    result.handled = true;
    if (!descriptor || !action) {
        result.failureReason = QStringLiteral(
            "Focus graph Action is unavailable.");
    } else if (!action->isEnabled()) {
        result.failureReason =
            descriptor->unavailableReason;
    } else {
        result = executeAction(*descriptor, *this);
    }
    return result;
}

ActionExecutionResult
InsightFocusController::executeActionRoute(
    const ActionDescriptor& descriptor,
    const ActionInvocation& invocation)
{
    Q_UNUSED(invocation);
    ActionExecutionResult result;
    result.handled = true;
    PanelEntry* entry = activeEntry();
    if (!entry) {
        result.failureReason = QStringLiteral(
            "Enter an Insight Focus View first.");
        return result;
    }

    const QString& route = descriptor.executionRoute;
    if (route
        == QStringLiteral("insight.graphView.fit")
        && entry->registration.fit) {
        entry->registration.fit();
        result.succeeded = true;
    } else if (route
                   == QStringLiteral(
                       "insight.graphView.zoomOut")
               && entry->registration.zoomOut) {
        entry->registration.zoomOut();
        result.succeeded = true;
    } else if (route
                   == QStringLiteral(
                       "insight.graphView.zoomIn")
               && entry->registration.zoomIn) {
        entry->registration.zoomIn();
        result.succeeded = true;
    } else {
        result.failureReason = QStringLiteral(
            "Action is not available for the focused Insight panel.");
    }
    return result;
}
