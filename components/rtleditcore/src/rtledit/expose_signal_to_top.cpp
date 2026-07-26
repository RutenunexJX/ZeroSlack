#include "rtledit/expose_signal_to_top.h"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <tuple>
#include <utility>

namespace rtledit {
namespace {

struct PendingEdit {
    WorkspaceTextEdit edit;
    TextEditProvenance provenance;
    int roleOrder = 0;
};

ExposeSignalToTopPlanResult reject(
    const ExposeSignalToTopRequest& request,
    const ResolvedExposeSignalToTop& resolved,
    ExposeSignalFailureReason reason,
    std::string message) {
    ExposeSignalToTopPlanResult result;
    result.status = ExposeSignalPlanStatus::Rejected;
    result.failureReason = reason;
    result.message = std::move(message);
    result.plan.request = request;
    result.plan.path = resolved.path;
    result.plan.impact = resolved.impact;
    result.plan.diagnostics = resolved.diagnostics;
    return result;
}

bool isAncestorOrSelf(
    const std::string& ancestor,
    const std::string& descendant) {
    if (ancestor.empty() || descendant.empty()) {
        return false;
    }
    if (ancestor == descendant) {
        return true;
    }
    return descendant.size() > ancestor.size() &&
           descendant.compare(0, ancestor.size(), ancestor) == 0 &&
           descendant[ancestor.size()] == '.';
}

bool anchorRangeValid(const StructuredInsertionAnchor& anchor) {
    return anchor.present() && !(anchor.range.end < anchor.range.start);
}

std::map<std::string, DocumentVersion> capturedVersions(
    const ExposeSignalToTopRequest& request,
    bool* conflict) {
    std::map<std::string, DocumentVersion> result;
    *conflict = false;
    for (const auto& captured : request.documentVersions) {
        if (captured.filePath.empty()) {
            *conflict = true;
            return {};
        }
        const auto [it, inserted] =
            result.emplace(captured.filePath, captured.version);
        if (!inserted && it->second != captured.version) {
            *conflict = true;
            return {};
        }
    }
    return result;
}

std::optional<ExposeSignalToTopPlanResult> validateAnchor(
    const ExposeSignalToTopRequest& request,
    const ResolvedExposeSignalToTop& resolved,
    const StructuredInsertionAnchor& anchor,
    const std::map<std::string, DocumentVersion>& versions,
    const std::string& description) {
    if (!anchorRangeValid(anchor)) {
        return reject(
            request,
            resolved,
            ExposeSignalFailureReason::InvalidStructuralAnchor,
            "Invalid Tree-sitter anchor for " + description + ".");
    }
    const auto version = versions.find(anchor.filePath);
    if (version == versions.end()) {
        return reject(
            request,
            resolved,
            ExposeSignalFailureReason::MissingDocumentVersion,
            "No captured document version for " + anchor.filePath + ".");
    }
    if (version->second != anchor.documentVersion) {
        return reject(
            request,
            resolved,
            ExposeSignalFailureReason::DocumentVersionConflict,
            "Captured document version does not match the structural anchor for " +
                anchor.filePath + ".");
    }
    return std::nullopt;
}

TextEditProvenance makeProvenance(
    const ExposeSignalToTopRequest& request,
    const StructuredInsertionAnchor& anchor,
    std::string anchorName,
    std::string description,
    std::optional<std::size_t> hierarchyStep,
    std::string sourceFilePath,
    SourceRange sourceRange) {
    TextEditProvenance provenance;
    provenance.actionId = kExposeSignalToTopActionId;
    provenance.anchorName = std::move(anchorName);
    provenance.description = std::move(description);
    provenance.anchor = anchor.provenance;
    provenance.signalQualifiedName =
        request.signalSemanticId.qualifiedName;
    provenance.sourceInstancePath = request.sourceInstancePath;
    provenance.hierarchyStepIndex = hierarchyStep;
    provenance.sourceFilePath = std::move(sourceFilePath);
    provenance.sourceRange = sourceRange;
    return provenance;
}

void appendInsertion(
    std::vector<PendingEdit>* edits,
    const StructuredInsertionAnchor& anchor,
    std::string generatedText,
    TextEditProvenance provenance,
    int roleOrder) {
    edits->push_back(PendingEdit{
        WorkspaceTextEdit{
            anchor.filePath,
            anchor.documentVersion,
            anchor.range,
            anchor.expectedText,
            anchor.prefix + std::move(generatedText) + anchor.suffix},
        std::move(provenance),
        roleOrder});
}

std::string portText(const ExposeSignalPortSite& site) {
    return "output " + site.renderedDataType + " " + site.portName;
}

std::string connectionText(
    const ExposeSignalConnectionSite& site) {
    return "." + site.portName + "(" + site.expressionName + ")";
}

std::optional<ExposeSignalToTopPlanResult> validatePortSite(
    const ExposeSignalToTopRequest& request,
    const ResolvedExposeSignalToTop& resolved,
    const ExposeSignalPortSite& site,
    const std::map<std::string, DocumentVersion>& versions,
    const std::string& description) {
    if (site.state == ExposeEndpointState::Conflict) {
        const auto reason =
            site.conflictReason == ExposeSignalFailureReason::None
                ? ExposeSignalFailureReason::NameConflict
                : site.conflictReason;
        return reject(
            request,
            resolved,
            reason,
            site.conflictMessage.empty()
                ? "Incompatible existing port in " + site.moduleName + "."
                : site.conflictMessage);
    }
    if (site.moduleName.empty() || site.portName.empty() ||
        site.renderedDataType.empty()) {
        return reject(
            request,
            resolved,
            ExposeSignalFailureReason::InvalidRequest,
            "Resolved port facts are incomplete for " + description + ".");
    }
    if (site.portName != request.exportedPortName) {
        return reject(
            request,
            resolved,
            ExposeSignalFailureReason::NameConflict,
            "Resolved port name differs from the requested export name.");
    }
    if (site.state == ExposeEndpointState::Add) {
        if (const auto failure = validateAnchor(
                request,
                resolved,
                site.insertion,
                versions,
                description + " port insertion")) {
            return failure;
        }
        if (site.delimiterInsertion.present()) {
            if (const auto failure = validateAnchor(
                    request,
                    resolved,
                    site.delimiterInsertion,
                    versions,
                    description + " port delimiter")) {
                return failure;
            }
        }
    }
    return std::nullopt;
}

std::optional<ExposeSignalToTopPlanResult> validateBridgeSite(
    const ExposeSignalToTopRequest& request,
    const ResolvedExposeSignalToTop& resolved,
    const ExposeSignalBridgeSite& bridge,
    const std::map<std::string, DocumentVersion>& versions) {
    if (bridge.state == ExposeEndpointState::Conflict) {
        const auto reason =
            bridge.conflictReason == ExposeSignalFailureReason::None
                ? ExposeSignalFailureReason::DriverConflict
                : bridge.conflictReason;
        return reject(
            request,
            resolved,
            reason,
            bridge.conflictMessage.empty()
                ? "The existing source output has no compatible Slang-proven driver."
                : bridge.conflictMessage);
    }
    if (bridge.moduleName != resolved.sourceModuleName ||
        bridge.outputPortName != request.exportedPortName ||
        bridge.signalName != resolved.signalName) {
        return reject(
            request,
            resolved,
            ExposeSignalFailureReason::DriverConflict,
            "Resolved source bridge does not match the selected signal and output.");
    }
    if (bridge.state == ExposeEndpointState::Add) {
        if (const auto failure = validateAnchor(
                request,
                resolved,
                bridge.insertion,
                versions,
                "source signal bridge")) {
            return failure;
        }
    }
    return std::nullopt;
}

std::optional<ExposeSignalToTopPlanResult> validateConnectionSite(
    const ExposeSignalToTopRequest& request,
    const ResolvedExposeSignalToTop& resolved,
    const ExposeSignalConnectionSite& site,
    const std::map<std::string, DocumentVersion>& versions) {
    if (site.state == ExposeEndpointState::Conflict) {
        const auto reason =
            site.conflictReason == ExposeSignalFailureReason::None
                ? ExposeSignalFailureReason::ConnectionConflict
                : site.conflictReason;
        return reject(
            request,
            resolved,
            reason,
            site.conflictMessage.empty()
                ? "Incompatible existing instance connection for " +
                      site.childInstancePath + "."
                : site.conflictMessage);
    }
    if (site.childInstancePath.empty() ||
        site.parentInstancePath.empty() ||
        site.portName != request.exportedPortName ||
        site.expressionName != request.exportedPortName) {
        return reject(
            request,
            resolved,
            ExposeSignalFailureReason::ConnectionConflict,
            "Resolved instance connection does not match the requested export.");
    }
    if (site.state == ExposeEndpointState::Add) {
        if (const auto failure = validateAnchor(
                request,
                resolved,
                site.insertion,
                versions,
                "instance connection insertion")) {
            return failure;
        }
        if (site.delimiterInsertion.present()) {
            if (const auto failure = validateAnchor(
                    request,
                    resolved,
                    site.delimiterInsertion,
                    versions,
                    "instance connection delimiter")) {
                return failure;
            }
        }
    }
    return std::nullopt;
}

void appendPortEdits(
    std::vector<PendingEdit>* edits,
    const ExposeSignalToTopRequest& request,
    const ExposeSignalPortSite& site,
    std::optional<std::size_t> hierarchyStep,
    const std::string& sourceFilePath,
    SourceRange sourceRange) {
    if (site.state != ExposeEndpointState::Add) {
        return;
    }
    if (site.delimiterInsertion.present()) {
        appendInsertion(
            edits,
            site.delimiterInsertion,
            ",",
            makeProvenance(
                request,
                site.delimiterInsertion,
                "port.previousDelimiter",
                "Terminate the previous ANSI port before exposing the signal.",
                hierarchyStep,
                sourceFilePath,
                sourceRange),
            0);
    }
    appendInsertion(
        edits,
        site.insertion,
        portText(site),
        makeProvenance(
            request,
            site.insertion,
            "port.insert",
            "Add an output port for the propagated signal.",
            hierarchyStep,
            sourceFilePath,
            sourceRange),
        1);
}

void appendConnectionEdits(
    std::vector<PendingEdit>* edits,
    const ExposeSignalToTopRequest& request,
    const SignalPropagationStep& step) {
    const auto& site = step.connection;
    if (site.state != ExposeEndpointState::Add) {
        return;
    }
    if (site.delimiterInsertion.present()) {
        appendInsertion(
            edits,
            site.delimiterInsertion,
            ",",
            makeProvenance(
                request,
                site.delimiterInsertion,
                "connection.previousDelimiter",
                "Terminate the previous named connection.",
                step.index,
                step.instanceSourceFilePath,
                step.instanceSourceRange),
            2);
    }
    appendInsertion(
        edits,
        site.insertion,
        connectionText(site),
        makeProvenance(
            request,
            site.insertion,
            "connection.insert",
            "Connect the selected child instance output to the parent output.",
            step.index,
            step.instanceSourceFilePath,
            step.instanceSourceRange),
        3);
}

}  // namespace

const char* exposeSignalFailureReasonName(
    ExposeSignalFailureReason reason) {
    switch (reason) {
    case ExposeSignalFailureReason::None:
        return "none";
    case ExposeSignalFailureReason::InvalidRequest:
        return "invalid_request";
    case ExposeSignalFailureReason::SignalNotFound:
        return "signal_not_found";
    case ExposeSignalFailureReason::SignalNotInSelectedInstance:
        return "signal_not_in_selected_instance";
    case ExposeSignalFailureReason::SignalNotPropagatable:
        return "signal_not_propagatable";
    case ExposeSignalFailureReason::TargetNotAncestor:
        return "target_not_ancestor";
    case ExposeSignalFailureReason::AmbiguousHierarchy:
        return "ambiguous_hierarchy";
    case ExposeSignalFailureReason::MissingSource:
        return "missing_source";
    case ExposeSignalFailureReason::ReadOnlyFile:
        return "read_only_file";
    case ExposeSignalFailureReason::MacroExpansion:
        return "macro_expansion";
    case ExposeSignalFailureReason::UnsupportedInterface:
        return "unsupported_interface";
    case ExposeSignalFailureReason::UnsupportedUnpackedArray:
        return "unsupported_unpacked_array";
    case ExposeSignalFailureReason::UnsupportedGenerateSourceMap:
        return "unsupported_generate_source_map";
    case ExposeSignalFailureReason::UnsupportedPortList:
        return "unsupported_port_list";
    case ExposeSignalFailureReason::UnsupportedInstanceConnection:
        return "unsupported_instance_connection";
    case ExposeSignalFailureReason::PositionalInstantiation:
        return "positional_instantiation";
    case ExposeSignalFailureReason::NameConflict:
        return "name_conflict";
    case ExposeSignalFailureReason::DirectionConflict:
        return "direction_conflict";
    case ExposeSignalFailureReason::TypeMismatch:
        return "type_mismatch";
    case ExposeSignalFailureReason::ConnectionConflict:
        return "connection_conflict";
    case ExposeSignalFailureReason::DriverConflict:
        return "driver_conflict";
    case ExposeSignalFailureReason::ParameterWidthConflict:
        return "parameter_width_conflict";
    case ExposeSignalFailureReason::MissingStructuralAnchor:
        return "missing_structural_anchor";
    case ExposeSignalFailureReason::InvalidStructuralAnchor:
        return "invalid_structural_anchor";
    case ExposeSignalFailureReason::MissingDocumentVersion:
        return "missing_document_version";
    case ExposeSignalFailureReason::DocumentVersionConflict:
        return "document_version_conflict";
    case ExposeSignalFailureReason::StaleSemanticGeneration:
        return "stale_semantic_generation";
    }
    return "unknown";
}

ExposeSignalToTopPlanResult planExposeSignalToTop(
    const ExposeSignalToTopRequest& request,
    const ResolvedExposeSignalToTop& resolved) {
    if ((request.signalSemanticId.kind != SemanticObjectKind::Signal &&
         request.signalSemanticId.kind != SemanticObjectKind::Port) ||
        request.signalSemanticId.qualifiedName.empty() ||
        request.sourceInstancePath.empty() ||
        request.targetAncestorInstancePath.empty() ||
        request.exportedPortName.empty() ||
        request.semanticGeneration == 0) {
        return reject(
            request,
            resolved,
            ExposeSignalFailureReason::InvalidRequest,
            "Expose signal request is incomplete.");
    }
    if (request.semanticGeneration != resolved.semanticGeneration) {
        return reject(
            request,
            resolved,
            ExposeSignalFailureReason::StaleSemanticGeneration,
            "Semantic generation changed before the plan was built.");
    }
    if (request.sourceInstancePath != resolved.sourceInstancePath ||
        request.targetAncestorInstancePath !=
            resolved.targetAncestorInstancePath) {
        return reject(
            request,
            resolved,
            ExposeSignalFailureReason::AmbiguousHierarchy,
            "Resolved hierarchy does not match the requested instance path.");
    }
    if (!isAncestorOrSelf(
            request.targetAncestorInstancePath,
            request.sourceInstancePath)) {
        return reject(
            request,
            resolved,
            ExposeSignalFailureReason::TargetNotAncestor,
            "The target instance is not an ancestor of the source instance.");
    }
    if (resolved.signalName.empty() ||
        resolved.sourceModuleName.empty() ||
        resolved.renderedPortDataType.empty() ||
        resolved.bitWidth == 0) {
        return reject(
            request,
            resolved,
            ExposeSignalFailureReason::SignalNotPropagatable,
            "Slang did not provide a fixed-width propagatable signal type.");
    }
    for (const auto& diagnostic : resolved.diagnostics) {
        if (diagnostic.blocking()) {
            return reject(
                request,
                resolved,
                diagnostic.reason,
                diagnostic.message.empty()
                    ? "Expose signal semantic preflight failed."
                    : diagnostic.message);
        }
    }

    bool versionConflict = false;
    const auto versions = capturedVersions(request, &versionConflict);
    if (versionConflict) {
        return reject(
            request,
            resolved,
            ExposeSignalFailureReason::DocumentVersionConflict,
            "Captured document versions are invalid or inconsistent.");
    }

    if (const auto failure = validatePortSite(
            request,
            resolved,
            resolved.sourcePort,
            versions,
            "source module")) {
        return *failure;
    }
    if (const auto failure = validateBridgeSite(
            request,
            resolved,
            resolved.sourceBridge,
            versions)) {
        return *failure;
    }

    std::string expectedChildPath = request.sourceInstancePath;
    for (std::size_t index = 0; index < resolved.path.steps.size(); ++index) {
        const auto& step = resolved.path.steps[index];
        if (step.index != index ||
            step.childInstancePath != expectedChildPath ||
            step.connection.childInstancePath != step.childInstancePath ||
            step.connection.parentInstancePath != step.parentInstancePath ||
            !isAncestorOrSelf(
                step.parentInstancePath,
                step.childInstancePath)) {
            return reject(
                request,
                resolved,
                ExposeSignalFailureReason::AmbiguousHierarchy,
                "Propagation hierarchy steps are not contiguous and unique.");
        }
        if (const auto failure = validatePortSite(
                request,
                resolved,
                step.parentPort,
                versions,
                "parent module")) {
            return *failure;
        }
        if (const auto failure = validateConnectionSite(
                request,
                resolved,
                step.connection,
                versions)) {
            return *failure;
        }
        expectedChildPath = step.parentInstancePath;
    }
    if (expectedChildPath != request.targetAncestorInstancePath) {
        return reject(
            request,
            resolved,
            ExposeSignalFailureReason::AmbiguousHierarchy,
            "Propagation path does not terminate at the requested ancestor.");
    }

    std::vector<PendingEdit> pending;
    appendPortEdits(
        &pending,
        request,
        resolved.sourcePort,
        std::nullopt,
        request.signalSemanticId.filePath,
        request.signalSemanticId.range);
    if (resolved.sourceBridge.state == ExposeEndpointState::Add) {
        const auto& bridge = resolved.sourceBridge;
        appendInsertion(
            &pending,
            bridge.insertion,
            "assign " + request.exportedPortName + " = " +
                resolved.signalName + ";",
            makeProvenance(
                request,
                bridge.insertion,
                "source.bridge",
                "Bridge the selected internal signal to the new output port.",
                std::nullopt,
                request.signalSemanticId.filePath,
                request.signalSemanticId.range),
            4);
    }
    for (const auto& step : resolved.path.steps) {
        appendPortEdits(
            &pending,
            request,
            step.parentPort,
            step.index,
            step.instanceSourceFilePath,
            step.instanceSourceRange);
        appendConnectionEdits(&pending, request, step);
    }

    std::stable_sort(
        pending.begin(),
        pending.end(),
        [](const PendingEdit& lhs, const PendingEdit& rhs) {
            return std::tie(
                       lhs.edit.filePath,
                       lhs.edit.range.start.line,
                       lhs.edit.range.start.column,
                       lhs.edit.range.end.line,
                       lhs.edit.range.end.column,
                       lhs.roleOrder,
                       lhs.edit.newText) <
                   std::tie(
                       rhs.edit.filePath,
                       rhs.edit.range.start.line,
                       rhs.edit.range.start.column,
                       rhs.edit.range.end.line,
                       rhs.edit.range.end.column,
                       rhs.roleOrder,
                       rhs.edit.newText);
        });

    std::vector<WorkspaceTextEdit> edits;
    std::vector<TextEditProvenance> provenance;
    edits.reserve(pending.size());
    provenance.reserve(pending.size());
    for (std::size_t index = 0; index < pending.size(); ++index) {
        pending[index].provenance.editIndex = index;
        edits.push_back(std::move(pending[index].edit));
        provenance.push_back(std::move(pending[index].provenance));
    }

    SemanticEditIntent intent;
    intent.kind = SemanticEditKind::ExposeSignalToTop;
    intent.target = request.signalSemanticId;
    auto workspaceEdit = makeWorkspaceEditPlan(
        std::move(intent),
        RiskLevel::High,
        PreviewPolicy::Diff,
        std::move(edits),
        std::move(provenance));
    workspaceEdit.semanticSnapshot =
        SemanticIndexSnapshot{std::to_string(request.semanticGeneration)};
    workspaceEdit.semanticIndexFilePaths =
        resolved.semanticIndexFilePaths;
    std::sort(
        workspaceEdit.semanticIndexFilePaths.begin(),
        workspaceEdit.semanticIndexFilePaths.end());
    workspaceEdit.semanticIndexFilePaths.erase(
        std::unique(
            workspaceEdit.semanticIndexFilePaths.begin(),
            workspaceEdit.semanticIndexFilePaths.end()),
        workspaceEdit.semanticIndexFilePaths.end());

    ExposeSignalToTopPlanResult result;
    result.status = ExposeSignalPlanStatus::Ready;
    result.failureReason = ExposeSignalFailureReason::None;
    result.message = workspaceEdit.edits.empty()
        ? "The signal is already exposed to the requested ancestor."
        : "Expose signal plan is ready.";
    result.plan.request = request;
    result.plan.path = resolved.path;
    result.plan.impact = resolved.impact;
    result.plan.diagnostics = resolved.diagnostics;
    result.plan.workspaceEdit = std::move(workspaceEdit);
    return result;
}

}  // namespace rtledit
