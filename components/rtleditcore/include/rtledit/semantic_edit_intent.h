#pragma once

#include "rtledit/semantic_object.h"

namespace rtledit {

enum class SemanticEditKind {
    ExposeSignalToTop,
    ReplaceText
};

inline constexpr const char* kExposeSignalToTopActionId =
    "signal.exposeToTop";
inline constexpr const char* kReplaceTextActionId =
    "edit.replace";

inline const char* semanticEditActionId(SemanticEditKind kind) {
    switch (kind) {
    case SemanticEditKind::ExposeSignalToTop:
        return kExposeSignalToTopActionId;
    case SemanticEditKind::ReplaceText:
        return kReplaceTextActionId;
    }
    return "unknown";
}

struct SemanticEditIntent {
    SemanticEditKind kind = SemanticEditKind::ExposeSignalToTop;
    SemanticObjectId target;
};

}  // namespace rtledit
