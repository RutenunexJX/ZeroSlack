#ifndef WORKSPACEEDITTRANSACTIONSERVICE_H
#define WORKSPACEEDITTRANSACTIONSERVICE_H

#include <rtledit/workspace_edit_transaction.h>

#include <cstdint>

class WorkspaceEditTransactionService
{
public:
    static WorkspaceEditTransactionService* getInstance();

    rtledit::PreparedWorkspaceEditTransaction prepare(
        rtledit::WorkspaceEditPlan plan,
        rtledit::SemanticIndexSnapshot currentSemanticSnapshot,
        const rtledit::WorkspaceDocumentManager& documents,
        bool dryRun = false) const;
    rtledit::WorkspaceEditTransactionResult applyConfirmed(
        rtledit::PreparedWorkspaceEditTransaction prepared,
        rtledit::SemanticIndexSnapshot currentSemanticSnapshot,
        rtledit::WorkspaceDocumentManager& documents);
    rtledit::WorkspaceEditTransactionResult undo(
        rtledit::WorkspaceDocumentManager& documents);
    rtledit::WorkspaceEditTransactionResult redo(
        rtledit::WorkspaceDocumentManager& documents);
    bool canUndo() const;
    bool canRedo() const;
    std::uint64_t historyGeneration() const;
    void clearHistory();

private:
    rtledit::WorkspaceEditTransactionCoordinator transactions;
    std::uint64_t generation = 0;
};

#endif // WORKSPACEEDITTRANSACTIONSERVICE_H
