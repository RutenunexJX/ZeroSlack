#include "temporaryeditorsearchprovider.h"

#include "symboltaxonomy.h"
#include "workspacemanager.h"
#include "semanticindexsnapshot.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QPointer>
#include <QFutureWatcher>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrent>
#include <atomic>
#include <optional>

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

EditorSearchCandidateType typeFor(const SemanticSymbolRecord& record)
{
    const SymbolTaxonomy::SemanticMetadata metadata =
        semanticMetadataForSymbolRecord(record);
    if (SymbolTaxonomy::isModuleDeclaration(metadata))
        return EditorSearchCandidateType::Module;
    if (SymbolTaxonomy::isPackageDeclaration(metadata))
        return EditorSearchCandidateType::Package;
    return EditorSearchCandidateType::Symbol;
}

QString candidateIdentity(const EditorSearchCandidate& candidate,
                          const QString& fileIdentity)
{
    return QStringLiteral("%1|%2|%3|%4|%5")
        .arg(static_cast<int>(candidate.type))
        .arg(fileIdentity)
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

// A single owned worker and one replaceable pending input. Publications share
// unchanged per-file projections, rather than materializing all semantic records
// in MainWindow's completion handler. The worker also releases retired shards.
struct TemporaryEditorSearchProvider::AsyncCatalog {
    struct File {
        QList<SemanticSymbolRecord> source;
        QList<IndexedCandidate> candidates;
        QHash<QString, QString> fileIdentities;
    };
    struct Result {
        QString root;
        QStringList sourceFiles;
        QList<IndexedCandidate> fileCandidates;
        QHash<QString, std::shared_ptr<const File>> files;
        QHash<QString, const IndexedCandidate*> uniqueCandidates;
    };
    struct Request {
        std::shared_ptr<const SemanticIndexSnapshot> snapshot;
        QString root;
        std::uint64_t generation = 0;
        QStringList workspaceFiles;
    };
    TemporaryEditorSearchProvider* owner;
    QObject context;
    QThreadPool pool;
    QFutureWatcher<std::shared_ptr<const Result>>* watcher = nullptr;
    std::optional<Request> pending;
    std::shared_ptr<const Result> current;
    std::weak_ptr<const SemanticIndexSnapshot> latestSnapshot;
    QStringList requestedFiles;
    std::shared_ptr<std::atomic<bool>> cancel;
    std::uint64_t generation = 0;
    bool ready = true;

    explicit AsyncCatalog(TemporaryEditorSearchProvider* provider) : owner(provider) {
        pool.setMaxThreadCount(1);
        pool.setThreadPriority(QThread::LowPriority);
        pool.setObjectName(QStringLiteral("ZeroSlackSearchCatalog"));
    }
    ~AsyncCatalog() {
        if (cancel) cancel->store(true);
        if (watcher) QObject::disconnect(watcher, nullptr, &context, nullptr);
        pool.start([old = std::move(current), request = std::move(pending)] {});
        pool.waitForDone();
    }
    void reset() {
        ++generation;
        if (cancel) cancel->store(true);
        auto old = std::exchange(current, {});
        auto request = std::exchange(pending, {});
        if (old || request)
            pool.start([old = std::move(old), request = std::move(request)] {});
        latestSnapshot.reset();
        ready = true;
    }
    void launch() {
        if (watcher || !pending) return;
        const Request request = std::move(*pending);
        pending.reset();
        const auto previous = current;
        const auto cancelled = std::make_shared<std::atomic<bool>>(false);
        cancel = cancelled;
        auto* observed = new QFutureWatcher<std::shared_ptr<const Result>>(&context);
        watcher = observed;
        QObject::connect(observed, &QFutureWatcher<std::shared_ptr<const Result>>::finished,
            &context, [this, observed, requested = request.generation] {
                auto result = observed->future().takeResult();
                watcher = nullptr;
                observed->deleteLater();
                if (result && requested == generation) {
                    auto old = std::exchange(current, std::move(result));
                    if (old) pool.start([old = std::move(old)] {});
                    ready = true;
                    const QPointer<QObject> alive(&context);
                    if (owner->catalogChanged) owner->catalogChanged();
                    if (!alive) return;
                } else if (result) {
                    pool.start([old = std::move(result)] {});
                }
                launch();
            });
        observed->setFuture(QtConcurrent::run(&pool, [request, previous, cancelled]() -> std::shared_ptr<const Result> {
            auto result = std::make_shared<Result>();
            result->root = request.root;
            result->sourceFiles = request.workspaceFiles;
            if (previous && previous->root == request.root
                && previous->sourceFiles == request.workspaceFiles) {
                result->fileCandidates = previous->fileCandidates;
            } else {
                TemporaryEditorSearchProvider builder;
                builder.cancellationCheck = [cancelled] { return cancelled->load(); };
                builder.setWorkspaceFiles(request.workspaceFiles, request.root);
                result->fileCandidates = std::move(builder.indexedFileCandidates);
            }
            if (cancelled->load()) return {};
            if (!request.snapshot) return result;
            QHash<QString, QString> currentFileIdentities;
            auto identityFor = [&currentFileIdentities](const QString& path) {
                auto found = currentFileIdentities.constFind(path);
                if (found != currentFileIdentities.cend()) return found.value();
                const auto identity = EditorFileIdentity::lookupKey(path);
                currentFileIdentities.insert(path, identity);
                return identity;
            };
            for (const auto& file : request.snapshot->symbolFiles()) {
                if (cancelled->load()) return {};
                const auto records = request.snapshot->getSymbolRecords(file);
                const auto reusable = previous && previous->root == request.root
                    ? previous->files.value(file) : std::shared_ptr<const File>{};
                bool sameAliases = bool(reusable);
                if (reusable)
                    for (auto it = reusable->fileIdentities.cbegin(); it != reusable->fileIdentities.cend(); ++it) {
                        if (cancelled->load()) return {};
                        if (identityFor(it.key()) != it.value()) { sameAliases = false; break; }
                    }
                if (reusable && sameAliases && reusable->source.constData() == records.constData()) {
                    result->files.insert(file, reusable);
                    continue;
                }
                TemporaryEditorSearchProvider builder;
                builder.workspaceRootValue = request.root;
                builder.cancellationCheck = [cancelled] { return cancelled->load(); };
                builder.setSemanticRecords(records);
                if (cancelled->load()) return {};
                auto prepared = std::make_shared<File>();
                prepared->source = records;
                prepared->candidates = std::move(builder.indexedSemanticCandidates);
                for (const auto& entry : prepared->candidates)
                    prepared->fileIdentities.insert(entry.candidate.location.filePath,
                        identityFor(entry.candidate.location.filePath));
                result->files.insert(file, std::move(prepared));
            }
            // Resolve aliases before fuzzy matching, exactly as the existing
            // catalog does. Pointers refer to immutable, owned file shards.
            for (const auto& file : result->files)
                for (const auto& entry : file->candidates) {
                    if (cancelled->load()) return {};
                    const auto previous = result->uniqueCandidates.value(entry.identity);
                    if (!previous || candidateMetadataLess(entry.candidate, previous->candidate))
                        result->uniqueCandidates.insert(entry.identity, &entry);
                }
            return result;
        }));
    }
};

TemporaryEditorSearchProvider::TemporaryEditorSearchProvider() = default;
TemporaryEditorSearchProvider::~TemporaryEditorSearchProvider() = default;

void TemporaryEditorSearchProvider::setSemanticSnapshot(
    std::shared_ptr<const SemanticIndexSnapshot> snapshot)
{
    if (!asyncCatalog) {
        asyncCatalog = std::make_unique<AsyncCatalog>(this);
        asyncCatalog->requestedFiles = cachedWorkspaceFiles;
    }
    asyncCatalog->latestSnapshot = snapshot;
    if (asyncCatalog->cancel) asyncCatalog->cancel->store(true);
    asyncCatalog->pending = AsyncCatalog::Request{
        std::move(snapshot), workspaceRootValue, ++asyncCatalog->generation,
        asyncCatalog->requestedFiles};
    asyncCatalog->ready = false;
    asyncCatalog->launch();
    if (catalogChanged) catalogChanged();
}

bool TemporaryEditorSearchProvider::semanticCatalogReady() const
{
    return !asyncCatalog || asyncCatalog->ready;
}

void TemporaryEditorSearchProvider::setCatalogChangedHandler(std::function<void()> handler)
{
    catalogChanged = std::move(handler);
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
    const QString& workspaceRoot,
    bool deferred)
{
    const QString previousRoot = workspaceRootValue;
    workspaceRootValue = workspaceRoot.trimmed().isEmpty()
        ? QString()
        : QDir::cleanPath(workspaceRoot);
    if (deferred) {
        if (!asyncCatalog) asyncCatalog = std::make_unique<AsyncCatalog>(this);
        asyncCatalog->requestedFiles = filePaths;
        indexedFileCandidates.clear();
        setSemanticSnapshot(previousRoot == workspaceRootValue
            ? asyncCatalog->latestSnapshot.lock() : nullptr);
        return;
    }
    cachedWorkspaceFiles.clear();
    cachedWorkspaceFiles.reserve(filePaths.size());
    QSet<QString> seen;
    for (const QString& filePath : filePaths) {
        if (cancellationCheck && cancellationCheck()) return;
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
    if (asyncCatalog && (previousRoot != workspaceRootValue
                        || asyncCatalog->requestedFiles != cachedWorkspaceFiles)) {
        asyncCatalog->requestedFiles = cachedWorkspaceFiles;
        if (auto snapshot = asyncCatalog->latestSnapshot.lock())
            setSemanticSnapshot(std::move(snapshot));
    }
}

void TemporaryEditorSearchProvider::setSemanticCatalog(
    const QList<SearchResult>& semanticCatalog)
{
    if (asyncCatalog) asyncCatalog->reset();
    semanticSourceCatalog.clear();
    semanticSourceCatalog.reserve(semanticCatalog.size());
    for (const SearchResult& match : semanticCatalog) {
        semanticSourceCatalog.append(catalogEntry(
            match.symbolRecord, match.symbolDisplayName,
            match.codeLink, match.symbolStableKey));
    }
    rebuildSemanticCatalog();
}

void TemporaryEditorSearchProvider::setSemanticRecords(
    const QList<SemanticSymbolRecord>& records)
{
    if (asyncCatalog) asyncCatalog->reset();
    // Search needs only presentation and navigation fields. Retaining complete
    // records keeps large semantic payloads alive until a GUI-thread clear.
    semanticSourceCatalog.clear();
    semanticSourceCatalog.reserve(records.size());
    for (const SemanticSymbolRecord& record : records) {
        if (cancellationCheck && cancellationCheck()) return;
        const RtlInsightCodeLink link{
            record.location.fileName,
            record.location.startLine,
            record.location.startColumn,
            {}, {}};
        semanticSourceCatalog.append(catalogEntry(
            record,
            record.name.isEmpty() ? QStringLiteral("<unknown>") : record.name,
            link,
            record.stableKey.isValid() ? record.stableKey : SymbolStableKey{}));
    }
    rebuildSemanticCatalog();
}

TemporaryEditorSearchProvider::SemanticCatalogEntry
TemporaryEditorSearchProvider::catalogEntry(
    const SemanticSymbolRecord& record,
    const QString& displayName,
    const RtlInsightCodeLink& codeLink,
    const SymbolStableKey& stableKey)
{
    SemanticCatalogEntry entry;
    entry.title = !displayName.trimmed().isEmpty()
        ? displayName.trimmed() : record.name.trimmed();
    entry.owner = record.owner.name.trimmed();
    entry.type = typeFor(record);
    entry.location.filePath = codeLink.fileName;
    entry.location.line = qMax(1, codeLink.line);
    entry.location.column = qMax(1, codeLink.column);
    entry.location.symbolKey = entry.title;
    entry.location.sourceLinkId = stableKey.toString();
    return entry;
}

EditorSearchCandidates TemporaryEditorSearchProvider::query(const QString& rawQuery) const
{
    return queryTask(rawQuery)({});
}

EditorSearchTask TemporaryEditorSearchProvider::queryTask(const QString& rawQuery) const
{
    const QString query = rawQuery.trimmed().toCaseFolded();
    const auto catalog = asyncCatalog ? asyncCatalog->current : nullptr;
    const auto files = catalog && catalog->root == workspaceRootValue
        && catalog->sourceFiles == asyncCatalog->requestedFiles ? catalog->fileCandidates : indexedFileCandidates;
    const bool usePublished = catalog && asyncCatalog->ready;
    const auto semantic = !catalog && semanticCatalogReady() ? indexedSemanticCandidates : QList<IndexedCandidate>{};
    return [query, catalog, files, semantic, usePublished](const EditorSearchCancellation& cancelled) {
        if (query.isEmpty() || (cancelled && cancelled())) return EditorSearchCandidates{};
        struct Cancelled {};
        try {
            QHash<QString, EditorSearchCandidate> matches;
            const auto append = [&](const IndexedCandidate& indexed) {
                if (cancelled && cancelled()) throw Cancelled{};
                const int score = qMax(fuzzyScoreFolded(indexed.foldedTitle, query),
                    fuzzyScoreFolded(indexed.foldedSearchText, query) - 200);
                if (score < 0) return;
                auto candidate = indexed.candidate;
                candidate.score = score;
                const auto previous = matches.constFind(indexed.identity);
                if (previous == matches.cend() || candidate.score > previous->score
                    || (candidate.score == previous->score && candidateMetadataLess(candidate, *previous)))
                    matches.insert(indexed.identity, std::move(candidate));
            };
            for (const auto& indexed : files) append(indexed);
            if (usePublished)
                for (const auto* indexed : catalog->uniqueCandidates) append(*indexed);
            for (const auto& indexed : semantic) append(indexed);
            auto candidates = matches.values();
            quint64 comparisons = 0;
            std::sort(candidates.begin(), candidates.end(), [&](const auto& left, const auto& right) {
                if ((++comparisons & 255) == 0 && cancelled && cancelled()) throw Cancelled{};
                return left.score != right.score ? left.score > right.score : candidateMetadataLess(left, right);
            });
            return cancelled && cancelled() ? EditorSearchCandidates{} : candidates;
        } catch (const Cancelled&) { return EditorSearchCandidates{}; }
    };
}

void TemporaryEditorSearchProvider::rebuildFileCatalog()
{
    indexedFileCandidates.clear();
    indexedFileCandidates.reserve(cachedWorkspaceFiles.size());
    for (const QString& filePath : cachedWorkspaceFiles) {
        if (cancellationCheck && cancellationCheck()) return;
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
        indexed.identity = candidateIdentity(candidate, EditorFileIdentity::lookupKey(filePath));
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
    QHash<QString, QString> relativePaths;
    for (const SemanticCatalogEntry& entry : semanticSourceCatalog) {
        if (cancellationCheck && cancellationCheck()) return;
        if (entry.location.filePath.trimmed().isEmpty())
            continue;
        const QString& title = entry.title;
        if (title.isEmpty())
            continue;
        auto relativePath = relativePaths.constFind(entry.location.filePath);
        if (relativePath == relativePaths.cend()) {
            relativePath = relativePaths.insert(
                entry.location.filePath,
                normalizedRelativePath(entry.location.filePath, workspaceRootValue));
        }
        const QString& relative = relativePath.value();
        const QString& owner = entry.owner;
        const EditorSearchCandidateType type = entry.type;
        const QString typeLabel = editorSearchCandidateTypeLabel(type);
        const QString disambiguation = owner.isEmpty()
            ? QStringLiteral("%1 in %2").arg(typeLabel, relative)
            : QStringLiteral("%1 in %2 (%3)")
                  .arg(typeLabel, relative, owner);

        EditorSearchCandidate candidate;
        candidate.location = entry.location;
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
    // Resolving an identity can open the file (including junction/symlink
    // resolution). All symbols from the same path share that work for this
    // rebuild only, so a later catalog refresh observes changed aliases.
    QHash<QString, QString> fileIdentities;
    for (const IndexedCandidate& indexed : rebuilt) {
        if (cancellationCheck && cancellationCheck()) return;
        const QString& path = indexed.candidate.location.filePath;
        auto fileIdentity = fileIdentities.constFind(path);
        if (fileIdentity == fileIdentities.cend()) {
            fileIdentity = fileIdentities.insert(
                path, EditorFileIdentity::lookupKey(path));
        }
        const QString identity = candidateIdentity(indexed.candidate,
                                                   fileIdentity.value());
        if (seen.contains(identity))
            continue;
        seen.insert(identity);
        auto unique = indexed;
        unique.identity = identity;
        indexedSemanticCandidates.append(std::move(unique));
    }
}
