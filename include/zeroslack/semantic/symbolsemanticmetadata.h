#ifndef SYMBOLSEMANTICMETADATA_H
#define SYMBOLSEMANTICMETADATA_H

namespace SymbolSemanticMetadata {

enum class DeclarationKind {
    Unknown,
    Module,
    Interface,
    Package,
    Typedef,
    Enum,
    Parameter,
    Localparam,
    Port,
    Signal,
    Struct,
    StructVariable,
    StructMember,
    Instance,
    Modport,
    Task,
    Function,
    Macro,
    Process,
    Generate,
    Constraint,
    User
};

enum class SourceRole {
    Unknown,
    DesignSource,
    Header,
    ExternalHeader,
    Generated
};

enum class SymbolOwnerScope {
    Unknown,
    Global,
    Module,
    Interface,
    Package,
    Struct
};

enum class SymbolVisibility {
    Unknown,
    Global,
    ScopeLocal,
    PackageVisible,
    Member
};

enum class SymbolUsageRole {
    Unknown,
    Declaration,
    Reference,
    Process
};

enum class DeclarationGroup {
    Unknown,
    Port,
    Parameter,
    Instance,
    Signal
};

enum class CollectorKind {
    Reg,
    Wire,
    Logic,
    Interface,
    InterfaceAssocStruct,
    InterfaceParameter,
    InterfaceModport,
    Enum,
    EnumVariable,
    EnumValue,
    PackedStruct,
    UnpackedStruct,
    PackedStructVariable,
    UnpackedStructVariable,
    StructMember,
    Typedef,
    GenerateIf,
    GenerateFor,
    GenerateCase,
    Always,
    AlwaysFf,
    AlwaysComb,
    AlwaysLatch,
    Assign,
    DefIfdef,
    DefIfndef,
    DefElse,
    DefElsif,
    DefEndif,
    DefDefine,
    MacroReference,
    InactivePreprocessorBranch,
    DefParameter,
    Case,
    Casex,
    Casez,
    Endcase,
    CaseDefault,
    FsmState,
    Initial,
    Task,
    Function,
    XilinxConstraint,
    User,
    Localparam,
    Parameter,
    Module,
    ModuleParameter,
    Inst,
    InstPin,
    PortInput,
    PortOutput,
    PortInout,
    PortRef,
    PortInterface,
    PortInterfaceModport,
    Package,
    PackageImport
};

} // namespace SymbolSemanticMetadata

#endif // SYMBOLSEMANTICMETADATA_H
