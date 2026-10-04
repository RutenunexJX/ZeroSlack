#ifndef TEMPORARYEDITORSEARCHPROVIDER_H
#define TEMPORARYEDITORSEARCHPROVIDER_H

#include "editorsearchcandidate.h"
#include "searchservice.h"

#include <QMetaObject>
#include <QStringList>

#include <functional>
#include <memory>

class QObject;
class WorkspaceManager;
class SemanticIndexSnapshot;

QMetaObject::Connection connectTemporaryEditorFileCatalogRefresh(
    WorkspaceManager* workspaceManager,
    QObject* context,
    std::function<void()> refresh);

class TemporaryEditorSearchProvider final
{
public:
    TemporaryEditorSearchProvider();
    ~TemporaryEditorSearchProvider();
    void setWorkspaceFiles(const QStringList& filePaths,
                           const QString& workspaceRoot,
                           bool deferred = false);
    void setSemanticCatalog(
        const QList<SearchResult>& semanticCatalog);
    void setSemanticRecords(
        const QList<SemanticSymbolRecord>& records);
    void setSemanticSnapshot(std::shared_ptr<const SemanticIndexSnapshot> snapshot);
    bool semanticCatalogReady() const;
    void setCatalogChangedHandler(std::function<void()> handler);

    // This hot path only filters precomputed, case-folded in-memory catalog
    // entries. SemanticIndex traversal occurs only when the caller refreshes
    // the catalog; cached entries retain no complete semantic records.
    EditorSearchCandidates query(const QString& rawQuery) const;

private:
    struct SemanticCatalogEntry {
        EditorLocation location;
        QString title;
        QString owner;
        EditorSearchCandidateType type = EditorSearchCandidateType::Symbol;
    };

    struct IndexedCandidate {
        EditorSearchCandidate candidate;
        QString foldedTitle;
        QString foldedSearchText;
        QString identity;
    };

    QStringList cachedWorkspaceFiles;
    QList<SemanticCatalogEntry> semanticSourceCatalog;
    QList<IndexedCandidate> indexedFileCandidates;
    QList<IndexedCandidate> indexedSemanticCandidates;
    QString workspaceRootValue;
    struct AsyncCatalog;
    std::unique_ptr<AsyncCatalog> asyncCatalog;
    std::function<bool()> cancellationCheck;
    std::function<void()> catalogChanged;

    void rebuildFileCatalog();
    void rebuildSemanticCatalog();
    static SemanticCatalogEntry catalogEntry(
        const SemanticSymbolRecord& record,
        const QString& displayName,
        const RtlInsightCodeLink& codeLink,
        const SymbolStableKey& stableKey);
};

#endif // TEMPORARYEDITORSEARCHPROVIDER_H
