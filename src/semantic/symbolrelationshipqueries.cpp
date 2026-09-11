#include "symbolrelationshipengine.h"

QList<int> SymbolRelationshipEngine::getRelatedSymbols(int symbolId, RelationType type, bool outgoing) const
{
    const QString cacheKey = QStringLiteral("%1:%2:%3")
                                 .arg(symbolId)
                                 .arg(static_cast<int>(type))
                                 .arg(outgoing ? 1 : 0);
    if (cacheValid && queryCache.contains(cacheKey)) {
        return queryCache[cacheKey];
    }

    QList<int> result;

    if (!relationshipGraph.contains(symbolId)) {
        return result;
    }

    const RelationshipNode& node = relationshipGraph[symbolId];
    const QList<RelationshipEdge>& edges = outgoing ? node.outgoingEdges : node.incomingEdges;

    result.reserve(edges.size());

    for (const RelationshipEdge& edge : edges) {
        if (edge.type == type) {
            result.append(edge.targetId);
        }
    }

    if (cacheValid) {
        queryCache[cacheKey] = result;
    }

    return result;
}
bool SymbolRelationshipEngine::hasRelationship(int fromSymbolId, int toSymbolId, RelationType type) const
{
    if (!relationshipGraph.contains(fromSymbolId))
        return false;

    const RelationshipNode& fromNode = relationshipGraph[fromSymbolId];

    for (const RelationshipEdge& edge : fromNode.outgoingEdges) {
        if (edge.targetId == toSymbolId && edge.type == type) {
            return true;
        }
    }

    return false;
}

int SymbolRelationshipEngine::getRelationshipCount() const
{
    int count = 0;
    for (auto it = relationshipGraph.begin(); it != relationshipGraph.end(); ++it) {
        count += it.value().outgoingEdges.size();
    }
    return count;
}
