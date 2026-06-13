#include "navigationwidget.h"

#include <QFileInfo>
#include <QStyle>

QString NavigationWidget::getSymbolTypeDisplayName(sym_list::sym_type_e symbolType)
{
    switch (symbolType) {
    case sym_list::sym_module: return "Module";
    case sym_list::sym_reg: return "Register";
    case sym_list::sym_wire: return "Wire";
    case sym_list::sym_logic: return "Logic";
    case sym_list::sym_task: return "Task";
    case sym_list::sym_function: return "Function";
    case sym_list::sym_parameter: return "Parameter";
    case sym_list::sym_localparam: return "Local Parameter";
    case sym_list::sym_port_input: return "Input Port";
    case sym_list::sym_port_output: return "Output Port";
    case sym_list::sym_port_inout: return "Inout Port";
    case sym_list::sym_port_ref: return "Ref Port";
    case sym_list::sym_enum: return "Enum Type";
    case sym_list::sym_inst: return "Instance";
    case sym_list::sym_packed_struct: return "Packed Struct Type";
    case sym_list::sym_unpacked_struct: return "Unpacked Struct Type";
    case sym_list::sym_packed_struct_var: return "Packed Struct Variable";
    case sym_list::sym_unpacked_struct_var: return "Unpacked Struct Variable";
    case sym_list::sym_struct_member: return "Struct Member";
    case sym_list::sym_typedef: return "Typedef";
    case sym_list::sym_enum_var: return "Enum Variable";
    case sym_list::sym_enum_value: return "Enum Value";
    default: return "Symbols";
    }
}

QIcon NavigationWidget::getFileIcon(const QString& filePath)
{
    QString suffix = QFileInfo(filePath).suffix().toLower();

    if (suffix == "sv" || suffix == "v") {
        return style()->standardIcon(QStyle::SP_FileIcon);
    } else if (suffix == "vh" || suffix == "svh") {
        return style()->standardIcon(QStyle::SP_FileDialogDetailedView);
    }

    return style()->standardIcon(QStyle::SP_FileIcon);
}

QIcon NavigationWidget::getSymbolIcon(sym_list::sym_type_e symbolType)
{
    switch (symbolType) {
    case sym_list::sym_module:
        return style()->standardIcon(QStyle::SP_ComputerIcon);
    case sym_list::sym_reg:
    case sym_list::sym_wire:
    case sym_list::sym_logic:
        return style()->standardIcon(QStyle::SP_DialogApplyButton);
    case sym_list::sym_task:
    case sym_list::sym_function:
        return style()->standardIcon(QStyle::SP_MediaPlay);
    case sym_list::sym_parameter:
    case sym_list::sym_localparam:
        return style()->standardIcon(QStyle::SP_FileDialogDetailedView);
    case sym_list::sym_port_input:
    case sym_list::sym_port_output:
    case sym_list::sym_port_inout:
    case sym_list::sym_port_ref:
        return style()->standardIcon(QStyle::SP_ArrowRight);
    case sym_list::sym_inst:
        return style()->standardIcon(QStyle::SP_DirIcon);
    case sym_list::sym_packed_struct:
    case sym_list::sym_unpacked_struct:
    case sym_list::sym_packed_struct_var:
    case sym_list::sym_unpacked_struct_var:
    case sym_list::sym_struct_member:
    case sym_list::sym_typedef:
    case sym_list::sym_enum:
    case sym_list::sym_enum_var:
    case sym_list::sym_enum_value:
        return style()->standardIcon(QStyle::SP_FileIcon);
    default:
        return style()->standardIcon(QStyle::SP_FileIcon);
    }
}
