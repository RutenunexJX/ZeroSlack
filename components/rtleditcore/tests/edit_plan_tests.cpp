#include "rtledit/edit_plan.h"
#include "rtledit/mock_workspace_document_manager.h"

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using rtledit::ApplyStatus;
using rtledit::AnchorProvenance;
using rtledit::AnchorResolutionSource;
using rtledit::DocumentVersion;
using rtledit::EditPlanValidationIssue;
using rtledit::EditPlanStaleReason;
using rtledit::MockWorkspaceDocumentManager;
using rtledit::PlanApplyStatus;
using rtledit::PreviewPolicy;
using rtledit::PreviewStatus;
using rtledit::RiskLevel;
using rtledit::SemanticEditIntent;
using rtledit::SemanticEditKind;
using rtledit::SemanticIndexSnapshot;
using rtledit::SemanticObjectId;
using rtledit::SemanticObjectKind;
using rtledit::SourceDiffLineKind;
using rtledit::SourceDiffStatus;
using rtledit::SourcePosition;
using rtledit::SourceRange;
using rtledit::TextEditProvenance;
using rtledit::WorkspaceTextEdit;

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

WorkspaceTextEdit edit(
    std::string filePath,
    std::uint64_t version,
    SourcePosition start,
    SourcePosition end,
    std::string expectedText,
    std::string newText) {
    return WorkspaceTextEdit{
        std::move(filePath),
        DocumentVersion{version},
        SourceRange{start, end},
        std::move(expectedText),
        std::move(newText)};
}

SemanticEditIntent exposeIntent() {
    return SemanticEditIntent{
        SemanticEditKind::ExposeSignalToTop,
        SemanticObjectId{
            SemanticObjectKind::Signal,
            "top.payload",
            "",
            "top.sv",
            SourceRange{SourcePosition{0, 0}, SourcePosition{0, 0}},
            ""}};
}

bool publicNameHelpersAreStable() {
    require(
        std::string(rtledit::riskLevelName(RiskLevel::High)) == "high",
        "risk level name mismatch");
    require(
        std::string(rtledit::previewPolicyName(PreviewPolicy::Diff)) == "diff",
        "preview policy name mismatch");
    require(
        std::string(rtledit::applyStatusName(ApplyStatus::RangeTextMismatch)) ==
            "range_text_mismatch",
        "apply status name mismatch");
    require(
        std::string(rtledit::editPlanStaleReasonName(
            EditPlanStaleReason::VersionChanged)) == "version_changed",
        "stale reason name mismatch");
    require(
        std::string(rtledit::editPlanStaleReasonName(
            EditPlanStaleReason::SemanticSnapshotChanged)) ==
            "semantic_snapshot_changed",
        "semantic snapshot stale reason name mismatch");
    require(
        std::string(rtledit::editPlanValidationIssueName(
            EditPlanValidationIssue::InvalidRange)) == "invalid_range",
        "validation issue name mismatch");
    require(
        std::string(rtledit::previewStatusName(PreviewStatus::InvalidPlan)) ==
            "invalid_plan",
        "preview status name mismatch");
    require(
        std::string(rtledit::previewStatusName(PreviewStatus::Stale)) == "stale",
        "stale preview status name mismatch");
    require(
        std::string(rtledit::sourceDiffStatusName(
            SourceDiffStatus::RangeOutOfBounds)) == "range_out_of_bounds",
        "source diff status name mismatch");
    require(
        std::string(rtledit::sourceDiffLineKindName(
            SourceDiffLineKind::Added)) == "added",
        "source diff line kind name mismatch");
    require(
        std::string(rtledit::anchorResolutionSourceName(
            AnchorResolutionSource::TreeSitter)) == "tree_sitter",
        "anchor resolution source name mismatch");
    require(
        std::string(rtledit::planApplyStatusName(PlanApplyStatus::PatchFailed)) ==
            "patch_failed",
        "plan apply status name mismatch");
    return true;
}

bool planCapturesBaselines() {
    const auto plan = rtledit::makeWorkspaceEditPlan(
        exposeIntent(),
        RiskLevel::Medium,
        PreviewPolicy::Inline,
        {
            edit("top.sv", 7, {2, 21}, {2, 21}, "", ","),
            edit("top.sv", 7, {3, 0}, {3, 0}, "", "    output logic done\n")
        });

    require(plan.riskLevel == RiskLevel::Medium, "risk level mismatch");
    require(plan.previewPolicy == PreviewPolicy::Inline, "preview policy mismatch");
    require(plan.semanticSnapshot.empty(), "direct plan should use empty semantic snapshot");
    require(
        plan.semanticIndexFilePaths.empty(),
        "direct plan should not report semantic index files");
    require(plan.baselines.size() == 1, "plan should collect one baseline");
    require(plan.baselines.front().filePath == "top.sv", "baseline file mismatch");
    require(plan.baselines.front().version == DocumentVersion{7}, "baseline version mismatch");
    require(!plan.hasMixedDocumentVersions, "plan should not have mixed versions");
    require(plan.edits.size() == 2, "plan should keep edits");
    require(plan.provenance.size() == 2, "plan should generate default provenance");
    require(plan.provenance.front().actionId == "signal.exposeToTop", "default provenance action mismatch");
    return true;
}

bool planCapturesExplicitProvenance() {
    const auto plan = rtledit::makeWorkspaceEditPlan(
        exposeIntent(),
        RiskLevel::Medium,
        PreviewPolicy::Inline,
        {
            edit("top.sv", 7, {2, 21}, {2, 21}, "", ","),
            edit("top.sv", 7, {3, 0}, {3, 0}, "", "    output logic done\n")
        },
        {
            TextEditProvenance{
                0,
                "signal.exposeToTop",
                "previousPortTerminator",
                "Append comma to the previous ANSI port."},
            TextEditProvenance{
                1,
                "signal.exposeToTop",
                "insert",
                "Insert the new ANSI port line."}
        });

    require(plan.provenance.size() == 2, "explicit provenance should be kept");
    require(
        plan.provenance.front().anchorName == "previousPortTerminator",
        "first provenance anchor mismatch");
    require(
        plan.provenance.back().anchorName == "insert",
        "second provenance anchor mismatch");
    require(
        plan.provenance.back().description == "Insert the new ANSI port line.",
        "provenance description mismatch");
    return true;
}

bool previewSummarizesPlanMetadata() {
    const auto plan = rtledit::makeWorkspaceEditPlan(
        exposeIntent(),
        RiskLevel::Medium,
        PreviewPolicy::Inline,
        {
            edit("top.sv", 7, {2, 21}, {2, 21}, "", ","),
            edit("top.sv", 7, {3, 0}, {3, 0}, "", "    output logic done\n")
        },
        {
            TextEditProvenance{
                0,
                "signal.exposeToTop",
                "previousPortTerminator",
                "Append comma to the previous ANSI port."},
            TextEditProvenance{
                1,
                "signal.exposeToTop",
                "insert",
                "Insert the new ANSI port line."}
        });

    const auto preview = rtledit::buildWorkspaceEditPreview(plan);
    require(preview.built(), "preview should build");
    require(preview.status == PreviewStatus::Built, "preview status mismatch");
    require(preview.actionId == "signal.exposeToTop", "preview action mismatch");
    require(preview.riskLevel == RiskLevel::Medium, "preview risk mismatch");
    require(preview.previewPolicy == PreviewPolicy::Inline, "preview policy mismatch");
    require(
        preview.semanticSnapshot.empty(),
        "direct preview should not report semantic snapshot");
    require(
        preview.semanticIndexFilePaths.empty(),
        "direct preview should not report semantic index files");
    require(preview.fileCount == 1, "preview file count mismatch");
    require(preview.editCount == 2, "preview edit count mismatch");
    require(preview.files.size() == 1, "preview should include one file");
    require(preview.files.front().filePath == "top.sv", "preview file mismatch");
    require(preview.files.front().editCount == 2, "preview file edit count mismatch");
    require(preview.edits.size() == 2, "preview should include two edits");
    require(preview.edits.front().isInsertion, "comma edit should be insertion");
    require(preview.edits.front().newTextSize == 1, "comma size mismatch");
    require(
        preview.edits.front().provenance.anchorName == "previousPortTerminator",
        "preview provenance mismatch");
    require(preview.edits.back().isInsertion, "port line should be insertion");
    require(preview.edits.back().newTextSize > 1, "port line size mismatch");
    return true;
}

bool previewWithDocumentManagerReportsVersionStale() {
    MockWorkspaceDocumentManager documents;
    documents.openDocument("top.sv", "module top;\n", DocumentVersion{8});

    const auto plan = rtledit::makeWorkspaceEditPlan(
        exposeIntent(),
        RiskLevel::Medium,
        PreviewPolicy::Inline,
        {edit("top.sv", 7, {0, 10}, {0, 10}, "", " ()")});

    const auto preview = rtledit::buildWorkspaceEditPreview(plan, documents);
    require(!preview.built(), "stale preview should not be built");
    require(preview.status == PreviewStatus::Stale, "wrong stale preview status");
    require(
        preview.staleStatus.reason == EditPlanStaleReason::VersionChanged,
        "wrong stale preview reason");
    require(preview.staleStatus.filePath == "top.sv", "stale preview file mismatch");
    require(preview.fileCount == 0, "stale preview file count mismatch");
    require(preview.editCount == 0, "stale preview edit count mismatch");
    require(preview.files.empty(), "stale preview should not contain file summaries");
    require(preview.edits.empty(), "stale preview should not contain edit summaries");
    return true;
}

bool previewWithDocumentManagerReportsMissingDocumentStale() {
    MockWorkspaceDocumentManager documents;

    const auto plan = rtledit::makeWorkspaceEditPlan(
        exposeIntent(),
        RiskLevel::Medium,
        PreviewPolicy::Inline,
        {edit("missing.sv", 7, {0, 0}, {0, 0}, "", "x")});

    const auto preview = rtledit::buildWorkspaceEditPreview(plan, documents);
    require(!preview.built(), "missing document preview should not be built");
    require(
        preview.status == PreviewStatus::Stale,
        "wrong missing document preview status");
    require(
        preview.staleStatus.reason == EditPlanStaleReason::DocumentMissing,
        "wrong missing document preview stale reason");
    require(
        preview.staleStatus.filePath == "missing.sv",
        "missing document preview file mismatch");
    return true;
}

bool previewWithDocumentManagerReportsSemanticSnapshotStale() {
    MockWorkspaceDocumentManager documents;
    documents.openDocument("top.sv", "module top;\n", DocumentVersion{7});

    auto plan = rtledit::makeWorkspaceEditPlan(
        exposeIntent(),
        RiskLevel::Medium,
        PreviewPolicy::Inline,
        {edit("top.sv", 7, {0, 10}, {0, 10}, "", " ()")});
    plan.semanticSnapshot = SemanticIndexSnapshot{"snapshot-a"};

    const auto preview = rtledit::buildWorkspaceEditPreview(
        plan,
        SemanticIndexSnapshot{"snapshot-b"},
        documents);
    require(!preview.built(), "semantic-stale preview should not be built");
    require(
        preview.status == PreviewStatus::Stale,
        "wrong semantic-stale preview status");
    require(
        preview.staleStatus.reason ==
            EditPlanStaleReason::SemanticSnapshotChanged,
        "wrong semantic-stale preview reason");
    require(
        preview.staleStatus.expectedSemanticSnapshotId == "snapshot-a",
        "semantic-stale preview expected snapshot mismatch");
    require(
        preview.staleStatus.actualSemanticSnapshotId == "snapshot-b",
        "semantic-stale preview actual snapshot mismatch");
    require(preview.files.empty(), "semantic-stale preview should not contain files");
    require(preview.edits.empty(), "semantic-stale preview should not contain edits");
    return true;
}

bool previewWithDocumentManagerRejectsInvalidPlanBeforeStale() {
    MockWorkspaceDocumentManager documents;
    documents.openDocument("top.sv", "module top;\n", DocumentVersion{8});

    auto plan = rtledit::makeWorkspaceEditPlan(
        exposeIntent(),
        RiskLevel::Medium,
        PreviewPolicy::Inline,
        {edit("top.sv", 7, {0, 0}, {0, 0}, "", "x")});
    plan.provenance.clear();

    const auto preview = rtledit::buildWorkspaceEditPreview(plan, documents);
    require(!preview.built(), "invalid stale preview should not build");
    require(
        preview.status == PreviewStatus::InvalidPlan,
        "invalid plan should take precedence over stale preview");
    require(
        preview.validationResult.issue == EditPlanValidationIssue::ProvenanceMissing,
        "wrong invalid stale preview validation issue");
    require(
        !preview.staleStatus.stale(),
        "invalid preview should not report stale status");
    return true;
}

bool previewRejectsInvalidPlan() {
    auto plan = rtledit::makeWorkspaceEditPlan(
        exposeIntent(),
        RiskLevel::Medium,
        PreviewPolicy::Inline,
        {
            edit("top.sv", 7, {2, 21}, {2, 21}, "", ","),
            edit("top.sv", 7, {3, 0}, {3, 0}, "", "    output logic done\n")
        });
    plan.provenance.pop_back();

    const auto preview = rtledit::buildWorkspaceEditPreview(plan);
    require(!preview.built(), "invalid plan preview should not build");
    require(
        preview.status == PreviewStatus::InvalidPlan,
        "wrong invalid plan preview status");
    require(
        preview.validationResult.issue == EditPlanValidationIssue::ProvenanceMissing,
        "wrong invalid plan preview validation issue");
    require(
        preview.validationResult.editIndex == 1,
        "invalid plan preview edit index mismatch");
    require(preview.fileCount == 0, "invalid plan preview file count mismatch");
    require(preview.editCount == 0, "invalid plan preview edit count mismatch");
    require(preview.files.empty(), "invalid plan preview should not contain files");
    require(preview.edits.empty(), "invalid plan preview should not contain edits");
    return true;
}

bool previewRejectsInvalidRangePlanBeforeSummaries() {
    auto plan = rtledit::makeWorkspaceEditPlan(
        exposeIntent(),
        RiskLevel::Medium,
        PreviewPolicy::Inline,
        {edit("top.sv", 7, {1, 0}, {0, 0}, "", "x")});

    const auto preview = rtledit::buildWorkspaceEditPreview(plan);
    require(!preview.built(), "invalid range preview should not build");
    require(
        preview.status == PreviewStatus::InvalidPlan,
        "wrong invalid range preview status");
    require(
        preview.validationResult.issue == EditPlanValidationIssue::InvalidRange,
        "wrong invalid range preview validation issue");
    require(preview.files.empty(), "invalid range preview should not contain files");
    require(preview.edits.empty(), "invalid range preview should not contain edits");
    return true;
}

bool previewSummarizesMultipleFiles() {
    const auto plan = rtledit::makeWorkspaceEditPlan(
        exposeIntent(),
        RiskLevel::High,
        PreviewPolicy::Diff,
        {
            edit("a.sv", 1, {0, 0}, {0, 0}, "", "a"),
            edit("b.sv", 2, {0, 0}, {0, 1}, "x", "y")
        });

    const auto preview = rtledit::buildWorkspaceEditPreview(plan);
    require(preview.fileCount == 2, "multi-file preview file count mismatch");
    require(preview.editCount == 2, "multi-file preview edit count mismatch");
    require(preview.riskLevel == RiskLevel::High, "multi-file risk mismatch");
    require(preview.previewPolicy == PreviewPolicy::Diff, "multi-file policy mismatch");
    require(preview.files.front().filePath == "a.sv", "first file mismatch");
    require(preview.files.back().filePath == "b.sv", "second file mismatch");
    require(preview.edits.front().isInsertion, "first edit should be insertion");
    require(preview.edits.back().isReplacement, "second edit should be replacement");
    return true;
}

bool previewRenderingReportsInvalidPlan() {
    auto plan = rtledit::makeWorkspaceEditPlan(
        exposeIntent(),
        RiskLevel::Medium,
        PreviewPolicy::Inline,
        {edit("top.sv", 7, {0, 0}, {0, 0}, "", "x")});
    plan.provenance.clear();

    const auto rendered = rtledit::renderWorkspaceEditPreview(plan);
    require(
        rendered ==
            "status=invalid_plan action=signal.exposeToTop risk=medium policy=inline files=0 edits=0\n"
            "invalid issue=provenance_missing file=top.sv edit=0 message=Workspace edit plan provenance is missing an edit entry.\n"
            "files\n"
            "edits\n",
        "rendered invalid plan preview mismatch");
    return true;
}

bool previewRenderingReportsStale() {
    MockWorkspaceDocumentManager documents;
    documents.openDocument("top.sv", "module top;\n", DocumentVersion{8});

    const auto plan = rtledit::makeWorkspaceEditPlan(
        exposeIntent(),
        RiskLevel::Medium,
        PreviewPolicy::Inline,
        {edit("top.sv", 7, {0, 10}, {0, 10}, "", " ()")});

    const auto rendered =
        rtledit::renderWorkspaceEditPreview(plan, documents);
    require(
        rendered ==
            "status=stale action=signal.exposeToTop risk=medium policy=inline files=0 edits=0\n"
            "stale reason=version_changed file=top.sv expected=7 actual=8\n"
            "files\n"
            "edits\n",
        "rendered stale preview mismatch");
    return true;
}

bool previewRenderingReportsSemanticSnapshotStale() {
    MockWorkspaceDocumentManager documents;
    documents.openDocument("top.sv", "module top;\n", DocumentVersion{7});

    auto plan = rtledit::makeWorkspaceEditPlan(
        exposeIntent(),
        RiskLevel::Medium,
        PreviewPolicy::Inline,
        {edit("top.sv", 7, {0, 10}, {0, 10}, "", " ()")});
    plan.semanticSnapshot = SemanticIndexSnapshot{"snapshot-a"};

    const auto rendered = rtledit::renderWorkspaceEditPreview(
        plan,
        SemanticIndexSnapshot{"snapshot-b"},
        documents);
    require(
        rendered ==
            "status=stale action=signal.exposeToTop risk=medium policy=inline files=0 edits=0\n"
            "semanticSnapshot=snapshot-a\n"
            "stale reason=semantic_snapshot_changed expectedSemanticSnapshot=snapshot-a actualSemanticSnapshot=snapshot-b\n"
            "files\n"
            "edits\n",
        "rendered semantic-stale preview mismatch");
    return true;
}

bool previewRenderingIsStable() {
    const auto plan = rtledit::makeWorkspaceEditPlan(
        exposeIntent(),
        RiskLevel::Medium,
        PreviewPolicy::Inline,
        {
            edit("top.sv", 7, {2, 21}, {2, 21}, "", ","),
            edit("top.sv", 7, {3, 0}, {3, 0}, "", "    output logic done\n")
        },
        {
            TextEditProvenance{
                0,
                "signal.exposeToTop",
                "previousPortTerminator",
                "Append comma."},
            TextEditProvenance{
                1,
                "signal.exposeToTop",
                "insert",
                "Insert port."}
        });

    const auto rendered = rtledit::renderWorkspaceEditPreview(plan);
    require(
        rendered ==
            "action=signal.exposeToTop risk=medium policy=inline files=1 edits=2\n"
            "files\n"
            "- top.sv version=7 edits=2\n"
            "edits\n"
            "- #0 insert top.sv 2:21-2:21 expected=0 new=1 action=signal.exposeToTop anchor=previousPortTerminator note=Append comma.\n"
            "- #1 insert top.sv 3:0-3:0 expected=0 new=22 action=signal.exposeToTop anchor=insert note=Insert port.\n",
        "rendered preview mismatch");
    return true;
}

bool previewRenderingIncludesStructuredAnchorProvenance() {
    const auto plan = rtledit::makeWorkspaceEditPlan(
        exposeIntent(),
        RiskLevel::Medium,
        PreviewPolicy::Inline,
        {edit("top.sv", 7, {3, 0}, {3, 0}, "", "    output logic done\n")},
        {TextEditProvenance{
            0,
            "signal.exposeToTop",
            "insert",
            "Insert port.",
            AnchorProvenance{
                AnchorResolutionSource::TreeSitter,
                "module_port_list.bottom",
                "semantic-snapshot-7"}}});

    const auto rendered = rtledit::renderWorkspaceEditPreview(plan);
    require(
        rendered ==
            "action=signal.exposeToTop risk=medium policy=inline files=1 edits=1\n"
            "files\n"
            "- top.sv version=7 edits=1\n"
            "edits\n"
            "- #0 insert top.sv 3:0-3:0 expected=0 new=22 action=signal.exposeToTop anchor=insert note=Insert port. anchorSource=tree_sitter resolver=module_port_list.bottom anchorSnapshot=semantic-snapshot-7\n",
        "rendered structured anchor provenance mismatch");
    return true;
}

bool sourceDiffBuildsBeforeAfterText() {
    MockWorkspaceDocumentManager documents;
    documents.openDocument(
        "top.sv",
        "module top (\n"
        "    input logic clk\n"
        ");\n",
        DocumentVersion{7});

    const auto plan = rtledit::makeWorkspaceEditPlan(
        exposeIntent(),
        RiskLevel::Medium,
        PreviewPolicy::Diff,
        {
            edit("top.sv", 7, {1, 19}, {1, 19}, "", ","),
            edit("top.sv", 7, {2, 0}, {2, 0}, "", "    output logic done\n")
        });

    const auto diff = rtledit::buildWorkspaceEditSourceDiff(plan, documents);
    require(diff.built(), "source diff should build");
    require(diff.status == SourceDiffStatus::Built, "wrong source diff status");
    require(diff.semanticSnapshot.empty(), "direct source diff snapshot should be empty");
    require(
        diff.semanticIndexFilePaths.empty(),
        "direct source diff semantic index files should be empty");
    require(diff.files.size() == 1, "source diff should contain one file");
    require(diff.files.front().filePath == "top.sv", "source diff file mismatch");
    require(diff.files.front().editCount == 2, "source diff edit count mismatch");
    require(
        diff.files.front().beforeText ==
            "module top (\n"
            "    input logic clk\n"
            ");\n",
        "source diff before text mismatch");
    require(
        diff.files.front().afterText ==
            "module top (\n"
            "    input logic clk,\n"
            "    output logic done\n"
            ");\n",
        "source diff after text mismatch");
    require(diff.files.front().hunks.size() == 1, "source diff hunk count mismatch");
    const auto& hunk = diff.files.front().hunks.front();
    require(hunk.oldStartLine == 1, "source diff hunk old start mismatch");
    require(hunk.oldLineCount == 3, "source diff hunk old count mismatch");
    require(hunk.newStartLine == 1, "source diff hunk new start mismatch");
    require(hunk.newLineCount == 4, "source diff hunk new count mismatch");
    require(hunk.lines.size() == 5, "source diff hunk line count mismatch");
    require(
        hunk.lines[0].kind == SourceDiffLineKind::Context,
        "first hunk line should be context");
    require(
        hunk.lines[1].kind == SourceDiffLineKind::Removed,
        "second hunk line should be removed");
    require(
        hunk.lines[1].oldLine == 2 && hunk.lines[1].newLine == 0,
        "removed line numbers mismatch");
    require(
        hunk.lines[2].kind == SourceDiffLineKind::Added,
        "third hunk line should be added");
    require(
        hunk.lines[2].oldLine == 0 && hunk.lines[2].newLine == 2,
        "added line numbers mismatch");
    require(
        hunk.lines[3].kind == SourceDiffLineKind::Added,
        "fourth hunk line should be added");
    require(
        hunk.lines[4].kind == SourceDiffLineKind::Context,
        "last hunk line should be context");
    return true;
}

bool sourceDiffHunksAreEmptyForIdenticalText() {
    const auto hunks =
        rtledit::buildSourceDiffHunks("module top;\n", "module top;\n");
    require(hunks.empty(), "identical text should not produce diff hunks");
    return true;
}

bool sourceDiffHunksSplitDistantChanges() {
    const auto hunks = rtledit::buildSourceDiffHunks(
        "a\nb\nc\nd\ne\nf\ng\nh\n",
        "A\nb\nc\nd\ne\nf\ng\nH\n",
        1);

    require(hunks.size() == 2, "distant changes should produce two hunks");
    require(hunks.front().oldStartLine == 1, "first hunk old start mismatch");
    require(hunks.front().newStartLine == 1, "first hunk new start mismatch");
    require(hunks.front().lines.size() == 3, "first hunk line count mismatch");
    require(
        hunks.front().lines.front().kind == SourceDiffLineKind::Removed,
        "first hunk should start with removed line");
    require(
        hunks.front().lines.back().kind == SourceDiffLineKind::Context,
        "first hunk should include trailing context");

    require(hunks.back().oldStartLine == 7, "second hunk old start mismatch");
    require(hunks.back().newStartLine == 7, "second hunk new start mismatch");
    require(hunks.back().lines.size() == 3, "second hunk line count mismatch");
    require(
        hunks.back().lines.front().kind == SourceDiffLineKind::Context,
        "second hunk should include leading context");
    require(
        hunks.back().lines[1].kind == SourceDiffLineKind::Removed,
        "second hunk should contain removed line");
    require(
        hunks.back().lines[2].kind == SourceDiffLineKind::Added,
        "second hunk should contain added line");
    return true;
}

bool sourceDiffRejectsRangeTextMismatch() {
    MockWorkspaceDocumentManager documents;
    documents.openDocument("top.sv", "module top;\n", DocumentVersion{7});

    const auto plan = rtledit::makeWorkspaceEditPlan(
        exposeIntent(),
        RiskLevel::Medium,
        PreviewPolicy::Diff,
        {edit("top.sv", 7, {0, 0}, {0, 6}, "wrong", "package")});

    const auto diff = rtledit::buildWorkspaceEditSourceDiff(plan, documents);
    require(!diff.built(), "range mismatch source diff should not build");
    require(
        diff.status == SourceDiffStatus::RangeTextMismatch,
        "wrong source diff range mismatch status");
    require(diff.filePath == "top.sv", "source diff failure file mismatch");
    require(
        documents.text("top.sv") == "module top;\n",
        "source diff failure should not mutate document");
    return true;
}

bool sourceDiffRejectsInvalidPlanBeforeDocumentChecks() {
    MockWorkspaceDocumentManager documents;
    documents.openDocument("top.sv", "module top;\n", DocumentVersion{8});

    auto plan = rtledit::makeWorkspaceEditPlan(
        exposeIntent(),
        RiskLevel::Medium,
        PreviewPolicy::Diff,
        {edit("top.sv", 7, {0, 0}, {0, 0}, "", "x")});
    plan.provenance.clear();

    const auto diff = rtledit::buildWorkspaceEditSourceDiff(plan, documents);
    require(!diff.built(), "invalid plan source diff should not build");
    require(
        diff.status == SourceDiffStatus::InvalidPlan,
        "wrong invalid plan source diff status");
    require(
        diff.validationResult.issue == EditPlanValidationIssue::ProvenanceMissing,
        "wrong invalid plan source diff validation issue");
    require(
        diff.filePath == "top.sv",
        "invalid plan source diff file mismatch");
    require(
        diff.files.empty(),
        "invalid plan source diff should not contain files");
    require(
        documents.text("top.sv") == "module top;\n",
        "invalid plan source diff should not mutate document");
    return true;
}

bool sourceDiffRejectsInvalidRangeAsInvalidPlan() {
    MockWorkspaceDocumentManager documents;
    documents.openDocument("top.sv", "module top;\n", DocumentVersion{7});

    const auto plan = rtledit::makeWorkspaceEditPlan(
        exposeIntent(),
        RiskLevel::Medium,
        PreviewPolicy::Diff,
        {edit("top.sv", 7, {1, 0}, {0, 0}, "", "x")});

    const auto diff = rtledit::buildWorkspaceEditSourceDiff(plan, documents);
    require(!diff.built(), "invalid range source diff should not build");
    require(
        diff.status == SourceDiffStatus::InvalidPlan,
        "wrong invalid range source diff status");
    require(
        diff.validationResult.issue == EditPlanValidationIssue::InvalidRange,
        "wrong invalid range source diff validation issue");
    require(
        documents.text("top.sv") == "module top;\n",
        "invalid range source diff should not mutate document");
    return true;
}

bool sourceDiffPreservesSamePointInsertOrder() {
    MockWorkspaceDocumentManager documents;
    documents.openDocument("top.sv", "ac", DocumentVersion{7});

    const auto plan = rtledit::makeWorkspaceEditPlan(
        exposeIntent(),
        RiskLevel::Medium,
        PreviewPolicy::Diff,
        {
            edit("top.sv", 7, {0, 1}, {0, 1}, "", "1"),
            edit("top.sv", 7, {0, 1}, {0, 1}, "", "2"),
            edit("top.sv", 7, {0, 1}, {0, 1}, "", "3")
        });

    const auto diff = rtledit::buildWorkspaceEditSourceDiff(plan, documents);
    require(diff.built(), "same-point source diff should build");
    require(diff.files.size() == 1, "same-point source diff file count mismatch");
    require(
        diff.files.front().afterText == "a123c",
        "same-point source diff should preserve input order");
    require(
        documents.text("top.sv") == "ac",
        "source diff should not mutate document");
    return true;
}

bool sourceDiffRenderingReportsInvalidPlan() {
    MockWorkspaceDocumentManager documents;
    documents.openDocument("top.sv", "module top;\n", DocumentVersion{8});

    auto plan = rtledit::makeWorkspaceEditPlan(
        exposeIntent(),
        RiskLevel::Medium,
        PreviewPolicy::Diff,
        {edit("top.sv", 7, {0, 0}, {0, 0}, "", "x")});
    plan.provenance.clear();

    const auto rendered = rtledit::renderWorkspaceEditSourceDiff(plan, documents);
    require(
        rendered ==
            "status=invalid_plan files=0 file=top.sv message=Workspace edit plan provenance is missing an edit entry.\n"
            "invalid issue=provenance_missing file=top.sv edit=0 message=Workspace edit plan provenance is missing an edit entry.\n",
        "rendered invalid plan source diff mismatch");
    return true;
}

bool sourceDiffRenderingIsStable() {
    MockWorkspaceDocumentManager documents;
    documents.openDocument(
        "top.sv",
        "module top (\n"
        "    input logic clk\n"
        ");\n",
        DocumentVersion{7});

    const auto plan = rtledit::makeWorkspaceEditPlan(
        exposeIntent(),
        RiskLevel::Medium,
        PreviewPolicy::Diff,
        {
            edit("top.sv", 7, {1, 19}, {1, 19}, "", ","),
            edit("top.sv", 7, {2, 0}, {2, 0}, "", "    output logic done\n")
        });

    const auto rendered = rtledit::renderWorkspaceEditSourceDiff(plan, documents);
    require(
        rendered ==
            "status=built files=1\n"
            "file top.sv version=7 edits=2\n"
            "before\n"
            "---\n"
            "module top (\n"
            "    input logic clk\n"
            ");\n"
            "---\n"
            "after\n"
            "---\n"
            "module top (\n"
            "    input logic clk,\n"
            "    output logic done\n"
            ");\n"
            "---\n",
        "rendered source diff mismatch");
    return true;
}

bool sourceDiffHunkRenderingIsStable() {
    MockWorkspaceDocumentManager documents;
    documents.openDocument(
        "top.sv",
        "module top (\n"
        "    input logic clk\n"
        ");\n",
        DocumentVersion{7});

    const auto plan = rtledit::makeWorkspaceEditPlan(
        exposeIntent(),
        RiskLevel::Medium,
        PreviewPolicy::Diff,
        {
            edit("top.sv", 7, {1, 19}, {1, 19}, "", ","),
            edit("top.sv", 7, {2, 0}, {2, 0}, "", "    output logic done\n")
        });

    const auto rendered =
        rtledit::renderWorkspaceEditSourceDiffHunks(plan, documents);
    require(
        rendered ==
            "status=built files=1\n"
            "file top.sv version=7 edits=2 hunks=1\n"
            "@@ -1,3 +1,4 @@\n"
            " module top (\n"
            "-    input logic clk\n"
            "+    input logic clk,\n"
            "+    output logic done\n"
            " );\n",
        "rendered source diff hunks mismatch");
    return true;
}

bool sourceDiffHunkRenderingReportsInvalidPlan() {
    MockWorkspaceDocumentManager documents;
    documents.openDocument("top.sv", "module top;\n", DocumentVersion{8});

    auto plan = rtledit::makeWorkspaceEditPlan(
        exposeIntent(),
        RiskLevel::Medium,
        PreviewPolicy::Diff,
        {edit("top.sv", 7, {0, 0}, {0, 0}, "", "x")});
    plan.provenance.clear();

    const auto rendered =
        rtledit::renderWorkspaceEditSourceDiffHunks(plan, documents);
    require(
        rendered ==
            "status=invalid_plan files=0 file=top.sv message=Workspace edit plan provenance is missing an edit entry.\n"
            "invalid issue=provenance_missing file=top.sv edit=0 message=Workspace edit plan provenance is missing an edit entry.\n",
        "rendered invalid plan source diff hunks mismatch");
    return true;
}

bool sourceDiffRenderingReportsFailure() {
    MockWorkspaceDocumentManager documents;
    documents.openDocument("top.sv", "module top;\n", DocumentVersion{8});

    const auto plan = rtledit::makeWorkspaceEditPlan(
        exposeIntent(),
        RiskLevel::Medium,
        PreviewPolicy::Diff,
        {edit("top.sv", 7, {0, 10}, {0, 10}, "", " ()")});

    const auto rendered = rtledit::renderWorkspaceEditSourceDiff(plan, documents);
    require(
        rendered ==
            "status=stale files=0 file=top.sv message=Workspace edit plan is stale.\n"
            "stale reason=version_changed file=top.sv expected=7 actual=8\n",
        "rendered stale source diff mismatch");
    return true;
}

bool sourceDiffRenderingReportsSemanticSnapshotStale() {
    MockWorkspaceDocumentManager documents;
    documents.openDocument("top.sv", "module top;\n", DocumentVersion{7});

    auto plan = rtledit::makeWorkspaceEditPlan(
        exposeIntent(),
        RiskLevel::Medium,
        PreviewPolicy::Diff,
        {edit("top.sv", 7, {0, 10}, {0, 10}, "", " ()")});
    plan.semanticSnapshot = SemanticIndexSnapshot{"snapshot-a"};

    const auto rendered = rtledit::renderWorkspaceEditSourceDiff(
        plan,
        SemanticIndexSnapshot{"snapshot-b"},
        documents);
    require(
        rendered ==
            "status=stale files=0 message=Workspace edit plan is stale.\n"
            "semanticSnapshot=snapshot-a\n"
            "stale reason=semantic_snapshot_changed expectedSemanticSnapshot=snapshot-a actualSemanticSnapshot=snapshot-b\n",
        "rendered semantic-stale source diff mismatch");
    require(
        documents.text("top.sv") == "module top;\n",
        "semantic-stale source diff should not mutate document");
    return true;
}

bool staleStatusTracksMixedVersions() {
    MockWorkspaceDocumentManager documents;
    documents.openDocument("top.sv", "module top;\n", DocumentVersion{7});

    const auto plan = rtledit::makeWorkspaceEditPlan(
        exposeIntent(),
        RiskLevel::Medium,
        PreviewPolicy::Inline,
        {
            edit("top.sv", 7, {0, 10}, {0, 10}, "", " ("),
            edit("top.sv", 8, {0, 10}, {0, 10}, "", ")")
        });

    require(plan.hasMixedDocumentVersions, "plan should mark mixed versions");

    const auto stale = rtledit::staleStatus(plan, documents);
    require(stale.stale(), "mixed versions should be stale");
    require(
        stale.reason == EditPlanStaleReason::MixedDocumentVersions,
        "wrong mixed-version reason");
    require(stale.filePath == "top.sv", "mixed-version file mismatch");
    return true;
}

bool staleStatusScansMixedVersionsWhenCachedFlagIsStale() {
    MockWorkspaceDocumentManager documents;
    documents.openDocument("top.sv", "module top;\n", DocumentVersion{7});

    auto plan = rtledit::makeWorkspaceEditPlan(
        exposeIntent(),
        RiskLevel::Medium,
        PreviewPolicy::Inline,
        {
            edit("top.sv", 7, {0, 10}, {0, 10}, "", " ("),
            edit("top.sv", 8, {0, 10}, {0, 10}, "", ")")
        });
    plan.hasMixedDocumentVersions = false;

    const auto stale = rtledit::staleStatus(plan, documents);
    require(
        stale.reason == EditPlanStaleReason::MixedDocumentVersions,
        "stale status should scan actual edits for mixed versions");
    require(stale.filePath == "top.sv", "scanned mixed-version file mismatch");
    return true;
}

bool staleStatusTracksVersionChange() {
    MockWorkspaceDocumentManager documents;
    documents.openDocument("top.sv", "module top;\n", DocumentVersion{7});

    const auto plan = rtledit::makeWorkspaceEditPlan(
        exposeIntent(),
        RiskLevel::Medium,
        PreviewPolicy::Inline,
        {edit("top.sv", 7, {0, 10}, {0, 10}, "", " ()")});

    const auto fresh = rtledit::staleStatus(plan, documents);
    require(!fresh.stale(), "matching version should not be stale");

    documents.openDocument("top.sv", "module top;\n", DocumentVersion{8});
    const auto stale = rtledit::staleStatus(plan, documents);
    require(stale.stale(), "changed version should be stale");
    require(
        stale.reason == EditPlanStaleReason::VersionChanged,
        "wrong stale reason");
    require(stale.filePath == "top.sv", "stale file mismatch");
    require(stale.expectedVersion == DocumentVersion{7}, "expected version mismatch");
    require(stale.actualVersion == DocumentVersion{8}, "actual version mismatch");
    return true;
}

bool staleStatusTracksMissingDocument() {
    MockWorkspaceDocumentManager documents;

    const auto plan = rtledit::makeWorkspaceEditPlan(
        exposeIntent(),
        RiskLevel::Medium,
        PreviewPolicy::Inline,
        {edit("missing.sv", 3, {0, 0}, {0, 0}, "", "x")});

    const auto stale = rtledit::staleStatus(plan, documents);
    require(stale.stale(), "missing document should be stale");
    require(
        stale.reason == EditPlanStaleReason::DocumentMissing,
        "wrong missing-document reason");
    require(stale.filePath == "missing.sv", "missing file mismatch");
    require(stale.expectedVersion == DocumentVersion{3}, "missing expected version mismatch");
    return true;
}

bool staleStatusTracksSemanticSnapshotChange() {
    MockWorkspaceDocumentManager documents;
    documents.openDocument("top.sv", "module top;\n", DocumentVersion{7});

    auto plan = rtledit::makeWorkspaceEditPlan(
        exposeIntent(),
        RiskLevel::Medium,
        PreviewPolicy::Inline,
        {edit("top.sv", 7, {0, 10}, {0, 10}, "", " ()")});
    plan.semanticSnapshot = SemanticIndexSnapshot{"snapshot-a"};

    const auto fresh = rtledit::staleStatus(
        plan,
        SemanticIndexSnapshot{"snapshot-a"},
        documents);
    require(!fresh.stale(), "matching semantic snapshot should not be stale");

    const auto stale = rtledit::staleStatus(
        plan,
        SemanticIndexSnapshot{"snapshot-b"},
        documents);
    require(stale.stale(), "changed semantic snapshot should be stale");
    require(
        stale.reason == EditPlanStaleReason::SemanticSnapshotChanged,
        "wrong semantic snapshot stale reason");
    require(
        stale.expectedSemanticSnapshotId == "snapshot-a",
        "expected semantic snapshot mismatch");
    require(
        stale.actualSemanticSnapshotId == "snapshot-b",
        "actual semantic snapshot mismatch");
    return true;
}

bool planValidationAcceptsGeneratedPlan() {
    const auto plan = rtledit::makeWorkspaceEditPlan(
        exposeIntent(),
        RiskLevel::Medium,
        PreviewPolicy::Inline,
        {
            edit("top.sv", 7, {2, 21}, {2, 21}, "", ","),
            edit("top.sv", 7, {3, 0}, {3, 0}, "", "    output logic done\n")
        });

    const auto validation = rtledit::validateWorkspaceEditPlan(plan);
    require(validation.valid(), "generated plan should validate");
    require(
        validation.issue == EditPlanValidationIssue::None,
        "generated plan validation issue mismatch");
    return true;
}

bool planValidationRejectsMissingProvenance() {
    auto plan = rtledit::makeWorkspaceEditPlan(
        exposeIntent(),
        RiskLevel::Medium,
        PreviewPolicy::Inline,
        {
            edit("top.sv", 7, {2, 21}, {2, 21}, "", ","),
            edit("top.sv", 7, {3, 0}, {3, 0}, "", "    output logic done\n")
        });
    plan.provenance.pop_back();

    const auto validation = rtledit::validateWorkspaceEditPlan(plan);
    require(!validation.valid(), "missing provenance should fail validation");
    require(
        validation.issue == EditPlanValidationIssue::ProvenanceMissing,
        "wrong missing provenance validation issue");
    require(validation.editIndex == 1, "missing provenance edit index mismatch");
    require(validation.filePath == "top.sv", "missing provenance file mismatch");
    return true;
}

bool planValidationRejectsOutOfBoundsProvenance() {
    auto plan = rtledit::makeWorkspaceEditPlan(
        exposeIntent(),
        RiskLevel::Medium,
        PreviewPolicy::Inline,
        {edit("top.sv", 7, {0, 0}, {0, 0}, "", "x")});
    plan.provenance.front().editIndex = 1;

    const auto validation = rtledit::validateWorkspaceEditPlan(plan);
    require(!validation.valid(), "out-of-bounds provenance should fail validation");
    require(
        validation.issue == EditPlanValidationIssue::ProvenanceEditIndexOutOfBounds,
        "wrong out-of-bounds provenance validation issue");
    require(validation.editIndex == 1, "out-of-bounds provenance edit index mismatch");
    return true;
}

bool planValidationRejectsDuplicateProvenance() {
    auto plan = rtledit::makeWorkspaceEditPlan(
        exposeIntent(),
        RiskLevel::Medium,
        PreviewPolicy::Inline,
        {
            edit("top.sv", 7, {2, 21}, {2, 21}, "", ","),
            edit("top.sv", 7, {3, 0}, {3, 0}, "", "    output logic done\n")
        });
    plan.provenance.back().editIndex = 0;

    const auto validation = rtledit::validateWorkspaceEditPlan(plan);
    require(!validation.valid(), "duplicate provenance should fail validation");
    require(
        validation.issue == EditPlanValidationIssue::DuplicateProvenanceEditIndex,
        "wrong duplicate provenance validation issue");
    require(validation.editIndex == 0, "duplicate provenance edit index mismatch");
    require(validation.filePath == "top.sv", "duplicate provenance file mismatch");
    return true;
}

bool planValidationRejectsMissingBaseline() {
    auto plan = rtledit::makeWorkspaceEditPlan(
        exposeIntent(),
        RiskLevel::Medium,
        PreviewPolicy::Inline,
        {edit("top.sv", 7, {0, 0}, {0, 0}, "", "x")});
    plan.baselines.clear();

    const auto validation = rtledit::validateWorkspaceEditPlan(plan);
    require(!validation.valid(), "missing baseline should fail validation");
    require(
        validation.issue == EditPlanValidationIssue::MissingBaseline,
        "wrong missing baseline validation issue");
    require(validation.editIndex == 0, "missing baseline edit index mismatch");
    require(validation.filePath == "top.sv", "missing baseline file mismatch");
    return true;
}

bool planValidationRejectsEmptyFilePath() {
    const auto plan = rtledit::makeWorkspaceEditPlan(
        exposeIntent(),
        RiskLevel::Medium,
        PreviewPolicy::Inline,
        {edit("", 7, {0, 0}, {0, 0}, "", "x")});

    const auto validation = rtledit::validateWorkspaceEditPlan(plan);
    require(!validation.valid(), "empty file path should fail validation");
    require(
        validation.issue == EditPlanValidationIssue::EmptyFilePath,
        "wrong empty file path validation issue");
    require(validation.editIndex == 0, "empty file path edit index mismatch");
    require(validation.filePath.empty(), "empty file path should be reported as empty");
    return true;
}

bool planValidationRejectsInvalidRange() {
    const auto plan = rtledit::makeWorkspaceEditPlan(
        exposeIntent(),
        RiskLevel::Medium,
        PreviewPolicy::Inline,
        {edit("top.sv", 7, {1, 0}, {0, 0}, "", "x")});

    const auto validation = rtledit::validateWorkspaceEditPlan(plan);
    require(!validation.valid(), "invalid range should fail validation");
    require(
        validation.issue == EditPlanValidationIssue::InvalidRange,
        "wrong invalid range validation issue");
    require(validation.editIndex == 0, "invalid range edit index mismatch");
    require(validation.filePath == "top.sv", "invalid range file mismatch");
    return true;
}

bool planValidationRejectsMixedVersions() {
    const auto plan = rtledit::makeWorkspaceEditPlan(
        exposeIntent(),
        RiskLevel::Medium,
        PreviewPolicy::Inline,
        {
            edit("top.sv", 7, {0, 10}, {0, 10}, "", " ("),
            edit("top.sv", 8, {0, 10}, {0, 10}, "", ")")
        });

    const auto validation = rtledit::validateWorkspaceEditPlan(plan);
    require(!validation.valid(), "mixed versions should fail validation");
    require(
        validation.issue == EditPlanValidationIssue::MixedDocumentVersions,
        "wrong mixed-version validation issue");
    require(validation.filePath == "top.sv", "mixed-version validation file mismatch");
    return true;
}

bool planApplyAppliesFreshPlan() {
    MockWorkspaceDocumentManager documents;
    documents.openDocument(
        "top.sv",
        "module top (\n"
        "    input logic clk\n"
        ");\n",
        DocumentVersion{7});

    const auto plan = rtledit::makeWorkspaceEditPlan(
        exposeIntent(),
        RiskLevel::Medium,
        PreviewPolicy::Inline,
        {
            edit("top.sv", 7, {1, 19}, {1, 19}, "", ","),
            edit("top.sv", 7, {2, 0}, {2, 0}, "", "    output logic done\n")
        });

    const auto result = rtledit::applyWorkspaceEditPlan(plan, documents);
    require(result.applied(), "fresh plan should apply");
    require(result.status == PlanApplyStatus::Applied, "wrong applied status");
    require(result.patchResult.applied(), "patch result should be applied");
    require(result.semanticSnapshot.empty(), "direct apply snapshot should be empty");
    require(
        result.semanticIndexFilePaths.empty(),
        "direct apply semantic index files should be empty");
    require(
        documents.text("top.sv") ==
            "module top (\n"
            "    input logic clk,\n"
            "    output logic done\n"
            ");\n",
        "fresh plan apply output mismatch");
    return true;
}

bool planApplyRejectsEmptyFilePathBeforePatch() {
    MockWorkspaceDocumentManager documents;

    const auto plan = rtledit::makeWorkspaceEditPlan(
        exposeIntent(),
        RiskLevel::Medium,
        PreviewPolicy::Inline,
        {edit("", 7, {0, 0}, {0, 0}, "", "x")});

    const auto result = rtledit::applyWorkspaceEditPlan(plan, documents);
    require(!result.applied(), "empty file path plan should not apply");
    require(result.status == PlanApplyStatus::Invalid, "wrong empty path apply status");
    require(
        result.validationResult.issue == EditPlanValidationIssue::EmptyFilePath,
        "wrong empty path apply validation issue");
    require(
        result.patchResult.status == rtledit::ApplyStatus::DocumentApplyFailed,
        "wrong empty path apply patch status");
    return true;
}

bool planApplyRejectsInvalidPlanBeforePatch() {
    MockWorkspaceDocumentManager documents;
    documents.openDocument("top.sv", "module top;\n", DocumentVersion{7});

    auto plan = rtledit::makeWorkspaceEditPlan(
        exposeIntent(),
        RiskLevel::Medium,
        PreviewPolicy::Inline,
        {edit("top.sv", 7, {0, 0}, {0, 0}, "", "x")});
    plan.provenance.clear();

    const auto result = rtledit::applyWorkspaceEditPlan(plan, documents);
    require(!result.applied(), "invalid plan should not apply");
    require(result.status == PlanApplyStatus::Invalid, "wrong invalid apply status");
    require(
        result.validationResult.issue == EditPlanValidationIssue::ProvenanceMissing,
        "wrong invalid apply validation issue");
    require(
        result.patchResult.status == rtledit::ApplyStatus::DocumentApplyFailed,
        "wrong invalid apply patch status");
    require(
        documents.text("top.sv") == "module top;\n",
        "invalid plan should not change document");
    return true;
}

bool planApplyRejectsStalePlanBeforePatch() {
    MockWorkspaceDocumentManager documents;
    documents.openDocument("top.sv", "module top;\n", DocumentVersion{8});

    const auto plan = rtledit::makeWorkspaceEditPlan(
        exposeIntent(),
        RiskLevel::Medium,
        PreviewPolicy::Inline,
        {edit("top.sv", 7, {0, 10}, {0, 10}, "", " ()")});

    const auto result = rtledit::applyWorkspaceEditPlan(plan, documents);
    require(!result.applied(), "stale plan should not apply");
    require(result.status == PlanApplyStatus::Stale, "wrong stale apply status");
    require(
        result.staleStatus.reason == EditPlanStaleReason::VersionChanged,
        "wrong stale apply reason");
    require(
        documents.text("top.sv") == "module top;\n",
        "stale plan should not change document");
    return true;
}

bool planApplyRejectsSemanticSnapshotStaleBeforePatch() {
    MockWorkspaceDocumentManager documents;
    documents.openDocument("top.sv", "module top;\n", DocumentVersion{7});

    auto plan = rtledit::makeWorkspaceEditPlan(
        exposeIntent(),
        RiskLevel::Medium,
        PreviewPolicy::Inline,
        {edit("top.sv", 7, {0, 10}, {0, 10}, "", " ()")});
    plan.semanticSnapshot = SemanticIndexSnapshot{"snapshot-a"};

    const auto result = rtledit::applyWorkspaceEditPlan(
        plan,
        SemanticIndexSnapshot{"snapshot-b"},
        documents);
    require(!result.applied(), "semantic-stale plan should not apply");
    require(
        result.status == PlanApplyStatus::Stale,
        "wrong semantic-stale apply status");
    require(
        result.staleStatus.reason ==
            EditPlanStaleReason::SemanticSnapshotChanged,
        "wrong semantic-stale apply reason");
    require(
        result.patchResult.status == ApplyStatus::VersionMismatch,
        "wrong semantic-stale patch status");
    require(
        result.semanticSnapshot.id == "snapshot-a",
        "semantic-stale apply should preserve plan snapshot");
    require(
        result.semanticIndexFilePaths.empty(),
        "semantic-stale direct apply semantic index files should be empty");
    require(
        documents.text("top.sv") == "module top;\n",
        "semantic-stale plan should not change document");
    return true;
}

bool planApplyRenderingReportsAppliedPlan() {
    MockWorkspaceDocumentManager documents;
    documents.openDocument("top.sv", "module top;\n", DocumentVersion{7});

    auto plan = rtledit::makeWorkspaceEditPlan(
        exposeIntent(),
        RiskLevel::Medium,
        PreviewPolicy::Inline,
        {edit("top.sv", 7, {0, 10}, {0, 10}, "", " ()")});
    plan.semanticSnapshot = SemanticIndexSnapshot{"snapshot-a"};
    plan.semanticIndexFilePaths = {"top.sv"};

    const auto rendered =
        rtledit::renderWorkspaceEditPlanApplyResult(plan, documents);
    require(
        rendered ==
            "status=applied patch=applied message=Applied 1 edit to 1 document.\n"
            "semanticSnapshot=snapshot-a\n"
            "semanticIndexFiles=1\n"
            "- top.sv\n"
            "changedFiles=1\n"
            "- top.sv\n",
        "rendered applied plan apply result mismatch");
    require(
        documents.text("top.sv") == "module top ();\n",
        "rendered plan apply should apply edits");
    return true;
}

bool planApplyRenderingReportsSemanticSnapshotStale() {
    MockWorkspaceDocumentManager documents;
    documents.openDocument("top.sv", "module top;\n", DocumentVersion{7});

    auto plan = rtledit::makeWorkspaceEditPlan(
        exposeIntent(),
        RiskLevel::Medium,
        PreviewPolicy::Inline,
        {edit("top.sv", 7, {0, 10}, {0, 10}, "", " ()")});
    plan.semanticSnapshot = SemanticIndexSnapshot{"snapshot-a"};

    const auto rendered = rtledit::renderWorkspaceEditPlanApplyResult(
        plan,
        SemanticIndexSnapshot{"snapshot-b"},
        documents);
    require(
        rendered ==
            "status=stale patch=version_mismatch message=Workspace edit plan is stale.\n"
            "semanticSnapshot=snapshot-a\n"
            "stale reason=semantic_snapshot_changed expectedSemanticSnapshot=snapshot-a actualSemanticSnapshot=snapshot-b\n",
        "rendered semantic-stale plan apply result mismatch");
    require(
        documents.text("top.sv") == "module top;\n",
        "rendered semantic-stale plan apply should not mutate document");
    return true;
}

bool planApplyReportsPatchFailure() {
    MockWorkspaceDocumentManager documents;
    documents.openDocument("top.sv", "module top;\n", DocumentVersion{7});

    const auto plan = rtledit::makeWorkspaceEditPlan(
        exposeIntent(),
        RiskLevel::Medium,
        PreviewPolicy::Inline,
        {edit("top.sv", 7, {0, 0}, {0, 6}, "wrong", "package")});

    const auto result = rtledit::applyWorkspaceEditPlan(plan, documents);
    require(!result.applied(), "range mismatch should not apply");
    require(
        result.status == PlanApplyStatus::PatchFailed,
        "wrong patch-failure status");
    require(
        result.patchResult.status == rtledit::ApplyStatus::RangeTextMismatch,
        "wrong patch failure reason");
    require(
        documents.text("top.sv") == "module top;\n",
        "patch failure should not change document");
    return true;
}

}  // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<bool()>>> tests = {
        {"publicNameHelpersAreStable", publicNameHelpersAreStable},
        {"planCapturesBaselines", planCapturesBaselines},
        {"planCapturesExplicitProvenance", planCapturesExplicitProvenance},
        {"previewSummarizesPlanMetadata", previewSummarizesPlanMetadata},
        {"previewWithDocumentManagerReportsVersionStale", previewWithDocumentManagerReportsVersionStale},
        {"previewWithDocumentManagerReportsMissingDocumentStale", previewWithDocumentManagerReportsMissingDocumentStale},
        {"previewWithDocumentManagerReportsSemanticSnapshotStale", previewWithDocumentManagerReportsSemanticSnapshotStale},
        {"previewWithDocumentManagerRejectsInvalidPlanBeforeStale", previewWithDocumentManagerRejectsInvalidPlanBeforeStale},
        {"previewRejectsInvalidPlan", previewRejectsInvalidPlan},
        {"previewRejectsInvalidRangePlanBeforeSummaries", previewRejectsInvalidRangePlanBeforeSummaries},
        {"previewSummarizesMultipleFiles", previewSummarizesMultipleFiles},
        {"previewRenderingReportsInvalidPlan", previewRenderingReportsInvalidPlan},
        {"previewRenderingReportsStale", previewRenderingReportsStale},
        {"previewRenderingReportsSemanticSnapshotStale", previewRenderingReportsSemanticSnapshotStale},
        {"previewRenderingIsStable", previewRenderingIsStable},
        {"previewRenderingIncludesStructuredAnchorProvenance", previewRenderingIncludesStructuredAnchorProvenance},
        {"sourceDiffBuildsBeforeAfterText", sourceDiffBuildsBeforeAfterText},
        {"sourceDiffHunksAreEmptyForIdenticalText", sourceDiffHunksAreEmptyForIdenticalText},
        {"sourceDiffHunksSplitDistantChanges", sourceDiffHunksSplitDistantChanges},
        {"sourceDiffRejectsRangeTextMismatch", sourceDiffRejectsRangeTextMismatch},
        {"sourceDiffRejectsInvalidPlanBeforeDocumentChecks", sourceDiffRejectsInvalidPlanBeforeDocumentChecks},
        {"sourceDiffRejectsInvalidRangeAsInvalidPlan", sourceDiffRejectsInvalidRangeAsInvalidPlan},
        {"sourceDiffPreservesSamePointInsertOrder", sourceDiffPreservesSamePointInsertOrder},
        {"sourceDiffRenderingReportsInvalidPlan", sourceDiffRenderingReportsInvalidPlan},
        {"sourceDiffRenderingIsStable", sourceDiffRenderingIsStable},
        {"sourceDiffHunkRenderingIsStable", sourceDiffHunkRenderingIsStable},
        {"sourceDiffHunkRenderingReportsInvalidPlan", sourceDiffHunkRenderingReportsInvalidPlan},
        {"sourceDiffRenderingReportsFailure", sourceDiffRenderingReportsFailure},
        {"sourceDiffRenderingReportsSemanticSnapshotStale", sourceDiffRenderingReportsSemanticSnapshotStale},
        {"staleStatusTracksMixedVersions", staleStatusTracksMixedVersions},
        {"staleStatusScansMixedVersionsWhenCachedFlagIsStale", staleStatusScansMixedVersionsWhenCachedFlagIsStale},
        {"staleStatusTracksVersionChange", staleStatusTracksVersionChange},
        {"staleStatusTracksMissingDocument", staleStatusTracksMissingDocument},
        {"staleStatusTracksSemanticSnapshotChange", staleStatusTracksSemanticSnapshotChange},
        {"planValidationAcceptsGeneratedPlan", planValidationAcceptsGeneratedPlan},
        {"planValidationRejectsMissingProvenance", planValidationRejectsMissingProvenance},
        {"planValidationRejectsOutOfBoundsProvenance", planValidationRejectsOutOfBoundsProvenance},
        {"planValidationRejectsDuplicateProvenance", planValidationRejectsDuplicateProvenance},
        {"planValidationRejectsMissingBaseline", planValidationRejectsMissingBaseline},
        {"planValidationRejectsEmptyFilePath", planValidationRejectsEmptyFilePath},
        {"planValidationRejectsInvalidRange", planValidationRejectsInvalidRange},
        {"planValidationRejectsMixedVersions", planValidationRejectsMixedVersions},
        {"planApplyAppliesFreshPlan", planApplyAppliesFreshPlan},
        {"planApplyRejectsEmptyFilePathBeforePatch", planApplyRejectsEmptyFilePathBeforePatch},
        {"planApplyRejectsInvalidPlanBeforePatch", planApplyRejectsInvalidPlanBeforePatch},
        {"planApplyRejectsStalePlanBeforePatch", planApplyRejectsStalePlanBeforePatch},
        {"planApplyRejectsSemanticSnapshotStaleBeforePatch", planApplyRejectsSemanticSnapshotStaleBeforePatch},
        {"planApplyRenderingReportsAppliedPlan", planApplyRenderingReportsAppliedPlan},
        {"planApplyRenderingReportsSemanticSnapshotStale", planApplyRenderingReportsSemanticSnapshotStale},
        {"planApplyReportsPatchFailure", planApplyReportsPatchFailure},
    };

    int failures = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& ex) {
            ++failures;
            std::cerr << "[FAIL] " << name << ": " << ex.what() << '\n';
        }
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    return 0;
}
