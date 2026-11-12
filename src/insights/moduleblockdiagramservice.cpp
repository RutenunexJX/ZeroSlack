#include "moduleblockdiagramservice.h"
#include "semanticindex.h"
#include <QDir>
#include <QFileInfo>
#include <stdexcept>

std::unique_ptr<ModuleBlockDiagramService> ModuleBlockDiagramService::instance;
ModuleBlockDiagramService* ModuleBlockDiagramService::getInstance()
{
    if (!instance) instance = std::make_unique<ModuleBlockDiagramService>();
    return instance.get();
}
ModuleBlockDiagramService::ModuleBlockDiagramService(SemanticIndex* value)
    : index(value ? value : SemanticIndex::getInstance()), hierarchyService(index) {}
ModuleBlockDiagramService::ModuleBlockDiagramService(SemanticSnapshotToken snapshot)
    : ModuleBlockDiagramService(std::make_shared<SemanticIndex>(std::move(snapshot))) {}
ModuleBlockDiagramService::ModuleBlockDiagramService(std::shared_ptr<SemanticIndex> owner)
    : ModuleBlockDiagramService(owner.get()) { ownedIndex = std::move(owner); }
ModuleBlockDiagramService::~ModuleBlockDiagramService() = default;
void ModuleBlockDiagramService::setSemanticIndex(SemanticIndex* value)
{ index = value ? value : SemanticIndex::getInstance(); hierarchyService.setSemanticIndex(index); }
SemanticIndex* ModuleBlockDiagramService::semanticIndex() const
{ return index ? index : SemanticIndex::getInstance(); }

ModuleBlockDiagramReport ModuleBlockDiagramService::buildModuleBlockDiagram(const ModuleBlockDiagramQuery& query) const
{
    const auto cancelled = [&] {
        if (query.isCancelled && query.isCancelled()) throw std::runtime_error("Module report cancelled");
    };
    cancelled();
    const auto hierarchy = hierarchyService.automaticDesignHierarchy({}, query.isCancelled);
    cancelled();
    ModuleBlockDiagramReport report;
    report.groupDisplayName = QStringLiteral("Module Block Diagram");
    const auto normalized = [](const QString& file) { return QDir::cleanPath(QFileInfo(file).absoluteFilePath()).toCaseFolded(); };
    QList<int> candidates;
    for (int i = 0; i < hierarchy.nodes.size(); ++i) {
        const auto& node = hierarchy.nodes[i];
        if (!query.rootIdentity.isEmpty() && node.rootId != query.rootIdentity) continue;
        if (!query.instancePath.isEmpty() && node.instancePath != query.instancePath) continue;
        if (!query.configurationIdentity.isEmpty() && node.configurationId != query.configurationIdentity) continue;
        if (query.moduleStableKey.isValid() && node.definitionId != HierarchyService::definitionIdentity(query.moduleStableKey)) continue;
        if (!query.moduleName.isEmpty() && node.moduleType != query.moduleName) continue;
        if (!query.fileName.isEmpty() && normalized(node.definitionFile) != normalized(query.fileName)) continue;
        candidates.append(i);
    }
    if (candidates.size() > 1 && query.instancePath.isEmpty()) {
        const auto all = candidates;
        candidates.clear();
        for (int i : all) if (hierarchy.nodes[i].isTop) candidates.append(i);
    }
    if (candidates.size() != 1) {
        report.notFoundReason = ModuleBlockDiagramNotFoundReason::NoRootModule;
        report.notFoundReasonDisplayName = QStringLiteral("No unambiguous module context. Select an instance in Design.");
        return report;
    }
    const auto& selected = hierarchy.nodes[candidates.first()];
    const int rootIndex = hierarchy.nodeById.value(selected.rootId, candidates.first());
    report.initialFocusId = selected.id;
    QHash<QString, int> ids;
    QList<QPair<int, int>> pending{{rootIndex, 0}};
    while (!pending.isEmpty()) {
        cancelled();
        const auto [position, depth] = pending.takeLast();
        const auto& source = hierarchy.nodes[position];
        ModuleBlockDiagramNode node;
        node.nodeId = report.nodes.size(); node.parentNodeId = ids.value(source.parentId, -1); node.depth = depth;
        node.moduleStableKey = source.definitionKey;
        node.moduleSymbolRecord = semanticIndex()->getSymbolRecordByStableKey(source.definitionKey);
        node.sourceRoleDisplayName = SymbolTaxonomy::sourceRoleDisplayName(node.moduleSymbolRecord.sourceRole);
        node.moduleDisplayName = source.moduleType; node.moduleTypeDisplayName = source.moduleType;
        node.instanceDisplayName = source.instanceName; node.unresolved = source.unresolved;
        node.unresolvedReason = source.unresolvedReason; node.status = source.status;
        node.stableId = source.id; node.rootIdentity = source.rootId; node.rootModule = source.rootModule;
        node.rootDefinitionFile = hierarchy.nodes[rootIndex].definitionFile;
        node.definitionIdentity = source.definitionId; node.configurationIdentity = source.configurationId;
        node.instancePath = source.instancePath;
        node.definitionCodeLink = RtlInsightLink::fromFileLine(source.definitionFile, source.definitionLine, source.definitionColumn);
        node.instanceCodeLink = RtlInsightLink::fromFileLine(source.instanceFile, source.instanceLine, source.instanceColumn);
        ids.insert(source.id, node.nodeId);
        if (node.parentNodeId >= 0) {
            const auto& parent = report.nodes[node.parentNodeId];
            ModuleBlockDiagramEdge edge;
            edge.fromNodeId = parent.nodeId; edge.toNodeId = node.nodeId;
            edge.fromStableKey = parent.moduleStableKey; edge.toStableKey = node.moduleStableKey;
            edge.relationshipDisplayName = QStringLiteral("Instantiates");
            edge.parentModuleDisplayName = parent.moduleDisplayName; edge.childModuleDisplayName = node.moduleDisplayName;
            edge.childInstanceDisplayName = node.instanceDisplayName; edge.unresolved = node.unresolved;
            edge.unresolvedReason = node.unresolvedReason;
            edge.childDefinitionCodeLink = node.definitionCodeLink; edge.childInstanceCodeLink = node.instanceCodeLink;
            report.edges.append(edge);
            if (node.unresolved) ++report.unresolvedInstanceCount; else ++report.resolvedInstanceCount;
        }
        report.nodes.append(node);
        if (query.maxDepth >= 0 && depth >= query.maxDepth) continue;
        const auto& children = hierarchy.childrenById.value(source.id);
        for (auto it = children.crbegin(); it != children.crend(); ++it) pending.append({*it, depth + 1});
    }
    report.found = !report.nodes.isEmpty();
    if (report.found) report.root = report.nodes.first();
    report.moduleCount = report.nodes.size(); report.edgeCount = report.edges.size();
    return report;
}
