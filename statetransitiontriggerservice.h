#ifndef STATETRANSITIONTRIGGERSERVICE_H
#define STATETRANSITIONTRIGGERSERVICE_H

#include <QString>
#include <memory>

class SemanticIndex;

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

    void setSemanticIndex(SemanticIndex* semanticIndex);

    StateTransitionTriggerReport triggerForSymbol(
        const StateTransitionTriggerQuery& query) const;

private:
    SemanticIndex* index = nullptr;
    static std::unique_ptr<StateTransitionTriggerService> instance;
};

#endif // STATETRANSITIONTRIGGERSERVICE_H
