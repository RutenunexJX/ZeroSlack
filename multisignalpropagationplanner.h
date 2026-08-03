#ifndef MULTISIGNALPROPAGATIONPLANNER_H
#define MULTISIGNALPROPAGATIONPLANNER_H

#include "exposesignaltotopservice.h"
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

class HierarchyService;
class TSDocument;

enum class MultiSignalPropagationMode {
    IndependentPorts,
    PortGroup
};

enum class MultiSignalPropagationStatus {
    Ready,
    NoChanges,
    Rejected
};

enum class MultiSignalPropagationFailure {
    None,
    InvalidRequest,
    InvalidGroupName,
    InvalidMemberName,
    DuplicateSignal,
    DuplicatePortName,
    MissingSemanticSnapshot,
    StaleSemanticGeneration,
    MissingDocumentSnapshot,
    StaleDocumentRevision,
    StaleSemanticSource,
    InvalidTreeSnapshot,
    SyntaxError,
    InconsistentSourceInstance,
    TargetNotAncestor,
    SingleSignalRejected,
    IncompatibleMemberPlans,
    TransactionPreparationFailed
};

struct MultiSignalPropagationDocumentSnapshot {
    QString fileName;
    std::uint64_t revision = 0;
    QString text;
    std::shared_ptr<const TSDocument> syntax;
    bool unsaved = false;

    bool isValid() const;
};

struct MultiSignalPropagationMemberRequest {
    EditorSemanticContext context;

    // Independent mode: optional exact exported port name. When empty, the
    // canonical single-signal "<signal>_out" rule is used.
    QString exportedPortName;

    // Port-group mode: optional member suffix. When empty, the selected
    // Slang-bound signal identifier is used.
    QString groupMemberName;
};

struct MultiSignalPropagationQuery {
    QList<MultiSignalPropagationMemberRequest> members;
    MultiSignalPropagationMode mode =
        MultiSignalPropagationMode::IndependentPorts;
    QString groupName;

    // Empty means the active design top. Otherwise this must be the source
    // instance itself or an exact parent instance path in every member plan.
    QString targetAncestorInstancePath;

    QSet<QString> workspaceFiles;
    SemanticSnapshotToken semanticToken;

    // Immutable live-buffer snapshots captured with semanticToken. Saved
    // files must still equal the Slang snapshot source. Unsaved files retain
    // Slang semantics but use these Tree-sitter trees for structural anchors.
    QHash<QString, MultiSignalPropagationDocumentSnapshot> documents;
    bool dryRun = true;
};

struct MultiSignalPropagationMemberView {
    SemanticSymbolRecord signal;
    QString sourceSignalName;
    QString memberName;
    QString exportedPortName;
    QString sourceInstancePath;
    QString targetAncestorInstancePath;
    int retainedHierarchyStepCount = 0;
};

struct MultiSignalPortGroupView {
    QString groupName;
    QString namingRule;
    QStringList orderedMembers;
    QStringList orderedPortNames;
};

struct MultiSignalPropagationProposal {
    MultiSignalPropagationStatus status =
        MultiSignalPropagationStatus::Rejected;
    MultiSignalPropagationFailure failure =
        MultiSignalPropagationFailure::InvalidRequest;
    QString message;
    QStringList blockers;
    QList<MultiSignalPropagationMemberView> members;
    MultiSignalPortGroupView portGroup;
    QString sourceInstancePath;
    QString targetAncestorInstancePath;
    bool dryRun = true;

    rtledit::WorkspaceEditPlan workspaceEdit;
    rtledit::PreparedWorkspaceEditTransaction transaction;
    rtledit::WorkspaceEditSourceDiff sourceDiff;
    QString renderedDiff;

    bool ready() const;
};

class MultiSignalPropagationPlanner
{
public:
    explicit MultiSignalPropagationPlanner(
        SemanticIndex* semanticIndex = nullptr,
        HierarchyService* hierarchyService = nullptr);

    // Analyze and plan only. Applying the returned high-risk plan remains the
    // responsibility of WorkspaceEditTransactionService after preview.
    MultiSignalPropagationProposal plan(
        const MultiSignalPropagationQuery& query,
        rtledit::WorkspaceDocumentManager& documents) const;

private:
    SemanticIndex* index = nullptr;
    HierarchyService* hierarchy = nullptr;

    SemanticIndex* semanticIndex() const;
    HierarchyService* hierarchyService() const;
};

#endif // MULTISIGNALPROPAGATIONPLANNER_H
