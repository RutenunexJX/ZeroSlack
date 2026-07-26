#include "rtledit/rtledit.h"
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

SemanticObjectId signalId(SemanticObjectKind kind = SemanticObjectKind::Signal) {
    return SemanticObjectId{
        kind,
        "top.payload_out",
        "top",
        "top.sv",
        SourceRange{SourcePosition{0, 0}, SourcePosition{0, 7}},
        "signal-7"};
}

bool umbrellaSupportsTypedExposePlanner() {
    ExposeSignalToTopRequest request;
    request.signalSemanticId = signalId(SemanticObjectKind::Port);
    request.sourceInstancePath = "top";
    request.targetAncestorInstancePath = "top";
    request.exportedPortName = "payload_out";
    request.semanticGeneration = 7;
    request.documentVersions.push_back(
        CapturedDocumentVersion{"top.sv", DocumentVersion{3}});

    ResolvedExposeSignalToTop resolved;
    resolved.semanticGeneration = 7;
    resolved.signalName = "payload_out";
    resolved.sourceModuleName = "top";
    resolved.sourceInstancePath = "top";
    resolved.targetAncestorInstancePath = "top";
    resolved.renderedPortDataType = "logic [7:0]";
    resolved.bitWidth = 8;
    resolved.sourcePort.state = ExposeEndpointState::Reuse;
    resolved.sourcePort.moduleName = "top";
    resolved.sourcePort.instancePath = "top";
    resolved.sourcePort.portName = "payload_out";
    resolved.sourcePort.renderedDataType = "logic [7:0]";
    resolved.sourceBridge.state = ExposeEndpointState::Reuse;
    resolved.sourceBridge.moduleName = "top";
    resolved.sourceBridge.outputPortName = "payload_out";
    resolved.sourceBridge.signalName = "payload_out";
    resolved.semanticIndexFilePaths = {"top.sv"};

    const ExposeSignalToTopPlanResult result =
        planExposeSignalToTop(request, resolved);
    require(result.ready(), "typed expose planner should accept idempotent top output");
    require(result.plan.workspaceEdit.edits.empty(),
            "idempotent top output should need no edits");
    require(result.plan.workspaceEdit.intent.kind ==
                SemanticEditKind::ExposeSignalToTop,
            "typed expose intent kind mismatch");
    require(buildWorkspaceEditPreview(result.plan.workspaceEdit).actionId ==
                kExposeSignalToTopActionId,
            "typed expose action id mismatch");
    return true;
}

bool umbrellaSupportsPlanDiffProvenanceAndTransaction() {
    MockWorkspaceDocumentManager documents;
    documents.openDocument("top.sv", "payload\n", DocumentVersion{3});

    SemanticEditIntent intent;
    intent.target = signalId();
    WorkspaceTextEdit edit{
        "top.sv",
        DocumentVersion{3},
        SourceRange{SourcePosition{0, 0}, SourcePosition{0, 7}},
        "payload",
        "payload_out"};
    TextEditProvenance provenance;
    provenance.editIndex = 0;
    provenance.actionId = kExposeSignalToTopActionId;
    provenance.anchorName = "source.bridge";
    provenance.signalQualifiedName = "top.payload_out";
    provenance.sourceInstancePath = "top";
    provenance.anchor.source = AnchorResolutionSource::TreeSitter;
    provenance.anchor.resolver = "host-verified";

    WorkspaceEditPlan plan = makeWorkspaceEditPlan(
        intent,
        RiskLevel::High,
        PreviewPolicy::Diff,
        {edit},
        {provenance});
    plan.semanticSnapshot = SemanticIndexSnapshot{"7"};
    plan.semanticIndexFilePaths = {"top.sv"};

    const WorkspaceEditSourceDiff diff = buildWorkspaceEditSourceDiff(
        plan, SemanticIndexSnapshot{"7"}, documents);
    require(diff.built() && diff.files.size() == 1,
            "source diff should be built");
    require(diff.files.front().afterText == "payload_out\n",
            "source diff after text mismatch");
    require(plan.provenance.front().signalQualifiedName == "top.payload_out",
            "provenance should retain signal identity");

    const PlanApplyResult applied = applyWorkspaceEditPlan(
        plan, SemanticIndexSnapshot{"7"}, documents);
    require(applied.applied(), "workspace transaction should apply");
    require(documents.text("top.sv") == "payload_out\n",
            "workspace transaction text mismatch");

    MockWorkspaceDocumentManager staleDocuments;
    staleDocuments.openDocument("top.sv", "payload\n", DocumentVersion{4});
    require(applyWorkspaceEditPlan(
                plan, SemanticIndexSnapshot{"7"}, staleDocuments).status ==
                PlanApplyStatus::Stale,
            "document version precondition should reject stale apply");

    MockWorkspaceDocumentManager conflictingDocuments;
    conflictingDocuments.openDocument(
        "top.sv", "another\n", DocumentVersion{3});
    const PlanApplyResult conflict = applyWorkspaceEditPlan(
        plan, SemanticIndexSnapshot{"7"}, conflictingDocuments);
    require(conflict.status == PlanApplyStatus::PatchFailed,
            "expected text mismatch should fail the transaction");
    require(conflictingDocuments.text("top.sv") == "another\n",
            "failed transaction must leave the document unchanged");
    return true;
}

}  // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<bool()>>> tests = {
        {"umbrellaSupportsTypedExposePlanner",
         umbrellaSupportsTypedExposePlanner},
        {"umbrellaSupportsPlanDiffProvenanceAndTransaction",
         umbrellaSupportsPlanDiffProvenanceAndTransaction},
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
