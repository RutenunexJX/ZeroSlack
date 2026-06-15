#include "relationshipspanelcoordinator.h"

#include "hierarchyservice.h"
#include "semanticpanelutils.h"

#include <QFileInfo>
#include <QMap>

namespace {

QString hierarchyDirectionText(HierarchyQuery::Direction direction)
{
    switch (direction) {
    case HierarchyQuery::Children:
        return QStringLiteral("Outgoing");
    case HierarchyQuery::Parents:
        return QStringLiteral("Incoming");
    case HierarchyQuery::Both:
        break;
    }
    return QStringLiteral("Related");
}

QString hierarchyNodeDirectionText(const HierarchyNode& node)
{
    return node.directionDisplayName.isEmpty()
        ? hierarchyDirectionText(node.direction)
        : node.directionDisplayName;
}

QTreeWidgetItem* getOrCreateHierarchyDirectionGroup(
    QTreeWidgetItem* parent,
    QMap<HierarchyQuery::Direction, QTreeWidgetItem*>& groups,
    const HierarchyRootDirectionGroup& directionGroup)
{
    const HierarchyQuery::Direction direction = directionGroup.direction;
    if (groups.contains(direction))
        return groups.value(direction);

    auto* group = new QTreeWidgetItem(parent);
    group->setText(0, directionGroup.displayName.isEmpty()
                          ? hierarchyDirectionText(direction)
                          : directionGroup.displayName);
    groups.insert(direction, group);
    return group;
}

QTreeWidgetItem* createHierarchyItem(QTreeWidgetItem* parent,
                                     const HierarchyNode& node,
                                     const QString& roleText)
{
    auto* item = new QTreeWidgetItem(parent);
    item->setText(0, roleText);
    item->setText(1, node.symbol.symbolName);
    item->setText(2, QFileInfo(node.symbol.fileName).fileName());
    item->setText(3, QString::number(node.symbol.startLine));
    item->setText(4, node.relationshipTypeDisplayName.isEmpty()
                         ? (node.depth == 0
                                ? QStringLiteral("Root")
                                : SemanticPanelUtils::relationshipTypeText(node.viaType))
                         : node.relationshipTypeDisplayName);
    item->setToolTip(2, node.symbol.fileName);
    item->setData(0, Qt::UserRole, node.symbol.fileName);
    item->setData(0, Qt::UserRole + 1, node.symbol.startLine);
    item->setData(0, Qt::UserRole + 2, node.symbol.startColumn);
    return item;
}

HierarchyPanelDirection hierarchyPanelDirectionFromValue(int value)
{
    switch (value) {
    case 1:
        return HierarchyPanelDirection::Outgoing;
    case 2:
        return HierarchyPanelDirection::Incoming;
    case 0:
    default:
        return HierarchyPanelDirection::All;
    }
}

} // namespace

void RelationshipsPanelCoordinator::refreshHierarchyTree(int typeFilter)
{
    HierarchyPanelQueryOptions hierarchyOptions;
    hierarchyOptions.symbolName = currentRelationshipSymbolName;
    hierarchyOptions.fileName = currentRelationshipFileName;
    hierarchyOptions.moduleName = currentRelationshipModuleName;
    hierarchyOptions.maxDepth = relationshipDepthCombo
        ? relationshipDepthCombo->currentData().toInt()
        : 2;
    hierarchyOptions.typeFilter = typeFilter;
    hierarchyOptions.direction = hierarchyPanelDirectionFromValue(
        relationshipDirectionCombo ? relationshipDirectionCombo->currentData().toInt() : 0);

    HierarchyService* hierarchyService = HierarchyService::getInstance();
    const HierarchyQuery hierarchyQuery =
        hierarchyService->queryForPanel(hierarchyOptions);
    const HierarchyReport report =
        hierarchyService->getHierarchyReport(hierarchyQuery);

    const bool hadExpandableItems =
        SemanticPanelUtils::treeHasExpandableItems(relationshipsTree);
    const QSet<QString> expandedKeys =
        SemanticPanelUtils::collectExpandedKeys(relationshipsTree);
    relationshipsTree->clear();
    QMap<int, QTreeWidgetItem*> itemByNodeId;
    QMap<HierarchyQuery::Direction, QTreeWidgetItem*> rootDirectionItems;
    QMap<HierarchyQuery::Direction, HierarchyRootDirectionGroup> rootDirectionGroups;
    for (const HierarchyRootDirectionGroup& directionGroup : report.rootDirectionGroups)
        rootDirectionGroups.insert(directionGroup.direction, directionGroup);
    QTreeWidgetItem* rootItem = nullptr;
    int visibleCount = 0;
    for (const HierarchyNode& node : report.nodes) {
        if (node.symbol.symbolId < 0)
            continue;

        QTreeWidgetItem* parent = relationshipsTree->invisibleRootItem();
        if (node.depth == 0) {
            rootItem = createHierarchyItem(parent, node, QStringLiteral("Root"));
            itemByNodeId.insert(node.nodeId, rootItem);
            visibleCount++;
            continue;
        }

        if (node.parentNodeId >= 0 && itemByNodeId.contains(node.parentNodeId))
            parent = itemByNodeId.value(node.parentNodeId);
        if (node.parentNodeId == 0 && rootItem) {
            const HierarchyRootDirectionGroup directionGroup =
                rootDirectionGroups.value(node.direction);
            parent = getOrCreateHierarchyDirectionGroup(rootItem,
                                                        rootDirectionItems,
                                                        directionGroup);
        }

        QTreeWidgetItem* item = createHierarchyItem(
            parent,
            node,
            hierarchyNodeDirectionText(node));
        itemByNodeId.insert(node.nodeId, item);
        visibleCount++;
    }
    for (const HierarchyRootDirectionGroup& directionGroup : report.rootDirectionGroups) {
        if (!rootDirectionItems.contains(directionGroup.direction))
            continue;
        rootDirectionItems.value(directionGroup.direction)->setText(
            0,
            SemanticPanelUtils::countLabel(
                directionGroup.displayName.isEmpty()
                    ? hierarchyDirectionText(directionGroup.direction)
                    : directionGroup.displayName,
                directionGroup.count));
    }
    SemanticPanelUtils::restoreTreeExpansion(relationshipsTree,
                                             hadExpandableItems,
                                             expandedKeys);

    if (relationshipsDock) {
        relationshipsDock->setWindowTitle(
            QStringLiteral("Relationships: %1 tree (%2)")
                .arg(currentRelationshipSymbolName)
                .arg(visibleCount));
        relationshipsDock->show();
        relationshipsDock->raise();
    }

    if (statusMessageHandler) {
        statusMessageHandler(
            QStringLiteral("Found %1 hierarchy nodes for %2")
                .arg(visibleCount)
                .arg(currentRelationshipSymbolName),
            3000);
    }
}
