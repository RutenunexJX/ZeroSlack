#include "relationshipspanelcoordinator.h"

#include "hierarchyservice.h"
#include "relationshipservice.h"
#include "semanticpanelutils.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMap>
#include <QVBoxLayout>

#include <utility>

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

} // namespace

RelationshipsPanelCoordinator::RelationshipsPanelCoordinator(QWidget* parent)
{
    auto* panel = new QWidget(parent);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto* filtersLayout = new QHBoxLayout();
    filtersLayout->setContentsMargins(0, 0, 0, 0);
    filtersLayout->setSpacing(6);

    relationshipViewCombo = new QComboBox(panel);
    relationshipViewCombo->setObjectName(QStringLiteral("relationshipViewCombo"));
    relationshipViewCombo->addItem(QStringLiteral("Direct"), 0);
    relationshipViewCombo->addItem(QStringLiteral("Tree"), 1);
    relationshipViewCombo->setToolTip(QStringLiteral("Relationship view"));
    filtersLayout->addWidget(relationshipViewCombo);

    relationshipDirectionCombo = new QComboBox(panel);
    relationshipDirectionCombo->setObjectName(QStringLiteral("relationshipDirectionCombo"));
    relationshipDirectionCombo->addItem(QStringLiteral("All Directions"), 0);
    relationshipDirectionCombo->addItem(QStringLiteral("Outgoing"), 1);
    relationshipDirectionCombo->addItem(QStringLiteral("Incoming"), 2);
    relationshipDirectionCombo->setToolTip(QStringLiteral("Relationship direction"));
    filtersLayout->addWidget(relationshipDirectionCombo);

    relationshipTypeCombo = new QComboBox(panel);
    relationshipTypeCombo->setObjectName(QStringLiteral("relationshipTypeCombo"));
    relationshipTypeCombo->addItem(QStringLiteral("All Types"), -1);
    relationshipTypeCombo->addItem(SemanticPanelUtils::relationshipTypeText(
                                       SymbolRelationshipEngine::REFERENCES),
                                   static_cast<int>(SymbolRelationshipEngine::REFERENCES));
    relationshipTypeCombo->addItem(SemanticPanelUtils::relationshipTypeText(
                                       SymbolRelationshipEngine::INSTANTIATES),
                                   static_cast<int>(SymbolRelationshipEngine::INSTANTIATES));
    relationshipTypeCombo->addItem(SemanticPanelUtils::relationshipTypeText(
                                       SymbolRelationshipEngine::CALLS),
                                   static_cast<int>(SymbolRelationshipEngine::CALLS));
    relationshipTypeCombo->addItem(SemanticPanelUtils::relationshipTypeText(
                                       SymbolRelationshipEngine::ASSIGNS_TO),
                                   static_cast<int>(SymbolRelationshipEngine::ASSIGNS_TO));
    relationshipTypeCombo->addItem(SemanticPanelUtils::relationshipTypeText(
                                       SymbolRelationshipEngine::READS_FROM),
                                   static_cast<int>(SymbolRelationshipEngine::READS_FROM));
    relationshipTypeCombo->addItem(SemanticPanelUtils::relationshipTypeText(
                                       SymbolRelationshipEngine::CLOCKS),
                                   static_cast<int>(SymbolRelationshipEngine::CLOCKS));
    relationshipTypeCombo->addItem(SemanticPanelUtils::relationshipTypeText(
                                       SymbolRelationshipEngine::RESETS),
                                   static_cast<int>(SymbolRelationshipEngine::RESETS));
    relationshipTypeCombo->addItem(SemanticPanelUtils::relationshipTypeText(
                                       SymbolRelationshipEngine::CONTAINS),
                                   static_cast<int>(SymbolRelationshipEngine::CONTAINS));
    relationshipTypeCombo->addItem(SemanticPanelUtils::relationshipTypeText(
                                       SymbolRelationshipEngine::GENERATES),
                                   static_cast<int>(SymbolRelationshipEngine::GENERATES));
    relationshipTypeCombo->setToolTip(QStringLiteral("Relationship type"));
    filtersLayout->addWidget(relationshipTypeCombo);

    relationshipDepthCombo = new QComboBox(panel);
    relationshipDepthCombo->setObjectName(QStringLiteral("relationshipDepthCombo"));
    relationshipDepthCombo->addItem(QStringLiteral("Depth 1"), 1);
    relationshipDepthCombo->addItem(QStringLiteral("Depth 2"), 2);
    relationshipDepthCombo->addItem(QStringLiteral("Depth 3"), 3);
    relationshipDepthCombo->addItem(QStringLiteral("Depth 4"), 4);
    relationshipDepthCombo->setToolTip(QStringLiteral("Tree depth"));
    relationshipDepthCombo->setEnabled(false);
    filtersLayout->addWidget(relationshipDepthCombo);
    filtersLayout->addStretch(1);
    layout->addLayout(filtersLayout);

    relationshipsTree = new QTreeWidget(panel);
    relationshipsTree->setObjectName(QStringLiteral("relationshipsTree"));
    relationshipsTree->setColumnCount(5);
    relationshipsTree->setHeaderLabels({"Direction", "Symbol", "File", "Line", "Relationship"});
    relationshipsTree->setRootIsDecorated(true);
    relationshipsTree->setAlternatingRowColors(true);
    relationshipsTree->setSelectionMode(QAbstractItemView::SingleSelection);
    relationshipsTree->header()->setStretchLastSection(true);
    relationshipsTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    relationshipsTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    relationshipsTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    relationshipsTree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    layout->addWidget(relationshipsTree);

    relationshipsDock = new QDockWidget("Relationships", parent);
    relationshipsDock->setObjectName(QStringLiteral("relationshipsDock"));
    relationshipsDock->setWidget(panel);
    relationshipsDock->setFeatures(QDockWidget::DockWidgetMovable |
                                   QDockWidget::DockWidgetFloatable |
                                   QDockWidget::DockWidgetClosable);
    relationshipsDock->hide();

    QObject::connect(relationshipDirectionCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                     relationshipsDock, [this](int) { refresh(); });
    QObject::connect(relationshipTypeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                     relationshipsDock, [this](int) { refresh(); });
    QObject::connect(relationshipDepthCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                     relationshipsDock, [this](int) { refresh(); });
    QObject::connect(relationshipViewCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                     relationshipsDock, [this](int) {
                         const bool treeMode = relationshipViewCombo
                             && relationshipViewCombo->currentData().toInt() == 1;
                         if (relationshipDepthCombo)
                             relationshipDepthCombo->setEnabled(treeMode);
                         refresh();
                     });

    QObject::connect(relationshipsTree, &QTreeWidget::itemDoubleClicked,
                     relationshipsDock, [this](QTreeWidgetItem* item, int) {
                         if (!item || !navigationHandler)
                             return;
                         const QString fileName = item->data(0, Qt::UserRole).toString();
                         if (fileName.isEmpty())
                             return;
                         const int line = item->data(0, Qt::UserRole + 1).toInt();
                         const int column = item->data(0, Qt::UserRole + 2).toInt();
                         navigationHandler(fileName, line, column);
                     });
}

void RelationshipsPanelCoordinator::setNavigationHandler(
    std::function<void(const QString&, int, int)> handler)
{
    navigationHandler = std::move(handler);
}

void RelationshipsPanelCoordinator::setStatusMessageHandler(
    std::function<void(const QString&, int)> handler)
{
    statusMessageHandler = std::move(handler);
}

void RelationshipsPanelCoordinator::showRelationshipsForSymbol(const QString& symbolName,
                                                               const QString& fileName,
                                                               const QString& moduleName)
{
    if (!relationshipsTree || symbolName.isEmpty())
        return;

    currentRelationshipSymbolName = symbolName;
    currentRelationshipFileName = fileName;
    currentRelationshipModuleName = moduleName;
    refresh();
}

void RelationshipsPanelCoordinator::refresh()
{
    if (!relationshipsTree || currentRelationshipSymbolName.isEmpty())
        return;

    RelationshipQuery query;
    query.symbolName = currentRelationshipSymbolName;
    query.fileName = currentRelationshipFileName;
    query.moduleName = currentRelationshipModuleName;

    const int typeFilter = relationshipTypeCombo
        ? relationshipTypeCombo->currentData().toInt()
        : -1;
    if (typeFilter >= 0) {
        query.types = {
            static_cast<SymbolRelationshipEngine::RelationType>(typeFilter)
        };
    }

    const bool treeMode = relationshipViewCombo
        && relationshipViewCombo->currentData().toInt() == 1;
    if (treeMode) {
        HierarchyQuery hierarchyQuery;
        hierarchyQuery.symbolName = currentRelationshipSymbolName;
        hierarchyQuery.fileName = currentRelationshipFileName;
        hierarchyQuery.moduleName = currentRelationshipModuleName;
        hierarchyQuery.maxDepth = relationshipDepthCombo
            ? relationshipDepthCombo->currentData().toInt()
            : 2;
        hierarchyQuery.types = query.types.isEmpty()
            ? HierarchyService::allRelationshipTypes()
            : query.types;
        const int directionFilter = relationshipDirectionCombo
            ? relationshipDirectionCombo->currentData().toInt()
            : 0;
        if (directionFilter == 1)
            hierarchyQuery.direction = HierarchyQuery::Children;
        else if (directionFilter == 2)
            hierarchyQuery.direction = HierarchyQuery::Parents;
        else
            hierarchyQuery.direction = HierarchyQuery::Both;

        const HierarchyReport report =
            HierarchyService::getInstance()->getHierarchyReport(hierarchyQuery);

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

    const int directionFilter = relationshipDirectionCombo
        ? relationshipDirectionCombo->currentData().toInt()
        : 0;

    RelationshipBrowseQuery browseQuery;
    browseQuery.symbolName = query.symbolName;
    browseQuery.fileName = query.fileName;
    browseQuery.moduleName = query.moduleName;
    browseQuery.types = query.types;
    browseQuery.includeOutgoing = directionFilter == 0 || directionFilter == 1;
    browseQuery.includeIncoming = directionFilter == 0 || directionFilter == 2;

    const RelationshipReport report =
        RelationshipService::getInstance()->findRelationshipReport(browseQuery);
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
