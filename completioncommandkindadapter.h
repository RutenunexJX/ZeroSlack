#ifndef COMPLETIONCOMMANDKINDADAPTER_H
#define COMPLETIONCOMMANDKINDADAPTER_H

#include "completiontypes.h"
#include "symboltaxonomy.h"

inline sym_list::sym_type_e rawCollectorKindForCompletionCommandKind(
    CompletionCommandKind kind)
{
    switch (kind) {
    case CompletionCommandKind::Reg:
        return sym_list::sym_reg;
    case CompletionCommandKind::Wire:
        return sym_list::sym_wire;
    case CompletionCommandKind::Logic:
        return sym_list::sym_logic;
    case CompletionCommandKind::Module:
        return sym_list::sym_module;
    case CompletionCommandKind::Task:
        return sym_list::sym_task;
    case CompletionCommandKind::Function:
        return sym_list::sym_function;
    case CompletionCommandKind::Interface:
        return sym_list::sym_interface;
    case CompletionCommandKind::Package:
        return sym_list::sym_package;
    case CompletionCommandKind::Macro:
        return sym_list::sym_def_define;
    case CompletionCommandKind::Localparam:
        return sym_list::sym_localparam;
    case CompletionCommandKind::Parameter:
        return sym_list::sym_parameter;
    case CompletionCommandKind::AlwaysProcess:
        return sym_list::sym_always;
    case CompletionCommandKind::ContinuousAssign:
        return sym_list::sym_assign;
    case CompletionCommandKind::Typedef:
        return sym_list::sym_typedef;
    case CompletionCommandKind::EnumValue:
        return sym_list::sym_enum_value;
    case CompletionCommandKind::EnumType:
        return sym_list::sym_enum;
    case CompletionCommandKind::EnumVariable:
        return sym_list::sym_enum_var;
    case CompletionCommandKind::StructMember:
        return sym_list::sym_struct_member;
    case CompletionCommandKind::PackedStructType:
        return sym_list::sym_packed_struct;
    case CompletionCommandKind::UnpackedStructType:
        return sym_list::sym_unpacked_struct;
    case CompletionCommandKind::PackedStructVariable:
        return sym_list::sym_packed_struct_var;
    case CompletionCommandKind::UnpackedStructVariable:
        return sym_list::sym_unpacked_struct_var;
    case CompletionCommandKind::User:
        return sym_list::sym_user;
    }
    return sym_list::sym_user;
}

inline bool completionCommandKindRequiresModuleContext(
    CompletionCommandKind kind)
{
    return SymbolTaxonomy::isDirectModuleContextCompletionRequest(
        rawCollectorKindForCompletionCommandKind(kind));
}

#endif // COMPLETIONCOMMANDKINDADAPTER_H
