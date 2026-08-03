#ifndef RTLRENAMEPLANNER_H
#define RTLRENAMEPLANNER_H

#include "semanticindex.h"

#include <rtledit/edit_plan.h>
#include <rtledit/workspace_edit_transaction.h>

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <memory>

class SemanticIndex;
class TSDocument;

enum class RtlRenamePlanStatus {
    Ready,
    InvalidRequest,
    InvalidNewName,
    NoChange,
    MissingSemanticSnapshot,
    StaleSemanticGeneration,
    SubjectNotFound,
    AmbiguousSubject,
    UnsupportedSubject,
    ConflictingDefinition,
    MissingDocumentSnapshot,
    StaleDocumentRevision,
    InvalidTreeSnapshot,
    SyntaxError,
    OrderedConnection,
    IncompleteSemanticBinding,
    AmbiguousStructure,
    TransactionPreparationFailed
};

struct RtlRenameDocumentSnapshot {
    QString fileName;
    std::uint64_t revision = 0;
    QString text;
    std::shared_ptr<const TSDocument> syntax;
    bool unsaved = false;

    bool isValid() const;
};

struct RtlRenamePlanQuery {
    SymbolStableKey subjectStableKey;
    QString newName;

    // Both members are immutable. The planner also compares this token with
    // SemanticIndex::snapshotToken() immediately before producing a plan.
    SemanticSnapshotToken semanticToken;

    // Every file touched by the proposal must have one exact live document
    // snapshot. The Tree-sitter tree and text must describe the same revision.
    QHash<QString, RtlRenameDocumentSnapshot> documents;
    QStringList workspaceFiles;

    // A dry-run transaction builds the complete preview and diff but cannot
    // be applied through the transaction coordinator.
    bool dryRun = true;
};

struct RtlRenameFilePreview {
    QString fileName;
    std::uint64_t revision = 0;
    int editCount = 0;
};

struct RtlRenameProposal {
    RtlRenamePlanStatus status =
        RtlRenamePlanStatus::InvalidRequest;
    QString message;
    SemanticSymbolRecord subject;
    QString oldName;
    QString newName;
    QList<RtlRenameFilePreview> files;
    QStringList blockers;
    bool dryRun = true;

    rtledit::WorkspaceEditPlan workspaceEdit;
    rtledit::PreparedWorkspaceEditTransaction transaction;
    rtledit::WorkspaceEditSourceDiff sourceDiff;
    QString renderedDiff;

    bool ready() const;
};

class RtlRenamePlanner
{
public:
    explicit RtlRenamePlanner(
        SemanticIndex* semanticIndex = nullptr);

    // Analyze and plan only. This class intentionally has no apply method.
    RtlRenameProposal plan(
        const RtlRenamePlanQuery& query,
        const rtledit::WorkspaceDocumentManager& documents) const;

private:
    SemanticIndex* index = nullptr;

    SemanticIndex* semanticIndex() const;
};

#endif // RTLRENAMEPLANNER_H
