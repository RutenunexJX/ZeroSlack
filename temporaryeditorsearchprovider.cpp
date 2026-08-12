#include "temporaryeditorsearchprovider.h"

#include "symboltaxonomy.h"
#include "workspacemanager.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <algorithm>
#include <utility>

namespace {
int fuzzyScoreFolded(const QString& text, const QString& query)
{
    if (query.isEmpty())
        return -1;
    if (text == query)
        return 10000;
    if (text.startsWith(query))
        return 8000 - qMin(
            1000,
            static_cast<int>(text.size() - query.size()));
    const qsizetype containsAt = text.indexOf(query);
    if (containsAt >= 0)
        return 6500 - qMin(1000, static_cast<int>(containsAt));

    qsizetype queryIndex = 0;
    int gapPenalty = 0;
    qsizetype previousMatch = -1;
    for (qsizetype index = 0;
         index < text.size() && queryIndex < query.size();
         ++index) {
        if (text.at(index) != query.at(queryIndex))
            continue;
        if (previousMatch >= 0)
            gapPenalty += index - previousMatch - 1;
        previousMatch = index;
        ++queryIndex;
    }
    if (queryIndex != query.size())
        return -1;
    return 4000 - qMin(2000, gapPenalty * 10);
}

QString normalizedRelativePath(const QString& filePath,
                               const QString& workspaceRoot)
{
    const QString cleanFile = QDir::cleanPath(filePath);
    if (workspaceRoot.trimmed().isEmpty())
        return QDir::fromNativeSeparators(cleanFile);
    const QString relative = QDir(workspaceRoot).relativeFilePath(cleanFile);
    return QDir::fromNativeSeparators(QDir::cleanPath(relative));
}

EditorSearchCandidateType typeFor(const SearchResult& result)
{
    const SymbolTaxonomy::SemanticMetadata metadata =
        semanticMetadataForSymbolRecord(result.symbolRecord);
    if (SymbolTaxonomy::isModuleDeclaration(metadata))
        return EditorSearchCandidateType::Module;
    if (SymbolTaxonomy::isPackageDeclaration(metadata))
        return EditorSearchCandidateType::Package;
    return EditorSearchCandidateType::Symbol;
}

QString candidateIdentity(const EditorSearchCandidate& candidate)
{
    return QStringLiteral("%1|%2|%3|%4|%5")
        .arg(static_cast<int>(candidate.type))
        .arg(EditorFileIdentity::lookupKey(candidate.location.filePath))
        .arg(candidate.location.line)
        .arg(candidate.location.column)
        .arg(candidate.location.sourceLinkId);
}

bool candidateMetadataLess(const EditorSearchCandidate& lhs,
                           const EditorSearchCandidate& rhs)
{
    const int titleOrder = lhs.title.compare(
        rhs.title, Qt::CaseInsensitive);
    if (titleOrder != 0)
        return titleOrder < 0;
    if (lhs.title != rhs.title)
        return lhs.title < rhs.title;
    if (lhs.type != rhs.type) {
        return static_cast<int>(lhs.type)
            < static_cast<int>(rhs.type);
    }
    const int disambiguationOrder = lhs.disambiguation.compare(
        rhs.disambiguation, Qt::CaseInsensitive);
    if (disambiguationOrder != 0)
        return disambiguationOrder < 0;
    if (lhs.disambiguation != rhs.disambiguation)
        return lhs.disambiguation < rhs.disambiguation;
    const int pathOrder = lhs.location.filePath.compare(
        rhs.location.filePath, Qt::CaseInsensitive);
    if (pathOrder != 0)
        return pathOrder < 0;
    if (lhs.location.filePath != rhs.location.filePath)
        return lhs.location.filePath < rhs.location.filePath;
    if (lhs.location.line != rhs.location.line)
        return lhs.location.line < rhs.location.line;
    if (lhs.location.column != rhs.location.column)
        return lhs.location.column < rhs.location.column;
    return lhs.location.sourceLinkId < rhs.location.sourceLinkId;
}
}

QMetaObject::Connection connectTemporaryEditorFileCatalogRefresh(
    WorkspaceManager* workspaceManager,
    QObject* context,
    std::function<void()> refresh)
{
    if (!workspaceManager || !context || !refresh)
        return {};
    return QObject::connect(
        workspaceManager,
        &WorkspaceManager::filesScanned,
        context,
        [refresh = std::move(refresh)](const QStringList&) {
            refresh();
        });
}

void TemporaryEditorSearchProvider::setWorkspaceFiles(
    const QStringList& filePaths,
    const QString& workspaceRoot)
{
    workspaceRootValue = workspaceRoot.trimmed().isEmpty()
        ? QString()
        : QDir::cleanPath(workspaceRoot);
    cachedWorkspaceFiles.clear();
    cachedWorkspaceFiles.reserve(filePaths.size());
    QSet<QString> seen;
    for (const QString& filePath : filePaths) {
        const QString trimmed = filePath.trimmed();
        if (trimmed.isEmpty())
            continue;
        const QString clean = QDir::cleanPath(trimmed);
        const QString identity = EditorFileIdentity::lookupKey(clean);
        if (seen.contains(identity))
            continue;
        seen.insert(identity);
        cachedWorkspaceFiles.append(clean);
    }
    std::sort(
        cachedWorkspaceFiles.begin(),
        cachedWorkspaceFiles.end(),
        [](const QString& lhs, const QString& rhs) {
            return lhs.compare(rhs, Qt::CaseInsensitive) < 0;
        });
    rebuildFileCatalog();
    rebuildSemanticCatalog();
}

void TemporaryEditorSearchProvider::setSemanticCatalog(
    const QList<SearchResult>& semanticCatalog)
{
    semanticSourceCatalog = semanticCatalog;
    rebuildSemanticCatalog();
}

EditorSearchCandidates TemporaryEditorSearchProvider::query(
    const QString& rawQuery) const
{
    const QString query = rawQuery.trimmed().toCaseFolded();
    if (query.isEmpty())
        return {};

    EditorSearchCandidates candidates;
    candidates.reserve(
        indexedFileCandidates.size()
        + indexedSemanticCandidates.size());
    const auto appendMatches = [&candidates, &query](
                                   const QList<IndexedCandidate>& catalog) {
        for (const IndexedCandidate& indexed : catalog) {
            const int score = qMax(
                fuzzyScoreFolded(indexed.foldedTitle, query),
                fuzzyScoreFolded(indexed.foldedSearchText, query) - 200);
            if (score < 0)
                continue;
            EditorSearchCandidate candidate = indexed.candidate;
            candidate.score = score;
            candidates.append(candidate);
        }
    };
    appendMatches(indexedFileCandidates);
    appendMatches(indexedSemanticCandidates);

    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const EditorSearchCandidate& lhs,
           const EditorSearchCandidate& rhs) {
            if (lhs.score != rhs.score)
                return lhs.score > rhs.score;
            return candidateMetadataLess(lhs, rhs);
        });
    return candidates;
}

void TemporaryEditorSearchProvider::rebuildFileCatalog()
{
    indexedFileCandidates.clear();
    indexedFileCandidates.reserve(cachedWorkspaceFiles.size());
    for (const QString& filePath : cachedWorkspaceFiles) {
        const QString title = QFileInfo(filePath).fileName();
        const QString relative = normalizedRelativePath(
            filePath, workspaceRootValue);
        EditorSearchCandidate candidate;
        candidate.location.filePath = filePath;
        candidate.title = title;
        candidate.type = EditorSearchCandidateType::File;
        candidate.disambiguation = relative;
        IndexedCandidate indexed;
        indexed.candidate = candidate;
        indexed.foldedTitle = title.toCaseFolded();
        indexed.foldedSearchText =
            QStringLiteral("%1 %2 file")
                .arg(title, relative)
                .toCaseFolded();
        indexedFileCandidates.append(indexed);
    }
}

void TemporaryEditorSearchProvider::rebuildSemanticCatalog()
{
    QList<IndexedCandidate> rebuilt;
    rebuilt.reserve(semanticSourceCatalog.size());
    for (const SearchResult& match : semanticSourceCatalog) {
        if (match.codeLink.fileName.trimmed().isEmpty())
            continue;
        const QString title = !match.symbolDisplayName.trimmed().isEmpty()
            ? match.symbolDisplayName.trimmed()
            : match.symbolRecord.name.trimmed();
        if (title.isEmpty())
            continue;
        const QString relative = normalizedRelativePath(
            match.codeLink.fileName, workspaceRootValue);
        const QString owner = match.symbolRecord.owner.name.trimmed();
        const EditorSearchCandidateType type = typeFor(match);
        const QString typeLabel = editorSearchCandidateTypeLabel(type);
        const QString disambiguation = owner.isEmpty()
            ? QStringLiteral("%1 in %2").arg(typeLabel, relative)
            : QStringLiteral("%1 in %2 (%3)")
                  .arg(typeLabel, relative, owner);

        EditorSearchCandidate candidate;
        candidate.location = editorLocationFromActionParameters(
            QVariantMap{
                {QStringLiteral("path"), match.codeLink.fileName},
                {QStringLiteral("line"), qMax(1, match.codeLink.line)},
                {QStringLiteral("column"), qMax(1, match.codeLink.column)},
                {QStringLiteral("symbolId"), title},
                {QStringLiteral("sourceLinkId"),
                 match.symbolStableKey.toString()},
            });
        candidate.title = title;
        candidate.type = type;
        candidate.disambiguation = disambiguation;
        IndexedCandidate indexed;
        indexed.candidate = candidate;
        indexed.foldedTitle = title.toCaseFolded();
        indexed.foldedSearchText =
            QStringLiteral("%1 %2 %3 %4")
                .arg(title, typeLabel, relative, owner)
                .toCaseFolded();
        rebuilt.append(indexed);
    }

    std::sort(
        rebuilt.begin(),
        rebuilt.end(),
        [](const IndexedCandidate& lhs,
           const IndexedCandidate& rhs) {
            return candidateMetadataLess(
                lhs.candidate, rhs.candidate);
        });

    indexedSemanticCandidates.clear();
    indexedSemanticCandidates.reserve(rebuilt.size());
    QSet<QString> seen;
    for (const IndexedCandidate& indexed : rebuilt) {
        const QString identity = candidateIdentity(indexed.candidate);
        if (seen.contains(identity))
            continue;
        seen.insert(identity);
        indexedSemanticCandidates.append(indexed);
    }
}
