#include "relationshipspanelcoordinator.h"

#include "relationshipservice.h"
#include "semanticpanelutils.h"

namespace {

QTreeWidgetItem* createRelationshipItem(QTreeWidgetItem* parent,
                                        const DirectedRelationshipResult& relationship,
                                        const QString& direction)
{
    const sym_list::SymbolInfo& symbol = relationship.peerSymbol;
    auto* item = new QTreeWidgetItem(parent);
    item->setText(0, direction);
    item->setText(1, relationship.peerSymbolDisplayName);
    item->setText(2, relationship.peerFileDisplayName);
    item->setText(3, relationship.peerLineDisplayName);
    item->setText(4, relationship.typeDisplayName);
    const QString& explanation = relationship.explanation;
    item->setText(5, explanation);
    item->setToolTip(0, explanation);
    item->setToolTip(1, explanation);
    item->setToolTip(2, symbol.fileName);
    item->setToolTip(4, explanation);
    item->setToolTip(5, explanation);
    item->setData(0, Qt::UserRole, symbol.fileName);
    item->setData(0, Qt::UserRole + 1, symbol.startLine);
    item->setData(0, Qt::UserRole + 2, symbol.startColumn);
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
        refreshHierarchyTree(typeFilter);
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
        const QString direction = directionGroupReport.displayName.isEmpty()
            ? (directionGroupReport.direction == DirectedRelationshipResult::Outgoing
                   ? QStringLiteral("Outgoing")
                   : QStringLiteral("Incoming"))
            : directionGroupReport.displayName;
        auto* directionGroup = new QTreeWidgetItem(relationshipsTree);
        directionGroup->setText(0, SemanticPanelUtils::countLabel(
                                       direction,
                                       directionGroupReport.count));

        for (const RelationshipTypeGroup& typeGroupReport : directionGroupReport.typeGroups) {
            auto* typeGroup = new QTreeWidgetItem(directionGroup);
            typeGroup->setText(4, SemanticPanelUtils::countLabel(typeGroupReport.displayName,
                                                                 typeGroupReport.count));

            for (const DirectedRelationshipResult& directed : typeGroupReport.relationships) {
                createRelationshipItem(typeGroup, directed, direction);
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
