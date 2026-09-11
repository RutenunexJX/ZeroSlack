#include "workspaceedittransactionservice.h"

#include <utility>

WorkspaceEditTransactionService*
WorkspaceEditTransactionService::getInstance()
{
    static WorkspaceEditTransactionService service;
    return &service;
}

rtledit::PreparedWorkspaceEditTransaction
WorkspaceEditTransactionService::prepare(
    rtledit::WorkspaceEditPlan plan,
    rtledit::SemanticIndexSnapshot currentSemanticSnapshot,
    const rtledit::WorkspaceDocumentManager& documents,
    bool dryRun) const
{
    return transactions.prepare(
        std::move(plan),
        std::move(currentSemanticSnapshot),
        documents,
        dryRun);
}

rtledit::WorkspaceEditTransactionResult
WorkspaceEditTransactionService::applyConfirmed(
    rtledit::PreparedWorkspaceEditTransaction prepared,
    rtledit::SemanticIndexSnapshot currentSemanticSnapshot,
    rtledit::WorkspaceDocumentManager& documents)
{
    if (!transactions.confirmPreview(&prepared)) {
        rtledit::WorkspaceEditTransactionResult result;
        result.status =
            rtledit::TransactionStatus::InvalidPreparation;
        result.message =
            "Workspace edit preview is not ready for confirmation.";
        return result;
    }
    rtledit::WorkspaceEditTransactionResult result =
        transactions.apply(
        prepared,
        std::move(currentSemanticSnapshot),
        documents);
    if (result.status
        == rtledit::TransactionStatus::Applied) {
        ++generation;
    }
    return result;
}

rtledit::WorkspaceEditTransactionResult
WorkspaceEditTransactionService::undo(
    rtledit::WorkspaceDocumentManager& documents)
{
    rtledit::WorkspaceEditTransactionResult result =
        transactions.undo(documents);
    if (result.status
        == rtledit::TransactionStatus::Undone) {
        ++generation;
    }
    return result;
}

rtledit::WorkspaceEditTransactionResult
WorkspaceEditTransactionService::redo(
    rtledit::WorkspaceDocumentManager& documents)
{
    rtledit::WorkspaceEditTransactionResult result =
        transactions.redo(documents);
    if (result.status
        == rtledit::TransactionStatus::Redone) {
        ++generation;
    }
    return result;
}

bool WorkspaceEditTransactionService::canUndo() const
{
    return transactions.canUndo();
}

bool WorkspaceEditTransactionService::canRedo() const
{
    return transactions.canRedo();
}

std::uint64_t
WorkspaceEditTransactionService::historyGeneration() const
{
    return generation;
}

void WorkspaceEditTransactionService::clearHistory()
{
    transactions.clearHistory();
    ++generation;
}
