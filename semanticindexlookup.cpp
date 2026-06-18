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

SymbolTaxonomy::SemanticMetadata metadataForRecord(
    const SemanticSymbolRecord& record)
{
    SymbolTaxonomy::SemanticMetadata metadata;
    metadata.declarationKind = record.declarationKind;
    metadata.usageRole = record.usageRole;
    metadata.ownerScope = record.owner.kind;
    metadata.visibility = record.visibility;
    metadata.sourceRole = record.sourceRole;
    metadata.rawCollectorKind = record.rawCollectorKind;
    metadata.interfaceLikeOwner = record.owner.interfaceLike;
    return metadata;
}

bool definitionRecordVisibleInContext(
    const SemanticSymbolRecord& record,
    const QString& moduleName)
{
    const SymbolTaxonomy::SemanticMetadata metadata = metadataForRecord(record);
    return SymbolTaxonomy::isMemberScopeDefinitionCandidate(metadata)
        || metadata.rawCollectorKind == sym_list::sym_enum_value
        || SymbolTaxonomy::isGlobalDefinition(metadata)
        || moduleName.isEmpty()
        || record.owner.name == moduleName
        || metadata.visibility == SymbolTaxonomy::SymbolVisibility::PackageVisible;
}

int definitionRecordContextPriorityAdjustment(
    const SemanticSymbolRecord& record,
    const QString& moduleName)
{
    if (!moduleName.isEmpty() && record.owner.name == moduleName)
        return -100;
    if (record.visibility == SymbolTaxonomy::SymbolVisibility::PackageVisible)
        return -20;
    return 0;
}

QString definitionSortOwnerName(const sym_list::SymbolInfo& symbol)
{
    return semanticSymbolRecordForSymbol(symbol).owner.name;
}

}

QList<SemanticSymbolSearchResult> SemanticIndex::searchSymbols(
    const SemanticSymbolSearchQuery& query) const
{
    QList<SemanticSymbolSearchResult> result;
    const QList<sym_list::SymbolInfo> symbols = getSymbols(query.fileName);
    for (const sym_list::SymbolInfo& symbol : symbols) {
        const SemanticSymbolRecord record = semanticSymbolRecordForSymbol(symbol);
        if (!symbolSearchTypeMatches(record, query.types, query.intent))
            continue;

        const int score = symbolSearchMatchScore(record.name, query);
        if (score <= 0)
            continue;

        SemanticSymbolSearchResult item;
        item.symbol = symbol;
        item.symbolRecord = record;
        item.symbolStableKey = item.symbolRecord.stableKey;
        item.score = score;
        result.append(item);
    }

    std::stable_sort(result.begin(), result.end(),
                     [](const SemanticSymbolSearchResult& a,
                        const SemanticSymbolSearchResult& b) {
        if (a.score != b.score)
            return a.score > b.score;
        if (a.symbolRecord.location.fileName != b.symbolRecord.location.fileName)
            return a.symbolRecord.location.fileName
                < b.symbolRecord.location.fileName;
        if (a.symbolRecord.location.startLine != b.symbolRecord.location.startLine)
            return a.symbolRecord.location.startLine
                < b.symbolRecord.location.startLine;
        return a.symbolRecord.name < b.symbolRecord.name;
    });

    if (query.maxResults >= 0 && result.size() > query.maxResults)
        result = result.mid(0, query.maxResults);
    return result;
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
            if (!context.moduleName.isEmpty()
                && definitionSortOwnerName(s) == context.moduleName)
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
        const SemanticSymbolRecord record =
            semanticSymbolRecordForSymbol(symbol, packages);
        ++best.inspectedCandidateCount;
        if (!semanticDefinitionRecordMatches(record, query.symbolName))
            continue;
        ++best.matchingNameCandidateCount;
        if (semanticDefinitionSkipForStructMemberType(record, query))
            continue;
        ++best.typeCompatibleCandidateCount;
        if (!definitionRecordVisibleInContext(record, query.moduleName)) {
            continue;
        }
        ++best.visibleCandidateCount;

        int priority = semanticDefinitionTypePriority(record)
            + definitionRecordContextPriorityAdjustment(record, query.moduleName);

        if (!best.found || priority < bestPriority) {
            best.found = true;
            best.localFile = localFile;
            best.symbol = symbol;
            best.symbolRecord = record;
            best.symbolStableKey = best.symbolRecord.stableKey;
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
