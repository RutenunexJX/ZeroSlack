#pragma once

#include <vector>

#include "rtledit/core_types.h"
#include "rtledit/workspace_document_manager.h"

namespace rtledit {

class PatchEngine {
public:
    explicit PatchEngine(WorkspaceDocumentManager& documentManager);

    ApplyResult apply(const std::vector<WorkspaceTextEdit>& edits);

private:
    WorkspaceDocumentManager& documentManager_;
};

}  // namespace rtledit
