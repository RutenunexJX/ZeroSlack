#include "declaresignalservice.h"

#include <cstdio>

namespace {

int checks = 0;
int failures = 0;

void expect(const char* label, bool condition)
{
    ++checks;
    if (!condition)
        ++failures;
    std::printf("[%s] %s\n",
                condition ? "PASS" : "FAIL",
                label);
}

DeclareSignalFactProvenance slang(
    quint64 revision,
    const QString& evidence)
{
    return {
        DeclareSignalFactSource::Slang,
        revision,
        evidence,
    };
}

DeclareSignalFactProvenance tree(
    quint64 revision,
    const QString& evidence)
{
    return {
        DeclareSignalFactSource::TreeSitter,
        revision,
        evidence,
    };
}

DeclareSignalConstantEvaluationStep constantStep(
    const QString& id,
    const QStringList& operands,
    const QStringList& parameters,
    const QString& value,
    quint64 revision)
{
    DeclareSignalConstantEvaluationStep step;
    step.id = id;
    step.operationName = operands.isEmpty()
        ? QStringLiteral("literal-or-reference")
        : QStringLiteral("bound-operation");
    step.expressionText = id;
    step.operandStepIds = operands;
    step.parameterDependencies = parameters;
    step.resultValueText = value;
    step.evaluated = true;
    step.provenance =
        slang(revision, QStringLiteral("expr:") + id);
    return step;
}

DeclareSignalConstantDependencyFacts widthDepthConstants(
    quint64 revision)
{
    DeclareSignalConstantDependencyFacts facts;
    facts.provenance =
        slang(revision, QStringLiteral("constants:pixel_t"));
    facts.evaluationSteps = {
        constantStep(QStringLiteral("literal.8"),
                     {}, {}, QStringLiteral("8"), revision),
        constantStep(QStringLiteral("parameter.WIDTH"),
                     {QStringLiteral("literal.8")},
                     {},
                     QStringLiteral("8"), revision),
        constantStep(QStringLiteral("literal.2"),
                     {}, {}, QStringLiteral("2"), revision),
        constantStep(QStringLiteral("localparam.DEPTH"),
                     {QStringLiteral("parameter.WIDTH"),
                      QStringLiteral("literal.2")},
                     {QStringLiteral("WIDTH")},
                     QStringLiteral("16"), revision),
        constantStep(QStringLiteral("literal.1"),
                     {}, {}, QStringLiteral("1"), revision),
        constantStep(QStringLiteral("literal.0"),
                     {}, {}, QStringLiteral("0"), revision),
        constantStep(QStringLiteral("width.high"),
                     {QStringLiteral("parameter.WIDTH"),
                      QStringLiteral("literal.1")},
                     {QStringLiteral("WIDTH")},
                     QStringLiteral("7"), revision),
        constantStep(QStringLiteral("depth.high"),
                     {QStringLiteral("localparam.DEPTH"),
                      QStringLiteral("literal.1")},
                     {QStringLiteral("DEPTH")},
                     QStringLiteral("15"), revision),
    };

    DeclareSignalParameterFact width;
    width.name = QStringLiteral("WIDTH");
    width.rootEvaluationStepId =
        QStringLiteral("parameter.WIDTH");
    width.valueText = QStringLiteral("8");
    width.evaluated = true;
    width.provenance =
        slang(revision, QStringLiteral("parameter:WIDTH"));

    DeclareSignalParameterFact depth;
    depth.name = QStringLiteral("DEPTH");
    depth.localparam = true;
    depth.rootEvaluationStepId =
        QStringLiteral("localparam.DEPTH");
    depth.dependencies = {QStringLiteral("WIDTH")};
    depth.valueText = QStringLiteral("16");
    depth.evaluated = true;
    depth.provenance =
        slang(revision, QStringLiteral("localparam:DEPTH"));

    facts.parameters = {width, depth};
    return facts;
}

DeclareSignalTypeFact pixelType(
    quint64 revision,
    const QString& declarationShape =
        QStringLiteral("pixel_t@pkg::types"))
{
    DeclareSignalTypeFact type;
    type.rootTypeId = QStringLiteral("type:pixel_t");
    type.canonicalTypeId =
        QStringLiteral("type:logic:signed:8");
    type.declarationShapeId = declarationShape;
    type.declarationBaseText = QStringLiteral("pixel_t");
    type.signednessKnown = true;
    type.signedIntegral = true;
    type.complete = true;
    type.provenance =
        slang(revision, QStringLiteral("formal:data"));

    DeclareSignalDimensionFact packed;
    packed.kind = DeclareSignalDimensionKind::Packed;
    packed.canonicalId =
        QStringLiteral("packed:7:0");
    packed.declarationText =
        QStringLiteral("[WIDTH-1:0]");
    packed.leftEvaluationStepId =
        QStringLiteral("width.high");
    packed.rightEvaluationStepId =
        QStringLiteral("literal.0");
    packed.elementCountText = QStringLiteral("8");
    // The packed range is introduced inside pixel_t and must not be emitted
    // again after the typedef name.
    packed.emitInDeclaration = false;
    packed.complete = true;
    packed.provenance =
        slang(revision, QStringLiteral("dimension:pixel_t.packed"));

    DeclareSignalDimensionFact unpacked;
    unpacked.kind = DeclareSignalDimensionKind::Unpacked;
    unpacked.canonicalId =
        QStringLiteral("unpacked:15:0");
    unpacked.declarationText =
        QStringLiteral("[DEPTH-1:0]");
    unpacked.leftEvaluationStepId =
        QStringLiteral("depth.high");
    unpacked.rightEvaluationStepId =
        QStringLiteral("literal.0");
    unpacked.elementCountText = QStringLiteral("16");
    unpacked.emitInDeclaration = true;
    unpacked.complete = true;
    unpacked.provenance =
        slang(revision, QStringLiteral("dimension:data.unpacked"));
    type.dimensions = {packed, unpacked};

    DeclareSignalTypedefStep alias;
    alias.sourceTypeId = QStringLiteral("type:pixel_t");
    alias.sourceTypeName = QStringLiteral("pixel_t");
    alias.targetTypeId =
        QStringLiteral("type:logic:signed:8");
    alias.declarationIdentity =
        QStringLiteral("pkg.sv:typedef:pixel_t");
    alias.introducedDimensionIds = {
        QStringLiteral("packed:7:0"),
    };
    alias.provenance =
        slang(revision, QStringLiteral("typedef:pixel_t"));
    type.typedefChain = {alias};
    type.constants = widthDepthConstants(revision);
    return type;
}

DeclareSignalTypeFact logicType(
    quint64 revision,
    int width,
    const QString& shape = QStringLiteral("logic-shape"))
{
    DeclareSignalTypeFact type;
    type.rootTypeId =
        QStringLiteral("type:logic:%1").arg(width);
    type.canonicalTypeId = type.rootTypeId;
    type.declarationShapeId = shape;
    type.declarationBaseText = QStringLiteral("logic");
    type.signednessKnown = true;
    type.complete = true;
    type.provenance =
        slang(revision,
              QStringLiteral("type:logic:%1").arg(width));
    type.constants.provenance =
        slang(revision, QStringLiteral("constants:none"));

    if (width > 1) {
        DeclareSignalDimensionFact dimension;
        dimension.kind =
            DeclareSignalDimensionKind::Packed;
        dimension.canonicalId =
            QStringLiteral("packed:%1:0").arg(width - 1);
        dimension.declarationText =
            QStringLiteral("[%1:0]").arg(width - 1);
        dimension.elementCountText =
            QString::number(width);
        dimension.complete = true;
        dimension.provenance =
            slang(revision,
                  QStringLiteral("dimension:logic"));
        type.dimensions = {dimension};
    }
    return type;
}

DeclareSignalUseFact namedPortUse(
    const DeclareSignalTypeFact& type,
    quint64 documentRevision,
    const QString& evidence)
{
    DeclareSignalUseFact use;
    use.kind = DeclareSignalUseKind::NamedPortActual;
    use.moduleName = QStringLiteral("top");
    use.sourcePosition = 40;
    use.syntaxProvenance =
        tree(documentRevision, evidence);
    use.hasExpectedType = true;
    use.expectedType = type;
    use.objectConstraint =
        DeclareSignalObjectConstraint::VariableOnly;
    use.formalPortName = QStringLiteral("data");
    use.formalPortIdentity =
        QStringLiteral("child.sv:port:data");
    use.formalDirection = QStringLiteral("input");
    use.exactFormalBinding = true;
    return use;
}

DeclareSignalRequest baseRequest(
    quint64 semanticRevision,
    quint64 documentRevision)
{
    DeclareSignalRequest request;
    request.identifier = QStringLiteral("payload");
    request.currentModuleName = QStringLiteral("top");
    request.semanticRevision = semanticRevision;
    request.documentRevision = documentRevision;
    request.identifierAcceptedBySyntax = true;
    request.moduleUseSetComplete = true;
    request.useSetProvenance =
        tree(documentRevision,
             QStringLiteral("module:top:uses:payload"));
    return request;
}

bool hasIssue(const DeclareSignalProposal& proposal,
              DeclareSignalIssueCode code)
{
    for (const DeclareSignalIssue& current :
         proposal.issues) {
        if (current.code == code)
            return true;
    }
    return false;
}

} // namespace

int main()
{
    constexpr quint64 semanticRevision = 41;
    constexpr quint64 documentRevision = 17;

    const DeclareSignalTypeFact exactPixel =
        pixelType(semanticRevision);
    const DeclareSignalTypeAnalysis pixelAnalysis =
        DeclareSignalService::analyzeTypeFact(
            exactPixel, semanticRevision);
    expect("typedef chain resolves to the terminal Slang type",
           pixelAnalysis.complete
               && !pixelAnalysis.conflict
               && pixelAnalysis.typedefResolutionOrder
                      == QStringList{QStringLiteral("pixel_t")});
    expect("parameter and localparam dependency order is explicit",
           pixelAnalysis.constants.parameterEvaluationOrder
               == QStringList{
                   QStringLiteral("WIDTH"),
                   QStringLiteral("DEPTH")});
    expect("constant expression trace preserves operand order",
           pixelAnalysis.constants.expressionEvaluationOrder
                  .indexOf(QStringLiteral("parameter.WIDTH"))
               < pixelAnalysis.constants.expressionEvaluationOrder
                     .indexOf(QStringLiteral("localparam.DEPTH"))
               && pixelAnalysis.constants.expressionEvaluationOrder
                      .indexOf(QStringLiteral("localparam.DEPTH"))
                  < pixelAnalysis.constants
                        .expressionEvaluationOrder
                        .indexOf(QStringLiteral("depth.high")));

    DeclareSignalRequest named =
        baseRequest(semanticRevision, documentRevision);
    named.uses = {
        namedPortUse(exactPixel,
                     documentRevision,
                     QStringLiteral("inst:u_child.data")),
    };
    const DeclareSignalProposal exact =
        DeclareSignalService::propose(named);
    expect("exact named-port actual produces an Exact proposal",
           exact.classification
                   == DeclareSignalProposalClass::Exact
               && exact.actionable());
    expect("formal actual type and unpacked dimensions are copied",
           exact.primaryCandidate()
               && exact.primaryCandidate()->type.canonicalTypeId
                      == exactPixel.canonicalTypeId
               && exact.primaryCandidate()->type.dimensions.size()
                      == 2
               && exact.primaryCandidate()->declarationText(
                      named.identifier)
                      == QStringLiteral(
                          "pixel_t payload [DEPTH-1:0];"));
    expect("formal direction is evidence and is never copied",
           exact.primaryCandidate()
               && !exact.primaryCandidate()->declarationText(
                       named.identifier)
                       .contains(QStringLiteral("input"))
               && !exact.primaryCandidate()->declarationText(
                       named.identifier)
                       .contains(QStringLiteral("output")));

    DeclareSignalRequest continuous =
        baseRequest(semanticRevision, documentRevision);
    DeclareSignalUseFact continuousUse;
    continuousUse.kind =
        DeclareSignalUseKind::ContinuousAssignmentLhs;
    continuousUse.moduleName = QStringLiteral("top");
    continuousUse.syntaxProvenance =
        tree(documentRevision,
             QStringLiteral("assign:payload"));
    continuousUse.hasExpectedType = true;
    continuousUse.expectedType =
        logicType(semanticRevision, 8);
    continuous.uses = {continuousUse};
    const DeclareSignalProposal continuousProposal =
        DeclareSignalService::propose(continuous);
    expect("continuous assignment LHS prefers a module net",
           continuousProposal.classification
                   == DeclareSignalProposalClass::Inferred
               && continuousProposal.primaryCandidate()
               && continuousProposal.primaryCandidate()->objectKind
                      == DeclareSignalObjectKind::Net
               && continuousProposal.primaryCandidate()->scopeKind
                      == DeclareSignalScopeKind::Module
               && continuousProposal.primaryCandidate()
                      ->declarationText(continuous.identifier)
                      == QStringLiteral(
                          "wire logic [7:0] payload;"));

    DeclareSignalRequest procedural =
        baseRequest(semanticRevision, documentRevision);
    DeclareSignalUseFact proceduralWrite;
    proceduralWrite.kind =
        DeclareSignalUseKind::ProceduralAssignmentLhs;
    proceduralWrite.moduleName = QStringLiteral("top");
    proceduralWrite.syntaxScopeId =
        QStringLiteral("always_ff:state");
    proceduralWrite.blockLocalDeclarationAllowed = true;
    proceduralWrite.syntaxProvenance =
        tree(documentRevision,
             QStringLiteral("always_ff:write"));
    proceduralWrite.hasExpectedType = true;
    proceduralWrite.expectedType =
        logicType(semanticRevision, 8);

    DeclareSignalUseFact proceduralRead = proceduralWrite;
    proceduralRead.kind =
        DeclareSignalUseKind::ProceduralReference;
    proceduralRead.sourcePosition = 110;
    proceduralRead.syntaxProvenance =
        tree(documentRevision,
             QStringLiteral("always_ff:read"));
    procedural.uses = {proceduralWrite, proceduralRead};
    const DeclareSignalProposal localProposal =
        DeclareSignalService::propose(procedural);
    expect("all procedural use points in one block infer block local",
           localProposal.classification
                   == DeclareSignalProposalClass::Inferred
               && localProposal.primaryCandidate()
               && localProposal.primaryCandidate()->objectKind
                      == DeclareSignalObjectKind::Variable
               && localProposal.primaryCandidate()->scopeKind
                      == DeclareSignalScopeKind::BlockLocal
               && localProposal.primaryCandidate()->blockScopeId
                      == QStringLiteral("always_ff:state"));

    proceduralRead.syntaxScopeId =
        QStringLiteral("always_comb:decode");
    procedural.uses = {proceduralWrite, proceduralRead};
    const DeclareSignalProposal moduleProposal =
        DeclareSignalService::propose(procedural);
    expect("procedural uses across blocks infer module signal",
           moduleProposal.primaryCandidate()
               && moduleProposal.primaryCandidate()->scopeKind
                      == DeclareSignalScopeKind::Module);

    DeclareSignalUseFact incompatibleRead =
        proceduralRead;
    incompatibleRead.expectedType =
        logicType(semanticRevision, 16,
                  QStringLiteral("logic16-shape"));
    procedural.uses = {
        proceduralWrite,
        incompatibleRead,
    };
    const DeclareSignalProposal typeConflict =
        DeclareSignalService::propose(procedural);
    expect("incompatible use-point types produce Conflict",
           typeConflict.classification
                   == DeclareSignalProposalClass::Conflict
               && hasIssue(typeConflict,
                           DeclareSignalIssueCode::
                               ConflictingTypes)
               && !typeConflict.actionable());

    DeclareSignalRequest mixed =
        baseRequest(semanticRevision, documentRevision);
    mixed.uses = {continuousUse, proceduralWrite};
    const DeclareSignalProposal mixedProposal =
        DeclareSignalService::propose(mixed);
    expect("continuous and procedural drivers conflict",
           mixedProposal.classification
                   == DeclareSignalProposalClass::Conflict
               && hasIssue(mixedProposal,
                           DeclareSignalIssueCode::
                               MixedDriverKinds));

    DeclareSignalRequest incomplete =
        baseRequest(semanticRevision, documentRevision);
    incomplete.moduleUseSetComplete = false;
    incomplete.uses = {proceduralWrite};
    const DeclareSignalProposal uncertain =
        DeclareSignalService::propose(incomplete);
    expect("incomplete module use set is Uncertain and module scoped",
           uncertain.classification
                   == DeclareSignalProposalClass::Uncertain
               && uncertain.primaryCandidate()
               && uncertain.primaryCandidate()->scopeKind
                      == DeclareSignalScopeKind::Module);

    DeclareSignalRequest existing =
        baseRequest(semanticRevision, documentRevision);
    existing.uses = {proceduralWrite};
    DeclareSignalExistingDeclarationFact declaration;
    declaration.name = QStringLiteral("payload");
    declaration.moduleName = QStringLiteral("top");
    declaration.declarationIdentity =
        QStringLiteral("top.sv:signal:payload");
    declaration.provenance =
        slang(semanticRevision,
              declaration.declarationIdentity);
    existing.existingDeclarations = {declaration};
    const DeclareSignalProposal existingConflict =
        DeclareSignalService::propose(existing);
    expect("existing Slang declaration is an explicit Conflict",
           existingConflict.classification
                   == DeclareSignalProposalClass::Conflict
               && hasIssue(existingConflict,
                           DeclareSignalIssueCode::
                               ExistingDeclaration));

    DeclareSignalRequest stale =
        baseRequest(semanticRevision, documentRevision);
    DeclareSignalUseFact staleUse = proceduralWrite;
    staleUse.expectedType.provenance.revision =
        semanticRevision - 1;
    stale.uses = {staleUse};
    const DeclareSignalProposal staleProposal =
        DeclareSignalService::propose(stale);
    expect("stale Slang facts cannot produce an actionable proposal",
           staleProposal.classification
                   == DeclareSignalProposalClass::Conflict
               && hasIssue(staleProposal,
                           DeclareSignalIssueCode::
                               StaleSemanticFact));

    DeclareSignalRequest declarationAmbiguity =
        baseRequest(semanticRevision, documentRevision);
    DeclareSignalUseFact firstPort =
        namedPortUse(exactPixel,
                     documentRevision,
                     QStringLiteral("inst:a.data"));
    DeclareSignalTypeFact alternateAlias =
        exactPixel;
    alternateAlias.rootTypeId =
        QStringLiteral("type:byte_alias_t");
    alternateAlias.declarationShapeId =
        QStringLiteral("byte_alias_t@pkg::types");
    alternateAlias.declarationBaseText =
        QStringLiteral("byte_alias_t");
    alternateAlias.typedefChain[0].sourceTypeId =
        alternateAlias.rootTypeId;
    alternateAlias.typedefChain[0].sourceTypeName =
        QStringLiteral("byte_alias_t");
    alternateAlias.typedefChain[0].declarationIdentity =
        QStringLiteral("pkg.sv:typedef:byte_alias_t");
    DeclareSignalUseFact secondPort =
        namedPortUse(alternateAlias,
                     documentRevision,
                     QStringLiteral("inst:b.data"));
    declarationAmbiguity.uses = {firstPort, secondPort};
    const DeclareSignalProposal aliasProposal =
        DeclareSignalService::propose(
            declarationAmbiguity);
    expect("equivalent types with different exact spellings stay editable",
           aliasProposal.classification
                   == DeclareSignalProposalClass::Uncertain
               && aliasProposal.candidates.size() == 2
               && hasIssue(aliasProposal,
                           DeclareSignalIssueCode::
                               AmbiguousDeclarationShape));

    DeclareSignalTypeFact cyclic = exactPixel;
    DeclareSignalTypedefStep cycle;
    cycle.sourceTypeId =
        cyclic.canonicalTypeId;
    cycle.sourceTypeName = QStringLiteral("logic8");
    cycle.targetTypeId = cyclic.rootTypeId;
    cycle.declarationIdentity =
        QStringLiteral("pkg.sv:typedef:cycle");
    cycle.provenance =
        slang(semanticRevision,
              QStringLiteral("typedef:cycle"));
    cyclic.typedefChain.append(cycle);
    const DeclareSignalTypeAnalysis cyclicAnalysis =
        DeclareSignalService::analyzeTypeFact(
            cyclic, semanticRevision);
    expect("typedef cycles are rejected structurally",
           cyclicAnalysis.conflict);

    DeclareSignalConstantDependencyFacts missing =
        widthDepthConstants(semanticRevision);
    missing.parameters[1].dependencies.append(
        QStringLiteral("MISSING"));
    const DeclareSignalConstantAnalysis missingAnalysis =
        DeclareSignalService::analyzeConstantFacts(
            missing, semanticRevision);
    expect("missing parameter dependency is Uncertain, not guessed",
           !missingAnalysis.complete
               && !missingAnalysis.conflict);

    DeclareSignalConstantDependencyFacts dependencyCycle =
        widthDepthConstants(semanticRevision);
    dependencyCycle.parameters[0].dependencies = {
        QStringLiteral("DEPTH"),
    };
    const DeclareSignalConstantAnalysis cycleAnalysis =
        DeclareSignalService::analyzeConstantFacts(
            dependencyCycle, semanticRevision);
    expect("parameter/localparam dependency cycles are Conflict",
           cycleAnalysis.conflict);

    DeclareSignalRequest nonAuthoritative =
        baseRequest(semanticRevision, documentRevision);
    DeclareSignalUseFact lexicalUse = proceduralWrite;
    lexicalUse.expectedType.provenance.source =
        DeclareSignalFactSource::TreeSitter;
    nonAuthoritative.uses = {lexicalUse};
    const DeclareSignalProposal nonAuthoritativeProposal =
        DeclareSignalService::propose(nonAuthoritative);
    expect("Tree-sitter type guesses cannot replace Slang facts",
           nonAuthoritativeProposal.classification
                   == DeclareSignalProposalClass::Conflict
               && hasIssue(
                   nonAuthoritativeProposal,
                   DeclareSignalIssueCode::
                       NonAuthoritativeSemanticFact));

    std::printf("%d checks, %d failures\n",
                checks, failures);
    return failures == 0 ? 0 : 1;
}
