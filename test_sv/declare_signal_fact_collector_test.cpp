#include "declaresignalfactcollector.h"

#include "declaresignalservice.h"
#include "semanticindexsnapshot.h"
#include "slangmanager.h"
#include "tsdocument.h"

#include <QDir>
#include <QHash>
#include <QString>

#include <algorithm>
#include <cstdio>
#include <memory>

namespace {

using CollectionCode = DeclareSignalFactCollectionIssueCode;
using CollectionStatus = DeclareSignalFactCollectionStatus;
using CollectorKind = SymbolTaxonomy::CollectorKind;
using DeclarationKind = SymbolTaxonomy::DeclarationKind;

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

bool hasIssue(
    const DeclareSignalFactCollectionResult& result,
    CollectionCode code)
{
    for (const DeclareSignalFactCollectionIssue& issue :
         result.issues) {
        if (issue.code == code)
            return true;
    }
    return false;
}

int useCount(const DeclareSignalRequest& request,
             DeclareSignalUseKind kind)
{
    int count = 0;
    for (const DeclareSignalUseFact& use : request.uses) {
        if (use.kind == kind)
            ++count;
    }
    return count;
}

SemanticSymbolRecord findRecord(
    const QList<SemanticSymbolRecord>& records,
    const QString& name,
    DeclarationKind declarationKind,
    const QString& owner)
{
    for (const SemanticSymbolRecord& record : records) {
        if (record.name == name
            && record.declarationKind == declarationKind
            && record.owner.name == owner) {
            return record;
        }
    }
    return {};
}

SemanticDeclaredTypeFacts selectedDeclaredTypeFacts(
    const SemanticSymbolRecord& record)
{
    if (!record.presentation.declaredTypeFactsByPath.isEmpty()) {
        QStringList paths =
            record.presentation.declaredTypeFactsByPath.keys();
        std::sort(paths.begin(), paths.end());
        return record.presentation.declaredTypeFactsByPath.value(
            paths.constFirst());
    }
    return record.presentation.defaultDeclaredTypeFacts;
}

const DeclareSignalDimensionFact* dimensionOfKind(
    const DeclareSignalTypeFact& type,
    DeclareSignalDimensionKind kind)
{
    for (const DeclareSignalDimensionFact& dimension :
         type.dimensions) {
        if (dimension.kind == kind)
            return &dimension;
    }
    return nullptr;
}

const DeclareSignalParameterFact* parameterNamed(
    const DeclareSignalConstantDependencyFacts& facts,
    const QString& name)
{
    for (const DeclareSignalParameterFact& parameter :
         facts.parameters) {
        if (parameter.name == name)
            return &parameter;
    }
    return nullptr;
}

DeclareSignalFactCollectionResult collectAt(
    const TSDocument& document,
    const SemanticSnapshotToken& token,
    const QString& fileName,
    const QString& text,
    const QString& identifier,
    quint64 documentRevision = 13,
    quint64 expectedDocumentRevision = 13,
    quint64 expectedSemanticGeneration = 77,
    const QString& hierarchyInstancePath = QString())
{
    DeclareSignalFactCollectionQuery query;
    query.document = &document;
    query.semanticSnapshot = token;
    query.fileName = fileName;
    query.hierarchyInstancePath =
        hierarchyInstancePath;
    query.cursorPosition = text.indexOf(identifier);
    query.expectedSemanticGeneration =
        expectedSemanticGeneration;
    query.documentRevision = documentRevision;
    query.expectedDocumentRevision =
        expectedDocumentRevision;
    return DeclareSignalFactCollector::collect(query);
}

} // namespace

int main()
{
    const QString fileName =
        QDir::current().absoluteFilePath(
            QStringLiteral(
                "declare_signal_fact_fixture.sv"));
    const QString savedSource = QStringLiteral(
        "package types_pkg;\n"
        "  parameter int WIDTH = 8;\n"
        "  localparam int DEPTH = WIDTH * 2;\n"
        "  typedef logic signed [WIDTH-1:0] pixel_base_t;\n"
        "  typedef pixel_base_t pixel_t;\n"
        "endpackage\n"
        "\n"
        "module child #(parameter int N = types_pkg::DEPTH) (\n"
        "  input types_pkg::pixel_t data [N-1:0]\n"
        ");\n"
        "endmodule\n"
        "\n"
        "module top(input logic source, output logic sink);\n"
        "  types_pkg::pixel_t placeholder "
        "[types_pkg::DEPTH-1:0];\n"
        "  child u_child(.data(placeholder));\n"
        "  always_comb begin : saved_block\n"
        "    sink = source;\n"
        "  end\n"
        "endmodule\n");

    const QString currentSource = QStringLiteral(
        "package types_pkg;\n"
        "  parameter int WIDTH = 8;\n"
        "  localparam int DEPTH = WIDTH * 2;\n"
        "  typedef logic signed [WIDTH-1:0] pixel_base_t;\n"
        "  typedef pixel_base_t pixel_t;\n"
        "endpackage\n"
        "\n"
        "module child #(parameter int N = types_pkg::DEPTH) (\n"
        "  input types_pkg::pixel_t data [N-1:0]\n"
        ");\n"
        "endmodule\n"
        "\n"
        "module top(input logic source, output logic sink);\n"
        "  child u_child(.data(payload));\n"
        "  assign wire_missing = source;\n"
        "  always_comb begin : only_block\n"
        "    local_missing = source;\n"
        "    sink = local_missing;\n"
        "  end\n"
        "  always_comb begin : first_block\n"
        "    cross_missing = source;\n"
        "  end\n"
        "  always_comb begin : second_block\n"
        "    sink = cross_missing;\n"
        "  end\n"
        "endmodule\n");

    SlangManager slang;
    QList<SemanticSymbolRecord> records =
        slang.extractSymbolRecords(fileName, savedSource);
    SemanticSymbolRecord formal =
        findRecord(records,
                   QStringLiteral("data"),
                   DeclarationKind::Port,
                   QStringLiteral("child"));
    const SemanticSymbolRecord width =
        findRecord(records,
                   QStringLiteral("WIDTH"),
                   DeclarationKind::Parameter,
                   QStringLiteral("types_pkg"));
    const SemanticSymbolRecord depth =
        findRecord(records,
                   QStringLiteral("DEPTH"),
                   DeclarationKind::Localparam,
                   QStringLiteral("types_pkg"));
    const SemanticSymbolRecord pixelTypedef =
        findRecord(records,
                   QStringLiteral("pixel_t"),
                   DeclarationKind::Typedef,
                   QStringLiteral("types_pkg"));
    const SemanticSymbolRecord placeholder =
        findRecord(records,
                   QStringLiteral("placeholder"),
                   DeclarationKind::Signal,
                   QStringLiteral("top"));
    expect("real Slang fixture publishes formal, typedef, parameter and localparam",
           formal.isValid()
               && pixelTypedef.isValid()
               && width.isValid()
               && depth.isValid()
               && placeholder.isValid());

    const SemanticDeclaredTypeFacts formalType =
        selectedDeclaredTypeFacts(formal);
    const SemanticDeclaredTypeFacts aliasType =
        selectedDeclaredTypeFacts(pixelTypedef);
    const SemanticDeclaredTypeFacts variableType =
        selectedDeclaredTypeFacts(placeholder);
    if (!formalType.complete || !aliasType.complete) {
        std::fprintf(
            stderr,
            "formal facts: complete=%d root=%s canonical=%s shape=%s "
            "base=%s dimensions=%lld typedefs=%lld eval=%lld params=%lld "
            "constantsComplete=%d failure=%s\n",
            formalType.complete,
            qPrintable(formalType.rootTypeId),
            qPrintable(formalType.canonicalTypeId),
            qPrintable(formalType.declarationShapeId),
            qPrintable(formalType.declarationBaseText),
            static_cast<long long>(formalType.dimensions.size()),
            static_cast<long long>(formalType.typedefChain.size()),
            static_cast<long long>(
                formalType.constants.evaluationNodes.size()),
            static_cast<long long>(
                formalType.constants.parameters.size()),
            formalType.constants.complete,
            qPrintable(formalType.failureReason));
        std::fprintf(
            stderr,
            "formal constant failure=%s\n",
            qPrintable(formalType.constants.failureReason));
        for (const SemanticTypeDimensionFact& dimension :
             formalType.dimensions) {
            std::fprintf(
                stderr,
                "dimension kind=%d text=%s complete=%d left=%s right=%s\n",
                static_cast<int>(dimension.kind),
                qPrintable(dimension.declarationText),
                dimension.complete,
                qPrintable(dimension.leftEvaluationNodeId),
                qPrintable(dimension.rightEvaluationNodeId));
        }
        for (const SemanticParameterDependencyNode& parameter :
             formalType.constants.parameters) {
            std::fprintf(
                stderr,
                "parameter name=%s evaluated=%d root=%s deps=%lld "
                "value=%s\n",
                qPrintable(parameter.name),
                parameter.evaluated,
                qPrintable(parameter.rootEvaluationNodeId),
                static_cast<long long>(
                    parameter.dependencyIds.size()),
                qPrintable(parameter.valueText));
        }
        std::fprintf(
            stderr,
            "alias facts: complete=%d root=%s canonical=%s shape=%s "
            "base=%s dimensions=%lld typedefs=%lld eval=%lld params=%lld "
            "constantsComplete=%d failure=%s\n",
            aliasType.complete,
            qPrintable(aliasType.rootTypeId),
            qPrintable(aliasType.canonicalTypeId),
            qPrintable(aliasType.declarationShapeId),
            qPrintable(aliasType.declarationBaseText),
            static_cast<long long>(aliasType.dimensions.size()),
            static_cast<long long>(aliasType.typedefChain.size()),
            static_cast<long long>(
                aliasType.constants.evaluationNodes.size()),
            static_cast<long long>(
                aliasType.constants.parameters.size()),
            aliasType.constants.complete,
            qPrintable(aliasType.failureReason));
    }
    expect("Slang publishes complete machine-readable formal type facts",
           formalType.complete
               && !formalType.rootTypeId.isEmpty()
               && !formalType.canonicalTypeId.isEmpty()
               && !formalType.declarationShapeId.isEmpty()
               && formalType.declarationBaseText
                      == QStringLiteral("types_pkg::pixel_t")
               && formalType.dimensions.size() == 2);
    expect("typedef record and formal both expose a target chain",
           aliasType.complete
               && aliasType.typedefChain.size() == 2
               && formalType.typedefChain.size() == 2
               && !formalType.typedefChain.constLast()
                       .targetTypeId.isEmpty()
               && formalType.typedefChain.constLast()
                      .targetTypeId
                      == formalType.canonicalTypeId
               && formalType.typedefChain.constFirst()
                      .targetTypeId
                      == formalType.typedefChain.constLast()
                             .sourceTypeId
               && formalType.typedefChain.constLast()
                      .introducedDimensionIds.size() == 1);
    expect("Slang variables publish the same structured type chain",
           variableType.complete
               && variableType.canonicalTypeId
                      == formalType.canonicalTypeId
               && variableType.typedefChain.size() == 2
               && variableType.dimensions.size() == 2);
    expect("bound nodes and parameter/localparam DAG come from Slang",
           formalType.constants.complete
               && !formalType.constants.evaluationNodes.isEmpty()
               && formalType.constants.parameters.size() >= 3);

    // Publish two different elaborated formal types. A source-bound
    // top.u_child query must select only that exact path; an unbound query
    // must remain ambiguous instead of guessing a width.
    for (SemanticSymbolRecord& record : records) {
        if (record.name != QStringLiteral("data")
            || record.declarationKind
                   != DeclarationKind::Port
            || record.owner.name
                   != QStringLiteral("child")) {
            continue;
        }
        SemanticElaboratedSymbolInfo exactInfo =
            record.presentation.defaultInfo;
        if (!record.presentation.instanceInfoByPath.isEmpty()) {
            exactInfo =
                record.presentation.instanceInfoByPath
                    .constBegin()
                    .value();
        }
        SemanticDeclaredTypeFacts exactFacts =
            record.presentation.defaultDeclaredTypeFacts;
        if (!record.presentation
                 .declaredTypeFactsByPath.isEmpty()) {
            exactFacts =
                record.presentation
                    .declaredTypeFactsByPath
                    .constBegin()
                    .value();
        }
        SemanticElaboratedSymbolInfo alternateInfo =
            exactInfo;
        alternateInfo.bitWidth += 1;
        alternateInfo.resolvedTypeText +=
            QStringLiteral(" alternate");
        SemanticDeclaredTypeFacts alternateFacts =
            exactFacts;
        alternateFacts.canonicalTypeId +=
            QStringLiteral(":alternate");
        alternateFacts.declarationShapeId +=
            QStringLiteral(":alternate");
        record.presentation.instanceInfoByPath.insert(
            QStringLiteral("top.u_child"),
            exactInfo);
        record.presentation.declaredTypeFactsByPath.insert(
            QStringLiteral("top.u_child"),
            exactFacts);
        record.presentation.instanceInfoByPath.insert(
            QStringLiteral("top.u_other"),
            alternateInfo);
        record.presentation.declaredTypeFactsByPath.insert(
            QStringLiteral("top.u_other"),
            alternateFacts);
    }

    // Poison every legacy renderer field before snapshot construction. The
    // collector must remain exact because it consumes only the structured
    // Slang facts above.
    for (SemanticSymbolRecord& record : records) {
        if (record.name != QStringLiteral("data")
            || record.declarationKind
                   != DeclarationKind::Port
            || record.owner.name
                   != QStringLiteral("child")) {
            continue;
        }
        record.presentation.declarationText =
            QStringLiteral("input renderer_only_bogus");
        record.presentation.packedDimensionsText =
            QStringLiteral("[renderer_only_bogus]");
        record.presentation.unpackedDimensionsText =
            QStringLiteral("[renderer_only_bogus]");
        record.presentation.defaultInfo.resolvedTypeText =
            QStringLiteral("renderer_only_bogus");
        record.presentation.defaultInfo.packedDimensionsText =
            QStringLiteral("[renderer_only_bogus]");
        record.presentation.defaultInfo.unpackedDimensionsText =
            QStringLiteral("[renderer_only_bogus]");
        for (auto it =
                 record.presentation.instanceInfoByPath.begin();
             it != record.presentation.instanceInfoByPath.end();
             ++it) {
            it->resolvedTypeText =
                QStringLiteral("renderer_only_bogus");
            it->packedDimensionsText =
                QStringLiteral("[renderer_only_bogus]");
            it->unpackedDimensionsText =
                QStringLiteral("[renderer_only_bogus]");
        }
    }

    auto snapshot =
        std::make_shared<SemanticIndexSnapshot>(
            SemanticIndexSnapshot::fromSymbolRecords(
                records,
                {},
                {},
                {{fileName, savedSource}}));
    const SemanticSnapshotToken token{snapshot, 77};

    TSDocument document;
    document.setText(currentSource);
    expect("current unsaved fixture is structurally valid",
           !document.hasError());

    const DeclareSignalFactCollectionResult named =
        collectAt(document,
                  token,
                  fileName,
                  currentSource,
                  QStringLiteral("payload"),
                  13,
                  13,
                  77,
                  QStringLiteral("top"));
    expect("named actual binds only to the current snapshot formal",
           named.acceptedForProposal()
               && named.request.identifier
                      == QStringLiteral("payload")
               && named.request.currentModuleName
                      == QStringLiteral("top")
               && named.request.uses.size() == 1
               && named.request.uses.constFirst().kind
                      == DeclareSignalUseKind::NamedPortActual
               && named.request.uses.constFirst()
                      .formalPortName
                      == QStringLiteral("data")
               && named.request.uses.constFirst()
                      .formalDirection
                      == QStringLiteral("input")
               && named.request.uses.constFirst()
                      .exactFormalBinding
               && named.request.uses.constFirst()
                      .syntaxProvenance.isTreeSitter(13)
               && named.request.uses.constFirst()
                      .expectedType.provenance.isSlang(77));
    expect("formal direction remains evidence outside the copied type",
           named.request.uses.constFirst()
                       .expectedType.declarationBaseText
                       .contains(QStringLiteral("input"))
                   == false);
    const DeclareSignalTypeFact& namedType =
        named.request.uses.constFirst().expectedType;
    const DeclareSignalDimensionFact* packed =
        dimensionOfKind(
            namedType, DeclareSignalDimensionKind::Packed);
    const DeclareSignalDimensionFact* unpacked =
        dimensionOfKind(
            namedType, DeclareSignalDimensionKind::Unpacked);
    expect("packed and unpacked dimensions retain machine bounds",
           named.status == CollectionStatus::Ready
               && namedType.complete
               && packed
               && unpacked
               && packed->complete
               && unpacked->complete
               && !packed->leftEvaluationStepId.isEmpty()
               && !packed->rightEvaluationStepId.isEmpty()
               && !unpacked->leftEvaluationStepId.isEmpty()
               && !unpacked->rightEvaluationStepId.isEmpty()
               && !packed->emitInDeclaration
               && unpacked->emitInDeclaration
               && unpacked->declarationText
                      == QStringLiteral("[15:0]")
               && !hasIssue(
                   named,
                   CollectionCode::
                       DimensionDecompositionUnavailable));
    expect("typedef target chain is contiguous and terminates canonically",
           namedType.typedefChain.size() == 2
               && namedType.typedefChain.constFirst()
                      .sourceTypeId
                      == namedType.rootTypeId
               && namedType.typedefChain.constFirst()
                      .targetTypeId
                      == namedType.typedefChain.constLast()
                             .sourceTypeId
               && namedType.typedefChain.constLast()
                      .targetTypeId
                      == namedType.canonicalTypeId
               && !hasIssue(
                   named,
                   CollectionCode::
                       TypedefResolutionUnavailable));
    const DeclareSignalParameterFact* n =
        parameterNamed(
            namedType.constants, QStringLiteral("N"));
    const DeclareSignalParameterFact* depthFact =
        parameterNamed(
            namedType.constants, QStringLiteral("DEPTH"));
    const DeclareSignalParameterFact* widthFact =
        parameterNamed(
            namedType.constants, QStringLiteral("WIDTH"));
    expect("parameter and localparam DAG preserves exact dependencies",
           namedType.constants.complete
               && n
               && depthFact
               && widthFact
               && n->dependencies
                      == QStringList{QStringLiteral("DEPTH")}
               && depthFact->localparam
               && depthFact->dependencies
                      == QStringList{QStringLiteral("WIDTH")}
               && widthFact->dependencies.isEmpty()
               && !namedType.constants.evaluationSteps.isEmpty()
               && !hasIssue(
                   named,
                   CollectionCode::
                       ConstantDependencyGraphUnavailable));
    const DeclareSignalProposal proposal =
        named.proposalInput()
        ? DeclareSignalService::propose(
              *named.proposalInput())
        : DeclareSignalProposal{};
    expect("complete snapshot type produces an exact actionable proposal",
           proposal.classification
                   == DeclareSignalProposalClass::Exact
               && proposal.actionable()
               && proposal.primaryCandidate()
               && proposal.primaryCandidate()
                      ->declarationText(
                          named.request.identifier)
                      == QStringLiteral(
                          "types_pkg::pixel_t payload [15:0];"));

    const DeclareSignalFactCollectionResult unboundNamed =
        collectAt(document,
                  token,
                  fileName,
                  currentSource,
                  QStringLiteral("payload"));
    expect("unbound named actual never guesses between elaborated widths",
           unboundNamed.status
                   == CollectionStatus::Incomplete
               && hasIssue(
                   unboundNamed,
                   CollectionCode::
                       AmbiguousElaboratedFormalType));

    const DeclareSignalFactCollectionResult continuous =
        collectAt(document,
                  token,
                  fileName,
                  currentSource,
                  QStringLiteral("wire_missing"));
    expect("continuous assignment LHS is a module net constraint",
           continuous.acceptedForProposal()
               && continuous.request.moduleUseSetComplete
               && continuous.request.uses.size() == 1
               && continuous.request.uses.constFirst().kind
                      == DeclareSignalUseKind::
                          ContinuousAssignmentLhs
               && continuous.request.uses.constFirst()
                      .objectConstraint
                      == DeclareSignalObjectConstraint::NetOnly
               && continuous.request.uses.constFirst()
                      .syntaxScopeId.isEmpty()
               && continuous.request.uses.constFirst()
                      .hasExpectedType);
    const DeclareSignalProposal continuousProposal =
        DeclareSignalService::propose(
            continuous.request);
    expect("continuous assignment copies structured RHS type and prefers net",
           continuousProposal.classification
                   == DeclareSignalProposalClass::Inferred
               && continuousProposal.primaryCandidate()
               && continuousProposal.primaryCandidate()
                      ->objectKind
                      == DeclareSignalObjectKind::Net
               && continuousProposal.primaryCandidate()
                      ->scopeKind
                      == DeclareSignalScopeKind::Module);

    const DeclareSignalFactCollectionResult local =
        collectAt(document,
                  token,
                  fileName,
                  currentSource,
                  QStringLiteral("local_missing"));
    expect("procedural use aggregation keeps write and read in one block",
           local.request.moduleUseSetComplete
               && useCount(
                      local.request,
                      DeclareSignalUseKind::
                          ProceduralAssignmentLhs)
                      == 1
               && useCount(
                      local.request,
                      DeclareSignalUseKind::
                          ProceduralReference)
                      == 1
               && local.request.uses.size() == 2
               && local.request.uses.at(0)
                      .blockLocalDeclarationAllowed
               && local.request.uses.at(1)
                      .blockLocalDeclarationAllowed
               && local.request.uses.at(0).syntaxScopeId
                      == local.request.uses.at(1)
                             .syntaxScopeId);
    const DeclareSignalProposal localProposal =
        DeclareSignalService::propose(local.request);
    expect("procedural use types come from structured Slang counterparts",
           local.request.uses.at(0).hasExpectedType
               && local.request.uses.at(1).hasExpectedType
               && localProposal.primaryCandidate()
               && localProposal.primaryCandidate()
                      ->scopeKind
                      == DeclareSignalScopeKind::BlockLocal);

    const DeclareSignalFactCollectionResult cross =
        collectAt(document,
                  token,
                  fileName,
                  currentSource,
                  QStringLiteral("cross_missing"));
    expect("procedural uses in two blocks retain distinct structural scopes",
           cross.request.moduleUseSetComplete
               && cross.request.uses.size() == 2
               && cross.request.uses.at(0).syntaxScopeId
                      != cross.request.uses.at(1)
                             .syntaxScopeId);
    const DeclareSignalProposal crossProposal =
        DeclareSignalService::propose(cross.request);
    expect("procedural uses across blocks choose module scope",
           crossProposal.primaryCandidate()
               && crossProposal.primaryCandidate()
                      ->scopeKind
                      == DeclareSignalScopeKind::Module);

    const DeclareSignalFactCollectionResult staleSemantic =
        collectAt(document,
                  token,
                  fileName,
                  currentSource,
                  QStringLiteral("payload"),
                  13,
                  13,
                  78);
    expect("stale SemanticIndex generation is rejected before collection",
           staleSemantic.status
                   == CollectionStatus::Rejected
               && !staleSemantic.acceptedForProposal()
               && staleSemantic.request.uses.isEmpty()
               && hasIssue(
                   staleSemantic,
                   CollectionCode::
                       StaleSemanticGeneration));

    const DeclareSignalFactCollectionResult staleDocument =
        collectAt(document,
                  token,
                  fileName,
                  currentSource,
                  QStringLiteral("payload"),
                  13,
                  14,
                  77);
    expect("stale Tree-sitter document revision is rejected before collection",
           staleDocument.status
                   == CollectionStatus::Rejected
               && !staleDocument.acceptedForProposal()
               && staleDocument.request.uses.isEmpty()
               && hasIssue(
                   staleDocument,
                   CollectionCode::
                       StaleDocumentRevision));

    const QString categorySource =
        QStringLiteral(
            "module category_top;\n"
            "  missing_module u_missing();\n"
            "  missing_pkg::missing_type typed_value;\n"
            "  `MISSING_MACRO\n"
            "endmodule\n");
    TSDocument categoryDocument;
    categoryDocument.setText(categorySource);
    const auto collectCategory =
        [&](const QString& identifier) {
            return collectAt(
                categoryDocument,
                token,
                fileName,
                categorySource,
                identifier);
        };
    const DeclareSignalFactCollectionResult moduleType =
        collectCategory(QStringLiteral("missing_module"));
    const DeclareSignalFactCollectionResult packageQualifier =
        collectCategory(QStringLiteral("missing_pkg"));
    const DeclareSignalFactCollectionResult typeName =
        collectCategory(QStringLiteral("missing_type"));
    const DeclareSignalFactCollectionResult macroName =
        collectCategory(QStringLiteral("MISSING_MACRO"));
    expect("module instance type is rejected as a non-value identifier",
           !moduleType.request.identifierAcceptedBySyntax
               && moduleType.request.uses.isEmpty()
               && hasIssue(
                   moduleType,
                   CollectionCode::NonValueIdentifier));
    expect("package qualifier is rejected as a non-value identifier",
           !packageQualifier.request.identifierAcceptedBySyntax
               && packageQualifier.request.uses.isEmpty()
               && hasIssue(
                   packageQualifier,
                   CollectionCode::NonValueIdentifier));
    expect("scoped type name is rejected as a non-value identifier",
           !typeName.request.identifierAcceptedBySyntax
               && typeName.request.uses.isEmpty()
               && hasIssue(
                   typeName,
                   CollectionCode::NonValueIdentifier));
    expect("macro name is rejected as an unsupported value site",
           !macroName.request.identifierAcceptedBySyntax
               && macroName.request.uses.isEmpty()
               && hasIssue(
                   macroName,
                   CollectionCode::UnsupportedUseSite));

    std::printf("%d checks, %d failures\n",
                checks, failures);
    return failures == 0 ? 0 : 1;
}
