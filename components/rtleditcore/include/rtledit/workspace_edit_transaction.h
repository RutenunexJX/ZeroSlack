#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "rtledit/edit_plan.h"
#include "rtledit/workspace_document_manager.h"

namespace rtledit {

enum class TransactionPrepareStatus {
    Ready,
    InvalidPlan,
    Stale,
    DiffFailed
};

struct PreparedWorkspaceEditTransaction {
    TransactionPrepareStatus status = TransactionPrepareStatus::InvalidPlan;
    WorkspaceEditPlan plan;
    WorkspaceEditPreview preview;
    WorkspaceEditSourceDiff sourceDiff;
    bool dryRun = false;
    bool previewConfirmed = false;

    bool ready() const {
        return status == TransactionPrepareStatus::Ready &&
            preview.built() && sourceDiff.built();
    }
};

enum class TransactionStatus {
    Applied,
    DryRunOnly,
    InvalidPreparation,
    PreviewRequired,
    Stale,
    ApplyFailed,
    Undone,
    Redone,
    Conflict,
    RestoreFailed,
    NothingToUndo,
    NothingToRedo
};

const char* transactionStatusName(TransactionStatus status);

struct WorkspaceEditTransactionResult {
    TransactionStatus status = TransactionStatus::InvalidPreparation;
    PlanApplyResult applyResult;
    std::string message;
    std::vector<std::string> changedFiles;
    std::vector<std::string> residualFiles;

    bool succeeded() const {
        return status == TransactionStatus::Applied ||
            status == TransactionStatus::DryRunOnly ||
            status == TransactionStatus::Undone ||
            status == TransactionStatus::Redone;
    }
};

class WorkspaceEditTransactionCoordinator {
public:
    explicit WorkspaceEditTransactionCoordinator(
        std::size_t maximumHistoryEntries = 32);

    PreparedWorkspaceEditTransaction prepare(
        WorkspaceEditPlan plan,
        const WorkspaceDocumentManager& documents,
        bool dryRun = false) const;
    PreparedWorkspaceEditTransaction prepare(
        WorkspaceEditPlan plan,
        SemanticIndexSnapshot currentSemanticSnapshot,
        const WorkspaceDocumentManager& documents,
        bool dryRun = false) const;

    bool confirmPreview(PreparedWorkspaceEditTransaction* prepared) const;

    WorkspaceEditTransactionResult apply(
        const PreparedWorkspaceEditTransaction& prepared,
        WorkspaceDocumentManager& documents);
    WorkspaceEditTransactionResult apply(
        const PreparedWorkspaceEditTransaction& prepared,
        SemanticIndexSnapshot currentSemanticSnapshot,
        WorkspaceDocumentManager& documents);

    bool canUndo() const;
    bool canRedo() const;
    std::size_t undoDepth() const;
    std::size_t redoDepth() const;
    WorkspaceEditTransactionResult undo(
        WorkspaceDocumentManager& documents);
    WorkspaceEditTransactionResult redo(
        WorkspaceDocumentManager& documents);
    void clearHistory();

private:
    struct DocumentState {
        std::string filePath;
        WorkspaceDocumentSnapshot snapshot;
    };

    struct HistoryEntry {
        std::vector<DocumentState> before;
        std::vector<DocumentState> after;
        std::vector<DocumentState> expectedCurrent;
    };

    struct RestoreResult {
        TransactionStatus status = TransactionStatus::RestoreFailed;
        std::string message;
        std::vector<std::string> changedFiles;
        std::vector<std::string> residualFiles;
        std::vector<DocumentState> resultingState;

        bool restored() const {
            return status == TransactionStatus::Undone ||
                status == TransactionStatus::Redone;
        }
    };

    std::size_t historyLimit = 32;
    std::vector<HistoryEntry> undoEntries;
    std::vector<HistoryEntry> redoEntries;

    static std::vector<DocumentState> captureStates(
        const std::vector<DocumentBaseline>& baselines,
        const WorkspaceDocumentManager& documents);
    static std::vector<DocumentState> captureStates(
        const std::vector<DocumentState>& requested,
        const WorkspaceDocumentManager& documents);
    static std::vector<std::string> filePaths(
        const std::vector<DocumentState>& states);
    static RestoreResult restoreAtomically(
        const std::vector<DocumentState>& expectedCurrent,
        const std::vector<DocumentState>& target,
        TransactionStatus successStatus,
        WorkspaceDocumentManager& documents);
    static WorkspaceEditTransactionResult applyPrepared(
        const PreparedWorkspaceEditTransaction& prepared,
        const SemanticIndexSnapshot* currentSemanticSnapshot,
        WorkspaceDocumentManager& documents,
        HistoryEntry* historyEntry);
    void pushUndo(HistoryEntry entry);
};

}  // namespace rtledit
