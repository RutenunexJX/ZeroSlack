#ifndef HIERARCHYSERVICE_H
#define HIERARCHYSERVICE_H

#include "relationshipservice.h"
#include "rtlinsightlink.h"

#include <QList>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <cstdint>
#include <memory>

struct HierarchyQuery {
    SymbolStableKey symbolStableKey;
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
    SemanticSymbolRecord symbolRecord;
    SymbolStableKey symbolStableKey;
    int depth = 0;
    SymbolStableKey parentStableKey;
    int nodeId = -1;
    int parentNodeId = -1;
    HierarchyQuery::Direction direction = HierarchyQuery::Children;
    SymbolRelationshipEngine::RelationType viaType = SymbolRelationshipEngine::CONTAINS;
    QString directionDisplayName;
    QString relationshipTypeDisplayName;
    QString symbolDisplayName;
    QString symbolTypeDisplayName;
    QString sourceRoleDisplayName;
    RtlInsightCodeLink codeLink;
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
    SemanticSymbolRecord rootSymbolRecord;
    SymbolStableKey rootStableKey;
    QList<HierarchyNode> nodes;
    QList<HierarchyRootDirectionGroup> rootDirectionGroups;
    int totalCount = 0;
    QMap<int, int> depthCounts;
    QMap<HierarchyQuery::Direction, int> directionCounts;
    QMap<HierarchyQuery::Direction, int> rootDirectionCounts;
    QMap<SymbolRelationshipEngine::RelationType, int> typeCounts;
};

struct DesignHierarchyNode {
    QString id;
    QString parentId;
    QString instanceName;
    QString moduleType;
    QString instanceFile;
    int instanceLine = -1;
    int instanceColumn = -1;
    QString definitionFile;
    int definitionLine = -1;
    int definitionColumn = -1;
    bool isTop = false;
    bool unresolved = false;
    QString unresolvedReason;
};

struct DesignHierarchyReport {
    QString topModule;
    QList<DesignHierarchyNode> nodes;
    QSet<QString> participatingFiles;
    QList<QString> unresolvedModules;
    std::uint64_t snapshotGeneration = 0;
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
    QString inferDesignTopModule() const;
    DesignHierarchyReport getDesignHierarchyReport(const QString& topModule) const;
    QStringList modulesDefinedInFile(const QString& fileName) const;
    HierarchyQuery queryForPanel(const HierarchyPanelQueryOptions& options) const;
    QList<HierarchyNode> moduleInstantiationChildren(const SymbolStableKey& moduleStableKey) const;

private:
    SemanticIndex* index = nullptr;
    RelationshipService relationshipService;
    static std::unique_ptr<HierarchyService> instance;

    SemanticIndex* semanticIndex() const;
    SemanticSymbolRecord resolveSubjectSymbolRecord(
        const HierarchyQuery& query) const;
    QList<SymbolRelationshipEngine::RelationType> effectiveTypes(const HierarchyQuery& query) const;
    static QString directionDisplayName(HierarchyQuery::Direction direction);
    static QString relationshipTypeDisplayName(SymbolRelationshipEngine::RelationType type);
    static QString fileDisplayName(const QString& fileName);
    static QString lineDisplayName(int line);
    static void fillDisplayMetadata(HierarchyNode& node);
    static HierarchyQuery normalizedQuery(const HierarchyQuery& query);
};

#endif // HIERARCHYSERVICE_H
