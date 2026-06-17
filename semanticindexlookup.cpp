#include "semanticindex.h"

#include "semanticindexlookuphelpers.h"
#include "semanticindexsnapshot.h"
#include "symboltaxonomy.h"

#include <QSet>
#include <algorithm>

using namespace semantic_index_lookup;

namespace {

SemanticDefinitionResult combinedDefinitionMissEvidence(
    const SemanticDefinitionResult& local,
    const SemanticDefinitionResult& global)
{
    SemanticDefinitionResult combined;
    combined.inspectedCandidateCount =
        local.inspectedCandidateCount + global.inspectedCandidateCount;
    combined.matchingNameCandidateCount =
        local.matchingNameCandidateCount + global.matchingNameCandidateCount;
    combined.typeCompatibleCandidateCount =
        local.typeCompatibleCandidateCount + global.typeCompatibleCandidateCount;
    combined.visibleCandidateCount =
        local.visibleCandidateCount + global.visibleCandidateCount;

    if (combined.inspectedCandidateCount == 0) {
        combined.missReason = SemanticDefinitionMissReason::NoCandidateSymbols;
    } else if (combined.matchingNameCandidateCount == 0) {
        combined.missReason = SemanticDefinitionMissReason::NoMatchingName;
    } else if (combined.typeCompatibleCandidateCount == 0) {
        combined.missReason = SemanticDefinitionMissReason::StructMemberTypeMismatch;
    } else {
        combined.missReason = SemanticDefinitionMissReason::NotVisibleInContext;
    }
    return combined;
}

}

QList<SemanticSymbolSearchResult> SemanticIndex::searchSymbols(
    const SemanticSymbolSearchQuery& query) const
{
    QList<SemanticSymbolSearchResult> result;
    const QList<sym_list::SymbolInfo> symbols = getSymbols(query.fileName);
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (!symbolSearchTypeMatches(symbol, query.types, query.intent))
            continue;

        const int score = symbolSearchMatchScore(symbol.symbolName, query);
        if (score <= 0)
            continue;

        SemanticSymbolSearchResult item;
        item.symbol = symbol;
        item.score = score;
        result.append(item);
    }

    std::stable_sort(result.begin(), result.end(),
                     [](const SemanticSymbolSearchResult& a,
                        const SemanticSymbolSearchResult& b) {
        if (a.score != b.score)
            return a.score > b.score;
        if (a.symbol.fileName != b.symbol.fileName)
            return a.symbol.fileName < b.symbol.fileName;
        if (a.symbol.startLine != b.symbol.startLine)
            return a.symbol.startLine < b.symbol.startLine;
        return a.symbol.symbolName < b.symbol.symbolName;
    });

    if (query.maxResults >= 0 && result.size() > query.maxResults)
        result = result.mid(0, query.maxResults);
    return result;
}

sym_list::SymbolInfo SemanticIndex::getSymbolById(int symbolId) const
{
    if (m_snapshot)
        return m_snapshot->getSymbolById(symbolId);

    if (symbolId < 0) {
        sym_list::SymbolInfo missing;
        missing.symbolId = -1;
        return missing;
    }

    sym_list::SymbolInfo symbol = symbolDatabase()->getSymbolById(symbolId);
    if (symbol.symbolId != -1)
        return symbol;

    const QList<sym_list::SymbolInfo> allSymbols = getSymbols();
    for (const sym_list::SymbolInfo& candidate : allSymbols) {
        if (candidate.symbolId == symbolId)
            return candidate;
    }

    sym_list::SymbolInfo missing;
    missing.symbolId = -1;
    return missing;
}

sym_list::SymbolInfo SemanticIndex::getSymbolByStableKey(
    const SymbolStableKey& key) const
{
    if (m_snapshot)
        return m_snapshot->getSymbolByStableKey(key);

    if (!key.isValid()) {
        sym_list::SymbolInfo missing;
        missing.symbolId = -1;
        return missing;
    }

    const QList<sym_list::SymbolInfo> allSymbols = getSymbols();
    for (const sym_list::SymbolInfo& symbol : allSymbols) {
        if (symbolStableKeyForSymbol(symbol) == key)
            return symbol;
    }

    sym_list::SymbolInfo missing;
    missing.symbolId = -1;
    return missing;
}

SemanticDefinitionResult SemanticIndex::resolveDefinition(
    const SemanticDefinitionQuery& query) const
{
    SemanticDefinitionResult empty;
    if (query.symbolName.isEmpty()) {
        empty.missReason = SemanticDefinitionMissReason::EmptySymbolName;
        return empty;
    }

    SemanticDefinitionResult local = bestDefinitionFromCandidates(
        getSymbols(query.fileName),
        query,
        true);
    if (local.found)
        return local;

    QList<sym_list::SymbolInfo> globalCandidates = findDefinitions(query.symbolName);
    const QString queryFile = normalizedLookupFileName(query.fileName);
    globalCandidates.erase(
        std::remove_if(globalCandidates.begin(), globalCandidates.end(),
                       [&queryFile](const sym_list::SymbolInfo& symbol) {
                           return normalizedLookupFileName(symbol.fileName) == queryFile;
                       }),
        globalCandidates.end());
    SemanticDefinitionResult global =
        bestDefinitionFromCandidates(globalCandidates, query, false);
    if (global.found)
        return global;
    return combinedDefinitionMissEvidence(local, global);
}

QList<sym_list::SymbolInfo> SemanticIndex::findDefinitionSymbols(
    const SemanticDefinitionQuery& query) const
{
    QList<sym_list::SymbolInfo> result;
    const SemanticDefinitionResult resolved = resolveDefinition(query);
    if (resolved.found)
        result.append(resolved.symbol);
    return result;
}

int SemanticIndex::findSymbolId(const QString& name,
                                const SemanticQueryContext& context) const
{
    if (m_snapshot)
        return m_snapshot->findSymbolId(name, context);

    const QList<sym_list::SymbolInfo> symbols = findDefinitions(name, context);
    if (symbols.isEmpty())
        return -1;
    return symbols.first().symbolId;
}

int SemanticIndex::findSymbolId(const SymbolStableKey& key) const
{
    const sym_list::SymbolInfo symbol = getSymbolByStableKey(key);
    return symbol.symbolId;
}

QList<sym_list::SymbolInfo> SemanticIndex::findDefinitions(
    const QString& name,
    const SemanticQueryContext& context) const
{
    if (name.isEmpty())
        return {};

    if (m_snapshot)
        return m_snapshot->findDefinitions(name, context);

    QList<sym_list::SymbolInfo> symbols = symbolDatabase()->findSymbolsByName(name);
    if (symbols.isEmpty())
        return symbols;

    return sortedDefinitions(symbols, context);
}

QList<sym_list::SymbolInfo> SemanticIndex::sortedDefinitions(
    const QList<sym_list::SymbolInfo>& symbols,
    const SemanticQueryContext& context) const
{
    QList<sym_list::SymbolInfo> sorted = symbols;
    const QString normalizedContextFile = normalizedLookupFileName(context.fileName);
    std::stable_sort(sorted.begin(), sorted.end(),
                     [&context, &normalizedContextFile](const sym_list::SymbolInfo& a,
                                                        const sym_list::SymbolInfo& b) {
        auto score = [&context, &normalizedContextFile](const sym_list::SymbolInfo& s) {
            int value = 0;
            if (!normalizedContextFile.isEmpty()
                && normalizedLookupFileName(s.fileName) == normalizedContextFile)
                value += 100;
            if (!context.moduleName.isEmpty() && s.moduleScope == context.moduleName)
                value += 50;
            if (SymbolTaxonomy::isGlobalDefinition(
                    SymbolTaxonomy::semanticMetadata(s))) {
                value += 10;
            }
            return value;
        };

        const int aScore = score(a);
        const int bScore = score(b);
        if (aScore != bScore)
            return aScore > bScore;
        if (a.fileName != b.fileName)
            return a.fileName < b.fileName;
        if (a.startLine != b.startLine)
            return a.startLine < b.startLine;
        return a.symbolId < b.symbolId;
    });
    return sorted;
}

SemanticDefinitionResult SemanticIndex::bestDefinitionFromCandidates(
    const QList<sym_list::SymbolInfo>& candidates,
    const SemanticDefinitionQuery& query,
    bool localFile) const
{
    SemanticDefinitionResult best;
    best.localFile = localFile;
    int bestPriority = 999;
    const QSet<QString> packages =
        SymbolTaxonomy::packageScopeNames(getSymbols());

    for (const sym_list::SymbolInfo& symbol : candidates) {
        ++best.inspectedCandidateCount;
        if (!semanticDefinitionSymbolMatches(symbol, query.symbolName))
            continue;
        ++best.matchingNameCandidateCount;
        if (semanticDefinitionSkipForStructMemberType(symbol, query))
            continue;
        ++best.typeCompatibleCandidateCount;
        if (!SymbolTaxonomy::isDefinitionVisibleInContext(
                symbol,
                query.moduleName,
                packages)) {
            continue;
        }
        ++best.visibleCandidateCount;

        int priority = semanticDefinitionTypePriority(symbol)
            + SymbolTaxonomy::definitionContextPriorityAdjustment(
                symbol,
                query.moduleName,
                packages);

        if (!best.found || priority < bestPriority) {
            best.found = true;
            best.localFile = localFile;
            best.symbol = symbol;
            best.symbolStableKey = symbolStableKeyForSymbol(symbol);
            best.missReason = SemanticDefinitionMissReason::None;
            bestPriority = priority;
        }
    }

    if (!best.found) {
        if (best.inspectedCandidateCount == 0) {
            best.missReason = SemanticDefinitionMissReason::NoCandidateSymbols;
        } else if (best.matchingNameCandidateCount == 0) {
            best.missReason = SemanticDefinitionMissReason::NoMatchingName;
        } else if (best.typeCompatibleCandidateCount == 0) {
            best.missReason = SemanticDefinitionMissReason::StructMemberTypeMismatch;
        } else {
            best.missReason = SemanticDefinitionMissReason::NotVisibleInContext;
        }
    }
    return best;
}
