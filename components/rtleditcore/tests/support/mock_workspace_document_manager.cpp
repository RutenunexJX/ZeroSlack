#include "rtledit/mock_workspace_document_manager.h"

#include "rtledit/text_edit.h"

#include <stdexcept>

namespace rtledit {

void MockWorkspaceDocumentManager::openDocument(
    std::string filePath,
    std::string text,
    DocumentVersion version) {
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

bool MockWorkspaceDocumentManager::applyTextEdits(
    const std::string& filePath,
    DocumentVersion expectedVersion,
    const std::vector<WorkspaceTextEdit>& editsInApplicationOrder) {
    auto rejectIt = rejectedApplyCounts_.find(filePath);
    if (rejectIt != rejectedApplyCounts_.end() && rejectIt->second > 0) {
        --rejectIt->second;
        return false;
    }

    auto it = documents_.find(filePath);
    if (it == documents_.end() || it->second.version != expectedVersion) {
        return false;
    }

    auto& text = it->second.text;
    const auto newText = applyTextEditsToString(text, editsInApplicationOrder);
    if (!newText) {
        return false;
    }
    text = *newText;

    if (!editsInApplicationOrder.empty()) {
        ++it->second.version.value;
    }

    return true;
}

bool MockWorkspaceDocumentManager::restoreSnapshot(
    const std::string& filePath,
    const WorkspaceDocumentSnapshot& snapshot) {
    auto it = documents_.find(filePath);
    if (it == documents_.end()) {
        return false;
    }

    it->second = DocumentRecord{snapshot.version, snapshot.text};
    return true;
}

}  // namespace rtledit
