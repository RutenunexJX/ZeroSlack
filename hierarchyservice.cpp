#include "hierarchyservice.h"

#include <QSet>
#include <algorithm>

std::unique_ptr<HierarchyService> HierarchyService::instance = nullptr;

static bool hierarchyNodeLess(const HierarchyNode& lhs, const HierarchyNode& rhs)
{
    if (lhs.viaType != rhs.viaType)
        return static_cast<int>(lhs.viaType) < static_cast<int>(rhs.viaType);

    const int fileCompare = QString::compare(lhs.symbol.fileName,
                                             rhs.symbol.fileName,
                                             Qt::CaseInsensitive);
    if (fileCompare != 0)
        return fileCompare < 0;
    if (lhs.symbol.startLine != rhs.symbol.startLine)
        return lhs.symbol.startLine < rhs.symbol.startLine;
    if (lhs.symbol.startColumn != rhs.symbol.startColumn)
        return lhs.symbol.startColumn < rhs.symbol.startColumn;
    return QString::compare(lhs.symbol.symbolName,
                            rhs.symbol.symbolName,
                            Qt::CaseInsensitive) < 0;
}

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
        node.direction = HierarchyQuery::Children;
        node.viaType = rel.relationship.type;
        result.append(node);
    }
    std::sort(result.begin(), result.end(), hierarchyNodeLess);
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
        node.direction = HierarchyQuery::Parents;
        node.viaType = rel.relationship.type;
        result.append(node);
    }
    std::sort(result.begin(), result.end(), hierarchyNodeLess);
    return result;
}

QList<HierarchyNode> HierarchyService::getHierarchy(const HierarchyQuery& query) const
{
    const int rootId = resolveSymbolId(query);
    if (rootId < 0)
        return {};

    const int maxDepth = query.maxDepth < 0 ? 0 : query.maxDepth;
    QList<HierarchyNode> result;
    struct WorkItem {
        HierarchyNode node;
        QSet<int> path;
    };
    QList<WorkItem> queue;
    QSet<QString> emittedEdges;
    int nextNodeId = 0;

    HierarchyNode root;
    root.symbol = semanticIndex()->getSymbolById(rootId);
    root.depth = 0;
    root.parentSymbolId = -1;
    root.nodeId = nextNodeId++;
    root.parentNodeId = -1;
    root.direction = query.direction;
    WorkItem rootItem;
    rootItem.node = root;
    rootItem.path.insert(rootId);
    queue.append(rootItem);

    while (!queue.isEmpty()) {
        const WorkItem current = queue.takeFirst();
        result.append(current.node);

        if (current.node.depth >= maxDepth)
            continue;

        auto appendNext = [&](QList<HierarchyNode> nextNodes,
                              HierarchyQuery::Direction edgeDirection) {
            for (HierarchyNode child : nextNodes) {
                if (child.symbol.symbolId < 0)
                    continue;
                if (current.path.contains(child.symbol.symbolId))
                    continue;

                const QString edgeKey = QStringLiteral("%1:%2:%3:%4")
                    .arg(current.node.nodeId)
                    .arg(static_cast<int>(edgeDirection))
                    .arg(static_cast<int>(child.viaType))
                    .arg(child.symbol.symbolId);
                if (emittedEdges.contains(edgeKey))
                    continue;
                emittedEdges.insert(edgeKey);

                child.depth = current.node.depth + 1;
                child.parentSymbolId = current.node.symbol.symbolId;
                child.nodeId = nextNodeId++;
                child.parentNodeId = current.node.nodeId;
                child.direction = edgeDirection;

                WorkItem childItem;
                childItem.node = child;
                childItem.path = current.path;
                childItem.path.insert(child.symbol.symbolId);
                queue.append(childItem);
            }
        };

        HierarchyQuery childQuery = query;
        childQuery.symbolId = current.node.symbol.symbolId;
        if (query.direction == HierarchyQuery::Children
            || query.direction == HierarchyQuery::Both) {
            appendNext(getChildren(childQuery), HierarchyQuery::Children);
        }
        if (query.direction == HierarchyQuery::Parents
            || query.direction == HierarchyQuery::Both) {
            appendNext(getParents(childQuery), HierarchyQuery::Parents);
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
