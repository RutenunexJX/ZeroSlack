#ifndef RTLBATCHEDITSERVICE_H
#define RTLBATCHEDITSERVICE_H

#include "completiontypes.h"

#include <QList>
#include <QString>
#include <memory>

enum class RtlClearAssignmentRhsStatus {
    Ready,
    EmptySelection,
    NoAssignments,
    UnsupportedSelection,
    AmbiguousAssignment
};

enum class RtlAssignmentStatementKind {
    Blocking,
    NonBlocking,
    ContinuousAssign
};

struct RtlClearAssignmentRhsQuery {
    QString selectedText;
    int selectionStartPosition = 0;
};

struct RtlClearAssignmentRhsEdit {
    RtlAssignmentStatementKind kind = RtlAssignmentStatementKind::Blocking;
    int selectionRhsStart = -1;
    int selectionRhsEnd = -1;
    int documentRhsStart = -1;
    int documentRhsEnd = -1;
    int replacementSlotStart = -1;
    int line = 0;
    int column = 0;
    QString removedText;
};

struct RtlClearAssignmentRhsReport {
    RtlClearAssignmentRhsStatus status =
        RtlClearAssignmentRhsStatus::UnsupportedSelection;
    QString failureReason;
    QString replacementText;
    QList<RtlClearAssignmentRhsEdit> edits;
    CodeTemplateSlotList templateSlots;

    bool canApply() const { return status == RtlClearAssignmentRhsStatus::Ready; }
};

class RtlBatchEditService
{
public:
    static RtlBatchEditService* getInstance();

    RtlClearAssignmentRhsReport planClearAssignmentRhs(
        const RtlClearAssignmentRhsQuery& query) const;

private:
    static std::unique_ptr<RtlBatchEditService> instance;
};

#endif // RTLBATCHEDITSERVICE_H
