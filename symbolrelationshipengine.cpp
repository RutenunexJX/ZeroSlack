#include "symbolrelationshipengine.h"
#include "syminfo.h"
#include <QCoreApplication>
#include <QThread>
#include <QMetaObject>
#include <algorithm>

SymbolRelationshipEngine::SymbolRelationshipEngine(QObject *parent)
    : QObject(parent)
{
    relationshipGraph.reserve(1000);
    queryCache.reserve(500);
}

SymbolRelationshipEngine::SymbolRelationshipEngine(sym_list* symbols, QObject *parent)
    : SymbolRelationshipEngine(parent)
{
    setSymbolDatabase(symbols);
}

SymbolRelationshipEngine::~SymbolRelationshipEngine()
{
}

void SymbolRelationshipEngine::setSymbolDatabase(sym_list* symbols)
{
    symbolDatabase = symbols;
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

    QList<sym_list::SymbolInfo> fileSymbols = symbols()->findSymbolsByFileName(fileName);

    for (const sym_list::SymbolInfo& symbol : std::as_const(fileSymbols)) {
        if (symbol.symbolType == sym_list::sym_module) {
            int moduleId = symbol.symbolId;
            symbolsByFile[fileName].insert(moduleId);

            for (const sym_list::SymbolInfo& otherSymbol : std::as_const(fileSymbols)) {
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

    QList<sym_list::SymbolInfo> allSymbols = symbols()->getAllSymbols();

    QHash<QString, QList<sym_list::SymbolInfo>> symbolsByFile;
    for (const sym_list::SymbolInfo& symbol : std::as_const(allSymbols)) {
        symbolsByFile[symbol.fileName].append(symbol);
    }

    for (auto it = symbolsByFile.begin(); it != symbolsByFile.end(); ++it) {
        buildFileRelationships(it.key());
    }
}

void SymbolRelationshipEngine::invalidateCache()
{
    queryCache.clear();
    cacheValid = true;
}

void SymbolRelationshipEngine::invalidateCacheForRelationship(int fromId, int toId, RelationType type)
{
    Q_UNUSED(fromId)
    Q_UNUSED(toId)
    Q_UNUSED(type)
    invalidateCache();
}

void SymbolRelationshipEngine::invalidateCacheForSymbol(int symbolId)
{
    Q_UNUSED(symbolId)
    invalidateCache();
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

sym_list* SymbolRelationshipEngine::symbols() const
{
    return symbolDatabase ? symbolDatabase : sym_list::getInstance();
}
