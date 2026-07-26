#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "rtledit/core_types.h"
#include "rtledit/provenance.h"
#include "rtledit/semantic_edit_intent.h"
#include "rtledit/workspace_document_manager.h"

namespace rtledit {

enum class RiskLevel {
    Low,
    Medium,
    High
};

inline const char* riskLevelName(RiskLevel riskLevel) {
    switch (riskLevel) {
    case RiskLevel::Low:
        return "low";
    case RiskLevel::Medium:
        return "medium";
    case RiskLevel::High:
        return "high";
    }
    return "unknown";
}

enum class PreviewPolicy {
    Inline,
    Diff
};

inline const char* previewPolicyName(PreviewPolicy previewPolicy) {
    switch (previewPolicy) {
    case PreviewPolicy::Inline:
        return "inline";
    case PreviewPolicy::Diff:
        return "diff";
    }
    return "unknown";
}

struct DocumentBaseline {
    std::string filePath;
    DocumentVersion version;
};

enum class EditPlanStaleReason {
    NotStale,
    MixedDocumentVersions,
    DocumentMissing,
    VersionChanged,
    SemanticSnapshotChanged
};

const char* editPlanStaleReasonName(EditPlanStaleReason reason);

struct EditPlanStaleStatus {
    EditPlanStaleReason reason = EditPlanStaleReason::NotStale;
    std::string filePath;
    DocumentVersion expectedVersion;
    DocumentVersion actualVersion;
    std::string expectedSemanticSnapshotId;
    std::string actualSemanticSnapshotId;

    bool stale() const {
        return reason != EditPlanStaleReason::NotStale;
    }
};

struct WorkspaceEditPlan {
    SemanticEditIntent intent;
    SemanticIndexSnapshot semanticSnapshot;
    std::vector<std::string> semanticIndexFilePaths;
    RiskLevel riskLevel = RiskLevel::Low;
    PreviewPolicy previewPolicy = PreviewPolicy::Inline;
    bool hasMixedDocumentVersions = false;
    std::vector<DocumentBaseline> baselines;
    std::vector<WorkspaceTextEdit> edits;
    std::vector<TextEditProvenance> provenance;
};

enum class EditPlanValidationIssue {
    None,
    EmptyFilePath,
    InvalidRange,
    MixedDocumentVersions,
    MissingBaseline,
    BaselineVersionMismatch,
    ProvenanceMissing,
    ProvenanceEditIndexOutOfBounds,
    DuplicateProvenanceEditIndex
};

const char* editPlanValidationIssueName(EditPlanValidationIssue issue);

struct EditPlanValidationResult {
    EditPlanValidationIssue issue = EditPlanValidationIssue::None;
    std::string filePath;
    std::size_t editIndex = 0;
    std::string message;

    bool valid() const {
        return issue == EditPlanValidationIssue::None;
    }
};

struct PreviewFileSummary {
    std::string filePath;
    DocumentVersion version;
    std::size_t editCount = 0;
};

struct PreviewEditSummary {
    std::size_t editIndex = 0;
    std::string filePath;
    SourceRange range;
    std::size_t expectedTextSize = 0;
    std::size_t newTextSize = 0;
    bool isInsertion = false;
    bool isDeletion = false;
    bool isReplacement = false;
    TextEditProvenance provenance;
};

enum class PreviewStatus {
    Built,
    InvalidPlan,
    Stale
};

const char* previewStatusName(PreviewStatus status);

struct WorkspaceEditPreview {
    PreviewStatus status = PreviewStatus::Built;
    std::string actionId;
    RiskLevel riskLevel = RiskLevel::Low;
    PreviewPolicy previewPolicy = PreviewPolicy::Inline;
    EditPlanValidationResult validationResult;
    EditPlanStaleStatus staleStatus;
    std::size_t fileCount = 0;
    std::size_t editCount = 0;
    std::vector<PreviewFileSummary> files;
    std::vector<PreviewEditSummary> edits;
    SemanticIndexSnapshot semanticSnapshot;
    std::vector<std::string> semanticIndexFilePaths;

    bool built() const {
        return status == PreviewStatus::Built;
    }
};

enum class SourceDiffStatus {
    Built,
    InvalidPlan,
    Stale,
    InvalidRange,
    RangeOutOfBounds,
    RangeTextMismatch,
    OverlappingEdits
};

const char* sourceDiffStatusName(SourceDiffStatus status);

enum class SourceDiffLineKind {
    Context,
    Removed,
    Added
};

const char* sourceDiffLineKindName(SourceDiffLineKind kind);

struct SourceDiffLine {
    SourceDiffLineKind kind = SourceDiffLineKind::Context;
    std::size_t oldLine = 0;
    std::size_t newLine = 0;
    std::string text;
};

struct SourceDiffHunk {
    std::size_t oldStartLine = 0;
    std::size_t oldLineCount = 0;
    std::size_t newStartLine = 0;
    std::size_t newLineCount = 0;
    std::vector<SourceDiffLine> lines;
};

struct SourceDiffFile {
    std::string filePath;
    DocumentVersion version;
    std::size_t editCount = 0;
    std::string beforeText;
    std::string afterText;
    std::vector<SourceDiffHunk> hunks;
};

struct WorkspaceEditSourceDiff {
    SourceDiffStatus status = SourceDiffStatus::Built;
    EditPlanStaleStatus staleStatus;
    EditPlanValidationResult validationResult;
    std::string filePath;
    std::string message;
    std::vector<SourceDiffFile> files;
    SemanticIndexSnapshot semanticSnapshot;
    std::vector<std::string> semanticIndexFilePaths;

    bool built() const {
        return status == SourceDiffStatus::Built;
    }
};

enum class PlanApplyStatus {
    Applied,
    Invalid,
    Stale,
    PatchFailed
};

const char* planApplyStatusName(PlanApplyStatus status);

struct PlanApplyResult {
    PlanApplyStatus status = PlanApplyStatus::Applied;
    EditPlanStaleStatus staleStatus;
    ApplyResult patchResult;
    EditPlanValidationResult validationResult;
    SemanticIndexSnapshot semanticSnapshot;
    std::vector<std::string> semanticIndexFilePaths;

    bool applied() const {
        return status == PlanApplyStatus::Applied;
    }
};

std::vector<DocumentBaseline> collectDocumentBaselines(
    const std::vector<WorkspaceTextEdit>& edits);

WorkspaceEditPlan makeWorkspaceEditPlan(
    SemanticEditIntent intent,
    RiskLevel riskLevel,
    PreviewPolicy previewPolicy,
    std::vector<WorkspaceTextEdit> edits);

WorkspaceEditPlan makeWorkspaceEditPlan(
    SemanticEditIntent intent,
    RiskLevel riskLevel,
    PreviewPolicy previewPolicy,
    std::vector<WorkspaceTextEdit> edits,
    std::vector<TextEditProvenance> provenance);

EditPlanValidationResult validateWorkspaceEditPlan(
    const WorkspaceEditPlan& plan);

EditPlanStaleStatus staleStatus(
    const WorkspaceEditPlan& plan,
    const WorkspaceDocumentManager& documentManager);

EditPlanStaleStatus staleStatus(
    const WorkspaceEditPlan& plan,
    SemanticIndexSnapshot currentSemanticSnapshot,
    const WorkspaceDocumentManager& documentManager);

WorkspaceEditPreview buildWorkspaceEditPreview(const WorkspaceEditPlan& plan);

WorkspaceEditPreview buildWorkspaceEditPreview(
    const WorkspaceEditPlan& plan,
    const WorkspaceDocumentManager& documentManager);

WorkspaceEditPreview buildWorkspaceEditPreview(
    const WorkspaceEditPlan& plan,
    SemanticIndexSnapshot currentSemanticSnapshot,
    const WorkspaceDocumentManager& documentManager);

std::string renderWorkspaceEditPreview(const WorkspaceEditPreview& preview);

std::string renderWorkspaceEditPreview(const WorkspaceEditPlan& plan);

std::string renderWorkspaceEditPreview(
    const WorkspaceEditPlan& plan,
    const WorkspaceDocumentManager& documentManager);

std::string renderWorkspaceEditPreview(
    const WorkspaceEditPlan& plan,
    SemanticIndexSnapshot currentSemanticSnapshot,
    const WorkspaceDocumentManager& documentManager);

WorkspaceEditSourceDiff buildWorkspaceEditSourceDiff(
    const WorkspaceEditPlan& plan,
    const WorkspaceDocumentManager& documentManager);

WorkspaceEditSourceDiff buildWorkspaceEditSourceDiff(
    const WorkspaceEditPlan& plan,
    SemanticIndexSnapshot currentSemanticSnapshot,
    const WorkspaceDocumentManager& documentManager);

std::vector<SourceDiffHunk> buildSourceDiffHunks(
    const std::string& beforeText,
    const std::string& afterText,
    std::size_t contextLineCount = 3);

std::string renderWorkspaceEditSourceDiff(const WorkspaceEditSourceDiff& diff);

std::string renderWorkspaceEditSourceDiffHunks(
    const WorkspaceEditSourceDiff& diff);

std::string renderWorkspaceEditSourceDiff(
    const WorkspaceEditPlan& plan,
    const WorkspaceDocumentManager& documentManager);

std::string renderWorkspaceEditSourceDiffHunks(
    const WorkspaceEditPlan& plan,
    const WorkspaceDocumentManager& documentManager);

std::string renderWorkspaceEditSourceDiff(
    const WorkspaceEditPlan& plan,
    SemanticIndexSnapshot currentSemanticSnapshot,
    const WorkspaceDocumentManager& documentManager);

std::string renderWorkspaceEditSourceDiffHunks(
    const WorkspaceEditPlan& plan,
    SemanticIndexSnapshot currentSemanticSnapshot,
    const WorkspaceDocumentManager& documentManager);

PlanApplyResult applyWorkspaceEditPlan(
    const WorkspaceEditPlan& plan,
    WorkspaceDocumentManager& documentManager);

PlanApplyResult applyWorkspaceEditPlan(
    const WorkspaceEditPlan& plan,
    SemanticIndexSnapshot currentSemanticSnapshot,
    WorkspaceDocumentManager& documentManager);

std::string renderWorkspaceEditPlanApplyResult(const PlanApplyResult& result);

std::string renderWorkspaceEditPlanApplyResult(
    const WorkspaceEditPlan& plan,
    WorkspaceDocumentManager& documentManager);

std::string renderWorkspaceEditPlanApplyResult(
    const WorkspaceEditPlan& plan,
    SemanticIndexSnapshot currentSemanticSnapshot,
    WorkspaceDocumentManager& documentManager);

}  // namespace rtledit
