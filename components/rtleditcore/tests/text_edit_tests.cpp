#include "rtledit/text_edit.h"

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using rtledit::DocumentVersion;
using rtledit::IndexedWorkspaceTextEdit;
using rtledit::SourcePosition;
using rtledit::SourceRange;
using rtledit::TextOffsetRange;
using rtledit::WorkspaceTextEdit;

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

WorkspaceTextEdit edit(
    SourcePosition start,
    SourcePosition end,
    std::string expectedText,
    std::string newText) {
    return WorkspaceTextEdit{
        "top.sv",
        DocumentVersion{1},
        SourceRange{start, end},
        std::move(expectedText),
        std::move(newText)};
}

bool positionAndRangeOffsets() {
    const std::string text = "abc\ndef\n";

    const auto startLine1 = rtledit::positionToOffset(text, SourcePosition{1, 0});
    require(startLine1.has_value(), "line 1 offset should exist");
    require(*startLine1 == 4, "line 1 offset mismatch");

    const auto endOfDocument = rtledit::positionToOffset(text, SourcePosition{2, 0});
    require(endOfDocument.has_value(), "end offset should exist");
    require(*endOfDocument == text.size(), "end offset mismatch");

    const auto range = rtledit::rangeToOffsets(
        text,
        SourceRange{SourcePosition{0, 1}, SourcePosition{1, 2}});
    require(range.has_value(), "range offset should exist");
    require(range->start == 1, "range start offset mismatch");
    require(range->end == 6, "range end offset mismatch");

    const auto invalid = rtledit::rangeToOffsets(
        text,
        SourceRange{SourcePosition{1, 0}, SourcePosition{0, 0}});
    require(!invalid.has_value(), "reversed range should be invalid");
    return true;
}

bool overlapDetectionAllowsSamePointInserts() {
    const IndexedWorkspaceTextEdit first{
        edit({0, 1}, {0, 1}, "", "A"),
        0,
        TextOffsetRange{1, 1}};
    const IndexedWorkspaceTextEdit second{
        edit({0, 1}, {0, 1}, "", "B"),
        1,
        TextOffsetRange{1, 1}};
    const IndexedWorkspaceTextEdit replace{
        edit({0, 1}, {0, 3}, "bc", "X"),
        2,
        TextOffsetRange{1, 3}};
    const IndexedWorkspaceTextEdit overlap{
        edit({0, 2}, {0, 4}, "cd", "Y"),
        3,
        TextOffsetRange{2, 4}};
    const IndexedWorkspaceTextEdit adjacent{
        edit({0, 3}, {0, 4}, "d", "Z"),
        4,
        TextOffsetRange{3, 4}};

    require(
        !rtledit::textEditRangesOverlap(first, second),
        "same-point inserts should not overlap");
    require(
        rtledit::textEditRangesOverlap(replace, overlap),
        "intersecting ranges should overlap");
    require(
        !rtledit::textEditRangesOverlap(replace, adjacent),
        "adjacent ranges should not overlap");
    return true;
}

bool applicationOrderMergesSamePointInserts() {
    std::vector<IndexedWorkspaceTextEdit> indexed = {
        IndexedWorkspaceTextEdit{
            edit({0, 1}, {0, 1}, "", "1"),
            0,
            TextOffsetRange{1, 1}},
        IndexedWorkspaceTextEdit{
            edit({0, 3}, {0, 3}, "", "X"),
            1,
            TextOffsetRange{3, 3}},
        IndexedWorkspaceTextEdit{
            edit({0, 1}, {0, 1}, "", "2"),
            2,
            TextOffsetRange{1, 1}},
        IndexedWorkspaceTextEdit{
            edit({0, 1}, {0, 1}, "", "3"),
            3,
            TextOffsetRange{1, 1}},
    };

    const auto ordered = rtledit::buildTextEditApplicationOrder(std::move(indexed));
    require(ordered.size() == 2, "same-point inserts should merge");
    require(ordered.front().newText == "X", "later insert should apply first");
    require(ordered.back().newText == "123", "same-point insert order mismatch");
    return true;
}

bool applyTextEditsToStringAppliesOrderedEdits() {
    const std::vector<WorkspaceTextEdit> ordered = {
        edit({0, 3}, {0, 3}, "", "X"),
        edit({0, 1}, {0, 1}, "", "123"),
    };

    const auto result = rtledit::applyTextEditsToString("abcd", ordered);
    require(result.has_value(), "ordered edits should apply");
    require(*result == "a123bcXd", "ordered edit output mismatch");

    const std::vector<WorkspaceTextEdit> invalid = {
        edit({9, 0}, {9, 0}, "", "x"),
    };
    require(
        !rtledit::applyTextEditsToString("abcd", invalid).has_value(),
        "out-of-bounds edit should fail");
    return true;
}

}  // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<bool()>>> tests = {
        {"positionAndRangeOffsets", positionAndRangeOffsets},
        {"overlapDetectionAllowsSamePointInserts", overlapDetectionAllowsSamePointInserts},
        {"applicationOrderMergesSamePointInserts", applicationOrderMergesSamePointInserts},
        {"applyTextEditsToStringAppliesOrderedEdits", applyTextEditsToStringAppliesOrderedEdits},
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
