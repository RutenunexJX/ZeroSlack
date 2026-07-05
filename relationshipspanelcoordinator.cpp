#include "relationshipspanelcoordinator.h"

#include "relationshipservice.h"
#include "semanticindex.h"
#include "statetransitiontriggerservice.h"
#include "symboltaxonomy.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMenu>
#include <QPoint>
#include <QVBoxLayout>

#include <utility>

namespace {

constexpr int kNavigationFileRole = Qt::UserRole;
constexpr int kNavigationLineRole = Qt::UserRole + 1;
constexpr int kNavigationColumnRole = Qt::UserRole + 2;
constexpr int kGraphSymbolRole = Qt::UserRole + 3;
constexpr int kGraphModuleRole = Qt::UserRole + 4;
constexpr int kRelationshipTypeRole = Qt::UserRole + 5;
constexpr int kModuleBlockFileRole = Qt::UserRole + 6;
constexpr int kModuleBlockNameRole = Qt::UserRole + 7;

QString relationshipSourceDisplayName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QStringLiteral("<none>");
    const QString displayName = QFileInfo(fileName).fileName();
    return displayName.isEmpty() ? fileName : displayName;
}

QString relationshipQueryContextText(const QString& symbolName,
                                     const QString& fileName,
                                     QComboBox* viewCombo,
                                     QComboBox* directionCombo,
                                     QComboBox* typeCombo,
                                     QComboBox* depthCombo)
{
    const QString symbol = symbolName.isEmpty()
        ? QStringLiteral("<none>")
        : symbolName;
    QStringList parts;
    parts.append(QStringLiteral("Symbol: %1").arg(symbol));
    parts.append(QStringLiteral("Source: %1")
                     .arg(relationshipSourceDisplayName(fileName)));
    parts.append(QStringLiteral("View: %1")
                     .arg(viewCombo ? viewCombo->currentText()
                                    : QStringLiteral("Direct")));
    parts.append(QStringLiteral("Direction: %1")
                     .arg(directionCombo ? directionCombo->currentText()
                                         : QStringLiteral("All Directions")));
    parts.append(QStringLiteral("Type: %1")
                     .arg(typeCombo ? typeCombo->currentText()
                                    : QStringLiteral("All Types")));
    if (viewCombo && viewCombo->currentData().toInt() == 1) {
        parts.append(QStringLiteral("Depth: %1")
                         .arg(depthCombo ? depthCombo->currentText()
                                         : QStringLiteral("Depth 1")));
    }
    return parts.join(QStringLiteral(" | "));
}

bool itemHasNavigationTarget(QTreeWidgetItem* item)
{
    return item
        && !item->data(0, kNavigationFileRole).toString().isEmpty();
}

QString itemPath(QTreeWidgetItem* item)
{
    return item ? item->data(0, kNavigationFileRole).toString() : QString();
}

QString itemFileLine(QTreeWidgetItem* item)
{
    const QString fileName = itemPath(item);
    if (fileName.isEmpty())
        return QString();
    const int line = item ? item->data(0, kNavigationLineRole).toInt() : 0;
    if (line <= 0)
        return fileName;
    return QStringLiteral("%1:%2").arg(fileName).arg(line);
}

bool stateTransitionGraphAvailable(QTreeWidgetItem* item)
{
    if (!item)
        return false;
    StateTransitionTriggerQuery query;
    query.symbolName = item->data(0, kGraphSymbolRole).toString();
    query.fileName = item->data(0, kNavigationFileRole).toString();
    query.moduleName = item->data(0, kGraphModuleRole).toString();
    return StateTransitionTriggerService::getInstance()
        ->triggerForSymbol(query)
        .available;
}

bool moduleBlockDiagramAvailable(QTreeWidgetItem* item)
{
    return item
        && !item->data(0, kModuleBlockNameRole).toString().isEmpty()
        && !item->data(0, kModuleBlockFileRole).toString().isEmpty();
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
    for (const RelationshipTypeFilterOption& option :
         RelationshipService::relationshipPanelTypeFilterOptions()) {
        relationshipTypeCombo->addItem(option.displayName, option.value);
    }
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

    relationshipContextLabel = new QLabel(panel);
    relationshipContextLabel->setObjectName(QStringLiteral("relationshipContextLabel"));
    relationshipContextLabel->setText(relationshipQueryContextText(
        currentRelationshipSymbolName,
        currentRelationshipFileName,
        relationshipViewCombo,
        relationshipDirectionCombo,
        relationshipTypeCombo,
        relationshipDepthCombo));
    layout->addWidget(relationshipContextLabel);

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
    relationshipsTree->setContextMenuPolicy(Qt::CustomContextMenu);
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

    auto navigateRelationshipItem = [this](QTreeWidgetItem* item) {
        if (!item)
            return;
        if (!itemHasNavigationTarget(item)) {
            if (statusMessageHandler) {
                statusMessageHandler(QStringLiteral("Relationship row has no source location"),
                                     3000);
            }
            return;
        }
        if (!navigationHandler) {
            if (statusMessageHandler)
                statusMessageHandler(QStringLiteral("Navigation unavailable"), 3000);
            return;
        }

        const QString fileName = item->data(0, kNavigationFileRole).toString();
        const int line = item->data(0, kNavigationLineRole).toInt();
        const int column = item->data(0, kNavigationColumnRole).toInt();
        if (!navigationHandler(fileName, line, column)
            && statusMessageHandler) {
            statusMessageHandler(QStringLiteral("Relationship jump failed"), 4000);
        }
    };
    QObject::connect(relationshipsTree, &QTreeWidget::itemClicked,
                     relationshipsDock, [navigateRelationshipItem](QTreeWidgetItem* item, int) {
                         navigateRelationshipItem(item);
                     });
    QObject::connect(relationshipsTree, &QTreeWidget::itemActivated,
                     relationshipsDock, [navigateRelationshipItem](QTreeWidgetItem* item, int) {
                         navigateRelationshipItem(item);
                     });
    QObject::connect(relationshipsTree, &QTreeWidget::customContextMenuRequested,
                     relationshipsDock, [this](const QPoint& pos) {
                         QTreeWidgetItem* item = relationshipsTree
                             ? relationshipsTree->itemAt(pos)
                             : nullptr;
                         if (!item)
                             return;

                         QMenu menu(relationshipsTree);
                         QAction* copyPath =
                             menu.addAction(QStringLiteral("Copy Path"));
                         QAction* copyFileLine =
                             menu.addAction(QStringLiteral("Copy file:line"));
                         copyPath->setEnabled(itemHasNavigationTarget(item));
                         copyFileLine->setEnabled(itemHasNavigationTarget(item));
                         menu.addSeparator();
                         QAction* signalKernelGraph =
                             menu.addAction(QStringLiteral("Signal Kernel Graph"));
                         QAction* moduleBlockDiagram =
                             menu.addAction(QStringLiteral("Module Block Diagram"));
                         QAction* stateTransitionGraph =
                             menu.addAction(QStringLiteral("State Transition Graph"));
                         const bool hasGraphSymbol =
                             !item->data(0, kGraphSymbolRole).toString().isEmpty()
                             && !item->data(0, kNavigationFileRole).toString().isEmpty();
                         signalKernelGraph->setEnabled(
                             hasGraphSymbol && bool(signalKernelGraphHandler));
                         moduleBlockDiagram->setEnabled(
                             moduleBlockDiagramAvailable(item)
                             && bool(moduleBlockDiagramHandler));
                         stateTransitionGraph->setEnabled(
                             stateTransitionGraphAvailable(item)
                             && bool(stateTransitionGraphHandler));
                         signalKernelGraph->setStatusTip(
                             signalKernelGraph->isEnabled()
                                 ? QString()
                                 : QStringLiteral("Signal Kernel Graph unavailable"));
                         moduleBlockDiagram->setStatusTip(
                             moduleBlockDiagram->isEnabled()
                                 ? QString()
                                 : QStringLiteral("Module Block Diagram requires a module name"));
                         stateTransitionGraph->setStatusTip(
                             stateTransitionGraph->isEnabled()
                                 ? QString()
                                 : QStringLiteral("State Transition Graph requires ns/next_state"));

                         QAction* chosen =
                             menu.exec(relationshipsTree->viewport()->mapToGlobal(pos));
                         if (!chosen)
                             return;

                         if (chosen == copyPath || chosen == copyFileLine) {
                             const QString text = chosen == copyFileLine
                                 ? itemFileLine(item)
                                 : itemPath(item);
                             if (text.isEmpty())
                                 return;
                             QApplication::clipboard()->setText(text);
                             if (statusMessageHandler) {
                                 statusMessageHandler(QStringLiteral("Copied %1").arg(text),
                                                      1500);
                             }
                             return;
                         }

                         const QString symbolName =
                             item->data(0, kGraphSymbolRole).toString();
                         const QString fileName =
                             item->data(0, kNavigationFileRole).toString();
                         const QString moduleName =
                             item->data(0, kGraphModuleRole).toString();
                         if (chosen == signalKernelGraph && signalKernelGraphHandler) {
                             signalKernelGraphHandler(symbolName, fileName, moduleName);
                         } else if (chosen == stateTransitionGraph
                                    && stateTransitionGraphHandler) {
                             stateTransitionGraphHandler(symbolName, fileName, moduleName);
                         } else if (chosen == moduleBlockDiagram
                                    && moduleBlockDiagramHandler) {
                             moduleBlockDiagramHandler(
                                 item->data(0, kModuleBlockFileRole).toString(),
                                 item->data(0, kModuleBlockNameRole).toString());
                         }
                     });
}

void RelationshipsPanelCoordinator::setNavigationHandler(
    std::function<bool(const QString&, int, int)> handler)
{
    navigationHandler = std::move(handler);
}

void RelationshipsPanelCoordinator::setStatusMessageHandler(
    std::function<void(const QString&, int)> handler)
{
    statusMessageHandler = std::move(handler);
}

void RelationshipsPanelCoordinator::setSignalKernelGraphHandler(
    std::function<void(const QString&, const QString&, const QString&)> handler)
{
    signalKernelGraphHandler = std::move(handler);
}

void RelationshipsPanelCoordinator::setStateTransitionGraphHandler(
    std::function<void(const QString&, const QString&, const QString&)> handler)
{
    stateTransitionGraphHandler = std::move(handler);
}

void RelationshipsPanelCoordinator::setModuleBlockDiagramHandler(
    std::function<void(const QString&, const QString&)> handler)
{
    moduleBlockDiagramHandler = std::move(handler);
}

void RelationshipsPanelCoordinator::showRelationshipsForSymbol(const QString& symbolName,
                                                               const QString& fileName,
                                                               const QString& moduleName)
{
    if (!relationshipsTree)
        return;

    currentRelationshipSymbolName = symbolName;
    currentRelationshipFileName = fileName;
    currentRelationshipModuleName = moduleName;
    refresh();
}
