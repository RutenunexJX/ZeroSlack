#include "searchservice.h"

std::unique_ptr<SearchService> SearchService::instance = nullptr;

namespace {
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

QString symbolDisplayNameForRecord(const SemanticSymbolRecord& record)
{
    if (!record.name.isEmpty())
        return record.name;
    return QStringLiteral("<unknown>");
}

RtlInsightCodeLink codeLinkForRecord(const SemanticSymbolRecord& record)
{
    return RtlInsightLink::fromFileLine(record.location.fileName,
                                        record.location.startLine,
                                        record.location.startColumn);
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
        item.symbolRecord = indexResult.symbolRecord;
        item.symbolStableKey = item.symbolRecord.stableKey.isValid()
            ? item.symbolRecord.stableKey
            : indexResult.symbolStableKey;
        item.symbolDisplayName =
            symbolDisplayNameForRecord(item.symbolRecord);
        const SymbolTaxonomy::SemanticMetadata metadata =
            metadataForRecord(item.symbolRecord);
        item.symbolTypeDisplayName = SymbolTaxonomy::symbolTypeLabel(metadata);
        item.sourceRoleDisplayName =
            SymbolTaxonomy::sourceRoleDisplayName(metadata.sourceRole);
        item.codeLink = codeLinkForRecord(item.symbolRecord);
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
