#include "insightgraphadapter.h"
#include "insightgraphcore.h"

namespace InsightGraphAdapter {
namespace {
QString stableId(const SymbolStableKey& key)
{ return key.isValid() ? InsightGraphCore::stableSymbolId(key) : QString(); }
}
insightgraph::FsmGraphData fsm(const FsmGraph& graph)
{
    insightgraph::FsmGraphData result;
    result.stateRegisterDisplayName = graph.stateRegisterDisplayName;
    result.initialStateDisplayName = graph.initialStateDisplayName;
    for (const auto& row : graph.stateRows)
        result.stateRows.append({row.stateDisplayName, row.detailDisplayName, row.statusDisplayName,
            stableId(row.stateStableKey), row.deadEndState, row.codeLink});
    for (const auto& row : graph.transitionRows)
        result.transitionRows.append({row.fromStateDisplayName, row.toStateDisplayName,
            row.conditionDisplayName, row.assignmentTargetDisplayName, stableId(row.fromStateStableKey),
            stableId(row.toStateStableKey), row.codeLink});
    return result;
}
insightgraph::HierarchyData hierarchy(const ModuleBlockDiagramReport& report)
{
    insightgraph::HierarchyData result;
    result.found = report.found;
    result.rootModule = report.root.moduleDisplayName;
    result.notFoundReason = report.notFoundReasonDisplayName;
    for (const auto& node : report.nodes)
        result.nodes.append({node.nodeId, node.parentNodeId, node.depth, stableId(node.moduleStableKey),
            node.moduleDisplayName, node.instanceDisplayName, node.moduleTypeDisplayName,
            node.unresolved, node.definitionCodeLink});
    for (const auto& edge : report.edges)
        result.edges.append({edge.fromNodeId, edge.toNodeId, edge.childInstanceDisplayName, edge.unresolved});
    return result;
}
QList<insightgraph::HeatEvidence> heat(const QList<SignalTraceEvidence>& evidence)
{
    static_assert(int(SignalTraceRole::Write) == int(insightgraph::HeatRole::Write));
    static_assert(int(SignalTraceRole::Read) == int(insightgraph::HeatRole::Read));
    static_assert(int(SignalTraceRole::Port) == int(insightgraph::HeatRole::Port));
    static_assert(int(SignalTraceRole::Control) == int(insightgraph::HeatRole::Control));
    static_assert(int(SignalTraceRole::Clock) == int(insightgraph::HeatRole::Clock));
    static_assert(int(SignalTraceRole::Unknown) == int(insightgraph::HeatRole::Unknown));
    QList<insightgraph::HeatEvidence> result;
    result.reserve(evidence.size());
    for (const auto& item : evidence)
        result.append({item.id, item.range.fileName, item.moduleName, item.text,
            SignalTraceService::roleName(item.role), static_cast<insightgraph::HeatRole>(item.role),
            item.sourceHash, item.fileLineCount, item.range.line});
    return result;
}
insightgraph::TraceStatus status(SignalTraceStatus value)
{
    switch (value) {
    case SignalTraceStatus::Ready: return insightgraph::TraceStatus::Ready;
    case SignalTraceStatus::DefinitionOnly: return insightgraph::TraceStatus::DefinitionOnly;
    case SignalTraceStatus::Unavailable: return insightgraph::TraceStatus::Unavailable;
    case SignalTraceStatus::Cancelled: return insightgraph::TraceStatus::Cancelled;
    case SignalTraceStatus::Partial: return insightgraph::TraceStatus::Partial;
    case SignalTraceStatus::Stale: return insightgraph::TraceStatus::Stale;
    }
    return insightgraph::TraceStatus::Unavailable;
}
insightgraph::TraceProjection tracePage(const SignalTracePage& page, const QString& parentPath)
{
    insightgraph::TracePageData data;
    data.subjectId = page.subject.id();
    data.direction = static_cast<int>(page.direction);
    data.semanticRevision = page.semanticRevision;
    data.status = status(page.status);
    data.limitations = page.limitations;
    data.nextCursor = page.nextCursor;
    data.scanned = page.scanned;
    for (const auto& group : page.groups) data.groups.append({group.object.id(), group.dataPath});
    return insightgraph::projectTracePage(data, parentPath);
}
QList<insightgraph::FeedbackSummary> feedback(const SignalFeedbackReport& report)
{
    QList<insightgraph::FeedbackFindingData> data;
    for (const auto& finding : report.findings) {
        insightgraph::FeedbackFindingData row;
        row.id = finding.id;
        switch (finding.kind) {
        case SignalFeedbackKind::Combinational: row.kind = insightgraph::FeedbackKind::Combinational; break;
        case SignalFeedbackKind::Sequential: row.kind = insightgraph::FeedbackKind::Sequential; break;
        case SignalFeedbackKind::Uncertain: row.kind = insightgraph::FeedbackKind::Uncertain; break;
        }
        row.kindDescription = SignalFeedbackService::kindName(finding.kind);
        row.limitations = finding.limitations;
        for (const auto& edge : finding.edges) row.fromDisplayNames.append(edge.from.displayName());
        data.append(row);
    }
    return insightgraph::projectFeedback(data);
}
} // namespace InsightGraphAdapter
