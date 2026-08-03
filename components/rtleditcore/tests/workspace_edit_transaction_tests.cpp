#include "rtledit/mock_workspace_document_manager.h"
#include "rtledit/workspace_edit_transaction.h"

#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using rtledit::DocumentVersion;
using rtledit::MockWorkspaceDocumentManager;
using rtledit::PreparedWorkspaceEditTransaction;
using rtledit::PreviewPolicy;
using rtledit::RiskLevel;
using rtledit::SemanticEditIntent;
using rtledit::SourcePosition;
using rtledit::SourceRange;
using rtledit::TransactionStatus;
using rtledit::WorkspaceDocumentManager;
using rtledit::WorkspaceDocumentSnapshot;
using rtledit::WorkspaceEditPlan;
using rtledit::WorkspaceEditTransactionCoordinator;
using rtledit::WorkspaceTextEdit;

void require(bool condition, const std::string& message) {
    if (!condition)
        throw std::runtime_error(message);
}

WorkspaceTextEdit insertion(std::string filePath,
                            std::uint64_t version,
                            SourcePosition position,
                            std::string text) {
    return WorkspaceTextEdit{
        std::move(filePath),
        DocumentVersion{version},
        SourceRange{position, position},
        "",
        std::move(text)};
}

WorkspaceEditPlan twoFilePlan() {
    return rtledit::makeWorkspaceEditPlan(
        SemanticEditIntent{},
        RiskLevel::High,
        PreviewPolicy::Diff,
        {
            insertion("a.sv", 1, {0, 5}, ";"),
            insertion("b.sv", 1, {0, 4}, ";")
    });
}

class VerificationFailingDocumentManager final
    : public WorkspaceDocumentManager {
public:
    void openDocument(
        std::string filePath,
        std::string text,
        DocumentVersion version = DocumentVersion{1}) {
        documents_.openDocument(
            std::move(filePath), std::move(text), version);
    }

    const std::string& text(const std::string& filePath) const {
        return documents_.text(filePath);
    }

    DocumentVersion version(const std::string& filePath) const {
        return documents_.version(filePath);
    }

    void rejectVerificationSnapshotAfterRestores(
        std::size_t restoreCount) {
        restoresUntilSnapshotFailure_ = restoreCount;
    }

    std::optional<WorkspaceDocumentSnapshot> snapshot(
        const std::string& filePath) const override {
        if (rejectNextSnapshot_) {
            rejectNextSnapshot_ = false;
            return std::nullopt;
        }
        return documents_.snapshot(filePath);
    }

    bool applyTextEdits(
        const std::string& filePath,
        DocumentVersion expectedVersion,
        const std::vector<WorkspaceTextEdit>& editsInApplicationOrder)
        override {
        return documents_.applyTextEdits(
            filePath, expectedVersion, editsInApplicationOrder);
    }

    bool restoreSnapshot(
        const std::string& filePath,
        const WorkspaceDocumentSnapshot& snapshot) override {
        const bool restored = documents_.restoreSnapshot(filePath, snapshot);
        if (restored && restoresUntilSnapshotFailure_ > 0
            && --restoresUntilSnapshotFailure_ == 0) {
            rejectNextSnapshot_ = true;
        }
        return restored;
    }

private:
    MockWorkspaceDocumentManager documents_;
    std::size_t restoresUntilSnapshotFailure_ = 0;
    mutable bool rejectNextSnapshot_ = false;
};

bool previewIsMandatoryForHighRiskApply() {
    MockWorkspaceDocumentManager documents;
    documents.openDocument("a.sv", "alpha\n");
    documents.openDocument("b.sv", "beta\n");
    WorkspaceEditTransactionCoordinator coordinator;

    PreparedWorkspaceEditTransaction prepared =
        coordinator.prepare(twoFilePlan(), documents);
    require(prepared.ready(), "transaction should prepare");
    require(prepared.preview.built(), "preview should be built");
    require(prepared.sourceDiff.built(), "source diff should be built");

    const auto rejected = coordinator.apply(prepared, documents);
    require(
        rejected.status == TransactionStatus::PreviewRequired,
        "unconfirmed high-risk apply should be rejected");
    require(
        documents.text("a.sv") == "alpha\n" &&
            documents.text("b.sv") == "beta\n",
        "preview rejection changed documents");
    return true;
}

bool oneUndoAndRedoRestoresEveryFile() {
    MockWorkspaceDocumentManager documents;
    documents.openDocument("a.sv", "alpha\n");
    documents.openDocument("b.sv", "beta\n");
    WorkspaceEditTransactionCoordinator coordinator;

    auto prepared = coordinator.prepare(twoFilePlan(), documents);
    require(
        coordinator.confirmPreview(&prepared),
        "ready preview should confirm");
    const auto applied = coordinator.apply(prepared, documents);
    require(
        applied.status == TransactionStatus::Applied,
        "confirmed transaction should apply");
    require(
        documents.text("a.sv") == "alpha;\n" &&
            documents.text("b.sv") == "beta;\n",
        "transaction did not apply both files");
    require(
        coordinator.undoDepth() == 1 && coordinator.redoDepth() == 0,
        "applied transaction should create one history entry");

    const auto undone = coordinator.undo(documents);
    require(
        undone.status == TransactionStatus::Undone,
        "one undo should succeed");
    require(
        documents.text("a.sv") == "alpha\n" &&
            documents.text("b.sv") == "beta\n",
        "one undo did not restore all files");
    require(
        !coordinator.canUndo() && coordinator.canRedo(),
        "undo should move one atomic entry to redo");

    const auto redone = coordinator.redo(documents);
    require(
        redone.status == TransactionStatus::Redone,
        "one redo should succeed");
    require(
        documents.text("a.sv") == "alpha;\n" &&
            documents.text("b.sv") == "beta;\n",
        "one redo did not restore all files");
    return true;
}

bool dryRunNeverMutatesOrCreatesHistory() {
    MockWorkspaceDocumentManager documents;
    documents.openDocument("a.sv", "alpha\n");
    documents.openDocument("b.sv", "beta\n");
    WorkspaceEditTransactionCoordinator coordinator;

    auto prepared =
        coordinator.prepare(twoFilePlan(), documents, true);
    require(
        coordinator.confirmPreview(&prepared),
        "dry-run preview should confirm");
    const auto result = coordinator.apply(prepared, documents);
    require(
        result.status == TransactionStatus::DryRunOnly,
        "dry-run should return its explicit status");
    require(
        documents.text("a.sv") == "alpha\n" &&
            documents.text("b.sv") == "beta\n",
        "dry-run changed documents");
    require(
        !coordinator.canUndo() && !coordinator.canRedo(),
        "dry-run created transaction history");
    return true;
}

bool undoConflictIsDetectedBeforeAnyRestore() {
    MockWorkspaceDocumentManager documents;
    documents.openDocument("a.sv", "alpha\n");
    documents.openDocument("b.sv", "beta\n");
    WorkspaceEditTransactionCoordinator coordinator;
    auto prepared = coordinator.prepare(twoFilePlan(), documents);
    coordinator.confirmPreview(&prepared);
    require(
        coordinator.apply(prepared, documents).succeeded(),
        "fixture apply should succeed");

    documents.openDocument(
        "b.sv", "externally changed\n", DocumentVersion{99});
    const auto result = coordinator.undo(documents);
    require(
        result.status == TransactionStatus::Conflict,
        "external change should reject undo as conflict");
    require(
        documents.text("a.sv") == "alpha;\n",
        "conflict changed an earlier valid file");
    require(
        documents.text("b.sv") == "externally changed\n",
        "conflict overwrote the external change");
    require(
        coordinator.undoDepth() == 1,
        "conflicted undo consumed history");
    return true;
}

bool stalePreparedTransactionCannotApply() {
    MockWorkspaceDocumentManager documents;
    documents.openDocument("a.sv", "alpha\n");
    documents.openDocument("b.sv", "beta\n");
    WorkspaceEditTransactionCoordinator coordinator;
    auto prepared = coordinator.prepare(twoFilePlan(), documents);
    coordinator.confirmPreview(&prepared);

    documents.openDocument("a.sv", "new alpha\n", DocumentVersion{2});
    const auto result = coordinator.apply(prepared, documents);
    require(
        result.status == TransactionStatus::Stale,
        "old generation should be rejected as stale");
    require(
        documents.text("a.sv") == "new alpha\n" &&
            documents.text("b.sv") == "beta\n",
        "stale apply partially changed documents");
    return true;
}

bool failedUndoVerificationRollsBackAtomically() {
    VerificationFailingDocumentManager documents;
    documents.openDocument("a.sv", "alpha\n");
    documents.openDocument("b.sv", "beta\n");
    WorkspaceEditTransactionCoordinator coordinator;
    auto prepared = coordinator.prepare(twoFilePlan(), documents);
    coordinator.confirmPreview(&prepared);
    require(
        coordinator.apply(prepared, documents).succeeded(),
        "fixture apply should succeed");

    documents.rejectVerificationSnapshotAfterRestores(2);
    const auto result = coordinator.undo(documents);
    require(
        result.status == TransactionStatus::RestoreFailed,
        "failed post-restore verification should reject undo");
    require(
        result.residualFiles.empty(),
        "successful verification rollback should leave no residual files");
    require(
        documents.text("a.sv") == "alpha;\n" &&
            documents.text("b.sv") == "beta;\n",
        "failed verification did not restore the pre-undo document set");
    require(
        documents.version("a.sv") == DocumentVersion{2} &&
            documents.version("b.sv") == DocumentVersion{2},
        "failed verification did not restore pre-undo revisions");
    require(
        coordinator.undoDepth() == 1 && coordinator.redoDepth() == 0,
        "failed verification consumed transaction history");
    return true;
}

}  // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<bool()>>> tests = {
        {"previewIsMandatoryForHighRiskApply",
         previewIsMandatoryForHighRiskApply},
        {"oneUndoAndRedoRestoresEveryFile",
         oneUndoAndRedoRestoresEveryFile},
        {"dryRunNeverMutatesOrCreatesHistory",
         dryRunNeverMutatesOrCreatesHistory},
        {"undoConflictIsDetectedBeforeAnyRestore",
         undoConflictIsDetectedBeforeAnyRestore},
        {"stalePreparedTransactionCannotApply",
         stalePreparedTransactionCannotApply},
        {"failedUndoVerificationRollsBackAtomically",
         failedUndoVerificationRollsBackAtomically}
    };

    int failures = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& exception) {
            ++failures;
            std::cerr << "[FAIL] " << name << ": "
                      << exception.what() << '\n';
        }
    }
    return failures == 0 ? 0 : 1;
}
