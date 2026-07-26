#include "rtledit/edit_plan.h"

#include "rtledit/patch_engine.h"
#include "rtledit/text_edit.h"

#include <algorithm>
#include <map>
#include <sstream>
#include <utility>

namespace rtledit {
namespace {

std::string mixedDocumentVersionFile(
    const std::vector<WorkspaceTextEdit>& edits) {
    std::map<std::string, DocumentVersion> byFile;
    for (const auto& edit : edits) {
        const auto [it, inserted] =
            byFile.emplace(edit.filePath, edit.expectedDocumentVersion);
        if (!inserted && it->second != edit.expectedDocumentVersion) {
            return edit.filePath;
        }
    }

    return "";
}

TextEditProvenance makeDefaultProvenance(
    std::size_t editIndex,
    SemanticEditKind kind) {
    TextEditProvenance provenance;
    provenance.editIndex = editIndex;
    provenance.actionId = semanticEditActionId(kind);
    return provenance;
}

std::vector<TextEditProvenance> defaultProvenance(
    const SemanticEditIntent& intent,
    const std::vector<WorkspaceTextEdit>& edits) {
    std::vector<TextEditProvenance> provenance;
    provenance.reserve(edits.size());

    for (std::size_t i = 0; i < edits.size(); ++i) {
        provenance.push_back(makeDefaultProvenance(i, intent.kind));
    }

    return provenance;
}

TextEditProvenance provenanceForEdit(
    const WorkspaceEditPlan& plan,
    std::size_t editIndex) {
    for (const auto& provenance : plan.provenance) {
        if (provenance.editIndex == editIndex) {
            return provenance;
        }
    }

    return makeDefaultProvenance(editIndex, plan.intent.kind);
}

WorkspaceEditPreview withPlanPreviewMetadata(
    WorkspaceEditPreview preview,
    const WorkspaceEditPlan& plan) {
    preview.semanticSnapshot = plan.semanticSnapshot;
    preview.semanticIndexFilePaths = plan.semanticIndexFilePaths;
    return preview;
}

WorkspaceEditSourceDiff withPlanSourceDiffMetadata(
    WorkspaceEditSourceDiff diff,
    const WorkspaceEditPlan& plan) {
    diff.semanticSnapshot = plan.semanticSnapshot;
    diff.semanticIndexFilePaths = plan.semanticIndexFilePaths;
    return diff;
}

PlanApplyResult withPlanApplyMetadata(
    PlanApplyResult result,
    const WorkspaceEditPlan& plan) {
    result.semanticSnapshot = plan.semanticSnapshot;
    result.semanticIndexFilePaths = plan.semanticIndexFilePaths;
    return result;
}

const char* editKindName(const PreviewEditSummary& edit) {
    if (edit.isInsertion) {
        return "insert";
    }
    if (edit.isDeletion) {
        return "delete";
    }
    if (edit.isReplacement) {
        return "replace";
    }
    return "empty";
}

void appendPosition(std::ostringstream& output, SourcePosition position) {
    output << position.line << ':' << position.column;
}

void appendRange(std::ostringstream& output, const SourceRange& range) {
    appendPosition(output, range.start);
    output << '-';
    appendPosition(output, range.end);
}

void appendStaleStatus(
    std::ostringstream& output,
    const EditPlanStaleStatus& staleStatus) {
    output << "stale reason="
           << editPlanStaleReasonName(staleStatus.reason);

    if (staleStatus.reason == EditPlanStaleReason::SemanticSnapshotChanged) {
        output << " expectedSemanticSnapshot="
               << staleStatus.expectedSemanticSnapshotId
               << " actualSemanticSnapshot="
               << staleStatus.actualSemanticSnapshotId
               << '\n';
        return;
    }

    output << " file=" << staleStatus.filePath
           << " expected=" << staleStatus.expectedVersion.value
           << " actual=" << staleStatus.actualVersion.value
           << '\n';
}

void appendSemanticMetadata(
    std::ostringstream& output,
    const SemanticIndexSnapshot& semanticSnapshot,
    const std::vector<std::string>& semanticIndexFilePaths) {
    if (!semanticSnapshot.empty()) {
        output << "semanticSnapshot=" << semanticSnapshot.id << '\n';
    }
    if (!semanticIndexFilePaths.empty()) {
        output << "semanticIndexFiles=" << semanticIndexFilePaths.size()
               << '\n';
        for (const auto& filePath : semanticIndexFilePaths) {
            output << "- " << filePath << '\n';
        }
    }
}

void appendValidationResult(
    std::ostringstream& output,
    const EditPlanValidationResult& validationResult) {
    output << "invalid issue="
           << editPlanValidationIssueName(validationResult.issue)
           << " file=" << validationResult.filePath
           << " edit=" << validationResult.editIndex;
    if (!validationResult.message.empty()) {
        output << " message=" << validationResult.message;
    }
    output << '\n';
}

void appendChangedFiles(
    std::ostringstream& output,
    const std::vector<std::string>& changedFiles) {
    if (changedFiles.empty()) {
        return;
    }

    output << "changedFiles=" << changedFiles.size() << '\n';
    for (const auto& filePath : changedFiles) {
        output << "- " << filePath << '\n';
    }
}

void appendTextBlock(
    std::ostringstream& output,
    const char* label,
    const std::string& text) {
    output << label << "\n---\n";
    output << text;
    if (text.empty() || text.back() != '\n') {
        output << '\n';
    }
    output << "---\n";
}

char sourceDiffLinePrefix(SourceDiffLineKind kind) {
    switch (kind) {
    case SourceDiffLineKind::Context:
        return ' ';
    case SourceDiffLineKind::Removed:
        return '-';
    case SourceDiffLineKind::Added:
        return '+';
    }

    return '?';
}

WorkspaceEditSourceDiff sourceDiffFailure(
    SourceDiffStatus status,
    std::string filePath,
    std::string message) {
    return WorkspaceEditSourceDiff{
        status,
        EditPlanStaleStatus{},
        EditPlanValidationResult{},
        std::move(filePath),
        std::move(message),
        {},
        SemanticIndexSnapshot{},
        {}};
}

WorkspaceEditSourceDiff sourceDiffFailureForPlan(
    const WorkspaceEditPlan& plan,
    SourceDiffStatus status,
    std::string filePath,
    std::string message) {
    return withPlanSourceDiffMetadata(
        sourceDiffFailure(status, std::move(filePath), std::move(message)),
        plan);
}

EditPlanValidationResult validationFailure(
    EditPlanValidationIssue issue,
    std::string filePath,
    std::size_t editIndex,
    std::string message) {
    return EditPlanValidationResult{
        issue,
        std::move(filePath),
        editIndex,
        std::move(message)};
}

EditPlanStaleStatus semanticSnapshotStaleStatus(
    const WorkspaceEditPlan& plan,
    const SemanticIndexSnapshot& currentSemanticSnapshot) {
    if (plan.semanticSnapshot.empty()) {
        return EditPlanStaleStatus{};
    }

    if (currentSemanticSnapshot.id == plan.semanticSnapshot.id) {
        return EditPlanStaleStatus{};
    }

    return EditPlanStaleStatus{
        EditPlanStaleReason::SemanticSnapshotChanged,
        "",
        DocumentVersion{},
        DocumentVersion{},
        plan.semanticSnapshot.id,
        currentSemanticSnapshot.id};
}

std::vector<std::string> splitDiffLines(const std::string& text) {
    std::vector<std::string> lines;
    if (text.empty()) {
        return lines;
    }

    std::size_t lineStart = 0;
    while (lineStart < text.size()) {
        const auto newline = text.find('\n', lineStart);
        auto lineEnd = newline == std::string::npos ? text.size() : newline;
        if (lineEnd > lineStart && text[lineEnd - 1] == '\r') {
            --lineEnd;
        }

        lines.push_back(text.substr(lineStart, lineEnd - lineStart));

        if (newline == std::string::npos) {
            break;
        }
        lineStart = newline + 1;
    }

    return lines;
}

std::vector<SourceDiffLine> fullReplacementLines(
    const std::vector<std::string>& beforeLines,
    const std::vector<std::string>& afterLines) {
    std::vector<SourceDiffLine> lines;
    lines.reserve(beforeLines.size() + afterLines.size());

    for (std::size_t index = 0; index < beforeLines.size(); ++index) {
        lines.push_back(SourceDiffLine{
            SourceDiffLineKind::Removed,
            index + 1,
            0,
            beforeLines[index]});
    }
    for (std::size_t index = 0; index < afterLines.size(); ++index) {
        lines.push_back(SourceDiffLine{
            SourceDiffLineKind::Added,
            0,
            index + 1,
            afterLines[index]});
    }

    return lines;
}

std::vector<SourceDiffLine> lineDiff(
    const std::vector<std::string>& beforeLines,
    const std::vector<std::string>& afterLines) {
    constexpr std::size_t maxLineProduct = 2000000;
    if (!beforeLines.empty() &&
        afterLines.size() > maxLineProduct / beforeLines.size()) {
        return fullReplacementLines(beforeLines, afterLines);
    }

    std::vector<std::vector<std::size_t>> lcs(
        beforeLines.size() + 1,
        std::vector<std::size_t>(afterLines.size() + 1, 0));

    for (std::size_t i = beforeLines.size(); i-- > 0;) {
        for (std::size_t j = afterLines.size(); j-- > 0;) {
            if (beforeLines[i] == afterLines[j]) {
                lcs[i][j] = lcs[i + 1][j + 1] + 1;
            } else {
                lcs[i][j] = std::max(lcs[i + 1][j], lcs[i][j + 1]);
            }
        }
    }

    std::vector<SourceDiffLine> lines;
    std::size_t beforeIndex = 0;
    std::size_t afterIndex = 0;
    while (beforeIndex < beforeLines.size() &&
           afterIndex < afterLines.size()) {
        if (beforeLines[beforeIndex] == afterLines[afterIndex]) {
            lines.push_back(SourceDiffLine{
                SourceDiffLineKind::Context,
                beforeIndex + 1,
                afterIndex + 1,
                beforeLines[beforeIndex]});
            ++beforeIndex;
            ++afterIndex;
            continue;
        }

        if (lcs[beforeIndex + 1][afterIndex] >=
            lcs[beforeIndex][afterIndex + 1]) {
            lines.push_back(SourceDiffLine{
                SourceDiffLineKind::Removed,
                beforeIndex + 1,
                0,
                beforeLines[beforeIndex]});
            ++beforeIndex;
        } else {
            lines.push_back(SourceDiffLine{
                SourceDiffLineKind::Added,
                0,
                afterIndex + 1,
                afterLines[afterIndex]});
            ++afterIndex;
        }
    }

    while (beforeIndex < beforeLines.size()) {
        lines.push_back(SourceDiffLine{
            SourceDiffLineKind::Removed,
            beforeIndex + 1,
            0,
            beforeLines[beforeIndex]});
        ++beforeIndex;
    }

    while (afterIndex < afterLines.size()) {
        lines.push_back(SourceDiffLine{
            SourceDiffLineKind::Added,
            0,
            afterIndex + 1,
            afterLines[afterIndex]});
        ++afterIndex;
    }

    return lines;
}

std::size_t firstOldLineOrInsertionPoint(
    const std::vector<SourceDiffLine>& lines,
    std::size_t hunkStart) {
    for (std::size_t index = hunkStart; index < lines.size(); ++index) {
        if (lines[index].oldLine != 0) {
            return lines[index].oldLine;
        }
    }

    std::size_t previousOldLine = 0;
    for (std::size_t index = 0; index < hunkStart; ++index) {
        if (lines[index].oldLine != 0) {
            previousOldLine = lines[index].oldLine;
        }
    }
    return previousOldLine + 1;
}

std::size_t firstNewLineOrInsertionPoint(
    const std::vector<SourceDiffLine>& lines,
    std::size_t hunkStart) {
    for (std::size_t index = hunkStart; index < lines.size(); ++index) {
        if (lines[index].newLine != 0) {
            return lines[index].newLine;
        }
    }

    std::size_t previousNewLine = 0;
    for (std::size_t index = 0; index < hunkStart; ++index) {
        if (lines[index].newLine != 0) {
            previousNewLine = lines[index].newLine;
        }
    }
    return previousNewLine + 1;
}

SourceDiffHunk makeHunk(
    const std::vector<SourceDiffLine>& lines,
    std::size_t hunkStart,
    std::size_t hunkEnd) {
    SourceDiffHunk hunk;
    hunk.oldStartLine = firstOldLineOrInsertionPoint(lines, hunkStart);
    hunk.newStartLine = firstNewLineOrInsertionPoint(lines, hunkStart);

    for (std::size_t index = hunkStart; index <= hunkEnd; ++index) {
        const auto& line = lines[index];
        if (line.kind != SourceDiffLineKind::Added) {
            ++hunk.oldLineCount;
        }
        if (line.kind != SourceDiffLineKind::Removed) {
            ++hunk.newLineCount;
        }
        hunk.lines.push_back(line);
    }

    return hunk;
}

}  // namespace

const char* editPlanStaleReasonName(EditPlanStaleReason reason) {
    switch (reason) {
    case EditPlanStaleReason::NotStale:
        return "not_stale";
    case EditPlanStaleReason::MixedDocumentVersions:
        return "mixed_document_versions";
    case EditPlanStaleReason::DocumentMissing:
        return "document_missing";
    case EditPlanStaleReason::VersionChanged:
        return "version_changed";
    case EditPlanStaleReason::SemanticSnapshotChanged:
        return "semantic_snapshot_changed";
    }

    return "unknown";
}

const char* editPlanValidationIssueName(EditPlanValidationIssue issue) {
    switch (issue) {
    case EditPlanValidationIssue::None:
        return "none";
    case EditPlanValidationIssue::EmptyFilePath:
        return "empty_file_path";
    case EditPlanValidationIssue::InvalidRange:
        return "invalid_range";
    case EditPlanValidationIssue::MixedDocumentVersions:
        return "mixed_document_versions";
    case EditPlanValidationIssue::MissingBaseline:
        return "missing_baseline";
    case EditPlanValidationIssue::BaselineVersionMismatch:
        return "baseline_version_mismatch";
    case EditPlanValidationIssue::ProvenanceMissing:
        return "provenance_missing";
    case EditPlanValidationIssue::ProvenanceEditIndexOutOfBounds:
        return "provenance_edit_index_out_of_bounds";
    case EditPlanValidationIssue::DuplicateProvenanceEditIndex:
        return "duplicate_provenance_edit_index";
    }

    return "unknown";
}

const char* previewStatusName(PreviewStatus status) {
    switch (status) {
    case PreviewStatus::Built:
        return "built";
    case PreviewStatus::InvalidPlan:
        return "invalid_plan";
    case PreviewStatus::Stale:
        return "stale";
    }

    return "unknown";
}

const char* sourceDiffStatusName(SourceDiffStatus status) {
    switch (status) {
    case SourceDiffStatus::Built:
        return "built";
    case SourceDiffStatus::InvalidPlan:
        return "invalid_plan";
    case SourceDiffStatus::Stale:
        return "stale";
    case SourceDiffStatus::InvalidRange:
        return "invalid_range";
    case SourceDiffStatus::RangeOutOfBounds:
        return "range_out_of_bounds";
    case SourceDiffStatus::RangeTextMismatch:
        return "range_text_mismatch";
    case SourceDiffStatus::OverlappingEdits:
        return "overlapping_edits";
    }

    return "unknown";
}

const char* sourceDiffLineKindName(SourceDiffLineKind kind) {
    switch (kind) {
    case SourceDiffLineKind::Context:
        return "context";
    case SourceDiffLineKind::Removed:
        return "removed";
    case SourceDiffLineKind::Added:
        return "added";
    }

    return "unknown";
}

const char* planApplyStatusName(PlanApplyStatus status) {
    switch (status) {
    case PlanApplyStatus::Applied:
        return "applied";
    case PlanApplyStatus::Invalid:
        return "invalid";
    case PlanApplyStatus::Stale:
        return "stale";
    case PlanApplyStatus::PatchFailed:
        return "patch_failed";
    }

    return "unknown";
}

std::vector<DocumentBaseline> collectDocumentBaselines(
    const std::vector<WorkspaceTextEdit>& edits) {
    std::map<std::string, DocumentVersion> byFile;
    for (const auto& edit : edits) {
        byFile.emplace(edit.filePath, edit.expectedDocumentVersion);
    }

    std::vector<DocumentBaseline> baselines;
    baselines.reserve(byFile.size());
    for (const auto& [filePath, version] : byFile) {
        baselines.push_back(DocumentBaseline{filePath, version});
    }
    return baselines;
}

WorkspaceEditPlan makeWorkspaceEditPlan(
    SemanticEditIntent intent,
    RiskLevel riskLevel,
    PreviewPolicy previewPolicy,
    std::vector<WorkspaceTextEdit> edits) {
    auto provenance = defaultProvenance(intent, edits);
    return makeWorkspaceEditPlan(
        std::move(intent),
        riskLevel,
        previewPolicy,
        std::move(edits),
        std::move(provenance));
}

WorkspaceEditPlan makeWorkspaceEditPlan(
    SemanticEditIntent intent,
    RiskLevel riskLevel,
    PreviewPolicy previewPolicy,
    std::vector<WorkspaceTextEdit> edits,
    std::vector<TextEditProvenance> provenance) {
    auto baselines = collectDocumentBaselines(edits);
    const bool hasMixedVersions = !mixedDocumentVersionFile(edits).empty();
    return WorkspaceEditPlan{
        std::move(intent),
        SemanticIndexSnapshot{},
        {},
        riskLevel,
        previewPolicy,
        hasMixedVersions,
        std::move(baselines),
        std::move(edits),
        std::move(provenance)};
}

EditPlanValidationResult validateWorkspaceEditPlan(
    const WorkspaceEditPlan& plan) {
    for (std::size_t i = 0; i < plan.edits.size(); ++i) {
        const auto& edit = plan.edits[i];
        if (edit.filePath.empty()) {
            return validationFailure(
                EditPlanValidationIssue::EmptyFilePath,
                edit.filePath,
                i,
                "Workspace edit plan contains an edit with an empty file path.");
        }

        if (edit.range.end < edit.range.start) {
            return validationFailure(
                EditPlanValidationIssue::InvalidRange,
                edit.filePath,
                i,
                "Workspace edit plan contains an edit whose range end precedes start.");
        }
    }

    const auto mixedFile = mixedDocumentVersionFile(plan.edits);
    if (plan.hasMixedDocumentVersions || !mixedFile.empty()) {
        return validationFailure(
            EditPlanValidationIssue::MixedDocumentVersions,
            mixedFile,
            0,
            "Workspace edit plan uses mixed document versions.");
    }

    std::map<std::string, DocumentVersion> baselinesByFile;
    for (const auto& baseline : plan.baselines) {
        const auto [it, inserted] =
            baselinesByFile.emplace(baseline.filePath, baseline.version);
        if (!inserted && it->second != baseline.version) {
            return validationFailure(
                EditPlanValidationIssue::BaselineVersionMismatch,
                baseline.filePath,
                0,
                "Workspace edit plan has conflicting baselines.");
        }
    }

    for (std::size_t i = 0; i < plan.edits.size(); ++i) {
        const auto& edit = plan.edits[i];
        const auto baselineIt = baselinesByFile.find(edit.filePath);
        if (baselineIt == baselinesByFile.end()) {
            return validationFailure(
                EditPlanValidationIssue::MissingBaseline,
                edit.filePath,
                i,
                "Workspace edit plan is missing a document baseline.");
        }

        if (baselineIt->second != edit.expectedDocumentVersion) {
            return validationFailure(
                EditPlanValidationIssue::BaselineVersionMismatch,
                edit.filePath,
                i,
                "Workspace edit plan baseline version does not match edit version.");
        }
    }

    std::vector<bool> seenProvenance(plan.edits.size(), false);
    for (const auto& provenance : plan.provenance) {
        if (provenance.editIndex >= plan.edits.size()) {
            return validationFailure(
                EditPlanValidationIssue::ProvenanceEditIndexOutOfBounds,
                "",
                provenance.editIndex,
                "Workspace edit plan provenance edit index is outside the edit list.");
        }

        if (seenProvenance[provenance.editIndex]) {
            return validationFailure(
                EditPlanValidationIssue::DuplicateProvenanceEditIndex,
                plan.edits[provenance.editIndex].filePath,
                provenance.editIndex,
                "Workspace edit plan provenance has duplicate edit index.");
        }

        seenProvenance[provenance.editIndex] = true;
    }

    for (std::size_t i = 0; i < seenProvenance.size(); ++i) {
        if (!seenProvenance[i]) {
            return validationFailure(
                EditPlanValidationIssue::ProvenanceMissing,
                plan.edits[i].filePath,
                i,
                "Workspace edit plan provenance is missing an edit entry.");
        }
    }

    return EditPlanValidationResult{};
}

EditPlanStaleStatus staleStatus(
    const WorkspaceEditPlan& plan,
    const WorkspaceDocumentManager& documentManager) {
    const auto mixedFile = mixedDocumentVersionFile(plan.edits);
    if (plan.hasMixedDocumentVersions || !mixedFile.empty()) {
        return EditPlanStaleStatus{
            EditPlanStaleReason::MixedDocumentVersions,
            mixedFile,
            DocumentVersion{},
            DocumentVersion{},
            "",
            ""};
    }

    for (const auto& baseline : plan.baselines) {
        const auto snapshot = documentManager.snapshot(baseline.filePath);
        if (!snapshot) {
            return EditPlanStaleStatus{
                EditPlanStaleReason::DocumentMissing,
                baseline.filePath,
                baseline.version,
                DocumentVersion{},
                "",
                ""};
        }

        if (snapshot->version != baseline.version) {
            return EditPlanStaleStatus{
                EditPlanStaleReason::VersionChanged,
                baseline.filePath,
                baseline.version,
                snapshot->version,
                "",
                ""};
        }
    }

    return EditPlanStaleStatus{};
}

EditPlanStaleStatus staleStatus(
    const WorkspaceEditPlan& plan,
    SemanticIndexSnapshot currentSemanticSnapshot,
    const WorkspaceDocumentManager& documentManager) {
    const auto semanticStale =
        semanticSnapshotStaleStatus(plan, currentSemanticSnapshot);
    if (semanticStale.stale()) {
        return semanticStale;
    }

    return staleStatus(plan, documentManager);
}

WorkspaceEditPreview buildWorkspaceEditPreview(const WorkspaceEditPlan& plan) {
    const auto validation = validateWorkspaceEditPlan(plan);
    if (!validation.valid()) {
        return withPlanPreviewMetadata(WorkspaceEditPreview{
            PreviewStatus::InvalidPlan,
            semanticEditActionId(plan.intent.kind),
            plan.riskLevel,
            plan.previewPolicy,
            validation,
            EditPlanStaleStatus{},
            0,
            0,
            {},
            {},
            SemanticIndexSnapshot{},
            {}},
            plan);
    }

    std::map<std::string, PreviewFileSummary> filesByPath;
    for (const auto& baseline : plan.baselines) {
        filesByPath.emplace(
            baseline.filePath,
            PreviewFileSummary{baseline.filePath, baseline.version, 0});
    }

    std::vector<PreviewEditSummary> editSummaries;
    editSummaries.reserve(plan.edits.size());

    for (std::size_t i = 0; i < plan.edits.size(); ++i) {
        const auto& edit = plan.edits[i];
        auto fileIt = filesByPath.find(edit.filePath);
        if (fileIt == filesByPath.end()) {
            fileIt = filesByPath.emplace(
                edit.filePath,
                PreviewFileSummary{
                    edit.filePath,
                    edit.expectedDocumentVersion,
                    0}).first;
        }
        ++fileIt->second.editCount;

        const bool emptyRange = isZeroLength(edit.range);
        const bool emptyNewText = edit.newText.empty();

        editSummaries.push_back(PreviewEditSummary{
            i,
            edit.filePath,
            edit.range,
            edit.expectedText.size(),
            edit.newText.size(),
            emptyRange && !emptyNewText,
            !emptyRange && emptyNewText,
            !emptyRange && !emptyNewText,
            provenanceForEdit(plan, i)});
    }

    std::vector<PreviewFileSummary> files;
    files.reserve(filesByPath.size());
    for (const auto& [_, summary] : filesByPath) {
        files.push_back(summary);
    }

    return withPlanPreviewMetadata(WorkspaceEditPreview{
        PreviewStatus::Built,
        semanticEditActionId(plan.intent.kind),
        plan.riskLevel,
        plan.previewPolicy,
        EditPlanValidationResult{},
        EditPlanStaleStatus{},
        files.size(),
        editSummaries.size(),
        std::move(files),
        std::move(editSummaries),
        SemanticIndexSnapshot{},
        {}},
        plan);
}

WorkspaceEditPreview buildWorkspaceEditPreview(
    const WorkspaceEditPlan& plan,
    const WorkspaceDocumentManager& documentManager) {
    const auto validation = validateWorkspaceEditPlan(plan);
    if (!validation.valid()) {
        return withPlanPreviewMetadata(WorkspaceEditPreview{
            PreviewStatus::InvalidPlan,
            semanticEditActionId(plan.intent.kind),
            plan.riskLevel,
            plan.previewPolicy,
            validation,
            EditPlanStaleStatus{},
            0,
            0,
            {},
            {},
            SemanticIndexSnapshot{},
            {}},
            plan);
    }

    const auto stale = staleStatus(plan, documentManager);
    if (stale.stale()) {
        return withPlanPreviewMetadata(WorkspaceEditPreview{
            PreviewStatus::Stale,
            semanticEditActionId(plan.intent.kind),
            plan.riskLevel,
            plan.previewPolicy,
            EditPlanValidationResult{},
            stale,
            0,
            0,
            {},
            {},
            SemanticIndexSnapshot{},
            {}},
            plan);
    }

    return buildWorkspaceEditPreview(plan);
}

WorkspaceEditPreview buildWorkspaceEditPreview(
    const WorkspaceEditPlan& plan,
    SemanticIndexSnapshot currentSemanticSnapshot,
    const WorkspaceDocumentManager& documentManager) {
    const auto validation = validateWorkspaceEditPlan(plan);
    if (!validation.valid()) {
        return withPlanPreviewMetadata(WorkspaceEditPreview{
            PreviewStatus::InvalidPlan,
            semanticEditActionId(plan.intent.kind),
            plan.riskLevel,
            plan.previewPolicy,
            validation,
            EditPlanStaleStatus{},
            0,
            0,
            {},
            {},
            SemanticIndexSnapshot{},
            {}},
            plan);
    }

    const auto stale =
        staleStatus(plan, std::move(currentSemanticSnapshot), documentManager);
    if (stale.stale()) {
        return withPlanPreviewMetadata(WorkspaceEditPreview{
            PreviewStatus::Stale,
            semanticEditActionId(plan.intent.kind),
            plan.riskLevel,
            plan.previewPolicy,
            EditPlanValidationResult{},
            stale,
            0,
            0,
            {},
            {},
            SemanticIndexSnapshot{},
            {}},
            plan);
    }

    return buildWorkspaceEditPreview(plan);
}

std::string renderWorkspaceEditPreview(const WorkspaceEditPreview& preview) {
    std::ostringstream output;
    if (preview.status != PreviewStatus::Built) {
        output << "status=" << previewStatusName(preview.status) << " ";
    }

    output << "action=" << preview.actionId
           << " risk=" << riskLevelName(preview.riskLevel)
           << " policy=" << previewPolicyName(preview.previewPolicy)
           << " files=" << preview.fileCount
           << " edits=" << preview.editCount << '\n';

    appendSemanticMetadata(
        output,
        preview.semanticSnapshot,
        preview.semanticIndexFilePaths);

    if (!preview.validationResult.valid()) {
        output << "invalid issue="
               << editPlanValidationIssueName(preview.validationResult.issue)
               << " file=" << preview.validationResult.filePath
               << " edit=" << preview.validationResult.editIndex;
        if (!preview.validationResult.message.empty()) {
            output << " message=" << preview.validationResult.message;
        }
        output << '\n';
    }

    if (preview.staleStatus.stale()) {
        appendStaleStatus(output, preview.staleStatus);
    }

    output << "files\n";
    for (const auto& file : preview.files) {
        output << "- " << file.filePath
               << " version=" << file.version.value
               << " edits=" << file.editCount << '\n';
    }

    output << "edits\n";
    for (const auto& edit : preview.edits) {
        output << "- #" << edit.editIndex
               << " " << editKindName(edit)
               << " " << edit.filePath << " ";
        appendRange(output, edit.range);
        output << " expected=" << edit.expectedTextSize
               << " new=" << edit.newTextSize
               << " action=" << edit.provenance.actionId;

        if (!edit.provenance.anchorName.empty()) {
            output << " anchor=" << edit.provenance.anchorName;
        }
        if (!edit.provenance.description.empty()) {
            output << " note=" << edit.provenance.description;
        }
        if (edit.provenance.anchor.source !=
            AnchorResolutionSource::Unspecified) {
            output << " anchorSource="
                   << anchorResolutionSourceName(edit.provenance.anchor.source);
        }
        if (!edit.provenance.anchor.resolver.empty()) {
            output << " resolver=" << edit.provenance.anchor.resolver;
        }
        if (!edit.provenance.anchor.semanticSnapshotId.empty()) {
            output << " anchorSnapshot="
                   << edit.provenance.anchor.semanticSnapshotId;
        }
        output << '\n';
    }

    return output.str();
}

std::string renderWorkspaceEditPreview(const WorkspaceEditPlan& plan) {
    return renderWorkspaceEditPreview(buildWorkspaceEditPreview(plan));
}

std::string renderWorkspaceEditPreview(
    const WorkspaceEditPlan& plan,
    const WorkspaceDocumentManager& documentManager) {
    return renderWorkspaceEditPreview(
        buildWorkspaceEditPreview(plan, documentManager));
}

std::string renderWorkspaceEditPreview(
    const WorkspaceEditPlan& plan,
    SemanticIndexSnapshot currentSemanticSnapshot,
    const WorkspaceDocumentManager& documentManager) {
    return renderWorkspaceEditPreview(
        buildWorkspaceEditPreview(
            plan,
            std::move(currentSemanticSnapshot),
            documentManager));
}

WorkspaceEditSourceDiff buildWorkspaceEditSourceDiff(
    const WorkspaceEditPlan& plan,
    const WorkspaceDocumentManager& documentManager) {
    const auto validation = validateWorkspaceEditPlan(plan);
    if (!validation.valid()) {
        return withPlanSourceDiffMetadata(WorkspaceEditSourceDiff{
            SourceDiffStatus::InvalidPlan,
            EditPlanStaleStatus{},
            validation,
            validation.filePath,
            validation.message,
            {},
            SemanticIndexSnapshot{},
            {}},
            plan);
    }

    const auto stale = staleStatus(plan, documentManager);
    if (stale.stale()) {
        return withPlanSourceDiffMetadata(WorkspaceEditSourceDiff{
            SourceDiffStatus::Stale,
            stale,
            EditPlanValidationResult{},
            stale.filePath,
            "Workspace edit plan is stale.",
            {},
            SemanticIndexSnapshot{},
            {}},
            plan);
    }

    std::map<std::string, std::vector<WorkspaceTextEdit>> editsByFile;
    for (const auto& edit : plan.edits) {
        editsByFile[edit.filePath].push_back(edit);
    }

    std::vector<SourceDiffFile> files;
    files.reserve(editsByFile.size());

    for (const auto& [filePath, fileEdits] : editsByFile) {
        const auto snapshot = documentManager.snapshot(filePath);
        if (!snapshot) {
            return withPlanSourceDiffMetadata(WorkspaceEditSourceDiff{
                SourceDiffStatus::Stale,
                EditPlanStaleStatus{
                    EditPlanStaleReason::DocumentMissing,
                    filePath,
                    fileEdits.empty() ? DocumentVersion{} :
                        fileEdits.front().expectedDocumentVersion,
                    DocumentVersion{},
                    "",
                    ""},
                EditPlanValidationResult{},
                filePath,
                "Document is missing.",
                {},
                SemanticIndexSnapshot{},
                {}},
                plan);
        }

        std::vector<IndexedWorkspaceTextEdit> indexedEdits;
        indexedEdits.reserve(fileEdits.size());

        for (std::size_t i = 0; i < fileEdits.size(); ++i) {
            const auto& edit = fileEdits[i];
            if (edit.range.end < edit.range.start) {
                return sourceDiffFailureForPlan(
                    plan,
                    SourceDiffStatus::InvalidRange,
                    filePath,
                    "Edit range end precedes start.");
            }

            const auto offsets = rangeToOffsets(snapshot->text, edit.range);
            if (!offsets) {
                return sourceDiffFailureForPlan(
                    plan,
                    SourceDiffStatus::RangeOutOfBounds,
                    filePath,
                    "Edit range is outside document bounds.");
            }

            const auto actualText = snapshot->text.substr(
                offsets->start,
                offsets->end - offsets->start);
            if (actualText != edit.expectedText) {
                return sourceDiffFailureForPlan(
                    plan,
                    SourceDiffStatus::RangeTextMismatch,
                    filePath,
                    "Range text mismatch.");
            }

            indexedEdits.push_back(IndexedWorkspaceTextEdit{edit, i, *offsets});
        }

        auto sortedByStart = indexedEdits;
        std::sort(sortedByStart.begin(), sortedByStart.end(),
            [](const IndexedWorkspaceTextEdit& lhs,
               const IndexedWorkspaceTextEdit& rhs) {
                if (lhs.offsets.start != rhs.offsets.start) {
                    return lhs.offsets.start < rhs.offsets.start;
                }
                if (lhs.offsets.end != rhs.offsets.end) {
                    return lhs.offsets.end < rhs.offsets.end;
                }
                return lhs.inputIndex < rhs.inputIndex;
            });

        for (std::size_t i = 1; i < sortedByStart.size(); ++i) {
            if (textEditRangesOverlap(sortedByStart[i - 1], sortedByStart[i])) {
                return sourceDiffFailureForPlan(
                    plan,
                    SourceDiffStatus::OverlappingEdits,
                    filePath,
                    "Overlapping edits.");
            }
        }

        const auto afterText = applyTextEditsToString(
            snapshot->text,
            buildTextEditApplicationOrder(std::move(indexedEdits)));
        if (!afterText) {
            return sourceDiffFailureForPlan(
                plan,
                SourceDiffStatus::RangeOutOfBounds,
                filePath,
                "Edit range is outside document bounds.");
        }

        files.push_back(SourceDiffFile{
            filePath,
            snapshot->version,
            fileEdits.size(),
            snapshot->text,
            *afterText,
            buildSourceDiffHunks(snapshot->text, *afterText)});
    }

    return withPlanSourceDiffMetadata(WorkspaceEditSourceDiff{
        SourceDiffStatus::Built,
        EditPlanStaleStatus{},
        EditPlanValidationResult{},
        "",
        "",
        std::move(files),
        SemanticIndexSnapshot{},
        {}},
        plan);
}

WorkspaceEditSourceDiff buildWorkspaceEditSourceDiff(
    const WorkspaceEditPlan& plan,
    SemanticIndexSnapshot currentSemanticSnapshot,
    const WorkspaceDocumentManager& documentManager) {
    const auto validation = validateWorkspaceEditPlan(plan);
    if (!validation.valid()) {
        return withPlanSourceDiffMetadata(WorkspaceEditSourceDiff{
            SourceDiffStatus::InvalidPlan,
            EditPlanStaleStatus{},
            validation,
            validation.filePath,
            validation.message,
            {},
            SemanticIndexSnapshot{},
            {}},
            plan);
    }

    const auto stale =
        staleStatus(plan, std::move(currentSemanticSnapshot), documentManager);
    if (stale.stale()) {
        return withPlanSourceDiffMetadata(WorkspaceEditSourceDiff{
            SourceDiffStatus::Stale,
            stale,
            EditPlanValidationResult{},
            stale.filePath,
            "Workspace edit plan is stale.",
            {},
            SemanticIndexSnapshot{},
            {}},
            plan);
    }

    return buildWorkspaceEditSourceDiff(plan, documentManager);
}

std::vector<SourceDiffHunk> buildSourceDiffHunks(
    const std::string& beforeText,
    const std::string& afterText,
    std::size_t contextLineCount) {
    if (beforeText == afterText) {
        return {};
    }

    const auto lines =
        lineDiff(splitDiffLines(beforeText), splitDiffLines(afterText));

    std::vector<std::size_t> changedLineIndexes;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (lines[index].kind != SourceDiffLineKind::Context) {
            changedLineIndexes.push_back(index);
        }
    }

    std::vector<SourceDiffHunk> hunks;
    std::size_t nextChanged = 0;
    while (nextChanged < changedLineIndexes.size()) {
        const auto firstChanged = changedLineIndexes[nextChanged];
        std::size_t hunkStart = firstChanged > contextLineCount ?
            firstChanged - contextLineCount :
            0;
        std::size_t hunkEnd =
            std::min(lines.size() - 1, firstChanged + contextLineCount);
        ++nextChanged;

        while (nextChanged < changedLineIndexes.size()) {
            const auto changed = changedLineIndexes[nextChanged];
            const auto nextStart = changed > contextLineCount ?
                changed - contextLineCount :
                0;
            if (nextStart > hunkEnd + 1) {
                break;
            }
            hunkEnd = std::min(lines.size() - 1, changed + contextLineCount);
            ++nextChanged;
        }

        hunks.push_back(makeHunk(lines, hunkStart, hunkEnd));
    }

    return hunks;
}

std::string renderWorkspaceEditSourceDiff(const WorkspaceEditSourceDiff& diff) {
    std::ostringstream output;
    output << "status=" << sourceDiffStatusName(diff.status)
           << " files=" << diff.files.size();

    if (!diff.filePath.empty()) {
        output << " file=" << diff.filePath;
    }
    if (!diff.message.empty()) {
        output << " message=" << diff.message;
    }
    output << '\n';

    appendSemanticMetadata(
        output,
        diff.semanticSnapshot,
        diff.semanticIndexFilePaths);

    if (diff.staleStatus.stale()) {
        appendStaleStatus(output, diff.staleStatus);
    }

    if (!diff.validationResult.valid()) {
        output << "invalid issue="
               << editPlanValidationIssueName(diff.validationResult.issue)
               << " file=" << diff.validationResult.filePath
               << " edit=" << diff.validationResult.editIndex;
        if (!diff.validationResult.message.empty()) {
            output << " message=" << diff.validationResult.message;
        }
        output << '\n';
    }

    for (const auto& file : diff.files) {
        output << "file " << file.filePath
               << " version=" << file.version.value
               << " edits=" << file.editCount << '\n';
        appendTextBlock(output, "before", file.beforeText);
        appendTextBlock(output, "after", file.afterText);
    }

    return output.str();
}

std::string renderWorkspaceEditSourceDiffHunks(
    const WorkspaceEditSourceDiff& diff) {
    std::ostringstream output;
    output << "status=" << sourceDiffStatusName(diff.status)
           << " files=" << diff.files.size();

    if (!diff.filePath.empty()) {
        output << " file=" << diff.filePath;
    }
    if (!diff.message.empty()) {
        output << " message=" << diff.message;
    }
    output << '\n';

    appendSemanticMetadata(
        output,
        diff.semanticSnapshot,
        diff.semanticIndexFilePaths);

    if (diff.staleStatus.stale()) {
        appendStaleStatus(output, diff.staleStatus);
    }

    if (!diff.validationResult.valid()) {
        output << "invalid issue="
               << editPlanValidationIssueName(diff.validationResult.issue)
               << " file=" << diff.validationResult.filePath
               << " edit=" << diff.validationResult.editIndex;
        if (!diff.validationResult.message.empty()) {
            output << " message=" << diff.validationResult.message;
        }
        output << '\n';
    }

    for (const auto& file : diff.files) {
        output << "file " << file.filePath
               << " version=" << file.version.value
               << " edits=" << file.editCount
               << " hunks=" << file.hunks.size() << '\n';
        for (const auto& hunk : file.hunks) {
            output << "@@ -" << hunk.oldStartLine << ','
                   << hunk.oldLineCount
                   << " +" << hunk.newStartLine << ','
                   << hunk.newLineCount << " @@\n";
            for (const auto& line : hunk.lines) {
                output << sourceDiffLinePrefix(line.kind) << line.text << '\n';
            }
        }
    }

    return output.str();
}

std::string renderWorkspaceEditSourceDiff(
    const WorkspaceEditPlan& plan,
    const WorkspaceDocumentManager& documentManager) {
    return renderWorkspaceEditSourceDiff(
        buildWorkspaceEditSourceDiff(plan, documentManager));
}

std::string renderWorkspaceEditSourceDiffHunks(
    const WorkspaceEditPlan& plan,
    const WorkspaceDocumentManager& documentManager) {
    return renderWorkspaceEditSourceDiffHunks(
        buildWorkspaceEditSourceDiff(plan, documentManager));
}

std::string renderWorkspaceEditSourceDiff(
    const WorkspaceEditPlan& plan,
    SemanticIndexSnapshot currentSemanticSnapshot,
    const WorkspaceDocumentManager& documentManager) {
    return renderWorkspaceEditSourceDiff(
        buildWorkspaceEditSourceDiff(
            plan,
            std::move(currentSemanticSnapshot),
            documentManager));
}

std::string renderWorkspaceEditSourceDiffHunks(
    const WorkspaceEditPlan& plan,
    SemanticIndexSnapshot currentSemanticSnapshot,
    const WorkspaceDocumentManager& documentManager) {
    return renderWorkspaceEditSourceDiffHunks(
        buildWorkspaceEditSourceDiff(
            plan,
            std::move(currentSemanticSnapshot),
            documentManager));
}

std::string renderWorkspaceEditPlanApplyResult(const PlanApplyResult& result) {
    std::ostringstream output;
    output << "status=" << planApplyStatusName(result.status)
           << " patch=" << applyStatusName(result.patchResult.status);
    if (!result.patchResult.message.empty()) {
        output << " message=" << result.patchResult.message;
    }
    output << '\n';

    appendSemanticMetadata(
        output,
        result.semanticSnapshot,
        result.semanticIndexFilePaths);

    if (result.staleStatus.stale()) {
        appendStaleStatus(output, result.staleStatus);
    }

    if (!result.validationResult.valid()) {
        appendValidationResult(output, result.validationResult);
    }

    appendChangedFiles(output, result.patchResult.changedFiles);

    return output.str();
}

std::string renderWorkspaceEditPlanApplyResult(
    const WorkspaceEditPlan& plan,
    WorkspaceDocumentManager& documentManager) {
    return renderWorkspaceEditPlanApplyResult(
        applyWorkspaceEditPlan(plan, documentManager));
}

std::string renderWorkspaceEditPlanApplyResult(
    const WorkspaceEditPlan& plan,
    SemanticIndexSnapshot currentSemanticSnapshot,
    WorkspaceDocumentManager& documentManager) {
    return renderWorkspaceEditPlanApplyResult(
        applyWorkspaceEditPlan(
            plan,
            std::move(currentSemanticSnapshot),
            documentManager));
}

PlanApplyResult applyWorkspaceEditPlan(
    const WorkspaceEditPlan& plan,
    WorkspaceDocumentManager& documentManager) {
    const auto validation = validateWorkspaceEditPlan(plan);
    if (!validation.valid()) {
        return withPlanApplyMetadata(PlanApplyResult{
            PlanApplyStatus::Invalid,
            EditPlanStaleStatus{},
            ApplyResult{
                ApplyStatus::DocumentApplyFailed,
                validation.message,
                {}},
            validation,
            SemanticIndexSnapshot{},
            {}},
            plan);
    }

    const auto stale = staleStatus(plan, documentManager);
    if (stale.stale()) {
        return withPlanApplyMetadata(PlanApplyResult{
            PlanApplyStatus::Stale,
            stale,
            ApplyResult{
                ApplyStatus::VersionMismatch,
                "Workspace edit plan is stale.",
                {}},
            EditPlanValidationResult{},
            SemanticIndexSnapshot{},
            {}},
            plan);
    }

    PatchEngine patchEngine(documentManager);
    auto patchResult = patchEngine.apply(plan.edits);
    if (!patchResult.applied()) {
        return withPlanApplyMetadata(PlanApplyResult{
            PlanApplyStatus::PatchFailed,
            EditPlanStaleStatus{},
            std::move(patchResult),
            EditPlanValidationResult{},
            SemanticIndexSnapshot{},
            {}},
            plan);
    }

    return withPlanApplyMetadata(PlanApplyResult{
        PlanApplyStatus::Applied,
        EditPlanStaleStatus{},
        std::move(patchResult),
        EditPlanValidationResult{},
        SemanticIndexSnapshot{},
        {}},
        plan);
}

PlanApplyResult applyWorkspaceEditPlan(
    const WorkspaceEditPlan& plan,
    SemanticIndexSnapshot currentSemanticSnapshot,
    WorkspaceDocumentManager& documentManager) {
    const auto validation = validateWorkspaceEditPlan(plan);
    if (!validation.valid()) {
        return withPlanApplyMetadata(PlanApplyResult{
            PlanApplyStatus::Invalid,
            EditPlanStaleStatus{},
            ApplyResult{
                ApplyStatus::DocumentApplyFailed,
                validation.message,
                {}},
            validation,
            SemanticIndexSnapshot{},
            {}},
            plan);
    }

    const auto stale =
        staleStatus(plan, std::move(currentSemanticSnapshot), documentManager);
    if (stale.stale()) {
        return withPlanApplyMetadata(PlanApplyResult{
            PlanApplyStatus::Stale,
            stale,
            ApplyResult{
                ApplyStatus::VersionMismatch,
                "Workspace edit plan is stale.",
                {}},
            EditPlanValidationResult{},
            SemanticIndexSnapshot{},
            {}},
            plan);
    }

    return applyWorkspaceEditPlan(plan, documentManager);
}

}  // namespace rtledit
