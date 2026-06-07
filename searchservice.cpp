#include "searchservice.h"

#include <algorithm>

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
    QList<SearchResult> result;
    const QList<sym_list::SymbolInfo> symbols = semanticIndex()->getSymbols(query.fileName);
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (!typeMatches(symbol.symbolType, query.types))
            continue;

        const int score = matchScore(symbol.symbolName, query);
        if (score <= 0)
            continue;

        SearchResult item;
        item.symbol = symbol;
        item.score = score;
        result.append(item);
    }

    std::stable_sort(result.begin(), result.end(), [](const SearchResult& a,
                                                      const SearchResult& b) {
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

bool SearchService::hasMatches(const SearchQuery& query) const
{
    return !findSymbols(query).isEmpty();
}

SemanticIndex* SearchService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}

bool SearchService::typeMatches(sym_list::sym_type_e type,
                                const QList<sym_list::sym_type_e>& types) const
{
    return types.isEmpty() || types.contains(type);
}

int SearchService::matchScore(const QString& symbolName, const SearchQuery& query) const
{
    if (symbolName.isEmpty())
        return 0;
    if (query.text.isEmpty())
        return 1;

    const Qt::CaseSensitivity sensitivity =
        query.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;

    if (QString::compare(symbolName, query.text, sensitivity) == 0)
        return 100;
    if (query.exactMatch)
        return 0;
    if (symbolName.startsWith(query.text, sensitivity))
        return 75;
    if (symbolName.contains(query.text, sensitivity))
        return 50;
    return 0;
}
