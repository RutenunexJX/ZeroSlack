#include "statetransitiontriggerservice.h"

#include "fsmgraphservice.h"
#include "semanticindex.h"

std::unique_ptr<StateTransitionTriggerService>
    StateTransitionTriggerService::instance = nullptr;

StateTransitionTriggerService* StateTransitionTriggerService::getInstance()
{
    if (!instance)
        instance = std::make_unique<StateTransitionTriggerService>();
    return instance.get();
}

StateTransitionTriggerService::StateTransitionTriggerService(SemanticIndex* semanticIndex)
    : index(semanticIndex ? semanticIndex : SemanticIndex::getInstance())
{
}

StateTransitionTriggerService::~StateTransitionTriggerService() = default;

void StateTransitionTriggerService::setSemanticIndex(
    SemanticIndex* semanticIndex)
{
    index = semanticIndex ? semanticIndex : SemanticIndex::getInstance();
}

StateTransitionTriggerReport
StateTransitionTriggerService::triggerForSymbol(
    const StateTransitionTriggerQuery& query,
    const FsmGraphReport* graphReport) const
{
    StateTransitionTriggerReport report;
    report.symbolName = query.symbolName;
    report.fileName = query.fileName;
    report.moduleName = query.moduleName;

    if (query.fileName.isEmpty() || query.moduleName.isEmpty()) {
        report.reasonDisplayName =
            QStringLiteral("No module context for state transition graph");
        return report;
    }

    FsmGraphService fsmService(index);
    FsmGraphQuery fsmQuery;
    fsmQuery.isCancelled = query.isCancelled;
    fsmQuery.fileName = query.fileName;
    fsmQuery.moduleName = query.moduleName;
    const FsmSymbolRoleReport role = graphReport
        ? FsmGraphService::roleForSymbol(*graphReport, query.symbolName)
        : fsmService.probeRoleForSymbol(fsmQuery, query.symbolName);
    if (role.role == FsmSymbolRole::NextState) {
        report.available = true;
        return report;
    }
    if (role.role == FsmSymbolRole::CurrentState) {
        report.reasonDisplayName =
            QStringLiteral("Please select the next-state signal for this FSM");
        return report;
    }

    report.reasonDisplayName =
        role.reasonDisplayName.isEmpty()
            ? QStringLiteral("Selected symbol is not part of a discovered FSM")
            : role.reasonDisplayName;
    return report;
}

StateTransitionTriggerService::StateTransitionTriggerService(SemanticSnapshotToken snapshot)
    : StateTransitionTriggerService(std::make_shared<SemanticIndex>(std::move(snapshot))) {}

StateTransitionTriggerService::StateTransitionTriggerService(std::shared_ptr<SemanticIndex> owner)
    : StateTransitionTriggerService(owner.get())
{
    ownedIndex = std::move(owner);
}
