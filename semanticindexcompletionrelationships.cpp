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

QStringList uniqueSortedRelationshipRecordNames(
    const QList<SemanticSymbolRecord>& records)
{
    QStringList result;
    QSet<QString> seenNames;
    for (const SemanticSymbolRecord& record : records) {
        const QString key = record.name.toCaseFolded();
        if (seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(record.name);
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

QString relationshipRecordDisplayName(const SemanticSymbolRecord& record)
{
    return record.name;
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
    QList<SemanticSymbolRecord> result;
    const QList<SemanticSymbolRecord> records = getSymbolRecords();
    for (const SemanticSymbolRecord& record : records) {
        if (!relationshipCompletionNameMatches(
                relationshipRecordDisplayName(record), prefix))
            continue;

        if (!record.stableKey.isValid())
            continue;
        const QList<SemanticRelationship> relationships =
            relationshipsForStableKey(record.stableKey, true);
        for (const SemanticRelationship& relationship : relationships) {
            if (relationship.type == type) {
                result.append(record);
                break;
            }
        }
    }

    return uniqueSortedRelationshipRecordNames(result);
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

    const QList<SemanticSymbolRecord> records = getSymbolRecords();
    for (const SemanticSymbolRecord& record : records) {
        if (relationshipRecordDisplayName(record) == symbolName
            && record.owner.name == moduleName) {
            return 20;
        }
    }
    return 0;
}
