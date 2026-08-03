#include "searchservice.h"

#include "editorfileidentity.h"
#include "tsdocument.h"

#include <QSet>
#include <QVector>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <utility>

std::unique_ptr<SearchService> SearchService::instance = nullptr;

namespace {
struct SearchCharRange {
    int start = -1;
    int end = -1;

    bool isValid() const
    {
        return start >= 0 && end >= start;
    }
};

struct SearchDocumentView {
    const SearchDocumentSnapshot* snapshot = nullptr;
    QString fileIdentity;
    QVector<int> lineStarts;
};

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

QString normalizedFileIdentity(const QString& fileName)
{
    QString identity = EditorFileIdentity::lookupKey(fileName);
    if (identity.isEmpty())
        identity = EditorFileIdentity::normalized(fileName);
    if (identity.isEmpty())
        identity = fileName;
    return identity;
}

QVector<int> lineStartsForText(const QString& text)
{
    QVector<int> starts;
    starts.reserve(
        qMax(1,
             static_cast<int>(
                 text.count(QLatin1Char('\n')))
                 + 1));
    starts.append(0);
    for (int index = 0; index < text.size(); ++index) {
        if (text.at(index) == QLatin1Char('\n'))
            starts.append(index + 1);
    }
    return starts;
}

int lineIndexAt(const QVector<int>& lineStarts, int charOffset)
{
    if (lineStarts.isEmpty())
        return 0;
    const auto upper =
        std::upper_bound(lineStarts.cbegin(),
                         lineStarts.cend(),
                         qMax(0, charOffset));
    return qMax(
        0,
        static_cast<int>(
            std::distance(lineStarts.cbegin(), upper))
            - 1);
}

QPair<int, int> lineColumnAt(const SearchDocumentView& view,
                             int charOffset)
{
    const int textSize =
        static_cast<int>(view.snapshot->text.size());
    const int bounded =
        qBound(0, charOffset, textSize);
    const int lineIndex = lineIndexAt(view.lineStarts, bounded);
    return qMakePair(
        lineIndex + 1,
        bounded - view.lineStarts.value(lineIndex) + 1);
}

int positionForLineColumn(const SearchDocumentView& view,
                          int oneBasedLine,
                          int oneBasedColumn)
{
    if (oneBasedLine <= 0
        || oneBasedLine > view.lineStarts.size()
        || oneBasedColumn <= 0) {
        return -1;
    }
    const int lineIndex = oneBasedLine - 1;
    const int lineStart = view.lineStarts.at(lineIndex);
    const int lineEnd =
        lineIndex + 1 < view.lineStarts.size()
        ? view.lineStarts.at(lineIndex + 1)
        : static_cast<int>(view.snapshot->text.size());
    return qBound(lineStart,
                  lineStart + oneBasedColumn - 1,
                  lineEnd);
}

rtledit::SourcePosition transactionPositionAt(
    const SearchDocumentView& view,
    int charOffset)
{
    const int textSize =
        static_cast<int>(view.snapshot->text.size());
    const int bounded =
        qBound(0, charOffset, textSize);
    const int lineIndex = lineIndexAt(view.lineStarts, bounded);
    const int lineStart = view.lineStarts.value(lineIndex);
    const int utf8Column =
        view.snapshot->text.mid(lineStart, bounded - lineStart)
            .toUtf8()
            .size();
    return rtledit::SourcePosition{
        static_cast<std::size_t>(lineIndex),
        static_cast<std::size_t>(utf8Column)};
}

bool nodeTypeIsOneOf(TSNode node,
                     std::initializer_list<const char*> types)
{
    const char* type = ts_node_type(node);
    if (!type)
        return false;
    for (const char* candidate : types) {
        if (std::strcmp(type, candidate) == 0)
            return true;
    }
    return false;
}

bool isModuleScopeNode(TSNode node)
{
    return nodeTypeIsOneOf(
        node,
        {"module_declaration",
         "interface_declaration",
         "program_declaration"});
}

bool isSyntaxBlockNode(TSNode node)
{
    return nodeTypeIsOneOf(
        node,
        {"seq_block",
         "par_block",
         "case_statement",
         "randcase_statement",
         "conditional_statement",
         "loop_statement",
         "always_construct",
         "initial_construct",
         "final_construct",
         "function_declaration",
         "task_declaration",
         "conditional_generate_construct",
         "loop_generate_construct",
         "case_generate_construct",
         "generate_region",
         "clocking_declaration",
         "property_declaration",
         "sequence_declaration",
         "covergroup_declaration",
         "class_declaration",
         "package_declaration",
         "module_declaration",
         "interface_declaration",
         "program_declaration"});
}

SearchCharRange enclosingTreeSitterRange(
    const SearchDocumentSnapshot& document,
    int cursorChar,
    ScopedSearchScope scope)
{
    if (!document.syntax
        || document.syntax->text() != document.text
        || document.text.isEmpty()) {
        return {};
    }

    const int bounded =
        qBound(0,
               cursorChar,
               static_cast<int>(document.text.size()) - 1);
    const uint32_t byteOffset =
        static_cast<uint32_t>(bounded * 2);
    TSNode node =
        ts_node_named_descendant_for_byte_range(
            document.syntax->rootNode(),
            byteOffset,
            byteOffset);
    while (!ts_node_is_null(node)) {
        const bool matches =
            scope == ScopedSearchScope::Module
            ? isModuleScopeNode(node)
            : isSyntaxBlockNode(node);
        if (matches) {
            const int start =
                static_cast<int>(ts_node_start_byte(node) / 2);
            const int end =
                static_cast<int>(ts_node_end_byte(node) / 2);
            return SearchCharRange{
                qBound(0,
                       start,
                       static_cast<int>(document.text.size())),
                qBound(0,
                       end,
                       static_cast<int>(document.text.size()))};
        }
        node = ts_node_parent(node);
    }
    return {};
}

SearchContextSnippet contextSnippet(
    const SearchDocumentView& view,
    int matchStart,
    int matchEnd,
    int requestedContextLines)
{
    SearchContextSnippet snippet;
    if (!view.snapshot
        || matchStart < 0
        || matchEnd < matchStart
        || matchEnd > view.snapshot->text.size()) {
        return snippet;
    }

    const int contextLines = qBound(0, requestedContextLines, 20);
    const int matchLine = lineIndexAt(view.lineStarts, matchStart);
    const int firstLine = qMax(0, matchLine - contextLines);
    const int lastLine =
        qMin(static_cast<int>(view.lineStarts.size()) - 1,
             matchLine + contextLines);
    const int contextStart = view.lineStarts.value(firstLine);
    const int contextEnd =
        lastLine + 1 < view.lineStarts.size()
        ? view.lineStarts.at(lastLine + 1)
        : static_cast<int>(view.snapshot->text.size());

    snippet.text =
        view.snapshot->text.mid(contextStart,
                                contextEnd - contextStart);
    snippet.sourceStartChar = contextStart;
    snippet.sourceEndChar = contextEnd;
    snippet.firstLine = firstLine + 1;
    snippet.matchStartInSnippet = matchStart - contextStart;
    snippet.matchLength = matchEnd - matchStart;
    snippet.documentRevision = view.snapshot->revision;
    return snippet;
}

bool isIdentifierCharacter(QChar character)
{
    return character.isLetterOrNumber()
        || character == QLatin1Char('_')
        || character == QLatin1Char('$');
}

bool isWholeWordMatch(const QString& text,
                      int start,
                      int end)
{
    const bool leftBoundary =
        start <= 0
        || !isIdentifierCharacter(text.at(start - 1));
    const bool rightBoundary =
        end >= text.size()
        || !isIdentifierCharacter(text.at(end));
    return leftBoundary && rightBoundary;
}

SearchMatchIdentity identityForRange(
    const SearchDocumentView& view,
    int start,
    int end)
{
    SearchMatchIdentity identity;
    identity.fileIdentity = view.fileIdentity;
    identity.anchorStartChar = start;
    identity.anchorEndChar = end;
    if (view.snapshot->syntax
        && view.snapshot->syntax->text() == view.snapshot->text) {
        const TSIdentifierTarget identifier =
            view.snapshot->syntax->identifierAt(start);
        if (identifier.ok()
            && start >= identifier.startChar
            && end <= identifier.endChar) {
            identity.kind =
                SearchMatchIdentityKind::SyntaxIdentifier;
        }
    }
    return identity;
}

const SearchDocumentView* viewForIdentity(
    const QList<SearchDocumentView>& views,
    const QHash<QString, int>& viewIndexByIdentity,
    const QString& fileIdentity)
{
    const auto it =
        viewIndexByIdentity.constFind(fileIdentity);
    if (it == viewIndexByIdentity.cend())
        return nullptr;
    return &views.at(it.value());
}

void buildDocumentViews(
    const QList<SearchDocumentSnapshot>& documents,
    QList<SearchDocumentView>* views,
    QHash<QString, int>* viewIndexByIdentity)
{
    if (!views || !viewIndexByIdentity)
        return;
    views->clear();
    viewIndexByIdentity->clear();
    views->reserve(documents.size());
    for (const SearchDocumentSnapshot& document : documents) {
        const QString identity =
            normalizedFileIdentity(document.fileName);
        if (identity.isEmpty()
            || viewIndexByIdentity->contains(identity)) {
            continue;
        }
        SearchDocumentView view;
        view.snapshot = &document;
        view.fileIdentity = identity;
        view.lineStarts = lineStartsForText(document.text);
        viewIndexByIdentity->insert(identity, views->size());
        views->append(std::move(view));
    }
}

int semanticRecordStart(
    const SemanticSymbolRecord& record,
    const SearchDocumentView& view)
{
    const int lineColumnPosition =
        positionForLineColumn(
            view,
            record.location.startLine,
            record.location.startColumn);
    const int storedPosition = record.location.position;
    if (storedPosition >= 0
        && storedPosition <= view.snapshot->text.size()) {
        if (lineColumnPosition < 0
            || lineColumnPosition == storedPosition) {
            return storedPosition;
        }
    }
    return lineColumnPosition;
}

int queryMatchInSemanticRecord(
    const SemanticSymbolRecord& record,
    const SearchDocumentView& view,
    const ScopedSearchQuery& query)
{
    const int queryLength =
        static_cast<int>(query.text.size());
    const int recordStart = semanticRecordStart(record, view);
    if (recordStart < 0)
        return -1;

    const Qt::CaseSensitivity sensitivity =
        query.caseSensitive
        ? Qt::CaseSensitive
        : Qt::CaseInsensitive;
    if (query.wholeWord
        && QString::compare(record.name,
                            query.text,
                            sensitivity) != 0) {
        return -1;
    }

    if (view.snapshot->syntax
        && view.snapshot->syntax->text() == view.snapshot->text) {
        const TSIdentifierTarget identifier =
            view.snapshot->syntax->identifierAt(recordStart);
        if (identifier.ok()
            && identifier.text == record.name) {
            const int relative =
                identifier.text.indexOf(
                    query.text, 0, sensitivity);
            if (relative >= 0) {
                const int match = identifier.startChar + relative;
                if (!query.wholeWord
                    || isWholeWordMatch(
                        view.snapshot->text,
                        match,
                        match + queryLength)) {
                    return match;
                }
            }
        }
    }

    if (record.name.isEmpty()
        || view.snapshot->text.mid(
               recordStart, record.name.size())
            != record.name) {
        return -1;
    }
    const int recordLength =
        qMax(record.location.length,
             static_cast<int>(record.name.size()));
    const int boundedEnd =
        qMin(static_cast<int>(view.snapshot->text.size()),
             recordStart + qMax(1, recordLength));
    const int match =
        view.snapshot->text.indexOf(
            query.text, recordStart, sensitivity);
    if (match < 0
        || match + queryLength > boundedEnd) {
        return -1;
    }
    if (query.wholeWord
        && !isWholeWordMatch(view.snapshot->text,
                             match,
                             match + queryLength)) {
        return -1;
    }
    return match;
}
}

bool SearchMatchIdentity::isValid() const
{
    return !fileIdentity.isEmpty()
        && anchorStartChar >= 0
        && anchorEndChar > anchorStartChar;
}

QString SearchMatchIdentity::toString() const
{
    return QStringLiteral("%1:%2|%3|%4:%5")
        .arg(fileIdentity.size())
        .arg(fileIdentity)
        .arg(static_cast<int>(kind))
        .arg(anchorStartChar)
        .arg(anchorEndChar);
}

bool SearchMatchIdentity::operator==(
    const SearchMatchIdentity& other) const
{
    return fileIdentity == other.fileIdentity
        && kind == other.kind
        && anchorStartChar == other.anchorStartChar
        && anchorEndChar == other.anchorEndChar;
}

bool SearchContextSnippet::isValid() const
{
    return sourceStartChar >= 0
        && sourceEndChar >= sourceStartChar
        && firstLine > 0
        && matchStartInSnippet >= 0
        && matchLength > 0
        && matchStartInSnippet + matchLength <= text.size();
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
    indexQuery.declarationKinds = query.declarationKinds;
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
            semanticMetadataForSymbolRecord(item.symbolRecord);
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

ScopedSearchResponse SearchService::search(
    const ScopedSearchQuery& query,
    const QList<SearchDocumentSnapshot>& documents) const
{
    ScopedSearchResponse response;
    if (query.text.isEmpty()) {
        response.status = ScopedSearchStatus::EmptyQuery;
        return response;
    }
    const int queryLength =
        static_cast<int>(query.text.size());

    QList<SearchDocumentView> views;
    QHash<QString, int> viewIndexByIdentity;
    buildDocumentViews(documents, &views, &viewIndexByIdentity);

    const bool activeScope =
        query.scope != ScopedSearchScope::Workspace;
    QString activeIdentity =
        normalizedFileIdentity(query.activeFileName);
    if (activeIdentity.isEmpty() && views.size() == 1)
        activeIdentity = views.first().fileIdentity;
    const SearchDocumentView* activeView =
        viewForIdentity(views,
                        viewIndexByIdentity,
                        activeIdentity);
    if (activeScope && !activeView) {
        response.status =
            ScopedSearchStatus::ActiveDocumentNotFound;
        return response;
    }

    SearchCharRange activeRange;
    if (activeView) {
        response.activeFileName =
            activeView->snapshot->fileName;
        if (query.scope == ScopedSearchScope::SyntaxBlock
            || query.scope == ScopedSearchScope::Module) {
            if (!activeView->snapshot->syntax
                || activeView->snapshot->syntax->text()
                    != activeView->snapshot->text) {
                response.status =
                    ScopedSearchStatus::SyntaxSnapshotRequired;
                return response;
            }
            activeRange = enclosingTreeSitterRange(
                *activeView->snapshot,
                query.cursorChar,
                query.scope);
            if (!activeRange.isValid()) {
                response.status =
                    ScopedSearchStatus::NoEnclosingScope;
                return response;
            }
        } else {
            activeRange = SearchCharRange{
                0,
                static_cast<int>(
                    activeView->snapshot->text.size())};
        }
        response.scopeStartChar = activeRange.start;
        response.scopeEndChar = activeRange.end;
    }

    QHash<QString, int> resultIndexByIdentity;
    const Qt::CaseSensitivity sensitivity =
        query.caseSensitive
        ? Qt::CaseSensitive
        : Qt::CaseInsensitive;

    for (const SearchDocumentView& view : std::as_const(views)) {
        SearchCharRange range{
            0,
            static_cast<int>(
                view.snapshot->text.size())};
        if (activeScope) {
            if (view.fileIdentity != activeIdentity)
                continue;
            range = activeRange;
        }

        int searchFrom = range.start;
        while (searchFrom <= range.end - queryLength) {
            const int matchStart =
                view.snapshot->text.indexOf(
                    query.text, searchFrom, sensitivity);
            if (matchStart < 0
                || matchStart + queryLength > range.end) {
                break;
            }
            const int matchEnd =
                matchStart + queryLength;
            searchFrom = matchEnd;
            if (query.wholeWord
                && !isWholeWordMatch(
                    view.snapshot->text,
                    matchStart,
                    matchEnd)) {
                continue;
            }

            ScopedSearchResult result;
            result.identity =
                identityForRange(view, matchStart, matchEnd);
            result.fileName = view.snapshot->fileName;
            result.matchStartChar = matchStart;
            result.matchEndChar = matchEnd;
            const QPair<int, int> lineColumn =
                lineColumnAt(view, matchStart);
            result.line = lineColumn.first;
            result.column = lineColumn.second;
            result.matchedText =
                view.snapshot->text.mid(
                    matchStart, queryLength);
            result.context =
                contextSnippet(
                    view,
                    matchStart,
                    matchEnd,
                    query.contextLineCount);
            result.fromTextSearch = true;

            const QString identityKey =
                result.identity.toString();
            if (!resultIndexByIdentity.contains(identityKey)) {
                resultIndexByIdentity.insert(
                    identityKey, response.results.size());
                response.results.append(std::move(result));
            }
        }
    }

    if (query.includeSemantic) {
        SearchQuery symbolQuery;
        symbolQuery.text = query.text;
        symbolQuery.caseSensitive = query.caseSensitive;
        symbolQuery.exactMatch = query.wholeWord;
        symbolQuery.maxResults = -1;
        if (activeScope && activeView)
            symbolQuery.fileName = activeView->snapshot->fileName;

        const QList<SearchResult> semanticResults =
            findSymbols(symbolQuery);
        for (const SearchResult& semantic :
             semanticResults) {
            const QString semanticFileIdentity =
                normalizedFileIdentity(
                    semantic.symbolRecord.location.fileName);
            const SearchDocumentView* view =
                viewForIdentity(
                    views,
                    viewIndexByIdentity,
                    semanticFileIdentity);
            if (!view)
                continue;

            const int matchStart =
                queryMatchInSemanticRecord(
                    semantic.symbolRecord,
                    *view,
                    query);
            const int matchEnd =
                matchStart < 0
                ? -1
                : matchStart + queryLength;
            if (matchStart < 0
                || matchEnd > view->snapshot->text.size()) {
                continue;
            }
            if (activeScope
                && (view->fileIdentity != activeIdentity
                    || matchStart < activeRange.start
                    || matchEnd > activeRange.end)) {
                continue;
            }

            const SearchMatchIdentity identity =
                identityForRange(
                    *view, matchStart, matchEnd);
            const QString identityKey = identity.toString();
            const auto existing =
                resultIndexByIdentity.constFind(identityKey);
            if (existing != resultIndexByIdentity.cend()) {
                ScopedSearchResult& merged =
                    response.results[existing.value()];
                merged.fromSemanticSearch = true;
                merged.hasSemanticRecord = true;
                merged.semanticRecord =
                    semantic.symbolRecord;
                merged.semanticScore =
                    qMax(merged.semanticScore,
                         semantic.score);
                continue;
            }

            ScopedSearchResult result;
            result.identity = identity;
            result.fileName = view->snapshot->fileName;
            result.matchStartChar = matchStart;
            result.matchEndChar = matchEnd;
            const QPair<int, int> lineColumn =
                lineColumnAt(*view, matchStart);
            result.line = lineColumn.first;
            result.column = lineColumn.second;
            result.matchedText =
                view->snapshot->text.mid(
                    matchStart, queryLength);
            result.context =
                contextSnippet(
                    *view,
                    matchStart,
                    matchEnd,
                    query.contextLineCount);
            result.fromSemanticSearch = true;
            result.hasSemanticRecord = true;
            result.semanticRecord =
                semantic.symbolRecord;
            result.semanticScore = semantic.score;
            resultIndexByIdentity.insert(
                identityKey, response.results.size());
            response.results.append(std::move(result));
        }
    }

    std::stable_sort(
        response.results.begin(),
        response.results.end(),
        [](const ScopedSearchResult& lhs,
           const ScopedSearchResult& rhs) {
            if (lhs.identity.fileIdentity
                != rhs.identity.fileIdentity) {
                return lhs.identity.fileIdentity
                    < rhs.identity.fileIdentity;
            }
            if (lhs.matchStartChar != rhs.matchStartChar)
                return lhs.matchStartChar < rhs.matchStartChar;
            return lhs.matchEndChar < rhs.matchEndChar;
        });
    if (query.maxResults >= 0
        && response.results.size() > query.maxResults) {
        response.results =
            response.results.mid(0, query.maxResults);
    }
    return response;
}

ReplacePreviewPlan SearchService::planReplace(
    const QList<ScopedSearchResult>& results,
    const QList<SearchDocumentSnapshot>& currentDocuments,
    const ReplacePreviewRequest& request) const
{
    ReplacePreviewPlan preview;

    QList<SearchDocumentView> views;
    QHash<QString, int> viewIndexByIdentity;
    buildDocumentViews(currentDocuments,
                       &views,
                       &viewIndexByIdentity);

    QHash<QString, int> filePreviewIndexByIdentity;
    QSet<QString> seenMatches;
    struct SelectedMatch {
        const ScopedSearchResult* result = nullptr;
        const SearchDocumentView* view = nullptr;
    };
    QList<SelectedMatch> selectedMatches;
    bool stale = false;
    QString staleReason;

    for (const ScopedSearchResult& result : results) {
        if (!result.identity.isValid())
            continue;
        const QString matchIdentity =
            result.identity.toString();
        if (seenMatches.contains(matchIdentity))
            continue;
        seenMatches.insert(matchIdentity);

        const SearchDocumentView* view =
            viewForIdentity(
                views,
                viewIndexByIdentity,
                result.identity.fileIdentity);
        int filePreviewIndex =
            filePreviewIndexByIdentity.value(
                result.identity.fileIdentity, -1);
        if (filePreviewIndex < 0) {
            ReplaceFilePreview file;
            file.fileIdentity =
                result.identity.fileIdentity;
            file.fileName =
                view
                ? view->snapshot->fileName
                : result.fileName;
            file.documentRevision =
                view
                ? view->snapshot->revision
                : result.context.documentRevision;
            file.selected =
                request.fileSelectionByIdentity.value(
                    file.fileIdentity,
                    request.filesSelectedByDefault);
            filePreviewIndex = preview.files.size();
            filePreviewIndexByIdentity.insert(
                file.fileIdentity, filePreviewIndex);
            preview.files.append(std::move(file));
        }

        ReplaceFilePreview& file =
            preview.files[filePreviewIndex];
        ReplaceMatchPreview match;
        match.identity = result.identity;
        match.matchedText = result.matchedText;
        match.context = result.context;
        match.selected =
            file.selected
            && request.matchSelectionByIdentity.value(
                matchIdentity,
                request.matchesSelectedByDefault);
        file.matches.append(match);

        if (!match.selected)
            continue;
        if (!view) {
            stale = true;
            staleReason =
                QStringLiteral(
                    "Search result document is no longer available: %1")
                    .arg(result.fileName);
            continue;
        }
        const bool rangeValid =
            result.matchStartChar >= 0
            && result.matchEndChar > result.matchStartChar
            && result.matchEndChar
                <= view->snapshot->text.size();
        const bool revisionMatches =
            result.context.documentRevision
            == view->snapshot->revision;
        const bool textMatches =
            rangeValid
            && view->snapshot->text.mid(
                   result.matchStartChar,
                   result.matchEndChar
                       - result.matchStartChar)
                == result.matchedText;
        if (!rangeValid
            || !revisionMatches
            || !textMatches) {
            stale = true;
            staleReason =
                QStringLiteral(
                    "Search result is stale in %1 at %2:%3.")
                    .arg(result.fileName)
                    .arg(result.line)
                    .arg(result.column);
            continue;
        }
        selectedMatches.append(
            SelectedMatch{&result, view});
    }

    if (stale) {
        preview.status =
            ReplacePreviewStatus::StaleSearchResult;
        preview.failureReason = staleReason;
        return preview;
    }
    if (selectedMatches.isEmpty()) {
        preview.status =
            ReplacePreviewStatus::NoSelectedMatches;
        preview.failureReason =
            QStringLiteral("No replacement matches are selected.");
        return preview;
    }

    QHash<QString, QList<QPair<int, int>>> rangesByFile;
    for (const SelectedMatch& selected :
         std::as_const(selectedMatches)) {
        rangesByFile[selected.result->identity.fileIdentity]
            .append(
                qMakePair(selected.result->matchStartChar,
                          selected.result->matchEndChar));
    }
    for (auto it = rangesByFile.begin();
         it != rangesByFile.end();
         ++it) {
        QList<QPair<int, int>>& ranges = it.value();
        std::sort(
            ranges.begin(),
            ranges.end(),
            [](const QPair<int, int>& lhs,
               const QPair<int, int>& rhs) {
                if (lhs.first != rhs.first)
                    return lhs.first < rhs.first;
                return lhs.second < rhs.second;
            });
        for (int index = 1; index < ranges.size(); ++index) {
            if (ranges.at(index).first
                < ranges.at(index - 1).second) {
                preview.status =
                    ReplacePreviewStatus::OverlappingMatches;
                preview.failureReason =
                    QStringLiteral(
                        "Selected replacement matches overlap in %1.")
                        .arg(it.key());
                return preview;
            }
        }
    }

    std::vector<rtledit::WorkspaceTextEdit> edits;
    std::vector<rtledit::TextEditProvenance> provenance;
    edits.reserve(
        static_cast<std::size_t>(selectedMatches.size()));
    provenance.reserve(
        static_cast<std::size_t>(selectedMatches.size()));
    for (int index = 0;
         index < selectedMatches.size();
         ++index) {
        const SelectedMatch& selected =
            selectedMatches.at(index);
        const ScopedSearchResult& result =
            *selected.result;
        const SearchDocumentView& view =
            *selected.view;
        const rtledit::SourceRange range{
            transactionPositionAt(
                view, result.matchStartChar),
            transactionPositionAt(
                view, result.matchEndChar)};

        rtledit::WorkspaceTextEdit edit;
        const std::string filePath =
            view.snapshot->fileName.toUtf8().toStdString();
        edit.filePath = filePath;
        edit.expectedDocumentVersion =
            rtledit::DocumentVersion{
                view.snapshot->revision};
        edit.range = range;
        edit.expectedText =
            result.matchedText.toUtf8().toStdString();
        edit.newText =
            request.replacementText.toUtf8().toStdString();
        edits.push_back(std::move(edit));

        rtledit::TextEditProvenance item;
        item.editIndex = static_cast<std::size_t>(index);
        item.actionId = rtledit::kReplaceTextActionId;
        item.anchorName =
            result.identity.toString()
                .toUtf8()
                .toStdString();
        item.description =
            "Selected search replacement";
        if (result.identity.kind
            == SearchMatchIdentityKind::SyntaxIdentifier) {
            item.anchor.source =
                rtledit::AnchorResolutionSource::TreeSitter;
            item.anchor.resolver =
                "SearchService::planReplace";
        }
        item.sourceFilePath = filePath;
        item.sourceRange = range;
        provenance.push_back(std::move(item));
    }

    rtledit::SemanticEditIntent intent;
    intent.kind = rtledit::SemanticEditKind::ReplaceText;
    preview.transactionPlan =
        rtledit::makeWorkspaceEditPlan(
            intent,
            rtledit::RiskLevel::High,
            rtledit::PreviewPolicy::Diff,
            std::move(edits),
            std::move(provenance));
    const rtledit::EditPlanValidationResult validation =
        rtledit::validateWorkspaceEditPlan(
            preview.transactionPlan);
    if (!validation.valid()) {
        preview.status =
            ReplacePreviewStatus::InvalidTransactionPlan;
        preview.failureReason =
            QString::fromStdString(validation.message);
        return preview;
    }

    preview.status = ReplacePreviewStatus::Ready;
    return preview;
}

SemanticIndex* SearchService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}
