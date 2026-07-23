#include "symboltaxonomy.h"

#include <QFileInfo>

namespace SymbolTaxonomy {

namespace {

bool isDefinitionCollectorKind(CollectorKind kind)
{
    switch (kind) {
    case CollectorKind::Module:
    case CollectorKind::Interface:
    case CollectorKind::InterfaceModport:
    case CollectorKind::Package:
    case CollectorKind::Inst:
    case CollectorKind::Task:
    case CollectorKind::Function:
    case CollectorKind::PortInput:
    case CollectorKind::PortOutput:
    case CollectorKind::PortInout:
    case CollectorKind::PortRef:
    case CollectorKind::PortInterface:
    case CollectorKind::PortInterfaceModport:
    case CollectorKind::Reg:
    case CollectorKind::Wire:
    case CollectorKind::Logic:
    case CollectorKind::Parameter:
    case CollectorKind::Localparam:
    case CollectorKind::PackedStruct:
    case CollectorKind::UnpackedStruct:
    case CollectorKind::PackedStructVariable:
    case CollectorKind::UnpackedStructVariable:
    case CollectorKind::StructMember:
    case CollectorKind::Typedef:
    case CollectorKind::Enum:
    case CollectorKind::EnumVariable:
    case CollectorKind::EnumValue:
    case CollectorKind::DefDefine:
        return true;
    case CollectorKind::InterfaceAssocStruct:
    case CollectorKind::InterfaceParameter:
    case CollectorKind::DefParameter:
    case CollectorKind::GenerateIf:
    case CollectorKind::GenerateFor:
    case CollectorKind::GenerateCase:
    case CollectorKind::Always:
    case CollectorKind::AlwaysFf:
    case CollectorKind::AlwaysComb:
    case CollectorKind::AlwaysLatch:
    case CollectorKind::Assign:
    case CollectorKind::DefIfdef:
    case CollectorKind::DefIfndef:
    case CollectorKind::DefElse:
    case CollectorKind::DefElsif:
    case CollectorKind::DefEndif:
    case CollectorKind::Case:
    case CollectorKind::Casex:
    case CollectorKind::Casez:
    case CollectorKind::Endcase:
    case CollectorKind::CaseDefault:
    case CollectorKind::FsmState:
    case CollectorKind::Initial:
    case CollectorKind::XilinxConstraint:
    case CollectorKind::User:
    case CollectorKind::ModuleParameter:
    case CollectorKind::InstPin:
    case CollectorKind::PackageImport:
    case CollectorKind::MacroReference:
    case CollectorKind::InactivePreprocessorBranch:
        return false;
    }
    return false;
}

int collectorKindDefinitionPriority(CollectorKind kind)
{
    switch (kind) {
    case CollectorKind::Module: return 0;
    case CollectorKind::Interface: return 1;
    case CollectorKind::Package: return 2;
    case CollectorKind::InterfaceModport:
    case CollectorKind::PortInput:
    case CollectorKind::PortOutput:
    case CollectorKind::PortInout:
    case CollectorKind::PortRef:
    case CollectorKind::PortInterface:
    case CollectorKind::PortInterfaceModport: return 3;
    case CollectorKind::Task:
    case CollectorKind::Function: return 4;
    case CollectorKind::Reg:
    case CollectorKind::Wire:
    case CollectorKind::Logic:
    case CollectorKind::PackedStructVariable:
    case CollectorKind::UnpackedStructVariable:
    case CollectorKind::EnumVariable: return 5;
    case CollectorKind::Parameter:
    case CollectorKind::Localparam:
    case CollectorKind::Enum:
    case CollectorKind::PackedStruct:
    case CollectorKind::UnpackedStruct:
    case CollectorKind::Typedef: return 6;
    case CollectorKind::StructMember:
    case CollectorKind::EnumValue: return 7;
    case CollectorKind::DefDefine: return 8;
    case CollectorKind::InterfaceAssocStruct:
    case CollectorKind::InterfaceParameter:
    case CollectorKind::DefParameter:
    case CollectorKind::GenerateIf:
    case CollectorKind::GenerateFor:
    case CollectorKind::GenerateCase:
    case CollectorKind::Always:
    case CollectorKind::AlwaysFf:
    case CollectorKind::AlwaysComb:
    case CollectorKind::AlwaysLatch:
    case CollectorKind::Assign:
    case CollectorKind::DefIfdef:
    case CollectorKind::DefIfndef:
    case CollectorKind::DefElse:
    case CollectorKind::DefElsif:
    case CollectorKind::DefEndif:
    case CollectorKind::Case:
    case CollectorKind::Casex:
    case CollectorKind::Casez:
    case CollectorKind::Endcase:
    case CollectorKind::CaseDefault:
    case CollectorKind::FsmState:
    case CollectorKind::Initial:
    case CollectorKind::XilinxConstraint:
    case CollectorKind::User:
    case CollectorKind::ModuleParameter:
    case CollectorKind::Inst:
    case CollectorKind::InstPin:
    case CollectorKind::PackageImport:
    case CollectorKind::MacroReference:
    case CollectorKind::InactivePreprocessorBranch:
        return 10;
    }
    return 10;
}

QString collectorKindLabel(CollectorKind kind)
{
    switch (kind) {
    case CollectorKind::Module:
        return QStringLiteral("module");
    case CollectorKind::Interface:
    case CollectorKind::InterfaceAssocStruct:
        return QStringLiteral("interface");
    case CollectorKind::Package:
        return QStringLiteral("package");
    case CollectorKind::PackageImport:
        return QStringLiteral("package import");
    case CollectorKind::Typedef:
        return QStringLiteral("typedef");
    case CollectorKind::EnumValue:
        return QStringLiteral("enum value");
    case CollectorKind::Enum:
    case CollectorKind::EnumVariable:
        return QStringLiteral("enum");
    case CollectorKind::Parameter:
    case CollectorKind::ModuleParameter:
    case CollectorKind::InterfaceParameter:
    case CollectorKind::DefParameter:
        return QStringLiteral("parameter");
    case CollectorKind::Localparam:
        return QStringLiteral("localparam");
    case CollectorKind::PortInput:
        return QStringLiteral("input");
    case CollectorKind::PortOutput:
        return QStringLiteral("output");
    case CollectorKind::PortInout:
        return QStringLiteral("inout");
    case CollectorKind::PortRef:
        return QStringLiteral("ref");
    case CollectorKind::PortInterface:
        return QStringLiteral("interface port");
    case CollectorKind::PortInterfaceModport:
        return QStringLiteral("modport port");
    case CollectorKind::Reg:
        return QStringLiteral("reg");
    case CollectorKind::Wire:
        return QStringLiteral("wire");
    case CollectorKind::Logic:
        return QStringLiteral("logic");
    case CollectorKind::PackedStruct:
    case CollectorKind::UnpackedStruct:
        return QStringLiteral("struct");
    case CollectorKind::PackedStructVariable:
    case CollectorKind::UnpackedStructVariable:
        return QStringLiteral("struct variable");
    case CollectorKind::StructMember:
        return QStringLiteral("member");
    case CollectorKind::Inst:
    case CollectorKind::InstPin:
        return QStringLiteral("instance");
    case CollectorKind::InterfaceModport:
        return QStringLiteral("modport");
    case CollectorKind::Task:
        return QStringLiteral("task");
    case CollectorKind::Function:
        return QStringLiteral("function");
    case CollectorKind::DefDefine:
    case CollectorKind::MacroReference:
    case CollectorKind::DefIfdef:
    case CollectorKind::DefIfndef:
    case CollectorKind::DefElse:
    case CollectorKind::DefElsif:
    case CollectorKind::DefEndif:
        return QStringLiteral("macro");
    case CollectorKind::InactivePreprocessorBranch:
        return QStringLiteral("inactive preprocessor branch");
    case CollectorKind::Always:
    case CollectorKind::AlwaysFf:
    case CollectorKind::AlwaysComb:
    case CollectorKind::AlwaysLatch:
    case CollectorKind::Assign:
    case CollectorKind::Initial:
    case CollectorKind::Case:
    case CollectorKind::Casex:
    case CollectorKind::Casez:
    case CollectorKind::Endcase:
    case CollectorKind::CaseDefault:
    case CollectorKind::FsmState:
        return QStringLiteral("process");
    case CollectorKind::GenerateIf:
    case CollectorKind::GenerateFor:
    case CollectorKind::GenerateCase:
        return QStringLiteral("generate");
    case CollectorKind::XilinxConstraint:
        return QStringLiteral("constraint");
    case CollectorKind::User:
        return QStringLiteral("user");
    }
    return QStringLiteral("symbol");
}

bool isInternalCompletionCollectorKind(CollectorKind kind)
{
    return kind == CollectorKind::Reg
        || kind == CollectorKind::Wire
        || kind == CollectorKind::Logic
        || kind == CollectorKind::Localparam
        || kind == CollectorKind::Parameter;
}

bool isGlobalCompletionCollectorKind(CollectorKind kind)
{
    return kind == CollectorKind::Module
        || kind == CollectorKind::Task
        || kind == CollectorKind::Function
        || kind == CollectorKind::Interface
        || kind == CollectorKind::Package;
}

bool isCommandGlobalCollectorKind(CollectorKind kind)
{
    return kind == CollectorKind::Module
        || kind == CollectorKind::Task
        || kind == CollectorKind::Function
        || kind == CollectorKind::Interface
        || kind == CollectorKind::Package
        || kind == CollectorKind::Typedef
        || kind == CollectorKind::DefDefine
        || kind == CollectorKind::PackedStruct
        || kind == CollectorKind::UnpackedStruct
        || kind == CollectorKind::Enum;
}

bool isGlobalSymbolCollectorKind(CollectorKind kind)
{
    return kind == CollectorKind::Module
        || kind == CollectorKind::Task
        || kind == CollectorKind::Function
        || kind == CollectorKind::Interface
        || kind == CollectorKind::Package
        || kind == CollectorKind::Typedef
        || kind == CollectorKind::DefDefine
        || kind == CollectorKind::PackedStruct
        || kind == CollectorKind::UnpackedStruct
        || kind == CollectorKind::PackedStructVariable
        || kind == CollectorKind::UnpackedStructVariable
        || kind == CollectorKind::Enum;
}

bool isFsmStateRegisterCollectorKind(CollectorKind kind)
{
    return kind == CollectorKind::Reg
        || kind == CollectorKind::Logic
        || kind == CollectorKind::EnumVariable;
}

bool isFsmStateValueCollectorKind(CollectorKind kind)
{
    return kind == CollectorKind::EnumValue
        || kind == CollectorKind::FsmState;
}

bool collectorKindIs(const SemanticMetadata& metadata,
                        CollectorKind kind)
{
    return metadata.collectorKind == kind;
}

bool hasCollectorKind(const SemanticMetadata& metadata)
{
    return !collectorKindIs(metadata, CollectorKind::User);
}

bool isOutlineCollectorKind(CollectorKind kind)
{
    switch (kind) {
    case CollectorKind::Module:
    case CollectorKind::Parameter:
    case CollectorKind::Localparam:
    case CollectorKind::PortInput:
    case CollectorKind::PortOutput:
    case CollectorKind::PortInout:
    case CollectorKind::PortRef:
    case CollectorKind::Reg:
    case CollectorKind::Wire:
    case CollectorKind::Logic:
    case CollectorKind::Typedef:
    case CollectorKind::Enum:
    case CollectorKind::EnumVariable:
    case CollectorKind::EnumValue:
    case CollectorKind::PackedStruct:
    case CollectorKind::UnpackedStruct:
    case CollectorKind::PackedStructVariable:
    case CollectorKind::UnpackedStructVariable:
    case CollectorKind::StructMember:
    case CollectorKind::Task:
    case CollectorKind::Function:
    case CollectorKind::Inst:
    case CollectorKind::DefDefine:
        return true;
    case CollectorKind::User:
    case CollectorKind::Interface:
    case CollectorKind::InterfaceAssocStruct:
    case CollectorKind::InterfaceParameter:
    case CollectorKind::Package:
    case CollectorKind::DefIfdef:
    case CollectorKind::DefIfndef:
    case CollectorKind::DefElse:
    case CollectorKind::DefElsif:
    case CollectorKind::DefEndif:
    case CollectorKind::DefParameter:
    case CollectorKind::InstPin:
    case CollectorKind::InterfaceModport:
    case CollectorKind::PortInterface:
    case CollectorKind::PortInterfaceModport:
    case CollectorKind::GenerateIf:
    case CollectorKind::GenerateFor:
    case CollectorKind::GenerateCase:
    case CollectorKind::Always:
    case CollectorKind::AlwaysFf:
    case CollectorKind::AlwaysComb:
    case CollectorKind::AlwaysLatch:
    case CollectorKind::Assign:
    case CollectorKind::Case:
    case CollectorKind::Casex:
    case CollectorKind::Casez:
    case CollectorKind::Endcase:
    case CollectorKind::CaseDefault:
    case CollectorKind::FsmState:
    case CollectorKind::Initial:
    case CollectorKind::XilinxConstraint:
    case CollectorKind::ModuleParameter:
    case CollectorKind::PackageImport:
    case CollectorKind::MacroReference:
    case CollectorKind::InactivePreprocessorBranch:
        return false;
    }
    return false;
}

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
        return collectorKindIs(metadata, CollectorKind::EnumVariable)
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

bool isDefinitionCandidate(const SemanticMetadata& metadata)
{
    if (metadata.usageRole != SymbolUsageRole::Declaration)
        return false;
    if (isDefinitionCollectorKind(metadata.collectorKind))
        return true;
    if (hasCollectorKind(metadata))
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
    case DeclarationKind::Macro:
        return true;
    case DeclarationKind::Unknown:
    case DeclarationKind::Process:
    case DeclarationKind::Generate:
    case DeclarationKind::Constraint:
    case DeclarationKind::User:
        break;
    }
    return false;
}

bool isGlobalDefinition(const SemanticMetadata& metadata)
{
    return metadata.declarationKind == DeclarationKind::Module
        || metadata.declarationKind == DeclarationKind::Interface
        || metadata.declarationKind == DeclarationKind::Package
        || metadata.declarationKind == DeclarationKind::Macro;
}

bool isPackageVisibleDefinition(const SemanticMetadata& metadata)
{
    return metadata.declarationKind == DeclarationKind::Parameter
        || metadata.declarationKind == DeclarationKind::Localparam
        || metadata.declarationKind == DeclarationKind::Typedef
        || metadata.declarationKind == DeclarationKind::Enum
        || metadata.declarationKind == DeclarationKind::Struct;
}

QString interfaceTypeName(const QString& dataType)
{
    if (dataType.isEmpty())
        return QString();

    const int dot = dataType.indexOf(QLatin1Char('.'));
    return dot >= 0 ? dataType.left(dot) : dataType;
}

QString interfaceModportName(const QString& dataType)
{
    const int dot = dataType.indexOf(QLatin1Char('.'));
    return dot >= 0 ? dataType.mid(dot + 1) : QString();
}

bool isModuleDeclaration(const SemanticMetadata& metadata)
{
    return metadata.declarationKind == DeclarationKind::Module;
}

bool isPackageDeclaration(const SemanticMetadata& metadata)
{
    return metadata.declarationKind == DeclarationKind::Package;
}

bool isPortDeclaration(const SemanticMetadata& metadata)
{
    return metadata.declarationKind == DeclarationKind::Port;
}

bool isSignalDeclaration(const SemanticMetadata& metadata)
{
    return metadata.declarationKind == DeclarationKind::Signal
        || metadata.declarationKind == DeclarationKind::StructVariable
        || collectorKindIs(metadata, CollectorKind::EnumVariable);
}

bool isLogicDeclaration(const SemanticMetadata& metadata)
{
    return collectorKindIs(metadata, CollectorKind::Logic);
}

bool isInstanceDeclaration(const SemanticMetadata& metadata)
{
    return metadata.declarationKind == DeclarationKind::Instance
        && metadata.usageRole == SymbolUsageRole::Declaration;
}

bool isPortConnectionPeer(const SemanticMetadata& metadata)
{
    return collectorKindIs(metadata, CollectorKind::InstPin)
        || isPortDeclaration(metadata);
}

bool isFsmStateRegisterDeclaration(const SemanticMetadata& metadata)
{
    if (hasCollectorKind(metadata))
        return isFsmStateRegisterCollectorKind(metadata.collectorKind);
    return false;
}

bool isFsmStateValueDeclaration(const SemanticMetadata& metadata)
{
    if (hasCollectorKind(metadata))
        return isFsmStateValueCollectorKind(metadata.collectorKind);
    return false;
}

bool isSubroutineDeclaration(const SemanticMetadata& metadata)
{
    return metadata.declarationKind == DeclarationKind::Task
        || metadata.declarationKind == DeclarationKind::Function;
}

bool isMemberScopeDefinitionCandidate(const SemanticMetadata& metadata)
{
    return metadata.declarationKind == DeclarationKind::StructMember
        || metadata.declarationKind == DeclarationKind::Modport;
}

bool isOutlineSymbol(const SemanticMetadata& metadata)
{
    if (hasCollectorKind(metadata))
        return isOutlineCollectorKind(metadata.collectorKind);

    switch (metadata.declarationKind) {
    case DeclarationKind::Module:
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
    case DeclarationKind::Task:
    case DeclarationKind::Function:
    case DeclarationKind::Macro:
        return true;
    case DeclarationKind::Unknown:
    case DeclarationKind::Interface:
    case DeclarationKind::Package:
    case DeclarationKind::Modport:
    case DeclarationKind::Process:
    case DeclarationKind::Generate:
    case DeclarationKind::Constraint:
    case DeclarationKind::User:
        return false;
    }
    return false;
}

int definitionPriority(const SemanticMetadata& metadata)
{
    if (hasCollectorKind(metadata))
        return collectorKindDefinitionPriority(metadata.collectorKind);

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
    case DeclarationKind::Macro:
        return 8;
    case DeclarationKind::Unknown:
    case DeclarationKind::Instance:
    case DeclarationKind::Process:
    case DeclarationKind::Generate:
    case DeclarationKind::Constraint:
    case DeclarationKind::User:
        break;
    }
    return 10;
}

QString symbolTypeLabel(const SemanticMetadata& metadata)
{
    if (hasCollectorKind(metadata)
        || metadata.declarationKind == DeclarationKind::User) {
        return collectorKindLabel(metadata.collectorKind);
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

bool isInternalCompletionCandidate(const SemanticMetadata& metadata)
{
    if (isInternalCompletionCollectorKind(metadata.collectorKind)) {
        return true;
    }
    if (hasCollectorKind(metadata))
        return false;
    return metadata.declarationKind == DeclarationKind::Signal
        || metadata.declarationKind == DeclarationKind::Parameter
        || metadata.declarationKind == DeclarationKind::Localparam;
}

bool isGlobalCompletionCandidate(const SemanticMetadata& metadata)
{
    if (isGlobalCompletionCollectorKind(metadata.collectorKind)) {
        return true;
    }
    if (hasCollectorKind(metadata))
        return false;
    return metadata.declarationKind == DeclarationKind::Module
        || metadata.declarationKind == DeclarationKind::Task
        || metadata.declarationKind == DeclarationKind::Function
        || metadata.declarationKind == DeclarationKind::Interface
        || metadata.declarationKind == DeclarationKind::Package;
}

bool isCommandGlobalCompletionType(const SemanticMetadata& metadata)
{
    if (isCommandGlobalCollectorKind(metadata.collectorKind)) {
        return true;
    }
    if (hasCollectorKind(metadata))
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

bool isGlobalSemanticSymbolType(const SemanticMetadata& metadata)
{
    if (isGlobalSymbolCollectorKind(metadata.collectorKind)) {
        return true;
    }
    if (hasCollectorKind(metadata))
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

bool isDefinitionVisibleInContext(
    const SemanticMetadata& metadata,
    const QString& ownerName,
    const QString& moduleName)
{
    return isMemberScopeDefinitionCandidate(metadata)
        || collectorKindIs(metadata, CollectorKind::EnumValue)
        || isGlobalDefinition(metadata)
        || moduleName.isEmpty()
        || ownerName == moduleName;
}

bool semanticCompletionKindMatches(const SemanticMetadata& metadata,
                                   SemanticCompletionKind kind,
                                   const QString& rawTypeText,
                                   bool parameterAlias)
{
    const bool semanticOnly = !hasCollectorKind(metadata);
    switch (kind) {
    case SemanticCompletionKind::Reg:
        return collectorKindIs(metadata, CollectorKind::Reg)
            || (semanticOnly
                && metadata.declarationKind == DeclarationKind::Signal);
    case SemanticCompletionKind::Wire:
        return collectorKindIs(metadata, CollectorKind::Wire)
            || (semanticOnly
                && metadata.declarationKind == DeclarationKind::Signal);
    case SemanticCompletionKind::Logic:
        return collectorKindIs(metadata, CollectorKind::Logic)
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
        return collectorKindIs(metadata, CollectorKind::Localparam)
            || (semanticOnly
                && metadata.declarationKind == DeclarationKind::Localparam);
    case SemanticCompletionKind::Parameter:
        return collectorKindIs(metadata, CollectorKind::Parameter)
            || (parameterAlias
                && collectorKindIs(metadata, CollectorKind::Localparam))
            || (semanticOnly
                && (metadata.declarationKind == DeclarationKind::Parameter
                    || (parameterAlias
                        && metadata.declarationKind
                            == DeclarationKind::Localparam)));
    case SemanticCompletionKind::AlwaysProcess:
        return collectorKindIs(metadata, CollectorKind::Always)
            || collectorKindIs(metadata, CollectorKind::AlwaysFf)
            || collectorKindIs(metadata, CollectorKind::AlwaysComb)
            || collectorKindIs(metadata, CollectorKind::AlwaysLatch)
            || (semanticOnly
                && metadata.declarationKind == DeclarationKind::Process);
    case SemanticCompletionKind::ContinuousAssign:
        return collectorKindIs(metadata, CollectorKind::Assign)
            || (semanticOnly
                && metadata.declarationKind == DeclarationKind::Process);
    case SemanticCompletionKind::Typedef:
        return collectorKindIs(metadata, CollectorKind::Typedef)
            || (semanticOnly
                && metadata.declarationKind == DeclarationKind::Typedef);
    case SemanticCompletionKind::EnumValue:
        return collectorKindIs(metadata, CollectorKind::EnumValue);
    case SemanticCompletionKind::EnumType:
        return collectorKindIs(metadata, CollectorKind::Enum)
            || (collectorKindIs(metadata, CollectorKind::Typedef)
                && rawTypeText == QLatin1String("enum"))
            || (semanticOnly
                && metadata.declarationKind == DeclarationKind::Typedef
                && rawTypeText == QLatin1String("enum"));
    case SemanticCompletionKind::EnumVariable:
        return collectorKindIs(metadata, CollectorKind::EnumVariable);
    case SemanticCompletionKind::StructMember:
        return collectorKindIs(metadata, CollectorKind::StructMember)
            || (semanticOnly
                && metadata.declarationKind == DeclarationKind::StructMember);
    case SemanticCompletionKind::PackedStructType:
        return collectorKindIs(metadata, CollectorKind::PackedStruct)
            || (semanticOnly
                && metadata.declarationKind == DeclarationKind::Struct);
    case SemanticCompletionKind::UnpackedStructType:
        return collectorKindIs(metadata, CollectorKind::UnpackedStruct)
            || (semanticOnly
                && metadata.declarationKind == DeclarationKind::Struct);
    case SemanticCompletionKind::PackedStructVariable:
        return collectorKindIs(
                   metadata,
                   CollectorKind::PackedStructVariable)
            || (semanticOnly
                && metadata.declarationKind == DeclarationKind::StructVariable);
    case SemanticCompletionKind::UnpackedStructVariable:
        return collectorKindIs(
                   metadata,
                   CollectorKind::UnpackedStructVariable)
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
