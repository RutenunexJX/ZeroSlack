#include "insightgraphcore.h"

#include "fsmgraphservice.h"
#include "moduleblockdiagramservice.h"
#include "semanticindex.h"
#include "signalkernelgraphservice.h"
#include "signalusagehotspotservice.h"
#include "statetransitiongraphservice.h"

#include "insightgraphadapter.h"
#include <insightgraph/projection.h>

namespace {
QString nodeIdOrFallback(const SymbolStableKey& key,
                           const QString& domain,
                           const QString& context,
                           const QString& identity)
{
    return key.isValid()
        ? InsightGraphCore::stableSymbolId(key)
        : InsightGraphCore::stableSyntheticId(domain, context, identity);
}

QString hotspotRoleName(SignalUsageHotspotRole role)
{
    return SignalUsageHotspotService::roleDisplayName(role);
}


}

QString InsightGraphCore::stableSymbolId(const SymbolStableKey& key)
{
    return stableSymbolId(
        key.fileName,
        key.ownerScope,
        static_cast<int>(key.declarationKind),
        key.symbolName);
}

InsightGraphDraft InsightGraphCore::fromSignalKernelGraph(
    const SignalKernelGraphReport& report)
{
    InsightGraphDraft draft;
    draft.layoutHint = QStringLiteral("kernel");
    draft.contextKey = report.kernelModuleName + QLatin1Char('|')
        + report.kernel.displayName;
    draft.metadata.insert(QStringLiteral("found"), report.found);
    draft.metadata.insert(
        QStringLiteral("notFoundReason"), report.notFoundReasonDisplayName);
    if (!report.found)
        return draft;

    QHash<int, QString> ids;
    const auto appendNode = [&draft, &ids](const SignalKernelGraphNode& source) {
        InsightGraphNode node;
        node.nodeId = nodeIdOrFallback(
            source.stableKey,
            QStringLiteral("kernel-node"),
            source.moduleDisplayName,
            QStringLiteral("%1|%2")
                .arg(static_cast<int>(source.role))
                .arg(source.displayName));
        node.displayName = source.displayName;
        node.detail = source.detailDisplayName;
        node.sourceFile = source.navigateCodeLink.fileName;
        node.sourceLine = source.navigateCodeLink.line;
        node.domain = source.role == SignalKernelGraphNodeRole::Kernel
            ? InsightGraphDomain::Symbol
            : InsightGraphDomain::DataFlow;
        node.attributes.insert(QStringLiteral("role"), static_cast<int>(source.role));
        node.attributes.insert(QStringLiteral("module"), source.moduleDisplayName);
        node.attributes.insert(QStringLiteral("crossModule"), source.crossModule);
        ids.insert(source.id, node.nodeId);
        draft.nodes.append(node);
    };
    appendNode(report.kernel);
    for (const SignalKernelGraphNode& node : report.inputs)
        appendNode(node);
    for (const SignalKernelGraphNode& node : report.outputs)
        appendNode(node);
    int edgeIndex = 0;
    for (const SignalKernelGraphEdge& source : report.edges) {
        InsightGraphEdge edge;
        edge.fromNodeId = ids.value(source.fromNodeId);
        edge.toNodeId = ids.value(source.toNodeId);
        edge.displayName = source.label;
        edge.domain = InsightGraphDomain::DataFlow;
        edge.edgeId = stableEdgeId(
            edge.fromNodeId,
            edge.toNodeId,
            QStringLiteral("data-flow"),
            QString::number(edgeIndex++));
        draft.edges.append(edge);
    }
    return draft;
}

InsightGraphDraft InsightGraphCore::fromModuleBlockDiagram(const ModuleBlockDiagramReport& report)
{
    return insightgraph::projectHierarchy(InsightGraphAdapter::hierarchy(report));
}

InsightGraphDraft InsightGraphCore::fromSignalUsageHotspot(
    const SignalUsageHotspotReport& report)
{
    InsightGraphDraft draft;
    draft.layoutHint = QStringLiteral("hotspot");
    draft.contextKey = report.declarationDisplayName;
    draft.metadata.insert(QStringLiteral("found"), report.found);
    draft.metadata.insert(
        QStringLiteral("notFoundReason"), report.notFoundReasonDisplayName);
    if (!report.found)
        return draft;

    InsightGraphNode declaration;
    declaration.nodeId = nodeIdOrFallback(
        report.declarationStableKey,
        QStringLiteral("hotspot-root"),
        report.declarationFileDisplayName,
        report.declarationDisplayName);
    declaration.displayName = report.declarationDisplayName;
    declaration.detail = report.declarationTypeDisplayName;
    declaration.sourceFile = report.declarationCodeLink.fileName;
    declaration.sourceLine = report.declarationCodeLink.line;
    declaration.domain = InsightGraphDomain::Symbol;
    declaration.attributes.insert(QStringLiteral("root"), true);
    draft.nodes.append(declaration);

    int index = 0;
    for (const SignalUsageHotspotItem& source : report.items) {
        const QString role = hotspotRoleName(source.role);
        InsightGraphNode evidence;
        evidence.nodeId = stableSyntheticId(
            QStringLiteral("hotspot-evidence"),
            declaration.nodeId,
            QStringLiteral("%1|%2|%3|%4|%5")
                .arg(source.fileName)
                .arg(source.line)
                .arg(source.column)
                .arg(role)
                .arg(index));
        evidence.displayName = role;
        evidence.detail = source.evidenceText.trimmed().isEmpty()
            ? source.snippet : source.evidenceText;
        evidence.sourceFile = source.fileName;
        evidence.sourceLine = source.line;
        evidence.domain = InsightGraphDomain::Hotspot;
        evidence.heatScore = qBound(0, source.confidence, 100);
        evidence.attributes.insert(QStringLiteral("role"), role);
        evidence.attributes.insert(QStringLiteral("module"), source.moduleName);
        draft.nodes.append(evidence);

        InsightGraphEdge edge;
        edge.fromNodeId = source.outgoing
            ? declaration.nodeId : evidence.nodeId;
        edge.toNodeId = source.outgoing
            ? evidence.nodeId : declaration.nodeId;
        edge.displayName = source.relationshipTypeDisplayName;
        edge.domain = InsightGraphDomain::Hotspot;
        edge.weight = qMax(1, source.confidence);
        edge.edgeId = stableEdgeId(
            edge.fromNodeId,
            edge.toNodeId,
            role,
            QString::number(index++));
        draft.edges.append(edge);
    }
    return draft;
}

InsightGraphDraft InsightGraphCore::fromStateTransitionGraph(
    const StateTransitionGraphReport& report)
{
    InsightGraphDraft draft;
    draft.layoutHint = QStringLiteral("state");
    draft.contextKey = report.moduleDisplayName + QLatin1Char('|')
        + report.selectedSignalDisplayName;
    draft.metadata.insert(QStringLiteral("found"), report.found);
    draft.metadata.insert(
        QStringLiteral("notFoundReason"), report.notFoundReasonDisplayName);
    if (report.found)
        insightgraph::appendFsmGraph(draft, InsightGraphAdapter::fsm(report.graph), draft.contextKey);
    return draft;
}

InsightGraphDraft InsightGraphCore::fromFsmGraph(const FsmGraphReport& report)
{
    InsightGraphDraft draft;
    draft.layoutHint = QStringLiteral("state");
    draft.metadata.insert(QStringLiteral("found"), report.found);
    draft.metadata.insert(
        QStringLiteral("notFoundReason"), report.notFoundReasonDisplayName);
    int graphIndex = 0;
    for (const FsmGraph& graph : report.graphs) {
        const QString context = QStringLiteral("%1|%2")
            .arg(graph.moduleDisplayName)
            .arg(graphIndex++);
        if (draft.contextKey.isEmpty())
            draft.contextKey = context;
        insightgraph::appendFsmGraph(draft, InsightGraphAdapter::fsm(graph), context);
    }
    return draft;
}
