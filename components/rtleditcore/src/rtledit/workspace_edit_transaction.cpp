#include "rtledit/workspace_edit_transaction.h"

#include <algorithm>
#include <map>
#include <sstream>
#include <utility>

namespace rtledit {
namespace {

class PreviewSnapshotCache final : public WorkspaceDocumentManager {
public:
    explicit PreviewSnapshotCache(const WorkspaceDocumentManager& source) : source(source) {}
    std::optional<WorkspaceDocumentSnapshot> snapshot(const std::string& path) const override {
        const auto found = values.find(path);
        if (found != values.end()) return found->second;
        return values.emplace(path, source.snapshot(path)).first->second;
    }
    rtledit::DocumentMutationResult applyTextEdits(const std::string&, DocumentVersion, const std::vector<WorkspaceTextEdit>&) override { return {}; }
    rtledit::DocumentMutationResult restoreSnapshot(const std::string&, const rtledit::WorkspaceDocumentSnapshot&,
        const rtledit::WorkspaceDocumentSnapshot&) override { return {}; }
private:
    const WorkspaceDocumentManager& source;
    mutable std::map<std::string, std::optional<WorkspaceDocumentSnapshot>> values;
};

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

bool PreparedWorkspaceEditTransaction::matchesPreviewedPlan() const {
    return previewedPlan && dryRun == previewedDryRun
        && sameWorkspaceEditPlan(plan, *previewedPlan);
}

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
    const PreviewSnapshotCache snapshots(documents);
    PreparedWorkspaceEditTransaction prepared;
    prepared.plan = std::move(plan);
    prepared.preview =
        buildWorkspaceEditPreview(prepared.plan, snapshots);
    prepared.sourceDiff =
        buildWorkspaceEditSourceDiff(prepared.plan, snapshots);
    prepared.status =
        prepareStatus(prepared.preview, prepared.sourceDiff);
    prepared.dryRun = dryRun;
    prepared.previewedPlan = std::make_shared<const WorkspaceEditPlan>(prepared.plan);
    prepared.previewedDryRun = dryRun;
    return prepared;
}

PreparedWorkspaceEditTransaction
WorkspaceEditTransactionCoordinator::prepare(
    WorkspaceEditPlan plan,
    SemanticIndexSnapshot currentSemanticSnapshot,
    const WorkspaceDocumentManager& documents,
    bool dryRun) const {
    const PreviewSnapshotCache snapshots(documents);
    PreparedWorkspaceEditTransaction prepared;
    prepared.plan = std::move(plan);
    prepared.preview = buildWorkspaceEditPreview(
        prepared.plan, currentSemanticSnapshot, snapshots);
    prepared.sourceDiff = buildWorkspaceEditSourceDiff(
        prepared.plan, std::move(currentSemanticSnapshot), snapshots);
    prepared.status =
        prepareStatus(prepared.preview, prepared.sourceDiff);
    prepared.dryRun = dryRun;
    prepared.previewedPlan = std::make_shared<const WorkspaceEditPlan>(prepared.plan);
    prepared.previewedDryRun = dryRun;
    return prepared;
}

bool WorkspaceEditTransactionCoordinator::confirmPreview(
    PreparedWorkspaceEditTransaction* prepared) const {
    if (!prepared || !prepared->ready() || !prepared->matchesPreviewedPlan())
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
    if (!restored.restored()) {
        rebaseHistory(entry.expectedCurrent, restored.resultingState);
        return result;
    }
    rebaseHistory(entry.before, restored.resultingState);
    entry = undoEntries.back();

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
    if (!restored.restored()) {
        rebaseHistory(entry.expectedCurrent, restored.resultingState);
        return result;
    }
    rebaseHistory(entry.after, restored.resultingState);
    entry = redoEntries.back();

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

bool WorkspaceEditTransactionCoordinator::undoMatches(
    const WorkspaceDocumentManager& documents) const {
    if (undoEntries.empty()) return false;
    for (const auto& state : undoEntries.back().expectedCurrent) {
        const auto current = documents.snapshot(state.filePath);
        if (!current || !sameSnapshot(*current, state.snapshot)) return false;
    }
    return true;
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

    std::vector<DocumentMutationReceipt> changed;
    const auto rollback = [&] {
        RestoreResult failed;
        std::map<std::string, WorkspaceDocumentSnapshot> verifiedRestorations;
        for (auto it = changed.rbegin(); it != changed.rend(); ++it) {
            const auto restored = documents.restoreSnapshot(it->filePath, it->after, it->before);
            const auto actual = restored && restored.ownedState ? documents.snapshot(it->filePath) : std::nullopt;
            if (!actual || !sameSnapshot(*actual, *restored.ownedState) || actual->text != it->before.text)
                failed.residualFiles.push_back(it->filePath);
            else verifiedRestorations[it->filePath] = *actual;
        }
        // Unmodified rejections are never restored. Rebase history only when
        // every current state is the original or a verified owned restoration.
        auto actual = captureStates(expectedCurrent, documents);
        if (actual.size() == expectedCurrent.size() && failed.residualFiles.empty()) {
            bool verified = true;
            for (std::size_t i = 0; i < actual.size(); ++i) {
                const auto restored = verifiedRestorations.find(actual[i].filePath);
                const auto& expected = restored == verifiedRestorations.end()
                    ? expectedCurrent[i].snapshot : restored->second;
                if (!sameSnapshot(actual[i].snapshot, expected))
                    verified = false;
            }
            if (verified) failed.resultingState = std::move(actual);
        }
        failed.status = TransactionStatus::RestoreFailed;
        failed.message = !failed.resultingState.empty()
            ? "Atomic restore failed; owned changes rolled back; history retained for retry."
            : "Atomic restore failed; external conflicts or residual changes require review.";
        return failed;
    };
    for (std::size_t index = 0; index < target.size(); ++index) {
        const auto mutation = documents.restoreSnapshot(target[index].filePath,
            expectedCurrent[index].snapshot, target[index].snapshot);
        if (mutation.modified && mutation.ownedState)
            changed.push_back({target[index].filePath, expectedCurrent[index].snapshot, *mutation.ownedState});
        if (!mutation || !mutation.ownedState || mutation.ownedState->text != target[index].snapshot.text) {
            auto failed = rollback();
            if (mutation.modified && !mutation.ownedState) failed.residualFiles.push_back(target[index].filePath);
            const auto current = documents.snapshot(target[index].filePath);
            if (!mutation.modified && (!current || !sameSnapshot(*current, expectedCurrent[index].snapshot)))
                failed.status = TransactionStatus::Conflict;
            return failed;
        }
        result.resultingState.push_back({target[index].filePath, *mutation.ownedState});
    }
    for (const auto& state : result.resultingState) {
        const auto actual = documents.snapshot(state.filePath);
        if (!actual || !sameSnapshot(*actual, state.snapshot)) return rollback();
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
    if (!prepared.matchesPreviewedPlan()) {
        return simpleResult(TransactionStatus::InvalidPreparation,
            "The edit plan changed after preview; prepare a new preview.");
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
        for (const auto& restored : applyResult.patchResult.rolledBackChanges)
            rebaseHistory({{restored.filePath, restored.before}}, {{restored.filePath, restored.after}});
        WorkspaceEditTransactionResult result;
        result.status =
            applyResult.status == PlanApplyStatus::Stale
            ? TransactionStatus::Stale
            : TransactionStatus::ApplyFailed;
        result.message =
            renderWorkspaceEditPlanApplyResult(applyResult);
        result.residualFiles = applyResult.patchResult.changedFiles;
        result.applyResult = std::move(applyResult);
        return result;
    }

    // The adapter returns the state it produced. A later capture could include
    // an external generation and must never grant this transaction ownership.
    if (historyEntry) {
        for (const auto& receipt : applyResult.patchResult.documentChanges) {
            historyEntry->before.push_back({receipt.filePath, receipt.before});
            historyEntry->after.push_back({receipt.filePath, receipt.after});
        }
        historyEntry->expectedCurrent = historyEntry->after;
    }
    WorkspaceEditTransactionResult result;
    result.status = TransactionStatus::Applied;
    result.message =
        "Applied one atomic workspace edit transaction.";
    result.changedFiles = applyResult.patchResult.changedFiles;
    result.applyResult = std::move(applyResult);
    return result;
}

void WorkspaceEditTransactionCoordinator::rebaseHistory(
    const std::vector<DocumentState>& previous,
    const std::vector<DocumentState>& restored) {
    if (previous.size() != restored.size()) return;
    for (std::size_t i = 0; i < previous.size(); ++i)
        if (previous[i].filePath != restored[i].filePath
            || previous[i].snapshot.text != restored[i].snapshot.text) return;
    const auto advance = [&](std::vector<DocumentState>& states) {
        for (auto& state : states)
            for (std::size_t i = 0; i < previous.size(); ++i)
                if (state.filePath == previous[i].filePath && sameSnapshot(state.snapshot, previous[i].snapshot)) {
                    state.snapshot = restored[i].snapshot;
                    break;
                }
    };
    // before/after carry the same state identities as expectedCurrent. Advance
    // all three so future redo chains can recognize this exact restored state.
    for (auto* stack : {&undoEntries, &redoEntries})
        for (auto& entry : *stack) {
            advance(entry.before); advance(entry.after); advance(entry.expectedCurrent);
        }
}

void WorkspaceEditTransactionCoordinator::pushUndo(
    HistoryEntry entry) {
    undoEntries.push_back(std::move(entry));
    if (undoEntries.size() > historyLimit)
        undoEntries.erase(undoEntries.begin());
    redoEntries.clear();
}

}  // namespace rtledit
