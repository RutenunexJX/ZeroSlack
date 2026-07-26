#include "rtledit/text_edit.h"

#include <algorithm>

namespace rtledit {

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

    std::vector<WorkspaceTextEdit> ordered;
    for (std::size_t i = 0; i < indexedEdits.size();) {
        const auto& current = indexedEdits[i];
        const bool currentZero = current.offsets.start == current.offsets.end;

        if (!currentZero) {
            ordered.push_back(current.edit);
            ++i;
            continue;
        }

        WorkspaceTextEdit merged = current.edit;
        ++i;

        while (i < indexedEdits.size() &&
               indexedEdits[i].offsets.start == current.offsets.start &&
               indexedEdits[i].offsets.end == current.offsets.end) {
            merged.newText += indexedEdits[i].edit.newText;
            ++i;
        }

        ordered.push_back(std::move(merged));
    }

    return ordered;
}

std::optional<std::string> applyTextEditsToString(
    std::string text,
    const std::vector<WorkspaceTextEdit>& editsInApplicationOrder) {
    for (const auto& edit : editsInApplicationOrder) {
        const auto offsets = rangeToOffsets(text, edit.range);
        if (!offsets) {
            return std::nullopt;
        }
        text.replace(offsets->start, offsets->end - offsets->start, edit.newText);
    }

    return text;
}

}  // namespace rtledit
