#ifndef COMPLETIONCOMMANDKINDADAPTER_H
#define COMPLETIONCOMMANDKINDADAPTER_H

#include "completiontypes.h"
#include "symboltaxonomy.h"

namespace completion_command_kind_adapter {

inline SymbolTaxonomy::SemanticCompletionKind semanticCompletionKindForCommand(
    CompletionCommandKind kind)
{
    switch (kind) {
    case CompletionCommandKind::VisibleSymbol:
        return SymbolTaxonomy::SemanticCompletionKind::VisibleSymbol;
    case CompletionCommandKind::Reg:
        return SymbolTaxonomy::SemanticCompletionKind::Reg;
    case CompletionCommandKind::Wire:
        return SymbolTaxonomy::SemanticCompletionKind::Wire;
    case CompletionCommandKind::Logic:
        return SymbolTaxonomy::SemanticCompletionKind::Logic;
    case CompletionCommandKind::Module:
        return SymbolTaxonomy::SemanticCompletionKind::Module;
    case CompletionCommandKind::Task:
        return SymbolTaxonomy::SemanticCompletionKind::Task;
    case CompletionCommandKind::Function:
        return SymbolTaxonomy::SemanticCompletionKind::Function;
    case CompletionCommandKind::Interface:
        return SymbolTaxonomy::SemanticCompletionKind::Interface;
    case CompletionCommandKind::Package:
        return SymbolTaxonomy::SemanticCompletionKind::Package;
    case CompletionCommandKind::Macro:
        return SymbolTaxonomy::SemanticCompletionKind::Macro;
    case CompletionCommandKind::Localparam:
        return SymbolTaxonomy::SemanticCompletionKind::Localparam;
    case CompletionCommandKind::Parameter:
        return SymbolTaxonomy::SemanticCompletionKind::Parameter;
    case CompletionCommandKind::AlwaysProcess:
        return SymbolTaxonomy::SemanticCompletionKind::AlwaysProcess;
    case CompletionCommandKind::ContinuousAssign:
        return SymbolTaxonomy::SemanticCompletionKind::ContinuousAssign;
    case CompletionCommandKind::Typedef:
        return SymbolTaxonomy::SemanticCompletionKind::Typedef;
    case CompletionCommandKind::EnumValue:
        return SymbolTaxonomy::SemanticCompletionKind::EnumValue;
    case CompletionCommandKind::EnumType:
        return SymbolTaxonomy::SemanticCompletionKind::EnumType;
    case CompletionCommandKind::EnumVariable:
        return SymbolTaxonomy::SemanticCompletionKind::EnumVariable;
    case CompletionCommandKind::StructMember:
        return SymbolTaxonomy::SemanticCompletionKind::StructMember;
    case CompletionCommandKind::PackedStructType:
        return SymbolTaxonomy::SemanticCompletionKind::PackedStructType;
    case CompletionCommandKind::UnpackedStructType:
        return SymbolTaxonomy::SemanticCompletionKind::UnpackedStructType;
    case CompletionCommandKind::PackedStructVariable:
        return SymbolTaxonomy::SemanticCompletionKind::PackedStructVariable;
    case CompletionCommandKind::UnpackedStructVariable:
        return SymbolTaxonomy::SemanticCompletionKind::UnpackedStructVariable;
    case CompletionCommandKind::User:
        return SymbolTaxonomy::SemanticCompletionKind::User;
    }
    return SymbolTaxonomy::SemanticCompletionKind::User;
}

inline bool metadataMatchesCompletionCommandKind(
    const SymbolTaxonomy::SemanticMetadata& metadata,
    CompletionCommandKind kind,
    const QString& rawTypeText,
    bool parameterAlias)
{
    return SymbolTaxonomy::semanticCompletionKindMatches(
        metadata,
        semanticCompletionKindForCommand(kind),
        rawTypeText,
        parameterAlias);
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
    case CompletionCommandKind::VisibleSymbol:
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
    case CompletionCommandKind::VisibleSymbol:
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
    case CompletionCommandKind::VisibleSymbol:
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
    case CompletionCommandKind::VisibleSymbol:
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
    case CompletionCommandKind::VisibleSymbol:
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
