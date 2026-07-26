#pragma once

#include <optional>
#include <string>
#include <vector>

#include "rtledit/core_types.h"

namespace rtledit {

struct WorkspaceDocumentSnapshot {
    DocumentVersion version;
    std::string text;
};

class WorkspaceDocumentManager {
public:
    virtual ~WorkspaceDocumentManager() = default;

    virtual std::optional<WorkspaceDocumentSnapshot> snapshot(
        const std::string& filePath) const = 0;

    virtual bool applyTextEdits(
        const std::string& filePath,
        DocumentVersion expectedVersion,
        const std::vector<WorkspaceTextEdit>& editsInApplicationOrder) = 0;

    virtual bool restoreSnapshot(
        const std::string& filePath,
        const WorkspaceDocumentSnapshot& snapshot) = 0;
};

}  // namespace rtledit
