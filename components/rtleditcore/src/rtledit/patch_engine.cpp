#include "rtledit/patch_engine.h"

#include "rtledit/text_edit.h"

#include <algorithm>
#include <map>
#include <sstream>
#include <string>
#include <utility>

namespace rtledit {
namespace {

struct FilePlan {
    DocumentVersion expectedVersion;
    WorkspaceDocumentSnapshot snapshot;
    std::vector<IndexedWorkspaceTextEdit> applicationEdits;
};

ApplyResult failure(ApplyStatus status, std::string message) {
    return ApplyResult{status, std::move(message), {}};
}

std::string versionString(DocumentVersion version) {
    return std::to_string(version.value);
}

std::string joinedFilePaths(
    const std::vector<std::string>& filePaths) {
    std::ostringstream output;
    for (std::size_t index = 0; index < filePaths.size(); ++index) {
        if (index != 0) {
            output << ", ";
        }
        output << filePaths[index];
    }
    return output.str();
}

}  // namespace

PatchEngine::PatchEngine(WorkspaceDocumentManager& documentManager)
    : documentManager_(documentManager) {}

ApplyResult PatchEngine::apply(const std::vector<WorkspaceTextEdit>& edits) {
    if (edits.empty()) {
        return ApplyResult{ApplyStatus::Applied, "No edits to apply.", {}};
    }

    std::map<std::string, std::vector<WorkspaceTextEdit>> editsByFile;
    for (const auto& edit : edits) {
        editsByFile[edit.filePath].push_back(edit);
    }

    std::map<std::string, FilePlan> plans;
    for (const auto& [filePath, fileEdits] : editsByFile) {
        if (filePath.empty()) {
            return failure(ApplyStatus::DocumentNotFound, "Edit has an empty file path.");
        }

        const auto snapshot = documentManager_.snapshot(filePath);
        if (!snapshot) {
            return failure(
                ApplyStatus::DocumentNotFound,
                "Document is not open: " + filePath);
        }

        const DocumentVersion expectedVersion =
            fileEdits.front().expectedDocumentVersion;
        for (const auto& edit : fileEdits) {
            if (edit.expectedDocumentVersion != expectedVersion) {
                return failure(
                    ApplyStatus::MixedDocumentVersions,
                    "Edits for one document use multiple baseline versions: " +
                        filePath);
            }
        }

        if (snapshot->version != expectedVersion) {
            return failure(
                ApplyStatus::VersionMismatch,
                "Document version mismatch for " + filePath + ": expected " +
                    versionString(expectedVersion) + ", actual " +
                    versionString(snapshot->version));
        }

        std::vector<IndexedWorkspaceTextEdit> indexedEdits;
        indexedEdits.reserve(fileEdits.size());

        const TextCoordinateIndex coordinateIndex(snapshot->text);
        for (std::size_t i = 0; i < fileEdits.size(); ++i) {
            const auto& edit = fileEdits[i];
            if (edit.range.end < edit.range.start) {
                return failure(
                    ApplyStatus::InvalidRange,
                    "Edit range end precedes start in " + filePath);
            }

            const auto offsets = coordinateIndex.offsets(edit.range);
            if (!offsets) {
                return failure(
                    ApplyStatus::RangeOutOfBounds,
                    "Edit range is outside document bounds in " + filePath);
            }

            const auto actualText = snapshot->text.substr(
                offsets->start,
                offsets->end - offsets->start);
            if (actualText != edit.expectedText) {
                return failure(
                    ApplyStatus::RangeTextMismatch,
                    "Range text mismatch in " + filePath);
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
                return failure(
                    ApplyStatus::OverlappingEdits,
                    "Overlapping edits in " + filePath);
            }
        }

        plans[filePath] = FilePlan{
            expectedVersion,
            *snapshot,
            buildIndexedTextEditApplicationOrder(std::move(indexedEdits))};
    }

    std::vector<std::string> changedFiles;
    std::vector<DocumentMutationReceipt> receipts;
    for (const auto& [filePath, plan] : plans) {
        const auto mutation = documentManager_.applyPreparedTextEdits(filePath, plan.snapshot, plan.applicationEdits);
        if (mutation.ownedState && mutation.modified)
            receipts.push_back({filePath, plan.snapshot, *mutation.ownedState});
        if (!mutation || !mutation.ownedState) {
            ApplyResult result = failure(ApplyStatus::DocumentApplyFailed,
                "Document manager rejected edits for " + filePath);
            if (mutation.modified && !mutation.ownedState) result.changedFiles.push_back(filePath);
            for (auto it = receipts.rbegin(); it != receipts.rend(); ++it) {
                const auto restored = documentManager_.restoreSnapshot(it->filePath, it->after, it->before);
                const auto verified = restored && restored.ownedState ? documentManager_.snapshot(it->filePath) : std::nullopt;
                if (verified && verified->version == restored.ownedState->version
                    && verified->text == restored.ownedState->text && verified->text == it->before.text) {
                    result.rolledBackChanges.push_back({it->filePath, it->before, *verified});
                } else {
                    result.changedFiles.push_back(it->filePath);
                }
            }
            result.documentChanges = std::move(receipts);
            result.message += result.changedFiles.empty()
                ? "; rolled back every state modified by this transaction."
                : "; rollback could not restore owned states; residual changed files: " + joinedFilePaths(result.changedFiles) + ".";
            return result;
        }
        if (mutation.modified) changedFiles.push_back(filePath);
    }

    std::ostringstream message;
    message << "Applied " << edits.size() << " edit";
    if (edits.size() != 1) {
        message << "s";
    }
    message << " to " << changedFiles.size() << " document";
    if (changedFiles.size() != 1) {
        message << "s";
    }
    message << ".";

    ApplyResult result{ApplyStatus::Applied, message.str(), std::move(changedFiles)};
    result.documentChanges = std::move(receipts);
    return result;
}

}  // namespace rtledit
