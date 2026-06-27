#include "moduleblockdiagramservice.h"

#include "semanticindex.h"
#include "symboltaxonomy.h"

#include <QHash>

std::unique_ptr<ModuleBlockDiagramService>
    ModuleBlockDiagramService::instance = nullptr;

namespace {
QString moduleBlockNotFoundReasonDisplayName(
    ModuleBlockDiagramNotFoundReason reason)
{
    switch (reason) {
    case ModuleBlockDiagramNotFoundReason::None:
        return QString();
    case ModuleBlockDiagramNotFoundReason::NoRootModule:
        return QStringLiteral("no root module");
    case ModuleBlockDiagramNotFoundReason::NoModuleContainment:
        return QStringLiteral("no module containment");
    }
    return QStringLiteral("module block diagram unavailable");
}

bool isModuleBlockDefinition(const SemanticSymbolRecord& record)
{
    if (!record.isValid())
        return false;

    const SymbolTaxonomy::SemanticMetadata metadata =
        semanticMetadataForSymbolRecord(record);
    return metadata.declarationKind == SymbolTaxonomy::DeclarationKind::Module
        || metadata.declarationKind == SymbolTaxonomy::DeclarationKind::Interface;
}

QString moduleBlockTypeNameForInstance(const SemanticSymbolRecord& record)
{
    if (!record.type.resolvedTypeName.isEmpty())
        return record.type.resolvedTypeName;
    if (record.type.stableKey.isValid()
        && !record.type.stableKey.symbolName.isEmpty()) {
        return record.type.stableKey.symbolName;
    }
    if (!record.type.rawTypeText.isEmpty())
        return SymbolTaxonomy::interfaceTypeName(record.type.rawTypeText.trimmed());
    return QString();
}

SemanticSymbolRecord moduleDefinitionForRecord(
    SemanticIndex* index,
    const SemanticSymbolRecord& record)
{
    if (isModuleBlockDefinition(record))
        return record;

    if (!record.isValid())
        return {};

    const SymbolTaxonomy::SemanticMetadata metadata =
        semanticMetadataForSymbolRecord(record);
    if (!SymbolTaxonomy::isInstanceDeclaration(metadata))
        return {};

    const QString moduleTypeName = moduleBlockTypeNameForInstance(record);
    if (moduleTypeName.isEmpty() || !index)
        return {};

    const QList<SemanticSymbolRecord> candidates =
        index->findDefinitionRecords(moduleTypeName);
    for (const SemanticSymbolRecord& candidate : candidates) {
        if (isModuleBlockDefinition(candidate))
            return candidate;
    }
    return {};
}

QString moduleDisplayName(const SemanticSymbolRecord& record)
{
    return record.name.isEmpty()
        ? QStringLiteral("<unnamed>")
        : record.name;
}

ModuleBlockDiagramNode moduleBlockNodeFromRecord(
    const SemanticSymbolRecord& record,
    int nodeId,
    int parentNodeId,
    int depth)
{
    ModuleBlockDiagramNode node;
    node.moduleSymbolRecord = record;
    node.moduleStableKey = record.stableKey;
    node.nodeId = nodeId;
    node.parentNodeId = parentNodeId;
    node.depth = depth;
    node.moduleDisplayName = moduleDisplayName(record);
    const SymbolTaxonomy::SemanticMetadata metadata =
        semanticMetadataForSymbolRecord(record);
    node.moduleTypeDisplayName = SymbolTaxonomy::symbolTypeLabel(metadata);
    node.sourceRoleDisplayName =
        SymbolTaxonomy::sourceRoleDisplayName(metadata.sourceRole);
    node.definitionCodeLink =
        RtlInsightLink::fromFileLine(record.location.fileName,
                                     record.location.startLine,
                                     record.location.startColumn);
    return node;
}

ModuleBlockDiagramEdge moduleBlockEdgeFromNodes(
    const ModuleBlockDiagramNode& parent,
    const ModuleBlockDiagramNode& child)
{
    ModuleBlockDiagramEdge edge;
    edge.fromNodeId = parent.nodeId;
    edge.toNodeId = child.nodeId;
    edge.fromStableKey = parent.moduleStableKey;
    edge.toStableKey = child.moduleStableKey;
    edge.relationshipType = SymbolRelationshipEngine::INSTANTIATES;
    edge.relationshipDisplayName = QStringLiteral("Instantiates");
    edge.parentModuleDisplayName = parent.moduleDisplayName;
    edge.childModuleDisplayName = child.moduleDisplayName;
    edge.childDefinitionCodeLink = child.definitionCodeLink;
    return edge;
}
}

ModuleBlockDiagramService* ModuleBlockDiagramService::getInstance()
{
    if (!instance)
        instance = std::make_unique<ModuleBlockDiagramService>();
    return instance.get();
}

ModuleBlockDiagramService::ModuleBlockDiagramService(
    SemanticIndex* semanticIndex)
    : index(semanticIndex ? semanticIndex : SemanticIndex::getInstance())
    , hierarchyService(index)
{
}

ModuleBlockDiagramService::~ModuleBlockDiagramService() = default;

void ModuleBlockDiagramService::setSemanticIndex(SemanticIndex* semanticIndex)
{
    index = semanticIndex ? semanticIndex : SemanticIndex::getInstance();
    hierarchyService.setSemanticIndex(index);
}

ModuleBlockDiagramReport
ModuleBlockDiagramService::buildModuleBlockDiagram(
    const ModuleBlockDiagramQuery& query) const
{
    ModuleBlockDiagramReport report;
    report.groupDisplayName = QStringLiteral("Module Block Diagram");

    HierarchyQuery hierarchyQuery;
    hierarchyQuery.symbolStableKey = query.moduleStableKey;
    hierarchyQuery.symbolName = query.moduleName;
    hierarchyQuery.fileName = query.fileName;
    hierarchyQuery.maxDepth = query.maxDepth < 0 ? 0 : query.maxDepth;
    hierarchyQuery.direction = HierarchyQuery::Children;
    hierarchyQuery.types = {SymbolRelationshipEngine::INSTANTIATES};

    const HierarchyReport hierarchyReport =
        hierarchyService.getHierarchyReport(hierarchyQuery);
    const SemanticSymbolRecord rootRecord =
        moduleDefinitionForRecord(semanticIndex(),
                                  hierarchyReport.rootSymbolRecord);
    if (!rootRecord.isValid()) {
        report.notFoundReason =
            ModuleBlockDiagramNotFoundReason::NoRootModule;
        report.notFoundReasonDisplayName =
            moduleBlockNotFoundReasonDisplayName(report.notFoundReason);
        return report;
    }

    int nextNodeId = 0;
    report.root = moduleBlockNodeFromRecord(rootRecord, nextNodeId++, -1, 0);
    report.nodes.append(report.root);
    report.found = true;

    QHash<int, int> outputNodeIdsByHierarchyNodeId;
    outputNodeIdsByHierarchyNodeId.insert(0, report.root.nodeId);

    for (const HierarchyNode& hierarchyNode : hierarchyReport.nodes) {
        if (hierarchyNode.depth <= 0)
            continue;
        if (hierarchyNode.viaType != SymbolRelationshipEngine::INSTANTIATES)
            continue;
        if (!outputNodeIdsByHierarchyNodeId.contains(
                hierarchyNode.parentNodeId)) {
            continue;
        }

        const SemanticSymbolRecord childRecord =
            moduleDefinitionForRecord(semanticIndex(),
                                      hierarchyNode.symbolRecord);
        if (!childRecord.isValid())
            continue;

        const int parentOutputId =
            outputNodeIdsByHierarchyNodeId.value(hierarchyNode.parentNodeId);
        if (parentOutputId < 0 || parentOutputId >= report.nodes.size())
            continue;

        const ModuleBlockDiagramNode child =
            moduleBlockNodeFromRecord(childRecord,
                                      nextNodeId++,
                                      parentOutputId,
                                      hierarchyNode.depth);
        const int childIndex = report.nodes.size();
        report.nodes.append(child);
        outputNodeIdsByHierarchyNodeId.insert(hierarchyNode.nodeId,
                                              child.nodeId);
        report.edges.append(moduleBlockEdgeFromNodes(
            report.nodes.at(parentOutputId),
            report.nodes.at(childIndex)));
    }

    report.moduleCount = report.nodes.size();
    report.edgeCount = report.edges.size();
    if (report.edges.isEmpty()) {
        report.notFoundReason =
            ModuleBlockDiagramNotFoundReason::NoModuleContainment;
        report.notFoundReasonDisplayName =
            moduleBlockNotFoundReasonDisplayName(report.notFoundReason);
    }
    return report;
}

SemanticIndex* ModuleBlockDiagramService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}
