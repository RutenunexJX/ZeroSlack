#ifndef SIGNALKERNELGRAPHSERVICE_H
#define SIGNALKERNELGRAPHSERVICE_H

#include "rtlinsightlink.h"
#include "semanticindex.h"
#include "signaljourneyservice.h"

#include <QList>
#include <QString>
#include <memory>

enum class SignalKernelGraphNodeRole {
    Kernel,
    Input,
    Output
};

enum class SignalKernelGraphInputLane {
    Data,
    Control,
    Timing
};

enum class SignalKernelGraphNotFoundReason {
    None,
    SignalJourneyUnavailable
};

struct SignalKernelGraphQuery {
    SymbolStableKey signalStableKey;
    QString signalName;
    QString signalAccessPath;
    QString fileName;
    QString moduleName;
};

struct SignalKernelGraphNode {
    int id = -1;
    SignalKernelGraphNodeRole role = SignalKernelGraphNodeRole::Kernel;
    SignalKernelGraphInputLane inputLane = SignalKernelGraphInputLane::Data;
    QString displayName;
    QString detailDisplayName;
    QString moduleDisplayName;
    QString typeDisplayName;
    QString sourceRoleDisplayName;
    SymbolStableKey stableKey;
    SemanticSymbolRecord symbolRecord;
    RtlInsightCodeLink declarationCodeLink;
    RtlInsightCodeLink navigateCodeLink;
    RtlInsightCodeLink previewCodeLink;
    SemanticSourceRange evidenceRange;
    bool preciseEvidence = false;
    bool crossModule = false;
};

struct SignalKernelGraphEdge {
    int fromNodeId = -1;
    int toNodeId = -1;
    QString label;
};

struct SignalKernelGraphModuleGroup {
    QString moduleName;
    QList<int> nodeIds;
    bool crossModule = false;
};

struct SignalKernelGraphFanoutGroup {
    int id = -1;
    SignalKernelGraphNodeRole role = SignalKernelGraphNodeRole::Output;
    SignalKernelGraphInputLane inputLane = SignalKernelGraphInputLane::Data;
    QString groupKey;
    QString displayName;
    QString moduleName;
    QList<int> nodeIds;
    int nodeCount = 0;
    int totalRoleNodeCount = 0;
    bool crossModule = false;
    bool highFanout = false;
};

struct SignalKernelGraphReport {
    bool found = false;
    SignalKernelGraphNotFoundReason notFoundReason =
        SignalKernelGraphNotFoundReason::None;
    QString notFoundReasonDisplayName;
    QString kernelModuleName;
    SignalKernelGraphNode kernel;
    QList<SignalKernelGraphNode> inputs;
    QList<SignalKernelGraphNode> outputs;
    QList<SignalKernelGraphEdge> edges;
    QList<SignalKernelGraphModuleGroup> inputModuleGroups;
    QList<SignalKernelGraphModuleGroup> outputModuleGroups;
    int fanoutGroupingThreshold = 0;
    QList<SignalKernelGraphFanoutGroup> inputFanoutGroups;
    QList<SignalKernelGraphFanoutGroup> outputFanoutGroups;
};

class SignalKernelGraphService
{
public:
    static SignalKernelGraphService* getInstance();

    explicit SignalKernelGraphService(SemanticIndex* semanticIndex = nullptr);
    ~SignalKernelGraphService();

    void setSemanticIndex(SemanticIndex* semanticIndex);

    SignalKernelGraphReport buildSignalKernelGraph(
        const SignalKernelGraphQuery& query) const;

    static QString nodeRoleDisplayName(SignalKernelGraphNodeRole role);
    static QString inputLaneDisplayName(SignalKernelGraphInputLane lane);

private:
    SemanticIndex* index = nullptr;
    static std::unique_ptr<SignalKernelGraphService> instance;

    SemanticIndex* semanticIndex() const;
};

#endif // SIGNALKERNELGRAPHSERVICE_H
