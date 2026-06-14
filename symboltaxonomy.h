#ifndef SYMBOLTAXONOMY_H
#define SYMBOLTAXONOMY_H

#include "syminfo.h"

#include <QList>
#include <QString>

namespace SymbolTaxonomy {

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
    Header
};

DeclarationKind declarationKind(sym_list::sym_type_e type);

bool isDefinitionCandidate(sym_list::sym_type_e type);
bool isGlobalDefinition(sym_list::sym_type_e type);
bool isPackageVisibleDefinition(sym_list::sym_type_e type);
bool isInterfaceLikeOwner(sym_list::sym_type_e type);
bool isModuleDeclaration(sym_list::sym_type_e type);
bool isSubroutineDeclaration(sym_list::sym_type_e type);
int definitionPriority(sym_list::sym_type_e type);
QList<sym_list::sym_type_e> outlineSymbolTypes();

bool commandSymbolTypeMatches(sym_list::sym_type_e symbolType,
                              sym_list::sym_type_e commandType,
                              const QString& dataType = QString());
bool isInternalCompletionCandidate(sym_list::sym_type_e type);
bool isGlobalCompletionCandidate(sym_list::sym_type_e type);
bool isCommandGlobalCompletionType(sym_list::sym_type_e type);
bool isGlobalSymbolInfoType(sym_list::sym_type_e type);
bool isAlwaysGlobalSymbolInfoType(sym_list::sym_type_e type);
bool isAlwaysGlobalCommandSymbolType(sym_list::sym_type_e type);
bool isPackageVisibleCommandRequest(sym_list::sym_type_e requestedType);

SourceRole sourceRoleForFileName(const QString& fileName);
bool isHeaderSourceRole(SourceRole role);

} // namespace SymbolTaxonomy

#endif // SYMBOLTAXONOMY_H
