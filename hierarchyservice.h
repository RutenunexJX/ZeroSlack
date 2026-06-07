#ifndef HIERARCHYSERVICE_H
#define HIERARCHYSERVICE_H

#include "relationshipservice.h"

#include <QList>
#include <QString>
#include <memory>

struct HierarchyQuery {
    int symbolId = -1;
    QString symbolName;
    QString fileName;
    QString moduleName;
    int maxDepth = 1;
    enum Direction {
        Children,
        Parents,
        Both
    } direction = Children;
    QList<SymbolRelationshipEngine::RelationType> types;
};

struct HierarchyNode {
    sym_list::SymbolInfo symbol;
    int depth = 0;
    int parentSymbolId = -1;
    int nodeId = -1;
    int parentNodeId = -1;
    HierarchyQuery::Direction direction = HierarchyQuery::Children;
    SymbolRelationshipEngine::RelationType viaType = SymbolRelationshipEngine::CONTAINS;
};

class HierarchyService
{
public:
    static HierarchyService* getInstance();

    explicit HierarchyService(SemanticIndex* semanticIndex = nullptr);
    ~HierarchyService();

    void setSemanticIndex(SemanticIndex* semanticIndex);

    QList<HierarchyNode> getChildren(const HierarchyQuery& query) const;
    QList<HierarchyNode> getParents(const HierarchyQuery& query) const;
    QList<HierarchyNode> getHierarchy(const HierarchyQuery& query) const;

private:
    SemanticIndex* index = nullptr;
    RelationshipService relationshipService;
    static std::unique_ptr<HierarchyService> instance;

    SemanticIndex* semanticIndex() const;
    int resolveSymbolId(const HierarchyQuery& query) const;
    QList<SymbolRelationshipEngine::RelationType> effectiveTypes(const HierarchyQuery& query) const;
};

#endif // HIERARCHYSERVICE_H
