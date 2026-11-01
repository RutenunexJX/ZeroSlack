#include <insightgraph/projection.h>

namespace insightgraph {
namespace {
QString nodeIdOrFallback(const QString& id, const QString& domain,
    const QString& context, const QString& identity)
{ return id.isEmpty() ? GraphStore::stableSyntheticId(domain, context, identity) : id; }
}
void appendFsmGraph(InsightGraphDraft& draft,
                    const FsmGraphData& graph,
                    const QString& context)
{
    QHash<QString, QString> stateIds;
    for (const FsmStateData& row : graph.stateRows) {
        InsightGraphNode node;
        node.nodeId = nodeIdOrFallback(
            row.stableId,
            QStringLiteral("fsm-state"),
            context,
            row.stateDisplayName);
        node.displayName = row.stateDisplayName;
        node.detail = row.detailDisplayName;
        node.sourceFile = row.codeLink.fileName;
        node.sourceLine = row.codeLink.line;
        node.domain = InsightGraphDomain::Fsm;
        node.attributes.insert(QStringLiteral("status"), row.statusDisplayName);
        node.attributes.insert(QStringLiteral("deadEnd"), row.deadEndState);
        node.attributes.insert(
            QStringLiteral("initial"),
            row.stateDisplayName == graph.initialStateDisplayName);
        draft.nodes.append(node);
        stateIds.insert(row.stateDisplayName, node.nodeId);
    }
    int transitionIndex = 0;
    for (const FsmTransitionData& row : graph.transitionRows) {
        const QString fromId = stateIds.value(
            row.fromStateDisplayName,
            nodeIdOrFallback(
                row.fromStableId,
                QStringLiteral("fsm-state"),
                context,
                row.fromStateDisplayName));
        const QString toId = stateIds.value(
            row.toStateDisplayName,
            nodeIdOrFallback(
                row.toStableId,
                QStringLiteral("fsm-state"),
                context,
                row.toStateDisplayName));
        InsightGraphEdge edge;
        edge.fromNodeId = fromId;
        edge.toNodeId = toId;
        edge.displayName = row.conditionDisplayName;
        edge.domain = InsightGraphDomain::Fsm;
        edge.edgeId = GraphStore::stableEdgeId(
            fromId,
            toId,
            QStringLiteral("transition"),
            QStringLiteral("%1|%2|%3")
                .arg(row.conditionDisplayName,
                     row.assignmentTargetDisplayName)
                .arg(transitionIndex++));
        edge.attributes.insert(QStringLiteral("sourceFile"), row.codeLink.fileName);
        edge.attributes.insert(QStringLiteral("sourceLine"), row.codeLink.line);
        draft.edges.append(edge);
    }
}

InsightGraphDraft projectHierarchy(
    const HierarchyData& report)
{
    InsightGraphDraft draft;
    draft.layoutHint = QStringLiteral("block");
    draft.contextKey = report.rootModule;
    draft.metadata.insert(QStringLiteral("found"), report.found);
    draft.metadata.insert(
        QStringLiteral("notFoundReason"), report.notFoundReason);
    QHash<int, QString> ids;
    for (const HierarchyNodeData& source : report.nodes) {
        InsightGraphNode node;
        const QString definitionSymbolId = nodeIdOrFallback(
            source.definitionSymbolId,
            QStringLiteral("module-definition"),
            report.rootModule,
            source.moduleDisplayName);
        const QString parentInstanceId = source.parentNodeId < 0
            ? QStringLiteral("root")
            : ids.value(source.parentNodeId,
                        QStringLiteral("parent:%1")
                            .arg(source.parentNodeId));
        const QString instanceIdentity =
            source.instanceDisplayName.trimmed().isEmpty()
            ? source.moduleDisplayName
            : source.instanceDisplayName;
        node.nodeId = GraphStore::stableSyntheticId(
            QStringLiteral("module-instance"),
            parentInstanceId,
            QStringLiteral("%1|%2")
                .arg(instanceIdentity, definitionSymbolId));
        node.displayName = source.instanceDisplayName.trimmed().isEmpty()
            ? source.moduleDisplayName : source.instanceDisplayName;
        node.detail = source.moduleTypeDisplayName;
        node.sourceFile = source.definitionCodeLink.fileName;
        node.sourceLine = source.definitionCodeLink.line;
        node.domain = InsightGraphDomain::Hierarchy;
        node.attributes.insert(QStringLiteral("depth"), source.depth);
        node.attributes.insert(QStringLiteral("unresolved"), source.unresolved);
        node.attributes.insert(QStringLiteral("module"), source.moduleDisplayName);
        node.attributes.insert(
            QStringLiteral("definitionSymbolId"), definitionSymbolId);
        ids.insert(source.nodeId, node.nodeId);
        draft.nodes.append(node);
    }
    int edgeIndex = 0;
    for (const HierarchyEdgeData& source : report.edges) {
        InsightGraphEdge edge;
        edge.fromNodeId = ids.value(source.fromNodeId);
        edge.toNodeId = ids.value(source.toNodeId);
        edge.displayName = source.childInstanceDisplayName;
        edge.domain = InsightGraphDomain::Connection;
        edge.edgeId = GraphStore::stableEdgeId(
            edge.fromNodeId,
            edge.toNodeId,
            QStringLiteral("instantiates"),
            QString::number(edgeIndex++));
        edge.attributes.insert(QStringLiteral("unresolved"), source.unresolved);
        edge.attributes.insert(
            QStringLiteral("relationship"), QStringLiteral("instantiates"));
        draft.edges.append(edge);
    }
    return draft;
}
}
