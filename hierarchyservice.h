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

enum class HierarchyReportNotFoundReason {
    None,
    NoRootSymbol,
    NoHierarchy
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
    SymbolStableKey symbolStableKey;
    int depth = 0;
    int parentSymbolId = -1;
    SymbolStableKey parentStableKey;
    int nodeId = -1;
    int parentNodeId = -1;
    HierarchyQuery::Direction direction = HierarchyQuery::Children;
    SymbolRelationshipEngine::RelationType viaType = SymbolRelationshipEngine::CONTAINS;
    QString directionDisplayName;
    QString relationshipTypeDisplayName;
    QString symbolDisplayName;
    QString fileDisplayName;
    QString lineDisplayName;
};

struct HierarchyRootDirectionGroup {
    HierarchyQuery::Direction direction = HierarchyQuery::Children;
    QString displayName;
    QList<HierarchyNode> nodes;
    int count = 0;
};

struct HierarchyReport {
    HierarchyReportNotFoundReason notFoundReason =
        HierarchyReportNotFoundReason::None;
    QString notFoundReasonDisplayName;
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
    QList<HierarchyNode> moduleInstantiationChildren(int moduleSymbolId) const;

private:
    SemanticIndex* index = nullptr;
    RelationshipService relationshipService;
    static std::unique_ptr<HierarchyService> instance;

    SemanticIndex* semanticIndex() const;
    int resolveSymbolId(const HierarchyQuery& query) const;
    QList<SymbolRelationshipEngine::RelationType> effectiveTypes(const HierarchyQuery& query) const;
    static QString directionDisplayName(HierarchyQuery::Direction direction);
    static QString relationshipTypeDisplayName(SymbolRelationshipEngine::RelationType type);
    static QString symbolDisplayName(const sym_list::SymbolInfo& symbol);
    static QString fileDisplayName(const QString& fileName);
    static QString lineDisplayName(int line);
    static void fillDisplayMetadata(HierarchyNode& node);
};

#endif // HIERARCHYSERVICE_H
