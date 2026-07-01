#include "statetransitiongraphservice.h"

std::unique_ptr<StateTransitionGraphService>
    StateTransitionGraphService::instance = nullptr;

namespace {
QString stateTransitionNotFoundReasonDisplayName(
    StateTransitionGraphNotFoundReason reason,
    const QString& detail = QString())
{
    switch (reason) {
    case StateTransitionGraphNotFoundReason::None:
        return QString();
    case StateTransitionGraphNotFoundReason::TriggerRejected:
        return detail.isEmpty()
            ? QStringLiteral("state transition trigger rejected")
            : detail;
    case StateTransitionGraphNotFoundReason::NoFsmGraph:
        return detail.isEmpty()
            ? QStringLiteral("no FSM graph")
            : detail;
    case StateTransitionGraphNotFoundReason::NoMatchingNextStateSignal:
        return detail.isEmpty()
            ? QStringLiteral("no matching next-state signal")
            : detail;
    }
    return QStringLiteral("state transition graph unavailable");
}

bool graphMatchesTrigger(const FsmGraph& graph, const QString& symbolName)
{
    return !symbolName.isEmpty()
        && graph.nextStateSignalRecord.isValid()
        && graph.nextStateSignalRecord.name == symbolName;
}
}

StateTransitionGraphService* StateTransitionGraphService::getInstance()
{
    if (!instance)
        instance = std::make_unique<StateTransitionGraphService>();
    return instance.get();
}

StateTransitionGraphService::StateTransitionGraphService(
    SemanticIndex* semanticIndex,
    StateTransitionTriggerService* triggerService)
    : fsmGraphService(semanticIndex)
    , trigger(triggerService)
{
    this->triggerService()->setSemanticIndex(semanticIndex);
}

StateTransitionGraphService::~StateTransitionGraphService() = default;

void StateTransitionGraphService::setSemanticIndex(SemanticIndex* semanticIndex)
{
    fsmGraphService.setSemanticIndex(semanticIndex);
    triggerService()->setSemanticIndex(semanticIndex);
}

void StateTransitionGraphService::setTriggerService(
    StateTransitionTriggerService* triggerService)
{
    trigger = triggerService;
}

StateTransitionGraphReport
StateTransitionGraphService::buildStateTransitionGraph(
    const StateTransitionGraphQuery& query) const
{
    StateTransitionGraphReport report;
    report.groupDisplayName = QStringLiteral("State Transition Graph");

    StateTransitionTriggerQuery triggerQuery;
    triggerQuery.symbolName = query.symbolName;
    triggerQuery.fileName = query.fileName;
    triggerQuery.moduleName = query.moduleName;
    report.trigger = triggerService()->triggerForSymbol(triggerQuery);
    if (!report.trigger.available) {
        report.notFoundReason =
            StateTransitionGraphNotFoundReason::TriggerRejected;
        report.notFoundReasonDisplayName =
            stateTransitionNotFoundReasonDisplayName(
                report.notFoundReason,
                report.trigger.reasonDisplayName);
        return report;
    }

    FsmGraphQuery fsmQuery;
    fsmQuery.fileName = report.trigger.fileName;
    fsmQuery.moduleName = report.trigger.moduleName;
    const FsmGraphReport fsmReport = fsmGraphService.buildFsmGraph(fsmQuery);
    if (!fsmReport.found) {
        report.notFoundReason = StateTransitionGraphNotFoundReason::NoFsmGraph;
        report.notFoundReasonDisplayName =
            stateTransitionNotFoundReasonDisplayName(
                report.notFoundReason,
                fsmReport.notFoundReasonDisplayName);
        return report;
    }

    for (const FsmGraph& graph : fsmReport.graphs) {
        if (!graphMatchesTrigger(graph, report.trigger.symbolName))
            continue;
        report.found = true;
        report.notFoundReason = StateTransitionGraphNotFoundReason::None;
        report.graph = graph;
        report.selectedSignalDisplayName = graph.nextStateSignalDisplayName;
        report.moduleDisplayName = graph.moduleDisplayName;
        report.stateCount = graph.stateCount;
        report.transitionCount = graph.transitionCount;
        return report;
    }

    report.notFoundReason =
        StateTransitionGraphNotFoundReason::NoMatchingNextStateSignal;
    report.notFoundReasonDisplayName =
        stateTransitionNotFoundReasonDisplayName(
            report.notFoundReason,
            QStringLiteral("no state transition graph for %1")
                .arg(report.trigger.symbolName));
    return report;
}

StateTransitionTriggerService*
StateTransitionGraphService::triggerService() const
{
    return trigger ? trigger : StateTransitionTriggerService::getInstance();
}
