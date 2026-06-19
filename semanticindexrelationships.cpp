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

sym_list::SymbolInfo symbolByLocalHandle(const SemanticIndex& index,
                                         int symbolId)
{
    if (const std::shared_ptr<const SemanticIndexSnapshot> snapshot =
            index.snapshot()) {
        if (symbolId >= 0) {
            for (const sym_list::SymbolInfo& symbol : snapshot->getSymbols()) {
                if (symbol.symbolId == symbolId)
                    return symbol;
            }
        }
    }

    if (index.symbolDatabase()) {
        const sym_list::SymbolInfo symbol =
            index.symbolDatabase()->getSymbolById(symbolId);
        if (symbol.symbolId >= 0 || !symbol.symbolName.isEmpty())
            return symbol;
    }

    for (const sym_list::SymbolInfo& symbol : index.getSymbols()) {
        if (symbol.symbolId == symbolId)
            return symbol;
    }

    sym_list::SymbolInfo missing;
    missing.symbolId = -1;
    return missing;
}

SemanticSymbolRecord recordByLocalHandle(const SemanticIndex& index,
                                         int symbolId)
{
    if (symbolId < 0)
        return {};

    if (const std::shared_ptr<const SemanticIndexSnapshot> snapshot =
            index.snapshot()) {
        const QList<SemanticSymbolRecord> records = snapshot->getSymbolRecords();
        for (const SemanticSymbolRecord& record : records) {
            if (record.localHandle == symbolId)
                return record;
        }
    }

    for (const sym_list::SymbolInfo& symbol : index.getSymbols()) {
        const SemanticSymbolRecord record = semanticSymbolRecordForSymbol(symbol);
        if (record.localHandle == symbolId)
            return record;
    }
    return {};
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

    return symbolByLocalHandle(index,
                               fromEndpoint
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
        item.fromSymbol = relationshipEndpointSymbol(*this, item.relationship, true);
        item.toSymbol = relationshipEndpointSymbol(*this, item.relationship, false);
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
