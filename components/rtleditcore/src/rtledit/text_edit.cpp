#include "rtledit/text_edit.h"

#include <algorithm>

namespace rtledit {

TextCoordinateIndex::TextCoordinateIndex(std::string_view source) : text(source), lineStarts{0} {
    for (std::size_t i = 0; i < text.size(); ++i)
        if (text[i] == '\n') lineStarts.push_back(i + 1);
}

std::optional<std::size_t> TextCoordinateIndex::offset(SourcePosition position) const {
    if (position.line >= lineStarts.size()) return std::nullopt;
    const auto start = lineStarts[position.line];
    const auto end = position.line + 1 < lineStarts.size() ? lineStarts[position.line + 1] - 1 : text.size();
    if (position.column > end - start) return std::nullopt;
    return start + position.column;
}

std::optional<TextOffsetRange> TextCoordinateIndex::offsets(const SourceRange& range) const {
    if (range.end < range.start) return std::nullopt;
    const auto start = offset(range.start), end = offset(range.end);
    if (!start || !end) return std::nullopt;
    return TextOffsetRange{*start, *end};
}

std::optional<std::size_t> positionToOffset(
    const std::string& text,
    SourcePosition position) {
    std::size_t line = 0;
    std::size_t column = 0;

    for (std::size_t offset = 0; offset <= text.size(); ++offset) {
        if (line == position.line && column == position.column) {
            return offset;
        }

        if (offset == text.size()) {
            break;
        }

        if (text[offset] == '\n') {
            ++line;
            column = 0;
        } else {
            ++column;
        }
    }

    return std::nullopt;
}

std::optional<TextOffsetRange> rangeToOffsets(
    const std::string& text,
    const SourceRange& range) {
    if (range.end < range.start) {
        return std::nullopt;
    }

    const auto start = positionToOffset(text, range.start);
    const auto end = positionToOffset(text, range.end);
    if (!start || !end || *end < *start) {
        return std::nullopt;
    }

    return TextOffsetRange{*start, *end};
}

bool textEditRangesOverlap(
    const IndexedWorkspaceTextEdit& lhs,
    const IndexedWorkspaceTextEdit& rhs) {
    if (lhs.offsets.start == lhs.offsets.end &&
        rhs.offsets.start == rhs.offsets.end &&
        lhs.offsets.start == rhs.offsets.start) {
        return false;
    }

    return lhs.offsets.start < rhs.offsets.end &&
           rhs.offsets.start < lhs.offsets.end;
}

std::vector<WorkspaceTextEdit> buildTextEditApplicationOrder(
    std::vector<IndexedWorkspaceTextEdit> indexedEdits) {
    auto indexed = buildIndexedTextEditApplicationOrder(std::move(indexedEdits));
    std::vector<WorkspaceTextEdit> result;
    result.reserve(indexed.size());
    for (auto& edit : indexed) result.push_back(std::move(edit.edit));
    return result;
}

std::vector<IndexedWorkspaceTextEdit> buildIndexedTextEditApplicationOrder(
    std::vector<IndexedWorkspaceTextEdit> indexedEdits) {
    std::sort(indexedEdits.begin(), indexedEdits.end(),
        [](const IndexedWorkspaceTextEdit& lhs,
           const IndexedWorkspaceTextEdit& rhs) {
            if (lhs.offsets.start != rhs.offsets.start) {
                return lhs.offsets.start > rhs.offsets.start;
            }

            const bool lhsZero = lhs.offsets.start == lhs.offsets.end;
            const bool rhsZero = rhs.offsets.start == rhs.offsets.end;
            if (lhsZero != rhsZero) {
                return !lhsZero;
            }

            return lhs.inputIndex < rhs.inputIndex;
        });

    std::vector<IndexedWorkspaceTextEdit> ordered;
    ordered.reserve(indexedEdits.size());
    for (std::size_t i = 0; i < indexedEdits.size();) {
        const auto& current = indexedEdits[i];
        const bool currentZero = current.offsets.start == current.offsets.end;

        if (!currentZero) {
            ordered.push_back(current);
            ++i;
            continue;
        }

        IndexedWorkspaceTextEdit merged = current;
        ++i;

        while (i < indexedEdits.size() &&
               indexedEdits[i].offsets.start == current.offsets.start &&
               indexedEdits[i].offsets.end == current.offsets.end) {
            merged.edit.newText += indexedEdits[i].edit.newText;
            ++i;
        }

        ordered.push_back(std::move(merged));
    }

    return ordered;
}

std::optional<std::string> applyTextEditsToString(
    std::string text,
    const std::vector<WorkspaceTextEdit>& editsInApplicationOrder) {
    TextCoordinateIndex index(text);
    std::vector<IndexedWorkspaceTextEdit> indexed;
    indexed.reserve(editsInApplicationOrder.size());
    for (const auto& edit : editsInApplicationOrder) {
        const auto offsets = index.offsets(edit.range);
        if (!offsets) {
            return std::nullopt;
        }
        indexed.push_back({edit, indexed.size(), *offsets});
    }
    return applyIndexedTextEditsToString(std::move(text), indexed);
}

std::optional<std::string> applyIndexedTextEditsToString(
    std::string text, const std::vector<IndexedWorkspaceTextEdit>& edits) {
    auto previousStart = text.size();
    for (const auto& indexed : edits) {
        const auto& offsets = indexed.offsets;
        if (offsets.end > previousStart || offsets.start > offsets.end || offsets.end > text.size()) return std::nullopt;
        text.replace(offsets.start, offsets.end - offsets.start, indexed.edit.newText);
        previousStart = offsets.start;
    }
    return text;
}

}  // namespace rtledit
