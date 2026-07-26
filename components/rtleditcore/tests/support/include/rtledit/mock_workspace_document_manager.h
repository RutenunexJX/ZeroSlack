#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "rtledit/workspace_document_manager.h"

namespace rtledit {

class MockWorkspaceDocumentManager final : public WorkspaceDocumentManager {
public:
    void openDocument(
        std::string filePath,
        std::string text,
        DocumentVersion version = DocumentVersion{1});

    bool hasDocument(const std::string& filePath) const;
    const std::string& text(const std::string& filePath) const;
    DocumentVersion version(const std::string& filePath) const;
    void rejectNextApplyTextEdits(std::string filePath);

    std::optional<WorkspaceDocumentSnapshot> snapshot(
        const std::string& filePath) const override;

    bool applyTextEdits(
        const std::string& filePath,
        DocumentVersion expectedVersion,
        const std::vector<WorkspaceTextEdit>& editsInApplicationOrder) override;

    bool restoreSnapshot(
        const std::string& filePath,
        const WorkspaceDocumentSnapshot& snapshot) override;

private:
    struct DocumentRecord {
        DocumentVersion version;
        std::string text;
    };

    std::unordered_map<std::string, DocumentRecord> documents_;
    std::unordered_map<std::string, std::size_t> rejectedApplyCounts_;
};

}  // namespace rtledit
