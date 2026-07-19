#include "symbolrelationshipengine.h"
#include "semanticindex.h"
#include "symboltaxonomy.h"
#include <QCoreApplication>
#include <QThread>
#include <QMetaObject>
#include <algorithm>
#include <utility>

namespace {
bool recordIsInModule(const SemanticSymbolRecord& record,
                      const SemanticSymbolRecord& moduleRecord)
{
    return record.location.fileName == moduleRecord.location.fileName
        && record.location.startLine > moduleRecord.location.startLine;
}
}

SymbolRelationshipEngine::SymbolRelationshipEngine(QObject *parent)
    : QObject(parent)
{
    relationshipGraph.reserve(1000);
    queryCache.reserve(500);
}

SymbolRelationshipEngine::SymbolRelationshipEngine(
    SymbolRecordProvider symbolRecordProvider,
    QObject *parent)
    : SymbolRelationshipEngine(parent)
{
    setSymbolRecordProvider(std::move(symbolRecordProvider));
}

SymbolRelationshipEngine::~SymbolRelationshipEngine()
{
}

void SymbolRelationshipEngine::setSymbolRecordProvider(
    SymbolRecordProvider symbolRecordProvider)
{
    m_symbolRecordProvider = std::move(symbolRecordProvider);
}

void SymbolRelationshipEngine::addRelationship(int fromSymbolId,
                                               int toSymbolId,
                                               RelationType type,
                                               const QString& context,
                                               int confidence,
                                               const SemanticSourceRange& evidenceRange)
{
    if (fromSymbolId == toSymbolId) return;

    if (hasRelationship(fromSymbolId, toSymbolId, type)) {
        return;
    }

    RelationshipEdge outgoingEdge(
        toSymbolId, type, context, confidence, evidenceRange);
    RelationshipEdge incomingEdge(
        fromSymbolId, type, context, confidence, evidenceRange);

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

SymbolRelationshipEngine::RelationshipEdgeMetadata
SymbolRelationshipEngine::getRelationshipMetadata(
    int fromSymbolId,
    int toSymbolId,
    RelationType type) const
{
    RelationshipEdgeMetadata metadata;
    if (!relationshipGraph.contains(fromSymbolId))
        return metadata;

    const RelationshipNode& node = relationshipGraph.value(fromSymbolId);
    for (const RelationshipEdge& edge : node.outgoingEdges) {
        if (edge.targetId != toSymbolId || edge.type != type)
            continue;
        metadata.found = true;
        metadata.context = edge.context;
        metadata.confidence = edge.confidence;
        metadata.evidenceRange = edge.evidenceRange;
        return metadata;
    }
    return metadata;
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

    const QList<SemanticSymbolRecord> fileRecords = symbolRecords(fileName);

    for (const SemanticSymbolRecord& record : std::as_const(fileRecords)) {
        if (SymbolTaxonomy::isModuleDeclaration(
                semanticMetadataForSymbolRecord(record))) {
            const int moduleHandle = record.localHandle;
            symbolsByFile[fileName].insert(moduleHandle);

            for (const SemanticSymbolRecord& otherRecord : std::as_const(fileRecords)) {
                if (otherRecord.localHandle != moduleHandle
                    && recordIsInModule(otherRecord, record)) {

                    addRelationship(moduleHandle, otherRecord.localHandle, CONTAINS);
                    symbolsByFile[fileName].insert(otherRecord.localHandle);
                }
            }
        } else {
            symbolsByFile[fileName].insert(record.localHandle);
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

    const QList<SemanticSymbolRecord> allRecords = symbolRecords();

    QSet<QString> files;
    for (const SemanticSymbolRecord& record : std::as_const(allRecords))
        files.insert(record.location.fileName);

    for (const QString& fileName : std::as_const(files))
        buildFileRelationships(fileName);
}

void SymbolRelationshipEngine::replaceRelationshipsFromSnapshot(
    const QList<SemanticSymbolRecord>& symbolRecords,
    const QList<SemanticRelationship>& relationships)
{
    relationshipGraph.clear();
    relationshipsByType.clear();
    symbolsByFile.clear();

    QSet<int> validHandles;
    validHandles.reserve(symbolRecords.size());
    for (const SemanticSymbolRecord& record : symbolRecords) {
        if (record.localHandle < 0)
            continue;
        validHandles.insert(record.localHandle);
        if (!record.location.fileName.isEmpty()) {
            symbolsByFile[record.location.fileName].insert(record.localHandle);
        }
    }

    QSet<QString> seenRelationships;
    seenRelationships.reserve(relationships.size());
    for (const SemanticRelationship& relationship : relationships) {
        if (relationship.fromId < 0 || relationship.toId < 0
            || relationship.fromId == relationship.toId
            || !validHandles.contains(relationship.fromId)
            || !validHandles.contains(relationship.toId)) {
            continue;
        }
        const QString relationshipKey = QStringLiteral("%1:%2:%3")
            .arg(relationship.fromId)
            .arg(relationship.toId)
            .arg(static_cast<int>(relationship.type));
        if (seenRelationships.contains(relationshipKey))
            continue;
        seenRelationships.insert(relationshipKey);

        relationshipGraph[relationship.fromId].outgoingEdges.append(
            RelationshipEdge(relationship.toId,
                             relationship.type,
                             relationship.evidenceText,
                             relationship.confidence,
                             relationship.evidenceRange));
        relationshipGraph[relationship.toId].incomingEdges.append(
            RelationshipEdge(relationship.fromId,
                             relationship.type,
                             relationship.evidenceText,
                             relationship.confidence,
                             relationship.evidenceRange));
        addToTypeIndex(relationship.fromId,
                       relationship.toId,
                       relationship.type);
    }

    invalidateCache();
    emit relationshipsReplaced();
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

QList<SemanticSymbolRecord> SymbolRelationshipEngine::symbolRecords(
    const QString& fileName) const
{
    if (m_symbolRecordProvider)
        return m_symbolRecordProvider(fileName);
    return {};
}
