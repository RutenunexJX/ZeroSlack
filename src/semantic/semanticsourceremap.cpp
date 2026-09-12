#include "semanticsourceremap.h"

#include "semanticindexsnapshot.h"
#include "triviapositionmap.h"

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
    const int oldStart = range->position >= 0
        ? range->position
        : oldLineIndex.positionForLineColumn(range->line,
                                             range->column);
    int oldEnd = range->length > 0
        ? oldStart + range->length
        : oldLineIndex.positionForLineColumn(range->endLine,
                                             range->endColumn);
    if (oldEnd < oldStart)
        oldEnd = oldStart;
    const int newStart = positionMap.map(oldStart, false);
    const int newEnd = positionMap.map(oldEnd, true);
    range->position = newStart;
    range->length = qMax(0, newEnd - newStart);
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
    if (!positionMap.isCompatible())
        return nullptr;
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
        for (SemanticSourceRange& range : diagnostic.ranges) {
            remapSourceRange(&range,
                             fileMatcher,
                             oldLineIndex,
                             newLineIndex,
                             positionMap);
        }
        if (!diagnostic.ranges.isEmpty()) {
            diagnostic.line = diagnostic.ranges.first().line;
            diagnostic.column = diagnostic.ranges.first().column;
        } else {
            const int oldPosition = oldLineIndex.positionForLineColumn(
                diagnostic.line, diagnostic.column);
            const int newPosition = positionMap.map(oldPosition, false);
            newLineIndex.lineColumnForPosition(newPosition,
                                               &diagnostic.line,
                                               &diagnostic.column);
        }
        if (documentRevision > 0)
            diagnostic.documentRevision = documentRevision;
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
