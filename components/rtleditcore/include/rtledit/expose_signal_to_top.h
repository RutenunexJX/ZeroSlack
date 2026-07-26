#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "rtledit/edit_plan.h"
#include "rtledit/provenance.h"
#include "rtledit/semantic_object.h"

namespace rtledit {

struct CapturedDocumentVersion {
    std::string filePath;
    DocumentVersion version;
};

struct ExposeSignalToTopRequest {
    SemanticObjectId signalSemanticId;
    std::string sourceInstancePath;
    std::string targetAncestorInstancePath;
    std::string exportedPortName;
    std::uint64_t semanticGeneration = 0;
    std::vector<CapturedDocumentVersion> documentVersions;
};

enum class ExposeSignalFailureReason {
    None,
    InvalidRequest,
    SignalNotFound,
    SignalNotInSelectedInstance,
    SignalNotPropagatable,
    TargetNotAncestor,
    AmbiguousHierarchy,
    MissingSource,
    ReadOnlyFile,
    MacroExpansion,
    UnsupportedInterface,
    UnsupportedUnpackedArray,
    UnsupportedGenerateSourceMap,
    UnsupportedPortList,
    UnsupportedInstanceConnection,
    PositionalInstantiation,
    NameConflict,
    DirectionConflict,
    TypeMismatch,
    ConnectionConflict,
    DriverConflict,
    ParameterWidthConflict,
    MissingStructuralAnchor,
    InvalidStructuralAnchor,
    MissingDocumentVersion,
    DocumentVersionConflict,
    StaleSemanticGeneration
};

const char* exposeSignalFailureReasonName(
    ExposeSignalFailureReason reason);

struct ExposeSignalDiagnostic {
    ExposeSignalFailureReason reason = ExposeSignalFailureReason::None;
    std::string message;
    std::string filePath;
    std::string instancePath;
    SourceRange sourceRange;

    bool blocking() const {
        return reason != ExposeSignalFailureReason::None;
    }
};

enum class ExposeEndpointState {
    Add,
    Reuse,
    Conflict
};

struct StructuredInsertionAnchor {
    std::string filePath;
    DocumentVersion documentVersion;
    SourceRange range;
    std::string expectedText;
    std::string prefix;
    std::string suffix;
    AnchorProvenance provenance;

    bool present() const {
        return !filePath.empty();
    }
};

struct ExposeSignalPortSite {
    ExposeEndpointState state = ExposeEndpointState::Conflict;
    std::string moduleName;
    std::string instancePath;
    std::string portName;
    std::string renderedDataType;
    StructuredInsertionAnchor insertion;
    StructuredInsertionAnchor delimiterInsertion;
    ExposeSignalFailureReason conflictReason =
        ExposeSignalFailureReason::NameConflict;
    std::string conflictMessage;
};

struct ExposeSignalBridgeSite {
    ExposeEndpointState state = ExposeEndpointState::Conflict;
    std::string moduleName;
    std::string outputPortName;
    std::string signalName;
    StructuredInsertionAnchor insertion;
    ExposeSignalFailureReason conflictReason =
        ExposeSignalFailureReason::DriverConflict;
    std::string conflictMessage;
};

struct ExposeSignalConnectionSite {
    ExposeEndpointState state = ExposeEndpointState::Conflict;
    std::string childModuleName;
    std::string childInstanceName;
    std::string childInstancePath;
    std::string parentModuleName;
    std::string parentInstancePath;
    std::string portName;
    std::string expressionName;
    StructuredInsertionAnchor insertion;
    StructuredInsertionAnchor delimiterInsertion;
    ExposeSignalFailureReason conflictReason =
        ExposeSignalFailureReason::ConnectionConflict;
    std::string conflictMessage;
};

struct SignalPropagationStep {
    std::size_t index = 0;
    std::string childModuleName;
    std::string childInstanceName;
    std::string childInstancePath;
    std::string parentModuleName;
    std::string parentInstancePath;
    std::string instanceSourceFilePath;
    SourceRange instanceSourceRange;
    ExposeSignalPortSite parentPort;
    ExposeSignalConnectionSite connection;
};

struct SignalPropagationPath {
    std::vector<SignalPropagationStep> steps;
};

struct AffectedModuleImpact {
    std::string moduleName;
    std::string definitionFilePath;
    std::vector<std::string> instancePaths;
};

struct ResolvedExposeSignalToTop {
    std::uint64_t semanticGeneration = 0;
    std::string signalName;
    std::string sourceModuleName;
    std::string sourceInstancePath;
    std::string targetAncestorInstancePath;
    std::string renderedPortDataType;
    std::size_t bitWidth = 0;
    ExposeSignalPortSite sourcePort;
    ExposeSignalBridgeSite sourceBridge;
    SignalPropagationPath path;
    std::vector<AffectedModuleImpact> impact;
    std::vector<ExposeSignalDiagnostic> diagnostics;
    std::vector<std::string> semanticIndexFilePaths;
};

struct ExposeSignalToTopPlan {
    ExposeSignalToTopRequest request;
    SignalPropagationPath path;
    std::vector<AffectedModuleImpact> impact;
    std::vector<ExposeSignalDiagnostic> diagnostics;
    WorkspaceEditPlan workspaceEdit;
};

enum class ExposeSignalPlanStatus {
    Ready,
    Rejected
};

struct ExposeSignalToTopPlanResult {
    ExposeSignalPlanStatus status = ExposeSignalPlanStatus::Rejected;
    ExposeSignalFailureReason failureReason =
        ExposeSignalFailureReason::InvalidRequest;
    std::string message;
    ExposeSignalToTopPlan plan;

    bool ready() const {
        return status == ExposeSignalPlanStatus::Ready;
    }
};

ExposeSignalToTopPlanResult planExposeSignalToTop(
    const ExposeSignalToTopRequest& request,
    const ResolvedExposeSignalToTop& resolved);

}  // namespace rtledit
