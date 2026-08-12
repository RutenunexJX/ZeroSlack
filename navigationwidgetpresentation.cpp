#include "navigationwidget.h"

#include "insightvisualstyle.h"

#include <QPainter>
#include <QPixmap>
#include <QStyle>
#include <QTreeWidgetItem>

#include "symboltaxonomy.h"

namespace {
QIcon makeInstanceIcon()
{
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const InsightTheme& theme = InsightVisualStyle::theme();
    QPen linePen(theme.graph.edge, 1.4);
    painter.setPen(linePen);
    painter.drawLine(QPointF(5.0, 5.0), QPointF(11.0, 11.0));
    painter.drawLine(QPointF(5.0, 11.0), QPointF(11.0, 5.0));

    painter.setPen(QPen(theme.graph.nodeBorder, 1.0));
    painter.setBrush(theme.graph.nodeFill);
    painter.drawRoundedRect(QRectF(2.0, 2.0, 5.0, 5.0), 1.2, 1.2);
    painter.drawRoundedRect(QRectF(9.0, 2.0, 5.0, 5.0), 1.2, 1.2);
    painter.drawRoundedRect(QRectF(5.5, 9.0, 5.0, 5.0), 1.2, 1.2);

    return QIcon(pixmap);
}
}

QIcon NavigationWidget::getFileIcon(const QString& filePath)
{
    const SymbolTaxonomy::SourceRole role =
        SymbolTaxonomy::sourceRoleForFileName(filePath);
    const int cacheKey = static_cast<int>(role);
    const auto cached = fileIconCache.constFind(cacheKey);
    if (cached != fileIconCache.constEnd())
        return cached.value();

    QIcon icon;
    if (role == SymbolTaxonomy::SourceRole::DesignSource) {
        icon = style()->standardIcon(QStyle::SP_FileIcon);
    } else if (SymbolTaxonomy::isHeaderSourceRole(role)) {
        icon = style()->standardIcon(QStyle::SP_FileDialogDetailedView);
    } else {
        icon = style()->standardIcon(QStyle::SP_FileIcon);
    }

    fileIconCache.insert(cacheKey, icon);
    return icon;
}

QIcon NavigationWidget::getSymbolIcon(SymbolOutlineIconKind iconKind)
{
    const int cacheKey = static_cast<int>(iconKind);
    const auto cached = symbolIconCache.constFind(cacheKey);
    if (cached != symbolIconCache.constEnd())
        return cached.value();

    QIcon icon;
    switch (iconKind) {
    case SymbolOutlineIconKind::Module:
        icon = style()->standardIcon(QStyle::SP_ComputerIcon);
        break;
    case SymbolOutlineIconKind::Signal:
        icon = style()->standardIcon(QStyle::SP_DialogApplyButton);
        break;
    case SymbolOutlineIconKind::Subroutine:
        icon = style()->standardIcon(QStyle::SP_MediaPlay);
        break;
    case SymbolOutlineIconKind::Parameter:
        icon = style()->standardIcon(QStyle::SP_FileDialogDetailedView);
        break;
    case SymbolOutlineIconKind::Port:
        icon = style()->standardIcon(QStyle::SP_ArrowRight);
        break;
    case SymbolOutlineIconKind::Instance:
        icon = makeInstanceIcon();
        break;
    case SymbolOutlineIconKind::Type:
        icon = style()->standardIcon(QStyle::SP_FileIcon);
        break;
    case SymbolOutlineIconKind::Symbol:
    default:
        icon = style()->standardIcon(QStyle::SP_FileIcon);
        break;
    }
    symbolIconCache.insert(cacheKey, icon);
    return icon;
}

void NavigationWidget::refreshThemePresentation()
{
    fileIconCache.clear();
    symbolIconCache.clear();
    refreshFileTreeIcons();
    refreshDesignTreeIcons();
    refreshFileTreeDirectoryDimming();
    if (fileTreeWidget && fileTreeWidget->viewport())
        fileTreeWidget->viewport()->update();
    if (designTreeWidget && designTreeWidget->viewport())
        designTreeWidget->viewport()->update();
}

QIcon NavigationWidget::symbolIconForTest(
    SymbolOutlineIconKind iconKind)
{
    return getSymbolIcon(iconKind);
}

int NavigationWidget::iconCacheEntryCountForTest() const
{
    return fileIconCache.size() + symbolIconCache.size();
}

void NavigationWidget::refreshFileTreeIcons()
{
    if (!fileTreeWidget)
        return;
    const auto refreshItem = [this](QTreeWidgetItem* item,
                                    const auto& self) -> void {
        if (!item)
            return;
        const FileTreeItemKind kind =
            static_cast<FileTreeItemKind>(
                item->data(0, FileTreeKindRole).toInt());
        if (kind == FileItem) {
            item->setIcon(
                0,
                getFileIcon(
                    item->data(0, Qt::UserRole).toString()));
        } else if (kind == DirectoryItem) {
            item->setIcon(
                0,
                fileTreeWidget->style()->standardIcon(
                    QStyle::SP_DirIcon));
        } else if (kind == VirtualSourceGroupItem) {
            item->setIcon(
                0,
                fileTreeWidget->style()->standardIcon(
                    QStyle::SP_DirLinkIcon));
        }
        for (int index = 0; index < item->childCount(); ++index)
            self(item->child(index), self);
    };
    for (int index = 0;
         index < fileTreeWidget->topLevelItemCount();
         ++index) {
        refreshItem(fileTreeWidget->topLevelItem(index), refreshItem);
    }
}

void NavigationWidget::refreshDesignTreeIcons()
{
    if (!designTreeWidget)
        return;
    const auto refreshItem = [this](QTreeWidgetItem* item,
                                    const auto& self) -> void {
        if (!item)
            return;
        const int payloadId =
            item->data(0, Qt::UserRole + 1).toInt();
        const auto payload = designItemPayloads.constFind(payloadId);
        if (payload != designItemPayloads.cend()) {
            const DesignHierarchyNode& node = payload.value();
            item->setIcon(
                0,
                getSymbolIcon(
                    node.isTop
                        ? SymbolOutlineIconKind::Module
                        : (node.unresolved
                               ? SymbolOutlineIconKind::Symbol
                               : SymbolOutlineIconKind::Instance)));
            applyDesignItemDimming(item, !node.inSelectedTop);
        }
        for (int index = 0; index < item->childCount(); ++index)
            self(item->child(index), self);
    };
    for (int index = 0;
         index < designTreeWidget->topLevelItemCount();
         ++index) {
        refreshItem(designTreeWidget->topLevelItem(index), refreshItem);
    }
}
