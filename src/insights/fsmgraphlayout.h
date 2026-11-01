#pragma once
#include "fsmgraphservice.h"
#include <insightgraph/fsm_layout.h>

FsmGraphLayout layoutFsmGraph(const FsmGraph& graph,
                             const FsmLayoutOptions& options = {});
