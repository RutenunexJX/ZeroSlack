#include "relationshipspanelcoordinator.h"

#include "hierarchyservice.h"
#include "relationshipservice.h"
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

QTreeWidgetItem* getOrCreateHierarchyDirectionGroup(
    QTreeWidgetItem* parent,
    QMap<HierarchyQuery::Direction, QTreeWidgetItem*>& groups,
    HierarchyQuery::Direction direction)
{
    if (groups.contains(direction))
        return groups.value(direction);

    auto* group = new QTreeWidgetItem(parent);
    group->setText(0, hierarchyDirectionText(direction));
    groups.insert(direction, group);
    return group;
}

QTreeWidgetItem* createRelationshipItem(QTreeWidgetItem* parent,
                                        const QString& direction,
                                        const sym_list::SymbolInfo& symbol,
                                        SymbolRelationshipEngine::RelationType type)
{
    auto* item = new QTreeWidgetItem(parent);
    item->setText(0, direction);
    item->setText(1, symbol.symbolName);
    item->setText(2, QFileInfo(symbol.fileName).fileName());
    item->setText(3, QString::number(symbol.startLine));
    item->setText(4, SemanticPanelUtils::relationshipTypeText(type));
    item->setToolTip(2, symbol.fileName);
    item->setData(0, Qt::UserRole, symbol.fileName);
    item->setData(0, Qt::UserRole + 1, symbol.startLine);
    item->setData(0, Qt::UserRole + 2, symbol.startColumn);
    return item;
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
    item->setText(4, node.depth == 0
                         ? QStringLiteral("Root")
                         : SemanticPanelUtils::relationshipTypeText(node.viaType));
    item->setToolTip(2, node.symbol.fileName);
    item->setData(0, Qt::UserRole, node.symbol.fileName);
    item->setData(0, Qt::UserRole + 1, node.symbol.startLine);
    item->setData(0, Qt::UserRole + 2, node.symbol.startColumn);
    return item;
}

RelationshipPanelDirection relationshipPanelDirectionFromValue(int value)
{
    switch (value) {
    case 1:
        return RelationshipPanelDirection::Outgoing;
    case 2:
        return RelationshipPanelDirection::Incoming;
    case 0:
    default:
        return RelationshipPanelDirection::All;
    }
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

void RelationshipsPanelCoordinator::refresh()
{
    if (!relationshipsTree || currentRelationshipSymbolName.isEmpty())
        return;

    const int typeFilter = relationshipTypeCombo
        ? relationshipTypeCombo->currentData().toInt()
        : -1;

    const bool treeMode = relationshipViewCombo
        && relationshipViewCombo->currentData().toInt() == 1;
    if (treeMode) {
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
                parent = getOrCreateHierarchyDirectionGroup(rootItem,
                                                            rootDirectionItems,
                                                            node.direction);
            }

            QTreeWidgetItem* item = createHierarchyItem(
                parent,
                node,
                hierarchyDirectionText(node.direction));
            itemByNodeId.insert(node.nodeId, item);
            visibleCount++;
        }
        for (const HierarchyRootDirectionGroup& directionGroup : report.rootDirectionGroups) {
            if (!rootDirectionItems.contains(directionGroup.direction))
                continue;
            rootDirectionItems.value(directionGroup.direction)->setText(
                0,
                SemanticPanelUtils::countLabel(
                    hierarchyDirectionText(directionGroup.direction),
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
        return;
    }

    RelationshipPanelQueryOptions browseOptions;
    browseOptions.symbolName = currentRelationshipSymbolName;
    browseOptions.fileName = currentRelationshipFileName;
    browseOptions.moduleName = currentRelationshipModuleName;
    browseOptions.typeFilter = typeFilter;
    browseOptions.direction = relationshipPanelDirectionFromValue(
        relationshipDirectionCombo ? relationshipDirectionCombo->currentData().toInt() : 0);

    RelationshipService* relationshipService = RelationshipService::getInstance();
    const RelationshipBrowseQuery browseQuery =
        relationshipService->queryForPanel(browseOptions);
    const RelationshipReport report =
        relationshipService->findRelationshipReport(browseQuery);
    const QString subjectName = report.subjectSymbol.symbolName.isEmpty()
        ? currentRelationshipSymbolName
        : report.subjectSymbol.symbolName;

    const bool hadExpandableItems =
        SemanticPanelUtils::treeHasExpandableItems(relationshipsTree);
    const QSet<QString> expandedKeys =
        SemanticPanelUtils::collectExpandedKeys(relationshipsTree);
    relationshipsTree->clear();
    for (const RelationshipDirectionGroup& directionGroupReport : report.directionGroups) {
        const QString direction = directionGroupReport.direction == DirectedRelationshipResult::Outgoing
            ? QStringLiteral("Outgoing")
            : QStringLiteral("Incoming");
        auto* directionGroup = new QTreeWidgetItem(relationshipsTree);
        directionGroup->setText(0, SemanticPanelUtils::countLabel(
                                       direction,
                                       directionGroupReport.count));

        for (const RelationshipTypeGroup& typeGroupReport : directionGroupReport.typeGroups) {
            auto* typeGroup = new QTreeWidgetItem(directionGroup);
            const QString typeText =
                SemanticPanelUtils::relationshipTypeText(typeGroupReport.type);
            typeGroup->setText(4, SemanticPanelUtils::countLabel(typeText,
                                                                 typeGroupReport.count));

            for (const DirectedRelationshipResult& directed : typeGroupReport.relationships) {
                createRelationshipItem(typeGroup,
                                       direction,
                                       directed.peerSymbol,
                                       directed.relationship.relationship.type);
            }
        }
    }
    SemanticPanelUtils::restoreTreeExpansion(relationshipsTree,
                                             hadExpandableItems,
                                             expandedKeys);

    if (relationshipsDock) {
        relationshipsDock->setWindowTitle(
            QStringLiteral("Relationships: %1 (%2)")
                .arg(subjectName)
                .arg(report.totalCount));
        relationshipsDock->show();
        relationshipsDock->raise();
    }

    if (statusMessageHandler) {
        statusMessageHandler(
            QStringLiteral("Found %1 relationships for %2")
                .arg(report.totalCount)
                .arg(subjectName),
            3000);
    }
}
