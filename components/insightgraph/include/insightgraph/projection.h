#pragma once
#include <insightgraph/graph.h>
#include <insightgraph/source.h>

namespace insightgraph {
struct FsmStateData {
    QString stateDisplayName, detailDisplayName, statusDisplayName, stableId;
    bool deadEndState = false;
    RtlInsightCodeLink codeLink;
};
struct FsmTransitionData {
    QString fromStateDisplayName, toStateDisplayName, conditionDisplayName;
    QString assignmentTargetDisplayName, fromStableId, toStableId;
    RtlInsightCodeLink codeLink;
};
struct FsmGraphData {
    QList<FsmStateData> stateRows;
    QList<FsmTransitionData> transitionRows;
    QString stateRegisterDisplayName, initialStateDisplayName;
};
struct HierarchyNodeData {
    int nodeId = -1, parentNodeId = -1, depth = 0;
    QString definitionSymbolId, moduleDisplayName, instanceDisplayName, moduleTypeDisplayName;
    bool unresolved = false;
    RtlInsightCodeLink definitionCodeLink;
};
struct HierarchyEdgeData {
    int fromNodeId = -1, toNodeId = -1;
    QString childInstanceDisplayName;
    bool unresolved = false;
};
struct HierarchyData {
    bool found = false;
    QString rootModule, notFoundReason;
    QList<HierarchyNodeData> nodes;
    QList<HierarchyEdgeData> edges;
};
// Only project supplied facts. Semantic records and navigation ownership stay
// with the producer; this layer neither resolves nor classifies RTL symbols.
void appendFsmGraph(InsightGraphDraft& draft, const FsmGraphData& graph,
                    const QString& context);
InsightGraphDraft projectHierarchy(const HierarchyData& report);
}
