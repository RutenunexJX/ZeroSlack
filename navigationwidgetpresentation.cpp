#include "navigationwidget.h"

#include <QStyle>

#include "symboltaxonomy.h"

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
        icon = style()->standardIcon(QStyle::SP_DirIcon);
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
