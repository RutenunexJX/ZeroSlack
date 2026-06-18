#include "semanticindex.h"

#include "semanticindexsnapshot.h"

#include <QSet>

namespace {
SymbolStableKey relationshipStableKeyForSymbol(const sym_list::SymbolInfo& symbol)
{
    const SemanticSymbolRecord record = semanticSymbolRecordForSymbol(symbol);
    return record.stableKey.isValid()
        ? record.stableKey
        : symbolStableKeyForSymbol(symbol);
}

sym_list::SymbolInfo relationshipEndpointSymbol(const SemanticIndex& index,
                                                const SemanticRelationship& relationship,
                                                bool fromEndpoint)
{
    const SymbolStableKey stableKey = fromEndpoint
        ? relationship.fromStableKey
        : relationship.toStableKey;
    if (stableKey.isValid()) {
        const sym_list::SymbolInfo symbol = index.getSymbolByStableKey(stableKey);
        if (symbol.symbolId >= 0 || !symbol.symbolName.isEmpty())
            return symbol;
    }

    return index.getSymbolById(fromEndpoint
                                   ? relationship.fromId
                                   : relationship.toId);
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

    return relationshipStableKeyForSymbol(
        relationshipEndpointSymbol(index, relationship, fromEndpoint));
}
}

QList<SemanticRelationship> SemanticIndex::getRelationships(const QString& scopeName,
                                                            bool outgoing) const
{
    const QList<sym_list::SymbolInfo> defs = findDefinitions(scopeName);
    if (defs.isEmpty())
        return {};
    return getRelationships(relationshipStableKeyForSymbol(defs.first()), outgoing);
}

QList<SemanticRelationship> SemanticIndex::getRelationships(
    const SymbolStableKey& key,
    bool outgoing) const
{
    if (m_snapshot)
        return m_snapshot->getRelationships(key, outgoing);

    if (!key.isValid())
        return {};

    const sym_list::SymbolInfo subject = getSymbolByStableKey(key);
    if (subject.symbolId < 0)
        return {};

    QList<SemanticRelationship> result;
    SymbolRelationshipEngine* engine = symbolDatabase()->getRelationshipEngine();
    if (!engine)
        return result;

    QSet<QString> seen;
    for (SymbolRelationshipEngine::RelationType type : relationshipTypes()) {
        const QList<int> related = engine->getRelatedSymbols(subject.symbolId,
                                                             type,
                                                             outgoing);
        for (int otherId : related) {
            SemanticRelationship rel;
            rel.fromId = outgoing ? subject.symbolId : otherId;
            rel.toId = outgoing ? otherId : subject.symbolId;
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
    const QList<SemanticRelationship> relationships = getRelationships(key, outgoing);
    result.reserve(relationships.size());
    for (const SemanticRelationship& relationship : relationships) {
        SemanticRelationshipResult item;
        item.relationship = relationship;
        item.fromSymbol = relationshipEndpointSymbol(*this, relationship, true);
        item.toSymbol = relationshipEndpointSymbol(*this, relationship, false);
        item.fromSymbolRecord = semanticSymbolRecordForSymbol(item.fromSymbol);
        item.toSymbolRecord = semanticSymbolRecordForSymbol(item.toSymbol);
        if (!item.relationship.fromStableKey.isValid())
            item.relationship.fromStableKey =
                item.fromSymbolRecord.stableKey.isValid()
                    ? item.fromSymbolRecord.stableKey
                    : symbolStableKeyForSymbol(item.fromSymbol);
        if (!item.relationship.toStableKey.isValid())
            item.relationship.toStableKey =
                item.toSymbolRecord.stableKey.isValid()
                    ? item.toSymbolRecord.stableKey
                    : symbolStableKeyForSymbol(item.toSymbol);
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
    const QList<sym_list::SymbolInfo> defs = findDefinitions(scopeName);
    if (defs.isEmpty())
        return {};
    return getRelationshipResults(relationshipStableKeyForSymbol(defs.first()), outgoing);
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
