#include "hierarchyservice.h"

#include <QSet>

namespace {
QString reportNotFoundReasonDisplayName(HierarchyReportNotFoundReason reason)
{
    switch (reason) {
    case HierarchyReportNotFoundReason::None:
        return QString();
    case HierarchyReportNotFoundReason::NoRootSymbol:
        return QStringLiteral("no root symbol");
    case HierarchyReportNotFoundReason::NoHierarchy:
        return QStringLiteral("no hierarchy");
    }
    return QStringLiteral("hierarchy report unavailable");
}

QString hierarchyNodePathKey(const HierarchyNode& node)
{
    const QString stableKey = symbolStableKeyText(node.symbolStableKey);
    if (!stableKey.isEmpty())
        return stableKey;
    return QStringLiteral("local:%1").arg(node.symbol.symbolId);
}
}

QList<HierarchyNode> HierarchyService::getHierarchy(const HierarchyQuery& query) const
{
    const HierarchyQuery normalized = normalizedQuery(query);
    const sym_list::SymbolInfo rootSymbol = resolveSubjectSymbol(normalized);
    if (rootSymbol.symbolId < 0)
        return {};

    const int maxDepth = normalized.maxDepth < 0 ? 0 : normalized.maxDepth;
    QList<HierarchyNode> result;
    struct WorkItem {
        HierarchyNode node;
        QSet<QString> path;
    };
    QList<WorkItem> queue;
    QSet<QString> emittedEdges;
    int nextNodeId = 0;

    HierarchyNode root;
    root.symbol = rootSymbol;
    root.symbolRecord = semanticSymbolRecordForSymbol(root.symbol);
    root.symbolStableKey = root.symbolRecord.stableKey.isValid()
        ? root.symbolRecord.stableKey
        : symbolStableKeyForSymbol(root.symbol);
    root.depth = 0;
    root.parentSymbolId = -1;
    root.nodeId = nextNodeId++;
    root.parentNodeId = -1;
    root.direction = normalized.direction;
    fillDisplayMetadata(root);
    WorkItem rootItem;
    rootItem.node = root;
    rootItem.path.insert(hierarchyNodePathKey(root));
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
                const QString childPathKey = hierarchyNodePathKey(child);
                if (current.path.contains(childPathKey))
                    continue;

                const QString edgeKey = QStringLiteral("%1:%2:%3:%4")
                    .arg(current.node.nodeId)
                    .arg(static_cast<int>(edgeDirection))
                    .arg(static_cast<int>(child.viaType))
                    .arg(childPathKey);
                if (emittedEdges.contains(edgeKey))
                    continue;
                emittedEdges.insert(edgeKey);

                child.depth = current.node.depth + 1;
                child.parentSymbolId = current.node.symbol.symbolId;
                child.parentStableKey = current.node.symbolStableKey;
                child.nodeId = nextNodeId++;
                child.parentNodeId = current.node.nodeId;
                child.direction = edgeDirection;
                fillDisplayMetadata(child);

                WorkItem childItem;
                childItem.node = child;
                childItem.path = current.path;
                childItem.path.insert(childPathKey);
                queue.append(childItem);
            }
        };

        HierarchyQuery childQuery = normalized;
        childQuery.symbolStableKey = current.node.symbolStableKey;
        if (!childQuery.symbolStableKey.isValid())
            childQuery.symbolId = current.node.symbol.symbolId;
        if (normalized.direction == HierarchyQuery::Children
            || normalized.direction == HierarchyQuery::Both) {
            appendNext(getChildren(childQuery), HierarchyQuery::Children);
        }
        if (normalized.direction == HierarchyQuery::Parents
            || normalized.direction == HierarchyQuery::Both) {
            appendNext(getParents(childQuery), HierarchyQuery::Parents);
        }
    }

    return result;
}

HierarchyReport HierarchyService::getHierarchyReport(const HierarchyQuery& query) const
{
    const HierarchyQuery normalized = normalizedQuery(query);
    HierarchyReport report;
    const sym_list::SymbolInfo rootSymbol = resolveSubjectSymbol(normalized);
    if (rootSymbol.symbolId < 0) {
        report.notFoundReason = HierarchyReportNotFoundReason::NoRootSymbol;
        report.notFoundReasonDisplayName =
            reportNotFoundReasonDisplayName(report.notFoundReason);
        return report;
    }

    HierarchyQuery resolvedQuery = normalized;
    resolvedQuery.symbolId = rootSymbol.symbolId;
    if (!resolvedQuery.symbolStableKey.isValid())
        resolvedQuery.symbolStableKey =
            symbolStableKeyForSymbol(rootSymbol);
    report.rootSymbolRecord = semanticSymbolRecordForSymbol(rootSymbol);
    report.rootStableKey = report.rootSymbolRecord.stableKey.isValid()
        ? report.rootSymbolRecord.stableKey
        : resolvedQuery.symbolStableKey;
    report.nodes = getHierarchy(resolvedQuery);
    report.totalCount = report.nodes.size();
    QMap<HierarchyQuery::Direction, int> rootDirectionGroupIndexes;
    for (const HierarchyNode& node : report.nodes) {
        report.depthCounts[node.depth]++;
        if (node.depth <= 0)
            continue;
        report.directionCounts[node.direction]++;
        report.typeCounts[node.viaType]++;
        if (node.parentNodeId == 0) {
            report.rootDirectionCounts[node.direction]++;
            if (!rootDirectionGroupIndexes.contains(node.direction)) {
                HierarchyRootDirectionGroup group;
                group.direction = node.direction;
                group.displayName = directionDisplayName(node.direction);
                rootDirectionGroupIndexes.insert(node.direction,
                                                 report.rootDirectionGroups.size());
                report.rootDirectionGroups.append(group);
            }
            HierarchyRootDirectionGroup& group =
                report.rootDirectionGroups[rootDirectionGroupIndexes.value(node.direction)];
            group.nodes.append(node);
            group.count++;
        }
    }
    if (report.totalCount <= 1) {
        report.notFoundReason = HierarchyReportNotFoundReason::NoHierarchy;
        report.notFoundReasonDisplayName =
            reportNotFoundReasonDisplayName(report.notFoundReason);
    } else {
        report.notFoundReason = HierarchyReportNotFoundReason::None;
    }
    return report;
}
