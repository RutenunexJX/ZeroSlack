#pragma once

#include <optional>
#include <string>
#include <vector>

#include "rtledit/core_types.h"

namespace rtledit {

struct TextOffsetRange {
    std::size_t start = 0;
    std::size_t end = 0;
};

struct IndexedWorkspaceTextEdit {
    WorkspaceTextEdit edit;
    std::size_t inputIndex = 0;
    TextOffsetRange offsets;
};

std::optional<std::size_t> positionToOffset(
    const std::string& text,
    SourcePosition position);

std::optional<TextOffsetRange> rangeToOffsets(
    const std::string& text,
    const SourceRange& range);

bool textEditRangesOverlap(
    const IndexedWorkspaceTextEdit& lhs,
    const IndexedWorkspaceTextEdit& rhs);

std::vector<WorkspaceTextEdit> buildTextEditApplicationOrder(
    std::vector<IndexedWorkspaceTextEdit> indexedEdits);

std::optional<std::string> applyTextEditsToString(
    std::string text,
    const std::vector<WorkspaceTextEdit>& editsInApplicationOrder);

}  // namespace rtledit
