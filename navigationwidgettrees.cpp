#include "navigationwidget.h"

#include <QDir>
#include <QFileInfo>

void NavigationWidget::populateFileTree()
{
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

    if (fileTreeWidget->topLevelItemCount() == 1) {
        fileTreeWidget->topLevelItem(0)->setExpanded(true);
    }

    expandCurrentFileNodes();
}

void NavigationWidget::populateModuleTree()
{
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
        rootItem->setIcon(0, rootIsFile ? getFileIcon(group.rootName) : getSymbolIcon(sym_list::sym_module));
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
        if (group.symbols.isEmpty()) continue;

        QTreeWidgetItem* typeItem = new QTreeWidgetItem(symbolTreeWidget);
        typeItem->setText(0, QString("%1 (%2)").arg(getSymbolTypeDisplayName(group.symbolType)).arg(group.symbols.size()));
        typeItem->setIcon(0, getSymbolIcon(group.symbolType));
        typeItem->setExpanded(true);
        typeItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

        int addedCount = 0;
        for (const sym_list::SymbolInfo& symbol : group.symbols) {
            if (!currentSearchFilter.isEmpty()
                && !symbol.symbolName.contains(currentSearchFilter, Qt::CaseInsensitive)) {
                continue;
            }

            QTreeWidgetItem* symbolItem = createSymbolItem(symbol);
            typeItem->addChild(symbolItem);
            addedCount++;
        }

        typeItem->setText(0, QString("%1 (%2)").arg(getSymbolTypeDisplayName(group.symbolType)).arg(addedCount));

        if (typeItem->childCount() == 0)
            delete typeItem;
    }
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

    return item;
}

QTreeWidgetItem* NavigationWidget::createModuleItem(const QString& moduleName, const QString& fileName)
{
    QTreeWidgetItem* item = new QTreeWidgetItem();

    item->setText(0, moduleName);
    item->setIcon(0, getSymbolIcon(sym_list::sym_module));
    item->setData(0, Qt::UserRole, fileName);
    item->setData(0, Qt::UserRole + 1, false);
    item->setToolTip(0, QString("Module: %1\nFile: %2").arg(moduleName, QFileInfo(fileName).fileName()));

    return item;
}

QTreeWidgetItem* NavigationWidget::createSymbolItem(const sym_list::SymbolInfo& symbol)
{
    QTreeWidgetItem* item = new QTreeWidgetItem();
    const int payloadId = nextSymbolItemPayloadId++;
    symbolItemPayloads.insert(payloadId, symbol);

    item->setText(0, symbol.symbolName);
    item->setIcon(0, getSymbolIcon(symbol.symbolType));
    item->setData(0, Qt::UserRole, static_cast<int>(symbol.symbolType));
    item->setData(0, Qt::UserRole + 1, payloadId);
    item->setToolTip(0, QString("%1: %2").arg(getSymbolTypeDisplayName(symbol.symbolType), symbol.symbolName));

    return item;
}

void NavigationWidget::expandCurrentFileNodes()
{
    if (currentHighlightedFile.isEmpty()) return;

    for (int i = 0; i < fileTreeWidget->topLevelItemCount(); ++i) {
        QTreeWidgetItem* dirItem = fileTreeWidget->topLevelItem(i);
        for (int j = 0; j < dirItem->childCount(); ++j) {
            QTreeWidgetItem* fileItem = dirItem->child(j);
            QString filePath = fileItem->data(0, Qt::UserRole).toString();
            if (filePath == currentHighlightedFile) {
                dirItem->setExpanded(true);
                fileTreeWidget->setCurrentItem(fileItem);
                return;
            }
        }
    }
}

QTreeWidgetItem* NavigationWidget::findItemByText(QTreeWidget* tree, const QString& text, int column)
{
    QList<QTreeWidgetItem*> items = tree->findItems(text, Qt::MatchRecursive | Qt::MatchExactly, column);
    return items.isEmpty() ? nullptr : items.first();
}
