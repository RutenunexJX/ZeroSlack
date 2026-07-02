#ifndef MODULEBLOCKDIAGRAMSERVICE_H
#define MODULEBLOCKDIAGRAMSERVICE_H

#include "hierarchyservice.h"

#include <QList>
#include <QString>
#include <memory>

class SemanticIndex;

struct ModuleBlockDiagramQuery {
    SymbolStableKey moduleStableKey;
    QString moduleName;
    QString fileName;
    int maxDepth = 2;
};

enum class ModuleBlockDiagramNotFoundReason {
    None,
    NoRootModule,
    NoModuleContainment
};

struct ModuleBlockDiagramNode {
    SemanticSymbolRecord moduleSymbolRecord;
    SymbolStableKey moduleStableKey;
    int nodeId = -1;
    int parentNodeId = -1;
    int depth = 0;
    QString moduleDisplayName;
    QString instanceDisplayName;
    QString moduleTypeDisplayName;
    QString sourceRoleDisplayName;
    bool unresolved = false;
    QString unresolvedReason;
    RtlInsightCodeLink definitionCodeLink;
    RtlInsightCodeLink instanceCodeLink;
};

struct ModuleBlockDiagramEdge {
    int fromNodeId = -1;
    int toNodeId = -1;
    SymbolStableKey fromStableKey;
    SymbolStableKey toStableKey;
    SymbolRelationshipEngine::RelationType relationshipType =
        SymbolRelationshipEngine::INSTANTIATES;
    QString relationshipDisplayName;
    QString parentModuleDisplayName;
    QString childModuleDisplayName;
    QString childInstanceDisplayName;
    bool unresolved = false;
    QString unresolvedReason;
    RtlInsightCodeLink childDefinitionCodeLink;
    RtlInsightCodeLink childInstanceCodeLink;
};

struct ModuleBlockDiagramReport {
    bool found = false;
    ModuleBlockDiagramNotFoundReason notFoundReason =
        ModuleBlockDiagramNotFoundReason::None;
    QString groupDisplayName;
    QString notFoundReasonDisplayName;
    ModuleBlockDiagramNode root;
    QList<ModuleBlockDiagramNode> nodes;
    QList<ModuleBlockDiagramEdge> edges;
    int moduleCount = 0;
    int edgeCount = 0;
    int resolvedInstanceCount = 0;
    int unresolvedInstanceCount = 0;
};

class ModuleBlockDiagramService
{
public:
    static ModuleBlockDiagramService* getInstance();

    explicit ModuleBlockDiagramService(SemanticIndex* semanticIndex = nullptr);
    ~ModuleBlockDiagramService();

    void setSemanticIndex(SemanticIndex* semanticIndex);

    ModuleBlockDiagramReport buildModuleBlockDiagram(
        const ModuleBlockDiagramQuery& query) const;

private:
    SemanticIndex* index = nullptr;
    HierarchyService hierarchyService;
    static std::unique_ptr<ModuleBlockDiagramService> instance;

    SemanticIndex* semanticIndex() const;
};

#endif // MODULEBLOCKDIAGRAMSERVICE_H
