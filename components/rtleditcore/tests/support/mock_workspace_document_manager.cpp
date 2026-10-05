#include "rtledit/mock_workspace_document_manager.h"

#include "rtledit/text_edit.h"

#include <stdexcept>
#include <algorithm>

namespace rtledit {

void MockWorkspaceDocumentManager::openDocument(
    std::string filePath,
    std::string text,
    DocumentVersion version) {
    const auto existing = documents_.find(filePath);
    if (existing != documents_.end()) version.value = std::max(version.value, existing->second.version.value + 1);
    documents_[std::move(filePath)] = DocumentRecord{version, std::move(text)};
}

bool MockWorkspaceDocumentManager::hasDocument(const std::string& filePath) const {
    return documents_.find(filePath) != documents_.end();
}

const std::string& MockWorkspaceDocumentManager::text(
    const std::string& filePath) const {
    auto it = documents_.find(filePath);
    if (it == documents_.end()) {
        throw std::out_of_range("document is not open");
    }
    return it->second.text;
}

DocumentVersion MockWorkspaceDocumentManager::version(
    const std::string& filePath) const {
    auto it = documents_.find(filePath);
    if (it == documents_.end()) {
        throw std::out_of_range("document is not open");
    }
    return it->second.version;
}

void MockWorkspaceDocumentManager::rejectNextApplyTextEdits(std::string filePath) {
    ++rejectedApplyCounts_[std::move(filePath)];
}

std::optional<WorkspaceDocumentSnapshot> MockWorkspaceDocumentManager::snapshot(
    const std::string& filePath) const {
    auto it = documents_.find(filePath);
    if (it == documents_.end()) {
        return std::nullopt;
    }

    return WorkspaceDocumentSnapshot{it->second.version, it->second.text};
}

rtledit::DocumentMutationResult MockWorkspaceDocumentManager::applyTextEdits(
    const std::string& filePath,
    DocumentVersion expectedVersion,
    const std::vector<WorkspaceTextEdit>& editsInApplicationOrder) {
    auto rejectIt = rejectedApplyCounts_.find(filePath);
    if (rejectIt != rejectedApplyCounts_.end() && rejectIt->second > 0) {
        --rejectIt->second;
        return {};
    }

    auto it = documents_.find(filePath);
    if (it == documents_.end() || it->second.version != expectedVersion) {
        return {};
    }

    auto& text = it->second.text;
    const auto newText = applyTextEditsToString(text, editsInApplicationOrder);
    if (!newText) {
        return {};
    }
    text = *newText;

    if (!editsInApplicationOrder.empty()) {
        ++it->second.version.value;
    }

    return rtledit::DocumentMutationResult::completed(*this->snapshot(filePath));
}

rtledit::DocumentMutationResult MockWorkspaceDocumentManager::restoreSnapshot(
    const std::string& filePath,
    const rtledit::WorkspaceDocumentSnapshot& expectedCurrent,
        const rtledit::WorkspaceDocumentSnapshot& snapshot) {
        const auto currentBefore = this->snapshot(filePath);
        if (!currentBefore || currentBefore->version != expectedCurrent.version
            || currentBefore->text != expectedCurrent.text) return {};

    auto it = documents_.find(filePath);
    if (it == documents_.end()) {
        return {};
    }

    if (it->second.text != snapshot.text) {
        it->second.text = snapshot.text;
        ++it->second.version.value;
    }
    return rtledit::DocumentMutationResult::completed(*this->snapshot(filePath), currentBefore->text != snapshot.text);
}

}  // namespace rtledit
