#ifndef SEARCHSERVICE_H
#define SEARCHSERVICE_H

#include "rtlinsightlink.h"
#include "semanticindex.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QtGlobal>
#include <rtledit/edit_plan.h>
#include <memory>

class TSDocument;

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

enum class ScopedSearchScope {
    SyntaxBlock,
    Module,
    File,
    Workspace
};

enum class ScopedSearchStatus {
    Ready,
    EmptyQuery,
    ActiveDocumentNotFound,
    SyntaxSnapshotRequired,
    NoEnclosingScope
};

// Immutable input captured by the caller at one document revision. The
// Tree-sitter pointer must describe exactly `text`; syntax-scoped searches
// reject a mismatched snapshot instead of falling back to saved Slang data.
struct SearchDocumentSnapshot {
    QString fileName;
    QString text;
    const TSDocument* syntax = nullptr;
    quint64 revision = 0;
};

enum class SearchMatchIdentityKind {
    TextRange,
    SyntaxIdentifier
};

// One physical source occurrence. Text and semantic search results use this
// identity as their merge key instead of comparing presentation strings.
struct SearchMatchIdentity {
    QString fileIdentity;
    SearchMatchIdentityKind kind = SearchMatchIdentityKind::TextRange;
    int anchorStartChar = -1;
    int anchorEndChar = -1;

    bool isValid() const;
    QString toString() const;
    bool operator==(const SearchMatchIdentity& other) const;
};

// Context is copied into each result when the search runs. Rendering a result
// later therefore never reads shifted lines from a newer document revision.
struct SearchContextSnippet {
    QString text;
    int sourceStartChar = -1;
    int sourceEndChar = -1;
    int firstLine = 0; // 1-based
    int matchStartInSnippet = -1;
    int matchLength = 0;
    quint64 documentRevision = 0;

    bool isValid() const;
};

struct ScopedSearchQuery {
    QString text;
    ScopedSearchScope scope = ScopedSearchScope::Workspace;
    QString activeFileName;
    int cursorChar = -1;
    bool caseSensitive = false;
    bool wholeWord = false;
    bool includeSemantic = true;
    int contextLineCount = 2;
    int maxResults = -1;
};

struct ScopedSearchResult {
    SearchMatchIdentity identity;
    QString fileName;
    int matchStartChar = -1;
    int matchEndChar = -1;
    int line = 0;   // 1-based
    int column = 0; // 1-based UTF-16 column
    QString matchedText;
    SearchContextSnippet context;
    bool fromTextSearch = false;
    bool fromSemanticSearch = false;
    bool hasSemanticRecord = false;
    SemanticSymbolRecord semanticRecord;
    int semanticScore = 0;
};

struct ScopedSearchResponse {
    ScopedSearchStatus status = ScopedSearchStatus::Ready;
    QString activeFileName;
    int scopeStartChar = -1;
    int scopeEndChar = -1;
    QList<ScopedSearchResult> results;

    bool ready() const { return status == ScopedSearchStatus::Ready; }
};

enum class ReplacePreviewStatus {
    Ready,
    NoSelectedMatches,
    StaleSearchResult,
    OverlappingMatches,
    InvalidTransactionPlan
};

struct ReplacePreviewRequest {
    QString replacementText;
    bool filesSelectedByDefault = true;
    bool matchesSelectedByDefault = true;
    QHash<QString, bool> fileSelectionByIdentity;
    QHash<QString, bool> matchSelectionByIdentity;
};

struct ReplaceMatchPreview {
    SearchMatchIdentity identity;
    QString matchedText;
    SearchContextSnippet context;
    bool selected = false;
};

struct ReplaceFilePreview {
    QString fileName;
    QString fileIdentity;
    quint64 documentRevision = 0;
    bool selected = false;
    QList<ReplaceMatchPreview> matches;
};

// This is a dry preview/checklist model. `transactionPlan` is suitable for the
// shared analyze -> preview -> atomic apply coordinator, but this service never
// applies it.
struct ReplacePreviewPlan {
    ReplacePreviewStatus status = ReplacePreviewStatus::NoSelectedMatches;
    QString failureReason;
    QList<ReplaceFilePreview> files;
    rtledit::WorkspaceEditPlan transactionPlan;

    bool ready() const { return status == ReplacePreviewStatus::Ready; }
};

class SearchService
{
public:
    static SearchService* getInstance();

    explicit SearchService(SemanticIndex* semanticIndex = nullptr);
    ~SearchService();

    void setSemanticIndex(SemanticIndex* semanticIndex);

    // Materializes the currently published semantic snapshot without query
    // truncation. Callers cache this catalog at publication boundaries; this
    // is not intended for per-keystroke search.
    QList<SearchResult> symbolCatalog() const;
    QList<SearchResult> findSymbols(const SearchQuery& query) const;
    bool hasMatches(const SearchQuery& query) const;
    ScopedSearchResponse search(
        const ScopedSearchQuery& query,
        const QList<SearchDocumentSnapshot>& documents) const;
    ReplacePreviewPlan planReplace(
        const QList<ScopedSearchResult>& results,
        const QList<SearchDocumentSnapshot>& currentDocuments,
        const ReplacePreviewRequest& request) const;

private:
    SemanticIndex* index = nullptr;
    static std::unique_ptr<SearchService> instance;

    SemanticIndex* semanticIndex() const;
};

#endif // SEARCHSERVICE_H
