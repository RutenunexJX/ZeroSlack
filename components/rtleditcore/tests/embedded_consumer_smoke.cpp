#include <rtledit/rtledit.h>

#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using namespace rtledit;

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class MemoryDocuments final : public WorkspaceDocumentManager {
public:
    void add(
        std::string path,
        std::string text,
        DocumentVersion version) {
        documents_.emplace(
            std::move(path),
            WorkspaceDocumentSnapshot{version, std::move(text)});
    }

    const std::string& text(const std::string& path) const {
        return documents_.at(path).text;
    }

    std::optional<WorkspaceDocumentSnapshot> snapshot(
        const std::string& path) const override {
        const auto found = documents_.find(path);
        if (found == documents_.end()) {
            return std::nullopt;
        }
        return found->second;
    }

    bool applyTextEdits(
        const std::string& path,
        DocumentVersion expectedVersion,
        const std::vector<WorkspaceTextEdit>& edits) override {
        auto found = documents_.find(path);
        if (found == documents_.end() ||
            found->second.version != expectedVersion) {
            return false;
        }
        const auto result =
            applyTextEditsToString(found->second.text, edits);
        if (!result) {
            return false;
        }
        found->second.text = *result;
        if (!edits.empty()) {
            ++found->second.version.value;
        }
        return true;
    }

    bool restoreSnapshot(
        const std::string& path,
        const WorkspaceDocumentSnapshot& snapshot) override {
        const auto found = documents_.find(path);
        if (found == documents_.end()) {
            return false;
        }
        found->second = snapshot;
        return true;
    }

private:
    std::unordered_map<std::string, WorkspaceDocumentSnapshot> documents_;
};

SemanticObjectId signalId(
    SemanticObjectKind kind = SemanticObjectKind::Signal) {
    return SemanticObjectId{
        kind,
        "child.payload",
        "child",
        "child.sv",
        SourceRange{SourcePosition{0, 0}, SourcePosition{0, 7}},
        "signal-42"};
}

void verifyTypedExposeContract() {
    ExposeSignalToTopRequest request;
    request.signalSemanticId = signalId(SemanticObjectKind::Port);
    request.signalSemanticId.qualifiedName = "top.payload_out";
    request.signalSemanticId.ownerScope = "top";
    request.signalSemanticId.filePath = "top.sv";
    request.sourceInstancePath = "top";
    request.targetAncestorInstancePath = "top";
    request.exportedPortName = "payload_out";
    request.semanticGeneration = 42;
    request.documentVersions.push_back(
        CapturedDocumentVersion{"top.sv", DocumentVersion{7}});

    ResolvedExposeSignalToTop resolved;
    resolved.semanticGeneration = 42;
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

    const ExposeSignalToTopPlanResult result =
        planExposeSignalToTop(request, resolved);
    require(result.ready(), "typed signal.exposeToTop planner failed");
    require(result.plan.workspaceEdit.intent.kind ==
                SemanticEditKind::ExposeSignalToTop,
            "typed action metadata was not retained");
}

WorkspaceEditPlan transactionPlan() {
    SemanticEditIntent intent;
    intent.target = signalId();

    std::vector<WorkspaceTextEdit> edits{
        WorkspaceTextEdit{
            "child.sv",
            DocumentVersion{3},
            SourceRange{SourcePosition{0, 0}, SourcePosition{0, 7}},
            "payload",
            "payload_out"},
        WorkspaceTextEdit{
            "top.sv",
            DocumentVersion{7},
            SourceRange{SourcePosition{0, 0}, SourcePosition{0, 5}},
            "trace",
            "trace_out"}};

    std::vector<TextEditProvenance> provenance(2);
    for (std::size_t index = 0; index < provenance.size(); ++index) {
        provenance[index].editIndex = index;
        provenance[index].actionId = kExposeSignalToTopActionId;
        provenance[index].signalQualifiedName = "child.payload";
        provenance[index].sourceInstancePath = "top.u_child";
        provenance[index].anchorName =
            index == 0 ? "source.bridge" : "step.0.connection";
        if (index == 1) {
            provenance[index].hierarchyStepIndex = 0;
        }
        provenance[index].anchor.source =
            AnchorResolutionSource::TreeSitter;
        provenance[index].anchor.resolver = "host-verified";
    }

    WorkspaceEditPlan plan = makeWorkspaceEditPlan(
        intent,
        RiskLevel::High,
        PreviewPolicy::Diff,
        std::move(edits),
        std::move(provenance));
    plan.semanticSnapshot = SemanticIndexSnapshot{"42"};
    plan.semanticIndexFilePaths = {"child.sv", "top.sv"};
    return plan;
}

void verifyPlanDiffPreconditionsAndTransaction() {
    const WorkspaceEditPlan plan = transactionPlan();
    MemoryDocuments documents;
    documents.add("child.sv", "payload\n", DocumentVersion{3});
    documents.add("top.sv", "trace\n", DocumentVersion{7});

    const WorkspaceEditSourceDiff diff = buildWorkspaceEditSourceDiff(
        plan, SemanticIndexSnapshot{"42"}, documents);
    require(diff.built() && diff.files.size() == 2,
            "installed package did not build a two-file diff");
    require(plan.provenance.size() == 2 &&
                plan.provenance[1].anchorName == "step.0.connection" &&
                plan.provenance[1].hierarchyStepIndex == 0,
            "installed package lost provenance");

    const PlanApplyResult applied = applyWorkspaceEditPlan(
        plan, SemanticIndexSnapshot{"42"}, documents);
    require(applied.applied(), "installed PatchEngine transaction failed");
    require(documents.text("child.sv") == "payload_out\n" &&
                documents.text("top.sv") == "trace_out\n",
            "installed PatchEngine produced incorrect text");

    MemoryDocuments stale;
    stale.add("child.sv", "payload\n", DocumentVersion{4});
    stale.add("top.sv", "trace\n", DocumentVersion{7});
    require(applyWorkspaceEditPlan(
                plan, SemanticIndexSnapshot{"42"}, stale).status ==
                PlanApplyStatus::Stale,
            "document version precondition was not enforced");

    MemoryDocuments conflict;
    conflict.add("child.sv", "changed\n", DocumentVersion{3});
    conflict.add("top.sv", "trace\n", DocumentVersion{7});
    require(applyWorkspaceEditPlan(
                plan, SemanticIndexSnapshot{"42"}, conflict).status ==
                PlanApplyStatus::PatchFailed,
            "expected text precondition was not enforced");
    require(conflict.text("child.sv") == "changed\n" &&
                conflict.text("top.sv") == "trace\n",
            "failed multi-file preflight modified a document");
}

}  // namespace

int main() {
    try {
        verifyTypedExposeContract();
        verifyPlanDiffPreconditionsAndTransaction();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
