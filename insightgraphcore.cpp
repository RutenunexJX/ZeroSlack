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
        hash.addData(edge.fromSymbolId.toUtf8());
        hash.addData(edge.toSymbolId.toUtf8());
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

QString symbolIdOrFallback(const SymbolStableKey& key,
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
        node.symbolId = symbolIdOrFallback(
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
        stateIds.insert(row.stateDisplayName, node.symbolId);
    }
    int transitionIndex = 0;
    for (const FsmTransitionRow& row : graph.transitionRows) {
        const QString fromId = stateIds.value(
            row.fromStateDisplayName,
            symbolIdOrFallback(
                row.fromStateStableKey,
                QStringLiteral("fsm-state"),
                context,
                row.fromStateDisplayName));
        const QString toId = stateIds.value(
            row.toStateDisplayName,
            symbolIdOrFallback(
                row.toStateStableKey,
                QStringLiteral("fsm-state"),
                context,
                row.toStateDisplayName));
        InsightGraphEdge edge;
        edge.fromSymbolId = fromId;
        edge.toSymbolId = toId;
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
    return !symbolId.trimmed().isEmpty();
}

bool InsightGraphEdge::isValid() const
{
    return !edgeId.trimmed().isEmpty()
        && !fromSymbolId.trimmed().isEmpty()
        && !toSymbolId.trimmed().isEmpty();
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
    return lhs.symbolId == rhs.symbolId
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
        && lhs.fromSymbolId == rhs.fromSymbolId
        && lhs.toSymbolId == rhs.toSymbolId
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

QString InsightGraphCore::stableEdgeId(const QString& fromSymbolId,
                                       const QString& toSymbolId,
                                       const QString& relation,
                                       const QString& discriminator)
{
    return digestId(
        QStringLiteral("edge:"),
        {fromSymbolId, toSymbolId, relation.trimmed(), discriminator});
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
        const QString symbolId = !fact.symbolId.trimmed().isEmpty()
            ? fact.symbolId
            : stableSymbolId(fact.sourceFile,
                             fact.ownerScope,
                             static_cast<int>(fact.domain),
                             fact.symbolName);
        if (fact.relatedSymbolId.trimmed().isEmpty()) {
            InsightGraphNode node;
            node.symbolId = symbolId;
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
        edge.fromSymbolId = symbolId;
        edge.toSymbolId = fact.relatedSymbolId;
        edge.displayName = fact.relation;
        edge.domain = fact.domain;
        edge.weight = qMax(1, fact.weight);
        edge.attributes = fact.attributes;
        edge.edgeId = stableEdgeId(
            edge.fromSymbolId,
            edge.toSymbolId,
            edge.displayName,
            fact.detail);
        edges.append(edge);
    }

    for (InsightGraphNode node : nodes) {
        if (!node.isValid()) {
            node.symbolId = stableSyntheticId(
                QString::number(static_cast<int>(node.domain)),
                draft.contextKey,
                node.displayName + QLatin1Char('|') + node.detail);
        }
        if (!node.isValid())
            continue;
        snapshot.nodes.insert(node.symbolId, node);
        incrementDomain(snapshot.statistics, node.domain);
        if (node.domain == InsightGraphDomain::Hotspot) {
            snapshot.statistics.hotspotScores.insert(
                node.symbolId, qMax(0, node.heatScore));
        }
    }
    for (InsightGraphEdge edge : edges) {
        if (edge.edgeId.trimmed().isEmpty()) {
            edge.edgeId = stableEdgeId(
                edge.fromSymbolId,
                edge.toSymbolId,
                edge.displayName);
        }
        if (!edge.isValid()
            || !snapshot.nodes.contains(edge.fromSymbolId)
            || !snapshot.nodes.contains(edge.toSymbolId)) {
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
        node.symbolId = symbolIdOrFallback(
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
        ids.insert(source.id, node.symbolId);
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
        edge.fromSymbolId = ids.value(source.fromNodeId);
        edge.toSymbolId = ids.value(source.toNodeId);
        edge.displayName = source.label;
        edge.domain = InsightGraphDomain::DataFlow;
        edge.edgeId = stableEdgeId(
            edge.fromSymbolId,
            edge.toSymbolId,
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
        const QString definitionSymbolId = symbolIdOrFallback(
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
        node.symbolId = stableSyntheticId(
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
        ids.insert(source.nodeId, node.symbolId);
        draft.nodes.append(node);
    }
    int edgeIndex = 0;
    for (const ModuleBlockDiagramEdge& source : report.edges) {
        InsightGraphEdge edge;
        edge.fromSymbolId = ids.value(source.fromNodeId);
        edge.toSymbolId = ids.value(source.toNodeId);
        edge.displayName = source.childInstanceDisplayName;
        edge.domain = InsightGraphDomain::Connection;
        edge.edgeId = stableEdgeId(
            edge.fromSymbolId,
            edge.toSymbolId,
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
    declaration.symbolId = symbolIdOrFallback(
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
        evidence.symbolId = stableSyntheticId(
            QStringLiteral("hotspot-evidence"),
            declaration.symbolId,
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
        edge.fromSymbolId = source.outgoing
            ? declaration.symbolId : evidence.symbolId;
        edge.toSymbolId = source.outgoing
            ? evidence.symbolId : declaration.symbolId;
        edge.displayName = source.relationshipTypeDisplayName;
        edge.domain = InsightGraphDomain::Hotspot;
        edge.weight = qMax(1, source.confidence);
        edge.edgeId = stableEdgeId(
            edge.fromSymbolId,
            edge.toSymbolId,
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
