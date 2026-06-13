#include "completionservice.h"

#include "completionsymbolquery.h"

#include <QVector>

std::unique_ptr<CompletionService> CompletionService::instance = nullptr;

CompletionService* CompletionService::getInstance()
{
    if (!instance)
        instance = std::make_unique<CompletionService>();
    return instance.get();
}

CompletionService::CompletionService(SemanticIndex* semanticIndex)
    : index(semanticIndex ? semanticIndex : SemanticIndex::getInstance())
{
}

CompletionService::~CompletionService() = default;

void CompletionService::setSemanticIndex(SemanticIndex* semanticIndex)
{
    index = semanticIndex ? semanticIndex : SemanticIndex::getInstance();
}

QStringList CompletionService::findCompletions(const CompletionQuery& query) const
{
    return findCompletionResult(query).names;
}

CompletionResult CompletionService::findCompletionResult(
    const CompletionQuery& query) const
{
    CompletionResult result;
    result.symbols = findCompletionSymbols(query);
    result.names = CompletionSymbolQuery::namesFromSymbols(result.symbols);
    return result;
}

QList<sym_list::SymbolInfo> CompletionService::findCompletionSymbols(
    const CompletionQuery& query) const
{
    if (!query.structTypeNameForMember.isEmpty()) {
        return CompletionSymbolQuery::structMemberSymbols(
            semanticIndex(), query.structTypeNameForMember, query.prefix);
    }

    if (query.prefix.isEmpty())
        return {};

    if (!query.moduleName.isEmpty())
        return semanticIndex()->getModuleCompletionSymbols(
            query.moduleName, query.prefix);

    return semanticIndex()->getGlobalCompletionSymbols(query.prefix);
}

QVector<QPair<QString, int>> CompletionService::findScoredAllSymbolCompletions(
    const QString& prefix,
    int maxResults) const
{
    return CompletionSymbolQuery::scoredNames(
        semanticIndex()->getCompletionSymbolNames(), prefix, maxResults);
}

QStringList CompletionService::findAllSymbolCompletions(
    const QString& prefix,
    int maxResults) const
{
    const QVector<QPair<QString, int>> scored =
        findScoredAllSymbolCompletions(prefix, maxResults);
    return CompletionSymbolQuery::namesFromScored(scored, maxResults);
}

QVector<QPair<sym_list::SymbolInfo, int>>
CompletionService::findScoredSymbolCompletionsByType(
    sym_list::sym_type_e symbolType,
    const QString& prefix,
    int maxResults) const
{
    return CompletionSymbolQuery::scoredTypedSymbols(
        semanticIndex(), symbolType, prefix, maxResults);
}

QStringList CompletionService::findSymbolCompletionsByType(
    sym_list::sym_type_e symbolType,
    const QString& prefix,
    int maxResults) const
{
    const QVector<QPair<sym_list::SymbolInfo, int>> scored =
        findScoredSymbolCompletionsByType(symbolType, prefix, maxResults);
    return CompletionSymbolQuery::symbolNamesFromScored(scored, maxResults);
}

SemanticIndex* CompletionService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}
