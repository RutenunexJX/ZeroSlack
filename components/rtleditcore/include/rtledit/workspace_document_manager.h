#pragma once

#include <optional>
#include <string>
#include <vector>
#include <utility>

#include "rtledit/core_types.h"
#include "rtledit/text_edit.h"

namespace rtledit {

struct DocumentMutationResult {
    bool applied = false;
    bool modified = false;
    std::optional<WorkspaceDocumentSnapshot> ownedState;

    operator bool() const { return applied; }
    static DocumentMutationResult completed(WorkspaceDocumentSnapshot state, bool modified = true) {
        return {true, modified, std::move(state)};
    }
    static DocumentMutationResult failedAfterModification(WorkspaceDocumentSnapshot state) {
        return {false, true, std::move(state)};
    }
};

class WorkspaceDocumentManager {
public:
    virtual ~WorkspaceDocumentManager() = default;

    virtual std::optional<WorkspaceDocumentSnapshot> snapshot(
        const std::string& filePath) const = 0;

    virtual DocumentMutationResult applyTextEdits(
        const std::string& filePath,
        DocumentVersion expectedVersion,
        const std::vector<WorkspaceTextEdit>& editsInApplicationOrder) = 0;

    // Fixed-version byte offsets prepared by the core. Adapters can consume
    // them directly; simple consumers may delegate to their raw-edit entry.
    virtual DocumentMutationResult applyPreparedTextEdits(
        const std::string& filePath,
        const WorkspaceDocumentSnapshot& expected,
        const std::vector<IndexedWorkspaceTextEdit>& edits) {
        std::vector<WorkspaceTextEdit> raw;
        raw.reserve(edits.size());
        for (const auto& edit : edits) raw.push_back(edit.edit);
        return applyTextEdits(filePath, expected.version, raw);
    }

    // Compare immediately before mutation. A stale/readonly rejection must
    // report modified=false. Partial failure returns its own post-state, or
    // no ownedState when reentrant changes make attribution impossible; the
    // latter is a residual to review and never authorizes a blind restore.
    virtual DocumentMutationResult restoreSnapshot(
        const std::string& filePath,
        const WorkspaceDocumentSnapshot& expectedCurrent,
        const WorkspaceDocumentSnapshot& snapshot) = 0;
};

}  // namespace rtledit
