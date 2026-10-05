#ifndef STATETRANSITIONGRAPHSERVICE_H
#define STATETRANSITIONGRAPHSERVICE_H

#include <zeroslack/semantic/fsmgraphservice.h>
#include <zeroslack/semantic/statetransitiontriggerservice.h>

#include <QString>
#include <memory>
#include <functional>

class SemanticIndex;

struct StateTransitionGraphQuery {
    QString symbolName;
    QString fileName;
    QString moduleName;
    std::function<bool()> isCancelled;
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
    explicit StateTransitionGraphService(SemanticSnapshotToken snapshot);
    static StateTransitionGraphService* getInstance();

    explicit StateTransitionGraphService(
        SemanticIndex* semanticIndex = nullptr,
        StateTransitionTriggerService* triggerService = nullptr);
    ~StateTransitionGraphService();

    void setSemanticIndex(SemanticIndex* semanticIndex);

    StateTransitionGraphReport buildStateTransitionGraph(
        const StateTransitionGraphQuery& query) const;

private:
    explicit StateTransitionGraphService(std::shared_ptr<SemanticIndex> owner);
    std::shared_ptr<SemanticIndex> ownedIndex;
    FsmGraphService fsmGraphService;
    StateTransitionTriggerService ownedTrigger;
    StateTransitionTriggerService* trigger = nullptr;
    static std::unique_ptr<StateTransitionGraphService> instance;

    StateTransitionTriggerService* triggerService() const;
};

#endif // STATETRANSITIONGRAPHSERVICE_H
