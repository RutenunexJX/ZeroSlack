#include "searchservice.h"

std::unique_ptr<SearchService> SearchService::instance = nullptr;

SearchService* SearchService::getInstance()
{
    if (!instance)
        instance = std::make_unique<SearchService>();
    return instance.get();
}

SearchService::SearchService(SemanticIndex* semanticIndex)
    : index(semanticIndex ? semanticIndex : SemanticIndex::getInstance())
{
}

SearchService::~SearchService() = default;

void SearchService::setSemanticIndex(SemanticIndex* semanticIndex)
{
    index = semanticIndex ? semanticIndex : SemanticIndex::getInstance();
}

QList<SearchResult> SearchService::findSymbols(const SearchQuery& query) const
{
    SemanticSymbolSearchQuery indexQuery;
    indexQuery.text = query.text;
    indexQuery.fileName = query.fileName;
    indexQuery.types = query.types;
    indexQuery.intent = query.intent;
    indexQuery.caseSensitive = query.caseSensitive;
    indexQuery.exactMatch = query.exactMatch;
    indexQuery.maxResults = query.maxResults;

    QList<SearchResult> result;
    const QList<SemanticSymbolSearchResult> indexResults =
        semanticIndex()->searchSymbols(indexQuery);
    result.reserve(indexResults.size());
    for (const SemanticSymbolSearchResult& indexResult : indexResults) {
        SearchResult item;
        item.symbol = indexResult.symbol;
        item.symbolRecord = indexResult.symbolRecord;
        item.symbolStableKey = item.symbolRecord.stableKey;
        item.score = indexResult.score;
        result.append(item);
    }
    return result;
}

bool SearchService::hasMatches(const SearchQuery& query) const
{
    return !findSymbols(query).isEmpty();
}

SemanticIndex* SearchService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}
