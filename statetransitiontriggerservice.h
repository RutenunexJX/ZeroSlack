#ifndef STATETRANSITIONTRIGGERSERVICE_H
#define STATETRANSITIONTRIGGERSERVICE_H

#include <QString>
#include <memory>

struct StateTransitionTriggerQuery {
    QString symbolName;
    QString fileName;
    QString moduleName;
};

struct StateTransitionTriggerReport {
    bool available = false;
    QString symbolName;
    QString fileName;
    QString moduleName;
    QString reasonDisplayName;
};

class StateTransitionTriggerService
{
public:
    static StateTransitionTriggerService* getInstance();

    StateTransitionTriggerService();
    ~StateTransitionTriggerService();

    StateTransitionTriggerReport triggerForSymbol(
        const StateTransitionTriggerQuery& query) const;

private:
    static std::unique_ptr<StateTransitionTriggerService> instance;
};

#endif // STATETRANSITIONTRIGGERSERVICE_H
