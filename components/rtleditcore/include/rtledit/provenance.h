#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "rtledit/core_types.h"

namespace rtledit {

enum class AnchorResolutionSource {
    Unspecified,
    TreeSitter
};

inline const char* anchorResolutionSourceName(AnchorResolutionSource source) {
    switch (source) {
    case AnchorResolutionSource::Unspecified:
        return "unspecified";
    case AnchorResolutionSource::TreeSitter:
        return "tree_sitter";
    }

    return "unknown";
}

struct AnchorProvenance {
    AnchorResolutionSource source = AnchorResolutionSource::Unspecified;
    std::string resolver;
    std::string semanticSnapshotId;
};

struct TextEditProvenance {
    std::size_t editIndex = 0;
    std::string actionId;
    std::string anchorName;
    std::string description;
    AnchorProvenance anchor;
    std::string signalQualifiedName;
    std::string sourceInstancePath;
    std::optional<std::size_t> hierarchyStepIndex;
    std::string sourceFilePath;
    SourceRange sourceRange;
};

}  // namespace rtledit
