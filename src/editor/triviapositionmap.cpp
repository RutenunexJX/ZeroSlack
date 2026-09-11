#include "triviapositionmap.h"

#include "tsdocument.h"

#include <algorithm>
#include <utility>

namespace {
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

bool triviaNode(TSNode node)
{
    const char* rawType = ts_node_type(node);
    const QString type =
        rawType ? QString::fromLatin1(rawType) : QString();
    return type == QLatin1String("one_line_comment")
        || type == QLatin1String("block_comment")
        || type == QLatin1String("comment");
}

void collectSemanticLeaves(const QString& text,
                           TSNode node,
                           QVector<SemanticLeafSpan>* leaves,
                           TriviaPositionLeafPolicy leafPolicy)
{
    if (!leaves || ts_node_is_null(node))
        return;
    const bool comment = triviaNode(node);
    if (comment
        && leafPolicy
               == TriviaPositionLeafPolicy::SemanticOnly) {
        return;
    }
    const uint32_t childCount = ts_node_child_count(node);
    if (childCount > 0 && !comment) {
        for (uint32_t index = 0; index < childCount; ++index) {
            collectSemanticLeaves(text,
                                  ts_node_child(node, index),
                                  leaves,
                                  leafPolicy);
        }
        return;
    }

    const int start =
        static_cast<int>(ts_node_start_byte(node) / 2u);
    const int end =
        static_cast<int>(ts_node_end_byte(node) / 2u);
    if (start < 0 || end <= start || start > text.size())
        return;
    const char* rawType = ts_node_type(node);
    SemanticLeafSpan span;
    span.type =
        rawType ? QString::fromLatin1(rawType) : QString();
    span.start = start;
    span.end = qMin(end, text.size());
    span.text = text.mid(span.start, span.end - span.start);
    leaves->append(std::move(span));
}

QString tokenIdentity(const SemanticLeafSpan& span)
{
    return span.type
        + QChar(0x1f)
        + span.text;
}
}

TriviaPositionMap::TriviaPositionMap(
    const QString& oldText,
    const QString& newText,
    TriviaPositionLeafPolicy leafPolicy)
    : TriviaPositionMap(
          oldText,
          newText,
          SourceTextDelta{
              0,
              static_cast<int>(oldText.size()),
              static_cast<int>(newText.size()),
          },
          leafPolicy)
{
}

TriviaPositionMap::TriviaPositionMap(
    const QString& oldText,
    const QString& newText,
    const SourceTextDelta& fallbackDelta,
    TriviaPositionLeafPolicy leafPolicy)
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
                          &oldLeaves,
                          leafPolicy);
    collectSemanticLeaves(newText,
                          newDocument.rootNode(),
                          &newLeaves,
                          leafPolicy);
    compatible = oldLeaves.size() == newLeaves.size();
    for (int index = 0;
         compatible && index < oldLeaves.size();
         ++index) {
        compatible =
            oldLeaves.at(index).type == newLeaves.at(index).type
            && oldLeaves.at(index).text
                == newLeaves.at(index).text;
    }
}

int TriviaPositionMap::map(int position,
                           bool endAffinity) const
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
    const int previousIndex =
        static_cast<int>(
            std::distance(oldLeaves.cbegin(), upper))
        - 1;
    if (previousIndex >= 0) {
        const SemanticLeafSpan& oldSpan =
            oldLeaves.at(previousIndex);
        const SemanticLeafSpan& newSpan =
            newLeaves.at(previousIndex);
        if (position >= oldSpan.start
            && position < oldSpan.end) {
            return qBound(
                newSpan.start,
                newSpan.start + position - oldSpan.start,
                newSpan.end);
        }
        if (endAffinity && position == oldSpan.end)
            return newSpan.end;
    }

    const int nextIndex = previousIndex + 1;
    if (!endAffinity
        && nextIndex >= 0
        && nextIndex < newLeaves.size()) {
        return newLeaves.at(nextIndex).start;
    }
    if (endAffinity && previousIndex >= 0)
        return newLeaves.at(previousIndex).end;
    if (!newLeaves.isEmpty()) {
        if (position <= oldLeaves.first().start) {
            return qBound(
                0,
                newLeaves.first().start
                    - (oldLeaves.first().start - position),
                newLength);
        }
        return qBound(
            0,
            newLeaves.last().end
                + (position - oldLeaves.last().end),
            newLength);
    }
    return fallbackMappedPosition(position,
                                  fallback,
                                  endAffinity);
}

TriviaLogicalPosition TriviaPositionMap::capture(
    int position,
    bool endAffinity) const
{
    TriviaLogicalPosition logical;
    logical.absolutePosition = position;
    logical.endAffinity = endAffinity;
    if (!compatible || oldLeaves.isEmpty())
        return logical;

    const int bounded = qBound(0, position, oldLength);
    int selectedIndex = -1;
    for (int index = 0; index < oldLeaves.size(); ++index) {
        const SemanticLeafSpan& span = oldLeaves.at(index);
        if (bounded >= span.start && bounded < span.end) {
            selectedIndex = index;
            break;
        }
        if (endAffinity && bounded == span.end) {
            selectedIndex = index;
            break;
        }
        if (!endAffinity && bounded <= span.start) {
            selectedIndex = index;
            break;
        }
        if (endAffinity && bounded < span.start)
            break;
    }
    if (selectedIndex < 0 && endAffinity) {
        for (int index = oldLeaves.size() - 1;
             index >= 0;
             --index) {
            if (oldLeaves.at(index).end <= bounded) {
                selectedIndex = index;
                break;
            }
        }
    }
    if (selectedIndex < 0 && !oldLeaves.isEmpty())
        selectedIndex = oldLeaves.size() - 1;
    if (selectedIndex < 0)
        return logical;

    const SemanticLeafSpan& span = oldLeaves.at(selectedIndex);
    logical.tokenIdentity = tokenIdentity(span);
    logical.tokenOrdinal = selectedIndex;
    logical.tokenOffset =
        qBound(0, bounded - span.start, span.end - span.start);
    return logical;
}

bool TriviaPositionMap::isCompatible() const
{
    return compatible;
}

const QVector<SemanticLeafSpan>&
TriviaPositionMap::oldSemanticLeaves() const
{
    return oldLeaves;
}

const QVector<SemanticLeafSpan>&
TriviaPositionMap::newSemanticLeaves() const
{
    return newLeaves;
}
