#include <insightgraph/graph.h>
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



QString insightgraph::GraphStore::stableSymbolId(const QString& sourceFile,
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

QString insightgraph::GraphStore::stableSyntheticId(const QString& domain,
                                            const QString& context,
                                            const QString& identity)
{
    return digestId(
        QStringLiteral("synthetic:"),
        {domain.trimmed().toLower(), context.trimmed(), identity.trimmed()});
}

QString insightgraph::GraphStore::stableEdgeId(const QString& fromNodeId,
                                       const QString& toNodeId,
                                       const QString& relation,
                                       const QString& discriminator)
{
    return digestId(
        QStringLiteral("edge:"),
        {fromNodeId, toNodeId, relation.trimmed(), discriminator});
}

InsightGraphUpdate insightgraph::GraphStore::update(const QString& channelId,
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

InsightGraphSnapshot insightgraph::GraphStore::snapshot(const QString& channelId) const
{
    QReadLocker locker(&lock);
    return snapshots.value(channelId.trimmed());
}

void insightgraph::GraphStore::clear(const QString& channelId)
{
    QWriteLocker locker(&lock);
    snapshots.remove(channelId.trimmed());
    generationCounters.remove(channelId.trimmed());
}

void insightgraph::GraphStore::clear()
{
    QWriteLocker locker(&lock);
    snapshots.clear();
    generationCounters.clear();
}

InsightGraphDiff insightgraph::GraphStore::diff(
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

InsightGraphSnapshot insightgraph::GraphStore::buildSnapshot(
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
