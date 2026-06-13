#include "symbolrelationshipengine.h"

#include <utility>

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

QList<int> SymbolRelationshipEngine::getAllRelatedSymbols(int symbolId, bool outgoing) const
{
    QList<int> result;

    if (!relationshipGraph.contains(symbolId)) {
        return result;
    }

    const RelationshipNode& node = relationshipGraph[symbolId];
    const QList<RelationshipEdge>& edges = outgoing ? node.outgoingEdges : node.incomingEdges;

    result.reserve(edges.size());

    for (const RelationshipEdge& edge : edges) {
        result.append(edge.targetId);
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

QList<int> SymbolRelationshipEngine::getModuleChildren(int moduleId) const
{
    return getRelatedSymbols(moduleId, CONTAINS, true);
}

QList<int> SymbolRelationshipEngine::getSymbolReferences(int symbolId) const
{
    return getRelatedSymbols(symbolId, REFERENCES, false);
}

QList<int> SymbolRelationshipEngine::getSymbolDependencies(int symbolId) const
{
    return getRelatedSymbols(symbolId, REFERENCES, true);
}

QList<int> SymbolRelationshipEngine::getModuleInstances(int moduleId) const
{
    if (moduleId <= 0)
        return QList<int>();
    if (!relationshipGraph.contains(moduleId))
        return QList<int>();
    return getRelatedSymbols(moduleId, INSTANTIATES, false);
}

QList<int> SymbolRelationshipEngine::getTaskCalls(int taskId) const
{
    return getRelatedSymbols(taskId, CALLS, false);
}

QList<int> SymbolRelationshipEngine::findRelationshipPath(int fromSymbolId, int toSymbolId, int maxDepth) const
{
    QList<QList<int>> allPaths;
    QSet<int> visited;
    QList<int> currentPath;

    findPathRecursive(fromSymbolId, toSymbolId, 0, maxDepth, visited, currentPath, allPaths);

    if (allPaths.isEmpty()) {
        return QList<int>();
    }

    QList<int> shortestPath = allPaths[0];
    for (const QList<int>& path : std::as_const(allPaths)) {
        if (path.size() < shortestPath.size()) {
            shortestPath = path;
        }
    }

    return shortestPath;
}

QList<int> SymbolRelationshipEngine::getInfluencedSymbols(int symbolId, int depth) const
{
    QList<int> result;
    QSet<int> visited;

    getInfluencedSymbolsRecursive(symbolId, 0, depth, visited, result);

    return result;
}

QList<int> SymbolRelationshipEngine::getSymbolHierarchy(int rootSymbolId) const
{
    QList<int> result;
    QSet<int> visited;
    QList<int> toProcess;

    toProcess.append(rootSymbolId);
    visited.insert(rootSymbolId);
    result.append(rootSymbolId);

    while (!toProcess.isEmpty()) {
        int currentId = toProcess.takeFirst();
        QList<int> children = getModuleChildren(currentId);

        for (int childId : std::as_const(children)) {
            if (!visited.contains(childId)) {
                visited.insert(childId);
                result.append(childId);
                toProcess.append(childId);
            }
        }
    }

    return result;
}

int SymbolRelationshipEngine::getRelationshipCount() const
{
    int count = 0;
    for (auto it = relationshipGraph.begin(); it != relationshipGraph.end(); ++it) {
        count += it.value().outgoingEdges.size();
    }
    return count;
}

int SymbolRelationshipEngine::getRelationshipCount(RelationType type) const
{
    if (relationshipsByType.contains(type)) {
        return relationshipsByType[type].size();
    }
    return 0;
}

QStringList SymbolRelationshipEngine::getRelationshipSummary() const
{
    QStringList summary;
    summary << QString("Total symbols: %1").arg(relationshipGraph.size());
    summary << QString("Total relationships: %1").arg(getRelationshipCount());

    QList<RelationType> types = {CONTAINS, REFERENCES, INSTANTIATES, CALLS};
    for (RelationType type : types) {
        int count = getRelationshipCount(type);
        if (count > 0) {
            summary << QString("%1: %2").arg(relationshipTypeToString(type)).arg(count);
        }
    }

    return summary;
}

QString SymbolRelationshipEngine::relationshipTypeToString(RelationType type) const
{
    switch (type) {
    case CONTAINS: return "Contains";
    case REFERENCES: return "References";
    case INSTANTIATES: return "Instantiates";
    case CALLS: return "Calls";
    case INHERITS: return "Inherits";
    case IMPLEMENTS: return "Implements";
    case ASSIGNS_TO: return "Assigns to";
    case READS_FROM: return "Reads from";
    case CLOCKS: return "Clocks";
    case RESETS: return "Resets";
    case GENERATES: return "Generates";
    case CONSTRAINS: return "Constrains";
    default: return "Unknown";
    }
}

void SymbolRelationshipEngine::findPathRecursive(int currentId, int targetId, int currentDepth, int maxDepth,
                                                QSet<int>& visited, QList<int>& currentPath,
                                                QList<QList<int>>& allPaths) const
{
    if (currentDepth > maxDepth)
        return;
    if (visited.contains(currentId))
        return;

    visited.insert(currentId);
    currentPath.append(currentId);

    if (currentId == targetId) {
        allPaths.append(currentPath);
    } else {
        QList<int> related = getAllRelatedSymbols(currentId, true);
        for (int relatedId : std::as_const(related)) {
            findPathRecursive(relatedId, targetId, currentDepth + 1, maxDepth, visited, currentPath, allPaths);
        }
    }

    visited.remove(currentId);
    currentPath.removeLast();
}

void SymbolRelationshipEngine::getInfluencedSymbolsRecursive(int symbolId, int currentDepth, int maxDepth,
                                                           QSet<int>& visited, QList<int>& result) const
{
    if (currentDepth > maxDepth)
        return;
    if (visited.contains(symbolId))
        return;

    visited.insert(symbolId);
    if (currentDepth > 0) {
        result.append(symbolId);
    }

    QList<int> influenced = getAllRelatedSymbols(symbolId, true);
    for (int influencedId : std::as_const(influenced)) {
        getInfluencedSymbolsRecursive(influencedId, currentDepth + 1, maxDepth, visited, result);
    }
}

SymbolRelationshipEngine::RelationType stringToRelationshipType(const QString& typeStr)
{
    if (typeStr == "Contains") return SymbolRelationshipEngine::CONTAINS;
    if (typeStr == "References") return SymbolRelationshipEngine::REFERENCES;
    if (typeStr == "Instantiates") return SymbolRelationshipEngine::INSTANTIATES;
    if (typeStr == "Calls") return SymbolRelationshipEngine::CALLS;
    if (typeStr == "Inherits") return SymbolRelationshipEngine::INHERITS;
    if (typeStr == "Implements") return SymbolRelationshipEngine::IMPLEMENTS;
    if (typeStr == "Assigns to") return SymbolRelationshipEngine::ASSIGNS_TO;
    if (typeStr == "Reads from") return SymbolRelationshipEngine::READS_FROM;
    if (typeStr == "Clocks") return SymbolRelationshipEngine::CLOCKS;
    if (typeStr == "Resets") return SymbolRelationshipEngine::RESETS;
    if (typeStr == "Generates") return SymbolRelationshipEngine::GENERATES;
    if (typeStr == "Constrains") return SymbolRelationshipEngine::CONSTRAINS;

    return SymbolRelationshipEngine::CONTAINS;
}
