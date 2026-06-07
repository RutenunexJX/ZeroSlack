#include "hierarchyservice.h"

#include <QSet>

std::unique_ptr<HierarchyService> HierarchyService::instance = nullptr;

HierarchyService* HierarchyService::getInstance()
{
    if (!instance)
        instance = std::make_unique<HierarchyService>();
    return instance.get();
}

HierarchyService::HierarchyService(SemanticIndex* semanticIndex)
    : index(semanticIndex ? semanticIndex : SemanticIndex::getInstance()),
      relationshipService(index)
{
}

HierarchyService::~HierarchyService() = default;

void HierarchyService::setSemanticIndex(SemanticIndex* semanticIndex)
{
    index = semanticIndex ? semanticIndex : SemanticIndex::getInstance();
    relationshipService.setSemanticIndex(index);
}

QList<HierarchyNode> HierarchyService::getChildren(const HierarchyQuery& query) const
{
    RelationshipQuery relationshipQuery;
    relationshipQuery.symbolId = resolveSymbolId(query);
    relationshipQuery.outgoing = true;
    relationshipQuery.types = effectiveTypes(query);

    QList<HierarchyNode> result;
    const QList<RelationshipResult> relationships =
        relationshipService.findRelationships(relationshipQuery);
    for (const RelationshipResult& rel : relationships) {
        if (rel.toSymbol.symbolId < 0)
            continue;

        HierarchyNode node;
        node.symbol = rel.toSymbol;
        node.depth = 1;
        node.parentSymbolId = rel.fromSymbol.symbolId;
        node.viaType = rel.relationship.type;
        result.append(node);
    }
    return result;
}

QList<HierarchyNode> HierarchyService::getParents(const HierarchyQuery& query) const
{
    RelationshipQuery relationshipQuery;
    relationshipQuery.symbolId = resolveSymbolId(query);
    relationshipQuery.outgoing = false;
    relationshipQuery.types = effectiveTypes(query);

    QList<HierarchyNode> result;
    const QList<RelationshipResult> relationships =
        relationshipService.findRelationships(relationshipQuery);
    for (const RelationshipResult& rel : relationships) {
        if (rel.fromSymbol.symbolId < 0)
            continue;

        HierarchyNode node;
        node.symbol = rel.fromSymbol;
        node.depth = 1;
        node.parentSymbolId = rel.toSymbol.symbolId;
        node.viaType = rel.relationship.type;
        result.append(node);
    }
    return result;
}

QList<HierarchyNode> HierarchyService::getHierarchy(const HierarchyQuery& query) const
{
    const int rootId = resolveSymbolId(query);
    if (rootId < 0)
        return {};

    const int maxDepth = query.maxDepth < 0 ? 0 : query.maxDepth;
    QList<HierarchyNode> result;
    QList<HierarchyNode> queue;
    QSet<int> visited;

    HierarchyNode root;
    root.symbol = semanticIndex()->getSymbolById(rootId);
    root.depth = 0;
    root.parentSymbolId = -1;
    queue.append(root);
    visited.insert(rootId);

    while (!queue.isEmpty()) {
        const HierarchyNode current = queue.takeFirst();
        result.append(current);

        if (current.depth >= maxDepth)
            continue;

        HierarchyQuery childQuery = query;
        childQuery.symbolId = current.symbol.symbolId;
        const QList<HierarchyNode> children = getChildren(childQuery);
        for (HierarchyNode child : children) {
            if (visited.contains(child.symbol.symbolId))
                continue;
            visited.insert(child.symbol.symbolId);
            child.depth = current.depth + 1;
            child.parentSymbolId = current.symbol.symbolId;
            queue.append(child);
        }
    }

    return result;
}

SemanticIndex* HierarchyService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}

int HierarchyService::resolveSymbolId(const HierarchyQuery& query) const
{
    if (query.symbolId >= 0)
        return query.symbolId;
    if (query.symbolName.isEmpty())
        return -1;

    SemanticQueryContext context;
    context.fileName = query.fileName;
    context.moduleName = query.moduleName;
    return semanticIndex()->findSymbolId(query.symbolName, context);
}

QList<SymbolRelationshipEngine::RelationType> HierarchyService::effectiveTypes(
    const HierarchyQuery& query) const
{
    if (!query.types.isEmpty())
        return query.types;
    return {SymbolRelationshipEngine::INSTANTIATES};
}
