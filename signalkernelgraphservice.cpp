#include "signalkernelgraphservice.h"

#include "symboltaxonomy.h"

#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <algorithm>

std::unique_ptr<SignalKernelGraphService>
    SignalKernelGraphService::instance = nullptr;

namespace {
QString symbolDisplayName(const SemanticSymbolRecord& record)
{
    return record.name.isEmpty() ? QStringLiteral("<unknown>") : record.name;
}

QString moduleNameForRecord(const SemanticSymbolRecord& record)
{
    if (!record.owner.name.isEmpty())
        return record.owner.name;
    if (record.declarationKind == SymbolTaxonomy::DeclarationKind::Module
        || record.declarationKind == SymbolTaxonomy::DeclarationKind::Interface
        || record.declarationKind == SymbolTaxonomy::DeclarationKind::Package) {
        return record.name;
    }
    return QString();
}

QString displayModuleNameForRecord(const SemanticSymbolRecord& record)
{
    const QString moduleName = moduleNameForRecord(record);
    if (!moduleName.isEmpty())
        return moduleName;
    return QFileInfo(record.location.fileName).fileName();
}

RtlInsightCodeLink codeLinkForRecord(const SemanticSymbolRecord& record)
{
    return RtlInsightLink::fromFileLine(record.location.fileName,
                                        record.location.startLine,
                                        record.location.startColumn);
}

SemanticSourceRange sourceRangeForRecord(const SemanticSymbolRecord& record)
{
    SemanticSourceRange range;
    range.fileName = record.location.fileName;
    range.line = record.location.startLine;
    range.column = record.location.startColumn;
    range.endLine = record.location.endLine;
    range.endColumn = record.location.endColumn;
    return range;
}

bool hasPreciseEvidence(const SignalJourneyItem& item)
{
    return item.evidenceRange.isValid()
        && !item.evidenceCodeLink.fileName.isEmpty()
        && item.evidenceCodeLink.line > 0;
}

RtlInsightCodeLink previewLinkForItem(const SignalJourneyItem& item)
{
    return hasPreciseEvidence(item)
        ? item.evidenceCodeLink
        : item.peerCodeLink;
}

RtlInsightCodeLink navigateLinkForItem(const SignalJourneyItem& item)
{
    return previewLinkForItem(item);
}

QString nodeKeyForItem(const SignalJourneyItem& item,
                       SignalKernelGraphNodeRole role)
{
    const SymbolStableKey stableKey = item.peerStableKey;
    QString key = stableKey.isValid()
        ? stableKey.toString()
        : QStringLiteral("%1:%2:%3")
              .arg(item.peerSymbolDisplayName,
                   item.peerCodeLink.fileName)
              .arg(item.peerCodeLink.line);
    if (hasPreciseEvidence(item)) {
        key += QStringLiteral("@%1:%2:%3")
                   .arg(item.evidenceRange.fileName)
                   .arg(item.evidenceRange.line)
                   .arg(item.evidenceRange.column);
    }
    return QStringLiteral("%1:%2")
        .arg(static_cast<int>(role))
        .arg(key);
}

QString edgeLabelForItem(const SignalJourneyItem& item)
{
    if (!item.relationshipTypeDisplayName.isEmpty())
        return item.relationshipTypeDisplayName;
    return item.detailDisplayName;
}

QString detailForItem(const SignalJourneyItem& item)
{
    QStringList parts;
    if (!item.detailDisplayName.isEmpty())
        parts.append(item.detailDisplayName);
    if (!item.evidenceDisplayName.isEmpty()
        && item.evidenceDisplayName != QStringLiteral("no evidence detail")) {
        parts.append(item.evidenceDisplayName);
    }
    return parts.join(QStringLiteral(" | "));
}

bool nameLooksInputLike(const QString& name)
{
    const QString lower = name.toLower();
    return lower.endsWith(QStringLiteral("_i"))
        || lower.endsWith(QStringLiteral("_in"))
        || lower.endsWith(QStringLiteral(".i"))
        || lower.contains(QStringLiteral(".data_i"))
        || lower.contains(QStringLiteral(".valid_i"))
        || lower.contains(QStringLiteral(".ready_i"));
}

bool nameLooksOutputLike(const QString& name)
{
    const QString lower = name.toLower();
    return lower.endsWith(QStringLiteral("_o"))
        || lower.endsWith(QStringLiteral("_out"))
        || lower.endsWith(QStringLiteral(".o"))
        || lower.contains(QStringLiteral(".data_o"))
        || lower.contains(QStringLiteral(".valid_o"))
        || lower.contains(QStringLiteral(".ready_o"));
}

SignalKernelGraphNodeRole roleForItem(
    const SignalJourneyItem& item,
    SignalKernelGraphNodeRole fallbackRole)
{
    switch (item.relationshipType) {
    case SymbolRelationshipEngine::ASSIGNS_TO:
        return SignalKernelGraphNodeRole::Input;
    case SymbolRelationshipEngine::READS_FROM:
    case SymbolRelationshipEngine::CLOCKS:
    case SymbolRelationshipEngine::RESETS:
        return SignalKernelGraphNodeRole::Output;
    default:
        break;
    }

    const SymbolTaxonomy::CollectorKind kind =
        item.peerSymbolRecord.collectorKind;
    if (kind == SymbolTaxonomy::CollectorKind::PortOutput)
        return SignalKernelGraphNodeRole::Input;
    if (kind == SymbolTaxonomy::CollectorKind::PortInput
        || kind == SymbolTaxonomy::CollectorKind::PortInterface
        || kind == SymbolTaxonomy::CollectorKind::PortInterfaceModport) {
        return SignalKernelGraphNodeRole::Output;
    }
    if (kind == SymbolTaxonomy::CollectorKind::InstPin) {
        if (nameLooksOutputLike(item.peerSymbolDisplayName))
            return SignalKernelGraphNodeRole::Input;
        if (nameLooksInputLike(item.peerSymbolDisplayName))
            return SignalKernelGraphNodeRole::Output;
    }

    return item.outgoing ? SignalKernelGraphNodeRole::Output : fallbackRole;
}

SignalKernelGraphNode nodeForJourneyItem(
    const SignalJourneyItem& item,
    SignalKernelGraphNodeRole role,
    int id,
    const QString& kernelModuleName)
{
    SignalKernelGraphNode node;
    node.id = id;
    node.role = role;
    node.displayName = item.peerSymbolDisplayName;
    node.detailDisplayName = detailForItem(item);
    node.moduleDisplayName = displayModuleNameForRecord(item.peerSymbolRecord);
    node.typeDisplayName = item.peerTypeDisplayName;
    node.sourceRoleDisplayName = item.peerSourceRoleDisplayName;
    node.stableKey = item.peerStableKey;
    node.symbolRecord = item.peerSymbolRecord;
    node.declarationCodeLink = item.peerCodeLink;
    node.navigateCodeLink = navigateLinkForItem(item);
    node.previewCodeLink = previewLinkForItem(item);
    node.evidenceRange = item.evidenceRange;
    node.preciseEvidence = hasPreciseEvidence(item);
    const QString moduleName = moduleNameForRecord(item.peerSymbolRecord);
    node.crossModule = !moduleName.isEmpty()
        && !kernelModuleName.isEmpty()
        && moduleName != kernelModuleName;
    return node;
}

SignalKernelGraphNode kernelNodeForJourney(
    const SignalJourneyReport& journey,
    const QString& kernelModuleName)
{
    SignalKernelGraphNode node;
    node.id = 0;
    node.role = SignalKernelGraphNodeRole::Kernel;
    node.displayName = journey.declarationDisplayName;
    node.detailDisplayName = journey.declarationTypeDisplayName;
    node.moduleDisplayName = kernelModuleName;
    node.typeDisplayName = journey.declarationTypeDisplayName;
    node.sourceRoleDisplayName = journey.declarationSourceRoleDisplayName;
    node.stableKey = journey.declarationStableKey;
    node.symbolRecord = journey.declarationSymbolRecord;
    node.declarationCodeLink = journey.declarationCodeLink;
    node.navigateCodeLink = journey.declarationCodeLink;
    node.previewCodeLink = journey.declarationCodeLink;
    node.evidenceRange = sourceRangeForRecord(journey.declarationSymbolRecord);
    node.preciseEvidence = node.evidenceRange.isValid();
    return node;
}

void appendModuleGroups(QList<SignalKernelGraphModuleGroup>& groups,
                        const QList<SignalKernelGraphNode>& nodes,
                        const QString& kernelModuleName)
{
    QHash<QString, int> groupIndexByModule;
    for (const SignalKernelGraphNode& node : nodes) {
        const QString moduleName = node.moduleDisplayName.isEmpty()
            ? QStringLiteral("<unknown>")
            : node.moduleDisplayName;
        if (!groupIndexByModule.contains(moduleName)) {
            SignalKernelGraphModuleGroup group;
            group.moduleName = moduleName;
            group.crossModule = !kernelModuleName.isEmpty()
                && moduleName != kernelModuleName;
            groupIndexByModule.insert(moduleName, groups.size());
            groups.append(group);
        }
        groups[groupIndexByModule.value(moduleName)].nodeIds.append(node.id);
    }
}

void appendGraphNode(SignalKernelGraphReport& report,
                     QHash<QString, int>& existingNodes,
                     int& nextNodeId,
                     const SignalJourneyItem& item,
                     SignalKernelGraphNodeRole role)
{
    const QString key = nodeKeyForItem(item, role);
    int nodeId = existingNodes.value(key, -1);
    if (nodeId < 0) {
        nodeId = nextNodeId++;
        existingNodes.insert(key, nodeId);
        SignalKernelGraphNode node =
            nodeForJourneyItem(item, role, nodeId, report.kernelModuleName);
        if (role == SignalKernelGraphNodeRole::Input)
            report.inputs.append(node);
        else
            report.outputs.append(node);
    }

    SignalKernelGraphEdge edge;
    edge.label = edgeLabelForItem(item);
    if (role == SignalKernelGraphNodeRole::Input) {
        edge.fromNodeId = nodeId;
        edge.toNodeId = report.kernel.id;
    } else {
        edge.fromNodeId = report.kernel.id;
        edge.toNodeId = nodeId;
    }
    report.edges.append(edge);
}

void sortNodes(QList<SignalKernelGraphNode>& nodes)
{
    std::sort(nodes.begin(), nodes.end(),
              [](const SignalKernelGraphNode& lhs,
                 const SignalKernelGraphNode& rhs) {
                  if (lhs.moduleDisplayName != rhs.moduleDisplayName)
                      return lhs.moduleDisplayName < rhs.moduleDisplayName;
                  if (lhs.displayName != rhs.displayName)
                      return lhs.displayName < rhs.displayName;
                  if (lhs.previewCodeLink.fileName != rhs.previewCodeLink.fileName)
                      return lhs.previewCodeLink.fileName < rhs.previewCodeLink.fileName;
                  if (lhs.previewCodeLink.line != rhs.previewCodeLink.line)
                      return lhs.previewCodeLink.line < rhs.previewCodeLink.line;
                  return lhs.id < rhs.id;
              });
}

SignalJourneyQuery journeyQueryForGraphQuery(
    const SignalKernelGraphQuery& query)
{
    SignalJourneyQuery journeyQuery;
    journeyQuery.signalStableKey = query.signalStableKey;
    journeyQuery.signalName = query.signalName;
    journeyQuery.fileName = query.fileName;
    journeyQuery.moduleName = query.moduleName;
    return journeyQuery;
}
}

SignalKernelGraphService* SignalKernelGraphService::getInstance()
{
    if (!instance)
        instance = std::make_unique<SignalKernelGraphService>();
    return instance.get();
}

SignalKernelGraphService::SignalKernelGraphService(SemanticIndex* semanticIndex)
    : index(semanticIndex ? semanticIndex : SemanticIndex::getInstance())
{
}

SignalKernelGraphService::~SignalKernelGraphService() = default;

void SignalKernelGraphService::setSemanticIndex(SemanticIndex* semanticIndex)
{
    index = semanticIndex ? semanticIndex : SemanticIndex::getInstance();
}

SignalKernelGraphReport SignalKernelGraphService::buildSignalKernelGraph(
    const SignalKernelGraphQuery& query) const
{
    SignalKernelGraphReport report;
    SignalJourneyService journeyService(semanticIndex());
    const SignalJourneyReport journey =
        journeyService.buildSignalJourney(journeyQueryForGraphQuery(query));
    if (!journey.found) {
        report.notFoundReason =
            SignalKernelGraphNotFoundReason::SignalJourneyUnavailable;
        report.notFoundReasonDisplayName =
            journey.notFoundReasonDisplayName.isEmpty()
                ? QStringLiteral("signal journey unavailable")
                : journey.notFoundReasonDisplayName;
        return report;
    }

    report.found = true;
    report.kernelModuleName =
        moduleNameForRecord(journey.declarationSymbolRecord);
    report.kernel = kernelNodeForJourney(journey, report.kernelModuleName);

    QHash<QString, int> existingNodes;
    int nextNodeId = 1;

    for (const SignalJourneyItem& item : journey.assignments)
        appendGraphNode(report,
                        existingNodes,
                        nextNodeId,
                        item,
                        SignalKernelGraphNodeRole::Input);
    for (const SignalJourneyItem& item : journey.reads)
        appendGraphNode(report,
                        existingNodes,
                        nextNodeId,
                        item,
                        SignalKernelGraphNodeRole::Output);
    for (const SignalJourneyItem& item : journey.portConnections) {
        appendGraphNode(report,
                        existingNodes,
                        nextNodeId,
                        item,
                        roleForItem(item, SignalKernelGraphNodeRole::Input));
    }
    for (const SignalJourneyItem& item : journey.interfaceConnections) {
        appendGraphNode(report,
                        existingNodes,
                        nextNodeId,
                        item,
                        roleForItem(item, SignalKernelGraphNodeRole::Input));
    }
    for (const SignalJourneyItem& item : journey.timingConnections) {
        appendGraphNode(report,
                        existingNodes,
                        nextNodeId,
                        item,
                        roleForItem(item, SignalKernelGraphNodeRole::Output));
    }

    sortNodes(report.inputs);
    sortNodes(report.outputs);
    appendModuleGroups(report.inputModuleGroups,
                       report.inputs,
                       report.kernelModuleName);
    appendModuleGroups(report.outputModuleGroups,
                       report.outputs,
                       report.kernelModuleName);
    return report;
}

SemanticIndex* SignalKernelGraphService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}

QString SignalKernelGraphService::nodeRoleDisplayName(
    SignalKernelGraphNodeRole role)
{
    switch (role) {
    case SignalKernelGraphNodeRole::Kernel:
        return QStringLiteral("kernel");
    case SignalKernelGraphNodeRole::Input:
        return QStringLiteral("input");
    case SignalKernelGraphNodeRole::Output:
        return QStringLiteral("output");
    }
    return QStringLiteral("node");
}
