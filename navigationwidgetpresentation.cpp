#include "navigationwidget.h"

#include <QPainter>
#include <QPixmap>
#include <QStyle>

#include "symboltaxonomy.h"

namespace {
QIcon makeInstanceIcon()
{
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen linePen(QColor(55, 65, 81), 1.4);
    painter.setPen(linePen);
    painter.drawLine(QPointF(5.0, 5.0), QPointF(11.0, 11.0));
    painter.drawLine(QPointF(5.0, 11.0), QPointF(11.0, 5.0));

    painter.setPen(QPen(QColor(31, 41, 55), 1.0));
    painter.setBrush(QColor(226, 232, 240));
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
