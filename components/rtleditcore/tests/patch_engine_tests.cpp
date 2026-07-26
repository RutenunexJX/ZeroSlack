#include "rtledit/mock_workspace_document_manager.h"
#include "rtledit/patch_engine.h"
#include "rtledit/text_edit.h"

#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using rtledit::ApplyStatus;
using rtledit::DocumentVersion;
using rtledit::MockWorkspaceDocumentManager;
using rtledit::PatchEngine;
using rtledit::SourcePosition;
using rtledit::SourceRange;
using rtledit::WorkspaceDocumentManager;
using rtledit::WorkspaceDocumentSnapshot;
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

class PartiallyApplyingDocumentManager final
    : public WorkspaceDocumentManager {
public:
    struct Document {
        DocumentVersion version;
        std::string text;
    };

    void open(
        std::string filePath,
        std::string text,
        DocumentVersion version = DocumentVersion{1}) {
        documents_.insert_or_assign(
            std::move(filePath),
            Document{version, std::move(text)});
    }

    void failAfterApply(std::string filePath) {
        failAfterApplyFile_ = std::move(filePath);
    }

    void failRestore(std::string filePath) {
        failedRestoreFiles_.insert(std::move(filePath));
    }

    const Document& document(const std::string& filePath) const {
        return documents_.at(filePath);
    }

    const std::vector<std::string>& restoreCalls() const {
        return restoreCalls_;
    }

    std::optional<WorkspaceDocumentSnapshot> snapshot(
        const std::string& filePath) const override {
        const auto found = documents_.find(filePath);
        if (found == documents_.end()) {
            return std::nullopt;
        }
        return WorkspaceDocumentSnapshot{
            found->second.version,
            found->second.text};
    }

    bool applyTextEdits(
        const std::string& filePath,
        DocumentVersion expectedVersion,
        const std::vector<WorkspaceTextEdit>& editsInApplicationOrder)
        override {
        const auto found = documents_.find(filePath);
        if (found == documents_.end() ||
            found->second.version != expectedVersion) {
            return false;
        }
        const auto changed = rtledit::applyTextEditsToString(
            found->second.text,
            editsInApplicationOrder);
        if (!changed) {
            return false;
        }
        found->second.text = *changed;
        ++found->second.version.value;
        if (filePath == failAfterApplyFile_) {
            failAfterApplyFile_.clear();
            return false;
        }
        return true;
    }

    bool restoreSnapshot(
        const std::string& filePath,
        const WorkspaceDocumentSnapshot& snapshot) override {
        restoreCalls_.push_back(filePath);
        if (failedRestoreFiles_.find(filePath) != failedRestoreFiles_.end()) {
            return false;
        }
        const auto found = documents_.find(filePath);
        if (found == documents_.end()) {
            return false;
        }
        found->second = Document{snapshot.version, snapshot.text};
        return true;
    }

private:
    std::map<std::string, Document> documents_;
    std::string failAfterApplyFile_;
    std::set<std::string> failedRestoreFiles_;
    std::vector<std::string> restoreCalls_;
};

bool singlePointInsert() {
    MockWorkspaceDocumentManager documents;
    documents.openDocument("top.sv", "module top;\nendmodule\n");

    PatchEngine engine(documents);
    const auto result = engine.apply({
        edit("top.sv", 1, {1, 0}, {1, 0}, "", "  wire a;\n")
    });

    require(result.applied(), "single insert should apply");
    require(
        documents.text("top.sv") == "module top;\n  wire a;\nendmodule\n",
        "single insert produced unexpected text");
    require(documents.version("top.sv") == DocumentVersion{2}, "version should advance");
    return true;
}

bool multiPointDescendingInsert() {
    MockWorkspaceDocumentManager documents;
    documents.openDocument("top.sv", "abc\ndef\nghi\n");

    PatchEngine engine(documents);
    const auto result = engine.apply({
        edit("top.sv", 1, {0, 0}, {0, 0}, "", "A\n"),
        edit("top.sv", 1, {2, 0}, {2, 0}, "", "C\n")
    });

    require(result.applied(), "multi insert should apply");
    require(
        documents.text("top.sv") == "A\nabc\ndef\nC\nghi\n",
        "multi insert should apply from later ranges first");
    return true;
}

bool overlappingEditsReject() {
    MockWorkspaceDocumentManager documents;
    documents.openDocument("top.sv", "abcdef");

    PatchEngine engine(documents);
    const auto result = engine.apply({
        edit("top.sv", 1, {0, 1}, {0, 4}, "bcd", "X"),
        edit("top.sv", 1, {0, 3}, {0, 5}, "de", "Y")
    });

    require(!result.applied(), "overlap should be rejected");
    require(result.status == ApplyStatus::OverlappingEdits, "wrong overlap status");
    require(documents.text("top.sv") == "abcdef", "overlap rejection changed text");
    return true;
}

bool versionMismatchReject() {
    MockWorkspaceDocumentManager documents;
    documents.openDocument("top.sv", "abcdef", DocumentVersion{2});

    PatchEngine engine(documents);
    const auto result = engine.apply({
        edit("top.sv", 1, {0, 1}, {0, 1}, "", "X")
    });

    require(!result.applied(), "version mismatch should be rejected");
    require(result.status == ApplyStatus::VersionMismatch, "wrong version mismatch status");
    require(documents.text("top.sv") == "abcdef", "version rejection changed text");
    return true;
}

bool rangeTextMismatchReject() {
    MockWorkspaceDocumentManager documents;
    documents.openDocument("top.sv", "abcdef");

    PatchEngine engine(documents);
    const auto result = engine.apply({
        edit("top.sv", 1, {0, 1}, {0, 3}, "zz", "X")
    });

    require(!result.applied(), "range text mismatch should be rejected");
    require(result.status == ApplyStatus::RangeTextMismatch, "wrong text mismatch status");
    require(documents.text("top.sv") == "abcdef", "text mismatch rejection changed text");
    return true;
}

bool multiFileRangeMismatchRejectsBeforeAnyApply() {
    MockWorkspaceDocumentManager documents;
    documents.openDocument("a.sv", "alpha\n", DocumentVersion{1});
    documents.openDocument("b.sv", "beta\n", DocumentVersion{1});

    PatchEngine engine(documents);
    const auto result = engine.apply({
        edit("a.sv", 1, {0, 5}, {0, 5}, "", ";\n"),
        edit("b.sv", 1, {0, 0}, {0, 4}, "wrong", "BETA")
    });

    require(!result.applied(), "multi-file range mismatch should be rejected");
    require(
        result.status == ApplyStatus::RangeTextMismatch,
        "wrong multi-file range mismatch status");
    require(
        result.changedFiles.empty(),
        "preflight range mismatch should not report changed files");
    require(
        documents.text("a.sv") == "alpha\n",
        "valid earlier file changed before range mismatch rejection");
    require(
        documents.text("b.sv") == "beta\n",
        "range mismatch file changed before rejection");
    require(
        documents.version("a.sv") == DocumentVersion{1},
        "valid earlier file version changed before range mismatch rejection");
    require(
        documents.version("b.sv") == DocumentVersion{1},
        "range mismatch file version changed before rejection");
    return true;
}

bool multiFileVersionMismatchRejectsBeforeAnyApply() {
    MockWorkspaceDocumentManager documents;
    documents.openDocument("a.sv", "alpha\n", DocumentVersion{1});
    documents.openDocument("b.sv", "beta\n", DocumentVersion{2});

    PatchEngine engine(documents);
    const auto result = engine.apply({
        edit("a.sv", 1, {0, 5}, {0, 5}, "", ";\n"),
        edit("b.sv", 1, {0, 4}, {0, 4}, "", ";\n")
    });

    require(!result.applied(), "multi-file version mismatch should be rejected");
    require(
        result.status == ApplyStatus::VersionMismatch,
        "wrong multi-file version mismatch status");
    require(
        result.changedFiles.empty(),
        "preflight version mismatch should not report changed files");
    require(
        documents.text("a.sv") == "alpha\n",
        "valid earlier file changed before version mismatch rejection");
    require(
        documents.text("b.sv") == "beta\n",
        "version mismatch file changed before rejection");
    require(
        documents.version("a.sv") == DocumentVersion{1},
        "valid earlier file version changed before version mismatch rejection");
    require(
        documents.version("b.sv") == DocumentVersion{2},
        "version mismatch file version changed before rejection");
    return true;
}

bool multiFileMissingDocumentRejectsBeforeAnyApply() {
    MockWorkspaceDocumentManager documents;
    documents.openDocument("a.sv", "alpha\n", DocumentVersion{1});

    PatchEngine engine(documents);
    const auto result = engine.apply({
        edit("a.sv", 1, {0, 5}, {0, 5}, "", ";\n"),
        edit("b.sv", 1, {0, 0}, {0, 0}, "", "missing")
    });

    require(!result.applied(), "multi-file missing document should be rejected");
    require(
        result.status == ApplyStatus::DocumentNotFound,
        "wrong multi-file missing document status");
    require(
        result.changedFiles.empty(),
        "preflight missing document should not report changed files");
    require(
        documents.text("a.sv") == "alpha\n",
        "valid earlier file changed before missing document rejection");
    require(
        documents.version("a.sv") == DocumentVersion{1},
        "valid earlier file version changed before missing document rejection");
    require(
        !documents.hasDocument("b.sv"),
        "missing document rejection created a document");
    return true;
}

bool multiFileCommitFailureRollsBackPriorApplies() {
    MockWorkspaceDocumentManager documents;
    documents.openDocument("a.sv", "alpha\n", DocumentVersion{1});
    documents.openDocument("b.sv", "beta\n", DocumentVersion{1});
    documents.rejectNextApplyTextEdits("b.sv");

    PatchEngine engine(documents);
    const auto result = engine.apply({
        edit("a.sv", 1, {0, 5}, {0, 5}, "", ";\n"),
        edit("b.sv", 1, {0, 4}, {0, 4}, "", ";\n")
    });

    require(!result.applied(), "commit failure should reject the full apply");
    require(
        result.status == ApplyStatus::DocumentApplyFailed,
        "wrong commit failure status");
    require(
        result.changedFiles.empty(),
        "successful rollback should not report changed files");
    require(
        documents.text("a.sv") == "alpha\n",
        "prior successful apply was not rolled back");
    require(
        documents.text("b.sv") == "beta\n",
        "rejected file should remain unchanged");
    require(
        documents.version("a.sv") == DocumentVersion{1},
        "prior successful apply version was not rolled back");
    require(
        documents.version("b.sv") == DocumentVersion{1},
        "rejected file version should remain unchanged");
    return true;
}

bool partialCurrentFileFailureRollsBackCurrentThenPriorFiles() {
    PartiallyApplyingDocumentManager documents;
    documents.open("a.sv", "alpha\n");
    documents.open("b.sv", "beta\n");
    documents.failAfterApply("b.sv");

    PatchEngine engine(documents);
    const auto result = engine.apply({
        edit("a.sv", 1, {0, 5}, {0, 5}, "", ";\n"),
        edit("b.sv", 1, {0, 4}, {0, 4}, "", ";\n")
    });

    require(!result.applied(), "partial current-file failure should reject");
    require(
        result.status == ApplyStatus::DocumentApplyFailed,
        "partial current-file failure returned the wrong status");
    require(
        result.changedFiles.empty(),
        "successful full rollback should report no changed files");
    require(
        documents.document("a.sv").text == "alpha\n" &&
            documents.document("a.sv").version == DocumentVersion{1},
        "prior file was not restored after partial current-file failure");
    require(
        documents.document("b.sv").text == "beta\n" &&
            documents.document("b.sv").version == DocumentVersion{1},
        "current failed file was not restored after partial mutation");
    require(
        documents.restoreCalls() ==
            std::vector<std::string>{"b.sv", "a.sv"},
        "rollback must restore the current file before prior files in reverse order");
    return true;
}

bool restoreFailureReportsExactResidualFiles() {
    PartiallyApplyingDocumentManager documents;
    documents.open("a.sv", "alpha\n");
    documents.open("b.sv", "beta\n");
    documents.failAfterApply("b.sv");
    documents.failRestore("b.sv");

    PatchEngine engine(documents);
    const auto result = engine.apply({
        edit("a.sv", 1, {0, 5}, {0, 5}, "", ";\n"),
        edit("b.sv", 1, {0, 4}, {0, 4}, "", ";\n")
    });

    require(!result.applied(), "restore failure should reject");
    require(
        result.changedFiles == std::vector<std::string>{"b.sv"},
        "restore failure did not report the exact residual file");
    require(
        result.message.find("b.sv") != std::string::npos,
        "restore failure message omitted the failed file");
    require(
        documents.document("a.sv").text == "alpha\n",
        "successful prior-file restore was lost");
    require(
        documents.document("b.sv").text != "beta\n",
        "failed current-file restore unexpectedly changed the residual");
    return true;
}

bool sameInsertionPointStableOrder() {
    MockWorkspaceDocumentManager documents;
    documents.openDocument("top.sv", "ac");

    PatchEngine engine(documents);
    const auto result = engine.apply({
        edit("top.sv", 1, {0, 1}, {0, 1}, "", "1"),
        edit("top.sv", 1, {0, 1}, {0, 1}, "", "2"),
        edit("top.sv", 1, {0, 1}, {0, 1}, "", "3")
    });

    require(result.applied(), "same-point inserts should apply");
    require(
        documents.text("top.sv") == "a123c",
        "same-point inserts should preserve input order");
    return true;
}

}  // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<bool()>>> tests = {
        {"singlePointInsert", singlePointInsert},
        {"multiPointDescendingInsert", multiPointDescendingInsert},
        {"overlappingEditsReject", overlappingEditsReject},
        {"versionMismatchReject", versionMismatchReject},
        {"rangeTextMismatchReject", rangeTextMismatchReject},
        {"multiFileRangeMismatchRejectsBeforeAnyApply", multiFileRangeMismatchRejectsBeforeAnyApply},
        {"multiFileVersionMismatchRejectsBeforeAnyApply", multiFileVersionMismatchRejectsBeforeAnyApply},
        {"multiFileMissingDocumentRejectsBeforeAnyApply", multiFileMissingDocumentRejectsBeforeAnyApply},
        {"multiFileCommitFailureRollsBackPriorApplies", multiFileCommitFailureRollsBackPriorApplies},
        {"partialCurrentFileFailureRollsBackCurrentThenPriorFiles", partialCurrentFileFailureRollsBackCurrentThenPriorFiles},
        {"restoreFailureReportsExactResidualFiles", restoreFailureReportsExactResidualFiles},
        {"sameInsertionPointStableOrder", sameInsertionPointStableOrder},
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
