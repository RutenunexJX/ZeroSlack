#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace rtledit {

struct SourcePosition {
    std::size_t line = 0;
    std::size_t column = 0;
};

inline bool operator==(const SourcePosition& lhs, const SourcePosition& rhs) {
    return lhs.line == rhs.line && lhs.column == rhs.column;
}

inline bool operator!=(const SourcePosition& lhs, const SourcePosition& rhs) {
    return !(lhs == rhs);
}

inline bool operator<(const SourcePosition& lhs, const SourcePosition& rhs) {
    if (lhs.line != rhs.line) {
        return lhs.line < rhs.line;
    }
    return lhs.column < rhs.column;
}

inline bool operator<=(const SourcePosition& lhs, const SourcePosition& rhs) {
    return lhs < rhs || lhs == rhs;
}

struct SourceRange {
    SourcePosition start;
    SourcePosition end;
};

inline bool isZeroLength(const SourceRange& range) {
    return range.start == range.end;
}

struct DocumentVersion {
    std::uint64_t value = 0;
};

inline bool operator==(DocumentVersion lhs, DocumentVersion rhs) {
    return lhs.value == rhs.value;
}

inline bool operator!=(DocumentVersion lhs, DocumentVersion rhs) {
    return !(lhs == rhs);
}

struct WorkspaceTextEdit {
    std::string filePath;
    DocumentVersion expectedDocumentVersion;
    SourceRange range;
    std::string expectedText;
    std::string newText;
};

enum class ApplyStatus {
    Applied,
    DocumentNotFound,
    MixedDocumentVersions,
    VersionMismatch,
    InvalidRange,
    RangeOutOfBounds,
    RangeTextMismatch,
    OverlappingEdits,
    DocumentApplyFailed
};

inline const char* applyStatusName(ApplyStatus status) {
    switch (status) {
    case ApplyStatus::Applied:
        return "applied";
    case ApplyStatus::DocumentNotFound:
        return "document_not_found";
    case ApplyStatus::MixedDocumentVersions:
        return "mixed_document_versions";
    case ApplyStatus::VersionMismatch:
        return "version_mismatch";
    case ApplyStatus::InvalidRange:
        return "invalid_range";
    case ApplyStatus::RangeOutOfBounds:
        return "range_out_of_bounds";
    case ApplyStatus::RangeTextMismatch:
        return "range_text_mismatch";
    case ApplyStatus::OverlappingEdits:
        return "overlapping_edits";
    case ApplyStatus::DocumentApplyFailed:
        return "document_apply_failed";
    }

    return "unknown";
}

struct ApplyResult {
    ApplyStatus status = ApplyStatus::Applied;
    std::string message;
    std::vector<std::string> changedFiles;

    bool applied() const {
        return status == ApplyStatus::Applied;
    }
};

}  // namespace rtledit
