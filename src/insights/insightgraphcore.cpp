#include "insightgraphcore.h"

#include "fsmgraphservice.h"
#include "moduleblockdiagramservice.h"
#include "semanticindex.h"
#include "signalkernelgraphservice.h"
#include "signalusagehotspotservice.h"
#include "statetransitiongraphservice.h"

#include <QCryptographicHash>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QReadLocker>
#include <QWriteLocker>

#include <algorithm>

namespace {
QString normalizedIdentityPath(const QString& path)
{
    QString result = QDir::cleanPath(QDir::fromNativeSeparators(path.trimmed()));
#ifdef Q_OS_WIN
    result = result.toLower();
#endif
    return result == QStringLiteral(".") ? QString() : result;
}

QString digestId(const QString& prefix, const QStringList& components)
{
    QByteArray canonical;
    for (const QString& component : components) {
        const QByteArray bytes = component.toUtf8();
        canonical.append(QByteArray::number(bytes.size()));
        canonical.append(':');
        canonical.append(bytes);
        canonical.append('|');
    }
    const QByteArray digest = QCryptographicHash::hash(
        canonical, QCryptographicHash::Sha256).toHex().left(32);
    return prefix + QString::fromLatin1(digest);
}

QByteArray variantFingerprint(const QVariantMap& map)
{
    return QJsonDocument(QJsonObject::fromVariantMap(map))
        .toJson(QJsonDocument::Compact);
}

QByteArray snapshotFingerprint(const InsightGraphSnapshot& snapshot)
{
    QStringList nodeIds = snapshot.nodes.keys();
    QStringList edgeIds = snapshot.edges.keys();
    nodeIds.sort();
    edgeIds.sort();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(snapshot.channelId.toUtf8());
    hash.addData(snapshot.workspaceId.toUtf8());
    hash.addData(snapshot.documentId.toUtf8());
    hash.addData(snapshot.contextKey.toUtf8());
    hash.addData(snapshot.layoutHint.toUtf8());
    hash.addData(variantFingerprint(snapshot.metadata));
    for (const QString& id : nodeIds) {
        const InsightGraphNode& node = snapshot.nodes.value(id);
        hash.addData(id.toUtf8());
        hash.addData(node.displayName.toUtf8());
        hash.addData(node.detail.toUtf8());
        hash.addData(node.sourceFile.toUtf8());
        hash.addData(QByteArray::number(node.sourceLine));
        hash.addData(QByteArray::number(static_cast<int>(node.domain)));
        hash.addData(QByteArray::number(node.heatScore));
        hash.addData(variantFingerprint(node.attributes));
    }
    for (const QString& id : edgeIds) {
        const InsightGraphEdge& edge = snapshot.edges.value(id);
        hash.addData(id.toUtf8());
        hash.addData(edge.fromNodeId.toUtf8());
        hash.addData(edge.toNodeId.toUtf8());
        hash.addData(edge.displayName.toUtf8());
        hash.addData(QByteArray::number(static_cast<int>(edge.domain)));
        hash.addData(QByteArray::number(edge.weight));
        hash.addData(variantFingerprint(edge.attributes));
    }
    return hash.result();
}

void incrementDomain(InsightGraphStatistics& statistics,
                     InsightGraphDomain domain,
                     int amount = 1)
{
    switch (domain) {
    case InsightGraphDomain::Ast:
        statistics.astCount += amount;
        break;
    case InsightGraphDomain::Symbol:
        statistics.symbolCount += amount;
        break;
    case InsightGraphDomain::Hierarchy:
        statistics.hierarchyCount += amount;
        break;
    case InsightGraphDomain::Connection:
        statistics.connectionCount += amount;
        break;
    case InsightGraphDomain::DataFlow:
        statistics.dataFlowCount += amount;
        break;
    case InsightGraphDomain::Fsm:
        statistics.fsmCount += amount;
        break;
    case InsightGraphDomain::Hotspot:
        statistics.hotspotCount += amount;
        break;
    }
}

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

void appendFsmGraph(InsightGraphDraft& draft,
                    const FsmGraph& graph,
                    const QString& context)
{
    QHash<QString, QString> stateIds;
    for (const FsmStateRow& row : graph.stateRows) {
        InsightGraphNode node;
        node.nodeId = nodeIdOrFallback(
            row.stateStableKey,
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
    for (const FsmTransitionRow& row : graph.transitionRows) {
        const QString fromId = stateIds.value(
            row.fromStateDisplayName,
            nodeIdOrFallback(
                row.fromStateStableKey,
                QStringLiteral("fsm-state"),
                context,
                row.fromStateDisplayName));
        const QString toId = stateIds.value(
            row.toStateDisplayName,
            nodeIdOrFallback(
                row.toStateStableKey,
                QStringLiteral("fsm-state"),
                context,
                row.toStateDisplayName));
        InsightGraphEdge edge;
        edge.fromNodeId = fromId;
        edge.toNodeId = toId;
        edge.displayName = row.conditionDisplayName;
        edge.domain = InsightGraphDomain::Fsm;
        edge.edgeId = InsightGraphCore::stableEdgeId(
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
}

bool InsightGraphNode::isValid() const
{
    return !nodeId.trimmed().isEmpty();
}

bool InsightGraphEdge::isValid() const
{
    return !edgeId.trimmed().isEmpty()
        && !fromNodeId.trimmed().isEmpty()
        && !toNodeId.trimmed().isEmpty();
}

bool InsightGraphSnapshot::isValid() const
{
    return !channelId.trimmed().isEmpty() && generation > 0;
}

bool InsightGraphSnapshot::isEmpty() const
{
    return nodes.isEmpty() && edges.isEmpty();
}

bool InsightGraphDiff::isEmpty() const
{
    return addedNodeIds.isEmpty()
        && removedNodeIds.isEmpty()
        && updatedNodeIds.isEmpty()
        && addedEdgeIds.isEmpty()
        && removedEdgeIds.isEmpty()
        && updatedEdgeIds.isEmpty()
        && !contextChanged
        && !layoutInvalidated
        && !metadataChanged;
}

bool InsightGraphDiff::topologyChanged() const
{
    return !addedNodeIds.isEmpty()
        || !removedNodeIds.isEmpty()
        || !addedEdgeIds.isEmpty()
        || !removedEdgeIds.isEmpty();
}

bool operator==(const InsightGraphNode& lhs, const InsightGraphNode& rhs)
{
    return lhs.nodeId == rhs.nodeId
        && lhs.displayName == rhs.displayName
        && lhs.detail == rhs.detail
        && lhs.sourceFile == rhs.sourceFile
        && lhs.sourceLine == rhs.sourceLine
        && lhs.domain == rhs.domain
        && lhs.heatScore == rhs.heatScore
        && lhs.attributes == rhs.attributes;
}

bool operator!=(const InsightGraphNode& lhs, const InsightGraphNode& rhs)
{
    return !(lhs == rhs);
}

bool operator==(const InsightGraphEdge& lhs, const InsightGraphEdge& rhs)
{
    return lhs.edgeId == rhs.edgeId
        && lhs.fromNodeId == rhs.fromNodeId
        && lhs.toNodeId == rhs.toNodeId
        && lhs.displayName == rhs.displayName
        && lhs.domain == rhs.domain
        && lhs.weight == rhs.weight
        && lhs.attributes == rhs.attributes;
}

bool operator!=(const InsightGraphEdge& lhs, const InsightGraphEdge& rhs)
{
    return !(lhs == rhs);
}

QString InsightGraphCore::stableSymbolId(const SymbolStableKey& key)
{
    return stableSymbolId(
        key.fileName,
        key.ownerScope,
        static_cast<int>(key.declarationKind),
        key.symbolName);
}

QString InsightGraphCore::stableSymbolId(const QString& sourceFile,
                                         const QString& ownerScope,
                                         int declarationKind,
                                         const QString& symbolName)
{
    return digestId(
        QStringLiteral("sym:"),
        {normalizedIdentityPath(sourceFile),
         ownerScope.trimmed(),
         QString::number(declarationKind),
         symbolName.trimmed()});
}

QString InsightGraphCore::stableSyntheticId(const QString& domain,
                                            const QString& context,
                                            const QString& identity)
{
    return digestId(
        QStringLiteral("synthetic:"),
        {domain.trimmed().toLower(), context.trimmed(), identity.trimmed()});
}

QString InsightGraphCore::stableEdgeId(const QString& fromNodeId,
                                       const QString& toNodeId,
                                       const QString& relation,
                                       const QString& discriminator)
{
    return digestId(
        QStringLiteral("edge:"),
        {fromNodeId, toNodeId, relation.trimmed(), discriminator});
}

InsightGraphUpdate InsightGraphCore::update(const QString& channelId,
                                            const InsightGraphDraft& draft)
{
    const QString normalizedChannel = channelId.trimmed();
    QWriteLocker locker(&lock);
    InsightGraphUpdate result;
    result.previousSnapshot = snapshots.value(normalizedChannel);
    const quint64 generation = generationCounters.value(normalizedChannel) + 1;
    generationCounters.insert(normalizedChannel, generation);
    result.snapshot = buildSnapshot(normalizedChannel, draft, generation);
    result.diff = diff(result.previousSnapshot, result.snapshot);
    snapshots.insert(normalizedChannel, result.snapshot);
    return result;
}

InsightGraphSnapshot InsightGraphCore::snapshot(const QString& channelId) const
{
    QReadLocker locker(&lock);
    return snapshots.value(channelId.trimmed());
}

void InsightGraphCore::clear(const QString& channelId)
{
    QWriteLocker locker(&lock);
    snapshots.remove(channelId.trimmed());
    generationCounters.remove(channelId.trimmed());
}

void InsightGraphCore::clear()
{
    QWriteLocker locker(&lock);
    snapshots.clear();
    generationCounters.clear();
}

InsightGraphDiff InsightGraphCore::diff(
    const InsightGraphSnapshot& previous,
    const InsightGraphSnapshot& current)
{
    InsightGraphDiff result;
    for (auto it = current.nodes.cbegin(); it != current.nodes.cend(); ++it) {
        if (!previous.nodes.contains(it.key()))
            result.addedNodeIds.append(it.key());
        else if (previous.nodes.value(it.key()) != it.value())
            result.updatedNodeIds.append(it.key());
    }
    for (const QString& id : previous.nodes.keys()) {
        if (!current.nodes.contains(id))
            result.removedNodeIds.append(id);
    }
    for (auto it = current.edges.cbegin(); it != current.edges.cend(); ++it) {
        if (!previous.edges.contains(it.key()))
            result.addedEdgeIds.append(it.key());
        else if (previous.edges.value(it.key()) != it.value())
            result.updatedEdgeIds.append(it.key());
    }
    for (const QString& id : previous.edges.keys()) {
        if (!current.edges.contains(id))
            result.removedEdgeIds.append(id);
    }
    result.contextChanged = previous.isValid()
        && (previous.workspaceId != current.workspaceId
            || previous.documentId != current.documentId
            || previous.contextKey != current.contextKey);
    result.metadataChanged = previous.metadata != current.metadata;
    result.layoutInvalidated = result.contextChanged
        || previous.layoutHint != current.layoutHint
        || result.topologyChanged();
    result.addedNodeIds.sort();
    result.removedNodeIds.sort();
    result.updatedNodeIds.sort();
    result.addedEdgeIds.sort();
    result.removedEdgeIds.sort();
    result.updatedEdgeIds.sort();
    return result;
}

InsightGraphSnapshot InsightGraphCore::buildSnapshot(
    const QString& channelId,
    const InsightGraphDraft& draft,
    quint64 generation)
{
    InsightGraphSnapshot snapshot;
    snapshot.channelId = channelId;
    snapshot.workspaceId = draft.workspaceId;
    snapshot.documentId = draft.documentId;
    snapshot.documentRevision = draft.documentRevision;
    snapshot.semanticRevision = draft.semanticRevision;
    snapshot.generation = generation;
    snapshot.contextKey = draft.contextKey;
    snapshot.layoutHint = draft.layoutHint;
    snapshot.metadata = draft.metadata;

    QList<InsightGraphNode> nodes = draft.nodes;
    QList<InsightGraphEdge> edges = draft.edges;
    for (const InsightGraphFact& fact : draft.facts) {
        const QString nodeId = !fact.nodeId.trimmed().isEmpty()
            ? fact.nodeId
            : stableSymbolId(fact.sourceFile,
                             fact.ownerScope,
                             static_cast<int>(fact.domain),
                             fact.symbolName);
        if (fact.relatedNodeId.trimmed().isEmpty()) {
            InsightGraphNode node;
            node.nodeId = nodeId;
            node.displayName = fact.symbolName;
            node.detail = fact.detail;
            node.sourceFile = fact.sourceFile;
            node.sourceLine = fact.sourceLine;
            node.domain = fact.domain;
            node.heatScore = qMax(0, fact.weight);
            node.attributes = fact.attributes;
            nodes.append(node);
            continue;
        }
        InsightGraphEdge edge;
        edge.fromNodeId = nodeId;
        edge.toNodeId = fact.relatedNodeId;
        edge.displayName = fact.relation;
        edge.domain = fact.domain;
        edge.weight = qMax(1, fact.weight);
        edge.attributes = fact.attributes;
        edge.edgeId = stableEdgeId(
            edge.fromNodeId,
            edge.toNodeId,
            edge.displayName,
            fact.detail);
        edges.append(edge);
    }

    for (InsightGraphNode node : nodes) {
        if (!node.isValid()) {
            node.nodeId = stableSyntheticId(
                QString::number(static_cast<int>(node.domain)),
                draft.contextKey,
                node.displayName + QLatin1Char('|') + node.detail);
        }
        if (!node.isValid())
            continue;
        snapshot.nodes.insert(node.nodeId, node);
        incrementDomain(snapshot.statistics, node.domain);
        if (node.domain == InsightGraphDomain::Hotspot) {
            snapshot.statistics.hotspotScores.insert(
                node.nodeId, qMax(0, node.heatScore));
        }
    }
    for (InsightGraphEdge edge : edges) {
        if (edge.edgeId.trimmed().isEmpty()) {
            edge.edgeId = stableEdgeId(
                edge.fromNodeId,
                edge.toNodeId,
                edge.displayName);
        }
        if (!edge.isValid()
            || !snapshot.nodes.contains(edge.fromNodeId)
            || !snapshot.nodes.contains(edge.toNodeId)) {
            continue;
        }
        snapshot.edges.insert(edge.edgeId, edge);
        incrementDomain(snapshot.statistics, edge.domain);
    }
    snapshot.contentFingerprint = snapshotFingerprint(snapshot);
    return snapshot;
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

InsightGraphDraft InsightGraphCore::fromModuleBlockDiagram(
    const ModuleBlockDiagramReport& report)
{
    InsightGraphDraft draft;
    draft.layoutHint = QStringLiteral("block");
    draft.contextKey = report.root.moduleDisplayName;
    draft.metadata.insert(QStringLiteral("found"), report.found);
    draft.metadata.insert(
        QStringLiteral("notFoundReason"), report.notFoundReasonDisplayName);
    QHash<int, QString> ids;
    for (const ModuleBlockDiagramNode& source : report.nodes) {
        InsightGraphNode node;
        const QString definitionSymbolId = nodeIdOrFallback(
            source.moduleStableKey,
            QStringLiteral("module-definition"),
            report.root.moduleDisplayName,
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
        node.nodeId = stableSyntheticId(
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
    for (const ModuleBlockDiagramEdge& source : report.edges) {
        InsightGraphEdge edge;
        edge.fromNodeId = ids.value(source.fromNodeId);
        edge.toNodeId = ids.value(source.toNodeId);
        edge.displayName = source.childInstanceDisplayName;
        edge.domain = InsightGraphDomain::Connection;
        edge.edgeId = stableEdgeId(
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
        appendFsmGraph(draft, report.graph, draft.contextKey);
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
        appendFsmGraph(draft, graph, context);
    }
    return draft;
}
