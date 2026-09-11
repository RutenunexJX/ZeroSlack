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

    // This hot path only filters precomputed, case-folded in-memory catalog
    // entries. SemanticIndex traversal occurs only when the caller refreshes
    // setSemanticCatalog() from a SearchService snapshot catalog.
    EditorSearchCandidates query(const QString& rawQuery) const;

private:
    struct IndexedCandidate {
        EditorSearchCandidate candidate;
        QString foldedTitle;
        QString foldedSearchText;
    };

    QStringList cachedWorkspaceFiles;
    QList<SearchResult> semanticSourceCatalog;
    QList<IndexedCandidate> indexedFileCandidates;
    QList<IndexedCandidate> indexedSemanticCandidates;
    QString workspaceRootValue;

    void rebuildFileCatalog();
    void rebuildSemanticCatalog();
};

#endif // TEMPORARYEDITORSEARCHPROVIDER_H
