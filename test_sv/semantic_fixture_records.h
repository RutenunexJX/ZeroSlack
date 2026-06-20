#ifndef TEST_SV_SEMANTIC_FIXTURE_RECORDS_H
#define TEST_SV_SEMANTIC_FIXTURE_RECORDS_H

#include "semanticindex.h"
#include "syminfo.h"

#include <QList>
#include <QSet>

static SymbolTaxonomy::CollectorKind collectorKindForFixtureType(sym_list::sym_type_e type)
{
    return static_cast<SymbolTaxonomy::CollectorKind>(type);
}

static SymbolTaxonomy::DeclarationKind declarationKindForFixtureType(sym_list::sym_type_e type)
{
    using DeclarationKind = SymbolTaxonomy::DeclarationKind;

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

static SymbolTaxonomy::SymbolUsageRole usageRoleForFixtureType(sym_list::sym_type_e type)
{
    using DeclarationKind = SymbolTaxonomy::DeclarationKind;
    using SymbolUsageRole = SymbolTaxonomy::SymbolUsageRole;

    switch (declarationKindForFixtureType(type)) {
    case DeclarationKind::Unknown:
        return SymbolUsageRole::Unknown;
    case DeclarationKind::Process:
    case DeclarationKind::Generate:
        return SymbolUsageRole::Process;
    case DeclarationKind::Instance:
        return type == sym_list::sym_inst_pin
            ? SymbolUsageRole::Reference
            : SymbolUsageRole::Declaration;
    case DeclarationKind::Module:
    case DeclarationKind::Interface:
    case DeclarationKind::Package:
    case DeclarationKind::Typedef:
    case DeclarationKind::Enum:
    case DeclarationKind::Parameter:
    case DeclarationKind::Localparam:
    case DeclarationKind::Port:
    case DeclarationKind::Signal:
    case DeclarationKind::Struct:
    case DeclarationKind::StructVariable:
    case DeclarationKind::StructMember:
    case DeclarationKind::Modport:
    case DeclarationKind::Task:
    case DeclarationKind::Function:
    case DeclarationKind::Macro:
    case DeclarationKind::Constraint:
    case DeclarationKind::User:
        return SymbolUsageRole::Declaration;
    }
    return SymbolUsageRole::Unknown;
}

static bool fixtureTypeIsGlobalDefinition(sym_list::sym_type_e type)
{
    return type == sym_list::sym_module
        || type == sym_list::sym_interface
        || type == sym_list::sym_package;
}

static bool fixtureTypeIsPackageVisibleDefinition(sym_list::sym_type_e type)
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

static bool fixtureTypeHasInterfaceLikeOwner(sym_list::sym_type_e type)
{
    return type == sym_list::sym_interface
        || type == sym_list::sym_inst
        || type == sym_list::sym_port_interface
        || type == sym_list::sym_port_interface_modport;
}

static SymbolTaxonomy::SymbolOwnerScope ownerScopeForFixtureSymbol(
    const sym_list::SymbolInfo& symbol,
    const QSet<QString>& packageScopes = {})
{
    using SymbolOwnerScope = SymbolTaxonomy::SymbolOwnerScope;

    const SymbolTaxonomy::DeclarationKind kind =
        declarationKindForFixtureType(symbol.symbolType);
    if (fixtureTypeIsGlobalDefinition(symbol.symbolType))
        return SymbolOwnerScope::Global;
    if (kind == SymbolTaxonomy::DeclarationKind::Modport)
        return SymbolOwnerScope::Interface;
    if (kind == SymbolTaxonomy::DeclarationKind::StructMember)
        return SymbolOwnerScope::Struct;
    if (!symbol.moduleScope.isEmpty()) {
        if (packageScopes.contains(symbol.moduleScope)
            && fixtureTypeIsPackageVisibleDefinition(symbol.symbolType)) {
            return SymbolOwnerScope::Package;
        }
        return SymbolOwnerScope::Module;
    }
    return SymbolOwnerScope::Unknown;
}

static SymbolTaxonomy::SymbolVisibility visibilityForFixtureSymbol(
    const sym_list::SymbolInfo& symbol,
    const QSet<QString>& packageScopes = {})
{
    using SymbolOwnerScope = SymbolTaxonomy::SymbolOwnerScope;
    using SymbolVisibility = SymbolTaxonomy::SymbolVisibility;

    const SymbolOwnerScope scope = ownerScopeForFixtureSymbol(symbol, packageScopes);
    if (scope == SymbolOwnerScope::Global)
        return SymbolVisibility::Global;
    if (scope == SymbolOwnerScope::Package
        && fixtureTypeIsPackageVisibleDefinition(symbol.symbolType)) {
        return SymbolVisibility::PackageVisible;
    }
    if (scope == SymbolOwnerScope::Interface
        || scope == SymbolOwnerScope::Struct) {
        return SymbolVisibility::Member;
    }
    if (scope == SymbolOwnerScope::Module)
        return SymbolVisibility::ScopeLocal;
    return SymbolVisibility::Unknown;
}

static SymbolTaxonomy::SemanticMetadata semanticMetadataForSymbolInfo(
    const sym_list::SymbolInfo& symbol,
    const QSet<QString>& packageScopes = {})
{
    if (symbol.hasSemanticMetadata) {
        SymbolTaxonomy::SemanticMetadata metadata;
        metadata.declarationKind = symbol.semanticDeclarationKind;
        metadata.usageRole = symbol.semanticUsageRole;
        metadata.ownerScope = symbol.semanticOwnerScope;
        metadata.visibility = symbol.semanticVisibility;
        metadata.sourceRole = symbol.semanticSourceRole;
        metadata.collectorKind = collectorKindForFixtureType(symbol.collectorKind);
        metadata.interfaceLikeOwner = symbol.interfaceLikeOwner;
        return metadata;
    }

    SymbolTaxonomy::SemanticMetadata metadata;
    metadata.declarationKind = declarationKindForFixtureType(symbol.symbolType);
    metadata.usageRole = usageRoleForFixtureType(symbol.symbolType);
    metadata.ownerScope = ownerScopeForFixtureSymbol(symbol, packageScopes);
    metadata.visibility = visibilityForFixtureSymbol(symbol, packageScopes);
    metadata.sourceRole = SymbolTaxonomy::sourceRoleForFileName(symbol.fileName);
    metadata.collectorKind = collectorKindForFixtureType(symbol.symbolType);
    metadata.interfaceLikeOwner = fixtureTypeHasInterfaceLikeOwner(symbol.symbolType);
    return metadata;
}

static SymbolTaxonomy::SemanticMetadata semanticMetadataForFixtureType(
    sym_list::sym_type_e type)
{
    sym_list::SymbolInfo symbol;
    symbol.symbolType = type;
    return semanticMetadataForSymbolInfo(symbol);
}

static SemanticSymbolRecord semanticSymbolRecordForSymbol(
    const sym_list::SymbolInfo& symbol,
    const QSet<QString>& packageScopes = {})
{
    const SymbolTaxonomy::SemanticMetadata metadata =
        semanticMetadataForSymbolInfo(symbol, packageScopes);

    SemanticSymbolRecord record;
    record.localHandle = symbol.symbolId;
    record.name = symbol.symbolName;
    record.location.fileName = symbol.fileName;
    record.location.startLine = symbol.startLine;
    record.location.startColumn = symbol.startColumn;
    record.location.endLine = symbol.endLine;
    record.location.endColumn = symbol.endColumn;
    record.location.position = symbol.position;
    record.location.length = symbol.length;
    record.declarationKind = metadata.declarationKind;
    record.usageRole = metadata.usageRole;
    record.visibility = metadata.visibility;
    record.sourceRole = metadata.sourceRole;
    record.collectorKind = metadata.collectorKind;
    record.owner.kind = metadata.ownerScope;
    record.owner.name = symbol.moduleScope;
    record.owner.interfaceLike = metadata.interfaceLikeOwner;
    record.type.rawTypeText = symbol.dataType;

    record.stableKey.fileName = record.location.fileName;
    record.stableKey.symbolName = record.name;
    record.stableKey.declarationKind = record.declarationKind;
    record.stableKey.ownerScope = record.owner.name;
    return record;
}

static QList<SemanticSymbolRecord> semanticSymbolRecordsForSymbols(
    const QList<sym_list::SymbolInfo>& symbols,
    const QSet<QString>& packageScopes = {})
{
    QList<SemanticSymbolRecord> records;
    records.reserve(symbols.size());
    for (const sym_list::SymbolInfo& symbol : symbols)
        records.append(semanticSymbolRecordForSymbol(symbol, packageScopes));
    return records;
}

#endif // TEST_SV_SEMANTIC_FIXTURE_RECORDS_H
