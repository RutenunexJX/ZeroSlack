#include "annotationlayer.h"

#include <QSet>

#include <algorithm>
#include <utility>

namespace {
QString deduplicationKey(const EditorAnnotation& annotation)
{
    const QString identity = annotation.semanticKey.isEmpty()
        ? annotation.text
        : annotation.semanticKey;
    return QStringLiteral("%1|%2|%3|%4|%5|%6|%7")
        .arg(static_cast<int>(annotation.kind))
        .arg(static_cast<int>(annotation.placement))
        .arg(annotation.range.startPosition)
        .arg(annotation.range.endPosition)
        .arg(annotation.range.firstLine)
        .arg(annotation.range.lastLine)
        .arg(identity);
}

QString occupancyKey(const EditorAnnotation& annotation, int lane)
{
    return QStringLiteral("%1|%2|%3")
        .arg(annotation.range.firstLine)
        .arg(static_cast<int>(annotation.placement))
        .arg(lane);
}

EditorAnnotationRange occupiedRange(const EditorAnnotation& annotation)
{
    EditorAnnotationRange range = annotation.range;
    if (range.endPosition <= range.startPosition) {
        range.endPosition =
            range.startPosition + qMax(1, annotation.text.size());
    }
    return range;
}

bool annotationOrder(const EditorAnnotation& left,
                     const EditorAnnotation& right)
{
    if (left.range.firstLine != right.range.firstLine)
        return left.range.firstLine < right.range.firstLine;
    if (left.priority != right.priority)
        return left.priority > right.priority;
    if (left.placement != right.placement) {
        return static_cast<int>(left.placement)
            < static_cast<int>(right.placement);
    }
    if (left.range.startPosition != right.range.startPosition)
        return left.range.startPosition < right.range.startPosition;
    if (left.range.endPosition != right.range.endPosition)
        return left.range.endPosition < right.range.endPosition;
    if (left.kind != right.kind)
        return static_cast<int>(left.kind) < static_cast<int>(right.kind);
    return left.text < right.text;
}

struct IntervalQueryNode {
    int treeIndex = 0;
    int begin = 0;
    int end = 0;
};

QList<int> overlappingAnnotationIndices(
    const QList<int>& maximumLastLineTree,
    int leafCount,
    int upper,
    int firstVisibleLine)
{
    QList<int> result;
    if (leafCount <= 0
        || upper <= 0
        || maximumLastLineTree.size() <= 1
        || maximumLastLineTree.at(1) < firstVisibleLine) {
        return result;
    }

    QList<IntervalQueryNode> pending;
    pending.reserve(32);
    pending.append({1, 0, leafCount});
    while (!pending.isEmpty()) {
        const IntervalQueryNode node = pending.takeLast();
        if (node.begin >= upper
            || maximumLastLineTree.at(node.treeIndex)
                   < firstVisibleLine) {
            continue;
        }
        if (node.end - node.begin == 1) {
            result.append(node.begin);
            continue;
        }

        const int middle = node.begin + (node.end - node.begin) / 2;
        pending.append(
            {node.treeIndex * 2 + 1, middle, node.end});
        pending.append(
            {node.treeIndex * 2, node.begin, middle});
    }
    return result;
}
}

bool EditorAnnotationRange::isValid() const
{
    return startPosition >= 0
        && endPosition >= startPosition
        && firstLine >= 0
        && lastLine >= firstLine;
}

bool EditorAnnotationRange::overlaps(
    const EditorAnnotationRange& other) const
{
    if (!isValid() || !other.isValid())
        return false;
    if (lastLine < other.firstLine || other.lastLine < firstLine)
        return false;
    return startPosition < other.endPosition
        && other.startPosition < endPosition;
}

bool EditorAnnotation::isValid() const
{
    const bool textRequired =
        kind != EditorAnnotationKind::TemplateSlot
        && kind != EditorAnnotationKind::ColumnCaret;
    return range.isValid() && (!textRequired || !text.isEmpty());
}

void AnnotationLayer::setSourceAnnotations(
    const QString& sourceId,
    const QList<EditorAnnotation>& annotations)
{
    if (sourceId.trimmed().isEmpty())
        return;

    SourceIndex index;
    index.inputCount = annotations.size();
    index.annotations.reserve(annotations.size());
    for (const EditorAnnotation& annotation : annotations) {
        if (annotation.isValid())
            index.annotations.append(annotation);
    }
    std::sort(index.annotations.begin(),
              index.annotations.end(),
              [](const EditorAnnotation& left,
                 const EditorAnnotation& right) {
                  if (left.range.firstLine
                      != right.range.firstLine) {
                      return left.range.firstLine
                          < right.range.firstLine;
                  }
                  if (left.range.lastLine
                      != right.range.lastLine) {
                      return left.range.lastLine
                          < right.range.lastLine;
                  }
                  return annotationOrder(left, right);
              });
    index.treeLeafCount = 1;
    while (index.treeLeafCount < index.annotations.size())
        index.treeLeafCount *= 2;
    index.maximumLastLineTree.fill(
        -1,
        index.treeLeafCount * 2);
    for (int annotationIndex = 0;
         annotationIndex < index.annotations.size();
         ++annotationIndex) {
        index.maximumLastLineTree[
            index.treeLeafCount + annotationIndex] =
            index.annotations.at(annotationIndex).range.lastLine;
    }
    for (int treeIndex = index.treeLeafCount - 1;
         treeIndex > 0;
         --treeIndex) {
        index.maximumLastLineTree[treeIndex] = qMax(
            index.maximumLastLineTree.at(treeIndex * 2),
            index.maximumLastLineTree.at(treeIndex * 2 + 1));
    }
    annotationsBySource.insert(sourceId, std::move(index));
}

void AnnotationLayer::removeSource(const QString& sourceId)
{
    annotationsBySource.remove(sourceId);
}

void AnnotationLayer::clear()
{
    annotationsBySource.clear();
}

QStringList AnnotationLayer::sourceIds() const
{
    QStringList result = annotationsBySource.keys();
    std::sort(result.begin(), result.end());
    return result;
}

AnnotationLayerReport AnnotationLayer::resolve(
    const AnnotationLayerQuery& query) const
{
    AnnotationLayerReport report;
    const int firstVisible = qMax(0, query.firstVisibleLine);
    const int lastVisible = qMax(firstVisible, query.lastVisibleLine);
    const int densityLimit = qMax(1, query.maxAnnotationsPerLine);
    const int laneLimit = qMax(1, query.maxLanes);

    QHash<QString, EditorAnnotation> unique;
    const QStringList sources = sourceIds();
    for (const QString& source : sources) {
        const auto sourceIt =
            annotationsBySource.constFind(source);
        if (sourceIt == annotationsBySource.constEnd())
            continue;
        const SourceIndex& sourceIndex = sourceIt.value();
        report.inputCount += sourceIndex.inputCount;
        const QList<EditorAnnotation>& sourceAnnotations =
            sourceIndex.annotations;
        if (sourceAnnotations.isEmpty())
            continue;

        const auto upperIt = std::upper_bound(
            sourceAnnotations.cbegin(),
            sourceAnnotations.cend(),
            lastVisible,
            [](int line, const EditorAnnotation& annotation) {
                return line < annotation.range.firstLine;
            });
        const int upper = static_cast<int>(
            std::distance(sourceAnnotations.cbegin(), upperIt));
        const QList<int> visibleIndices =
            overlappingAnnotationIndices(
                sourceIndex.maximumLastLineTree,
                sourceIndex.treeLeafCount,
                upper,
                firstVisible);
        int visibleCount = 0;
        for (const int annotationIndex : visibleIndices) {
            ++report.examinedCount;
            EditorAnnotation annotation =
                sourceAnnotations.at(annotationIndex);
            if (annotation.range.lastLine < firstVisible)
                continue;
            ++visibleCount;
            ++report.candidateCount;
            if (annotation.priority <= 0) {
                annotation.priority =
                    defaultPriority(annotation.kind);
            }

            const QString key = deduplicationKey(annotation);
            const auto existing = unique.constFind(key);
            if (existing == unique.constEnd()) {
                unique.insert(key, annotation);
                continue;
            }
            ++report.duplicateCount;
            if (annotation.priority > existing->priority
                || (annotation.priority == existing->priority
                    && annotation.sourceGeneration
                           > existing->sourceGeneration)) {
                unique.insert(key, annotation);
            }
        }
        report.offscreenCount +=
            sourceAnnotations.size() - visibleCount;
    }

    QList<EditorAnnotation> ordered = unique.values();
    std::sort(ordered.begin(), ordered.end(), annotationOrder);

    QHash<int, int> countByLine;
    QHash<QString, QList<EditorAnnotationRange>> occupied;
    for (const EditorAnnotation& annotation : ordered) {
        const int line = annotation.range.firstLine;
        if (countByLine.value(line) >= densityLimit) {
            ++report.densitySuppressedCount;
            continue;
        }

        const EditorAnnotationRange candidate =
            occupiedRange(annotation);
        int selectedLane = -1;
        for (int lane = 0; lane < laneLimit; ++lane) {
            const QString key = occupancyKey(annotation, lane);
            bool conflict = false;
            for (const EditorAnnotationRange& range :
                 occupied.value(key)) {
                if (candidate.overlaps(range)) {
                    conflict = true;
                    break;
                }
            }
            if (!conflict) {
                selectedLane = lane;
                occupied[key].append(candidate);
                break;
            }
        }
        if (selectedLane < 0) {
            ++report.conflictSuppressedCount;
            continue;
        }

        report.annotations.append(
            ResolvedEditorAnnotation{annotation, selectedLane});
        countByLine[line] = countByLine.value(line) + 1;
    }
    return report;
}

int AnnotationLayer::defaultPriority(EditorAnnotationKind kind)
{
    switch (kind) {
    case EditorAnnotationKind::Diagnostic:
        return 600;
    case EditorAnnotationKind::TemplateSlot:
        return 500;
    case EditorAnnotationKind::ColumnCaret:
        return 450;
    case EditorAnnotationKind::KeywordGhost:
        return 400;
    case EditorAnnotationKind::PortDefinition:
        return 300;
    case EditorAnnotationKind::EffectiveValue:
        return 200;
    }
    return 1;
}
