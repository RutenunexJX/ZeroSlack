#ifndef DECLARESIGNALFACTCOLLECTOR_H
#define DECLARESIGNALFACTCOLLECTOR_H

#include "declaresignalservice.h"
#include "semanticindex.h"

#include <QList>
#include <QString>
#include <QtGlobal>

class TSDocument;

enum class DeclareSignalFactCollectionStatus {
    Ready,
    Incomplete,
    Rejected
};

enum class DeclareSignalFactCollectionIssueCode {
    MissingDocument,
    MissingSemanticSnapshot,
    StaleSemanticGeneration,
    StaleDocumentRevision,
    DocumentChangedDuringCollection,
    MissingIdentifier,
    MissingEnclosingModule,
    ModuleSyntaxError,
    CurrentBufferDeclaration,
    NonValueIdentifier,
    UnsupportedUseSite,
    NoUseFacts,
    MissingExpectedTypeEvidence,
    MissingFormalPort,
    AmbiguousFormalPort,
    MissingFormalType,
    AmbiguousElaboratedFormalType,
    MissingMachineReadableTypeIdentity,
    DimensionDecompositionUnavailable,
    TypedefResolutionUnavailable,
    ConstantDependencyGraphUnavailable
};

struct DeclareSignalFactCollectionIssue {
    DeclareSignalFactCollectionIssueCode code =
        DeclareSignalFactCollectionIssueCode::UnsupportedUseSite;
    QString message;
    QString evidenceId;
};

// semanticSnapshot is an immutable token captured from SemanticIndex. The
// expected generation and both document revisions are explicit so a caller
// cannot accidentally combine facts from two analysis / editor generations.
struct DeclareSignalFactCollectionQuery {
    const TSDocument* document = nullptr;
    SemanticSnapshotToken semanticSnapshot;
    QString fileName;
    // Exact elaborated path of the enclosing instance, when the editor is
    // bound to one. A named-port actual appends its source instance name and
    // selects only the matching Slang formal type facts. The collector never
    // guesses between different elaborated widths.
    QString hierarchyInstancePath;
    int cursorPosition = -1;
    quint64 expectedSemanticGeneration = 0;
    quint64 documentRevision = 0;
    quint64 expectedDocumentRevision = 0;
};

struct DeclareSignalFactCollectionResult {
    DeclareSignalFactCollectionStatus status =
        DeclareSignalFactCollectionStatus::Rejected;
    DeclareSignalRequest request;
    QList<DeclareSignalFactCollectionIssue> issues;

    bool acceptedForProposal() const
    {
        return status != DeclareSignalFactCollectionStatus::Rejected;
    }

    bool complete() const
    {
        return status == DeclareSignalFactCollectionStatus::Ready;
    }

    const DeclareSignalRequest* proposalInput() const
    {
        return acceptedForProposal() ? &request : nullptr;
    }
};

// Collects only:
//   * semantic facts from the supplied immutable Slang/SemanticIndex snapshot;
//   * current-buffer structure from the supplied live Tree-sitter document.
// Presentation strings are copied only as render products. They are never
// scanned or parsed to infer SystemVerilog semantics.
class DeclareSignalFactCollector
{
public:
    static DeclareSignalFactCollectionResult collect(
        const DeclareSignalFactCollectionQuery& query);
};

#endif // DECLARESIGNALFACTCOLLECTOR_H
