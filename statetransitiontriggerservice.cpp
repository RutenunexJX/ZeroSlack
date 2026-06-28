#include "statetransitiontriggerservice.h"

std::unique_ptr<StateTransitionTriggerService>
    StateTransitionTriggerService::instance = nullptr;

namespace {
bool isAllowedNextStateName(const QString& symbolName)
{
    const QString lower = symbolName.toLower();
    return lower == QStringLiteral("ns")
        || lower == QStringLiteral("next_state")
        || lower.endsWith(QStringLiteral("_ns"))
        || lower.endsWith(QStringLiteral("_next_state"));
}

bool isRejectedCurrentStateName(const QString& symbolName)
{
    const QString lower = symbolName.toLower();
    return lower == QStringLiteral("cs")
        || lower == QStringLiteral("current_state")
        || lower.endsWith(QStringLiteral("_cs"))
        || lower.endsWith(QStringLiteral("_current_state"));
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
            isRejectedCurrentStateName(query.symbolName)
                ? QStringLiteral("State transition graph requires next-state signal, not current-state signal")
                : QStringLiteral("State transition graph requires ns/next_state or *_ns/*_next_state");
        return report;
    }

    report.available = true;
    return report;
}
