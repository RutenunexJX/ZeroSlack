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

inline SymbolTaxonomy::SemanticMetadata completionCommandMetadataForRecord(
    const SemanticSymbolRecord& record)
{
    SymbolTaxonomy::SemanticMetadata metadata;
    metadata.declarationKind = record.declarationKind;
    metadata.usageRole = record.usageRole;
    metadata.ownerScope = record.owner.kind;
    metadata.visibility = record.visibility;
    metadata.sourceRole = record.sourceRole;
    metadata.rawCollectorKind = record.rawCollectorKind;
    metadata.interfaceLikeOwner = record.owner.interfaceLike;
    return metadata;
}

inline bool completionCommandKindMatchesTypedRecord(
    const SemanticSymbolRecord& record,
    CompletionCommandKind kind)
{
    return SymbolTaxonomy::typedCompletionSymbolTypeMatches(
        completionCommandMetadataForRecord(record),
        rawCollectorKindForCompletionCommandKind(kind),
        record.type.rawTypeText);
}

inline bool completionCommandKindMatchesCommandRecord(
    const SemanticSymbolRecord& record,
    CompletionCommandKind kind)
{
    return SymbolTaxonomy::commandSymbolTypeMatches(
        completionCommandMetadataForRecord(record),
        rawCollectorKindForCompletionCommandKind(kind),
        record.type.rawTypeText);
}

inline bool completionCommandKindMatchesModuleContextRecord(
    const SemanticSymbolRecord& record,
    CompletionCommandKind kind)
{
    return completionCommandKindMatchesCommandRecord(record, kind);
}

inline bool completionCommandKindIsGlobalCommand(CompletionCommandKind kind)
{
    return SymbolTaxonomy::isCommandGlobalCompletionType(
        rawCollectorKindForCompletionCommandKind(kind));
}

inline bool completionCommandKindIsAlwaysGlobalCommand(CompletionCommandKind kind)
{
    return SymbolTaxonomy::isAlwaysGlobalCommandSymbolType(
        rawCollectorKindForCompletionCommandKind(kind));
}

inline bool completionCommandKindIsPackageVisibleCommand(CompletionCommandKind kind)
{
    return SymbolTaxonomy::isPackageVisibleCommandRequest(
        rawCollectorKindForCompletionCommandKind(kind));
}

inline bool completionCommandKindIsModuleRange(CompletionCommandKind kind)
{
    return SymbolTaxonomy::isModuleRangeType(
        rawCollectorKindForCompletionCommandKind(kind));
}

#endif // COMPLETIONCOMMANDKINDADAPTER_H
