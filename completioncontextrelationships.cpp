#include "completioncontextquery.h"

#include "semanticindex.h"
#include "symbolrelationshipengine.h"

QStringList CompletionContextQuery::moduleChildCompletions(
    SemanticIndex* semanticIndex,
    const QString& moduleName,
    const QString& prefix)
{
    if (!semanticIndex)
        return {};
    return semanticIndex->getRelationshipCompletionNames(
        moduleName,
        {SymbolRelationshipEngine::CONTAINS},
        true,
        prefix);
}

QStringList CompletionContextQuery::relatedSymbolCompletions(
    SemanticIndex* semanticIndex,
    const QString& symbolName,
    const QString& prefix)
{
    if (!semanticIndex)
        return {};
    return semanticIndex->getBidirectionalRelationshipCompletionNames(
        symbolName,
        {SymbolRelationshipEngine::REFERENCES},
        prefix);
}

QStringList CompletionContextQuery::symbolReferenceCompletions(
    SemanticIndex* semanticIndex,
    const QString& symbolName,
    const QString& prefix)
{
    if (!semanticIndex)
        return {};
    return semanticIndex->getRelationshipCompletionNames(
        symbolName,
        {SymbolRelationshipEngine::REFERENCES},
        false,
        prefix);
}

QStringList CompletionContextQuery::clockDomainCompletions(
    SemanticIndex* semanticIndex,
    const QString& prefix)
{
    if (!semanticIndex)
        return {};
    return semanticIndex->getSymbolsWithOutgoingRelationshipCompletionNames(
        SymbolRelationshipEngine::CLOCKS,
        prefix);
}

QStringList CompletionContextQuery::resetSignalCompletions(
    SemanticIndex* semanticIndex,
    const QString& prefix)
{
    if (!semanticIndex)
        return {};
    return semanticIndex->getSymbolsWithOutgoingRelationshipCompletionNames(
        SymbolRelationshipEngine::RESETS,
        prefix);
}
