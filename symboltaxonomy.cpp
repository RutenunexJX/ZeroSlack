#include "symboltaxonomy.h"

#include <QFileInfo>

namespace SymbolTaxonomy {

DeclarationKind declarationKind(sym_list::sym_type_e type)
{
    switch (type) {
    case sym_list::sym_module:
        return DeclarationKind::Module;
    case sym_list::sym_interface:
    case sym_list::sym_interface_assco_struct:
        return DeclarationKind::Interface;
    case sym_list::sym_package:
        return DeclarationKind::Package;
    case sym_list::sym_typedef:
        return DeclarationKind::Typedef;
    case sym_list::sym_enum:
    case sym_list::sym_enum_var:
    case sym_list::sym_enum_value:
        return DeclarationKind::Enum;
    case sym_list::sym_parameter:
    case sym_list::sym_module_parameter:
    case sym_list::sym_interface_parameter:
    case sym_list::sym_def_parameter:
        return DeclarationKind::Parameter;
    case sym_list::sym_localparam:
        return DeclarationKind::Localparam;
    case sym_list::sym_port_input:
    case sym_list::sym_port_output:
    case sym_list::sym_port_inout:
    case sym_list::sym_port_ref:
    case sym_list::sym_port_interface:
    case sym_list::sym_port_interface_modport:
        return DeclarationKind::Port;
    case sym_list::sym_reg:
    case sym_list::sym_wire:
    case sym_list::sym_logic:
        return DeclarationKind::Signal;
    case sym_list::sym_packed_struct:
    case sym_list::sym_unpacked_struct:
        return DeclarationKind::Struct;
    case sym_list::sym_packed_struct_var:
    case sym_list::sym_unpacked_struct_var:
        return DeclarationKind::StructVariable;
    case sym_list::sym_struct_member:
        return DeclarationKind::StructMember;
    case sym_list::sym_inst:
    case sym_list::sym_inst_pin:
        return DeclarationKind::Instance;
    case sym_list::sym_interface_modport:
        return DeclarationKind::Modport;
    case sym_list::sym_task:
        return DeclarationKind::Task;
    case sym_list::sym_function:
        return DeclarationKind::Function;
    case sym_list::sym_def_define:
    case sym_list::sym_def_ifdef:
    case sym_list::sym_def_ifndef:
    case sym_list::sym_def_else:
    case sym_list::sym_def_elsif:
    case sym_list::sym_def_endif:
        return DeclarationKind::Macro;
    case sym_list::sym_always:
    case sym_list::sym_always_ff:
    case sym_list::sym_always_comb:
    case sym_list::sym_always_latch:
    case sym_list::sym_assign:
    case sym_list::sym_initial:
    case sym_list::sym_case:
    case sym_list::sym_casex:
    case sym_list::sym_casez:
    case sym_list::sym_endcase:
    case sym_list::sym_case_default:
    case sym_list::sym_fsm_state:
        return DeclarationKind::Process;
    case sym_list::sym_generate_if:
    case sym_list::sym_generate_for:
    case sym_list::sym_generate_case:
        return DeclarationKind::Generate;
    case sym_list::sym_xilinx_constraint:
        return DeclarationKind::Constraint;
    case sym_list::sym_user:
        return DeclarationKind::User;
    }
    return DeclarationKind::Unknown;
}

bool isDefinitionCandidate(sym_list::sym_type_e type)
{
    switch (type) {
    case sym_list::sym_module:
    case sym_list::sym_interface:
    case sym_list::sym_interface_modport:
    case sym_list::sym_package:
    case sym_list::sym_inst:
    case sym_list::sym_task:
    case sym_list::sym_function:
    case sym_list::sym_port_input:
    case sym_list::sym_port_output:
    case sym_list::sym_port_inout:
    case sym_list::sym_port_ref:
    case sym_list::sym_port_interface:
    case sym_list::sym_port_interface_modport:
    case sym_list::sym_reg:
    case sym_list::sym_wire:
    case sym_list::sym_logic:
    case sym_list::sym_parameter:
    case sym_list::sym_localparam:
    case sym_list::sym_packed_struct:
    case sym_list::sym_unpacked_struct:
    case sym_list::sym_packed_struct_var:
    case sym_list::sym_unpacked_struct_var:
    case sym_list::sym_struct_member:
    case sym_list::sym_typedef:
    case sym_list::sym_enum:
    case sym_list::sym_enum_var:
    case sym_list::sym_enum_value:
        return true;
    default:
        return false;
    }
}

bool isGlobalDefinition(sym_list::sym_type_e type)
{
    return type == sym_list::sym_module
        || type == sym_list::sym_interface
        || type == sym_list::sym_package;
}

bool isPackageVisibleDefinition(sym_list::sym_type_e type)
{
    switch (type) {
    case sym_list::sym_parameter:
    case sym_list::sym_localparam:
    case sym_list::sym_typedef:
    case sym_list::sym_enum:
    case sym_list::sym_enum_value:
    case sym_list::sym_packed_struct:
    case sym_list::sym_unpacked_struct:
        return true;
    default:
        return false;
    }
}

bool isInterfaceLikeOwner(sym_list::sym_type_e type)
{
    return type == sym_list::sym_interface
        || type == sym_list::sym_inst
        || type == sym_list::sym_port_interface
        || type == sym_list::sym_port_interface_modport;
}

int definitionPriority(sym_list::sym_type_e type)
{
    switch (type) {
    case sym_list::sym_module: return 0;
    case sym_list::sym_interface: return 1;
    case sym_list::sym_package: return 2;
    case sym_list::sym_interface_modport: return 3;
    case sym_list::sym_port_input:
    case sym_list::sym_port_output:
    case sym_list::sym_port_inout:
    case sym_list::sym_port_ref:
    case sym_list::sym_port_interface:
    case sym_list::sym_port_interface_modport: return 3;
    case sym_list::sym_task:
    case sym_list::sym_function: return 4;
    case sym_list::sym_reg:
    case sym_list::sym_wire:
    case sym_list::sym_logic:
    case sym_list::sym_packed_struct_var:
    case sym_list::sym_unpacked_struct_var:
    case sym_list::sym_enum_var: return 5;
    case sym_list::sym_parameter:
    case sym_list::sym_localparam:
    case sym_list::sym_enum:
    case sym_list::sym_packed_struct:
    case sym_list::sym_unpacked_struct:
    case sym_list::sym_typedef: return 6;
    case sym_list::sym_struct_member:
    case sym_list::sym_enum_value: return 7;
    default: return 10;
    }
}

bool commandSymbolTypeMatches(sym_list::sym_type_e symbolType,
                              sym_list::sym_type_e commandType,
                              const QString& dataType)
{
    if (symbolType == commandType)
        return true;
    if (commandType == sym_list::sym_parameter
        && symbolType == sym_list::sym_localparam) {
        return true;
    }
    return commandType == sym_list::sym_enum
        && symbolType == sym_list::sym_typedef
        && dataType == QLatin1String("enum");
}

bool isInternalCompletionCandidate(sym_list::sym_type_e type)
{
    return type == sym_list::sym_reg
        || type == sym_list::sym_wire
        || type == sym_list::sym_logic
        || type == sym_list::sym_localparam
        || type == sym_list::sym_parameter;
}

bool isGlobalCompletionCandidate(sym_list::sym_type_e type)
{
    return type == sym_list::sym_module
        || type == sym_list::sym_task
        || type == sym_list::sym_function
        || type == sym_list::sym_interface
        || type == sym_list::sym_package;
}

bool isCommandGlobalCompletionType(sym_list::sym_type_e type)
{
    return type == sym_list::sym_module
        || type == sym_list::sym_task
        || type == sym_list::sym_function
        || type == sym_list::sym_interface
        || type == sym_list::sym_package
        || type == sym_list::sym_typedef
        || type == sym_list::sym_def_define
        || type == sym_list::sym_packed_struct
        || type == sym_list::sym_unpacked_struct
        || type == sym_list::sym_enum;
}

bool isGlobalSymbolInfoType(sym_list::sym_type_e type)
{
    return type == sym_list::sym_module
        || type == sym_list::sym_task
        || type == sym_list::sym_function
        || type == sym_list::sym_interface
        || type == sym_list::sym_package
        || type == sym_list::sym_typedef
        || type == sym_list::sym_def_define
        || type == sym_list::sym_packed_struct
        || type == sym_list::sym_unpacked_struct
        || type == sym_list::sym_packed_struct_var
        || type == sym_list::sym_unpacked_struct_var
        || type == sym_list::sym_enum;
}

bool isAlwaysGlobalSymbolInfoType(sym_list::sym_type_e type)
{
    return type == sym_list::sym_module
        || type == sym_list::sym_interface
        || type == sym_list::sym_package
        || type == sym_list::sym_packed_struct
        || type == sym_list::sym_unpacked_struct
        || type == sym_list::sym_enum;
}

bool isAlwaysGlobalCommandSymbolType(sym_list::sym_type_e type)
{
    return type == sym_list::sym_module
        || type == sym_list::sym_interface
        || type == sym_list::sym_package
        || type == sym_list::sym_def_define;
}

bool isPackageVisibleCommandRequest(sym_list::sym_type_e requestedType)
{
    switch (requestedType) {
    case sym_list::sym_parameter:
    case sym_list::sym_localparam:
    case sym_list::sym_typedef:
    case sym_list::sym_enum:
    case sym_list::sym_packed_struct:
    case sym_list::sym_unpacked_struct:
        return true;
    default:
        return false;
    }
}

SourceRole sourceRoleForFileName(const QString& fileName)
{
    const QString suffix = QFileInfo(fileName).suffix().toLower();
    if (suffix == QLatin1String("svh")
        || suffix == QLatin1String("vh")
        || suffix == QLatin1String("h")) {
        return SourceRole::Header;
    }
    if (suffix == QLatin1String("sv")
        || suffix == QLatin1String("v")) {
        return SourceRole::DesignSource;
    }
    return SourceRole::Unknown;
}

bool isHeaderSourceRole(SourceRole role)
{
    return role == SourceRole::Header;
}

} // namespace SymbolTaxonomy
