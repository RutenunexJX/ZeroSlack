#include "relationshipspanelcoordinator.h"

#include "semanticpanelutils.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QVBoxLayout>

#include <utility>

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
    relationshipsTree->setColumnCount(6);
    relationshipsTree->setHeaderLabels({"Direction",
                                        "Symbol",
                                        "File",
                                        "Line",
                                        "Relationship",
                                        "Explanation"});
    relationshipsTree->setRootIsDecorated(true);
    relationshipsTree->setAlternatingRowColors(true);
    relationshipsTree->setSelectionMode(QAbstractItemView::SingleSelection);
    relationshipsTree->header()->setStretchLastSection(true);
    relationshipsTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    relationshipsTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    relationshipsTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    relationshipsTree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    relationshipsTree->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
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
