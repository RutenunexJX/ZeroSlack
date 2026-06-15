#include "navigationwidget.h"

#include <QStyle>

#include "symboltaxonomy.h"

QIcon NavigationWidget::getFileIcon(const QString& filePath)
{
    const SymbolTaxonomy::SourceRole role =
        SymbolTaxonomy::sourceRoleForFileName(filePath);
    if (role == SymbolTaxonomy::SourceRole::DesignSource) {
        return style()->standardIcon(QStyle::SP_FileIcon);
    } else if (SymbolTaxonomy::isHeaderSourceRole(role)) {
        return style()->standardIcon(QStyle::SP_FileDialogDetailedView);
    }

    return style()->standardIcon(QStyle::SP_FileIcon);
}

QIcon NavigationWidget::getSymbolIcon(SymbolOutlineIconKind iconKind)
{
    switch (iconKind) {
    case SymbolOutlineIconKind::Module:
        return style()->standardIcon(QStyle::SP_ComputerIcon);
    case SymbolOutlineIconKind::Signal:
        return style()->standardIcon(QStyle::SP_DialogApplyButton);
    case SymbolOutlineIconKind::Subroutine:
        return style()->standardIcon(QStyle::SP_MediaPlay);
    case SymbolOutlineIconKind::Parameter:
        return style()->standardIcon(QStyle::SP_FileDialogDetailedView);
    case SymbolOutlineIconKind::Port:
        return style()->standardIcon(QStyle::SP_ArrowRight);
    case SymbolOutlineIconKind::Instance:
        return style()->standardIcon(QStyle::SP_DirIcon);
    case SymbolOutlineIconKind::Type:
        return style()->standardIcon(QStyle::SP_FileIcon);
    case SymbolOutlineIconKind::Symbol:
    default:
        return style()->standardIcon(QStyle::SP_FileIcon);
    }
}
