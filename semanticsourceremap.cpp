#include "semanticsourceremap.h"

#include "semanticindexsnapshot.h"
#include "tsdocument.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QVector>

#include <algorithm>

namespace {
QString normalizedRemapPath(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    QString path = QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
#ifdef Q_OS_WIN
    path = path.toCaseFolded();
#endif
    return path;
}

class RemapFileMatcher
{
public:
    explicit RemapFileMatcher(const QString& fileName)
        : targetFileName(fileName),
          normalizedTarget(normalizedRemapPath(fileName))
    {
    }

    bool matches(const QString& candidate) const
    {
        if (candidate.isEmpty() || normalizedTarget.isEmpty())
            return false;
        if (candidate == targetFileName)
            return true;
        const auto cached = matchCache.constFind(candidate);
        if (cached != matchCache.cend())
            return cached.value();
        const bool result = normalizedRemapPath(candidate) == normalizedTarget;
        matchCache.insert(candidate, result);
        return result;
    }

private:
    QString targetFileName;
    QString normalizedTarget;
    mutable QHash<QString, bool> matchCache;
};

int fallbackMappedPosition(int position,
                           const SourceTextDelta& delta,
                           bool endAffinity)
{
    if (position < 0)
        return position;
    if (delta.oldStart == delta.oldEnd
        && position == delta.oldStart) {
        // Existing text at an insertion point moves right. A range ending at
        // that point remains before the inserted trivia.
        return endAffinity ? position : delta.newEnd;
    }
    if (position <= delta.oldStart)
        return position;
    if (position >= delta.oldEnd)
        return position + delta.lengthDelta();
    return endAffinity ? delta.newEnd : delta.oldStart;
}

struct SemanticLeafSpan {
    QString type;
    QString text;
    int start = 0;
    int end = 0;
};

bool triviaNode(TSNode node)
{
    const char* rawType = ts_node_type(node);
    const QString type = rawType ? QString::fromLatin1(rawType) : QString();
    return type == QLatin1String("one_line_comment")
        || type == QLatin1String("block_comment")
        || type == QLatin1String("comment");
}

void collectSemanticLeaves(const QString& text,
                           TSNode node,
                           QVector<SemanticLeafSpan>* leaves)
{
    if (!leaves || ts_node_is_null(node) || triviaNode(node))
        return;
    const uint32_t childCount = ts_node_child_count(node);
    if (childCount > 0) {
        for (uint32_t index = 0; index < childCount; ++index) {
            collectSemanticLeaves(text,
                                  ts_node_child(node, index),
                                  leaves);
        }
        return;
    }

    const int start = static_cast<int>(ts_node_start_byte(node) / 2u);
    const int end = static_cast<int>(ts_node_end_byte(node) / 2u);
    if (start < 0 || end <= start || start > text.size())
        return;
    const char* rawType = ts_node_type(node);
    SemanticLeafSpan span;
    span.type = rawType ? QString::fromLatin1(rawType) : QString();
    span.start = start;
    span.end = qMin(end, text.size());
    span.text = text.mid(span.start, span.end - span.start);
    leaves->append(std::move(span));
}

class TriviaPositionMap
{
public:
    TriviaPositionMap(const QString& oldText,
                      const QString& newText,
                      const SourceTextDelta& fallbackDelta)
        : oldLength(oldText.size()),
          newLength(newText.size()),
          fallback(fallbackDelta)
    {
        TSDocument oldDocument;
        TSDocument newDocument;
        oldDocument.setText(oldText);
        newDocument.setText(newText);
        collectSemanticLeaves(oldText,
                              oldDocument.rootNode(),
                              &oldLeaves);
        collectSemanticLeaves(newText,
                              newDocument.rootNode(),
                              &newLeaves);
        compatible = oldLeaves.size() == newLeaves.size();
        for (int index = 0; compatible && index < oldLeaves.size(); ++index) {
            compatible = oldLeaves.at(index).type == newLeaves.at(index).type
                && oldLeaves.at(index).text == newLeaves.at(index).text;
        }
    }

    int map(int position, bool endAffinity) const
    {
        if (position < 0 || !compatible || oldLeaves.isEmpty()) {
            return fallbackMappedPosition(position,
                                          fallback,
                                          endAffinity);
        }
        position = qBound(0, position, oldLength);

        if (endAffinity) {
            const auto endingAtPosition = std::lower_bound(
                oldLeaves.cbegin(),
                oldLeaves.cend(),
                position,
                [](const SemanticLeafSpan& span, int value) {
                    return span.end < value;
                });
            if (endingAtPosition != oldLeaves.cend()
                && endingAtPosition->end == position) {
                const int index = static_cast<int>(std::distance(
                    oldLeaves.cbegin(), endingAtPosition));
                return newLeaves.at(index).end;
            }
        }

        const auto upper = std::upper_bound(
            oldLeaves.cbegin(),
            oldLeaves.cend(),
            position,
            [](int value, const SemanticLeafSpan& span) {
                return value < span.start;
            });
        const int previousIndex = static_cast<int>(
            std::distance(oldLeaves.cbegin(), upper)) - 1;
        if (previousIndex >= 0) {
            const SemanticLeafSpan& oldSpan = oldLeaves.at(previousIndex);
            const SemanticLeafSpan& newSpan = newLeaves.at(previousIndex);
            if (position >= oldSpan.start && position < oldSpan.end) {
                return qBound(newSpan.start,
                              newSpan.start + position - oldSpan.start,
                              newSpan.end);
            }
            if (endAffinity && position == oldSpan.end)
                return newSpan.end;
        }

        const int nextIndex = previousIndex + 1;
        if (!endAffinity && nextIndex >= 0
            && nextIndex < newLeaves.size()) {
            return newLeaves.at(nextIndex).start;
        }
        if (endAffinity && previousIndex >= 0)
            return newLeaves.at(previousIndex).end;
        if (!newLeaves.isEmpty()) {
            if (position <= oldLeaves.first().start) {
                return qBound(0,
                              newLeaves.first().start
                                  - (oldLeaves.first().start - position),
                              newLength);
            }
            return qBound(0,
                          newLeaves.last().end
                              + (position - oldLeaves.last().end),
                          newLength);
        }
        return fallbackMappedPosition(position, fallback, endAffinity);
    }

private:
    QVector<SemanticLeafSpan> oldLeaves;
    QVector<SemanticLeafSpan> newLeaves;
    int oldLength = 0;
    int newLength = 0;
    SourceTextDelta fallback;
    bool compatible = false;
};

class TextLineIndex
{
public:
    explicit TextLineIndex(const QString& text)
        : textLength(text.size())
    {
        lineStarts.append(0);
        for (int position = 0; position < text.size(); ++position) {
            if (text.at(position) == QLatin1Char('\n'))
                lineStarts.append(position + 1);
        }
    }

    int positionForLineColumn(int line, int column) const
    {
        if (line <= 0 || column <= 0)
            return -1;
        const int lineIndex = line - 1;
        if (lineIndex >= lineStarts.size())
            return textLength;
        const int lineStart = lineStarts.at(lineIndex);
        const int lineEnd = lineIndex + 1 < lineStarts.size()
            ? lineStarts.at(lineIndex + 1) - 1
            : textLength;
        return qBound(lineStart, lineStart + column - 1, lineEnd);
    }

    void lineColumnForPosition(int position,
                               int* line,
                               int* column) const
    {
        const int bounded = qBound(0, position, textLength);
        const auto upper = std::upper_bound(lineStarts.cbegin(),
                                            lineStarts.cend(),
                                            bounded);
        const int lineIndex = qMax(
            0,
            static_cast<int>(std::distance(lineStarts.cbegin(), upper)) - 1);
        const int lineStart = lineStarts.at(lineIndex);
        if (line)
            *line = lineIndex + 1;
        if (column)
            *column = bounded - lineStart + 1;
    }

private:
    QVector<int> lineStarts;
    int textLength = 0;
};

void remapStableKey(SymbolStableKey* key,
                    const RemapFileMatcher& fileMatcher,
                    const TriviaPositionMap& positionMap)
{
    if (!key || !fileMatcher.matches(key->fileName))
        return;
    const int start = positionMap.map(key->sourcePosition, false);
    const int end = positionMap.map(key->sourcePosition + key->sourceLength,
                                    true);
    key->sourcePosition = start;
    key->sourceLength = qMax(0, end - start);
}

void remapLocation(SemanticSymbolLocation* location,
                   const RemapFileMatcher& fileMatcher,
                   const TextLineIndex& oldLineIndex,
                   const TextLineIndex& newLineIndex,
                   const TriviaPositionMap& positionMap)
{
    if (!location || !fileMatcher.matches(location->fileName))
        return;
    int oldStart = location->position;
    if (oldStart < 0) {
        oldStart = oldLineIndex.positionForLineColumn(
            location->startLine, location->startColumn);
    }
    int oldEnd = oldStart + qMax(0, location->length);
    if (location->length <= 0 && location->endLine > 0) {
        oldEnd = oldLineIndex.positionForLineColumn(
            location->endLine, location->endColumn);
    }
    const int newStart = positionMap.map(oldStart, false);
    const int newEnd = positionMap.map(oldEnd, true);
    location->position = newStart;
    location->length = qMax(0, newEnd - newStart);
    newLineIndex.lineColumnForPosition(newStart,
                                       &location->startLine,
                                       &location->startColumn);
    newLineIndex.lineColumnForPosition(newEnd,
                                       &location->endLine,
                                       &location->endColumn);
}

void remapSourceRange(SemanticSourceRange* range,
                      const RemapFileMatcher& fileMatcher,
                      const TextLineIndex& oldLineIndex,
                      const TextLineIndex& newLineIndex,
                      const TriviaPositionMap& positionMap)
{
    if (!range || !fileMatcher.matches(range->fileName))
        return;
    const int oldStart = oldLineIndex.positionForLineColumn(range->line,
                                                            range->column);
    const int oldEnd = oldLineIndex.positionForLineColumn(range->endLine,
                                                          range->endColumn);
    const int newStart = positionMap.map(oldStart, false);
    const int newEnd = positionMap.map(oldEnd, true);
    newLineIndex.lineColumnForPosition(newStart,
                                       &range->line,
                                       &range->column);
    newLineIndex.lineColumnForPosition(newEnd,
                                       &range->endLine,
                                       &range->endColumn);
}
}

std::shared_ptr<const SemanticIndexSnapshot>
SemanticSourceRemapper::remapSnapshot(
    std::shared_ptr<const SemanticIndexSnapshot> snapshot,
    const QString& fileName,
    const QString& oldText,
    const QString& newText,
    const SourceTextDelta& delta,
    std::uint64_t documentRevision)
{
    if (!snapshot || fileName.isEmpty())
        return snapshot;

    const RemapFileMatcher fileMatcher(fileName);
    const TextLineIndex oldLineIndex(oldText);
    const TextLineIndex newLineIndex(newText);
    const TriviaPositionMap positionMap(oldText, newText, delta);
    QList<SemanticSymbolRecord> records = snapshot->getSymbolRecords();
    for (SemanticSymbolRecord& record : records) {
        remapStableKey(&record.stableKey, fileMatcher, positionMap);
        remapStableKey(&record.owner.stableKey, fileMatcher, positionMap);
        remapStableKey(&record.type.stableKey, fileMatcher, positionMap);
        if (!fileMatcher.matches(record.location.fileName))
            continue;
        remapLocation(&record.location,
                      fileMatcher,
                      oldLineIndex,
                      newLineIndex,
                      positionMap);
        if (documentRevision > 0)
            record.presentation.documentRevision = documentRevision;
    }

    QList<SemanticRelationship> relationships = snapshot->relationships();
    for (SemanticRelationship& relationship : relationships) {
        remapStableKey(&relationship.fromStableKey, fileMatcher, positionMap);
        remapStableKey(&relationship.toStableKey, fileMatcher, positionMap);
        remapSourceRange(&relationship.evidenceRange,
                         fileMatcher,
                         oldLineIndex,
                         newLineIndex,
                         positionMap);
    }

    QList<SemanticDiagnostic> diagnostics = snapshot->diagnostics();
    for (SemanticDiagnostic& diagnostic : diagnostics) {
        if (!fileMatcher.matches(diagnostic.fileName))
            continue;
        const int oldPosition = oldLineIndex.positionForLineColumn(
            diagnostic.line, diagnostic.column);
        const int newPosition = positionMap.map(oldPosition, false);
        newLineIndex.lineColumnForPosition(newPosition,
                                           &diagnostic.line,
                                           &diagnostic.column);
    }

    QHash<QString, QString> contents = snapshot->fileContents();
    bool replaced = false;
    for (auto it = contents.begin(); it != contents.end(); ++it) {
        if (fileMatcher.matches(it.key())) {
            it.value() = newText;
            replaced = true;
        }
    }
    if (!replaced)
        contents.insert(fileName, newText);

    return std::make_shared<const SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolRecords(std::move(records),
                                                 std::move(relationships),
                                                 std::move(diagnostics),
                                                 std::move(contents)));
}

QList<EffectiveValueFact> SemanticSourceRemapper::remapEffectiveFacts(
    QList<EffectiveValueFact> facts,
    const QString& fileName,
    const QString& oldText,
    const QString& newText,
    const SourceTextDelta& delta,
    std::uint64_t documentRevision)
{
    const RemapFileMatcher fileMatcher(fileName);
    const TextLineIndex newLineIndex(newText);
    const TriviaPositionMap positionMap(oldText, newText, delta);
    for (EffectiveValueFact& fact : facts) {
        if (!fileMatcher.matches(fact.fileName))
            continue;
        fact.startPosition = positionMap.map(fact.startPosition, false);
        fact.endPosition = positionMap.map(fact.endPosition, true);
        int column = 1;
        newLineIndex.lineColumnForPosition(fact.startPosition,
                                           &fact.line,
                                           &column);
        if (documentRevision > 0)
            fact.documentRevision = documentRevision;
    }
    return facts;
}
