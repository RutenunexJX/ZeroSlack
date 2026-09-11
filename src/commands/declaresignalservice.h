#ifndef DECLARESIGNALSERVICE_H
#define DECLARESIGNALSERVICE_H

#include <QList>
#include <QString>
#include <QStringList>
#include <QtGlobal>

// DeclareSignalService is deliberately a pure proposal engine. It consumes
// structured facts produced by Slang and Tree-sitter; it never parses source
// text or derives semantic meaning from presentation strings.

enum class DeclareSignalProposalClass {
    Exact,
    Inferred,
    Uncertain,
    Conflict
};

enum class DeclareSignalFactSource {
    Unknown,
    Slang,
    TreeSitter
};

struct DeclareSignalFactProvenance {
    DeclareSignalFactSource source =
        DeclareSignalFactSource::Unknown;
    quint64 revision = 0;
    QString evidenceId;

    bool isSlang(quint64 expectedRevision) const;
    bool isTreeSitter(quint64 expectedRevision) const;
};

enum class DeclareSignalDimensionKind {
    Packed,
    Unpacked
};

// A dimension is already decomposed by the semantic producer. declarationText
// is rendering data only. canonicalId, bound step ids, and elementCountText
// are the machine-readable facts used by the proposal engine.
struct DeclareSignalDimensionFact {
    DeclareSignalDimensionKind kind =
        DeclareSignalDimensionKind::Packed;
    QString canonicalId;
    QString declarationText;
    QString leftEvaluationStepId;
    QString rightEvaluationStepId;
    QString elementCountText;
    bool emitInDeclaration = true;
    bool complete = false;
    DeclareSignalFactProvenance provenance;
};

struct DeclareSignalTypedefStep {
    QString sourceTypeId;
    QString sourceTypeName;
    QString targetTypeId;
    QString declarationIdentity;
    QStringList introducedDimensionIds;
    DeclareSignalFactProvenance provenance;
};

// Each operation and result is supplied by Slang's bound constant expression.
// operandStepIds form a structured evaluation DAG. expressionText and
// operationName are diagnostic rendering only and are never reparsed.
struct DeclareSignalConstantEvaluationStep {
    QString id;
    QString operationName;
    QString expressionText;
    QStringList operandStepIds;
    QStringList parameterDependencies;
    QString resultValueText;
    bool evaluated = false;
    DeclareSignalFactProvenance provenance;
};

struct DeclareSignalParameterFact {
    QString name;
    bool localparam = false;
    QString rootEvaluationStepId;
    QStringList dependencies;
    QString valueText;
    bool evaluated = false;
    DeclareSignalFactProvenance provenance;
};

struct DeclareSignalConstantDependencyFacts {
    QList<DeclareSignalConstantEvaluationStep> evaluationSteps;
    QList<DeclareSignalParameterFact> parameters;
    bool complete = true;
    DeclareSignalFactProvenance provenance;
};

// rootTypeId is the declared type before typedef resolution and
// canonicalTypeId is the terminal Slang type. declarationBaseText is a Slang
// renderer product that excludes dimensions and any port direction.
struct DeclareSignalTypeFact {
    QString rootTypeId;
    QString canonicalTypeId;
    QString declarationShapeId;
    QString declarationBaseText;
    bool signednessKnown = false;
    bool signedIntegral = false;
    bool complete = false;
    QList<DeclareSignalDimensionFact> dimensions;
    QList<DeclareSignalTypedefStep> typedefChain;
    DeclareSignalConstantDependencyFacts constants;
    DeclareSignalFactProvenance provenance;
};

enum class DeclareSignalUseKind {
    NamedPortActual,
    ContinuousAssignmentLhs,
    ProceduralAssignmentLhs,
    ProceduralReference,
    ModuleItemReference
};

enum class DeclareSignalObjectConstraint {
    Any,
    NetOnly,
    VariableOnly
};

// One use fact combines a Tree-sitter structural site with optional Slang
// type information. formalDirection is retained only for evidence display;
// no proposal or declaration model has a direction field.
struct DeclareSignalUseFact {
    DeclareSignalUseKind kind =
        DeclareSignalUseKind::ModuleItemReference;
    QString moduleName;
    QString syntaxScopeId;
    bool blockLocalDeclarationAllowed = false;
    int sourcePosition = -1;
    DeclareSignalFactProvenance syntaxProvenance;

    bool hasExpectedType = false;
    DeclareSignalTypeFact expectedType;
    DeclareSignalObjectConstraint objectConstraint =
        DeclareSignalObjectConstraint::Any;

    QString formalPortName;
    QString formalPortIdentity;
    QString formalDirection;
    bool exactFormalBinding = false;
};

struct DeclareSignalExistingDeclarationFact {
    QString name;
    QString moduleName;
    QString declarationIdentity;
    DeclareSignalFactProvenance provenance;
};

struct DeclareSignalRequest {
    QString identifier;
    QString currentModuleName;
    quint64 semanticRevision = 0;
    quint64 documentRevision = 0;

    // This acceptance decision is made by the current-buffer Tree-sitter
    // producer; the proposal engine deliberately does not lex the identifier.
    bool identifierAcceptedBySyntax = false;
    bool moduleUseSetComplete = false;
    DeclareSignalFactProvenance useSetProvenance;

    QList<DeclareSignalUseFact> uses;
    QList<DeclareSignalExistingDeclarationFact> existingDeclarations;
};

enum class DeclareSignalObjectKind {
    Net,
    Variable
};

enum class DeclareSignalScopeKind {
    Module,
    BlockLocal
};

enum class DeclareSignalIssueDisposition {
    Note,
    Uncertain,
    Conflict
};

enum class DeclareSignalIssueCode {
    InvalidIdentifier,
    MissingModule,
    NoUseFacts,
    NonAuthoritativeSyntaxFact,
    NonAuthoritativeSemanticFact,
    StaleSyntaxFact,
    StaleSemanticFact,
    IncompleteModuleUseSet,
    ExistingDeclaration,
    MixedDriverKinds,
    ConflictingObjectConstraints,
    MissingTypeFact,
    IncompleteTypeFact,
    ConflictingTypes,
    AmbiguousDeclarationShape,
    AmbiguousObjectKind,
    BrokenTypedefChain,
    TypedefCycle,
    DuplicateConstantNode,
    MissingConstantDependency,
    ConstantDependencyCycle,
    IncompleteConstantEvaluation,
    InvalidDimensionFact,
    CrossModuleUse
};

struct DeclareSignalIssue {
    DeclareSignalIssueCode code =
        DeclareSignalIssueCode::MissingTypeFact;
    DeclareSignalIssueDisposition disposition =
        DeclareSignalIssueDisposition::Uncertain;
    QString message;
    QString evidenceId;
};

struct DeclareSignalConstantAnalysis {
    bool complete = false;
    bool conflict = false;
    QStringList parameterEvaluationOrder;
    QStringList expressionEvaluationOrder;
    QList<DeclareSignalIssue> issues;
};

struct DeclareSignalTypeAnalysis {
    bool complete = false;
    bool conflict = false;
    QStringList typedefResolutionOrder;
    DeclareSignalConstantAnalysis constants;
    QList<DeclareSignalIssue> issues;
};

struct DeclareSignalCandidate {
    QString candidateId;
    DeclareSignalObjectKind objectKind =
        DeclareSignalObjectKind::Variable;
    DeclareSignalScopeKind scopeKind =
        DeclareSignalScopeKind::Module;
    QString blockScopeId;
    DeclareSignalTypeFact type;
    QStringList evidenceIds;

    bool valid() const;
    QString declarationText(const QString& identifier) const;
};

struct DeclareSignalProposal {
    DeclareSignalProposalClass classification =
        DeclareSignalProposalClass::Uncertain;
    QString identifier;
    QString moduleName;
    QList<DeclareSignalCandidate> candidates;
    int primaryCandidateIndex = -1;
    QList<DeclareSignalIssue> issues;

    bool actionable() const;
    const DeclareSignalCandidate* primaryCandidate() const;
};

class DeclareSignalService
{
public:
    static DeclareSignalConstantAnalysis analyzeConstantFacts(
        const DeclareSignalConstantDependencyFacts& facts,
        quint64 expectedSemanticRevision);
    static DeclareSignalTypeAnalysis analyzeTypeFact(
        const DeclareSignalTypeFact& fact,
        quint64 expectedSemanticRevision);
    static DeclareSignalProposal propose(
        const DeclareSignalRequest& request);

    static bool semanticallyEquivalent(
        const DeclareSignalTypeFact& left,
        const DeclareSignalTypeFact& right);
    static bool declarationEquivalent(
        const DeclareSignalTypeFact& left,
        const DeclareSignalTypeFact& right);
};

#endif // DECLARESIGNALSERVICE_H
