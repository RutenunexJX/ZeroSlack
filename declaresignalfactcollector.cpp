#include "declaresignalfactcollector.h"

#include "semanticindexsnapshot.h"
#include "tsdocument.h"

#include <QHash>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <cstring>
#include <utility>

namespace {

using CollectionCode = DeclareSignalFactCollectionIssueCode;
using CollectionStatus = DeclareSignalFactCollectionStatus;
using CollectorKind = SymbolTaxonomy::CollectorKind;
using DeclarationKind = SymbolTaxonomy::DeclarationKind;

int nodeStartChar(TSNode node)
{
    return ts_node_is_null(node)
        ? -1
        : static_cast<int>(ts_node_start_byte(node) / 2u);
}

int nodeEndChar(TSNode node)
{
    return ts_node_is_null(node)
        ? -1
        : static_cast<int>(ts_node_end_byte(node) / 2u);
}

bool nodeTypeIs(TSNode node, const char* expected)
{
    if (ts_node_is_null(node) || !expected)
        return false;
    const char* type = ts_node_type(node);
    return type && std::strcmp(type, expected) == 0;
}

bool identifierNode(TSNode node)
{
    return nodeTypeIs(node, "simple_identifier")
        || nodeTypeIs(node, "escaped_identifier");
}

bool sameNode(TSNode left, TSNode right)
{
    return !ts_node_is_null(left)
        && !ts_node_is_null(right)
        && ts_node_eq(left, right);
}

QString nodeText(const QString& text, TSNode node)
{
    const int start = nodeStartChar(node);
    const int end = nodeEndChar(node);
    if (start < 0 || end <= start || end > text.size())
        return QString();
    return text.mid(start, end - start);
}

bool containsNode(TSNode outer, TSNode inner)
{
    if (ts_node_is_null(outer) || ts_node_is_null(inner))
        return false;
    return ts_node_start_byte(outer) <= ts_node_start_byte(inner)
        && ts_node_end_byte(outer) >= ts_node_end_byte(inner);
}

TSNode childByField(TSNode node, const char* field)
{
    if (ts_node_is_null(node) || !field)
        return {};
    return ts_node_child_by_field_name(
        node, field, static_cast<uint32_t>(std::strlen(field)));
}

TSNode ancestorOfType(TSNode node, const char* expected)
{
    while (!ts_node_is_null(node)) {
        if (nodeTypeIs(node, expected))
            return node;
        node = ts_node_parent(node);
    }
    return {};
}

bool isRtlContainer(TSNode node)
{
    return nodeTypeIs(node, "module_declaration")
        || nodeTypeIs(node, "interface_declaration")
        || nodeTypeIs(node, "program_declaration");
}

TSNode containingRtlContainer(TSNode root, int position)
{
    if (ts_node_is_null(root) || position < 0)
        return {};

    const uint32_t byte = static_cast<uint32_t>(position) * 2u;
    TSNode current = ts_node_named_descendant_for_byte_range(
        root, byte, byte);
    while (!ts_node_is_null(current)) {
        if (isRtlContainer(current))
            return current;
        current = ts_node_parent(current);
    }
    return {};
}

TSNode firstDirectIdentifier(TSNode node)
{
    const uint32_t count = ts_node_named_child_count(node);
    for (uint32_t index = 0; index < count; ++index) {
        const TSNode child = ts_node_named_child(node, index);
        if (identifierNode(child))
            return child;
    }
    return {};
}

TSNode firstIdentifierDescendant(TSNode node)
{
    if (ts_node_is_null(node))
        return {};
    if (identifierNode(node))
        return node;

    const uint32_t count = ts_node_named_child_count(node);
    for (uint32_t index = 0; index < count; ++index) {
        const TSNode result =
            firstIdentifierDescendant(
                ts_node_named_child(node, index));
        if (!ts_node_is_null(result))
            return result;
    }
    return {};
}

bool isDirectNamedChild(TSNode parent, TSNode child)
{
    if (ts_node_is_null(parent) || ts_node_is_null(child))
        return false;
    const uint32_t count = ts_node_named_child_count(parent);
    for (uint32_t index = 0; index < count; ++index) {
        if (sameNode(ts_node_named_child(parent, index), child))
            return true;
    }
    return false;
}

bool isField(TSNode parent, TSNode child, const char* field)
{
    return sameNode(childByField(parent, field), child);
}

enum class IdentifierRole {
    ValueUse,
    Declaration,
    NonValue,
    Unsupported
};

IdentifierRole declarationOrNonValueRole(TSNode identifier)
{
    const TSNode parent = ts_node_parent(identifier);
    if (ts_node_is_null(parent))
        return IdentifierRole::Unsupported;

    if (nodeTypeIs(parent, "variable_decl_assignment")
        && isField(parent, identifier, "name")) {
        return IdentifierRole::Declaration;
    }
    if (nodeTypeIs(parent, "ansi_port_declaration")
        && isField(parent, identifier, "port_name")) {
        return IdentifierRole::Declaration;
    }
    if ((nodeTypeIs(parent, "net_decl_assignment")
         || nodeTypeIs(parent, "param_assignment"))
        && sameNode(firstDirectIdentifier(parent), identifier)) {
        return IdentifierRole::Declaration;
    }
    if ((nodeTypeIs(parent, "list_of_port_identifiers")
         || nodeTypeIs(parent, "list_of_variable_identifiers")
         || nodeTypeIs(parent,
                       "list_of_variable_port_identifiers"))
        && isDirectNamedChild(parent, identifier)) {
        return IdentifierRole::Declaration;
    }

    if ((nodeTypeIs(parent, "module_ansi_header")
         || nodeTypeIs(parent, "module_nonansi_header")
         || nodeTypeIs(parent, "interface_ansi_header")
         || nodeTypeIs(parent, "interface_nonansi_header")
         || nodeTypeIs(parent, "program_ansi_header")
         || nodeTypeIs(parent, "program_nonansi_header"))
        && isField(parent, identifier, "name")) {
        return IdentifierRole::NonValue;
    }
    if (nodeTypeIs(parent, "type_declaration")
        && isField(parent, identifier, "type_name")) {
        return IdentifierRole::NonValue;
    }
    if (nodeTypeIs(parent, "package_scope")
        || nodeTypeIs(parent, "type_identifier")
        || nodeTypeIs(parent, "ps_type_identifier")
        || nodeTypeIs(parent, "class_identifier")
        || nodeTypeIs(parent, "interface_identifier")) {
        return IdentifierRole::NonValue;
    }
    if ((nodeTypeIs(parent, "data_type")
         || nodeTypeIs(parent, "class_type"))
        && isDirectNamedChild(parent, identifier)) {
        return IdentifierRole::NonValue;
    }
    if (!ts_node_is_null(
            ancestorOfType(identifier, "package_scope"))) {
        return IdentifierRole::NonValue;
    }
    if (nodeTypeIs(parent, "name_of_instance")
        && isField(parent, identifier, "instance_name")) {
        return IdentifierRole::NonValue;
    }
    if (nodeTypeIs(parent, "module_instantiation")
        && isField(parent, identifier, "instance_type")) {
        return IdentifierRole::NonValue;
    }
    if (nodeTypeIs(parent, "named_port_connection")
        && isField(parent, identifier, "port_name")) {
        return IdentifierRole::NonValue;
    }
    if ((nodeTypeIs(parent, "seq_block")
         || nodeTypeIs(parent, "par_block"))
        && isDirectNamedChild(parent, identifier)) {
        return IdentifierRole::NonValue;
    }

    TSNode current = parent;
    while (!ts_node_is_null(current)) {
        if (nodeTypeIs(current, "text_macro_usage")
            || nodeTypeIs(current, "text_macro_definition")) {
            return IdentifierRole::Unsupported;
        }
        if (isRtlContainer(current))
            break;
        current = ts_node_parent(current);
    }
    return IdentifierRole::ValueUse;
}

bool singleIdentifierExpression(TSNode expression,
                                TSNode expectedIdentifier)
{
    if (ts_node_is_null(expression)
        || ts_node_is_null(expectedIdentifier)) {
        return false;
    }

    int identifierCount = 0;
    bool expectedSeen = false;
    bool unsupportedLeaf = false;
    QList<TSNode> pending{expression};
    while (!pending.isEmpty()) {
        const TSNode node = pending.takeLast();
        const uint32_t childCount = ts_node_child_count(node);
        if (childCount == 0) {
            if (identifierNode(node)) {
                ++identifierCount;
                expectedSeen =
                    expectedSeen || sameNode(node, expectedIdentifier);
                continue;
            }
            if (!ts_node_is_named(node)) {
                const char* type = ts_node_type(node);
                if (type
                    && (std::strcmp(type, "(") == 0
                        || std::strcmp(type, ")") == 0)) {
                    continue;
                }
            }
            unsupportedLeaf = true;
            continue;
        }
        for (uint32_t index = 0; index < childCount; ++index)
            pending.append(ts_node_child(node, index));
    }
    return expectedSeen && identifierCount == 1 && !unsupportedLeaf;
}

TSNode nearestProceduralContainer(TSNode node)
{
    while (!ts_node_is_null(node)) {
        if (nodeTypeIs(node, "always_construct")
            || nodeTypeIs(node, "initial_construct")
            || nodeTypeIs(node, "final_construct")
            || nodeTypeIs(node, "function_declaration")
            || nodeTypeIs(node, "task_declaration")) {
            return node;
        }
        if (isRtlContainer(node))
            break;
        node = ts_node_parent(node);
    }
    return {};
}

TSNode nearestBlockScope(TSNode node)
{
    while (!ts_node_is_null(node)) {
        if (nodeTypeIs(node, "seq_block")
            || nodeTypeIs(node, "par_block")) {
            return node;
        }
        if (isRtlContainer(node))
            break;
        node = ts_node_parent(node);
    }
    return {};
}

QString structuralIdentity(const QString& fileName,
                           TSNode node,
                           const QString& role)
{
    return QStringLiteral("tree:%1:%2:%3:%4")
        .arg(fileName, role)
        .arg(nodeStartChar(node))
        .arg(nodeEndChar(node));
}

QString symbolEvidence(const SemanticSymbolRecord& record)
{
    if (record.stableKey.isValid())
        return record.stableKey.toString();
    return QStringLiteral("%1:%2:%3")
        .arg(record.location.fileName)
        .arg(record.location.position)
        .arg(record.name);
}

void appendIssue(DeclareSignalFactCollectionResult* result,
                 CollectionCode code,
                 const QString& message,
                 const QString& evidenceId = QString(),
                 bool rejected = false)
{
    if (!result)
        return;
    for (const DeclareSignalFactCollectionIssue& issue :
         std::as_const(result->issues)) {
        if (issue.code == code
            && issue.evidenceId == evidenceId) {
            if (rejected)
                result->status = CollectionStatus::Rejected;
            return;
        }
    }
    result->issues.append({code, message, evidenceId});
    if (rejected) {
        result->status = CollectionStatus::Rejected;
    } else if (result->status == CollectionStatus::Ready) {
        result->status = CollectionStatus::Incomplete;
    }
}

QString formalDirection(CollectorKind kind)
{
    switch (kind) {
    case CollectorKind::PortInput:
        return QStringLiteral("input");
    case CollectorKind::PortOutput:
        return QStringLiteral("output");
    case CollectorKind::PortInout:
        return QStringLiteral("inout");
    case CollectorKind::PortRef:
        return QStringLiteral("ref");
    case CollectorKind::PortInterface:
    case CollectorKind::PortInterfaceModport:
        return QStringLiteral("interface");
    default:
        return QString();
    }
}

bool sameTypeInfo(const SemanticElaboratedSymbolInfo& left,
                  const SemanticElaboratedSymbolInfo& right)
{
    return left.available == right.available
        && left.fixedSize == right.fixedSize
        && left.integral == right.integral
        && left.unpackedArray == right.unpackedArray
        && left.interfaceType == right.interfaceType
        && left.signedIntegral == right.signedIntegral
        && left.bitWidth == right.bitWidth
        && left.resolvedTypeText == right.resolvedTypeText
        && left.packedDimensionsText
               == right.packedDimensionsText
        && left.unpackedDimensionsText
               == right.unpackedDimensionsText
        && left.unpackedElementCountText
               == right.unpackedElementCountText
        && left.interfaceName == right.interfaceName
        && left.modportName == right.modportName;
}

bool sameDimensionFact(
    const SemanticTypeDimensionFact& left,
    const SemanticTypeDimensionFact& right)
{
    return left.kind == right.kind
        && left.canonicalId == right.canonicalId
        && left.declarationText == right.declarationText
        && left.leftEvaluationNodeId
               == right.leftEvaluationNodeId
        && left.rightEvaluationNodeId
               == right.rightEvaluationNodeId
        && left.elementCountText == right.elementCountText
        && left.emitInDeclaration == right.emitInDeclaration
        && left.complete == right.complete;
}

bool sameTypedefStep(
    const SemanticTypedefResolutionStep& left,
    const SemanticTypedefResolutionStep& right)
{
    return left.sourceTypeId == right.sourceTypeId
        && left.sourceTypeName == right.sourceTypeName
        && left.targetTypeId == right.targetTypeId
        && left.declarationIdentity
               == right.declarationIdentity
        && left.introducedDimensionIds
               == right.introducedDimensionIds;
}

bool sameDeclaredTypeFacts(
    const SemanticDeclaredTypeFacts& left,
    const SemanticDeclaredTypeFacts& right)
{
    if (left.rootTypeId != right.rootTypeId
        || left.canonicalTypeId != right.canonicalTypeId
        || left.declarationShapeId
               != right.declarationShapeId
        || left.declarationBaseText
               != right.declarationBaseText
        || left.signednessKnown != right.signednessKnown
        || left.signedIntegral != right.signedIntegral
        || left.complete != right.complete
        || left.constants.complete != right.constants.complete
        || left.dimensions.size() != right.dimensions.size()
        || left.typedefChain.size()
               != right.typedefChain.size()) {
        return false;
    }
    for (int index = 0; index < left.dimensions.size();
         ++index) {
        if (!sameDimensionFact(
                left.dimensions.at(index),
                right.dimensions.at(index))) {
            return false;
        }
    }
    for (int index = 0; index < left.typedefChain.size();
         ++index) {
        if (!sameTypedefStep(
                left.typedefChain.at(index),
                right.typedefChain.at(index))) {
            return false;
        }
    }
    return true;
}

bool selectFormalTypeInfo(
    const SemanticSymbolRecord& formal,
    const QString& exactInstancePath,
    SemanticElaboratedSymbolInfo* selected,
    DeclareSignalFactCollectionResult* result)
{
    if (!selected || !result)
        return false;

    const QString evidence = symbolEvidence(formal);
    const auto& byInstance =
        formal.presentation.instanceInfoByPath;
    if (byInstance.isEmpty()) {
        *selected = formal.presentation.defaultInfo;
        return true;
    }
    if (!exactInstancePath.isEmpty()) {
        const auto exact = byInstance.constFind(
            exactInstancePath);
        if (exact != byInstance.constEnd()) {
            *selected = exact.value();
            return true;
        }
    }

    QStringList paths = byInstance.keys();
    std::sort(paths.begin(), paths.end());
    *selected = byInstance.value(paths.constFirst());
    for (int index = 1; index < paths.size(); ++index) {
        if (!sameTypeInfo(
                *selected, byInstance.value(paths.at(index)))) {
            appendIssue(
                result,
                CollectionCode::AmbiguousElaboratedFormalType,
                QStringLiteral(
                    "The selected declaration has different Slang types in "
                    "multiple elaborated instance contexts, and the "
                    "snapshot has no source-instance to hierarchy-path "
                    "binding for this use."),
                evidence);
            return false;
        }
    }
    return true;
}

bool selectFormalDeclaredTypeFacts(
    const SemanticSymbolRecord& formal,
    const QString& exactInstancePath,
    SemanticDeclaredTypeFacts* selected,
    DeclareSignalFactCollectionResult* result)
{
    if (!selected || !result)
        return false;

    const auto& typeFactsByPath =
        formal.presentation.declaredTypeFactsByPath;
    const auto& infoByPath =
        formal.presentation.instanceInfoByPath;
    if (infoByPath.isEmpty()) {
        *selected =
            formal.presentation.defaultDeclaredTypeFacts;
        return !selected->rootTypeId.isEmpty()
            || !selected->evidenceId.isEmpty()
            || !selected->failureReason.isEmpty();
    }
    if (!exactInstancePath.isEmpty()) {
        const auto info = infoByPath.constFind(
            exactInstancePath);
        if (info != infoByPath.constEnd()) {
            const auto facts = typeFactsByPath.constFind(
                exactInstancePath);
            if (facts == typeFactsByPath.constEnd()) {
                appendIssue(
                    result,
                    CollectionCode::MissingFormalType,
                    QStringLiteral(
                        "The Slang snapshot has elaborated type info "
                        "but no structured declared-type facts for "
                        "instance %1.")
                        .arg(exactInstancePath),
                    symbolEvidence(formal));
                return false;
            }
            *selected = facts.value();
            return true;
        }
    }

    QStringList paths = infoByPath.keys();
    std::sort(paths.begin(), paths.end());
    bool haveSelected = false;
    for (const QString& path : std::as_const(paths)) {
        const auto facts =
            typeFactsByPath.constFind(path);
        if (facts == typeFactsByPath.constEnd()) {
            appendIssue(
                result,
                CollectionCode::MissingFormalType,
                QStringLiteral(
                    "The Slang snapshot has elaborated type info but "
                    "no structured declared-type facts for instance "
                    "%1.")
                    .arg(path),
                symbolEvidence(formal));
            return false;
        }
        if (!haveSelected) {
            *selected = facts.value();
            haveSelected = true;
            continue;
        }
        if (!sameDeclaredTypeFacts(
                *selected, facts.value())) {
            appendIssue(
                result,
                CollectionCode::AmbiguousElaboratedFormalType,
                QStringLiteral(
                    "The selected declaration has different structured Slang "
                    "types in multiple elaborated instance contexts, "
                    "and the snapshot has no source-instance to "
                    "hierarchy-path binding for this use."),
                symbolEvidence(formal));
            return false;
        }
    }
    return haveSelected;
}

QString canonicalTypeIdentity(
    const SemanticElaboratedSymbolInfo& info)
{
    if (!info.available)
        return QString();
    if (info.interfaceType
        && !info.interfaceName.isEmpty()) {
        return QStringLiteral("slang:interface:%1:%2")
            .arg(info.interfaceName, info.modportName);
    }
    if (info.fixedSize && info.integral
        && info.bitWidth > 0) {
        return QStringLiteral("slang:integral:%1:%2")
            .arg(info.signedIntegral
                     ? QStringLiteral("signed")
                     : QStringLiteral("unsigned"))
            .arg(info.bitWidth);
    }
    return QString();
}

QString parameterConsumerIdentity(
    const SemanticParameterDependencyNode& parameter,
    const QHash<QString, int>& nameCounts)
{
    return !parameter.name.isEmpty()
            && nameCounts.value(parameter.name) == 1
        ? parameter.name
        : parameter.identity;
}

DeclareSignalTypeFact convertDeclaredTypeFacts(
    const SemanticDeclaredTypeFacts& source,
    quint64 semanticGeneration,
    const QString& fallbackEvidence)
{
    DeclareSignalTypeFact result;
    const QString evidence =
        !source.evidenceId.isEmpty()
        ? source.evidenceId
        : fallbackEvidence;
    const auto provenance =
        DeclareSignalFactProvenance{
            DeclareSignalFactSource::Slang,
            semanticGeneration,
            evidence,
        };
    result.rootTypeId = source.rootTypeId;
    result.canonicalTypeId = source.canonicalTypeId;
    result.declarationShapeId =
        source.declarationShapeId;
    result.declarationBaseText =
        source.declarationBaseText;
    result.signednessKnown = source.signednessKnown;
    result.signedIntegral = source.signedIntegral;
    result.complete = source.complete;
    result.provenance = provenance;

    for (const SemanticTypeDimensionFact& dimension :
         source.dimensions) {
        DeclareSignalDimensionFact converted;
        converted.kind =
            dimension.kind
                    == SemanticTypeDimensionKind::Packed
                ? DeclareSignalDimensionKind::Packed
                : DeclareSignalDimensionKind::Unpacked;
        converted.canonicalId = dimension.canonicalId;
        converted.declarationText =
            dimension.declarationText;
        converted.leftEvaluationStepId =
            dimension.leftEvaluationNodeId;
        converted.rightEvaluationStepId =
            dimension.rightEvaluationNodeId;
        converted.elementCountText =
            dimension.elementCountText;
        converted.emitInDeclaration =
            dimension.emitInDeclaration;
        converted.complete = dimension.complete;
        converted.provenance = {
            DeclareSignalFactSource::Slang,
            semanticGeneration,
            !dimension.evidenceId.isEmpty()
                ? dimension.evidenceId
                : evidence,
        };
        result.dimensions.append(std::move(converted));
    }

    for (const SemanticTypedefResolutionStep& step :
         source.typedefChain) {
        DeclareSignalTypedefStep converted;
        converted.sourceTypeId = step.sourceTypeId;
        converted.sourceTypeName = step.sourceTypeName;
        converted.targetTypeId = step.targetTypeId;
        converted.declarationIdentity =
            step.declarationIdentity;
        converted.introducedDimensionIds =
            step.introducedDimensionIds;
        converted.provenance = {
            DeclareSignalFactSource::Slang,
            semanticGeneration,
            !step.evidenceId.isEmpty()
                ? step.evidenceId
                : evidence,
        };
        result.typedefChain.append(std::move(converted));
    }

    QHash<QString, int> parameterNameCounts;
    for (const SemanticParameterDependencyNode& parameter :
         source.constants.parameters) {
        if (!parameter.name.isEmpty()) {
            parameterNameCounts.insert(
                parameter.name,
                parameterNameCounts.value(parameter.name) + 1);
        }
    }
    QHash<QString, QString> consumerIdBySemanticId;
    for (const SemanticParameterDependencyNode& parameter :
         source.constants.parameters) {
        consumerIdBySemanticId.insert(
            parameter.identity,
            parameterConsumerIdentity(
                parameter, parameterNameCounts));
    }
    auto consumerParameterIds =
        [&](const QStringList& semanticIds) {
            QStringList resultIds;
            resultIds.reserve(semanticIds.size());
            for (const QString& id : semanticIds) {
                resultIds.append(
                    consumerIdBySemanticId.value(id, id));
            }
            return resultIds;
        };

    result.constants.complete =
        source.constants.complete;
    result.constants.provenance = {
        DeclareSignalFactSource::Slang,
        semanticGeneration,
        !source.constants.evidenceId.isEmpty()
            ? source.constants.evidenceId
            : evidence + QStringLiteral(":constants"),
    };
    for (const SemanticConstantEvaluationNode& node :
         source.constants.evaluationNodes) {
        DeclareSignalConstantEvaluationStep converted;
        converted.id = node.id;
        converted.operationName = node.operationName;
        converted.expressionText = node.expressionText;
        converted.operandStepIds = node.operandIds;
        converted.parameterDependencies =
            consumerParameterIds(
                node.parameterDependencyIds);
        converted.resultValueText =
            node.resultValueText;
        converted.evaluated = node.evaluated;
        converted.provenance = {
            DeclareSignalFactSource::Slang,
            semanticGeneration,
            !node.evidenceId.isEmpty()
                ? node.evidenceId
                : evidence,
        };
        result.constants.evaluationSteps.append(
            std::move(converted));
    }
    for (const SemanticParameterDependencyNode& parameter :
         source.constants.parameters) {
        DeclareSignalParameterFact converted;
        converted.name =
            consumerIdBySemanticId.value(
                parameter.identity,
                parameter.identity);
        converted.localparam = parameter.localparam;
        converted.rootEvaluationStepId =
            parameter.rootEvaluationNodeId;
        converted.dependencies =
            consumerParameterIds(parameter.dependencyIds);
        converted.valueText = parameter.valueText;
        converted.evaluated = parameter.evaluated;
        converted.provenance = {
            DeclareSignalFactSource::Slang,
            semanticGeneration,
            !parameter.evidenceId.isEmpty()
                ? parameter.evidenceId
                : evidence,
        };
        result.constants.parameters.append(
            std::move(converted));
    }
    return result;
}

DeclareSignalTypeFact collectFormalType(
    const SemanticSymbolRecord& formal,
    quint64 semanticGeneration,
    const QString& exactInstancePath,
    DeclareSignalFactCollectionResult* result)
{
    DeclareSignalTypeFact fact;
    const QString evidence = symbolEvidence(formal);
    fact.provenance = {
        DeclareSignalFactSource::Slang,
        semanticGeneration,
        evidence,
    };
    fact.constants.provenance = {
        DeclareSignalFactSource::Slang,
        semanticGeneration,
        evidence + QStringLiteral(":constants"),
    };

    SemanticElaboratedSymbolInfo info;
    const bool unambiguous =
        selectFormalTypeInfo(
            formal, exactInstancePath, &info, result);
    if (!unambiguous || !info.available) {
        appendIssue(
            result,
            CollectionCode::MissingFormalType,
            QStringLiteral(
                "The current Slang snapshot has no unambiguous "
                "elaborated type for the selected declaration."),
            evidence);
        return fact;
    }

    SemanticDeclaredTypeFacts structuredType;
    if (selectFormalDeclaredTypeFacts(
            formal,
            exactInstancePath,
            &structuredType,
            result)) {
        fact = convertDeclaredTypeFacts(
            structuredType,
            semanticGeneration,
            evidence);
        bool dimensionsComplete = true;
        for (const DeclareSignalDimensionFact& dimension :
             std::as_const(fact.dimensions)) {
            dimensionsComplete =
                dimensionsComplete && dimension.complete;
        }
        if (!dimensionsComplete) {
            appendIssue(
                result,
                CollectionCode::DimensionDecompositionUnavailable,
                QStringLiteral(
                    "The structured Slang dimension facts are "
                    "incomplete."),
                evidence);
        }
        bool typedefComplete = true;
        QString expectedType = fact.rootTypeId;
        for (const DeclareSignalTypedefStep& step :
             std::as_const(fact.typedefChain)) {
            if (step.sourceTypeId != expectedType
                || step.targetTypeId.isEmpty()
                || step.declarationIdentity.isEmpty()) {
                typedefComplete = false;
                break;
            }
            expectedType = step.targetTypeId;
        }
        typedefComplete =
            typedefComplete
            && expectedType == fact.canonicalTypeId;
        if (!typedefComplete) {
            appendIssue(
                result,
                CollectionCode::TypedefResolutionUnavailable,
                QStringLiteral(
                    "The structured Slang typedef target chain is "
                    "incomplete."),
                evidence);
        }
        if (!fact.constants.complete) {
            appendIssue(
                result,
                CollectionCode::ConstantDependencyGraphUnavailable,
                QStringLiteral(
                    "The structured Slang constant dependency graph "
                    "is incomplete."),
                evidence);
        }
        if (fact.rootTypeId.isEmpty()
            || fact.canonicalTypeId.isEmpty()
            || fact.declarationShapeId.isEmpty()
            || fact.declarationBaseText.isEmpty()) {
            appendIssue(
                result,
                CollectionCode::MissingMachineReadableTypeIdentity,
                QStringLiteral(
                    "The structured Slang declared type lacks an "
                    "identity or dimension-free declaration base."),
                evidence);
        }
        return fact;
    }

    fact.canonicalTypeId = canonicalTypeIdentity(info);
    if (fact.canonicalTypeId.isEmpty()) {
        appendIssue(
            result,
            CollectionCode::MissingMachineReadableTypeIdentity,
            QStringLiteral(
                "SemanticIndex does not expose a machine-readable "
                "canonical identity for this Slang type."),
            evidence);
    }

    if (formal.type.stableKey.isValid()) {
        fact.rootTypeId = formal.type.stableKey.toString();
    } else if (!fact.canonicalTypeId.isEmpty()) {
        fact.rootTypeId = fact.canonicalTypeId;
    }

    const bool hasPackedDimensions =
        !formal.presentation.packedDimensionsText.isEmpty()
        || !info.packedDimensionsText.isEmpty();
    const bool hasUnpackedDimensions =
        !formal.presentation.unpackedDimensionsText.isEmpty()
        || !info.unpackedDimensionsText.isEmpty();
    const bool hasDimensions =
        hasPackedDimensions || hasUnpackedDimensions;
    if (hasDimensions) {
        appendIssue(
            result,
            CollectionCode::DimensionDecompositionUnavailable,
            QStringLiteral(
                "SemanticIndex exposes rendered dimensions but not "
                "their Slang range identities, bound-expression nodes, "
                "or packed/unpacked decomposition."),
            evidence);
        appendIssue(
            result,
            CollectionCode::ConstantDependencyGraphUnavailable,
            QStringLiteral(
                "SemanticIndex does not publish the Slang constant "
                "expression DAG needed by the formal dimensions."),
            evidence);
        fact.constants.complete = false;
    } else {
        // With no dimensions there are no dimension-bound constants to
        // associate with this declaration.
        fact.constants.complete = true;
    }

    const bool typedefReference =
        formal.type.resolvedTypeKind == DeclarationKind::Typedef
        || formal.type.stableKey.declarationKind
               == DeclarationKind::Typedef;
    if (typedefReference) {
        if (formal.type.stableKey.isValid()) {
            DeclareSignalTypedefStep step;
            step.sourceTypeId = fact.rootTypeId;
            step.sourceTypeName =
                !formal.type.resolvedTypeName.isEmpty()
                    ? formal.type.resolvedTypeName
                    : formal.type.stableKey.symbolName;
            step.declarationIdentity =
                formal.type.stableKey.toString();
            step.provenance = {
                DeclareSignalFactSource::Slang,
                semanticGeneration,
                step.declarationIdentity,
            };
            fact.typedefChain.append(std::move(step));
        }
        appendIssue(
            result,
            CollectionCode::TypedefResolutionUnavailable,
            QStringLiteral(
                "SemanticIndex identifies the declared typedef but "
                "does not publish its Slang target-type chain."),
            evidence);
    }

    // Both fields below are renderer products already supplied by Slang. No
    // token, direction, dimension, or identifier is removed by this layer.
    // A declaration base is therefore usable only when dimensions need not be
    // separated from the renderer product.
    if (!hasDimensions) {
        fact.declarationBaseText =
            !formal.type.rawTypeText.isEmpty()
                ? formal.type.rawTypeText.trimmed()
                : info.resolvedTypeText.trimmed();
    }
    if (!fact.declarationBaseText.isEmpty()
        && !fact.canonicalTypeId.isEmpty()) {
        fact.declarationShapeId =
            QStringLiteral("%1:%2")
                .arg(fact.canonicalTypeId,
                     fact.declarationBaseText);
    }

    fact.signednessKnown = info.integral;
    fact.signedIntegral =
        info.integral && info.signedIntegral;
    fact.complete =
        unambiguous
        && info.available
        && !hasDimensions
        && !typedefReference
        && !fact.rootTypeId.isEmpty()
        && !fact.canonicalTypeId.isEmpty()
        && !fact.declarationShapeId.isEmpty()
        && !fact.declarationBaseText.isEmpty();

    if (!fact.complete
        && !hasDimensions
        && !typedefReference
        && (fact.rootTypeId.isEmpty()
            || fact.declarationBaseText.isEmpty())) {
        appendIssue(
            result,
            CollectionCode::MissingMachineReadableTypeIdentity,
            QStringLiteral(
                "SemanticIndex lacks the declared type identity or a "
                "safe Slang declaration renderer for this formal."),
            evidence);
    }
    return fact;
}

QList<SemanticSymbolRecord> exactFormalRecords(
    const SemanticIndexSnapshot& snapshot,
    const QString& moduleType,
    const QString& formalName)
{
    QList<SemanticSymbolRecord> result;
    QSet<QString> seen;
    for (const SemanticSymbolRecord& record :
         snapshot.getSymbolRecordsByName(formalName)) {
        if (record.declarationKind != DeclarationKind::Port
            || record.owner.name != moduleType) {
            continue;
        }
        const QString identity = symbolEvidence(record);
        if (seen.contains(identity))
            continue;
        seen.insert(identity);
        result.append(record);
    }
    std::sort(
        result.begin(), result.end(),
        [](const SemanticSymbolRecord& left,
           const SemanticSymbolRecord& right) {
            return symbolEvidence(left)
                < symbolEvidence(right);
        });
    return result;
}

QList<SemanticSymbolRecord> exactValueDeclarationRecords(
    const SemanticIndexSnapshot& snapshot,
    const QString& moduleName,
    const QString& symbolName)
{
    QList<SemanticSymbolRecord> result;
    QSet<QString> seen;
    for (const SemanticSymbolRecord& record :
         snapshot.getSymbolRecordsByName(symbolName)) {
        if (record.owner.name != moduleName
            || record.usageRole
                   != SymbolTaxonomy::SymbolUsageRole::Declaration) {
            continue;
        }
        if (record.declarationKind != DeclarationKind::Port
            && record.declarationKind != DeclarationKind::Signal
            && record.declarationKind
                   != DeclarationKind::Parameter
            && record.declarationKind
                   != DeclarationKind::Localparam
            && record.declarationKind
                   != DeclarationKind::StructVariable) {
            continue;
        }
        const QString identity = symbolEvidence(record);
        if (seen.contains(identity))
            continue;
        seen.insert(identity);
        result.append(record);
    }
    std::sort(
        result.begin(),
        result.end(),
        [](const SemanticSymbolRecord& left,
           const SemanticSymbolRecord& right) {
            return symbolEvidence(left)
                < symbolEvidence(right);
        });
    return result;
}

TSNode lvalueForAssignment(TSNode assignment)
{
    const uint32_t count =
        ts_node_named_child_count(assignment);
    for (uint32_t index = 0; index < count; ++index) {
        const TSNode child =
            ts_node_named_child(assignment, index);
        if (nodeTypeIs(child, "net_lvalue")
            || nodeTypeIs(child, "variable_lvalue")
            || nodeTypeIs(child, "nonrange_variable_lvalue")
            || nodeTypeIs(child, "hierarchical_identifier")) {
            return child;
        }
        if (nodeTypeIs(child, "operator_assignment")) {
            const TSNode nested =
                lvalueForAssignment(child);
            if (!ts_node_is_null(nested))
                return nested;
        }
    }
    return {};
}

TSNode exactSingleIdentifierCounterpart(
    TSNode assignment,
    TSNode selectedIdentifier,
    TSNode lvalue)
{
    if (ts_node_is_null(assignment)
        || ts_node_is_null(selectedIdentifier)
        || ts_node_is_null(lvalue)) {
        return {};
    }

    if (!containsNode(lvalue, selectedIdentifier)) {
        const TSNode lvalueIdentifier =
            firstIdentifierDescendant(lvalue);
        return singleIdentifierExpression(
                   lvalue, lvalueIdentifier)
            ? lvalueIdentifier
            : TSNode{};
    }

    const uint32_t count =
        ts_node_named_child_count(assignment);
    for (uint32_t reverse = count; reverse > 0; --reverse) {
        const TSNode child =
            ts_node_named_child(assignment, reverse - 1);
        if (sameNode(child, lvalue)
            || containsNode(child, lvalue)
            || nodeTypeIs(child, "delay_or_event_control")) {
            continue;
        }
        const TSNode counterpart =
            firstIdentifierDescendant(child);
        if (singleIdentifierExpression(child, counterpart))
            return counterpart;
    }
    return {};
}

void populateAssignmentExpectedType(
    DeclareSignalUseFact* use,
    TSNode assignment,
    TSNode selectedIdentifier,
    TSNode lvalue,
    const QString& text,
    const QString& moduleName,
    const QString& hierarchyInstancePath,
    const SemanticIndexSnapshot& snapshot,
    quint64 semanticGeneration,
    DeclareSignalFactCollectionResult* result)
{
    if (!use || use->hasExpectedType)
        return;
    const TSNode counterpart =
        exactSingleIdentifierCounterpart(
            assignment, selectedIdentifier, lvalue);
    if (ts_node_is_null(counterpart))
        return;

    const QString counterpartName =
        nodeText(text, counterpart);
    const QList<SemanticSymbolRecord> declarations =
        exactValueDeclarationRecords(
            snapshot, moduleName, counterpartName);
    if (declarations.size() != 1)
        return;

    use->expectedType = collectFormalType(
        declarations.constFirst(),
        semanticGeneration,
        hierarchyInstancePath,
        result);
    use->hasExpectedType =
        !use->expectedType.canonicalTypeId.isEmpty()
        && !use->expectedType.declarationBaseText.isEmpty();
}

TSNode nearestAssignment(TSNode node)
{
    while (!ts_node_is_null(node)) {
        if (nodeTypeIs(node, "net_assignment")
            || nodeTypeIs(node, "variable_assignment")
            || nodeTypeIs(node, "blocking_assignment")
            || nodeTypeIs(node, "nonblocking_assignment")
            || nodeTypeIs(node, "operator_assignment")) {
            return node;
        }
        if (isRtlContainer(node))
            break;
        node = ts_node_parent(node);
    }
    return {};
}

void populateProceduralScope(
    DeclareSignalUseFact* use,
    TSNode identifier,
    const QString& fileName)
{
    if (!use)
        return;
    const TSNode block = nearestBlockScope(identifier);
    if (!ts_node_is_null(block)) {
        use->syntaxScopeId =
            structuralIdentity(fileName, block,
                               QStringLiteral("block"));
        use->blockLocalDeclarationAllowed =
            nodeTypeIs(block, "seq_block");
        return;
    }
    const TSNode process =
        nearestProceduralContainer(identifier);
    if (!ts_node_is_null(process)) {
        use->syntaxScopeId =
            structuralIdentity(fileName, process,
                               QStringLiteral("process"));
    }
}

enum class ClassifiedUseStatus {
    Appended,
    Declaration,
    NonValue,
    Unsupported
};

ClassifiedUseStatus classifyUse(
    TSNode identifier,
    const QString& text,
    const QString& fileName,
    const QString& moduleName,
    quint64 documentRevision,
    const SemanticIndexSnapshot& snapshot,
    quint64 semanticGeneration,
    const QString& hierarchyInstancePath,
    DeclareSignalFactCollectionResult* result)
{
    const IdentifierRole initialRole =
        declarationOrNonValueRole(identifier);
    if (initialRole == IdentifierRole::Declaration)
        return ClassifiedUseStatus::Declaration;
    if (initialRole == IdentifierRole::NonValue)
        return ClassifiedUseStatus::NonValue;
    if (initialRole == IdentifierRole::Unsupported)
        return ClassifiedUseStatus::Unsupported;

    DeclareSignalUseFact use;
    use.moduleName = moduleName;
    use.sourcePosition = nodeStartChar(identifier);
    use.syntaxProvenance = {
        DeclareSignalFactSource::TreeSitter,
        documentRevision,
        structuralIdentity(fileName, identifier,
                           QStringLiteral("use")),
    };

    const TSNode connection =
        ancestorOfType(identifier, "named_port_connection");
    if (!ts_node_is_null(connection)) {
        const TSNode actual =
            childByField(connection, "connection");
        if (ts_node_is_null(actual)
            || !containsNode(actual, identifier)) {
            return ClassifiedUseStatus::NonValue;
        }

        if (!singleIdentifierExpression(actual, identifier)) {
            use.kind =
                DeclareSignalUseKind::ModuleItemReference;
            result->request.uses.append(std::move(use));
            appendIssue(
                result,
                CollectionCode::UnsupportedUseSite,
                QStringLiteral(
                    "A named-port actual contains a compound "
                    "expression; its formal type cannot be copied to "
                    "one identifier."),
                result->request.uses.constLast()
                    .syntaxProvenance.evidenceId);
            return ClassifiedUseStatus::Appended;
        }

        const TSNode formal =
            childByField(connection, "port_name");
        const TSNode instantiation =
            ancestorOfType(connection, "module_instantiation");
        const TSNode moduleType =
            childByField(instantiation, "instance_type");
        if (ts_node_is_null(formal)
            || ts_node_is_null(moduleType)) {
            return ClassifiedUseStatus::Unsupported;
        }

        use.kind = DeclareSignalUseKind::NamedPortActual;
        use.formalPortName = nodeText(text, formal);
        const QString instantiatedModule =
            nodeText(text, moduleType);
        const QList<SemanticSymbolRecord> formals =
            exactFormalRecords(snapshot,
                               instantiatedModule,
                               use.formalPortName);
        if (formals.isEmpty()) {
            appendIssue(
                result,
                CollectionCode::MissingFormalPort,
                QStringLiteral(
                    "The current Slang snapshot has no formal port "
                    "matching this named connection."),
                use.syntaxProvenance.evidenceId);
        } else if (formals.size() != 1) {
            appendIssue(
                result,
                CollectionCode::AmbiguousFormalPort,
                QStringLiteral(
                    "The current Slang snapshot has multiple formal "
                    "ports matching this named connection."),
                use.syntaxProvenance.evidenceId);
        } else {
            const SemanticSymbolRecord& formalRecord =
                formals.constFirst();
            QString childInstancePath =
                hierarchyInstancePath;
            const TSNode hierarchy =
                ancestorOfType(
                    connection, "hierarchical_instance");
            TSNode nameOfInstance{};
            const uint32_t hierarchyChildCount =
                ts_node_named_child_count(hierarchy);
            for (uint32_t index = 0;
                 index < hierarchyChildCount;
                 ++index) {
                const TSNode child =
                    ts_node_named_child(hierarchy, index);
                if (nodeTypeIs(child, "name_of_instance")) {
                    nameOfInstance = child;
                    break;
                }
            }
            const TSNode instanceNameNode =
                firstIdentifierDescendant(nameOfInstance);
            const QString sourceInstanceName =
                nodeText(text, instanceNameNode);
            if (!childInstancePath.isEmpty()
                && !sourceInstanceName.isEmpty()) {
                if (!childInstancePath.endsWith(
                        QLatin1Char('.'))) {
                    childInstancePath += QLatin1Char('.');
                }
                childInstancePath += sourceInstanceName;
            } else {
                childInstancePath.clear();
            }
            use.formalPortIdentity =
                symbolEvidence(formalRecord);
            use.formalDirection =
                formalDirection(formalRecord.collectorKind);
            // A copied named-port actual is declared as a SystemVerilog
            // variable by default. Direction remains evidence only and is
            // never emitted into the declaration; inout is the one case
            // that structurally requires a net endpoint.
            use.objectConstraint =
                formalRecord.collectorKind
                        == CollectorKind::PortInout
                    ? DeclareSignalObjectConstraint::NetOnly
                    : DeclareSignalObjectConstraint::VariableOnly;
            use.exactFormalBinding = true;
            use.hasExpectedType = true;
            use.expectedType =
                collectFormalType(formalRecord,
                                  semanticGeneration,
                                  childInstancePath,
                                  result);
        }
        result->request.uses.append(std::move(use));
        return ClassifiedUseStatus::Appended;
    }

    const TSNode assignment = nearestAssignment(identifier);
    if (!ts_node_is_null(assignment)) {
        const TSNode lvalue =
            lvalueForAssignment(assignment);
        const bool inLvalue =
            !ts_node_is_null(lvalue)
            && containsNode(lvalue, identifier);
        const bool rootLvalueIdentifier =
            inLvalue
            && sameNode(firstIdentifierDescendant(lvalue),
                        identifier);
        const bool continuous =
            !ts_node_is_null(
                ancestorOfType(assignment,
                               "continuous_assign"));

        if (inLvalue && !rootLvalueIdentifier)
            return ClassifiedUseStatus::Unsupported;

        if (continuous) {
            use.kind = rootLvalueIdentifier
                ? DeclareSignalUseKind::
                      ContinuousAssignmentLhs
                : DeclareSignalUseKind::ModuleItemReference;
            use.objectConstraint = rootLvalueIdentifier
                ? DeclareSignalObjectConstraint::NetOnly
                : DeclareSignalObjectConstraint::Any;
        } else {
            use.kind = rootLvalueIdentifier
                ? DeclareSignalUseKind::
                      ProceduralAssignmentLhs
                : DeclareSignalUseKind::
                      ProceduralReference;
            use.objectConstraint = rootLvalueIdentifier
                ? DeclareSignalObjectConstraint::VariableOnly
                : DeclareSignalObjectConstraint::Any;
            populateProceduralScope(&use, identifier, fileName);
        }
        populateAssignmentExpectedType(
            &use,
            assignment,
            identifier,
            lvalue,
            text,
            moduleName,
            hierarchyInstancePath,
            snapshot,
            semanticGeneration,
            result);
        result->request.uses.append(std::move(use));
        return ClassifiedUseStatus::Appended;
    }

    if (!ts_node_is_null(
            nearestProceduralContainer(identifier))) {
        use.kind = DeclareSignalUseKind::ProceduralReference;
        populateProceduralScope(&use, identifier, fileName);
    } else {
        use.kind = DeclareSignalUseKind::ModuleItemReference;
    }
    result->request.uses.append(std::move(use));
    return ClassifiedUseStatus::Appended;
}

void collectExistingDeclarations(
    const SemanticIndexSnapshot& snapshot,
    const QString& identifier,
    const QString& moduleName,
    quint64 semanticGeneration,
    DeclareSignalRequest* request)
{
    if (!request)
        return;
    QSet<QString> seen;
    for (const SemanticSymbolRecord& record :
         snapshot.getSymbolRecordsByName(identifier)) {
        if (record.usageRole
                != SymbolTaxonomy::SymbolUsageRole::Declaration) {
            continue;
        }
        const bool sameOwner =
            record.owner.name == moduleName;
        const bool globallyNamedCategory =
            record.declarationKind == DeclarationKind::Module
            || record.declarationKind
                   == DeclarationKind::Interface
            || record.declarationKind
                   == DeclarationKind::Package
            || record.declarationKind
                   == DeclarationKind::Enum
            || record.declarationKind
                   == DeclarationKind::Macro;
        if (!sameOwner && !globallyNamedCategory)
            continue;
        const QString evidence = symbolEvidence(record);
        if (seen.contains(evidence))
            continue;
        seen.insert(evidence);

        DeclareSignalExistingDeclarationFact fact;
        fact.name = record.name;
        fact.moduleName = moduleName;
        fact.declarationIdentity = evidence;
        fact.provenance = {
            DeclareSignalFactSource::Slang,
            semanticGeneration,
            evidence,
        };
        request->existingDeclarations.append(std::move(fact));
    }
}

} // namespace

DeclareSignalFactCollectionResult
DeclareSignalFactCollector::collect(
    const DeclareSignalFactCollectionQuery& query)
{
    DeclareSignalFactCollectionResult result;
    result.status = CollectionStatus::Ready;

    if (!query.document) {
        appendIssue(
            &result,
            CollectionCode::MissingDocument,
            QStringLiteral(
                "A live Tree-sitter document is required."),
            QString(), true);
        return result;
    }
    if (!query.semanticSnapshot.isValid()) {
        appendIssue(
            &result,
            CollectionCode::MissingSemanticSnapshot,
            QStringLiteral(
                "An immutable SemanticIndex snapshot is required."),
            QString(), true);
        return result;
    }
    if (query.semanticSnapshot.revision
        != query.expectedSemanticGeneration) {
        appendIssue(
            &result,
            CollectionCode::StaleSemanticGeneration,
            QStringLiteral(
                "The SemanticIndex snapshot generation no longer "
                "matches the requested analysis generation."),
            QStringLiteral("semantic:%1!=%2")
                .arg(query.semanticSnapshot.revision)
                .arg(query.expectedSemanticGeneration),
            true);
        return result;
    }
    if (query.documentRevision
        != query.expectedDocumentRevision) {
        appendIssue(
            &result,
            CollectionCode::StaleDocumentRevision,
            QStringLiteral(
                "The Tree-sitter document revision no longer "
                "matches the requested editor revision."),
            QStringLiteral("document:%1!=%2")
                .arg(query.documentRevision)
                .arg(query.expectedDocumentRevision),
            true);
        return result;
    }

    const QString initialText = query.document->text();
    const TSIdentifierTarget selected =
        query.document->identifierAt(query.cursorPosition);
    if (!selected.ok()) {
        appendIssue(
            &result,
            CollectionCode::MissingIdentifier,
            QStringLiteral(
                "Tree-sitter did not resolve a value identifier at "
                "the requested cursor position."),
            QString(), true);
        return result;
    }

    result.request.identifier = selected.text;
    result.request.semanticRevision =
        query.semanticSnapshot.revision;
    result.request.documentRevision =
        query.documentRevision;
    result.request.currentModuleName =
        query.document->enclosingModuleName(
            selected.startChar);

    const TSNode module =
        containingRtlContainer(
            query.document->rootNode(),
            selected.startChar);
    if (ts_node_is_null(module)
        || result.request.currentModuleName.isEmpty()) {
        appendIssue(
            &result,
            CollectionCode::MissingEnclosingModule,
            QStringLiteral(
                "Tree-sitter did not resolve an enclosing RTL "
                "module, interface, or program."),
            QString(), false);
        result.request.identifierAcceptedBySyntax = true;
        result.request.moduleUseSetComplete = false;
        return result;
    }

    result.request.identifierAcceptedBySyntax = true;
    bool syntaxUseSetComplete = !ts_node_has_error(module);
    if (!syntaxUseSetComplete) {
        appendIssue(
            &result,
            CollectionCode::ModuleSyntaxError,
            QStringLiteral(
                "The current RTL container has Tree-sitter ERROR or "
                "MISSING nodes; its use set is not complete."),
            structuralIdentity(
                query.fileName, module,
                QStringLiteral("module")));
    }

    const QString useSetEvidence =
        structuralIdentity(
            query.fileName, module,
            QStringLiteral("uses"))
        + QStringLiteral(":") + selected.text;
    result.request.useSetProvenance = {
        DeclareSignalFactSource::TreeSitter,
        query.documentRevision,
        useSetEvidence,
    };

    bool selectedWasValue = false;
    bool currentDeclaration = false;
    QList<TSNode> pending{module};
    while (!pending.isEmpty()) {
        const TSNode node = pending.takeLast();
        if (identifierNode(node)
            && nodeText(initialText, node) == selected.text) {
            const ClassifiedUseStatus status =
                classifyUse(
                    node,
                    initialText,
                    query.fileName,
                    result.request.currentModuleName,
                    query.documentRevision,
                    *query.semanticSnapshot.snapshot,
                    query.semanticSnapshot.revision,
                    query.hierarchyInstancePath,
                    &result);
            const bool isSelected =
                nodeStartChar(node) == selected.startChar
                && nodeEndChar(node) == selected.endChar;
            if (status == ClassifiedUseStatus::Appended) {
                selectedWasValue =
                    selectedWasValue || isSelected;
            } else if (status
                       == ClassifiedUseStatus::Declaration) {
                currentDeclaration = true;
                if (isSelected)
                    selectedWasValue = false;
            } else {
                syntaxUseSetComplete = false;
                if (isSelected)
                    selectedWasValue = false;
                appendIssue(
                    &result,
                    status == ClassifiedUseStatus::NonValue
                        ? CollectionCode::NonValueIdentifier
                        : CollectionCode::UnsupportedUseSite,
                    status == ClassifiedUseStatus::NonValue
                        ? QStringLiteral(
                              "The matching identifier is a type, "
                              "formal, instance, label, or declaration "
                              "designator rather than a value use.")
                        : QStringLiteral(
                              "A matching identifier occurs in a "
                              "Tree-sitter structure that this "
                              "collector cannot classify safely."),
                    structuralIdentity(
                        query.fileName, node,
                        QStringLiteral("identifier")));
            }
        }

        const uint32_t count =
            ts_node_named_child_count(node);
        for (uint32_t index = 0; index < count; ++index)
            pending.append(ts_node_named_child(node, index));
    }

    if (currentDeclaration) {
        result.request.identifierAcceptedBySyntax = false;
        appendIssue(
            &result,
            CollectionCode::CurrentBufferDeclaration,
            QStringLiteral(
                "The current Tree-sitter buffer already declares "
                "this identifier in the enclosing RTL container."),
            useSetEvidence);
    } else if (!selectedWasValue) {
        result.request.identifierAcceptedBySyntax = false;
    }

    result.request.moduleUseSetComplete =
        syntaxUseSetComplete;
    if (result.request.uses.isEmpty()) {
        appendIssue(
            &result,
            CollectionCode::NoUseFacts,
            QStringLiteral(
                "No supported value-use facts were found in the "
                "current RTL container."),
            useSetEvidence);
    } else {
        bool hasExpectedType = false;
        for (const DeclareSignalUseFact& use :
             std::as_const(result.request.uses)) {
            hasExpectedType =
                hasExpectedType || use.hasExpectedType;
        }
        if (!hasExpectedType) {
            appendIssue(
                &result,
                CollectionCode::MissingExpectedTypeEvidence,
                QStringLiteral(
                    "The current Slang snapshot has no type "
                    "constraint for any Tree-sitter use site. No type "
                    "is inferred from source text."),
                useSetEvidence);
        }
    }

    collectExistingDeclarations(
        *query.semanticSnapshot.snapshot,
        result.request.identifier,
        result.request.currentModuleName,
        query.semanticSnapshot.revision,
        &result.request);

    if (query.document->text() != initialText) {
        result.request = {};
        appendIssue(
            &result,
            CollectionCode::DocumentChangedDuringCollection,
            QStringLiteral(
                "The live document changed while facts were being "
                "collected."),
            QString(), true);
        return result;
    }

    return result;
}
