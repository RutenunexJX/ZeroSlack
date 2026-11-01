#include <insightgraph/fsm_layout.h>
#include <insightgraph/heatmap.h>
#include <insightgraph/trace.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>
#include <limits>

int main()
{
    int failures = 0;
    const auto check = [&](bool value, const char* name) {
        std::printf("[%s] %s\n", value ? "PASS" : "FAIL", name); failures += !value;
    };
    insightgraph::GraphStore store;
    InsightGraphDraft draft;
    draft.workspaceId = "workspace"; draft.documentId = "document"; draft.contextKey = "instance.signal";
    draft.documentRevision = std::numeric_limits<quint64>::max();
    draft.semanticRevision = (Q_UINT64_C(1) << 63) + 17;
    InsightGraphNode node;node.nodeId = "anchor";node.sourceFile = "C:\\source/../literal.sv";node.sourceLine = 27;
    draft.nodes.append(node);
    const auto first = store.update("state", draft);
    check(first.snapshot.documentRevision == draft.documentRevision && first.snapshot.semanticRevision == draft.semanticRevision,
        "full-width revisions survive native publication");
    check(first.snapshot.nodes["anchor"].sourceFile == node.sourceFile, "source anchor path is not normalized");
    const auto second = store.update("state", draft);
    check(second.snapshot.generation == 2 && second.diff.isEmpty()
        && first.snapshot.contentFingerprint == second.snapshot.contentFingerprint, "same facts keep fingerprint and advance generation once");
    draft.nodes[0].detail = "changed";
    check(store.update("state", draft).diff.updatedNodeIds == QStringList{"anchor"}, "diff observes replacement fact");
    store.clear("state");check(!store.snapshot("state").isValid(), "clear releases the only channel store");
    check(first.snapshot.nodes["anchor"].detail.isEmpty(), "published value remains independent after later updates");

    using namespace insightgraph;
    QList<HeatEvidence> evidence{
        {"a", "file,one.sv", "top", "read \"value\"", "Read", HeatRole::Read, "hash", 11, 1},
        {"b", "file,one.sv", "top", "write", "Write", HeatRole::Write, "hash", 11, 11},
        {"c", "unknown.sv", "top.child", "unavailable", "Unknown", HeatRole::Unknown, {}, 0, 0}};
    auto heat = projectHeatmap(evidence, {}, 63, File, Qt::AscendingOrder);
    check(heat.rows.size() == 2 && heat.maximum == 1 && heat.rows[0].counts[1] == 1
        && heat.rows[0].items == QList<int>{0,1}, "heatmap preserves role counts and original evidence indices");
    check(!heat.rows[1].available && heatCsv(heat, false).contains("Partial: loaded reference pages only")
        && heatCsv(heat, false).contains(QStringLiteral("—").toUtf8()), "unknown source and partial coverage are explicit");
    heat = projectHeatmap(evidence, "Read", 2, Total, Qt::DescendingOrder);
    check(heat.rows.size() == 1 && heat.rows[0].items == QList<int>{0}, "role and text filtering retain stable evidence");
    check(distribution(evidence) == QList<qreal>{0,1}, "distribution endpoints and unavailable file remain distinct");
    check(heatCsv(heat, true).contains("\"file,one.sv\""), "CSV quotes exact source file");

    TracePageData page;
    page.subjectId = "project/instance/signal[7:0]";
    page.semanticRevision = std::numeric_limits<quint64>::max();page.status = TraceStatus::Partial;
    page.groups = {{"member/one", true}, {"member/two", false}};
    page.nextCursor = 200;page.scanned = 100;page.limitations = {"not loaded"};
    const auto projection = projectTracePage(page, "0/root");
    check(projection.page.semanticRevision == page.semanticRevision && projection.page.nextCursor == 200
        && projection.page.limitations == page.limitations, "trace publication preserves revision, unloaded page and reasons");
    check(projection.branches[0].path == "0/root/member/one/data"
        && projection.branches[1].path == "0/root/member/two/related", "trace members keep exact path, order and class");
    check(traceStatusName(TraceStatus::Cancelled) != traceStatusName(TraceStatus::Unavailable)
        && traceStatusName(TraceStatus::Stale) != traceStatusName(TraceStatus::Partial), "cancelled stale unavailable partial are distinct");
    QList<FeedbackFindingData> findings;
    for (const auto kind : {FeedbackKind::Combinational, FeedbackKind::Sequential, FeedbackKind::Uncertain})
        findings.append({QString::number(int(kind)), kind, "producer evidence", {"a", "b", "a"}, {"producer limitation"}});
    const auto summaries = projectFeedback(findings);
    check(summaries.size() == 3 && summaries[0].label == QStringLiteral("Combinational — a → b")
        && summaries[1].label.startsWith("Sequential") && summaries[2].label.startsWith("Uncertain"), "feedback consumes all three producer classifications");
    check(summaries[2].toolTip == "producer evidence\nproducer limitation", "feedback limitations preserved without reclassification");

    HierarchyData hierarchy;hierarchy.found = true;hierarchy.rootModule = "top";
    hierarchy.nodes = {{0,-1,0,"def:top","top","top","top",false,{"top.sv",1,0,{},{}}},
        {1,0,1,"def:leaf","leaf","left","leaf",false,{"leaf.sv",4,0,{},{}}},
        {2,0,1,"def:leaf","leaf","right","leaf",false,{"leaf.sv",4,0,{},{}}}};
    hierarchy.edges = {{0,1,"left",false},{0,2,"right",false}};
    const auto tree = projectHierarchy(hierarchy);
    check(tree.nodes.size() == 3 && tree.edges.size() == 2 && tree.nodes[1].nodeId != tree.nodes[2].nodeId,
        "hierarchy retains distinct repeated-module instances");
    check(tree.nodes[1].attributes["definitionSymbolId"].toString() == "def:leaf", "hierarchy retains definition identity");
    return failures ? 1 : 0;
}
