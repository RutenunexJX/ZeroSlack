// This executable obtains all headers/symbols solely from insightgraph::core.
#include <insightgraph/source.h>
#include <insightgraph/graph.h>
#include <insightgraph/projection.h>
#include <insightgraph/fsm_layout.h>
#include <insightgraph/heatmap.h>
#include <insightgraph/trace.h>
#include <cstdio>

int main()
{
    insightgraph::FsmGraphData graph;
    graph.stateRows.append({"idle", "state_q", "", "", false, {"opaque/../source.sv", 9, 3, "source.sv", "9"}});
    graph.transitionRows.append({"idle", "idle", "enable", "state_q", "", "", {"source.sv", 17, 5, "source.sv", "17"}});
    const auto layout = insightgraph::layoutFsmGraph(graph);
    if (layout.nodes.size() != 1 || layout.edges.size() != 1 || !layout.edges.first().selfLoop
        || layout.nodes.first().codeLink.fileName != "opaque/../source.sv") return 1;
    insightgraph::GraphStore store;
    InsightGraphDraft draft;
    draft.documentRevision = Q_UINT64_C(18446744073709551614);
    insightgraph::appendFsmGraph(draft, graph, "instance/state_q");
    const auto update = store.update("state", draft);
    if (update.snapshot.documentRevision != draft.documentRevision || update.snapshot.nodes.size() != 1) return 2;
    std::puts("PASS: component-only headers, layout, projection, store; no application/GUI initialization");
    return 0;
}
