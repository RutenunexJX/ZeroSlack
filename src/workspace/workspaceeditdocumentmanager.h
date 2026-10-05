#ifndef WORKSPACEEDITDOCUMENTMANAGER_H
#define WORKSPACEEDITDOCUMENTMANAGER_H

#include <rtledit/workspace_document_manager.h>

class TabManager;

class WorkspaceEditDocumentManager final
    : public rtledit::WorkspaceDocumentManager
{
public:
    explicit WorkspaceEditDocumentManager(TabManager* tabManager);

    std::optional<rtledit::WorkspaceDocumentSnapshot> snapshot(
        const std::string& filePath) const override;
    rtledit::DocumentMutationResult applyTextEdits(
        const std::string& filePath,
        rtledit::DocumentVersion expectedVersion,
        const std::vector<rtledit::WorkspaceTextEdit>& edits) override;
    rtledit::DocumentMutationResult applyPreparedTextEdits(
        const std::string& filePath,
        const rtledit::WorkspaceDocumentSnapshot& expected,
        const std::vector<rtledit::IndexedWorkspaceTextEdit>& edits) override;
    rtledit::DocumentMutationResult restoreSnapshot(
        const std::string& filePath,
        const rtledit::WorkspaceDocumentSnapshot& expectedCurrent,
        const rtledit::WorkspaceDocumentSnapshot& snapshot) override;

private:
    TabManager* tabs = nullptr;
};

#endif // WORKSPACEEDITDOCUMENTMANAGER_H
