#ifndef STATETRANSITIONGRAPHSERVICE_H
#define STATETRANSITIONGRAPHSERVICE_H

#include "fsmgraphservice.h"
#include "statetransitiontriggerservice.h"

#include <QString>
#include <memory>

class SemanticIndex;

struct StateTransitionGraphQuery {
    QString symbolName;
    QString fileName;
    QString moduleName;
};

enum class StateTransitionGraphNotFoundReason {
    None,
    TriggerRejected,
    NoFsmGraph,
    NoMatchingNextStateSignal
};

struct StateTransitionGraphReport {
    bool found = false;
    StateTransitionGraphNotFoundReason notFoundReason =
        StateTransitionGraphNotFoundReason::None;
    QString groupDisplayName;
    QString notFoundReasonDisplayName;
    QString selectedSignalDisplayName;
    QString moduleDisplayName;
    int stateCount = 0;
    int transitionCount = 0;
    StateTransitionTriggerReport trigger;
    FsmGraph graph;
};

class StateTransitionGraphService
{
public:
    static StateTransitionGraphService* getInstance();

    explicit StateTransitionGraphService(
        SemanticIndex* semanticIndex = nullptr,
        StateTransitionTriggerService* triggerService = nullptr);
    ~StateTransitionGraphService();

    void setSemanticIndex(SemanticIndex* semanticIndex);

    StateTransitionGraphReport buildStateTransitionGraph(
        const StateTransitionGraphQuery& query) const;

private:
    FsmGraphService fsmGraphService;
    StateTransitionTriggerService* trigger = nullptr;
    static std::unique_ptr<StateTransitionGraphService> instance;

    StateTransitionTriggerService* triggerService() const;
};

#endif // STATETRANSITIONGRAPHSERVICE_H
