#include "semanticindex.h"

#include "semanticindexsnapshot.h"

#include <QSet>

QList<SemanticRelationship> SemanticIndex::relationshipsForStableKey(
    const SymbolStableKey& key,
    bool outgoing) const
{
    return m_snapshot ? m_snapshot->relationshipsForStableKey(key, outgoing)
                      : QList<SemanticRelationship>{};
}

QList<SemanticRelationshipResult> SemanticIndex::getRelationshipResults(
    const SymbolStableKey& key,
    bool outgoing) const
{
    QList<SemanticRelationshipResult> result;
    const QList<SemanticRelationship> relationships =
        relationshipsForStableKey(key, outgoing);
    result.reserve(relationships.size());
    for (const SemanticRelationship& relationship : relationships) {
        SemanticRelationshipResult item;
        item.relationship = m_snapshot
            ? m_snapshot->rebindRelationship(relationship) : relationship;
        item.fromSymbolRecord = getSymbolRecordByStableKey(item.relationship.fromStableKey);
        item.toSymbolRecord = getSymbolRecordByStableKey(item.relationship.toStableKey);
        item.fromStableKey = item.relationship.fromStableKey;
        item.toStableKey = item.relationship.toStableKey;
        item.fromAccessPath = item.relationship.fromAccessPath;
        item.toAccessPath = item.relationship.toAccessPath;
        item.provenance = item.relationship.provenance;
        item.confidence = item.relationship.confidence;
        item.evidenceText = item.relationship.evidenceText;
        item.evidenceRange = item.relationship.evidenceRange;
        item.exactValueForward = item.relationship.exactValueForward;
        result.append(item);
    }
    return result;
}

QList<SymbolRelationshipEngine::RelationType> SemanticIndex::relationshipTypes() const
{
    return {
        SymbolRelationshipEngine::CONTAINS,
        SymbolRelationshipEngine::REFERENCES,
        SymbolRelationshipEngine::INSTANTIATES,
        SymbolRelationshipEngine::CALLS,
        SymbolRelationshipEngine::INHERITS,
        SymbolRelationshipEngine::IMPLEMENTS,
        SymbolRelationshipEngine::ASSIGNS_TO,
        SymbolRelationshipEngine::READS_FROM,
        SymbolRelationshipEngine::CLOCKS,
        SymbolRelationshipEngine::RESETS,
        SymbolRelationshipEngine::GENERATES,
        SymbolRelationshipEngine::CONSTRAINS,
    };
}
