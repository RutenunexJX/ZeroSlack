#ifndef HIERARCHYSERVICE_H
#define HIERARCHYSERVICE_H

#include "relationshipservice.h"

#include <QList>
#include <QMap>
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

enum class HierarchyPanelDirection {
    All,
    Outgoing,
    Incoming
};

struct HierarchyPanelQueryOptions {
    QString symbolName;
    QString fileName;
    QString moduleName;
    int maxDepth = 2;
    HierarchyPanelDirection direction = HierarchyPanelDirection::All;
    int typeFilter = -1;
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

struct HierarchyRootDirectionGroup {
    HierarchyQuery::Direction direction = HierarchyQuery::Children;
    QList<HierarchyNode> nodes;
    int count = 0;
};

struct HierarchyReport {
    QList<HierarchyNode> nodes;
    QList<HierarchyRootDirectionGroup> rootDirectionGroups;
    int totalCount = 0;
    QMap<int, int> depthCounts;
    QMap<HierarchyQuery::Direction, int> directionCounts;
    QMap<HierarchyQuery::Direction, int> rootDirectionCounts;
    QMap<SymbolRelationshipEngine::RelationType, int> typeCounts;
};

class HierarchyService
{
public:
    static HierarchyService* getInstance();
    static QList<SymbolRelationshipEngine::RelationType> allRelationshipTypes();

    explicit HierarchyService(SemanticIndex* semanticIndex = nullptr);
    ~HierarchyService();

    void setSemanticIndex(SemanticIndex* semanticIndex);

    QList<HierarchyNode> getChildren(const HierarchyQuery& query) const;
    QList<HierarchyNode> getParents(const HierarchyQuery& query) const;
    QList<HierarchyNode> getHierarchy(const HierarchyQuery& query) const;
    HierarchyReport getHierarchyReport(const HierarchyQuery& query) const;
    HierarchyQuery queryForPanel(const HierarchyPanelQueryOptions& options) const;

private:
    SemanticIndex* index = nullptr;
    RelationshipService relationshipService;
    static std::unique_ptr<HierarchyService> instance;

    SemanticIndex* semanticIndex() const;
    int resolveSymbolId(const HierarchyQuery& query) const;
    QList<SymbolRelationshipEngine::RelationType> effectiveTypes(const HierarchyQuery& query) const;
};

#endif // HIERARCHYSERVICE_H
