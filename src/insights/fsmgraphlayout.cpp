#include "fsmgraphlayout.h"
#include "insightgraphadapter.h"

FsmGraphLayout layoutFsmGraph(const FsmGraph& graph, const FsmLayoutOptions& options)
{
    return insightgraph::layoutFsmGraph(InsightGraphAdapter::fsm(graph), options);
}
