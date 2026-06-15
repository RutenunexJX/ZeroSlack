#include "navigationwidget.h"

#include "symboltaxonomy.h"

#include <QStyle>

QString NavigationWidget::getSymbolTypeDisplayName(sym_list::sym_type_e symbolType)
{
    QString label = SymbolTaxonomy::symbolTypeLabel(symbolType);
    if (label.isEmpty() || label == QLatin1String("symbol"))
        return QStringLiteral("Symbols");
    label[0] = label.at(0).toUpper();
    return label;
}

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

QIcon NavigationWidget::getSymbolIcon(sym_list::sym_type_e symbolType)
{
    switch (SymbolTaxonomy::declarationKind(symbolType)) {
    case SymbolTaxonomy::DeclarationKind::Module:
        return style()->standardIcon(QStyle::SP_ComputerIcon);
    case SymbolTaxonomy::DeclarationKind::Signal:
        return style()->standardIcon(QStyle::SP_DialogApplyButton);
    case SymbolTaxonomy::DeclarationKind::Task:
    case SymbolTaxonomy::DeclarationKind::Function:
        return style()->standardIcon(QStyle::SP_MediaPlay);
    case SymbolTaxonomy::DeclarationKind::Parameter:
    case SymbolTaxonomy::DeclarationKind::Localparam:
        return style()->standardIcon(QStyle::SP_FileDialogDetailedView);
    case SymbolTaxonomy::DeclarationKind::Port:
        return style()->standardIcon(QStyle::SP_ArrowRight);
    case SymbolTaxonomy::DeclarationKind::Instance:
        return style()->standardIcon(QStyle::SP_DirIcon);
    case SymbolTaxonomy::DeclarationKind::Typedef:
    case SymbolTaxonomy::DeclarationKind::Enum:
    case SymbolTaxonomy::DeclarationKind::Struct:
    case SymbolTaxonomy::DeclarationKind::StructVariable:
    case SymbolTaxonomy::DeclarationKind::StructMember:
        return style()->standardIcon(QStyle::SP_FileIcon);
    case SymbolTaxonomy::DeclarationKind::Interface:
    case SymbolTaxonomy::DeclarationKind::Package:
    case SymbolTaxonomy::DeclarationKind::Modport:
    case SymbolTaxonomy::DeclarationKind::Macro:
    case SymbolTaxonomy::DeclarationKind::Process:
    case SymbolTaxonomy::DeclarationKind::Generate:
    case SymbolTaxonomy::DeclarationKind::Constraint:
    case SymbolTaxonomy::DeclarationKind::User:
    case SymbolTaxonomy::DeclarationKind::Unknown:
    default:
        return style()->standardIcon(QStyle::SP_FileIcon);
    }
}
