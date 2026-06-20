#ifndef COMPLETIONCOMMANDKINDADAPTER_H
#define COMPLETIONCOMMANDKINDADAPTER_H

#include "completiontypes.h"
#include "symboltaxonomy.h"

namespace completion_command_kind_adapter {

inline bool metadataMatchesCompletionCommandKind(
    const SymbolTaxonomy::SemanticMetadata& metadata,
    CompletionCommandKind kind,
    const QString& rawTypeText,
    bool parameterAlias)
{
    using DeclarationKind = SymbolTaxonomy::DeclarationKind;
    const bool semanticOnly =
        metadata.rawCollectorKind == sym_list::sym_user;
    switch (kind) {
    case CompletionCommandKind::Reg:
        return metadata.rawCollectorKind == sym_list::sym_reg
            || (semanticOnly
                && metadata.declarationKind == DeclarationKind::Signal);
    case CompletionCommandKind::Wire:
        return metadata.rawCollectorKind == sym_list::sym_wire
            || (semanticOnly
                && metadata.declarationKind == DeclarationKind::Signal);
    case CompletionCommandKind::Logic:
        return metadata.rawCollectorKind == sym_list::sym_logic
            || (semanticOnly
                && metadata.declarationKind == DeclarationKind::Signal);
    case CompletionCommandKind::Module:
        return metadata.declarationKind == DeclarationKind::Module;
    case CompletionCommandKind::Task:
        return metadata.declarationKind == DeclarationKind::Task;
    case CompletionCommandKind::Function:
        return metadata.declarationKind == DeclarationKind::Function;
    case CompletionCommandKind::Interface:
        return metadata.declarationKind == DeclarationKind::Interface;
    case CompletionCommandKind::Package:
        return metadata.declarationKind == DeclarationKind::Package;
    case CompletionCommandKind::Macro:
        return metadata.declarationKind == DeclarationKind::Macro;
    case CompletionCommandKind::Localparam:
        return metadata.rawCollectorKind == sym_list::sym_localparam
            || (semanticOnly
                && metadata.declarationKind == DeclarationKind::Localparam);
    case CompletionCommandKind::Parameter:
        return metadata.rawCollectorKind == sym_list::sym_parameter
            || (parameterAlias
                && metadata.rawCollectorKind == sym_list::sym_localparam)
            || (semanticOnly
                && (metadata.declarationKind == DeclarationKind::Parameter
                    || (parameterAlias
                        && metadata.declarationKind
                            == DeclarationKind::Localparam)));
    case CompletionCommandKind::AlwaysProcess:
        return metadata.rawCollectorKind == sym_list::sym_always
            || metadata.rawCollectorKind == sym_list::sym_always_ff
            || metadata.rawCollectorKind == sym_list::sym_always_comb
            || metadata.rawCollectorKind == sym_list::sym_always_latch
            || (semanticOnly
                && metadata.declarationKind == DeclarationKind::Process);
    case CompletionCommandKind::ContinuousAssign:
        return metadata.rawCollectorKind == sym_list::sym_assign
            || (semanticOnly
                && metadata.declarationKind == DeclarationKind::Process);
    case CompletionCommandKind::Typedef:
        return metadata.rawCollectorKind == sym_list::sym_typedef
            || (semanticOnly
                && metadata.declarationKind == DeclarationKind::Typedef);
    case CompletionCommandKind::EnumValue:
        return metadata.rawCollectorKind == sym_list::sym_enum_value;
    case CompletionCommandKind::EnumType:
        return metadata.rawCollectorKind == sym_list::sym_enum
            || (metadata.rawCollectorKind == sym_list::sym_typedef
                && rawTypeText == QLatin1String("enum"))
            || (semanticOnly
                && metadata.declarationKind == DeclarationKind::Typedef
                && rawTypeText == QLatin1String("enum"));
    case CompletionCommandKind::EnumVariable:
        return metadata.rawCollectorKind == sym_list::sym_enum_var;
    case CompletionCommandKind::StructMember:
        return metadata.rawCollectorKind == sym_list::sym_struct_member
            || (semanticOnly
                && metadata.declarationKind == DeclarationKind::StructMember);
    case CompletionCommandKind::PackedStructType:
        return metadata.rawCollectorKind == sym_list::sym_packed_struct
            || (semanticOnly
                && metadata.declarationKind == DeclarationKind::Struct);
    case CompletionCommandKind::UnpackedStructType:
        return metadata.rawCollectorKind == sym_list::sym_unpacked_struct
            || (semanticOnly
                && metadata.declarationKind == DeclarationKind::Struct);
    case CompletionCommandKind::PackedStructVariable:
        return metadata.rawCollectorKind == sym_list::sym_packed_struct_var
            || (semanticOnly
                && metadata.declarationKind == DeclarationKind::StructVariable);
    case CompletionCommandKind::UnpackedStructVariable:
        return metadata.rawCollectorKind == sym_list::sym_unpacked_struct_var
            || (semanticOnly
                && metadata.declarationKind == DeclarationKind::StructVariable);
    case CompletionCommandKind::User:
        return metadata.declarationKind == DeclarationKind::User;
    }
    return false;
}

} // namespace completion_command_kind_adapter

inline bool completionCommandKindRequiresModuleContext(
    CompletionCommandKind kind)
{
    switch (kind) {
    case CompletionCommandKind::PackedStructType:
    case CompletionCommandKind::UnpackedStructType:
    case CompletionCommandKind::PackedStructVariable:
    case CompletionCommandKind::UnpackedStructVariable:
        return true;
    case CompletionCommandKind::User:
    case CompletionCommandKind::Module:
    case CompletionCommandKind::Reg:
    case CompletionCommandKind::Wire:
    case CompletionCommandKind::Logic:
    case CompletionCommandKind::Task:
    case CompletionCommandKind::Function:
    case CompletionCommandKind::Interface:
    case CompletionCommandKind::Package:
    case CompletionCommandKind::Macro:
    case CompletionCommandKind::Typedef:
    case CompletionCommandKind::Localparam:
    case CompletionCommandKind::Parameter:
    case CompletionCommandKind::AlwaysProcess:
    case CompletionCommandKind::ContinuousAssign:
    case CompletionCommandKind::EnumValue:
    case CompletionCommandKind::EnumType:
    case CompletionCommandKind::EnumVariable:
    case CompletionCommandKind::StructMember:
        return false;
    }
    return false;
}

inline bool completionCommandKindMatchesTypedRecord(
    const SemanticSymbolRecord& record,
    CompletionCommandKind kind)
{
    return completion_command_kind_adapter::metadataMatchesCompletionCommandKind(
        semanticMetadataForSymbolRecord(record),
        kind,
        record.type.rawTypeText,
        false);
}

inline bool completionCommandKindMatchesCommandRecord(
    const SemanticSymbolRecord& record,
    CompletionCommandKind kind)
{
    return completion_command_kind_adapter::metadataMatchesCompletionCommandKind(
        semanticMetadataForSymbolRecord(record),
        kind,
        record.type.rawTypeText,
        true);
}

inline bool completionCommandKindMatchesModuleContextRecord(
    const SemanticSymbolRecord& record,
    CompletionCommandKind kind)
{
    return completionCommandKindMatchesCommandRecord(record, kind);
}

inline bool completionCommandKindIsGlobalCommand(CompletionCommandKind kind)
{
    switch (kind) {
    case CompletionCommandKind::Module:
    case CompletionCommandKind::Task:
    case CompletionCommandKind::Function:
    case CompletionCommandKind::Interface:
    case CompletionCommandKind::Package:
    case CompletionCommandKind::Typedef:
    case CompletionCommandKind::Macro:
    case CompletionCommandKind::PackedStructType:
    case CompletionCommandKind::UnpackedStructType:
    case CompletionCommandKind::EnumType:
        return true;
    case CompletionCommandKind::User:
    case CompletionCommandKind::Reg:
    case CompletionCommandKind::Wire:
    case CompletionCommandKind::Logic:
    case CompletionCommandKind::Localparam:
    case CompletionCommandKind::Parameter:
    case CompletionCommandKind::AlwaysProcess:
    case CompletionCommandKind::ContinuousAssign:
    case CompletionCommandKind::EnumValue:
    case CompletionCommandKind::EnumVariable:
    case CompletionCommandKind::StructMember:
    case CompletionCommandKind::PackedStructVariable:
    case CompletionCommandKind::UnpackedStructVariable:
        return false;
    }
    return false;
}

inline bool completionCommandKindIsAlwaysGlobalCommand(CompletionCommandKind kind)
{
    switch (kind) {
    case CompletionCommandKind::Module:
    case CompletionCommandKind::Interface:
    case CompletionCommandKind::Package:
    case CompletionCommandKind::Macro:
        return true;
    case CompletionCommandKind::User:
    case CompletionCommandKind::Reg:
    case CompletionCommandKind::Wire:
    case CompletionCommandKind::Logic:
    case CompletionCommandKind::Task:
    case CompletionCommandKind::Function:
    case CompletionCommandKind::Localparam:
    case CompletionCommandKind::Parameter:
    case CompletionCommandKind::AlwaysProcess:
    case CompletionCommandKind::ContinuousAssign:
    case CompletionCommandKind::Typedef:
    case CompletionCommandKind::EnumValue:
    case CompletionCommandKind::EnumType:
    case CompletionCommandKind::EnumVariable:
    case CompletionCommandKind::StructMember:
    case CompletionCommandKind::PackedStructType:
    case CompletionCommandKind::UnpackedStructType:
    case CompletionCommandKind::PackedStructVariable:
    case CompletionCommandKind::UnpackedStructVariable:
        return false;
    }
    return false;
}

inline bool completionCommandKindIsPackageVisibleCommand(CompletionCommandKind kind)
{
    switch (kind) {
    case CompletionCommandKind::Parameter:
    case CompletionCommandKind::Localparam:
    case CompletionCommandKind::Typedef:
    case CompletionCommandKind::EnumType:
    case CompletionCommandKind::PackedStructType:
    case CompletionCommandKind::UnpackedStructType:
        return true;
    case CompletionCommandKind::User:
    case CompletionCommandKind::Reg:
    case CompletionCommandKind::Wire:
    case CompletionCommandKind::Logic:
    case CompletionCommandKind::Module:
    case CompletionCommandKind::Task:
    case CompletionCommandKind::Function:
    case CompletionCommandKind::Interface:
    case CompletionCommandKind::Package:
    case CompletionCommandKind::Macro:
    case CompletionCommandKind::AlwaysProcess:
    case CompletionCommandKind::ContinuousAssign:
    case CompletionCommandKind::EnumValue:
    case CompletionCommandKind::EnumVariable:
    case CompletionCommandKind::StructMember:
    case CompletionCommandKind::PackedStructVariable:
    case CompletionCommandKind::UnpackedStructVariable:
        return false;
    }
    return false;
}

inline bool completionCommandKindIsModuleRange(CompletionCommandKind kind)
{
    switch (kind) {
    case CompletionCommandKind::PackedStructType:
    case CompletionCommandKind::UnpackedStructType:
    case CompletionCommandKind::PackedStructVariable:
    case CompletionCommandKind::UnpackedStructVariable:
        return true;
    case CompletionCommandKind::User:
    case CompletionCommandKind::Reg:
    case CompletionCommandKind::Wire:
    case CompletionCommandKind::Logic:
    case CompletionCommandKind::Module:
    case CompletionCommandKind::Task:
    case CompletionCommandKind::Function:
    case CompletionCommandKind::Interface:
    case CompletionCommandKind::Package:
    case CompletionCommandKind::Macro:
    case CompletionCommandKind::Typedef:
    case CompletionCommandKind::Localparam:
    case CompletionCommandKind::Parameter:
    case CompletionCommandKind::AlwaysProcess:
    case CompletionCommandKind::ContinuousAssign:
    case CompletionCommandKind::EnumValue:
    case CompletionCommandKind::EnumType:
    case CompletionCommandKind::EnumVariable:
    case CompletionCommandKind::StructMember:
        return false;
    }
    return false;
}

#endif // COMPLETIONCOMMANDKINDADAPTER_H
