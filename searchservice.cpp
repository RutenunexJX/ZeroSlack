#include "searchservice.h"

std::unique_ptr<SearchService> SearchService::instance = nullptr;

namespace {
SymbolTaxonomy::SemanticMetadata metadataForRecord(
    const SemanticSymbolRecord& record,
    const sym_list::SymbolInfo& fallback)
{
    SymbolTaxonomy::SemanticMetadata metadata =
        SymbolTaxonomy::semanticMetadata(fallback);
    if (!record.isValid())
        return metadata;

    metadata.declarationKind = record.declarationKind;
    metadata.usageRole = record.usageRole;
    metadata.visibility = record.visibility;
    metadata.sourceRole = record.sourceRole;
    metadata.rawCollectorKind = record.rawCollectorKind;
    metadata.interfaceLikeOwner = record.owner.interfaceLike;
    return metadata;
}

QString symbolDisplayNameForRecord(
    const SemanticSymbolRecord& record,
    const sym_list::SymbolInfo& fallback)
{
    if (!record.name.isEmpty())
        return record.name;
    if (!fallback.symbolName.isEmpty())
        return fallback.symbolName;
    return QStringLiteral("<unknown>");
}

RtlInsightCodeLink codeLinkForRecord(
    const SemanticSymbolRecord& record,
    const sym_list::SymbolInfo& fallback)
{
    if (record.isValid()) {
        const QString fileName = fallback.fileName.isEmpty()
            ? record.location.fileName
            : fallback.fileName;
        return RtlInsightLink::fromFileLine(fileName,
                                            record.location.startLine,
                                            record.location.startColumn);
    }
    return RtlInsightLink::fromSymbol(fallback);
}
}

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
        item.symbolDisplayName =
            symbolDisplayNameForRecord(item.symbolRecord, item.symbol);
        const SymbolTaxonomy::SemanticMetadata metadata =
            metadataForRecord(item.symbolRecord, item.symbol);
        item.symbolTypeDisplayName = SymbolTaxonomy::symbolTypeLabel(metadata);
        item.sourceRoleDisplayName =
            SymbolTaxonomy::sourceRoleDisplayName(metadata.sourceRole);
        item.codeLink = codeLinkForRecord(item.symbolRecord, item.symbol);
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
