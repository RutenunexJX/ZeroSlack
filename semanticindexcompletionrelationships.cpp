#include "semanticindex.h"

#include <QSet>

namespace {
bool relationshipCompletionNameMatches(const QString& name, const QString& prefix)
{
    if (prefix.isEmpty())
        return true;
    if (name.isEmpty())
        return false;

    const QString lowerName = name.toLower();
    const QString lowerPrefix = prefix.toLower();
    if (lowerName.startsWith(lowerPrefix))
        return true;

    int namePos = 0;
    int prefixPos = 0;
    while (prefixPos < lowerPrefix.length() && namePos < lowerName.length()) {
        if (lowerPrefix.at(prefixPos) == lowerName.at(namePos))
            ++prefixPos;
        ++namePos;
    }
    return prefixPos == lowerPrefix.length();
}

QStringList uniqueSortedRelationshipSymbolNames(const QList<sym_list::SymbolInfo>& symbols)
{
    QStringList result;
    QSet<QString> seenNames;
    for (const sym_list::SymbolInfo& symbol : symbols) {
        const SemanticSymbolRecord record = semanticSymbolRecordForSymbol(symbol);
        const QString displayName =
            record.name.isEmpty() ? symbol.symbolName : record.name;
        const QString key = displayName.toCaseFolded();
        if (seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(displayName);
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

QString relationshipRecordDisplayName(const SemanticSymbolRecord& record)
{
    return record.name;
}

QString relationshipRecordDisplayName(
    const SemanticSymbolRecord& record,
    const sym_list::SymbolInfo& fallback)
{
    return record.name.isEmpty() ? fallback.symbolName : record.name;
}

QString relationshipRecordOwnerName(
    const SemanticSymbolRecord& record,
    const sym_list::SymbolInfo& fallback)
{
    if (!record.owner.name.isEmpty())
        return record.owner.name;
    return semanticSymbolRecordForSymbol(fallback).owner.name;
}

}

QStringList SemanticIndex::getRelationshipCompletionNames(
    const QString& symbolName,
    const QList<SymbolRelationshipEngine::RelationType>& types,
    bool outgoing,
    const QString& prefix) const
{
    if (symbolName.isEmpty())
        return {};

    SemanticDefinitionQuery definitionQuery;
    definitionQuery.symbolName = symbolName;
    const SemanticDefinitionResult definition = resolveDefinition(definitionQuery);
    if (!definition.found)
        return {};
    if (!definition.symbolStableKey.isValid())
        return {};

    QStringList names;
    QSet<QString> seenNames;
    const QList<SemanticRelationshipResult> relationships =
        getRelationshipResults(definition.symbolStableKey, outgoing);
    for (const SemanticRelationshipResult& relationship : relationships) {
        if (!types.isEmpty() && !types.contains(relationship.relationship.type))
            continue;

        const SemanticSymbolRecord record =
            outgoing ? relationship.toSymbolRecord : relationship.fromSymbolRecord;
        const QString displayName = relationshipRecordDisplayName(record);
        if (displayName.isEmpty())
            continue;
        if (!relationshipCompletionNameMatches(displayName, prefix))
            continue;
        const QString key = displayName.toCaseFolded();
        if (seenNames.contains(key))
            continue;
        seenNames.insert(key);
        names.append(displayName);
    }

    names.sort(Qt::CaseInsensitive);
    return names;
}

QStringList SemanticIndex::getBidirectionalRelationshipCompletionNames(
    const QString& symbolName,
    const QList<SymbolRelationshipEngine::RelationType>& types,
    const QString& prefix) const
{
    QStringList result = getRelationshipCompletionNames(symbolName, types, true, prefix);
    result.append(getRelationshipCompletionNames(symbolName, types, false, prefix));
    result.removeDuplicates();
    result.sort(Qt::CaseInsensitive);
    return result;
}

QStringList SemanticIndex::getSymbolsWithOutgoingRelationshipCompletionNames(
    SymbolRelationshipEngine::RelationType type,
    const QString& prefix) const
{
    QList<sym_list::SymbolInfo> result;
    const QList<sym_list::SymbolInfo> symbols = getSymbols();
    for (const sym_list::SymbolInfo& symbol : symbols) {
        const SemanticSymbolRecord record = semanticSymbolRecordForSymbol(symbol);
        if (!relationshipCompletionNameMatches(
                relationshipRecordDisplayName(record, symbol), prefix))
            continue;

        const SymbolStableKey symbolStableKey = record.stableKey.isValid()
            ? record.stableKey
            : symbolStableKeyForSymbol(symbol);
        if (!symbolStableKey.isValid())
            continue;
        const QList<SemanticRelationship> relationships =
            getRelationships(symbolStableKey, true);
        for (const SemanticRelationship& relationship : relationships) {
            if (relationship.type == type) {
                result.append(symbol);
                break;
            }
        }
    }

    return uniqueSortedRelationshipSymbolNames(result);
}

bool SemanticIndex::hasRelationshipFacts() const
{
    if (m_snapshot)
        return true;
    return symbolDatabase()->getRelationshipEngine() != nullptr;
}

int SemanticIndex::scopeScoreForSymbol(const QString& symbolName,
                                       const QString& moduleName) const
{
    if (symbolName.isEmpty() || moduleName.isEmpty())
        return 0;

    const QList<sym_list::SymbolInfo> symbols = getSymbols();
    for (const sym_list::SymbolInfo& candidate : symbols) {
        const SemanticSymbolRecord record =
            semanticSymbolRecordForSymbol(candidate);
        if (relationshipRecordDisplayName(record, candidate) == symbolName
            && relationshipRecordOwnerName(record, candidate) == moduleName) {
            return 20;
        }
    }
    return 0;
}
