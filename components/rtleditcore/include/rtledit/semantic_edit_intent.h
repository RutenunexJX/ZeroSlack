#pragma once

#include "rtledit/semantic_object.h"

namespace rtledit {

enum class SemanticEditKind {
    ExposeSignalToTop
};

inline constexpr const char* kExposeSignalToTopActionId =
    "signal.exposeToTop";

inline const char* semanticEditActionId(SemanticEditKind) {
    return kExposeSignalToTopActionId;
}

struct SemanticEditIntent {
    SemanticEditKind kind = SemanticEditKind::ExposeSignalToTop;
    SemanticObjectId target;
};

}  // namespace rtledit
