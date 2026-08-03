#include "rtledit/workspace_edit_transaction.h"

#include <algorithm>
#include <sstream>
#include <utility>

namespace rtledit {
namespace {

bool sameSnapshot(const WorkspaceDocumentSnapshot& left,
                  const WorkspaceDocumentSnapshot& right) {
    return left.version == right.version && left.text == right.text;
}

TransactionPrepareStatus prepareStatus(
    const WorkspaceEditPreview& preview,
    const WorkspaceEditSourceDiff& diff) {
    if (preview.status == PreviewStatus::InvalidPlan ||
        diff.status == SourceDiffStatus::InvalidPlan) {
        return TransactionPrepareStatus::InvalidPlan;
    }
    if (preview.status == PreviewStatus::Stale ||
        diff.status == SourceDiffStatus::Stale) {
        return TransactionPrepareStatus::Stale;
    }
    if (!diff.built()) {
        return TransactionPrepareStatus::DiffFailed;
    }
    return preview.built() ? TransactionPrepareStatus::Ready
                           : TransactionPrepareStatus::InvalidPlan;
}

WorkspaceEditTransactionResult simpleResult(
    TransactionStatus status,
    std::string message) {
    WorkspaceEditTransactionResult result;
    result.status = status;
    result.message = std::move(message);
    return result;
}

}  // namespace

const char* transactionStatusName(TransactionStatus status) {
    switch (status) {
    case TransactionStatus::Applied:
        return "applied";
    case TransactionStatus::DryRunOnly:
        return "dry-run";
    case TransactionStatus::InvalidPreparation:
        return "invalid-preparation";
    case TransactionStatus::PreviewRequired:
        return "preview-required";
    case TransactionStatus::Stale:
        return "stale";
    case TransactionStatus::ApplyFailed:
        return "apply-failed";
    case TransactionStatus::Undone:
        return "undone";
    case TransactionStatus::Redone:
        return "redone";
    case TransactionStatus::Conflict:
        return "conflict";
    case TransactionStatus::RestoreFailed:
        return "restore-failed";
    case TransactionStatus::NothingToUndo:
        return "nothing-to-undo";
    case TransactionStatus::NothingToRedo:
        return "nothing-to-redo";
    }
    return "unknown";
}

WorkspaceEditTransactionCoordinator::WorkspaceEditTransactionCoordinator(
    std::size_t maximumHistoryEntries)
    : historyLimit(std::max<std::size_t>(1, maximumHistoryEntries)) {
}

PreparedWorkspaceEditTransaction
WorkspaceEditTransactionCoordinator::prepare(
    WorkspaceEditPlan plan,
    const WorkspaceDocumentManager& documents,
    bool dryRun) const {
    PreparedWorkspaceEditTransaction prepared;
    prepared.plan = std::move(plan);
    prepared.preview =
        buildWorkspaceEditPreview(prepared.plan, documents);
    prepared.sourceDiff =
        buildWorkspaceEditSourceDiff(prepared.plan, documents);
    prepared.status =
        prepareStatus(prepared.preview, prepared.sourceDiff);
    prepared.dryRun = dryRun;
    return prepared;
}

PreparedWorkspaceEditTransaction
WorkspaceEditTransactionCoordinator::prepare(
    WorkspaceEditPlan plan,
    SemanticIndexSnapshot currentSemanticSnapshot,
    const WorkspaceDocumentManager& documents,
    bool dryRun) const {
    PreparedWorkspaceEditTransaction prepared;
    prepared.plan = std::move(plan);
    prepared.preview = buildWorkspaceEditPreview(
        prepared.plan, currentSemanticSnapshot, documents);
    prepared.sourceDiff = buildWorkspaceEditSourceDiff(
        prepared.plan, std::move(currentSemanticSnapshot), documents);
    prepared.status =
        prepareStatus(prepared.preview, prepared.sourceDiff);
    prepared.dryRun = dryRun;
    return prepared;
}

bool WorkspaceEditTransactionCoordinator::confirmPreview(
    PreparedWorkspaceEditTransaction* prepared) const {
    if (!prepared || !prepared->ready())
        return false;
    prepared->previewConfirmed = true;
    return true;
}

WorkspaceEditTransactionResult
WorkspaceEditTransactionCoordinator::apply(
    const PreparedWorkspaceEditTransaction& prepared,
    WorkspaceDocumentManager& documents) {
    HistoryEntry history;
    WorkspaceEditTransactionResult result =
        applyPrepared(prepared, nullptr, documents, &history);
    if (result.status == TransactionStatus::Applied)
        pushUndo(std::move(history));
    return result;
}

WorkspaceEditTransactionResult
WorkspaceEditTransactionCoordinator::apply(
    const PreparedWorkspaceEditTransaction& prepared,
    SemanticIndexSnapshot currentSemanticSnapshot,
    WorkspaceDocumentManager& documents) {
    HistoryEntry history;
    WorkspaceEditTransactionResult result = applyPrepared(
        prepared, &currentSemanticSnapshot, documents, &history);
    if (result.status == TransactionStatus::Applied)
        pushUndo(std::move(history));
    return result;
}

bool WorkspaceEditTransactionCoordinator::canUndo() const {
    return !undoEntries.empty();
}

bool WorkspaceEditTransactionCoordinator::canRedo() const {
    return !redoEntries.empty();
}

std::size_t WorkspaceEditTransactionCoordinator::undoDepth() const {
    return undoEntries.size();
}

std::size_t WorkspaceEditTransactionCoordinator::redoDepth() const {
    return redoEntries.size();
}

WorkspaceEditTransactionResult
WorkspaceEditTransactionCoordinator::undo(
    WorkspaceDocumentManager& documents) {
    if (undoEntries.empty()) {
        return simpleResult(
            TransactionStatus::NothingToUndo,
            "No workspace edit transaction is available to undo.");
    }

    HistoryEntry entry = undoEntries.back();
    const RestoreResult restored = restoreAtomically(
        entry.expectedCurrent,
        entry.before,
        TransactionStatus::Undone,
        documents);
    WorkspaceEditTransactionResult result;
    result.status = restored.status;
    result.message = restored.message;
    result.changedFiles = restored.changedFiles;
    result.residualFiles = restored.residualFiles;
    if (!restored.restored())
        return result;

    undoEntries.pop_back();
    entry.expectedCurrent = restored.resultingState;
    redoEntries.push_back(std::move(entry));
    return result;
}

WorkspaceEditTransactionResult
WorkspaceEditTransactionCoordinator::redo(
    WorkspaceDocumentManager& documents) {
    if (redoEntries.empty()) {
        return simpleResult(
            TransactionStatus::NothingToRedo,
            "No workspace edit transaction is available to redo.");
    }

    HistoryEntry entry = redoEntries.back();
    const RestoreResult restored = restoreAtomically(
        entry.expectedCurrent,
        entry.after,
        TransactionStatus::Redone,
        documents);
    WorkspaceEditTransactionResult result;
    result.status = restored.status;
    result.message = restored.message;
    result.changedFiles = restored.changedFiles;
    result.residualFiles = restored.residualFiles;
    if (!restored.restored())
        return result;

    redoEntries.pop_back();
    entry.expectedCurrent = restored.resultingState;
    undoEntries.push_back(std::move(entry));
    return result;
}

void WorkspaceEditTransactionCoordinator::clearHistory() {
    undoEntries.clear();
    redoEntries.clear();
}

std::vector<WorkspaceEditTransactionCoordinator::DocumentState>
WorkspaceEditTransactionCoordinator::captureStates(
    const std::vector<DocumentBaseline>& baselines,
    const WorkspaceDocumentManager& documents) {
    std::vector<DocumentState> states;
    states.reserve(baselines.size());
    for (const auto& baseline : baselines) {
        const auto snapshot = documents.snapshot(baseline.filePath);
        if (!snapshot)
            return {};
        states.push_back(DocumentState{baseline.filePath, *snapshot});
    }
    return states;
}

std::vector<WorkspaceEditTransactionCoordinator::DocumentState>
WorkspaceEditTransactionCoordinator::captureStates(
    const std::vector<DocumentState>& requested,
    const WorkspaceDocumentManager& documents) {
    std::vector<DocumentState> states;
    states.reserve(requested.size());
    for (const auto& request : requested) {
        const auto snapshot = documents.snapshot(request.filePath);
        if (!snapshot)
            return {};
        states.push_back(DocumentState{request.filePath, *snapshot});
    }
    return states;
}

std::vector<std::string>
WorkspaceEditTransactionCoordinator::filePaths(
    const std::vector<DocumentState>& states) {
    std::vector<std::string> result;
    result.reserve(states.size());
    for (const auto& state : states)
        result.push_back(state.filePath);
    return result;
}

WorkspaceEditTransactionCoordinator::RestoreResult
WorkspaceEditTransactionCoordinator::restoreAtomically(
    const std::vector<DocumentState>& expectedCurrent,
    const std::vector<DocumentState>& target,
    TransactionStatus successStatus,
    WorkspaceDocumentManager& documents) {
    RestoreResult result;
    if (expectedCurrent.size() != target.size()) {
        result.message =
            "Workspace transaction history has inconsistent document sets.";
        return result;
    }

    for (std::size_t index = 0; index < expectedCurrent.size(); ++index) {
        if (expectedCurrent[index].filePath != target[index].filePath) {
            result.message =
                "Workspace transaction history has mismatched file order.";
            return result;
        }
        const auto current =
            documents.snapshot(expectedCurrent[index].filePath);
        if (!current ||
            !sameSnapshot(*current, expectedCurrent[index].snapshot)) {
            result.status = TransactionStatus::Conflict;
            result.message =
                "Document changed after the workspace transaction: " +
                expectedCurrent[index].filePath;
            return result;
        }
    }

    std::size_t restoredCount = 0;
    for (; restoredCount < target.size(); ++restoredCount) {
        if (documents.restoreSnapshot(
                target[restoredCount].filePath,
                target[restoredCount].snapshot)) {
            continue;
        }

        std::vector<std::string> residual;
        auto rollback = [&](std::size_t index) {
            if (!documents.restoreSnapshot(
                    expectedCurrent[index].filePath,
                    expectedCurrent[index].snapshot)) {
                residual.push_back(expectedCurrent[index].filePath);
            }
        };
        rollback(restoredCount);
        while (restoredCount > 0) {
            --restoredCount;
            rollback(restoredCount);
        }
        result.status = TransactionStatus::RestoreFailed;
        result.residualFiles = std::move(residual);
        result.message =
            "Atomic workspace transaction restore failed.";
        return result;
    }

    result.resultingState = captureStates(target, documents);
    if (result.resultingState.size() != target.size()) {
        std::vector<std::string> residual;
        for (std::size_t index = expectedCurrent.size(); index-- > 0;) {
            if (!documents.restoreSnapshot(
                    expectedCurrent[index].filePath,
                    expectedCurrent[index].snapshot)) {
                residual.push_back(expectedCurrent[index].filePath);
            }
        }
        result.status = TransactionStatus::RestoreFailed;
        result.residualFiles = std::move(residual);
        result.message = result.residualFiles.empty()
            ? "Unable to verify all restored workspace documents; "
              "restored the transaction's previous state."
            : "Unable to verify all restored workspace documents; "
              "rollback left residual files.";
        return result;
    }
    result.status = successStatus;
    result.changedFiles = filePaths(target);
    result.message = successStatus == TransactionStatus::Undone
        ? "Undid one atomic workspace edit transaction."
        : "Redid one atomic workspace edit transaction.";
    return result;
}

WorkspaceEditTransactionResult
WorkspaceEditTransactionCoordinator::applyPrepared(
    const PreparedWorkspaceEditTransaction& prepared,
    const SemanticIndexSnapshot* currentSemanticSnapshot,
    WorkspaceDocumentManager& documents,
    HistoryEntry* historyEntry) {
    if (!prepared.ready()) {
        return simpleResult(
            prepared.status == TransactionPrepareStatus::Stale
                ? TransactionStatus::Stale
                : TransactionStatus::InvalidPreparation,
            "Workspace edit transaction is not ready.");
    }
    if (prepared.plan.riskLevel == RiskLevel::High &&
        !prepared.previewConfirmed) {
        return simpleResult(
            TransactionStatus::PreviewRequired,
            "High-risk workspace edits require confirmed preview.");
    }
    if (prepared.dryRun) {
        return simpleResult(
            TransactionStatus::DryRunOnly,
            "Dry-run built a plan and source diff without mutation.");
    }

    const std::vector<DocumentState> before =
        captureStates(prepared.plan.baselines, documents);
    if (before.size() != prepared.plan.baselines.size()) {
        return simpleResult(
            TransactionStatus::Stale,
            "A workspace document disappeared before apply.");
    }

    PlanApplyResult applyResult = currentSemanticSnapshot
        ? applyWorkspaceEditPlan(
              prepared.plan, *currentSemanticSnapshot, documents)
        : applyWorkspaceEditPlan(prepared.plan, documents);
    if (!applyResult.applied()) {
        WorkspaceEditTransactionResult result;
        result.status =
            applyResult.status == PlanApplyStatus::Stale
            ? TransactionStatus::Stale
            : TransactionStatus::ApplyFailed;
        result.message =
            renderWorkspaceEditPlanApplyResult(applyResult);
        result.applyResult = std::move(applyResult);
        return result;
    }

    const std::vector<DocumentState> after =
        captureStates(before, documents);
    if (after.size() != before.size()) {
        std::vector<std::string> residual;
        for (const auto& state : before) {
            if (!documents.restoreSnapshot(
                    state.filePath, state.snapshot)) {
                residual.push_back(state.filePath);
            }
        }
        WorkspaceEditTransactionResult result;
        result.status = TransactionStatus::RestoreFailed;
        result.message =
            "Applied workspace edit but could not capture its undo state.";
        result.residualFiles = std::move(residual);
        result.applyResult = std::move(applyResult);
        return result;
    }

    if (historyEntry) {
        historyEntry->before = before;
        historyEntry->after = after;
        historyEntry->expectedCurrent = after;
    }
    WorkspaceEditTransactionResult result;
    result.status = TransactionStatus::Applied;
    result.message =
        "Applied one atomic workspace edit transaction.";
    result.changedFiles = applyResult.patchResult.changedFiles;
    result.applyResult = std::move(applyResult);
    return result;
}

void WorkspaceEditTransactionCoordinator::pushUndo(
    HistoryEntry entry) {
    undoEntries.push_back(std::move(entry));
    if (undoEntries.size() > historyLimit)
        undoEntries.erase(undoEntries.begin());
    redoEntries.clear();
}

}  // namespace rtledit
