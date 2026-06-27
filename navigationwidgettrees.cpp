#include "navigationwidget.h"

#include <QBrush>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QPalette>
#include <QStyle>

#include <functional>

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

constexpr int kSynchronousFileTreeLimit = 160;
constexpr int kFileTreeChunkSize = 48;
constexpr qint64 kFileTreeChunkBudgetMs = 8;

QString navigationFileNameFromPath(const QString& filePath)
{
    const QString normalized = QDir::fromNativeSeparators(filePath);
    const int slash = normalized.lastIndexOf(QLatin1Char('/'));
    return slash >= 0 ? normalized.mid(slash + 1) : normalized;
}

QString navigationDirectoryPathFromFile(const QString& filePath)
{
    const QString normalized = QDir::fromNativeSeparators(filePath);
    const int slash = normalized.lastIndexOf(QLatin1Char('/'));
    return slash > 0 ? normalized.left(slash) : QString();
}

QString navigationDirectoryDisplayName(const QString& dirPath)
{
    if (dirPath.isEmpty())
        return QStringLiteral(".");
    const int slash = dirPath.lastIndexOf(QLatin1Char('/'));
    const QString name = slash >= 0 ? dirPath.mid(slash + 1) : dirPath;
    return name.isEmpty() ? dirPath : name;
}

QString normalizedNavigationPath(const QString& path)
{
    if (path.isEmpty())
        return QString();
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()));
}

QString commonNavigationFileTreeRoot(const QStringList& files)
{
    QStringList rootParts;
    bool initialized = false;
    for (const QString& filePath : files) {
        const QString absolute = normalizedNavigationPath(filePath);
        if (absolute.isEmpty())
            continue;
        const QString dir = navigationDirectoryPathFromFile(absolute);
        const QStringList parts = dir.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        if (!initialized) {
            rootParts = parts;
            initialized = true;
            continue;
        }

        int common = 0;
        while (common < rootParts.size()
               && common < parts.size()
               && QString::compare(rootParts.at(common),
                                   parts.at(common),
                                   Qt::CaseInsensitive) == 0) {
            ++common;
        }
        rootParts = rootParts.mid(0, common);
    }
    return rootParts.join(QLatin1Char('/'));
}

QString relativeNavigationFilePath(const QString& filePath,
                                   const QString& rootPath)
{
    const QString absolute = normalizedNavigationPath(filePath);
    if (absolute.isEmpty() || rootPath.isEmpty())
        return absolute;

    if (QString::compare(absolute, rootPath, Qt::CaseInsensitive) == 0)
        return navigationFileNameFromPath(absolute);

    const QString prefix = rootPath + QLatin1Char('/');
    if (absolute.startsWith(prefix, Qt::CaseInsensitive))
        return absolute.mid(prefix.size());
    return absolute;
}

bool navigationFileMatchesFilter(const QString& filePath, const QString& filter)
{
    return filter.isEmpty()
        || navigationFileNameFromPath(filePath).contains(filter, Qt::CaseInsensitive);
}
}

void NavigationWidget::populateFileTree()
{
    if (!fileTreeWidget)
        return;
    cancelFileTreePopulation();

    QStringList visibleFiles;
    visibleFiles.reserve(currentFileList.size());
    fileTreeRootPath = commonNavigationFileTreeRoot(currentFileList);
    for (const QString& filePath : std::as_const(currentFileList)) {
        if (!navigationFileMatchesFilter(filePath, currentSearchFilter))
            continue;
        if (hideUnrelatedFiles
            && !designParticipatingFiles.isEmpty()
            && !fileParticipatesInDesign(filePath)) {
            continue;
        }
        visibleFiles.append(filePath);
    }

    if (visibleFiles.size() > kSynchronousFileTreeLimit) {
        startAsyncFileTreePopulation(visibleFiles);
        return;
    }

    populateFileTreeSynchronously(visibleFiles);
}

void NavigationWidget::populateFileTreeSynchronously(const QStringList& files)
{
    if (!fileTreeWidget)
        return;
    TreePopulationGuard guard(fileTreeWidget);
    fileTreeWidget->clear();
    fileItemsByNormalizedPath.clear();

    if (files.isEmpty()) {
        QTreeWidgetItem* emptyItem = new QTreeWidgetItem(fileTreeWidget);
        emptyItem->setText(0, "No SystemVerilog files found");
        emptyItem->setFlags(Qt::ItemIsEnabled);
        return;
    }

    QHash<QString, QTreeWidgetItem*> dirItems;
    for (const QString& filePath : files)
        appendFileTreeItem(filePath, &dirItems);

    refreshFileTreeDirectoryDimming();

    if (fileTreeWidget->topLevelItemCount() == 1) {
        fileTreeWidget->topLevelItem(0)->setExpanded(true);
    }

    expandCurrentFileNodes();
}

void NavigationWidget::startAsyncFileTreePopulation(const QStringList& files)
{
    if (!fileTreeWidget)
        return;

    pendingFileTreeFiles = files;
    pendingFileTreeDirItems.clear();
    fileItemsByNormalizedPath.clear();
    pendingFileTreeIndex = 0;
    pendingFileTreeClearPlaceholder = true;

    TreePopulationGuard guard(fileTreeWidget);
    fileTreeWidget->clear();
    QTreeWidgetItem* loadingItem = new QTreeWidgetItem(fileTreeWidget);
    loadingItem->setText(
        0,
        QStringLiteral("Loading %1 SystemVerilog files...").arg(files.size()));
    loadingItem->setFlags(Qt::ItemIsEnabled);

    if (fileTreePopulationTimer)
        fileTreePopulationTimer->start(0);
}

void NavigationWidget::cancelFileTreePopulation()
{
    if (fileTreePopulationTimer)
        fileTreePopulationTimer->stop();
    pendingFileTreeFiles.clear();
    pendingFileTreeDirItems.clear();
    pendingFileTreeIndex = 0;
    pendingFileTreeClearPlaceholder = false;
}

void NavigationWidget::processFileTreePopulationChunk()
{
    if (!fileTreeWidget)
        return;
    if (pendingFileTreeIndex >= pendingFileTreeFiles.size()) {
        cancelFileTreePopulation();
        refreshFileTreeDirectoryDimming();
        expandCurrentFileNodes();
        return;
    }

    QElapsedTimer chunkTimer;
    chunkTimer.start();
    int filesThisChunk = 0;

    fileTreeWidget->setUpdatesEnabled(false);
    if (pendingFileTreeClearPlaceholder) {
        fileTreeWidget->clear();
        pendingFileTreeClearPlaceholder = false;
    }

    while (pendingFileTreeIndex < pendingFileTreeFiles.size()) {
        appendFileTreeItem(pendingFileTreeFiles.at(pendingFileTreeIndex),
                           &pendingFileTreeDirItems);
        ++pendingFileTreeIndex;
        ++filesThisChunk;
        if (filesThisChunk >= kFileTreeChunkSize
            || chunkTimer.elapsed() >= kFileTreeChunkBudgetMs) {
            break;
        }
    }
    fileTreeWidget->setUpdatesEnabled(true);

    if (pendingFileTreeIndex >= pendingFileTreeFiles.size()) {
        cancelFileTreePopulation();
        refreshFileTreeDirectoryDimming();
        if (fileTreeWidget->topLevelItemCount() == 1)
            fileTreeWidget->topLevelItem(0)->setExpanded(true);
        expandCurrentFileNodes();
    }
}

void NavigationWidget::appendFileTreeItem(
    const QString& filePath,
    QHash<QString, QTreeWidgetItem*>* dirItems)
{
    if (!fileTreeWidget || !dirItems)
        return;

    const QString relativePath =
        relativeNavigationFilePath(filePath, fileTreeRootPath);
    const QString dirPath = navigationDirectoryPathFromFile(relativePath);
    if (dirPath.isEmpty()) {
        fileTreeWidget->addTopLevelItem(createFileItem(filePath));
        return;
    }

    QTreeWidgetItem* parentItem = nullptr;
    QString cumulativeDir;
    const QStringList parts = dirPath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        cumulativeDir = cumulativeDir.isEmpty()
            ? part
            : cumulativeDir + QLatin1Char('/') + part;
        QTreeWidgetItem* dirItem = dirItems->value(cumulativeDir, nullptr);
        if (!dirItem) {
            dirItem = new QTreeWidgetItem();
            dirItem->setText(0, navigationDirectoryDisplayName(cumulativeDir));
            dirItem->setIcon(0, fileTreeWidget->style()->standardIcon(QStyle::SP_DirIcon));
            dirItem->setExpanded(true);
            if (parentItem)
                parentItem->addChild(dirItem);
            else
                fileTreeWidget->addTopLevelItem(dirItem);
            dirItems->insert(cumulativeDir, dirItem);
        }
        parentItem = dirItem;
    }

    if (parentItem)
        parentItem->addChild(createFileItem(filePath));
}

void NavigationWidget::refreshFileTreeDirectoryDimming()
{
    if (!fileTreeWidget)
        return;
    std::function<bool(QTreeWidgetItem*)> refreshItem =
        [&](QTreeWidgetItem* item) {
            if (!item)
                return false;
            const QString filePath = item->data(0, Qt::UserRole).toString();
            if (!filePath.isEmpty()) {
                const bool participates = fileParticipatesInDesign(filePath);
                applyDesignFileDimming(
                    item,
                    !designParticipatingFiles.isEmpty() && !participates);
                return participates;
            }

            bool hasParticipatingChild = false;
            for (int j = 0; j < item->childCount(); ++j)
                hasParticipatingChild = refreshItem(item->child(j))
                    || hasParticipatingChild;
            applyDesignFileDimming(
                item,
                !designParticipatingFiles.isEmpty() && !hasParticipatingChild);
            return hasParticipatingChild;
        };

    for (int i = 0; i < fileTreeWidget->topLevelItemCount(); ++i)
        refreshItem(fileTreeWidget->topLevelItem(i));
}

void NavigationWidget::populateModuleTree()
{
    if (!moduleTreeWidget)
        return;
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
    if (!symbolTreeWidget)
        return;
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
    if (!designTreeWidget)
        return;
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
    for (int i = 0; i < designTreeWidget->topLevelItemCount(); ++i) {
        QTreeWidgetItem* root = designTreeWidget->topLevelItem(i);
        if (!root)
            continue;
        root->setExpanded(true);
        for (int child = 0; child < root->childCount(); ++child)
            root->child(child)->setExpanded(false);
    }
}

void NavigationWidget::applySearchFilter()
{
    switch (getActiveTab()) {
    case FileTab:
        populateFileTree();
        break;
    case DesignTab:
        populateDesignTree();
        break;
    }
}

QTreeWidgetItem* NavigationWidget::createFileItem(const QString& filePath)
{
    QTreeWidgetItem* item = new QTreeWidgetItem();

    item->setText(0, navigationFileNameFromPath(filePath));
    item->setIcon(0, getFileIcon(filePath));
    item->setData(0, Qt::UserRole, filePath);
    item->setToolTip(0, filePath);
    applyDesignFileDimming(item,
                           !designParticipatingFiles.isEmpty()
                               && !fileParticipatesInDesign(filePath));
    const QString normalized = normalizedFileItemPath(filePath);
    if (!normalized.isEmpty())
        fileItemsByNormalizedPath.insert(normalized, item);

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

    const QString instanceName =
        node.instanceName.isEmpty() ? QStringLiteral("<unnamed>") : node.instanceName;
    QString moduleType =
        node.moduleType.isEmpty() ? QStringLiteral("<unknown>") : node.moduleType;
    if (node.unresolved)
        moduleType += QStringLiteral(" (unresolved)");

    item->setText(0, instanceName);
    item->setText(1, moduleType);
    item->setIcon(0, getSymbolIcon(node.isTop
                                       ? SymbolOutlineIconKind::Module
                                       : (node.unresolved
                                              ? SymbolOutlineIconKind::Symbol
                                              : SymbolOutlineIconKind::Instance)));
    item->setData(0, Qt::UserRole, node.id);
    item->setData(0, Qt::UserRole + 1, payloadId);
    const QString location = node.isTop
        ? QStringLiteral("%1:%2").arg(node.definitionFile).arg(node.definitionLine)
        : QStringLiteral("instance %1:%2\nmodule %3:%4")
              .arg(node.instanceFile)
              .arg(node.instanceLine)
              .arg(node.definitionFile)
              .arg(node.definitionLine);
    item->setToolTip(0, node.unresolved
                            ? QStringLiteral("%1\n%2").arg(location, node.unresolvedReason)
                            : location);
    item->setToolTip(1, item->toolTip(0));
    applyDesignItemDimming(item, !node.inSelectedTop);
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

void NavigationWidget::applyDesignItemDimming(QTreeWidgetItem* item, bool dimmed)
{
    if (!item)
        return;
    QColor instanceColor = palette().color(QPalette::Text);
    QColor moduleColor(37, 99, 111);
    if (dimmed) {
        instanceColor.setAlphaF(0.35);
        moduleColor.setAlphaF(0.35);
    }
    item->setForeground(0, QBrush(instanceColor));
    item->setForeground(1, QBrush(moduleColor));
}

bool NavigationWidget::fileParticipatesInDesign(const QString& filePath) const
{
    if (designParticipatingFiles.isEmpty())
        return true;
    return designParticipatingFiles.contains(normalizedNavigationFileName(filePath));
}

QString NavigationWidget::normalizedFileItemPath(const QString& filePath) const
{
    return normalizedNavigationFileName(filePath);
}

void NavigationWidget::refreshDesignHeader()
{
    if (!designTopLabel || !designClearButton || !designRefreshButton)
        return;

    const bool hasTop = !currentDesignHierarchy.topModule.isEmpty();
    if (!hasTop) {
        designTopLabel->setText(
            QStringLiteral("Design top will be inferred after module analysis."));
    } else if (!currentDesignHierarchy.selectedTopModule.isEmpty()) {
        designTopLabel->setText(
            QStringLiteral("Selected Design Top: %1 (%2 roots)")
                .arg(currentDesignHierarchy.selectedTopModule)
                .arg(currentDesignHierarchy.rootModules.size()));
    } else if (currentDesignHierarchy.rootModules.size() > 1) {
        designTopLabel->setText(
            QStringLiteral("Auto Design Tops: %1 roots; largest %2")
                .arg(currentDesignHierarchy.rootModules.size())
                .arg(currentDesignHierarchy.topModule));
    } else {
        designTopLabel->setText(
            QStringLiteral("Auto Design Top: %1")
                .arg(currentDesignHierarchy.topModule));
    }
    designClearButton->setEnabled(!currentDesignHierarchy.selectedTopModule.isEmpty());
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
    if (!fileTreeWidget)
        return;

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
    if (!fileTreeWidget)
        return nullptr;
    return fileItemsByNormalizedPath.value(normalizedFileItemPath(filePath),
                                           nullptr);
}

QTreeWidgetItem* NavigationWidget::findItemByText(QTreeWidget* tree, const QString& text, int column)
{
    if (!tree)
        return nullptr;
    QList<QTreeWidgetItem*> items = tree->findItems(text, Qt::MatchRecursive | Qt::MatchExactly, column);
    return items.isEmpty() ? nullptr : items.first();
}
