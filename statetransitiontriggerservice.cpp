#include "statetransitiontriggerservice.h"

std::unique_ptr<StateTransitionTriggerService>
    StateTransitionTriggerService::instance = nullptr;

namespace {
bool isAllowedNextStateName(const QString& symbolName)
{
    return symbolName == QStringLiteral("ns")
        || symbolName == QStringLiteral("next_state");
}
}

StateTransitionTriggerService* StateTransitionTriggerService::getInstance()
{
    if (!instance)
        instance = std::make_unique<StateTransitionTriggerService>();
    return instance.get();
}

StateTransitionTriggerService::StateTransitionTriggerService() = default;

StateTransitionTriggerService::~StateTransitionTriggerService() = default;

StateTransitionTriggerReport
StateTransitionTriggerService::triggerForSymbol(
    const StateTransitionTriggerQuery& query) const
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

    if (!isAllowedNextStateName(query.symbolName)) {
        report.reasonDisplayName =
            QStringLiteral("State transition graph requires ns or next_state");
        return report;
    }

    report.available = true;
    return report;
}
