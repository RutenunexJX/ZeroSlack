#pragma once

#include <string>

#include "rtledit/core_types.h"

namespace rtledit {

enum class SemanticObjectKind {
    Unknown,
    Port,
    Signal
};

struct SemanticObjectId {
    SemanticObjectKind kind = SemanticObjectKind::Unknown;
    std::string qualifiedName;
    std::string ownerScope;
    std::string filePath;
    SourceRange range;
    std::string signatureHash;
};

struct SemanticIndexSnapshot {
    std::string id;

    bool empty() const {
        return id.empty();
    }
};

}  // namespace rtledit
