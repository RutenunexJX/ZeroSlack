#ifndef RTLCONNECTIONTRANSFORMPLANNER_H
#define RTLCONNECTIONTRANSFORMPLANNER_H

#include "semanticindex.h"

#include <rtledit/edit_plan.h>
#include <rtledit/workspace_document_manager.h>

#include <QList>
#include <QString>

#include <cstdint>

enum class RtlConnectionTransformStatus {
    Ready,
    NoChanges,
    Rejected
};

enum class RtlConnectionTransformFailure {
    None,
    InvalidRequest,
    MissingSemanticSnapshot,
    StaleSemanticGeneration,
    MissingDocument,
    StaleDocumentRevision,
    StaleSemanticSource,
    SyntaxError,
    InstanceNotFound,
    InstanceSemanticMismatch,
    AmbiguousModuleDefinition,
    AmbiguousFormal,
    MissingFormalType,
    MultiInstanceTypeDifference,
    WildcardConnection,
    MixedConnectionStyle,
    UnsupportedConnectionSyntax,
    OrderedConnectionCountExceedsFormals,
    UnknownNamedFormal,
    AmbiguousActual,
    MissingActualType,
    CastNotProvable,
    SemanticSnapshotChanged
};

enum class RtlMissingPortConnectionPolicy {
    LeaveUnconnected,
    ConnectSameNamedSignal
};

enum class RtlExplicitCastPolicy {
    PreserveExistingExpression,
    InsertWhenRequired
};

struct RtlConnectionTransformRequest {
    SymbolStableKey instanceStableKey;
    QString selectedInstancePath;
    QString parentInstancePath;
    std::uint64_t expectedSemanticGeneration = 0;
    std::uint64_t expectedDocumentRevision = 0;
    bool convertOrderedToNamed = true;
    bool addMissingPorts = false;
    RtlMissingPortConnectionPolicy missingPortPolicy =
        RtlMissingPortConnectionPolicy::LeaveUnconnected;
    RtlExplicitCastPolicy castPolicy =
        RtlExplicitCastPolicy::PreserveExistingExpression;
};

struct RtlConnectionFormalView {
    QString formalName;
    SymbolTaxonomy::CollectorKind direction =
        SymbolTaxonomy::CollectorKind::PortInout;
    QString actualText;
    bool originallyConnected = false;
    bool added = false;
    bool explicitCastInserted = false;
    bool fixedSize = false;
    bool integral = false;
    bool signedIntegral = false;
    std::uint64_t bitWidth = 0;
};

struct RtlConnectionTransformReport {
    RtlConnectionTransformStatus status =
        RtlConnectionTransformStatus::Rejected;
    RtlConnectionTransformFailure failure =
        RtlConnectionTransformFailure::InvalidRequest;
    QString message;
    SemanticSymbolRecord instanceRecord;
    QList<RtlConnectionFormalView> formals;
    rtledit::WorkspaceEditPlan workspaceEdit;

    bool ready() const
    {
        return status == RtlConnectionTransformStatus::Ready
            && !workspaceEdit.edits.empty();
    }
};

class RtlConnectionTransformPlanner
{
public:
    explicit RtlConnectionTransformPlanner(
        SemanticIndex* semanticIndex = nullptr);

    RtlConnectionTransformReport plan(
        const RtlConnectionTransformRequest& request,
        const rtledit::WorkspaceDocumentManager& documents) const;

private:
    SemanticIndex* index = nullptr;

    SemanticIndex* semanticIndex() const;
};

#endif // RTLCONNECTIONTRANSFORMPLANNER_H
