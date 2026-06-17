#include "definitionservice.h"

#include "completionservice.h"
#include "symboltaxonomy.h"

std::unique_ptr<DefinitionService> DefinitionService::instance = nullptr;

namespace {
SemanticDefinitionQuery toSemanticDefinitionQuery(const DefinitionQuery& query)
{
    SemanticDefinitionQuery semanticQuery;
    semanticQuery.symbolName = query.symbolName;
    semanticQuery.fileName = query.fileName;
    semanticQuery.moduleName = query.moduleName;
    semanticQuery.structTypeNameForMember = query.structTypeNameForMember;
    return semanticQuery;
}

DefinitionResult toDefinitionResult(const SemanticDefinitionResult& semanticResult)
{
    DefinitionResult result;
    result.found = semanticResult.found;
    result.localFile = semanticResult.localFile;
    result.symbol = semanticResult.symbol;
    result.symbolRecord = semanticResult.symbolRecord;
    result.symbolStableKey = result.symbolRecord.stableKey;
    result.inspectedCandidateCount = semanticResult.inspectedCandidateCount;
    result.matchingNameCandidateCount = semanticResult.matchingNameCandidateCount;
    result.typeCompatibleCandidateCount = semanticResult.typeCompatibleCandidateCount;
    result.visibleCandidateCount = semanticResult.visibleCandidateCount;
    result.missReason = semanticResult.missReason;
    return result;
}

}

DefinitionService* DefinitionService::getInstance()
{
    if (!instance)
        instance = std::make_unique<DefinitionService>();
    return instance.get();
}

DefinitionService::DefinitionService(SemanticIndex* semanticIndex)
    : index(semanticIndex ? semanticIndex : SemanticIndex::getInstance())
{
}

DefinitionService::~DefinitionService() = default;

void DefinitionService::setSemanticIndex(SemanticIndex* semanticIndex)
{
    index = semanticIndex ? semanticIndex : SemanticIndex::getInstance();
}

DefinitionResult DefinitionService::resolveDefinition(const DefinitionQuery& query) const
{
    DefinitionResult empty;
    if (query.symbolName.isEmpty()) {
        empty.missReason = SemanticDefinitionMissReason::EmptySymbolName;
        return empty;
    }

    const DefinitionQuery resolvedQuery = withResolvedMemberContext(query);
    return toDefinitionResult(
        semanticIndex()->resolveDefinition(toSemanticDefinitionQuery(resolvedQuery)));
}

QList<sym_list::SymbolInfo> DefinitionService::findDefinitions(const DefinitionQuery& query) const
{
    const DefinitionQuery resolvedQuery = withResolvedMemberContext(query);
    return semanticIndex()->findDefinitionSymbols(toSemanticDefinitionQuery(resolvedQuery));
}

bool DefinitionService::canResolveDefinition(const DefinitionQuery& query) const
{
    return resolveDefinition(query).found;
}

bool DefinitionService::isDefinition(const sym_list::SymbolInfo& symbol,
                                     const QString& searchWord) const
{
    if (symbol.symbolName != searchWord)
        return false;

    return SymbolTaxonomy::isDefinitionCandidate(
        SymbolTaxonomy::semanticMetadata(symbol));
}

SemanticIndex* DefinitionService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}

DefinitionQuery DefinitionService::withResolvedMemberContext(const DefinitionQuery& query) const
{
    if (!query.structTypeNameForMember.isEmpty()
        || query.linePrefixBeforeCursor.isEmpty()) {
        return query;
    }

    DefinitionQuery resolved = query;
    QString variableName;
    QString memberPrefix;
    if (!CompletionService::getInstance()->tryParseStructMemberContext(
            query.linePrefixBeforeCursor.trimmed(),
            variableName,
            memberPrefix)) {
        return resolved;
    }

    if (variableName.isEmpty())
        return resolved;

    resolved.structTypeNameForMember =
        semanticIndex()->getStructTypeForVariable(variableName, query.moduleName);
    if (!resolved.structTypeNameForMember.isEmpty())
        return resolved;

    const QList<sym_list::SymbolInfo> candidates = semanticIndex()->getSymbols();
    for (const sym_list::SymbolInfo& symbol : candidates) {
        const SymbolTaxonomy::SemanticMetadata metadata =
            SymbolTaxonomy::semanticMetadata(symbol);
        if (symbol.symbolName != variableName
            || !metadata.interfaceLikeOwner)
            continue;
        if (!query.moduleName.isEmpty()
            && !symbol.moduleScope.isEmpty()
            && symbol.moduleScope != query.moduleName) {
            continue;
        }
        resolved.structTypeNameForMember = SymbolTaxonomy::interfaceScopeFromOwner(symbol);
        if (!resolved.structTypeNameForMember.isEmpty())
            return resolved;
    }
    return resolved;
}
