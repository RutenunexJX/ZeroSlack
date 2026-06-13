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
        const QString key = symbol.symbolName.toCaseFolded();
        if (seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(symbol.symbolName);
    }
    result.sort(Qt::CaseInsensitive);
    return result;
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

    const int symbolId = findSymbolId(symbolName);
    if (symbolId < 0)
        return {};

    QList<sym_list::SymbolInfo> symbols;
    const QList<SemanticRelationship> relationships = getRelationships(symbolId, outgoing);
    for (const SemanticRelationship& relationship : relationships) {
        if (!types.isEmpty() && !types.contains(relationship.type))
            continue;

        const int peerId = outgoing ? relationship.toId : relationship.fromId;
        const sym_list::SymbolInfo symbol = getSymbolById(peerId);
        if (symbol.symbolId < 0)
            continue;
        if (!relationshipCompletionNameMatches(symbol.symbolName, prefix))
            continue;
        symbols.append(symbol);
    }

    return uniqueSortedRelationshipSymbolNames(symbols);
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
        if (!relationshipCompletionNameMatches(symbol.symbolName, prefix))
            continue;

        const QList<SemanticRelationship> relationships =
            getRelationships(symbol.symbolId, true);
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
        if (candidate.symbolName == symbolName
            && candidate.moduleScope == moduleName) {
            return 20;
        }
    }
    return 0;
}
