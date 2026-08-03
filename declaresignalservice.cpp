#include "declaresignalservice.h"

#include <QHash>
#include <QSet>
#include <QVector>

#include <algorithm>
#include <utility>

namespace {

DeclareSignalIssue issue(DeclareSignalIssueCode code,
                         DeclareSignalIssueDisposition disposition,
                         const QString& message,
                         const QString& evidenceId = QString())
{
    DeclareSignalIssue result;
    result.code = code;
    result.disposition = disposition;
    result.message = message;
    result.evidenceId = evidenceId;
    return result;
}

void appendIssue(QList<DeclareSignalIssue>* issues,
                 DeclareSignalIssueCode code,
                 DeclareSignalIssueDisposition disposition,
                 const QString& message,
                 const QString& evidenceId = QString())
{
    if (!issues)
        return;
    issues->append(issue(code, disposition, message, evidenceId));
}

bool hasDisposition(const QList<DeclareSignalIssue>& issues,
                    DeclareSignalIssueDisposition disposition)
{
    for (const DeclareSignalIssue& current : issues) {
        if (current.disposition == disposition)
            return true;
    }
    return false;
}

QString dimensionSemanticKey(
    const DeclareSignalDimensionFact& dimension)
{
    return QStringLiteral("%1:%2")
        .arg(dimension.kind == DeclareSignalDimensionKind::Packed
                 ? QStringLiteral("p")
                 : QStringLiteral("u"),
             dimension.canonicalId);
}

QString semanticTypeKey(const DeclareSignalTypeFact& type)
{
    if (type.canonicalTypeId.isEmpty())
        return QString();

    QStringList parts;
    parts.append(type.canonicalTypeId);
    for (const DeclareSignalDimensionFact& dimension :
         type.dimensions) {
        if (dimension.canonicalId.isEmpty())
            return QString();
        parts.append(dimensionSemanticKey(dimension));
    }
    return parts.join(QChar(u'\x1f'));
}

QString declarationTypeKey(const DeclareSignalTypeFact& type)
{
    if (type.declarationShapeId.isEmpty())
        return QString();
    return type.declarationShapeId;
}

template<typename Node, typename IdFunction, typename DependencyFunction>
QStringList stableTopologicalOrder(
    const QList<Node>& nodes,
    IdFunction idFor,
    DependencyFunction dependenciesFor,
    bool* duplicate,
    QStringList* missing,
    bool* cycle)
{
    if (duplicate)
        *duplicate = false;
    if (missing)
        missing->clear();
    if (cycle)
        *cycle = false;

    QHash<QString, int> indexById;
    for (int index = 0; index < nodes.size(); ++index) {
        const QString id = idFor(nodes.at(index));
        if (id.isEmpty() || indexById.contains(id)) {
            if (duplicate)
                *duplicate = true;
            continue;
        }
        indexById.insert(id, index);
    }

    QVector<int> indegree(nodes.size(), 0);
    QVector<QList<int>> outgoing(nodes.size());
    QSet<QString> missingSet;
    for (int index = 0; index < nodes.size(); ++index) {
        QSet<QString> uniqueDependencies;
        for (const QString& dependency :
             dependenciesFor(nodes.at(index))) {
            if (dependency.isEmpty()
                || uniqueDependencies.contains(dependency)) {
                continue;
            }
            uniqueDependencies.insert(dependency);
            const auto dependencyIt =
                indexById.constFind(dependency);
            if (dependencyIt == indexById.constEnd()) {
                missingSet.insert(dependency);
                continue;
            }
            ++indegree[index];
            outgoing[*dependencyIt].append(index);
        }
    }

    QList<int> ready;
    for (int index = 0; index < nodes.size(); ++index) {
        if (indegree.at(index) == 0
            && !idFor(nodes.at(index)).isEmpty()
            && indexById.value(idFor(nodes.at(index)), -1)
                == index) {
            ready.append(index);
        }
    }

    QStringList order;
    while (!ready.isEmpty()) {
        const int current = ready.takeFirst();
        order.append(idFor(nodes.at(current)));
        for (int dependent : outgoing.at(current)) {
            --indegree[dependent];
            if (indegree.at(dependent) == 0) {
                const auto insertIt = std::lower_bound(
                    ready.begin(), ready.end(), dependent);
                ready.insert(insertIt, dependent);
            }
        }
    }

    if (missing) {
        *missing = missingSet.values();
        std::sort(missing->begin(), missing->end());
    }
    if (cycle && order.size() != indexById.size())
        *cycle = true;
    return order;
}

QString renderedDimensions(
    const QList<DeclareSignalDimensionFact>& dimensions,
    DeclareSignalDimensionKind kind)
{
    QStringList rendered;
    for (const DeclareSignalDimensionFact& dimension :
         dimensions) {
        if (dimension.kind != kind
            || !dimension.emitInDeclaration
            || dimension.declarationText.trimmed().isEmpty()) {
            continue;
        }
        rendered.append(dimension.declarationText.trimmed());
    }
    return rendered.join(QLatin1Char(' '));
}

QString evidenceForType(const DeclareSignalTypeFact& type)
{
    return type.provenance.evidenceId;
}

bool isProceduralUse(DeclareSignalUseKind kind)
{
    return kind == DeclareSignalUseKind::ProceduralAssignmentLhs
        || kind == DeclareSignalUseKind::ProceduralReference;
}

bool forcesModuleScope(DeclareSignalUseKind kind)
{
    return kind == DeclareSignalUseKind::NamedPortActual
        || kind == DeclareSignalUseKind::ContinuousAssignmentLhs
        || kind == DeclareSignalUseKind::ModuleItemReference;
}

struct TypeGroup {
    QString semanticKey;
    QList<DeclareSignalTypeFact> declarationForms;
    QStringList evidenceIds;
    bool allExactFormalBindings = true;
    bool allTypeAnalysesComplete = true;
};

bool containsDeclarationForm(
    const QList<DeclareSignalTypeFact>& forms,
    const DeclareSignalTypeFact& candidate)
{
    for (const DeclareSignalTypeFact& form : forms) {
        if (DeclareSignalService::declarationEquivalent(
                form, candidate)) {
            return true;
        }
    }
    return false;
}

QString candidateId(const DeclareSignalTypeFact& type,
                    DeclareSignalObjectKind objectKind,
                    DeclareSignalScopeKind scopeKind,
                    const QString& blockScopeId)
{
    const QString semanticIdentity =
        semanticTypeKey(type);
    if (semanticIdentity.isEmpty()
        || type.declarationShapeId.isEmpty()) {
        return QString();
    }
    return QStringLiteral("%1|%2|%3|%4|%5")
        .arg(semanticIdentity,
             type.declarationShapeId,
             objectKind == DeclareSignalObjectKind::Net
                 ? QStringLiteral("net")
                 : QStringLiteral("variable"),
             scopeKind == DeclareSignalScopeKind::Module
                 ? QStringLiteral("module")
                 : QStringLiteral("block"),
             blockScopeId);
}

} // namespace

bool DeclareSignalFactProvenance::isSlang(
    quint64 expectedRevision) const
{
    return source == DeclareSignalFactSource::Slang
        && revision == expectedRevision
        && !evidenceId.isEmpty();
}

bool DeclareSignalFactProvenance::isTreeSitter(
    quint64 expectedRevision) const
{
    return source == DeclareSignalFactSource::TreeSitter
        && revision == expectedRevision
        && !evidenceId.isEmpty();
}

bool DeclareSignalCandidate::valid() const
{
    return !candidateId.isEmpty()
        && !type.declarationBaseText.trimmed().isEmpty()
        && !type.declarationShapeId.isEmpty()
        && (scopeKind == DeclareSignalScopeKind::Module
            || !blockScopeId.isEmpty());
}

QString DeclareSignalCandidate::declarationText(
    const QString& identifier) const
{
    if (!valid() || identifier.isEmpty())
        return QString();

    QStringList prefixParts;
    if (objectKind == DeclareSignalObjectKind::Net)
        prefixParts.append(QStringLiteral("wire"));
    prefixParts.append(type.declarationBaseText.trimmed());

    const QString packed = renderedDimensions(
        type.dimensions, DeclareSignalDimensionKind::Packed);
    if (!packed.isEmpty())
        prefixParts.append(packed);

    QString result =
        prefixParts.join(QLatin1Char(' '))
        + QLatin1Char(' ') + identifier;
    const QString unpacked = renderedDimensions(
        type.dimensions, DeclareSignalDimensionKind::Unpacked);
    if (!unpacked.isEmpty())
        result += QLatin1Char(' ') + unpacked;
    result += QLatin1Char(';');
    return result;
}

bool DeclareSignalProposal::actionable() const
{
    return classification != DeclareSignalProposalClass::Conflict
        && primaryCandidate() != nullptr;
}

const DeclareSignalCandidate*
DeclareSignalProposal::primaryCandidate() const
{
    if (primaryCandidateIndex < 0
        || primaryCandidateIndex >= candidates.size()) {
        return nullptr;
    }
    return &candidates.at(primaryCandidateIndex);
}

DeclareSignalConstantAnalysis
DeclareSignalService::analyzeConstantFacts(
    const DeclareSignalConstantDependencyFacts& facts,
    quint64 expectedSemanticRevision)
{
    DeclareSignalConstantAnalysis result;

    if (!facts.provenance.isSlang(
            expectedSemanticRevision)) {
        appendIssue(
            &result.issues,
            facts.provenance.source
                    == DeclareSignalFactSource::Slang
                ? DeclareSignalIssueCode::StaleSemanticFact
                : DeclareSignalIssueCode::
                      NonAuthoritativeSemanticFact,
            DeclareSignalIssueDisposition::Conflict,
            QStringLiteral(
                "Constant dependency facts are not from the current "
                "Slang snapshot."),
            facts.provenance.evidenceId);
    }

    bool parameterDuplicate = false;
    bool parameterCycle = false;
    QStringList missingParameters;
    result.parameterEvaluationOrder =
        stableTopologicalOrder(
            facts.parameters,
            [](const DeclareSignalParameterFact& parameter) {
                return parameter.name;
            },
            [](const DeclareSignalParameterFact& parameter) {
                return parameter.dependencies;
            },
            &parameterDuplicate,
            &missingParameters,
            &parameterCycle);

    if (parameterDuplicate) {
        appendIssue(
            &result.issues,
            DeclareSignalIssueCode::DuplicateConstantNode,
            DeclareSignalIssueDisposition::Conflict,
            QStringLiteral(
                "The parameter dependency graph has an empty or "
                "duplicate parameter identity."));
    }
    if (!missingParameters.isEmpty()) {
        appendIssue(
            &result.issues,
            DeclareSignalIssueCode::MissingConstantDependency,
            DeclareSignalIssueDisposition::Uncertain,
            QStringLiteral(
                "The parameter dependency graph is missing: %1")
                .arg(missingParameters.join(
                    QStringLiteral(", "))));
    }
    if (parameterCycle) {
        appendIssue(
            &result.issues,
            DeclareSignalIssueCode::ConstantDependencyCycle,
            DeclareSignalIssueDisposition::Conflict,
            QStringLiteral(
                "The parameter/localparam dependency graph contains "
                "a cycle."));
    }

    bool expressionDuplicate = false;
    bool expressionCycle = false;
    QStringList missingOperands;
    result.expressionEvaluationOrder =
        stableTopologicalOrder(
            facts.evaluationSteps,
            [](const DeclareSignalConstantEvaluationStep& step) {
                return step.id;
            },
            [](const DeclareSignalConstantEvaluationStep& step) {
                return step.operandStepIds;
            },
            &expressionDuplicate,
            &missingOperands,
            &expressionCycle);

    if (expressionDuplicate) {
        appendIssue(
            &result.issues,
            DeclareSignalIssueCode::DuplicateConstantNode,
            DeclareSignalIssueDisposition::Conflict,
            QStringLiteral(
                "The constant evaluation graph has an empty or "
                "duplicate step identity."));
    }
    if (!missingOperands.isEmpty()) {
        appendIssue(
            &result.issues,
            DeclareSignalIssueCode::MissingConstantDependency,
            DeclareSignalIssueDisposition::Uncertain,
            QStringLiteral(
                "The constant evaluation graph is missing operands: %1")
                .arg(missingOperands.join(
                    QStringLiteral(", "))));
    }
    if (expressionCycle) {
        appendIssue(
            &result.issues,
            DeclareSignalIssueCode::ConstantDependencyCycle,
            DeclareSignalIssueDisposition::Conflict,
            QStringLiteral(
                "The constant evaluation graph contains a cycle."));
    }

    QSet<QString> evaluationStepIds;
    QSet<QString> parameterNames;
    for (const DeclareSignalParameterFact& parameter :
         facts.parameters) {
        parameterNames.insert(parameter.name);
    }
    for (const DeclareSignalConstantEvaluationStep& step :
         facts.evaluationSteps) {
        evaluationStepIds.insert(step.id);
        if (!step.provenance.isSlang(
                expectedSemanticRevision)) {
            appendIssue(
                &result.issues,
                step.provenance.source
                        == DeclareSignalFactSource::Slang
                    ? DeclareSignalIssueCode::StaleSemanticFact
                    : DeclareSignalIssueCode::
                          NonAuthoritativeSemanticFact,
                DeclareSignalIssueDisposition::Conflict,
                QStringLiteral(
                    "Constant evaluation step %1 is not from the "
                    "current Slang snapshot.")
                    .arg(step.id),
                step.provenance.evidenceId);
        }
        QStringList missingStepParameters;
        for (const QString& dependency :
             step.parameterDependencies) {
            if (!parameterNames.contains(dependency))
                missingStepParameters.append(dependency);
        }
        missingStepParameters.removeDuplicates();
        if (!missingStepParameters.isEmpty()) {
            appendIssue(
                &result.issues,
                DeclareSignalIssueCode::
                    MissingConstantDependency,
                DeclareSignalIssueDisposition::Uncertain,
                QStringLiteral(
                    "Constant evaluation step %1 references missing "
                    "parameters: %2")
                    .arg(step.id,
                         missingStepParameters.join(
                             QStringLiteral(", "))),
                step.provenance.evidenceId);
        }
        if (!step.evaluated
            || step.resultValueText.isEmpty()) {
            appendIssue(
                &result.issues,
                DeclareSignalIssueCode::
                    IncompleteConstantEvaluation,
                DeclareSignalIssueDisposition::Uncertain,
                QStringLiteral(
                    "Constant evaluation step %1 has no exact "
                    "Slang result.")
                    .arg(step.id),
                step.provenance.evidenceId);
        }
    }

    for (const DeclareSignalParameterFact& parameter :
         facts.parameters) {
        if (!parameter.provenance.isSlang(
                expectedSemanticRevision)) {
            appendIssue(
                &result.issues,
                parameter.provenance.source
                        == DeclareSignalFactSource::Slang
                    ? DeclareSignalIssueCode::StaleSemanticFact
                    : DeclareSignalIssueCode::
                          NonAuthoritativeSemanticFact,
                DeclareSignalIssueDisposition::Conflict,
                QStringLiteral(
                    "%1 %2 is not from the current Slang snapshot.")
                    .arg(parameter.localparam
                             ? QStringLiteral("localparam")
                             : QStringLiteral("parameter"),
                         parameter.name),
                parameter.provenance.evidenceId);
        }
        if (parameter.rootEvaluationStepId.isEmpty()
            || !evaluationStepIds.contains(
                parameter.rootEvaluationStepId)
            || !parameter.evaluated
            || parameter.valueText.isEmpty()) {
            appendIssue(
                &result.issues,
                DeclareSignalIssueCode::
                    IncompleteConstantEvaluation,
                DeclareSignalIssueDisposition::Uncertain,
                QStringLiteral(
                    "%1 %2 has no complete Slang evaluation trace.")
                    .arg(parameter.localparam
                             ? QStringLiteral("localparam")
                             : QStringLiteral("parameter"),
                         parameter.name),
                parameter.provenance.evidenceId);
        }
    }

    if (!facts.complete) {
        appendIssue(
            &result.issues,
            DeclareSignalIssueCode::
                IncompleteConstantEvaluation,
            DeclareSignalIssueDisposition::Uncertain,
            QStringLiteral(
                "Slang marked the constant dependency facts "
                "incomplete."),
            facts.provenance.evidenceId);
    }

    result.conflict = hasDisposition(
        result.issues,
        DeclareSignalIssueDisposition::Conflict);
    result.complete = !result.conflict
        && !hasDisposition(
            result.issues,
            DeclareSignalIssueDisposition::Uncertain);
    return result;
}

DeclareSignalTypeAnalysis DeclareSignalService::analyzeTypeFact(
    const DeclareSignalTypeFact& fact,
    quint64 expectedSemanticRevision)
{
    DeclareSignalTypeAnalysis result;

    if (!fact.provenance.isSlang(
            expectedSemanticRevision)) {
        appendIssue(
            &result.issues,
            fact.provenance.source
                    == DeclareSignalFactSource::Slang
                ? DeclareSignalIssueCode::StaleSemanticFact
                : DeclareSignalIssueCode::
                      NonAuthoritativeSemanticFact,
            DeclareSignalIssueDisposition::Conflict,
            QStringLiteral(
                "The type fact is not from the current Slang "
                "snapshot."),
            fact.provenance.evidenceId);
    }

    if (fact.rootTypeId.isEmpty()
        || fact.canonicalTypeId.isEmpty()
        || fact.declarationShapeId.isEmpty()
        || fact.declarationBaseText.trimmed().isEmpty()) {
        appendIssue(
            &result.issues,
            DeclareSignalIssueCode::IncompleteTypeFact,
            DeclareSignalIssueDisposition::Uncertain,
            QStringLiteral(
                "The Slang type fact lacks a semantic identity or "
                "declaration rendering."),
            fact.provenance.evidenceId);
    }

    QString expectedSource = fact.rootTypeId;
    QSet<QString> visitedTypes;
    visitedTypes.insert(expectedSource);
    for (const DeclareSignalTypedefStep& step :
         fact.typedefChain) {
        result.typedefResolutionOrder.append(
            step.sourceTypeName.isEmpty()
                ? step.sourceTypeId
                : step.sourceTypeName);
        if (!step.provenance.isSlang(
                expectedSemanticRevision)) {
            appendIssue(
                &result.issues,
                step.provenance.source
                        == DeclareSignalFactSource::Slang
                    ? DeclareSignalIssueCode::StaleSemanticFact
                    : DeclareSignalIssueCode::
                          NonAuthoritativeSemanticFact,
                DeclareSignalIssueDisposition::Conflict,
                QStringLiteral(
                    "A typedef resolution step is not from the "
                    "current Slang snapshot."),
                step.provenance.evidenceId);
        }
        if (step.sourceTypeId != expectedSource
            || step.targetTypeId.isEmpty()
            || step.declarationIdentity.isEmpty()) {
            appendIssue(
                &result.issues,
                DeclareSignalIssueCode::BrokenTypedefChain,
                DeclareSignalIssueDisposition::Conflict,
                QStringLiteral(
                    "The typedef resolution chain is not contiguous."),
                step.provenance.evidenceId);
            break;
        }
        if (visitedTypes.contains(step.targetTypeId)) {
            appendIssue(
                &result.issues,
                DeclareSignalIssueCode::TypedefCycle,
                DeclareSignalIssueDisposition::Conflict,
                QStringLiteral(
                    "The typedef resolution chain contains a cycle."),
                step.provenance.evidenceId);
            break;
        }
        visitedTypes.insert(step.targetTypeId);
        expectedSource = step.targetTypeId;
    }
    if (expectedSource != fact.canonicalTypeId) {
        appendIssue(
            &result.issues,
            DeclareSignalIssueCode::BrokenTypedefChain,
            DeclareSignalIssueDisposition::Conflict,
            QStringLiteral(
                "The typedef chain does not terminate at the Slang "
                "canonical type."),
            fact.provenance.evidenceId);
    }

    QSet<QString> dimensionIds;
    QSet<QString> rawDimensionIds;
    QSet<QString> evaluationStepIds;
    for (const DeclareSignalConstantEvaluationStep& step :
         fact.constants.evaluationSteps) {
        evaluationStepIds.insert(step.id);
    }
    for (const DeclareSignalDimensionFact& dimension :
         fact.dimensions) {
        const QString dimensionKey =
            dimensionSemanticKey(dimension);
        if (dimension.canonicalId.isEmpty()
            || dimensionIds.contains(dimensionKey)
            || !dimension.complete
            || !dimension.provenance.isSlang(
                expectedSemanticRevision)
            || (dimension.emitInDeclaration
                && dimension.declarationText.trimmed().isEmpty())) {
            appendIssue(
                &result.issues,
                DeclareSignalIssueCode::InvalidDimensionFact,
                dimension.provenance.source
                            != DeclareSignalFactSource::Slang
                        || (dimension.provenance.source
                                == DeclareSignalFactSource::Slang
                            && dimension.provenance.revision
                                != expectedSemanticRevision)
                    ? DeclareSignalIssueDisposition::Conflict
                    : DeclareSignalIssueDisposition::Uncertain,
                QStringLiteral(
                    "A packed or unpacked dimension is not a "
                    "complete current Slang fact."),
                dimension.provenance.evidenceId);
        }
        dimensionIds.insert(dimensionKey);
        rawDimensionIds.insert(dimension.canonicalId);
        const QStringList boundSteps = {
            dimension.leftEvaluationStepId,
            dimension.rightEvaluationStepId,
        };
        for (const QString& boundStep : boundSteps) {
            if (!boundStep.isEmpty()
                && !evaluationStepIds.contains(boundStep)) {
                appendIssue(
                    &result.issues,
                    DeclareSignalIssueCode::
                        MissingConstantDependency,
                    DeclareSignalIssueDisposition::Uncertain,
                    QStringLiteral(
                        "Dimension %1 references missing constant "
                        "step %2.")
                        .arg(dimension.canonicalId, boundStep),
                    dimension.provenance.evidenceId);
            }
        }
    }
    for (const DeclareSignalTypedefStep& step :
         fact.typedefChain) {
        for (const QString& introducedDimension :
             step.introducedDimensionIds) {
            if (!rawDimensionIds.contains(
                    introducedDimension)) {
                appendIssue(
                    &result.issues,
                    DeclareSignalIssueCode::
                        InvalidDimensionFact,
                    DeclareSignalIssueDisposition::Uncertain,
                    QStringLiteral(
                        "Typedef %1 references missing dimension %2.")
                        .arg(step.sourceTypeName,
                             introducedDimension),
                    step.provenance.evidenceId);
            }
        }
    }

    if (!fact.constants.evaluationSteps.isEmpty()
        || !fact.constants.parameters.isEmpty()) {
        result.constants = analyzeConstantFacts(
            fact.constants, expectedSemanticRevision);
        result.issues.append(result.constants.issues);
    } else {
        result.constants.complete = true;
    }

    if (!fact.complete) {
        appendIssue(
            &result.issues,
            DeclareSignalIssueCode::IncompleteTypeFact,
            DeclareSignalIssueDisposition::Uncertain,
            QStringLiteral(
                "Slang marked the resolved type fact incomplete."),
            fact.provenance.evidenceId);
    }

    result.conflict = hasDisposition(
        result.issues,
        DeclareSignalIssueDisposition::Conflict);
    result.complete = !result.conflict
        && !hasDisposition(
            result.issues,
            DeclareSignalIssueDisposition::Uncertain);
    return result;
}

bool DeclareSignalService::semanticallyEquivalent(
    const DeclareSignalTypeFact& left,
    const DeclareSignalTypeFact& right)
{
    const QString leftKey = semanticTypeKey(left);
    return !leftKey.isEmpty()
        && leftKey == semanticTypeKey(right);
}

bool DeclareSignalService::declarationEquivalent(
    const DeclareSignalTypeFact& left,
    const DeclareSignalTypeFact& right)
{
    const QString leftKey = declarationTypeKey(left);
    return !leftKey.isEmpty()
        && leftKey == declarationTypeKey(right)
        && semanticallyEquivalent(left, right);
}

DeclareSignalProposal DeclareSignalService::propose(
    const DeclareSignalRequest& request)
{
    DeclareSignalProposal proposal;
    proposal.identifier = request.identifier;
    proposal.moduleName = request.currentModuleName;

    if (request.identifier.isEmpty()
        || !request.identifierAcceptedBySyntax) {
        appendIssue(
            &proposal.issues,
            DeclareSignalIssueCode::InvalidIdentifier,
            DeclareSignalIssueDisposition::Conflict,
            QStringLiteral(
                "Tree-sitter did not accept the selected identifier."));
    }
    if (request.currentModuleName.isEmpty()) {
        appendIssue(
            &proposal.issues,
            DeclareSignalIssueCode::MissingModule,
            DeclareSignalIssueDisposition::Conflict,
            QStringLiteral(
                "No current Tree-sitter module scope is available."));
    }
    if (request.uses.isEmpty()) {
        appendIssue(
            &proposal.issues,
            DeclareSignalIssueCode::NoUseFacts,
            DeclareSignalIssueDisposition::Uncertain,
            QStringLiteral(
                "No structured use facts are available."));
    }
    if (!request.useSetProvenance.isTreeSitter(
            request.documentRevision)) {
        appendIssue(
            &proposal.issues,
            request.useSetProvenance.source
                    == DeclareSignalFactSource::TreeSitter
                ? DeclareSignalIssueCode::StaleSyntaxFact
                : DeclareSignalIssueCode::
                      NonAuthoritativeSyntaxFact,
            DeclareSignalIssueDisposition::Conflict,
            QStringLiteral(
                "The module use set is not from the current "
                "Tree-sitter document."),
            request.useSetProvenance.evidenceId);
    }
    if (!request.moduleUseSetComplete) {
        appendIssue(
            &proposal.issues,
            DeclareSignalIssueCode::IncompleteModuleUseSet,
            DeclareSignalIssueDisposition::Uncertain,
            QStringLiteral(
                "The current module use set is incomplete."),
            request.useSetProvenance.evidenceId);
    }

    for (const DeclareSignalExistingDeclarationFact& existing :
         request.existingDeclarations) {
        if (existing.name != request.identifier
            || existing.moduleName
                != request.currentModuleName) {
            continue;
        }
        if (!existing.provenance.isSlang(
                request.semanticRevision)) {
            appendIssue(
                &proposal.issues,
                existing.provenance.source
                        == DeclareSignalFactSource::Slang
                    ? DeclareSignalIssueCode::StaleSemanticFact
                    : DeclareSignalIssueCode::
                          NonAuthoritativeSemanticFact,
                DeclareSignalIssueDisposition::Conflict,
                QStringLiteral(
                    "An existing declaration fact is not from the "
                    "current Slang snapshot."),
                existing.provenance.evidenceId);
        } else {
            appendIssue(
                &proposal.issues,
                DeclareSignalIssueCode::ExistingDeclaration,
                DeclareSignalIssueDisposition::Conflict,
                QStringLiteral(
                    "The identifier already has a Slang declaration "
                    "in the current module."),
                existing.declarationIdentity);
        }
    }

    bool hasContinuousAssignment = false;
    bool hasProceduralAssignment = false;
    bool hasNamedPort = false;
    bool hasNonNamedUse = false;
    bool forceModule = false;
    bool allBlockLocal = !request.uses.isEmpty();
    QString commonBlockScope;
    bool netOnly = false;
    bool variableOnly = false;
    bool unknownTypeEvidence = false;
    QList<TypeGroup> typeGroups;

    for (const DeclareSignalUseFact& use : request.uses) {
        if (!use.syntaxProvenance.isTreeSitter(
                request.documentRevision)) {
            appendIssue(
                &proposal.issues,
                use.syntaxProvenance.source
                        == DeclareSignalFactSource::TreeSitter
                    ? DeclareSignalIssueCode::StaleSyntaxFact
                    : DeclareSignalIssueCode::
                          NonAuthoritativeSyntaxFact,
                DeclareSignalIssueDisposition::Conflict,
                QStringLiteral(
                    "A use site is not from the current Tree-sitter "
                    "document."),
                use.syntaxProvenance.evidenceId);
        }
        if (use.moduleName != request.currentModuleName) {
            appendIssue(
                &proposal.issues,
                DeclareSignalIssueCode::CrossModuleUse,
                DeclareSignalIssueDisposition::Conflict,
                QStringLiteral(
                    "A use fact belongs to a different module."),
                use.syntaxProvenance.evidenceId);
        }

        hasContinuousAssignment =
            hasContinuousAssignment
            || use.kind
                == DeclareSignalUseKind::
                       ContinuousAssignmentLhs;
        hasProceduralAssignment =
            hasProceduralAssignment
            || use.kind
                == DeclareSignalUseKind::
                       ProceduralAssignmentLhs;
        hasNamedPort =
            hasNamedPort
            || use.kind
                == DeclareSignalUseKind::NamedPortActual;
        hasNonNamedUse =
            hasNonNamedUse
            || use.kind
                != DeclareSignalUseKind::NamedPortActual;

        if (use.kind
                == DeclareSignalUseKind::NamedPortActual
            && (!use.exactFormalBinding
                || use.formalPortName.isEmpty()
                || use.formalPortIdentity.isEmpty())) {
            appendIssue(
                &proposal.issues,
                DeclareSignalIssueCode::IncompleteTypeFact,
                DeclareSignalIssueDisposition::Uncertain,
                QStringLiteral(
                    "The named-port actual has no exact Slang formal "
                    "binding."),
                use.syntaxProvenance.evidenceId);
        }

        if (forcesModuleScope(use.kind))
            forceModule = true;
        if (!isProceduralUse(use.kind)
            || !use.blockLocalDeclarationAllowed
            || use.syntaxScopeId.isEmpty()) {
            allBlockLocal = false;
        } else if (commonBlockScope.isEmpty()) {
            commonBlockScope = use.syntaxScopeId;
        } else if (commonBlockScope != use.syntaxScopeId) {
            allBlockLocal = false;
        }

        DeclareSignalObjectConstraint constraint =
            use.objectConstraint;
        if (use.kind
            == DeclareSignalUseKind::
                   ContinuousAssignmentLhs) {
            constraint =
                DeclareSignalObjectConstraint::NetOnly;
        } else if (use.kind
                   == DeclareSignalUseKind::
                          ProceduralAssignmentLhs) {
            constraint =
                DeclareSignalObjectConstraint::VariableOnly;
        }
        netOnly = netOnly
            || constraint
                == DeclareSignalObjectConstraint::NetOnly;
        variableOnly = variableOnly
            || constraint
                == DeclareSignalObjectConstraint::VariableOnly;

        if (!use.hasExpectedType) {
            unknownTypeEvidence = true;
            appendIssue(
                &proposal.issues,
                DeclareSignalIssueCode::MissingTypeFact,
                DeclareSignalIssueDisposition::Uncertain,
                QStringLiteral(
                    "A use site has no Slang expected-type fact."),
                use.syntaxProvenance.evidenceId);
            continue;
        }

        const DeclareSignalTypeAnalysis analysis =
            analyzeTypeFact(
                use.expectedType,
                request.semanticRevision);
        proposal.issues.append(analysis.issues);

        const QString semanticKey =
            semanticTypeKey(use.expectedType);
        if (semanticKey.isEmpty()) {
            unknownTypeEvidence = true;
            continue;
        }
        auto groupIt = std::find_if(
            typeGroups.begin(), typeGroups.end(),
            [&semanticKey](const TypeGroup& group) {
                return group.semanticKey == semanticKey;
            });
        if (groupIt == typeGroups.end()) {
            TypeGroup group;
            group.semanticKey = semanticKey;
            group.declarationForms.append(
                use.expectedType);
            group.evidenceIds.append(
                evidenceForType(use.expectedType));
            group.allExactFormalBindings =
                use.kind
                    != DeclareSignalUseKind::NamedPortActual
                || use.exactFormalBinding;
            group.allTypeAnalysesComplete =
                analysis.complete;
            typeGroups.append(std::move(group));
        } else {
            if (!containsDeclarationForm(
                    groupIt->declarationForms,
                    use.expectedType)) {
                groupIt->declarationForms.append(
                    use.expectedType);
            }
            groupIt->evidenceIds.append(
                evidenceForType(use.expectedType));
            groupIt->allExactFormalBindings =
                groupIt->allExactFormalBindings
                && (use.kind
                        != DeclareSignalUseKind::
                               NamedPortActual
                    || use.exactFormalBinding);
            groupIt->allTypeAnalysesComplete =
                groupIt->allTypeAnalysesComplete
                && analysis.complete;
        }
    }

    if (hasContinuousAssignment
        && hasProceduralAssignment) {
        appendIssue(
            &proposal.issues,
            DeclareSignalIssueCode::MixedDriverKinds,
            DeclareSignalIssueDisposition::Conflict,
            QStringLiteral(
                "The signal has both continuous and procedural "
                "assignment sites."));
    }
    if (netOnly && variableOnly) {
        appendIssue(
            &proposal.issues,
            DeclareSignalIssueCode::
                ConflictingObjectConstraints,
            DeclareSignalIssueDisposition::Conflict,
            QStringLiteral(
                "Slang use facts require incompatible net and "
                "variable object kinds."));
    }
    if (typeGroups.size() > 1) {
        appendIssue(
            &proposal.issues,
            DeclareSignalIssueCode::ConflictingTypes,
            DeclareSignalIssueDisposition::Conflict,
            QStringLiteral(
                "Current-module use sites have incompatible Slang "
                "semantic types."));
    }
    if (typeGroups.size() == 1
        && typeGroups.constFirst().declarationForms.size() > 1) {
        appendIssue(
            &proposal.issues,
            DeclareSignalIssueCode::
                AmbiguousDeclarationShape,
            DeclareSignalIssueDisposition::Uncertain,
            QStringLiteral(
                "Equivalent Slang types have more than one exact "
                "declaration form."));
    }

    DeclareSignalScopeKind scopeKind =
        DeclareSignalScopeKind::Module;
    QString blockScopeId;
    if (!forceModule
        && request.moduleUseSetComplete
        && allBlockLocal
        && !commonBlockScope.isEmpty()) {
        scopeKind = DeclareSignalScopeKind::BlockLocal;
        blockScopeId = commonBlockScope;
    }

    QList<DeclareSignalObjectKind> objectKinds;
    if (netOnly && !variableOnly) {
        objectKinds.append(DeclareSignalObjectKind::Net);
    } else if (variableOnly && !netOnly) {
        objectKinds.append(DeclareSignalObjectKind::Variable);
    } else if (!netOnly && !variableOnly) {
        objectKinds.append(DeclareSignalObjectKind::Variable);
        objectKinds.append(DeclareSignalObjectKind::Net);
        appendIssue(
            &proposal.issues,
            DeclareSignalIssueCode::AmbiguousObjectKind,
            DeclareSignalIssueDisposition::Uncertain,
            QStringLiteral(
                "The Slang facts permit both variable and net "
                "declarations."));
    }

    if (typeGroups.size() == 1
        && !hasDisposition(
            proposal.issues,
            DeclareSignalIssueDisposition::Conflict)) {
        const TypeGroup& group = typeGroups.constFirst();
        for (const DeclareSignalTypeFact& form :
             group.declarationForms) {
            for (DeclareSignalObjectKind objectKind :
                 objectKinds) {
                DeclareSignalCandidate candidate;
                candidate.objectKind = objectKind;
                candidate.scopeKind = scopeKind;
                candidate.blockScopeId = blockScopeId;
                candidate.type = form;
                candidate.evidenceIds = group.evidenceIds;
                candidate.evidenceIds.removeDuplicates();
                candidate.candidateId = ::candidateId(
                    form, objectKind, scopeKind,
                    blockScopeId);
                if (candidate.valid())
                    proposal.candidates.append(
                        std::move(candidate));
            }
        }
    }

    if (hasDisposition(
            proposal.issues,
            DeclareSignalIssueDisposition::Conflict)) {
        proposal.classification =
            DeclareSignalProposalClass::Conflict;
        proposal.primaryCandidateIndex = -1;
        return proposal;
    }

    if (!proposal.candidates.isEmpty())
        proposal.primaryCandidateIndex = 0;

    const bool uncertain =
        unknownTypeEvidence
        || proposal.candidates.size() != 1
        || hasDisposition(
            proposal.issues,
            DeclareSignalIssueDisposition::Uncertain);
    if (uncertain) {
        proposal.classification =
            DeclareSignalProposalClass::Uncertain;
    } else if (hasNamedPort && !hasNonNamedUse
               && typeGroups.size() == 1
               && typeGroups.constFirst()
                      .allExactFormalBindings
               && typeGroups.constFirst()
                      .allTypeAnalysesComplete) {
        proposal.classification =
            DeclareSignalProposalClass::Exact;
    } else {
        proposal.classification =
            DeclareSignalProposalClass::Inferred;
    }
    return proposal;
}
