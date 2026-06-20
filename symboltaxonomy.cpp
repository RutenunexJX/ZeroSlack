#include "symboltaxonomylegacy.h"

#include <QFileInfo>

namespace SymbolTaxonomy {

RawCollectorKind rawCollectorKind(sym_list::sym_type_e type)
{
    return static_cast<RawCollectorKind>(type);
}

sym_list::sym_type_e legacySymbolType(RawCollectorKind kind)
{
    return static_cast<sym_list::sym_type_e>(kind);
}

namespace {

bool rawCollectorKindIs(const SemanticMetadata& metadata,
                        RawCollectorKind kind)
{
    return metadata.rawCollectorKind == kind;
}

bool hasRawCollectorKind(const SemanticMetadata& metadata)
{
    return !rawCollectorKindIs(metadata, RawCollectorKind::User);
}

}

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

SymbolUsageRole usageRole(sym_list::sym_type_e type)
{
    switch (declarationKind(type)) {
    case DeclarationKind::Unknown:
        return SymbolUsageRole::Unknown;
    case DeclarationKind::Process:
    case DeclarationKind::Generate:
        return SymbolUsageRole::Process;
    case DeclarationKind::Instance:
        if (type == sym_list::sym_inst_pin)
            return SymbolUsageRole::Reference;
        return SymbolUsageRole::Declaration;
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

DeclarationGroup declarationGroup(sym_list::sym_type_e type)
{
    if (isPortDeclaration(type))
        return DeclarationGroup::Port;
    if (isParameterDeclaration(type))
        return DeclarationGroup::Parameter;
    if (isInstanceDeclaration(type))
        return DeclarationGroup::Instance;
    if (isSignalDeclaration(type))
        return DeclarationGroup::Signal;
    return DeclarationGroup::Unknown;
}

DeclarationGroup declarationGroup(const SemanticMetadata& metadata)
{
    switch (metadata.declarationKind) {
    case DeclarationKind::Port:
        return DeclarationGroup::Port;
    case DeclarationKind::Parameter:
    case DeclarationKind::Localparam:
        return DeclarationGroup::Parameter;
    case DeclarationKind::Instance:
        return metadata.usageRole == SymbolUsageRole::Declaration
            ? DeclarationGroup::Instance
            : DeclarationGroup::Unknown;
    case DeclarationKind::Enum:
        return rawCollectorKindIs(metadata, RawCollectorKind::EnumVariable)
            ? DeclarationGroup::Signal
            : DeclarationGroup::Unknown;
    case DeclarationKind::Signal:
    case DeclarationKind::StructVariable:
        return DeclarationGroup::Signal;
    case DeclarationKind::Unknown:
    case DeclarationKind::Module:
    case DeclarationKind::Interface:
    case DeclarationKind::Package:
    case DeclarationKind::Typedef:
    case DeclarationKind::Struct:
    case DeclarationKind::StructMember:
    case DeclarationKind::Modport:
    case DeclarationKind::Task:
    case DeclarationKind::Function:
    case DeclarationKind::Macro:
    case DeclarationKind::Process:
    case DeclarationKind::Generate:
    case DeclarationKind::Constraint:
    case DeclarationKind::User:
        break;
    }
    return DeclarationGroup::Unknown;
}

namespace {

SymbolOwnerScope legacyOwnerScope(
    const sym_list::SymbolInfo& symbol,
    const QSet<QString>& packageScopes)
{
    const DeclarationKind kind = declarationKind(symbol.symbolType);
    if (isGlobalDefinition(symbol.symbolType))
        return SymbolOwnerScope::Global;
    if (kind == DeclarationKind::Modport)
        return SymbolOwnerScope::Interface;
    if (kind == DeclarationKind::StructMember)
        return SymbolOwnerScope::Struct;
    if (!symbol.moduleScope.isEmpty()) {
        if (packageScopes.contains(symbol.moduleScope)
            && isPackageVisibleDefinition(symbol.symbolType)) {
            return SymbolOwnerScope::Package;
        }
        return SymbolOwnerScope::Module;
    }
    return SymbolOwnerScope::Unknown;
}

SymbolVisibility legacyVisibility(
    const sym_list::SymbolInfo& symbol,
    const QSet<QString>& packageScopes)
{
    const SymbolOwnerScope scope = legacyOwnerScope(symbol, packageScopes);
    if (scope == SymbolOwnerScope::Global)
        return SymbolVisibility::Global;
    if (scope == SymbolOwnerScope::Package
        && isPackageVisibleDefinition(symbol.symbolType)) {
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

SemanticMetadata computedSemanticMetadata(
    const sym_list::SymbolInfo& symbol,
    const QSet<QString>& packageScopes)
{
    SemanticMetadata metadata;
    metadata.declarationKind = declarationKind(symbol.symbolType);
    metadata.usageRole = usageRole(symbol.symbolType);
    metadata.ownerScope = legacyOwnerScope(symbol, packageScopes);
    metadata.visibility = legacyVisibility(symbol, packageScopes);
    metadata.sourceRole = sourceRoleForFileName(symbol.fileName);
    metadata.rawCollectorKind = rawCollectorKind(symbol.symbolType);
    metadata.interfaceLikeOwner = isInterfaceLikeOwner(symbol.symbolType);
    return metadata;
}

bool semanticMetadataMatchesRequest(
    const SemanticMetadata& metadata,
    sym_list::sym_type_e requestedType,
    const QString& dataType,
    bool parameterAlias)
{
    switch (requestedType) {
    case sym_list::sym_module:
        return metadata.declarationKind == DeclarationKind::Module;
    case sym_list::sym_interface:
    case sym_list::sym_interface_assco_struct:
        return metadata.declarationKind == DeclarationKind::Interface;
    case sym_list::sym_package:
        return metadata.declarationKind == DeclarationKind::Package;
    case sym_list::sym_typedef:
        return metadata.declarationKind == DeclarationKind::Typedef;
    case sym_list::sym_enum:
        return metadata.declarationKind == DeclarationKind::Enum
            || (metadata.declarationKind == DeclarationKind::Typedef
                && dataType == QLatin1String("enum"));
    case sym_list::sym_parameter:
        return metadata.declarationKind == DeclarationKind::Parameter
            || (parameterAlias
                && metadata.declarationKind == DeclarationKind::Localparam);
    case sym_list::sym_localparam:
        return metadata.declarationKind == DeclarationKind::Localparam;
    case sym_list::sym_reg:
    case sym_list::sym_wire:
    case sym_list::sym_logic:
        return metadata.declarationKind == DeclarationKind::Signal;
    case sym_list::sym_packed_struct:
    case sym_list::sym_unpacked_struct:
        return metadata.declarationKind == DeclarationKind::Struct;
    case sym_list::sym_packed_struct_var:
    case sym_list::sym_unpacked_struct_var:
        return metadata.declarationKind == DeclarationKind::StructVariable;
    case sym_list::sym_struct_member:
        return metadata.declarationKind == DeclarationKind::StructMember;
    case sym_list::sym_inst:
        return isInstanceDeclaration(metadata);
    case sym_list::sym_task:
        return metadata.declarationKind == DeclarationKind::Task;
    case sym_list::sym_function:
        return metadata.declarationKind == DeclarationKind::Function;
    case sym_list::sym_def_define:
        return metadata.declarationKind == DeclarationKind::Macro;
    default:
        return false;
    }
}

} // namespace

SemanticMetadata semanticMetadata(
    const sym_list::SymbolInfo& symbol,
    const QSet<QString>& packageScopes)
{
    if (symbol.hasSemanticMetadata) {
        SemanticMetadata metadata;
        metadata.declarationKind = symbol.semanticDeclarationKind;
        metadata.usageRole = symbol.semanticUsageRole;
        metadata.ownerScope = symbol.semanticOwnerScope;
        metadata.visibility = symbol.semanticVisibility;
        metadata.sourceRole = symbol.semanticSourceRole;
        metadata.rawCollectorKind = rawCollectorKind(symbol.rawCollectorKind);
        metadata.interfaceLikeOwner = symbol.interfaceLikeOwner;
        return metadata;
    }

    return computedSemanticMetadata(symbol, packageScopes);
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

bool isDefinitionCandidate(const SemanticMetadata& metadata)
{
    if (metadata.usageRole != SymbolUsageRole::Declaration)
        return false;
    if (isDefinitionCandidate(legacySymbolType(metadata.rawCollectorKind)))
        return true;
    if (hasRawCollectorKind(metadata))
        return false;

    switch (metadata.declarationKind) {
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
    case DeclarationKind::Instance:
    case DeclarationKind::Modport:
    case DeclarationKind::Task:
    case DeclarationKind::Function:
        return true;
    case DeclarationKind::Unknown:
    case DeclarationKind::Macro:
    case DeclarationKind::Process:
    case DeclarationKind::Generate:
    case DeclarationKind::Constraint:
    case DeclarationKind::User:
        break;
    }
    return false;
}

bool isGlobalDefinition(sym_list::sym_type_e type)
{
    return type == sym_list::sym_module
        || type == sym_list::sym_interface
        || type == sym_list::sym_package;
}

bool isGlobalDefinition(const SemanticMetadata& metadata)
{
    return metadata.declarationKind == DeclarationKind::Module
        || metadata.declarationKind == DeclarationKind::Interface
        || metadata.declarationKind == DeclarationKind::Package;
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

bool isPackageVisibleDefinition(const SemanticMetadata& metadata)
{
    return metadata.declarationKind == DeclarationKind::Parameter
        || metadata.declarationKind == DeclarationKind::Localparam
        || metadata.declarationKind == DeclarationKind::Typedef
        || metadata.declarationKind == DeclarationKind::Enum
        || metadata.declarationKind == DeclarationKind::Struct;
}

bool isInterfaceLikeOwner(sym_list::sym_type_e type)
{
    return type == sym_list::sym_interface
        || type == sym_list::sym_inst
        || type == sym_list::sym_port_interface
        || type == sym_list::sym_port_interface_modport;
}

QString interfaceTypeName(const QString& dataType)
{
    if (dataType.isEmpty())
        return QString();

    const int dot = dataType.indexOf(QLatin1Char('.'));
    return dot >= 0 ? dataType.left(dot) : dataType;
}

QString interfaceTypeName(const sym_list::SymbolInfo& symbol)
{
    const SemanticMetadata metadata = semanticMetadata(symbol);
    if (metadata.declarationKind == DeclarationKind::Interface)
        return symbol.symbolName;
    if (!metadata.interfaceLikeOwner
        && metadata.declarationKind != DeclarationKind::Instance) {
        return QString();
    }
    return interfaceTypeName(symbol.dataType);
}

QString interfaceModportName(const QString& dataType)
{
    const int dot = dataType.indexOf(QLatin1Char('.'));
    return dot >= 0 ? dataType.mid(dot + 1) : QString();
}

QString interfaceModportName(const sym_list::SymbolInfo& symbol)
{
    return interfaceModportName(symbol.dataType);
}

QString interfaceScopeFromOwner(const sym_list::SymbolInfo& symbol)
{
    if (!isInterfaceLikeOwner(symbol.symbolType))
        return QString();
    return interfaceTypeName(symbol);
}

bool isModuleDeclaration(sym_list::sym_type_e type)
{
    return declarationKind(type) == DeclarationKind::Module;
}

bool isPackageDeclaration(sym_list::sym_type_e type)
{
    return declarationKind(type) == DeclarationKind::Package;
}

bool isModuleDeclaration(const SemanticMetadata& metadata)
{
    return metadata.declarationKind == DeclarationKind::Module;
}

bool isPackageDeclaration(const SemanticMetadata& metadata)
{
    return metadata.declarationKind == DeclarationKind::Package;
}

bool isModuleDeclaration(const sym_list::SymbolInfo& symbol)
{
    return isModuleDeclaration(semanticMetadata(symbol));
}

bool isPackageDeclaration(const sym_list::SymbolInfo& symbol)
{
    return isPackageDeclaration(semanticMetadata(symbol));
}

bool isPortDeclaration(sym_list::sym_type_e type)
{
    return declarationKind(type) == DeclarationKind::Port;
}

bool isPortDeclaration(const SemanticMetadata& metadata)
{
    return metadata.declarationKind == DeclarationKind::Port;
}

bool isParameterDeclaration(sym_list::sym_type_e type)
{
    const DeclarationKind kind = declarationKind(type);
    return kind == DeclarationKind::Parameter
        || kind == DeclarationKind::Localparam;
}

bool isSignalDeclaration(sym_list::sym_type_e type)
{
    const DeclarationKind kind = declarationKind(type);
    return kind == DeclarationKind::Signal
        || kind == DeclarationKind::StructVariable
        || type == sym_list::sym_enum_var;
}

bool isSignalDeclaration(const SemanticMetadata& metadata)
{
    return metadata.declarationKind == DeclarationKind::Signal
        || metadata.declarationKind == DeclarationKind::StructVariable
        || rawCollectorKindIs(metadata, RawCollectorKind::EnumVariable);
}

bool isLogicDeclaration(sym_list::sym_type_e type)
{
    return type == sym_list::sym_logic;
}

bool isLogicDeclaration(const SemanticMetadata& metadata)
{
    return rawCollectorKindIs(metadata, RawCollectorKind::Logic);
}

bool isInstanceDeclaration(sym_list::sym_type_e type)
{
    return type == sym_list::sym_inst;
}

bool isInstanceDeclaration(const SemanticMetadata& metadata)
{
    return metadata.declarationKind == DeclarationKind::Instance
        && metadata.usageRole == SymbolUsageRole::Declaration;
}

bool isInstanceDeclaration(const sym_list::SymbolInfo& symbol)
{
    return isInstanceDeclaration(semanticMetadata(symbol));
}

bool isPortConnectionPeer(sym_list::sym_type_e type)
{
    return type == sym_list::sym_inst_pin
        || isPortDeclaration(type);
}

bool isPortConnectionPeer(const SemanticMetadata& metadata)
{
    return rawCollectorKindIs(metadata, RawCollectorKind::InstPin)
        || isPortDeclaration(metadata);
}

bool isFsmStateRegisterDeclaration(sym_list::sym_type_e type)
{
    return type == sym_list::sym_reg
        || type == sym_list::sym_logic
        || type == sym_list::sym_enum_var;
}

bool isFsmStateRegisterDeclaration(const SemanticMetadata& metadata)
{
    if (hasRawCollectorKind(metadata))
        return isFsmStateRegisterDeclaration(
            legacySymbolType(metadata.rawCollectorKind));
    return false;
}

bool isFsmStateValueDeclaration(sym_list::sym_type_e type)
{
    return type == sym_list::sym_enum_value
        || type == sym_list::sym_fsm_state;
}

bool isFsmStateValueDeclaration(const SemanticMetadata& metadata)
{
    if (hasRawCollectorKind(metadata))
        return isFsmStateValueDeclaration(
            legacySymbolType(metadata.rawCollectorKind));
    return false;
}

bool isSubroutineDeclaration(sym_list::sym_type_e type)
{
    const DeclarationKind kind = declarationKind(type);
    return kind == DeclarationKind::Task
        || kind == DeclarationKind::Function;
}

bool isSubroutineDeclaration(const SemanticMetadata& metadata)
{
    return metadata.declarationKind == DeclarationKind::Task
        || metadata.declarationKind == DeclarationKind::Function;
}

bool isModuleRangeType(sym_list::sym_type_e type)
{
    const DeclarationKind kind = declarationKind(type);
    return kind == DeclarationKind::Struct
        || kind == DeclarationKind::StructVariable;
}

bool isMemberScopeDefinitionCandidate(sym_list::sym_type_e type)
{
    const DeclarationKind kind = declarationKind(type);
    return kind == DeclarationKind::StructMember
        || kind == DeclarationKind::Modport;
}

bool isMemberScopeDefinitionCandidate(const SemanticMetadata& metadata)
{
    return metadata.declarationKind == DeclarationKind::StructMember
        || metadata.declarationKind == DeclarationKind::Modport;
}

bool isDirectModuleContextCompletionRequest(sym_list::sym_type_e requestedType)
{
    return isModuleRangeType(requestedType);
}

bool isPackageScopeVisibleCompletion(
    const sym_list::SymbolInfo& symbol,
    sym_list::sym_type_e requestedType,
    const QSet<QString>& packageScopes)
{
    return isPackageVisibleCommandRequest(requestedType)
        && legacyVisibility(symbol, packageScopes)
            == SymbolVisibility::PackageVisible;
}

bool isOutlineSymbol(sym_list::sym_type_e type)
{
    return outlineSymbolTypes().contains(type);
}

bool isOutlineSymbol(const SemanticMetadata& metadata)
{
    return isOutlineSymbol(outlineGroupType(metadata));
}

sym_list::sym_type_e outlineGroupType(const SemanticMetadata& metadata)
{
    const sym_list::sym_type_e rawKind =
        legacySymbolType(metadata.rawCollectorKind);
    if (isOutlineSymbol(rawKind))
        return rawKind;
    if (hasRawCollectorKind(metadata))
        return sym_list::sym_user;

    switch (metadata.declarationKind) {
    case DeclarationKind::Module:
        return sym_list::sym_module;
    case DeclarationKind::Interface:
        return sym_list::sym_interface;
    case DeclarationKind::Package:
        return sym_list::sym_package;
    case DeclarationKind::Typedef:
        return sym_list::sym_typedef;
    case DeclarationKind::Enum:
        return sym_list::sym_enum;
    case DeclarationKind::Parameter:
        return sym_list::sym_parameter;
    case DeclarationKind::Localparam:
        return sym_list::sym_localparam;
    case DeclarationKind::Port:
        return sym_list::sym_port_input;
    case DeclarationKind::Signal:
        return sym_list::sym_logic;
    case DeclarationKind::Struct:
        return sym_list::sym_packed_struct;
    case DeclarationKind::StructVariable:
        return sym_list::sym_packed_struct_var;
    case DeclarationKind::StructMember:
        return sym_list::sym_struct_member;
    case DeclarationKind::Instance:
        return sym_list::sym_inst;
    case DeclarationKind::Task:
        return sym_list::sym_task;
    case DeclarationKind::Function:
        return sym_list::sym_function;
    case DeclarationKind::Unknown:
    case DeclarationKind::Modport:
    case DeclarationKind::Macro:
    case DeclarationKind::Process:
    case DeclarationKind::Generate:
    case DeclarationKind::Constraint:
    case DeclarationKind::User:
        break;
    }
    return sym_list::sym_user;
}

bool matchesRequestedSymbolType(const SemanticMetadata& metadata,
                                sym_list::sym_type_e requestedType,
                                const QString& dataType)
{
    if (typedCompletionSymbolTypeMatches(
            legacySymbolType(metadata.rawCollectorKind),
            requestedType,
            dataType)) {
        return true;
    }
    if (hasRawCollectorKind(metadata))
        return false;
    return semanticMetadataMatchesRequest(
        metadata,
        requestedType,
        dataType,
        false);
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

int definitionPriority(const SemanticMetadata& metadata)
{
    if (hasRawCollectorKind(metadata))
        return definitionPriority(legacySymbolType(metadata.rawCollectorKind));

    switch (metadata.declarationKind) {
    case DeclarationKind::Module:
        return 0;
    case DeclarationKind::Interface:
        return 1;
    case DeclarationKind::Package:
        return 2;
    case DeclarationKind::Modport:
    case DeclarationKind::Port:
        return 3;
    case DeclarationKind::Task:
    case DeclarationKind::Function:
        return 4;
    case DeclarationKind::Signal:
    case DeclarationKind::StructVariable:
        return 5;
    case DeclarationKind::Parameter:
    case DeclarationKind::Localparam:
    case DeclarationKind::Typedef:
    case DeclarationKind::Enum:
    case DeclarationKind::Struct:
        return 6;
    case DeclarationKind::StructMember:
        return 7;
    case DeclarationKind::Unknown:
    case DeclarationKind::Instance:
    case DeclarationKind::Macro:
    case DeclarationKind::Process:
    case DeclarationKind::Generate:
    case DeclarationKind::Constraint:
    case DeclarationKind::User:
        break;
    }
    return 10;
}

QList<sym_list::sym_type_e> outlineSymbolTypes()
{
    return {
        sym_list::sym_module,
        sym_list::sym_parameter,
        sym_list::sym_localparam,
        sym_list::sym_port_input,
        sym_list::sym_port_output,
        sym_list::sym_port_inout,
        sym_list::sym_port_ref,
        sym_list::sym_reg,
        sym_list::sym_wire,
        sym_list::sym_logic,
        sym_list::sym_typedef,
        sym_list::sym_enum,
        sym_list::sym_enum_var,
        sym_list::sym_enum_value,
        sym_list::sym_packed_struct,
        sym_list::sym_unpacked_struct,
        sym_list::sym_packed_struct_var,
        sym_list::sym_unpacked_struct_var,
        sym_list::sym_struct_member,
        sym_list::sym_task,
        sym_list::sym_function,
        sym_list::sym_inst
    };
}

QString symbolTypeLabel(sym_list::sym_type_e type)
{
    switch (declarationKind(type)) {
    case DeclarationKind::Module:
        return QStringLiteral("module");
    case DeclarationKind::Interface:
        return QStringLiteral("interface");
    case DeclarationKind::Package:
        return QStringLiteral("package");
    case DeclarationKind::Typedef:
        return QStringLiteral("typedef");
    case DeclarationKind::Enum:
        return type == sym_list::sym_enum_value
            ? QStringLiteral("enum value")
            : QStringLiteral("enum");
    case DeclarationKind::Parameter:
        return QStringLiteral("parameter");
    case DeclarationKind::Localparam:
        return QStringLiteral("localparam");
    case DeclarationKind::Port:
        if (type == sym_list::sym_port_input)
            return QStringLiteral("input");
        if (type == sym_list::sym_port_output)
            return QStringLiteral("output");
        if (type == sym_list::sym_port_inout)
            return QStringLiteral("inout");
        if (type == sym_list::sym_port_ref)
            return QStringLiteral("ref");
        if (type == sym_list::sym_port_interface)
            return QStringLiteral("interface port");
        if (type == sym_list::sym_port_interface_modport)
            return QStringLiteral("modport port");
        return QStringLiteral("port");
    case DeclarationKind::Signal:
        if (type == sym_list::sym_reg)
            return QStringLiteral("reg");
        if (type == sym_list::sym_wire)
            return QStringLiteral("wire");
        return QStringLiteral("logic");
    case DeclarationKind::Struct:
        return QStringLiteral("struct");
    case DeclarationKind::StructVariable:
        return QStringLiteral("struct variable");
    case DeclarationKind::StructMember:
        return QStringLiteral("member");
    case DeclarationKind::Instance:
        return QStringLiteral("instance");
    case DeclarationKind::Modport:
        return QStringLiteral("modport");
    case DeclarationKind::Task:
        return QStringLiteral("task");
    case DeclarationKind::Function:
        return QStringLiteral("function");
    case DeclarationKind::Macro:
        return QStringLiteral("macro");
    case DeclarationKind::Process:
        return QStringLiteral("process");
    case DeclarationKind::Generate:
        return QStringLiteral("generate");
    case DeclarationKind::Constraint:
        return QStringLiteral("constraint");
    case DeclarationKind::User:
        return QStringLiteral("user");
    case DeclarationKind::Unknown:
        break;
    }
    return QStringLiteral("symbol");
}

QString symbolTypeLabel(const SemanticMetadata& metadata)
{
    if (hasRawCollectorKind(metadata)
        || metadata.declarationKind == DeclarationKind::User) {
        return symbolTypeLabel(legacySymbolType(metadata.rawCollectorKind));
    }

    switch (metadata.declarationKind) {
    case DeclarationKind::Module:
        return QStringLiteral("module");
    case DeclarationKind::Interface:
        return QStringLiteral("interface");
    case DeclarationKind::Package:
        return QStringLiteral("package");
    case DeclarationKind::Typedef:
        return QStringLiteral("typedef");
    case DeclarationKind::Enum:
        return QStringLiteral("enum");
    case DeclarationKind::Parameter:
        return QStringLiteral("parameter");
    case DeclarationKind::Localparam:
        return QStringLiteral("localparam");
    case DeclarationKind::Port:
        return QStringLiteral("port");
    case DeclarationKind::Signal:
        return QStringLiteral("signal");
    case DeclarationKind::Struct:
        return QStringLiteral("struct");
    case DeclarationKind::StructVariable:
        return QStringLiteral("struct variable");
    case DeclarationKind::StructMember:
        return QStringLiteral("member");
    case DeclarationKind::Instance:
        return QStringLiteral("instance");
    case DeclarationKind::Modport:
        return QStringLiteral("modport");
    case DeclarationKind::Task:
        return QStringLiteral("task");
    case DeclarationKind::Function:
        return QStringLiteral("function");
    case DeclarationKind::Macro:
        return QStringLiteral("macro");
    case DeclarationKind::Process:
        return QStringLiteral("process");
    case DeclarationKind::Generate:
        return QStringLiteral("generate");
    case DeclarationKind::Constraint:
        return QStringLiteral("constraint");
    case DeclarationKind::Unknown:
    case DeclarationKind::User:
        break;
    }
    return QStringLiteral("symbol");
}

bool matchesSearchIntent(sym_list::sym_type_e type, SymbolSearchIntent intent)
{
    sym_list::SymbolInfo symbol;
    symbol.symbolType = type;
    return matchesSearchIntent(semanticMetadata(symbol), intent);
}

bool matchesSearchIntent(const SemanticMetadata& metadata, SymbolSearchIntent intent)
{
    switch (intent) {
    case SymbolSearchIntent::Any:
        return true;
    case SymbolSearchIntent::DefinitionCandidates:
        return isDefinitionCandidate(metadata);
    case SymbolSearchIntent::ModuleDeclarations:
        return metadata.declarationKind == DeclarationKind::Module;
    case SymbolSearchIntent::GlobalDefinitions:
        return isGlobalDefinition(metadata);
    case SymbolSearchIntent::TypeDeclarations: {
        return metadata.declarationKind == DeclarationKind::Typedef
            || metadata.declarationKind == DeclarationKind::Enum
            || metadata.declarationKind == DeclarationKind::Struct;
    }
    case SymbolSearchIntent::OutlineSymbols:
        return isOutlineSymbol(metadata);
    case SymbolSearchIntent::SubroutineDeclarations:
        return metadata.declarationKind == DeclarationKind::Task
            || metadata.declarationKind == DeclarationKind::Function;
    }
    return false;
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

bool commandSymbolTypeMatches(const SemanticMetadata& metadata,
                              sym_list::sym_type_e commandType,
                              const QString& dataType)
{
    if (commandSymbolTypeMatches(
            legacySymbolType(metadata.rawCollectorKind),
            commandType,
            dataType)) {
        return true;
    }
    if (hasRawCollectorKind(metadata))
        return false;
    return semanticMetadataMatchesRequest(
        metadata,
        commandType,
        dataType,
        true);
}

bool isInternalCompletionCandidate(sym_list::sym_type_e type)
{
    return type == sym_list::sym_reg
        || type == sym_list::sym_wire
        || type == sym_list::sym_logic
        || type == sym_list::sym_localparam
        || type == sym_list::sym_parameter;
}

bool isInternalCompletionCandidate(const SemanticMetadata& metadata)
{
    if (isInternalCompletionCandidate(
            legacySymbolType(metadata.rawCollectorKind))) {
        return true;
    }
    if (hasRawCollectorKind(metadata))
        return false;
    return metadata.declarationKind == DeclarationKind::Signal
        || metadata.declarationKind == DeclarationKind::Parameter
        || metadata.declarationKind == DeclarationKind::Localparam;
}

bool isGlobalCompletionCandidate(sym_list::sym_type_e type)
{
    return type == sym_list::sym_module
        || type == sym_list::sym_task
        || type == sym_list::sym_function
        || type == sym_list::sym_interface
        || type == sym_list::sym_package;
}

bool isGlobalCompletionCandidate(const SemanticMetadata& metadata)
{
    if (isGlobalCompletionCandidate(
            legacySymbolType(metadata.rawCollectorKind))) {
        return true;
    }
    if (hasRawCollectorKind(metadata))
        return false;
    return metadata.declarationKind == DeclarationKind::Module
        || metadata.declarationKind == DeclarationKind::Task
        || metadata.declarationKind == DeclarationKind::Function
        || metadata.declarationKind == DeclarationKind::Interface
        || metadata.declarationKind == DeclarationKind::Package;
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

bool isCommandGlobalCompletionType(const SemanticMetadata& metadata)
{
    if (isCommandGlobalCompletionType(
            legacySymbolType(metadata.rawCollectorKind))) {
        return true;
    }
    if (hasRawCollectorKind(metadata))
        return false;
    return metadata.declarationKind == DeclarationKind::Module
        || metadata.declarationKind == DeclarationKind::Task
        || metadata.declarationKind == DeclarationKind::Function
        || metadata.declarationKind == DeclarationKind::Interface
        || metadata.declarationKind == DeclarationKind::Package
        || metadata.declarationKind == DeclarationKind::Typedef
        || metadata.declarationKind == DeclarationKind::Macro
        || metadata.declarationKind == DeclarationKind::Struct
        || metadata.declarationKind == DeclarationKind::Enum;
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

bool isGlobalSymbolInfoType(const SemanticMetadata& metadata)
{
    if (isGlobalSymbolInfoType(
            legacySymbolType(metadata.rawCollectorKind))) {
        return true;
    }
    if (hasRawCollectorKind(metadata))
        return false;
    return metadata.declarationKind == DeclarationKind::Module
        || metadata.declarationKind == DeclarationKind::Task
        || metadata.declarationKind == DeclarationKind::Function
        || metadata.declarationKind == DeclarationKind::Interface
        || metadata.declarationKind == DeclarationKind::Package
        || metadata.declarationKind == DeclarationKind::Typedef
        || metadata.declarationKind == DeclarationKind::Macro
        || metadata.declarationKind == DeclarationKind::Struct
        || metadata.declarationKind == DeclarationKind::StructVariable
        || metadata.declarationKind == DeclarationKind::Enum;
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

QSet<QString> packageScopeNames(const QList<sym_list::SymbolInfo>& symbols)
{
    QSet<QString> names;
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (isPackageDeclaration(symbol) && !symbol.symbolName.isEmpty())
            names.insert(symbol.symbolName);
    }
    return names;
}

bool isPackageScopeVisibleDefinition(
    const sym_list::SymbolInfo& symbol,
    const QSet<QString>& packageScopes)
{
    return legacyVisibility(symbol, packageScopes)
        == SymbolVisibility::PackageVisible;
}

bool isDefinitionVisibleInContext(
    const sym_list::SymbolInfo& symbol,
    const QString& moduleName,
    const QSet<QString>& packageScopes)
{
    const SemanticMetadata metadata = semanticMetadata(symbol, packageScopes);
    return isDefinitionVisibleInContext(metadata, symbol.moduleScope, moduleName);
}

bool isDefinitionVisibleInContext(
    const SemanticMetadata& metadata,
    const QString& ownerName,
    const QString& moduleName)
{
    return isMemberScopeDefinitionCandidate(metadata)
        || rawCollectorKindIs(metadata, RawCollectorKind::EnumValue)
        || isGlobalDefinition(metadata)
        || moduleName.isEmpty()
        || ownerName == moduleName
        || metadata.visibility == SymbolVisibility::PackageVisible;
}

int definitionContextPriorityAdjustment(
    const sym_list::SymbolInfo& symbol,
    const QString& moduleName,
    const QSet<QString>& packageScopes)
{
    if (!moduleName.isEmpty() && symbol.moduleScope == moduleName)
        return -100;
    if (isPackageScopeVisibleDefinition(symbol, packageScopes))
        return -20;
    return 0;
}

bool isSymbolInModuleScope(
    const sym_list::SymbolInfo& symbol,
    const QString& moduleName)
{
    return moduleName.isEmpty() || symbol.moduleScope == moduleName;
}

bool isSymbolInModuleContext(
    const sym_list::SymbolInfo& symbol,
    const QString& moduleName)
{
    return isSymbolInModuleScope(symbol, moduleName)
        || (isModuleDeclaration(symbol)
            && symbol.symbolName == moduleName);
}

bool isCommandCompletionScopeVisible(
    const sym_list::SymbolInfo& symbol,
    sym_list::sym_type_e requestedType,
    const QString& moduleName,
    const QSet<QString>& packageScopes)
{
    const bool useGlobalScope = moduleName.isEmpty()
        || isAlwaysGlobalCommandSymbolType(requestedType);
    if (useGlobalScope)
        return symbol.moduleScope.isEmpty();
    return symbol.moduleScope == moduleName
        || isPackageScopeVisibleCompletion(
            symbol,
            requestedType,
            packageScopes);
}

bool isGlobalSymbolInfoVisible(
    const sym_list::SymbolInfo& symbol,
    sym_list::sym_type_e requestedType)
{
    return isAlwaysGlobalSymbolInfoType(requestedType)
        || symbol.moduleScope.isEmpty();
}

bool typedCompletionSymbolTypeMatches(
    sym_list::sym_type_e symbolType,
    sym_list::sym_type_e requestedType,
    const QString& dataType)
{
    if (symbolType == requestedType)
        return true;
    return requestedType == sym_list::sym_enum
        && symbolType == sym_list::sym_typedef
        && dataType == QLatin1String("enum");
}

bool typedCompletionSymbolTypeMatches(
    const SemanticMetadata& metadata,
    sym_list::sym_type_e requestedType,
    const QString& dataType)
{
    if (typedCompletionSymbolTypeMatches(
            legacySymbolType(metadata.rawCollectorKind),
            requestedType,
            dataType)) {
        return true;
    }
    if (hasRawCollectorKind(metadata))
        return false;
    return semanticMetadataMatchesRequest(
        metadata,
        requestedType,
        dataType,
        false);
}

bool semanticCompletionKindMatches(const SemanticMetadata& metadata,
                                   SemanticCompletionKind kind,
                                   const QString& rawTypeText,
                                   bool parameterAlias)
{
    const bool semanticOnly = !hasRawCollectorKind(metadata);
    switch (kind) {
    case SemanticCompletionKind::Reg:
        return rawCollectorKindIs(metadata, RawCollectorKind::Reg)
            || (semanticOnly
                && metadata.declarationKind == DeclarationKind::Signal);
    case SemanticCompletionKind::Wire:
        return rawCollectorKindIs(metadata, RawCollectorKind::Wire)
            || (semanticOnly
                && metadata.declarationKind == DeclarationKind::Signal);
    case SemanticCompletionKind::Logic:
        return rawCollectorKindIs(metadata, RawCollectorKind::Logic)
            || (semanticOnly
                && metadata.declarationKind == DeclarationKind::Signal);
    case SemanticCompletionKind::Module:
        return metadata.declarationKind == DeclarationKind::Module;
    case SemanticCompletionKind::Task:
        return metadata.declarationKind == DeclarationKind::Task;
    case SemanticCompletionKind::Function:
        return metadata.declarationKind == DeclarationKind::Function;
    case SemanticCompletionKind::Interface:
        return metadata.declarationKind == DeclarationKind::Interface;
    case SemanticCompletionKind::Package:
        return metadata.declarationKind == DeclarationKind::Package;
    case SemanticCompletionKind::Macro:
        return metadata.declarationKind == DeclarationKind::Macro;
    case SemanticCompletionKind::Localparam:
        return rawCollectorKindIs(metadata, RawCollectorKind::Localparam)
            || (semanticOnly
                && metadata.declarationKind == DeclarationKind::Localparam);
    case SemanticCompletionKind::Parameter:
        return rawCollectorKindIs(metadata, RawCollectorKind::Parameter)
            || (parameterAlias
                && rawCollectorKindIs(metadata, RawCollectorKind::Localparam))
            || (semanticOnly
                && (metadata.declarationKind == DeclarationKind::Parameter
                    || (parameterAlias
                        && metadata.declarationKind
                            == DeclarationKind::Localparam)));
    case SemanticCompletionKind::AlwaysProcess:
        return rawCollectorKindIs(metadata, RawCollectorKind::Always)
            || rawCollectorKindIs(metadata, RawCollectorKind::AlwaysFf)
            || rawCollectorKindIs(metadata, RawCollectorKind::AlwaysComb)
            || rawCollectorKindIs(metadata, RawCollectorKind::AlwaysLatch)
            || (semanticOnly
                && metadata.declarationKind == DeclarationKind::Process);
    case SemanticCompletionKind::ContinuousAssign:
        return rawCollectorKindIs(metadata, RawCollectorKind::Assign)
            || (semanticOnly
                && metadata.declarationKind == DeclarationKind::Process);
    case SemanticCompletionKind::Typedef:
        return rawCollectorKindIs(metadata, RawCollectorKind::Typedef)
            || (semanticOnly
                && metadata.declarationKind == DeclarationKind::Typedef);
    case SemanticCompletionKind::EnumValue:
        return rawCollectorKindIs(metadata, RawCollectorKind::EnumValue);
    case SemanticCompletionKind::EnumType:
        return rawCollectorKindIs(metadata, RawCollectorKind::Enum)
            || (rawCollectorKindIs(metadata, RawCollectorKind::Typedef)
                && rawTypeText == QLatin1String("enum"))
            || (semanticOnly
                && metadata.declarationKind == DeclarationKind::Typedef
                && rawTypeText == QLatin1String("enum"));
    case SemanticCompletionKind::EnumVariable:
        return rawCollectorKindIs(metadata, RawCollectorKind::EnumVariable);
    case SemanticCompletionKind::StructMember:
        return rawCollectorKindIs(metadata, RawCollectorKind::StructMember)
            || (semanticOnly
                && metadata.declarationKind == DeclarationKind::StructMember);
    case SemanticCompletionKind::PackedStructType:
        return rawCollectorKindIs(metadata, RawCollectorKind::PackedStruct)
            || (semanticOnly
                && metadata.declarationKind == DeclarationKind::Struct);
    case SemanticCompletionKind::UnpackedStructType:
        return rawCollectorKindIs(metadata, RawCollectorKind::UnpackedStruct)
            || (semanticOnly
                && metadata.declarationKind == DeclarationKind::Struct);
    case SemanticCompletionKind::PackedStructVariable:
        return rawCollectorKindIs(
                   metadata,
                   RawCollectorKind::PackedStructVariable)
            || (semanticOnly
                && metadata.declarationKind == DeclarationKind::StructVariable);
    case SemanticCompletionKind::UnpackedStructVariable:
        return rawCollectorKindIs(
                   metadata,
                   RawCollectorKind::UnpackedStructVariable)
            || (semanticOnly
                && metadata.declarationKind == DeclarationKind::StructVariable);
    case SemanticCompletionKind::User:
        return metadata.declarationKind == DeclarationKind::User;
    }
    return false;
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
        || suffix == QLatin1String("v")
        || suffix == QLatin1String("vp")
        || suffix == QLatin1String("svp")) {
        return SourceRole::DesignSource;
    }
    return SourceRole::Unknown;
}

QString sourceRoleDisplayName(SourceRole role)
{
    switch (role) {
    case SourceRole::DesignSource:
        return QStringLiteral("design source");
    case SourceRole::Header:
        return QStringLiteral("header");
    case SourceRole::ExternalHeader:
        return QStringLiteral("external header");
    case SourceRole::Generated:
        return QStringLiteral("generated source");
    case SourceRole::Unknown:
    default:
        return QStringLiteral("source");
    }
}

bool isHeaderSourceRole(SourceRole role)
{
    return role == SourceRole::Header
        || role == SourceRole::ExternalHeader;
}

} // namespace SymbolTaxonomy
