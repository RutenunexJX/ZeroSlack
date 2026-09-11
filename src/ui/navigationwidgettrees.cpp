#include "navigationwidget.h"
#include "editorfileidentity.h"
#include "insightvisualstyle.h"

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

    QStringList sourceFiles = currentFileList;
    QSet<QString> sourceFileKeys;
    for (const QString& filePath :
         std::as_const(sourceFiles)) {
        sourceFileKeys.insert(
            normalizedNavigationFileName(filePath));
    }
    visibleExplicitDirectories.clear();
    for (auto it = explicitlyTrackedPaths.cbegin();
         it != explicitlyTrackedPaths.cend();
         ++it) {
        if (!QFileInfo::exists(it.key()))
            continue;
        if (it.value()) {
            if (fileSearchFilter.isEmpty()
                || it.key().contains(
                    fileSearchFilter,
                    Qt::CaseInsensitive)) {
                visibleExplicitDirectories.append(it.key());
            }
        } else {
            const QString key =
                normalizedNavigationFileName(it.key());
            if (!sourceFileKeys.contains(key)) {
                sourceFileKeys.insert(key);
                sourceFiles.append(it.key());
            }
        }
    }

    QHash<QString, QString> sourceFilesByIdentity;
    sourceFilesByIdentity.reserve(sourceFiles.size());
    for (const QString& filePath :
         std::as_const(sourceFiles)) {
        const QString key =
            EditorFileIdentity::lookupKey(filePath);
        if (!key.isEmpty()
            && !sourceFilesByIdentity.contains(key)) {
            sourceFilesByIdentity.insert(
                key, filePath);
        }
    }

    QSet<QString> virtuallyGroupedFiles;
    visibleVirtualSourceGroups.clear();
    for (const WorkspaceVirtualSourceGroup& sourceGroup :
         std::as_const(virtualSourceGroups)) {
        WorkspaceVirtualSourceGroup visibleGroup;
        visibleGroup.name = sourceGroup.name;
        QSet<QString> visibleGroupIdentities;
        const bool groupMatches =
            fileSearchFilter.isEmpty()
            || sourceGroup.name.contains(
                fileSearchFilter,
                Qt::CaseInsensitive);
        for (const QString& configuredFile :
             sourceGroup.files) {
            const QString key =
                EditorFileIdentity::lookupKey(
                    configuredFile);
            const QString currentPath =
                sourceFilesByIdentity.value(key);
            if (currentPath.isEmpty())
                continue;
            virtuallyGroupedFiles.insert(key);
            if (visibleGroupIdentities.contains(
                    key)) {
                continue;
            }
            if (!groupMatches
                && !navigationFileMatchesFilter(
                    currentPath,
                    fileSearchFilter)) {
                continue;
            }
            if (hideUnrelatedFiles
                && !designParticipatingFiles.isEmpty()
                && !fileParticipatesInDesign(
                    currentPath)) {
                continue;
            }
            visibleGroupIdentities.insert(key);
            visibleGroup.files.append(
                currentPath);
        }
        if (!visibleGroup.files.isEmpty()
            || (fileSearchFilter.isEmpty()
                && sourceGroup.files.isEmpty())) {
            visibleVirtualSourceGroups.append(
                visibleGroup);
        }
    }

    QStringList visibleFiles;
    visibleFiles.reserve(sourceFiles.size());
    fileTreeRootPath =
        workspaceFileTreeRootPath.isEmpty()
        ? commonNavigationFileTreeRoot(sourceFiles)
        : workspaceFileTreeRootPath;
    for (const QString& filePath : std::as_const(sourceFiles)) {
        if (virtuallyGroupedFiles.contains(
                EditorFileIdentity::lookupKey(
                    filePath))) {
            continue;
        }
        if (!navigationFileMatchesFilter(filePath, fileSearchFilter))
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

    if (files.isEmpty()
        && visibleExplicitDirectories.isEmpty()
        && visibleVirtualSourceGroups.isEmpty()) {
        QTreeWidgetItem* emptyItem = new QTreeWidgetItem(fileTreeWidget);
        emptyItem->setText(0, "No files found");
        emptyItem->setData(
            0, FileTreeKindRole, PlaceholderItem);
        emptyItem->setFlags(Qt::ItemIsEnabled);
        return;
    }

    QHash<QString, QTreeWidgetItem*> dirItems;
    appendVirtualSourceGroupItems();
    for (const QString& directoryPath :
         std::as_const(visibleExplicitDirectories)) {
        appendDirectoryTreeItem(
            directoryPath, &dirItems);
    }
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
        QStringLiteral("Loading %1 files...").arg(files.size()));
    loadingItem->setData(
        0, FileTreeKindRole, PlaceholderItem);
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
        appendVirtualSourceGroupItems();
        for (const QString& directoryPath :
             std::as_const(visibleExplicitDirectories)) {
            appendDirectoryTreeItem(
                directoryPath,
                &pendingFileTreeDirItems);
        }
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

void NavigationWidget::appendVirtualSourceGroupItems()
{
    if (!fileTreeWidget)
        return;
    for (const WorkspaceVirtualSourceGroup& group :
         std::as_const(visibleVirtualSourceGroups)) {
        auto* groupItem =
            new QTreeWidgetItem(fileTreeWidget);
        groupItem->setText(0, group.name);
        groupItem->setIcon(
            0,
            fileTreeWidget->style()->standardIcon(
                QStyle::SP_DirLinkIcon));
        groupItem->setData(
            0,
            FileTreeKindRole,
            VirtualSourceGroupItem);
        groupItem->setFlags(
            Qt::ItemIsEnabled
            | Qt::ItemIsSelectable);
        groupItem->setExpanded(true);
        groupItem->setToolTip(
            0,
            QStringLiteral(
                "Virtual source group; files remain at their original paths."));
        for (const QString& filePath : group.files)
            groupItem->addChild(createFileItem(filePath));
    }
}

void NavigationWidget::appendFileTreeItem(
    const QString& filePath,
    QHash<QString, QTreeWidgetItem*>* dirItems)
{
    if (!fileTreeWidget || !dirItems)
        return;

    const QString absoluteFilePath =
        normalizedNavigationPath(filePath);
    const QString absoluteDirectoryPath =
        navigationDirectoryPathFromFile(
            absoluteFilePath);
    if (fileTreeRootPath.isEmpty()
        || QString::compare(
               absoluteDirectoryPath,
               fileTreeRootPath,
               Qt::CaseInsensitive) == 0) {
        fileTreeWidget->addTopLevelItem(createFileItem(filePath));
        return;
    }

    QTreeWidgetItem* parentItem =
        appendDirectoryTreeItem(
            absoluteDirectoryPath, dirItems);
    if (parentItem)
        parentItem->addChild(createFileItem(filePath));
    else
        fileTreeWidget->addTopLevelItem(createFileItem(filePath));
}

QTreeWidgetItem* NavigationWidget::appendDirectoryTreeItem(
    const QString& directoryPath,
    QHash<QString, QTreeWidgetItem*>* dirItems)
{
    if (!fileTreeWidget || !dirItems)
        return nullptr;
    const QString relativePath =
        relativeNavigationFilePath(
            directoryPath, fileTreeRootPath);
    if (relativePath.isEmpty()
        || relativePath == QStringLiteral(".")) {
        return nullptr;
    }

    QTreeWidgetItem* parentItem = nullptr;
    QString cumulativeDir;
    const QStringList parts =
        relativePath.split(
            QLatin1Char('/'), Qt::SkipEmptyParts);
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
            const QString absoluteDirectory =
                normalizedNavigationPath(
                    QDir(fileTreeRootPath)
                        .absoluteFilePath(
                            cumulativeDir));
            dirItem->setData(
                0, Qt::UserRole, absoluteDirectory);
            dirItem->setData(
                0, FileTreeKindRole, DirectoryItem);
            dirItem->setToolTip(
                0,
                QDir::toNativeSeparators(
                    absoluteDirectory));
            if (parentItem)
                parentItem->addChild(dirItem);
            else
                fileTreeWidget->addTopLevelItem(dirItem);
            dirItems->insert(cumulativeDir, dirItem);
        }
        parentItem = dirItem;
    }
    return parentItem;
}

void NavigationWidget::refreshFileTreeDirectoryDimming()
{
    if (!fileTreeWidget)
        return;
    std::function<bool(QTreeWidgetItem*)> refreshItem =
        [&](QTreeWidgetItem* item) {
            if (!item)
                return false;
            const QString filePath =
                item->data(0, Qt::UserRole).toString();
            const FileTreeItemKind kind =
                static_cast<FileTreeItemKind>(
                    item->data(
                        0, FileTreeKindRole).toInt());
            if (!filePath.isEmpty()
                && kind == FileItem) {
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

    QSet<QString> visibleIds;
    if (!designSearchFilter.isEmpty()) {
        QHash<QString, DesignHierarchyNode> nodesById;
        for (const DesignHierarchyNode& node :
             std::as_const(currentDesignHierarchy.nodes)) {
            nodesById.insert(node.id, node);
        }
        for (const DesignHierarchyNode& node :
             std::as_const(currentDesignHierarchy.nodes)) {
            const bool matches =
                node.instanceName.contains(designSearchFilter,
                                           Qt::CaseInsensitive)
                || node.moduleType.contains(designSearchFilter,
                                            Qt::CaseInsensitive)
                || node.instancePath.contains(designSearchFilter,
                                              Qt::CaseInsensitive);
            if (!matches)
                continue;
            QString id = node.id;
            while (!id.isEmpty() && !visibleIds.contains(id)) {
                visibleIds.insert(id);
                const auto it = nodesById.constFind(id);
                if (it == nodesById.constEnd())
                    break;
                id = it->parentId;
            }
        }
        if (visibleIds.isEmpty()) {
            QTreeWidgetItem* emptyItem =
                new QTreeWidgetItem(designTreeWidget);
            emptyItem->setText(0, QStringLiteral("No design matches"));
            emptyItem->setFlags(Qt::ItemIsEnabled);
            return;
        }
    }

    QHash<QString, QTreeWidgetItem*> itemsById;
    for (const DesignHierarchyNode& node : std::as_const(currentDesignHierarchy.nodes)) {
        if (!visibleIds.isEmpty() && !visibleIds.contains(node.id))
            continue;
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

QTreeWidgetItem* NavigationWidget::createFileItem(const QString& filePath)
{
    QTreeWidgetItem* item = new QTreeWidgetItem();

    item->setText(0, navigationFileNameFromPath(filePath));
    item->setIcon(0, getFileIcon(filePath));
    item->setData(0, FileTreeKindRole, FileItem);
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
    QColor moduleColor = InsightVisualStyle::roleColor(
        InsightVisualRole::Kernel);
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
