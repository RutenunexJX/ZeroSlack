#include "rtledit/edit_plan.h"
#include "rtledit/expose_signal_to_top.h"
#include "rtledit/mock_workspace_document_manager.h"

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace rtledit;

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

SourcePosition positionAt(const std::string& text, std::size_t offset) {
    SourcePosition result;
    require(offset <= text.size(), "test offset outside text");
    for (std::size_t index = 0; index < offset; ++index) {
        if (text[index] == '\n') {
            ++result.line;
            result.column = 0;
        } else {
            ++result.column;
        }
    }
    return result;
}

std::size_t after(const std::string& text, const std::string& needle) {
    const auto found = text.find(needle);
    require(found != std::string::npos, "test needle missing");
    return found + needle.size();
}

std::size_t before(const std::string& text, const std::string& needle) {
    const auto found = text.find(needle);
    require(found != std::string::npos, "test needle missing");
    return found;
}

StructuredInsertionAnchor anchor(
    const std::string& filePath,
    std::uint64_t version,
    const std::string& text,
    std::size_t offset,
    std::string prefix = {},
    std::string suffix = {}) {
    const auto position = positionAt(text, offset);
    return StructuredInsertionAnchor{
        filePath,
        DocumentVersion{version},
        SourceRange{position, position},
        "",
        std::move(prefix),
        std::move(suffix),
        AnchorProvenance{
            AnchorResolutionSource::TreeSitter,
            "tree-sitter",
            "42"}};
}

ExposeSignalPortSite addedPort(
    std::string moduleName,
    std::string instancePath,
    StructuredInsertionAnchor insertion,
    StructuredInsertionAnchor delimiter = {}) {
    return ExposeSignalPortSite{
        ExposeEndpointState::Add,
        std::move(moduleName),
        std::move(instancePath),
        "payload_out",
        "logic [7:0]",
        std::move(insertion),
        std::move(delimiter)};
}

ExposeSignalConnectionSite addedConnection(
    std::string childModule,
    std::string childInstance,
    std::string childPath,
    std::string parentModule,
    std::string parentPath,
    StructuredInsertionAnchor insertion,
    StructuredInsertionAnchor delimiter = {}) {
    return ExposeSignalConnectionSite{
        ExposeEndpointState::Add,
        std::move(childModule),
        std::move(childInstance),
        std::move(childPath),
        std::move(parentModule),
        std::move(parentPath),
        "payload_out",
        "payload_out",
        std::move(insertion),
        std::move(delimiter)};
}

ExposeSignalToTopRequest actionRequest(
    std::vector<std::pair<std::string, std::uint64_t>> versions,
    std::string sourcePath = "top.u_child") {
    ExposeSignalToTopRequest request;
    request.signalSemanticId = SemanticObjectId{
        SemanticObjectKind::Signal,
        "child.payload",
        "child",
        "child.sv",
        SourceRange{SourcePosition{3, 16}, SourcePosition{3, 23}},
        "signal-42"};
    request.sourceInstancePath = std::move(sourcePath);
    request.targetAncestorInstancePath = "top";
    request.exportedPortName = "payload_out";
    request.semanticGeneration = 42;
    for (auto& [filePath, version] : versions) {
        request.documentVersions.push_back(
            {std::move(filePath), DocumentVersion{version}});
    }
    return request;
}

struct SingleLayerFixture {
    std::string child =
        "module child(\n"
        "    input logic clk\n"
        ");\n"
        "    logic [7:0] payload;\n"
        "endmodule\n";
    std::string top =
        "module top(\n"
        ");\n"
        "    child u_child(\n"
        "        .clk(clk)\n"
        "    );\n"
        "endmodule\n";

    ExposeSignalToTopRequest request() const {
        return actionRequest({{"child.sv", 3}, {"top.sv", 7}});
    }

    ResolvedExposeSignalToTop facts() const {
        ResolvedExposeSignalToTop facts;
        facts.semanticGeneration = 42;
        facts.signalName = "payload";
        facts.sourceModuleName = "child";
        facts.sourceInstancePath = "top.u_child";
        facts.targetAncestorInstancePath = "top";
        facts.renderedPortDataType = "logic [7:0]";
        facts.bitWidth = 8;
        facts.sourcePort = addedPort(
            "child",
            "top.u_child",
            anchor(
                "child.sv",
                3,
                child,
                after(child, "    input logic clk"),
                "\n    "),
            anchor(
                "child.sv",
                3,
                child,
                after(child, "    input logic clk")));
        facts.sourceBridge.state = ExposeEndpointState::Add;
        facts.sourceBridge.moduleName = "child";
        facts.sourceBridge.outputPortName = "payload_out";
        facts.sourceBridge.signalName = "payload";
        facts.sourceBridge.insertion = anchor(
            "child.sv",
            3,
            child,
            after(child, "    logic [7:0] payload;"),
            "\n    ");

        SignalPropagationStep step;
        step.index = 0;
        step.childModuleName = "child";
        step.childInstanceName = "u_child";
        step.childInstancePath = "top.u_child";
        step.parentModuleName = "top";
        step.parentInstancePath = "top";
        step.instanceSourceFilePath = "top.sv";
        step.instanceSourceRange =
            SourceRange{SourcePosition{2, 4}, SourcePosition{4, 6}};
        step.parentPort = addedPort(
            "top",
            "top",
            anchor(
                "top.sv",
                7,
                top,
                before(top, ");"),
                "    ",
                "\n"));
        step.connection = addedConnection(
            "child",
            "u_child",
            "top.u_child",
            "top",
            "top",
            anchor(
                "top.sv",
                7,
                top,
                after(top, "        .clk(clk)"),
                "\n        "),
            anchor(
                "top.sv",
                7,
                top,
                after(top, "        .clk(clk)")));
        facts.path.steps.push_back(std::move(step));
        facts.impact = {
            {"child", "child.sv", {"top.u_child", "other.u_child"}},
            {"top", "top.sv", {"top"}}};
        facts.semanticIndexFilePaths = {"child.sv", "top.sv"};
        return facts;
    }
};

bool singleLayerBuildsAndApplies() {
    const SingleLayerFixture fixture;
    const auto result =
        planExposeSignalToTop(fixture.request(), fixture.facts());
    require(result.ready(), "single-layer plan should be ready");
    require(result.plan.workspaceEdit.edits.size() == 6,
            "single-layer plan should have six edits");
    require(result.plan.path.steps.size() == 1,
            "single-layer path should have one step");
    require(result.plan.impact.front().instancePaths.size() == 2,
            "impact should include all module instances");

    MockWorkspaceDocumentManager documents;
    documents.openDocument(
        "child.sv", fixture.child, DocumentVersion{3});
    documents.openDocument("top.sv", fixture.top, DocumentVersion{7});
    const auto apply = applyWorkspaceEditPlan(
        result.plan.workspaceEdit,
        SemanticIndexSnapshot{"42"},
        documents);
    require(apply.applied(), "single-layer plan should apply");
    require(
        documents.text("child.sv") ==
            "module child(\n"
            "    input logic clk,\n"
            "    output logic [7:0] payload_out\n"
            ");\n"
            "    logic [7:0] payload;\n"
            "    assign payload_out = payload;\n"
            "endmodule\n",
        "source bridge result mismatch");
    require(
        documents.text("top.sv") ==
            "module top(\n"
            "    output logic [7:0] payload_out\n"
            ");\n"
            "    child u_child(\n"
            "        .clk(clk),\n"
            "        .payload_out(payload_out)\n"
            "    );\n"
            "endmodule\n",
        "top propagation result mismatch");
    return true;
}

bool threeLayerCrossFilePlanIsComplete() {
    const SingleLayerFixture fixture;
    const std::string mid =
        "module mid(\n"
        "    input logic clk\n"
        ");\n"
        "    child u_child(\n"
        "        .clk(clk)\n"
        "    );\n"
        "endmodule\n";
    const std::string top =
        "module top(\n"
        ");\n"
        "    mid u_mid(\n"
        "        .clk(clk)\n"
        "    );\n"
        "endmodule\n";

    auto facts = fixture.facts();
    facts.sourceInstancePath = "top.u_mid.u_child";
    facts.path.steps.clear();
    facts.impact.clear();

    SignalPropagationStep childToMid;
    childToMid.index = 0;
    childToMid.childModuleName = "child";
    childToMid.childInstanceName = "u_child";
    childToMid.childInstancePath = "top.u_mid.u_child";
    childToMid.parentModuleName = "mid";
    childToMid.parentInstancePath = "top.u_mid";
    childToMid.instanceSourceFilePath = "mid.sv";
    childToMid.parentPort = addedPort(
        "mid",
        "top.u_mid",
        anchor(
            "mid.sv", 5, mid, after(mid, "    input logic clk"), "\n    "),
        anchor("mid.sv", 5, mid, after(mid, "    input logic clk")));
    childToMid.connection = addedConnection(
        "child",
        "u_child",
        "top.u_mid.u_child",
        "mid",
        "top.u_mid",
        anchor(
            "mid.sv", 5, mid, after(mid, "        .clk(clk)"), "\n        "),
        anchor("mid.sv", 5, mid, after(mid, "        .clk(clk)")));

    SignalPropagationStep midToTop;
    midToTop.index = 1;
    midToTop.childModuleName = "mid";
    midToTop.childInstanceName = "u_mid";
    midToTop.childInstancePath = "top.u_mid";
    midToTop.parentModuleName = "top";
    midToTop.parentInstancePath = "top";
    midToTop.instanceSourceFilePath = "top.sv";
    midToTop.parentPort = addedPort(
        "top",
        "top",
        anchor("top.sv", 9, top, before(top, ");"), "    ", "\n"));
    midToTop.connection = addedConnection(
        "mid",
        "u_mid",
        "top.u_mid",
        "top",
        "top",
        anchor(
            "top.sv", 9, top, after(top, "        .clk(clk)"), "\n        "),
        anchor("top.sv", 9, top, after(top, "        .clk(clk)")));
    facts.path.steps = {childToMid, midToTop};
    facts.impact = {
        {"child", "child.sv", {"top.u_mid.u_child"}},
        {"mid", "mid.sv", {"top.u_mid"}},
        {"top", "top.sv", {"top"}}};
    facts.semanticIndexFilePaths = {"child.sv", "mid.sv", "top.sv"};

    const auto result = planExposeSignalToTop(
        actionRequest(
            {{"child.sv", 3}, {"mid.sv", 5}, {"top.sv", 9}},
            "top.u_mid.u_child"),
        facts);
    require(result.ready(), "three-layer plan should be ready");
    require(result.plan.workspaceEdit.baselines.size() == 3,
            "three-layer plan should cover three files");
    require(result.plan.workspaceEdit.edits.size() == 10,
            "three-layer plan should have all port and connection edits");

    MockWorkspaceDocumentManager documents;
    documents.openDocument(
        "child.sv", fixture.child, DocumentVersion{3});
    documents.openDocument("mid.sv", mid, DocumentVersion{5});
    documents.openDocument("top.sv", top, DocumentVersion{9});
    require(
        applyWorkspaceEditPlan(
            result.plan.workspaceEdit,
            SemanticIndexSnapshot{"42"},
            documents)
            .applied(),
        "three-layer plan should apply");
    require(
        documents.text("mid.sv").find(".payload_out(payload_out)") !=
            std::string::npos,
        "middle connection missing");
    require(
        documents.text("top.sv").find(".payload_out(payload_out)") !=
            std::string::npos,
        "top connection missing");
    return true;
}

bool compatibleEndpointsAreIdempotent() {
    const SingleLayerFixture fixture;
    auto facts = fixture.facts();
    facts.sourcePort.state = ExposeEndpointState::Reuse;
    facts.sourcePort.insertion = {};
    facts.sourcePort.delimiterInsertion = {};
    facts.sourceBridge.state = ExposeEndpointState::Reuse;
    facts.sourceBridge.insertion = {};
    facts.path.steps.front().parentPort.state = ExposeEndpointState::Reuse;
    facts.path.steps.front().parentPort.insertion = {};
    facts.path.steps.front().connection.state = ExposeEndpointState::Reuse;
    facts.path.steps.front().connection.insertion = {};
    facts.path.steps.front().connection.delimiterInsertion = {};
    const auto result = planExposeSignalToTop(fixture.request(), facts);
    require(result.ready(), "compatible endpoints should be reusable");
    require(result.plan.workspaceEdit.edits.empty(),
            "idempotent plan should emit no edits");
    return true;
}

bool bridgeStateIsIndependentFromPortState() {
    const SingleLayerFixture fixture;
    auto facts = fixture.facts();
    facts.sourcePort.state = ExposeEndpointState::Reuse;
    facts.sourcePort.insertion = {};
    facts.sourcePort.delimiterInsertion = {};
    const auto result =
        planExposeSignalToTop(fixture.request(), facts);
    require(result.ready(),
            "reused source port with a missing bridge should be plannable");
    bool foundBridge = false;
    bool foundSourcePort = false;
    for (std::size_t index = 0;
         index < result.plan.workspaceEdit.edits.size();
         ++index) {
        const auto& provenance =
            result.plan.workspaceEdit.provenance.at(index);
        foundBridge = foundBridge
            || provenance.anchorName == "source.bridge";
        foundSourcePort = foundSourcePort
            || (provenance.anchorName == "port.insert"
                && !provenance.hierarchyStepIndex);
    }
    require(foundBridge,
            "independent Add bridge did not emit an assignment");
    require(!foundSourcePort,
            "reused source port emitted a duplicate port");
    return true;
}

bool incompatibleSourceDriverRejects() {
    const SingleLayerFixture fixture;
    auto facts = fixture.facts();
    facts.sourcePort.state = ExposeEndpointState::Reuse;
    facts.sourcePort.insertion = {};
    facts.sourcePort.delimiterInsertion = {};
    facts.sourceBridge.state = ExposeEndpointState::Conflict;
    facts.sourceBridge.conflictReason =
        ExposeSignalFailureReason::DriverConflict;
    facts.sourceBridge.conflictMessage =
        "payload_out is driven by unrelated";
    facts.sourceBridge.insertion = {};
    const auto result =
        planExposeSignalToTop(fixture.request(), facts);
    require(!result.ready(), "incompatible source driver should reject");
    require(
        result.failureReason == ExposeSignalFailureReason::DriverConflict,
        "incompatible source driver returned the wrong failure");
    return true;
}

bool typedFailuresRejectWithoutPartialPlan() {
    const SingleLayerFixture fixture;
    auto conflict = fixture.facts();
    conflict.path.steps.front().parentPort.state =
        ExposeEndpointState::Conflict;
    conflict.path.steps.front().parentPort.conflictReason =
        ExposeSignalFailureReason::NameConflict;
    conflict.path.steps.front().parentPort.conflictMessage =
        "top.payload_out already names an internal signal";
    const auto conflictResult =
        planExposeSignalToTop(fixture.request(), conflict);
    require(!conflictResult.ready(), "name conflict should reject");
    require(
        conflictResult.failureReason ==
            ExposeSignalFailureReason::NameConflict,
        "name conflict reason lost");
    require(conflictResult.plan.workspaceEdit.edits.empty(),
            "rejected plan contains partial edits");

    auto stale = fixture.facts();
    stale.semanticGeneration = 43;
    const auto staleResult =
        planExposeSignalToTop(fixture.request(), stale);
    require(
        staleResult.failureReason ==
            ExposeSignalFailureReason::StaleSemanticGeneration,
        "stale generation reason lost");
    return true;
}

bool expectedTextFailureAndMidApplyFailureAreAtomic() {
    const SingleLayerFixture fixture;
    const auto result =
        planExposeSignalToTop(fixture.request(), fixture.facts());
    require(result.ready(), "fixture plan should be ready");
    auto expectedTextPlan = result.plan.workspaceEdit;
    require(!expectedTextPlan.edits.empty(),
            "fixture should contain an edit");
    expectedTextPlan.edits.front().expectedText = "stale anchor text";

    std::string changed = fixture.child;
    changed.replace(
        changed.find("input logic clk"),
        std::string("input logic clk").size(),
        "input logic rst");
    MockWorkspaceDocumentManager mismatched;
    mismatched.openDocument("child.sv", changed, DocumentVersion{3});
    mismatched.openDocument("top.sv", fixture.top, DocumentVersion{7});
    const auto mismatch = applyWorkspaceEditPlan(
        expectedTextPlan,
        SemanticIndexSnapshot{"42"},
        mismatched);
    require(
        mismatch.patchResult.status == ApplyStatus::RangeTextMismatch,
        "expected text mismatch not reported");
    require(mismatched.text("child.sv") == changed &&
                mismatched.text("top.sv") == fixture.top,
            "preflight failure changed a file");

    MockWorkspaceDocumentManager rejected;
    rejected.openDocument(
        "child.sv", fixture.child, DocumentVersion{3});
    rejected.openDocument("top.sv", fixture.top, DocumentVersion{7});
    rejected.rejectNextApplyTextEdits("top.sv");
    const auto rollback = applyWorkspaceEditPlan(
        result.plan.workspaceEdit,
        SemanticIndexSnapshot{"42"},
        rejected);
    require(!rollback.applied(), "mid-apply failure should reject");
    require(rejected.text("child.sv") == fixture.child &&
                rejected.text("top.sv") == fixture.top,
            "mid-apply failure did not roll back all files");
    require(rollback.patchResult.changedFiles.empty(),
            "successful rollback reports changed files");
    return true;
}

bool sourceDiffIsDeterministic() {
    const SingleLayerFixture fixture;
    const auto first =
        planExposeSignalToTop(fixture.request(), fixture.facts());
    const auto second =
        planExposeSignalToTop(fixture.request(), fixture.facts());
    MockWorkspaceDocumentManager documents;
    documents.openDocument(
        "child.sv", fixture.child, DocumentVersion{3});
    documents.openDocument("top.sv", fixture.top, DocumentVersion{7});
    const auto render = [&documents](const ExposeSignalToTopPlanResult& result) {
        return renderWorkspaceEditSourceDiffHunks(
            buildWorkspaceEditSourceDiff(
                result.plan.workspaceEdit,
                SemanticIndexSnapshot{"42"},
                documents));
    };
    require(first.ready() && second.ready() && render(first) == render(second),
            "equivalent facts produced nondeterministic diff");
    return true;
}

}  // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<bool()>>> tests = {
        {"singleLayerBuildsAndApplies", singleLayerBuildsAndApplies},
        {"threeLayerCrossFilePlanIsComplete", threeLayerCrossFilePlanIsComplete},
        {"compatibleEndpointsAreIdempotent", compatibleEndpointsAreIdempotent},
        {"typedFailuresRejectWithoutPartialPlan", typedFailuresRejectWithoutPartialPlan},
        {"expectedTextFailureAndMidApplyFailureAreAtomic", expectedTextFailureAndMidApplyFailureAreAtomic},
        {"sourceDiffIsDeterministic", sourceDiffIsDeterministic},
        {"bridgeStateIsIndependentFromPortState", bridgeStateIsIndependentFromPortState},
        {"incompatibleSourceDriverRejects", incompatibleSourceDriverRejects},
    };
    int failures = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
        }
    }
    return failures == 0 ? 0 : 1;
}
