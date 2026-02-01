#include "symbolrelationshipengine.h"
#include "syminfo.h"
#include <QCoreApplication>
#include <QThread>
#include <QDebug>
#include <QMetaObject>
#include <algorithm>

SymbolRelationshipEngine::SymbolRelationshipEngine(QObject *parent)
    : QObject(parent)
{
    relationshipGraph.reserve(1000);
    queryCache.reserve(500);
}

SymbolRelationshipEngine::~SymbolRelationshipEngine()
{
}

void SymbolRelationshipEngine::addRelationship(int fromSymbolId, int toSymbolId,
                                              RelationType type, const QString& context, int confidence)
{
    if (fromSymbolId == toSymbolId) return;

    if (hasRelationship(fromSymbolId, toSymbolId, type)) {
        return;
    }

    RelationshipEdge outgoingEdge(toSymbolId, type, context, confidence);
    RelationshipEdge incomingEdge(fromSymbolId, type, context, confidence);

    relationshipGraph[fromSymbolId].outgoingEdges.append(outgoingEdge);
    relationshipGraph[toSymbolId].incomingEdges.append(incomingEdge);

    addToTypeIndex(fromSymbolId, toSymbolId, type);

    if (updateDepth == 0)
        invalidateCacheForRelationship(fromSymbolId, toSymbolId, type);

    if (QThread::currentThread() != QCoreApplication::instance()->thread()) {
        QMetaObject::invokeMethod(this, "emitRelationshipAddedQueued", Qt::QueuedConnection,
                                  Q_ARG(int, fromSymbolId), Q_ARG(int, toSymbolId), Q_ARG(int, static_cast<int>(type)));
    } else {
        emit relationshipAdded(fromSymbolId, toSymbolId, type);
    }
}

void SymbolRelationshipEngine::emitRelationshipAddedQueued(int fromSymbolId, int toSymbolId, int typeAsInt)
{
    emit relationshipAdded(fromSymbolId, toSymbolId, static_cast<RelationType>(typeAsInt));
}

void SymbolRelationshipEngine::removeRelationship(int fromSymbolId, int toSymbolId, RelationType type)
{
    if (!relationshipGraph.contains(fromSymbolId) || !relationshipGraph.contains(toSymbolId)) {
        return;
    }

    RelationshipNode& fromNode = relationshipGraph[fromSymbolId];
    fromNode.outgoingEdges.erase(
        std::remove_if(fromNode.outgoingEdges.begin(), fromNode.outgoingEdges.end(),
                      [toSymbolId, type](const RelationshipEdge& edge) {
                          return edge.targetId == toSymbolId && edge.type == type;
                      }),
        fromNode.outgoingEdges.end()
    );

    RelationshipNode& toNode = relationshipGraph[toSymbolId];
    toNode.incomingEdges.erase(
        std::remove_if(toNode.incomingEdges.begin(), toNode.incomingEdges.end(),
                      [fromSymbolId, type](const RelationshipEdge& edge) {
                          return edge.targetId == fromSymbolId && edge.type == type;
                      }),
        toNode.incomingEdges.end()
    );

    removeFromTypeIndex(fromSymbolId, toSymbolId, type);

    if (updateDepth == 0)
        invalidateCacheForRelationship(fromSymbolId, toSymbolId, type);

    emit relationshipRemoved(fromSymbolId, toSymbolId, type);
}

void SymbolRelationshipEngine::removeAllRelationships(int symbolId)
{
    if (!relationshipGraph.contains(symbolId)) return;

    const RelationshipNode& node = relationshipGraph[symbolId];

    if (updateDepth == 0)
        invalidateCacheForSymbol(symbolId);

    for (const RelationshipEdge& edge : node.outgoingEdges) {
        if (relationshipGraph.contains(edge.targetId)) {
            RelationshipNode& targetNode = relationshipGraph[edge.targetId];
            targetNode.incomingEdges.erase(
                std::remove_if(targetNode.incomingEdges.begin(), targetNode.incomingEdges.end(),
                              [symbolId](const RelationshipEdge& e) {
                                  return e.targetId == symbolId;
                              }),
                targetNode.incomingEdges.end()
            );
        }
        removeFromTypeIndex(symbolId, edge.targetId, edge.type);
    }

    for (const RelationshipEdge& edge : node.incomingEdges) {
        if (relationshipGraph.contains(edge.targetId)) {
            RelationshipNode& sourceNode = relationshipGraph[edge.targetId];
            sourceNode.outgoingEdges.erase(
                std::remove_if(sourceNode.outgoingEdges.begin(), sourceNode.outgoingEdges.end(),
                              [symbolId](const RelationshipEdge& e) {
                                  return e.targetId == symbolId;
                              }),
                sourceNode.outgoingEdges.end()
            );
        }
        removeFromTypeIndex(edge.targetId, symbolId, edge.type);
    }

    relationshipGraph.remove(symbolId);
}

void SymbolRelationshipEngine::clearAllRelationships()
{
    relationshipGraph.clear();
    relationshipsByType.clear();
    symbolsByFile.clear();
    invalidateCache();

    emit relationshipsCleared();
}

QList<int> SymbolRelationshipEngine::getRelatedSymbols(int symbolId, RelationType type, bool outgoing) const
{
    QPair<int, RelationType> cacheKey(symbolId, type);
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
    if (!relationshipGraph.contains(fromSymbolId)) return false;

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
    for (const QList<int>& path : qAsConst(allPaths)) {
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

        for (int childId : qAsConst(children)) {
            if (!visited.contains(childId)) {
                visited.insert(childId);
                result.append(childId);
                toProcess.append(childId);
            }
        }
    }

    return result;
}

void SymbolRelationshipEngine::beginUpdate()
{
    ++updateDepth;
}

void SymbolRelationshipEngine::endUpdate()
{
    if (updateDepth > 0) {
        --updateDepth;
        if (updateDepth == 0)
            invalidateCache();
    }
}

void SymbolRelationshipEngine::buildFileRelationships(const QString& fileName)
{
    beginUpdate();
    invalidateFileRelationships(fileName);

    sym_list* symbolList = sym_list::getInstance();
    QList<sym_list::SymbolInfo> fileSymbols = symbolList->findSymbolsByFileName(fileName);

    for (const sym_list::SymbolInfo& symbol : qAsConst(fileSymbols)) {
        if (symbol.symbolType == sym_list::sym_module) {
            int moduleId = symbol.symbolId;
            symbolsByFile[fileName].insert(moduleId);

            for (const sym_list::SymbolInfo& otherSymbol : qAsConst(fileSymbols)) {
                if (otherSymbol.symbolId != moduleId &&
                    isSymbolInModule(otherSymbol, symbol)) {

                    addRelationship(moduleId, otherSymbol.symbolId, CONTAINS);
                    symbolsByFile[fileName].insert(otherSymbol.symbolId);
                }
            }
        } else {
            symbolsByFile[fileName].insert(symbol.symbolId);
        }
    }

    endUpdate();
}

void SymbolRelationshipEngine::invalidateFileRelationships(const QString& fileName)
{
    if (!symbolsByFile.contains(fileName)) return;

    const QSet<int>& fileSymbolIds = symbolsByFile[fileName];

    for (int symbolId : fileSymbolIds) {
        removeAllRelationships(symbolId);
    }

    symbolsByFile.remove(fileName);
}

void SymbolRelationshipEngine::rebuildAllRelationships()
{
    clearAllRelationships();

    sym_list* symbolList = sym_list::getInstance();
    QList<sym_list::SymbolInfo> allSymbols = symbolList->getAllSymbols();

    QHash<QString, QList<sym_list::SymbolInfo>> symbolsByFile;
    for (const sym_list::SymbolInfo& symbol : qAsConst(allSymbols)) {
        symbolsByFile[symbol.fileName].append(symbol);
    }

    for (auto it = symbolsByFile.begin(); it != symbolsByFile.end(); ++it) {
        buildFileRelationships(it.key());
    }
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

void SymbolRelationshipEngine::invalidateCache()
{
    queryCache.clear();
    cacheValid = true;
}

void SymbolRelationshipEngine::invalidateCacheForRelationship(int fromId, int toId, RelationType type)
{
    queryCache.remove(qMakePair(fromId, type));
    queryCache.remove(qMakePair(toId, type));
}

void SymbolRelationshipEngine::invalidateCacheForSymbol(int symbolId)
{
    if (!relationshipGraph.contains(symbolId)) return;

    const RelationshipNode& node = relationshipGraph[symbolId];

    QMutableHashIterator<QPair<int, RelationType>, QList<int>> it(queryCache);
    while (it.hasNext()) {
        it.next();
        if (it.key().first == symbolId) {
            it.remove();
        }
    }

    for (const RelationshipEdge& edge : node.outgoingEdges) {
        queryCache.remove(qMakePair(edge.targetId, edge.type));
    }
    for (const RelationshipEdge& edge : node.incomingEdges) {
        queryCache.remove(qMakePair(edge.targetId, edge.type));
    }
}

void SymbolRelationshipEngine::addToTypeIndex(int fromId, int toId, RelationType type)
{
    relationshipsByType[type].append(qMakePair(fromId, toId));
}

void SymbolRelationshipEngine::removeFromTypeIndex(int fromId, int toId, RelationType type)
{
    if (relationshipsByType.contains(type)) {
        relationshipsByType[type].removeAll(qMakePair(fromId, toId));
    }
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
    if (currentDepth > maxDepth) return;
    if (visited.contains(currentId)) return;

    visited.insert(currentId);
    currentPath.append(currentId);

    if (currentId == targetId) {
        allPaths.append(currentPath);
    } else {
        QList<int> related = getAllRelatedSymbols(currentId, true);
        for (int relatedId : qAsConst(related)) {
            findPathRecursive(relatedId, targetId, currentDepth + 1, maxDepth, visited, currentPath, allPaths);
        }
    }

    visited.remove(currentId);
    currentPath.removeLast();
}

void SymbolRelationshipEngine::getInfluencedSymbolsRecursive(int symbolId, int currentDepth, int maxDepth,
                                                           QSet<int>& visited, QList<int>& result) const
{
    if (currentDepth > maxDepth) return;
    if (visited.contains(symbolId)) return;

    visited.insert(symbolId);
    if (currentDepth > 0) {
        result.append(symbolId);
    }

    QList<int> influenced = getAllRelatedSymbols(symbolId, true);
    for (int influencedId : qAsConst(influenced)) {
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
