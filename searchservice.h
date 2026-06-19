#ifndef SEARCHSERVICE_H
#define SEARCHSERVICE_H

#include "rtlinsightlink.h"
#include "semanticindex.h"

#include <QList>
#include <QString>
#include <memory>

struct SearchQuery {
    QString text;
    QString fileName;
    QList<SymbolTaxonomy::DeclarationKind> declarationKinds;
    SymbolTaxonomy::SymbolSearchIntent intent = SymbolTaxonomy::SymbolSearchIntent::Any;
    bool caseSensitive = false;
    bool exactMatch = false;
    int maxResults = -1;
};

struct SearchResult {
    SemanticSymbolRecord symbolRecord;
    SymbolStableKey symbolStableKey;
    QString symbolDisplayName;
    QString symbolTypeDisplayName;
    QString sourceRoleDisplayName;
    RtlInsightCodeLink codeLink;
    int score = 0;
};

class SearchService
{
public:
    static SearchService* getInstance();

    explicit SearchService(SemanticIndex* semanticIndex = nullptr);
    ~SearchService();

    void setSemanticIndex(SemanticIndex* semanticIndex);

    QList<SearchResult> findSymbols(const SearchQuery& query) const;
    bool hasMatches(const SearchQuery& query) const;

private:
    SemanticIndex* index = nullptr;
    static std::unique_ptr<SearchService> instance;

    SemanticIndex* semanticIndex() const;
};

#endif // SEARCHSERVICE_H
