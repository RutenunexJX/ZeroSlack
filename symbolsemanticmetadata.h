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

} // namespace SymbolSemanticMetadata

#endif // SYMBOLSEMANTICMETADATA_H
