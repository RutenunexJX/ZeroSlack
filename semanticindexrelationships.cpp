#include "semanticindex.h"

#include "semanticindexsnapshot.h"

#include <QSet>

namespace {
SemanticSymbolRecord recordByLocalHandle(const SemanticIndex& index,
                                         int localHandle)
{
    if (localHandle < 0)
        return {};

    if (const std::shared_ptr<const SemanticIndexSnapshot> snapshot =
            index.snapshot()) {
        const QList<SemanticSymbolRecord> records = snapshot->getSymbolRecords();
        for (const SemanticSymbolRecord& record : records) {
            if (record.localHandle == localHandle)
                return record;
        }
    }

    for (const SemanticSymbolRecord& record : index.getSymbolRecords()) {
        if (record.localHandle == localHandle)
            return record;
    }
    return {};
}

SymbolStableKey relationshipEndpointStableKey(
    const SemanticIndex& index,
    const SemanticRelationship& relationship,
    bool fromEndpoint)
{
    const SymbolStableKey stableKey = fromEndpoint
        ? relationship.fromStableKey
        : relationship.toStableKey;
    if (stableKey.isValid())
        return stableKey;

    const SemanticSymbolRecord record =
        recordByLocalHandle(index,
                            fromEndpoint
                                ? relationship.fromId
                                : relationship.toId);
    if (record.stableKey.isValid())
        return record.stableKey;

    return {};
}

SemanticSymbolRecord relationshipEndpointRecord(
    const SemanticIndex& index,
    const SemanticRelationship& relationship,
    bool fromEndpoint)
{
    const SymbolStableKey stableKey = fromEndpoint
        ? relationship.fromStableKey
        : relationship.toStableKey;
    if (stableKey.isValid()) {
        const SemanticSymbolRecord record =
            index.getSymbolRecordByStableKey(stableKey);
        if (record.isValid())
            return record;
    }

    return recordByLocalHandle(index,
                               fromEndpoint
                                   ? relationship.fromId
                                   : relationship.toId);
}
}

QList<SemanticRelationship> SemanticIndex::relationshipsForScopeName(
    const QString& scopeName,
    bool outgoing) const
{
    const QList<SemanticSymbolRecord> defs = findDefinitionRecords(scopeName);
    if (defs.isEmpty())
        return {};
    return relationshipsForStableKey(defs.first().stableKey, outgoing);
}

QList<SemanticRelationship> SemanticIndex::relationshipsForStableKey(
    const SymbolStableKey& key,
    bool outgoing) const
{
    if (m_snapshot)
        return m_snapshot->relationshipsForStableKey(key, outgoing);

    if (!key.isValid())
        return {};

    const SemanticSymbolRecord subject = getSymbolRecordByStableKey(key);
    if (subject.localHandle < 0)
        return {};

    QList<SemanticRelationship> result;
    SymbolRelationshipEngine* engine = relationshipEngine();
    if (!engine)
        return result;

    QSet<QString> seen;
    for (SymbolRelationshipEngine::RelationType type : relationshipTypes()) {
        const QList<int> related = engine->getRelatedSymbols(subject.localHandle,
                                                             type,
                                                             outgoing);
        for (int otherHandle : related) {
            SemanticRelationship rel;
            rel.fromId = outgoing ? subject.localHandle : otherHandle;
            rel.toId = outgoing ? otherHandle : subject.localHandle;
            rel.type = type;
            rel.fromStableKey = relationshipEndpointStableKey(*this, rel, true);
            rel.toStableKey = relationshipEndpointStableKey(*this, rel, false);
            const SymbolRelationshipEngine::RelationshipEdgeMetadata metadata =
                engine->getRelationshipMetadata(rel.fromId, rel.toId, rel.type);
            if (metadata.found) {
                rel.provenance = RelationshipProvenance::Inferred;
                rel.confidence = metadata.confidence;
                rel.evidenceText = metadata.context;
            }

            const QString dedupeKey = semanticRelationshipStableKeyText(rel).isEmpty()
                ? QStringLiteral("local:%1:%2:%3")
                      .arg(rel.fromId)
                      .arg(rel.toId)
                      .arg(static_cast<int>(rel.type))
                : semanticRelationshipStableKeyText(rel);
            if (seen.contains(dedupeKey))
                continue;
            seen.insert(dedupeKey);
            result.append(rel);
        }
    }

    return result;
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
        item.relationship = relationship;
        item.fromSymbolRecord =
            relationshipEndpointRecord(*this, item.relationship, true);
        item.toSymbolRecord =
            relationshipEndpointRecord(*this, item.relationship, false);
        if (!item.relationship.fromStableKey.isValid())
            item.relationship.fromStableKey =
                item.fromSymbolRecord.stableKey.isValid()
                    ? item.fromSymbolRecord.stableKey
                    : relationshipEndpointStableKey(*this, item.relationship, true);
        if (!item.relationship.toStableKey.isValid())
            item.relationship.toStableKey =
                item.toSymbolRecord.stableKey.isValid()
                    ? item.toSymbolRecord.stableKey
                    : relationshipEndpointStableKey(*this, item.relationship, false);
        item.fromStableKey = item.relationship.fromStableKey;
        item.toStableKey = item.relationship.toStableKey;
        item.provenance = item.relationship.provenance;
        item.confidence = item.relationship.confidence;
        item.evidenceText = item.relationship.evidenceText;
        result.append(item);
    }
    return result;
}

QList<SemanticRelationshipResult> SemanticIndex::getRelationshipResults(
    const QString& scopeName,
    bool outgoing) const
{
    const QList<SemanticSymbolRecord> defs = findDefinitionRecords(scopeName);
    if (defs.isEmpty())
        return {};
    return getRelationshipResults(defs.first().stableKey, outgoing);
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
