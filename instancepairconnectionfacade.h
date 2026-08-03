#ifndef INSTANCEPAIRCONNECTIONFACADE_H
#define INSTANCEPAIRCONNECTIONFACADE_H

#include "editorsemanticcontextservice.h"
#include "exposesignaltotopservice.h"
#include "hierarchyservice.h"
#include "semanticindex.h"

#include <rtledit/edit_plan.h>
#include <rtledit/workspace_edit_transaction.h>

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <memory>

class TSDocument;

enum class InstancePairConnectionStatus {
    Ready,
    NoChanges,
    Rejected
};

enum class InstancePairConnectionFailure {
    None,
    InvalidRequest,
    MissingSemanticSnapshot,
    StaleSemanticGeneration,
    MissingDocumentSnapshot,
    StaleDocumentRevision,
    StaleSemanticSource,
    InvalidTreeSnapshot,
    SyntaxError,
    SemanticErrors,
    InstanceNotFound,
    SameInstance,
    DifferentDesignRoots,
    AmbiguousHierarchy,
    MultiInstanceContext,
    SignalNotFound,
    SignalNotPropagatable,
    UnsupportedSignalType,
    LeftPropagationRejected,
    MissingSource,
    ReadOnlyFile,
    NameConflict,
    DirectionConflict,
    TypeMismatch,
    DriverConflict,
    UnsupportedPortList,
    OrderedConnection,
    WildcardConnection,
    UnsupportedConnectionSyntax,
    TransactionPreparationFailed
};

enum class InstancePairSide {
    Left,
    Right
};

enum class InstancePairFlowDirection {
    TowardLca,
    AwayFromLca
};

struct InstancePairDocumentSnapshot {
    QString fileName;
    std::uint64_t revision = 0;
    QString text;
    std::shared_ptr<const TSDocument> syntax;
    bool unsaved = false;

    bool isValid() const;
};

struct InstancePairConnectionQuery {
    EditorSemanticContext leftSignalContext;
    QString leftInstancePath;
    QString rightInstancePath;
    QString connectionName;
    QSet<QString> workspaceFiles;
    SemanticSnapshotToken semanticToken;
    QHash<QString, InstancePairDocumentSnapshot> documents;
    bool dryRun = true;
};

struct InstancePairBlockPortView {
    QString name;
    SymbolTaxonomy::CollectorKind direction =
        SymbolTaxonomy::CollectorKind::PortInout;
    SemanticSymbolRecord record;
    SemanticElaboratedSymbolInfo effectiveType;
};

struct InstancePairBlockSideView {
    InstancePairSide side = InstancePairSide::Left;
    QString instancePath;
    QString instanceName;
    QString moduleName;
    QString definitionFile;
    QString instanceFile;
    QList<InstancePairBlockPortView> ports;
};

struct InstancePairBlockView {
    std::uint64_t semanticGeneration = 0;
    QString activeTopModule;
    InstancePairBlockSideView left;
    InstancePairBlockSideView right;
    QString lcaInstancePath;
    QString lcaModuleName;
    QStringList leftPathToLca;
    QStringList rightPathFromLca;
};

struct InstancePairConnectionStepView {
    InstancePairSide side = InstancePairSide::Left;
    InstancePairFlowDirection direction =
        InstancePairFlowDirection::TowardLca;
    QString childInstancePath;
    QString childModule;
    QString parentInstancePath;
    QString parentModule;
    QString portName;
    rtledit::ExposeEndpointState portState =
        rtledit::ExposeEndpointState::Conflict;
    rtledit::ExposeEndpointState connectionState =
        rtledit::ExposeEndpointState::Conflict;
};

struct InstancePairResolvedPortSite {
    DesignHierarchyNode node;
    SemanticSymbolRecord moduleRecord;
    SemanticSymbolRecord existingPort;
    rtledit::ExposeEndpointState state =
        rtledit::ExposeEndpointState::Conflict;
    rtledit::StructuredInsertionAnchor insertion;
    rtledit::StructuredInsertionAnchor delimiterInsertion;
};

struct InstancePairResolvedConnectionSite {
    DesignHierarchyNode child;
    DesignHierarchyNode parent;
    SemanticSymbolRecord instanceRecord;
    rtledit::ExposeEndpointState state =
        rtledit::ExposeEndpointState::Conflict;
    rtledit::StructuredInsertionAnchor insertion;
    rtledit::StructuredInsertionAnchor delimiterInsertion;
};

struct InstancePairResolvedLocalSignalSite {
    SemanticSymbolRecord moduleRecord;
    SemanticSymbolRecord existingSignal;
    rtledit::ExposeEndpointState state =
        rtledit::ExposeEndpointState::Conflict;
    rtledit::StructuredInsertionAnchor insertion;
    rtledit::StructuredInsertionAnchor bridgeInsertion;
    bool selectedSourceSignal = false;
    bool bridgeRequired = false;
};

struct InstancePairConnectionAnalysis {
    InstancePairConnectionStatus status =
        InstancePairConnectionStatus::Rejected;
    InstancePairConnectionFailure failure =
        InstancePairConnectionFailure::InvalidRequest;
    QString message;
    QStringList blockers;
    InstancePairConnectionQuery query;
    QHash<QString, InstancePairDocumentSnapshot> capturedDocuments;
    InstancePairBlockView blockView;
    SemanticSymbolRecord leftSignal;
    SemanticElaboratedSymbolInfo signalType;
    QString renderedSignalType;
    ExposeSignalToTopReport leftBranchReport;
    InstancePairResolvedLocalSignalSite localSignal;
    InstancePairResolvedConnectionSite leftLcaConnection;
    QList<InstancePairResolvedPortSite> rightPorts;
    QList<InstancePairResolvedConnectionSite> rightConnections;
    QList<InstancePairConnectionStepView> steps;

    bool ready() const;
};

struct InstancePairConnectionProposal {
    InstancePairConnectionStatus status =
        InstancePairConnectionStatus::Rejected;
    InstancePairConnectionFailure failure =
        InstancePairConnectionFailure::InvalidRequest;
    QString message;
    QStringList blockers;
    InstancePairBlockView blockView;
    SemanticSymbolRecord leftSignal;
    QString connectionName;
    QString renderedSignalType;
    QList<InstancePairConnectionStepView> steps;
    bool dryRun = true;
    rtledit::WorkspaceEditPlan workspaceEdit;
    rtledit::PreparedWorkspaceEditTransaction transaction;
    rtledit::WorkspaceEditSourceDiff sourceDiff;
    QString renderedDiff;

    bool ready() const;
};

class InstancePairConnectionFacade
{
public:
    explicit InstancePairConnectionFacade(
        SemanticIndex* semanticIndex = nullptr,
        HierarchyService* hierarchyService = nullptr);

    InstancePairConnectionAnalysis analyze(
        const InstancePairConnectionQuery& query,
        rtledit::WorkspaceDocumentManager& documents) const;

    // Planning is side-effect free. The returned High+Diff transaction is
    // intentionally unconfirmed and must be applied by the existing
    // WorkspaceEditTransactionService after explicit preview confirmation.
    InstancePairConnectionProposal plan(
        const InstancePairConnectionAnalysis& analysis,
        rtledit::WorkspaceDocumentManager& documents) const;

private:
    SemanticIndex* index = nullptr;
    HierarchyService* hierarchy = nullptr;

    SemanticIndex* semanticIndex() const;
    HierarchyService* hierarchyService() const;
};

#endif // INSTANCEPAIRCONNECTIONFACADE_H
