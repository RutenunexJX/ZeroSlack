#ifndef STATETRANSITIONTRIGGERSERVICE_H
#define STATETRANSITIONTRIGGERSERVICE_H

#include <QString>
#include <memory>
#include <functional>

class SemanticIndex;
struct SemanticSnapshotToken;
struct FsmGraphReport;

struct StateTransitionTriggerQuery {
    QString symbolName;
    QString fileName;
    QString moduleName;
    std::function<bool()> isCancelled;
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
    explicit StateTransitionTriggerService(SemanticSnapshotToken snapshot);
    static StateTransitionTriggerService* getInstance();

    explicit StateTransitionTriggerService(SemanticIndex* semanticIndex = nullptr);
    ~StateTransitionTriggerService();

    void setSemanticIndex(SemanticIndex* semanticIndex);

    StateTransitionTriggerReport triggerForSymbol(
        const StateTransitionTriggerQuery& query,
        const FsmGraphReport* graphReport = nullptr) const;

private:
    explicit StateTransitionTriggerService(std::shared_ptr<SemanticIndex> owner);
    std::shared_ptr<SemanticIndex> ownedIndex;
    SemanticIndex* index = nullptr;
    static std::unique_ptr<StateTransitionTriggerService> instance;
};

#endif // STATETRANSITIONTRIGGERSERVICE_H
