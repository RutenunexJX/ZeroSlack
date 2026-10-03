#ifndef TEMPORARYEDITORSEARCHPROVIDER_H
#define TEMPORARYEDITORSEARCHPROVIDER_H

#include "editorsearchcandidate.h"
#include "searchservice.h"

#include <QMetaObject>
#include <QStringList>

#include <functional>

class QObject;
class WorkspaceManager;

QMetaObject::Connection connectTemporaryEditorFileCatalogRefresh(
    WorkspaceManager* workspaceManager,
    QObject* context,
    std::function<void()> refresh);

class TemporaryEditorSearchProvider final
{
public:
    void setWorkspaceFiles(const QStringList& filePaths,
                           const QString& workspaceRoot);
    void setSemanticCatalog(
        const QList<SearchResult>& semanticCatalog);
    void setSemanticRecords(
        const QList<SemanticSymbolRecord>& records);

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
    };

    QStringList cachedWorkspaceFiles;
    QList<SemanticCatalogEntry> semanticSourceCatalog;
    QList<IndexedCandidate> indexedFileCandidates;
    QList<IndexedCandidate> indexedSemanticCandidates;
    QString workspaceRootValue;

    void rebuildFileCatalog();
    void rebuildSemanticCatalog();
    static SemanticCatalogEntry catalogEntry(
        const SemanticSymbolRecord& record,
        const QString& displayName,
        const RtlInsightCodeLink& codeLink,
        const SymbolStableKey& stableKey);
};

#endif // TEMPORARYEDITORSEARCHPROVIDER_H
