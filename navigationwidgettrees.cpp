#include "navigationwidget.h"

#include <QBrush>
#include <QDir>
#include <QFileInfo>
#include <QPalette>

namespace {
class TreePopulationGuard
{
public:
    explicit TreePopulationGuard(QTreeWidget* tree)
        : treeWidget(tree),
          sortingEnabled(tree ? tree->isSortingEnabled() : false)
    {
        if (!treeWidget)
            return;
        treeWidget->setUpdatesEnabled(false);
        treeWidget->setSortingEnabled(false);
    }

    ~TreePopulationGuard()
    {
        if (!treeWidget)
            return;
        treeWidget->setSortingEnabled(sortingEnabled);
        treeWidget->setUpdatesEnabled(true);
    }

private:
    QTreeWidget* treeWidget = nullptr;
    bool sortingEnabled = false;
};

QString symbolRowDisplayName(const SymbolOutlineSymbolRow& row)
{
    if (!row.displayName.isEmpty())
        return row.displayName;
    if (row.symbolRecord.isValid() && !row.symbolRecord.name.isEmpty())
        return row.symbolRecord.name;
    return QStringLiteral("<unnamed symbol>");
}

QString symbolRowDetailDisplayName(const SymbolOutlineSymbolRow& row)
{
    if (!row.detailDisplayName.isEmpty())
        return row.detailDisplayName;
    if (row.symbolRecord.isValid()) {
        if (!row.symbolRecord.type.rawTypeText.isEmpty())
            return row.symbolRecord.type.rawTypeText;
        if (!row.symbolRecord.owner.name.isEmpty())
            return row.symbolRecord.owner.name;
        if (!row.symbolRecord.location.fileName.isEmpty())
            return row.symbolRecord.location.fileName;
    }
    return symbolRowDisplayName(row);
}

bool symbolRowMatchesFilter(
    const SymbolOutlineSymbolRow& row,
    const QString& filter)
{
    if (filter.isEmpty())
        return true;

    return symbolRowDisplayName(row).contains(filter, Qt::CaseInsensitive)
        || row.typeDisplayName.contains(filter, Qt::CaseInsensitive)
        || symbolRowDetailDisplayName(row).contains(filter, Qt::CaseInsensitive);
}

QString normalizedNavigationFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}
}

void NavigationWidget::populateFileTree()
{
    TreePopulationGuard guard(fileTreeWidget);
    fileTreeWidget->clear();

    if (currentFileList.isEmpty()) {
        QTreeWidgetItem* emptyItem = new QTreeWidgetItem(fileTreeWidget);
        emptyItem->setText(0, "No SystemVerilog files found");
        emptyItem->setFlags(Qt::ItemIsEnabled);
        return;
    }

    QHash<QString, QTreeWidgetItem*> dirItems;

    for (const QString& filePath : std::as_const(currentFileList)) {
        QFileInfo fileInfo(filePath);
        QString dirPath = fileInfo.absolutePath();
        QString fileName = fileInfo.fileName();

        if (!currentSearchFilter.isEmpty() &&
            !fileName.contains(currentSearchFilter, Qt::CaseInsensitive)) {
            continue;
        }

        QTreeWidgetItem* dirItem = nullptr;
        if (dirItems.contains(dirPath)) {
            dirItem = dirItems[dirPath];
        } else {
            dirItem = new QTreeWidgetItem(fileTreeWidget);
            dirItem->setText(0, QDir(dirPath).dirName());
            dirItem->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
            dirItem->setExpanded(true);
            dirItems[dirPath] = dirItem;
        }

        QTreeWidgetItem* fileItem = createFileItem(filePath);
        dirItem->addChild(fileItem);
    }

    if (!designParticipatingFiles.isEmpty()) {
        for (int i = 0; i < fileTreeWidget->topLevelItemCount(); ++i) {
            QTreeWidgetItem* dirItem = fileTreeWidget->topLevelItem(i);
            bool hasParticipatingChild = false;
            for (int j = 0; j < dirItem->childCount(); ++j) {
                const QString filePath = dirItem->child(j)->data(0, Qt::UserRole).toString();
                if (fileParticipatesInDesign(filePath)) {
                    hasParticipatingChild = true;
                    break;
                }
            }
            applyDesignFileDimming(dirItem, !hasParticipatingChild);
        }
    }

    if (fileTreeWidget->topLevelItemCount() == 1) {
        fileTreeWidget->topLevelItem(0)->setExpanded(true);
    }

    expandCurrentFileNodes();
}

void NavigationWidget::populateModuleTree()
{
    TreePopulationGuard guard(moduleTreeWidget);
    moduleTreeWidget->clear();

    if (currentModuleHierarchy.isEmpty()) {
        QTreeWidgetItem* emptyItem = new QTreeWidgetItem(moduleTreeWidget);
        emptyItem->setText(0, "No modules found");
        emptyItem->setFlags(Qt::ItemIsEnabled);
        return;
    }

    for (const ModuleHierarchyGroup& group : std::as_const(currentModuleHierarchy)) {
        if (group.childModules.isEmpty())
            continue;

        QTreeWidgetItem* rootItem = new QTreeWidgetItem(moduleTreeWidget);
        const bool rootIsFile = group.rootKind == ModuleHierarchyRootKind::FileGroup;
        rootItem->setText(0, group.rootDisplayName);
        rootItem->setIcon(0, rootIsFile
                                  ? getFileIcon(group.rootName)
                                  : getSymbolIcon(SymbolOutlineIconKind::Module));
        rootItem->setData(0, Qt::UserRole, group.rootName);
        rootItem->setData(0, Qt::UserRole + 1, rootIsFile);
        rootItem->setToolTip(0, group.rootToolTip);
        rootItem->setExpanded(true);

        for (const QString& moduleName : group.childModules) {
            if (!currentSearchFilter.isEmpty()
                && !moduleName.contains(currentSearchFilter, Qt::CaseInsensitive)) {
                continue;
            }

            QTreeWidgetItem* moduleItem = createModuleItem(moduleName, group.rootName);
            rootItem->addChild(moduleItem);
        }

        if (rootItem->childCount() == 0)
            delete rootItem;
    }
}

void NavigationWidget::populateSymbolTree()
{
    TreePopulationGuard guard(symbolTreeWidget);
    symbolTreeWidget->clear();
    symbolItemPayloads.clear();
    nextSymbolItemPayloadId = 1;

    if (currentSymbolHierarchy.isEmpty()) {
        QTreeWidgetItem* emptyItem = new QTreeWidgetItem(symbolTreeWidget);
        emptyItem->setText(0, "No symbols found");
        emptyItem->setFlags(Qt::ItemIsEnabled);
        return;
    }

    for (const SymbolOutlineGroup& group : std::as_const(currentSymbolHierarchy)) {
        if (group.symbolRows.isEmpty()) continue;

        const QString groupDisplayName = group.displayName.isEmpty()
            ? QStringLiteral("Symbols")
            : group.displayName;

        QTreeWidgetItem* typeItem = new QTreeWidgetItem(symbolTreeWidget);
        typeItem->setText(0, QString("%1 (%2)").arg(groupDisplayName).arg(group.symbolRows.size()));
        typeItem->setIcon(0, getSymbolIcon(group.iconKind));
        typeItem->setExpanded(true);
        typeItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

        int addedCount = 0;
        for (const SymbolOutlineSymbolRow& row : group.symbolRows) {
            if (!symbolRowMatchesFilter(row, currentSearchFilter))
                continue;

            QTreeWidgetItem* symbolItem = createSymbolItem(row);
            typeItem->addChild(symbolItem);
            addedCount++;
        }

        typeItem->setText(0, QString("%1 (%2)").arg(groupDisplayName).arg(addedCount));

        if (typeItem->childCount() == 0)
            delete typeItem;
    }
}

void NavigationWidget::populateDesignTree()
{
    TreePopulationGuard guard(designTreeWidget);
    designTreeWidget->clear();
    designItemPayloads.clear();
    nextDesignItemPayloadId = 1;

    if (currentDesignHierarchy.topModule.isEmpty()) {
        QTreeWidgetItem* emptyItem = new QTreeWidgetItem(designTreeWidget);
        emptyItem->setText(0, "Top not set. Right-click a module or file and choose Set as Design Top.");
        emptyItem->setFlags(Qt::ItemIsEnabled);
        return;
    }

    if (currentDesignHierarchy.nodes.isEmpty()) {
        QTreeWidgetItem* emptyItem = new QTreeWidgetItem(designTreeWidget);
        emptyItem->setText(0, QStringLiteral("No hierarchy for %1").arg(currentDesignHierarchy.topModule));
        emptyItem->setFlags(Qt::ItemIsEnabled);
        return;
    }

    QHash<QString, QTreeWidgetItem*> itemsById;
    for (const DesignHierarchyNode& node : std::as_const(currentDesignHierarchy.nodes)) {
        QTreeWidgetItem* item = createDesignItem(node);
        itemsById.insert(node.id, item);
        if (!node.parentId.isEmpty() && itemsById.contains(node.parentId)) {
            itemsById.value(node.parentId)->addChild(item);
        } else {
            designTreeWidget->addTopLevelItem(item);
        }
    }
    designTreeWidget->expandAll();
}

void NavigationWidget::applySearchFilter()
{
    switch (getActiveTab()) {
    case FileTab:
        populateFileTree();
        break;
    case ModuleTab:
        populateModuleTree();
        break;
    case SymbolTab:
        populateSymbolTree();
        break;
    case DesignTab:
        populateDesignTree();
        break;
    }
}

QTreeWidgetItem* NavigationWidget::createFileItem(const QString& filePath)
{
    QTreeWidgetItem* item = new QTreeWidgetItem();
    QFileInfo fileInfo(filePath);

    item->setText(0, fileInfo.fileName());
    item->setIcon(0, getFileIcon(filePath));
    item->setData(0, Qt::UserRole, filePath);
    item->setToolTip(0, filePath);
    applyDesignFileDimming(item,
                           !designParticipatingFiles.isEmpty()
                               && !fileParticipatesInDesign(filePath));

    return item;
}

QTreeWidgetItem* NavigationWidget::createModuleItem(const QString& moduleName, const QString& fileName)
{
    QTreeWidgetItem* item = new QTreeWidgetItem();

    item->setText(0, moduleName);
    item->setIcon(0, getSymbolIcon(SymbolOutlineIconKind::Module));
    item->setData(0, Qt::UserRole, fileName);
    item->setData(0, Qt::UserRole + 1, false);
    item->setToolTip(0, QString("Module: %1\nFile: %2").arg(moduleName, QFileInfo(fileName).fileName()));

    return item;
}

QTreeWidgetItem* NavigationWidget::createDesignItem(const DesignHierarchyNode& node)
{
    QTreeWidgetItem* item = new QTreeWidgetItem();
    const int payloadId = nextDesignItemPayloadId++;
    designItemPayloads.insert(payloadId, node);

    QString text = QStringLiteral("%1 : %2")
        .arg(node.instanceName.isEmpty() ? QStringLiteral("<unnamed>") : node.instanceName,
             node.moduleType.isEmpty() ? QStringLiteral("<unknown>") : node.moduleType);
    if (node.unresolved)
        text += QStringLiteral(" (unresolved)");

    item->setText(0, text);
    item->setIcon(0, getSymbolIcon(node.unresolved
                                       ? SymbolOutlineIconKind::Symbol
                                       : SymbolOutlineIconKind::Instance));
    item->setData(0, Qt::UserRole, node.id);
    item->setData(0, Qt::UserRole + 1, payloadId);
    const QString location = node.isTop
        ? QStringLiteral("%1:%2").arg(node.definitionFile).arg(node.definitionLine)
        : QStringLiteral("%1:%2").arg(node.instanceFile).arg(node.instanceLine);
    item->setToolTip(0, node.unresolved
                            ? QStringLiteral("%1\n%2").arg(location, node.unresolvedReason)
                            : location);
    return item;
}

void NavigationWidget::applyDesignFileDimming(QTreeWidgetItem* item, bool dimmed)
{
    if (!item)
        return;
    QColor color = palette().color(QPalette::Text);
    if (dimmed)
        color.setAlphaF(0.40);
    item->setForeground(0, QBrush(color));
}

bool NavigationWidget::fileParticipatesInDesign(const QString& filePath) const
{
    if (designParticipatingFiles.isEmpty())
        return true;
    return designParticipatingFiles.contains(normalizedNavigationFileName(filePath));
}

void NavigationWidget::refreshDesignHeader()
{
    if (!designTopLabel || !designClearButton || !designRefreshButton)
        return;

    const bool hasTop = !currentDesignHierarchy.topModule.isEmpty();
    designTopLabel->setText(hasTop
                                ? QStringLiteral("Design Top: %1")
                                      .arg(currentDesignHierarchy.topModule)
                                : QStringLiteral("Top not set. Right-click a module or file and choose Set as Design Top."));
    designClearButton->setEnabled(hasTop);
    designRefreshButton->setEnabled(hasTop);
}

QTreeWidgetItem* NavigationWidget::createSymbolItem(
    const SymbolOutlineSymbolRow& row)
{
    QTreeWidgetItem* item = new QTreeWidgetItem();
    const int payloadId = nextSymbolItemPayloadId++;
    symbolItemPayloads.insert(payloadId, row);
    const QString displayName = symbolRowDisplayName(row);

    item->setText(0, displayName);
    item->setIcon(0, getSymbolIcon(row.iconKind));
    item->setData(0, Qt::UserRole, static_cast<int>(row.iconKind));
    item->setData(0, Qt::UserRole + 1, payloadId);
    const QString typeDisplayName = row.typeDisplayName.isEmpty()
        ? QStringLiteral("Symbol")
        : row.typeDisplayName;
    const QString detail = symbolRowDetailDisplayName(row);
    item->setToolTip(0, QString("%1: %2\n%3")
                            .arg(typeDisplayName,
                                 displayName,
                                 detail));

    return item;
}

void NavigationWidget::expandCurrentFileNodes()
{
    if (currentHighlightedFile.isEmpty()) return;

    QTreeWidgetItem* fileItem = findFileItemByPath(currentHighlightedFile);
    if (!fileItem)
        return;

    if (QTreeWidgetItem* dirItem = fileItem->parent()) {
        dirItem->setExpanded(true);
    }
    fileTreeWidget->setCurrentItem(fileItem);
}

QTreeWidgetItem* NavigationWidget::findFileItemByPath(const QString& filePath)
{
    for (int i = 0; i < fileTreeWidget->topLevelItemCount(); ++i) {
        QTreeWidgetItem* dirItem = fileTreeWidget->topLevelItem(i);
        for (int j = 0; j < dirItem->childCount(); ++j) {
            QTreeWidgetItem* fileItem = dirItem->child(j);
            if (fileItem->data(0, Qt::UserRole).toString() == filePath)
                return fileItem;
        }
    }

    return nullptr;
}

QTreeWidgetItem* NavigationWidget::findItemByText(QTreeWidget* tree, const QString& text, int column)
{
    QList<QTreeWidgetItem*> items = tree->findItems(text, Qt::MatchRecursive | Qt::MatchExactly, column);
    return items.isEmpty() ? nullptr : items.first();
}
