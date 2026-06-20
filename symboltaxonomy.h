#ifndef SYMBOLTAXONOMY_H
#define SYMBOLTAXONOMY_H

#include "symbolsemanticmetadata.h"

#include <QList>
#include <QSet>
#include <QString>

namespace SymbolTaxonomy {

using DeclarationKind = SymbolSemanticMetadata::DeclarationKind;
using SourceRole = SymbolSemanticMetadata::SourceRole;
using SymbolOwnerScope = SymbolSemanticMetadata::SymbolOwnerScope;
using SymbolVisibility = SymbolSemanticMetadata::SymbolVisibility;
using SymbolUsageRole = SymbolSemanticMetadata::SymbolUsageRole;
using DeclarationGroup = SymbolSemanticMetadata::DeclarationGroup;
using CollectorKind = SymbolSemanticMetadata::CollectorKind;

enum class SymbolSearchIntent {
    Any,
    DefinitionCandidates,
    ModuleDeclarations,
    GlobalDefinitions,
    TypeDeclarations,
    OutlineSymbols,
    SubroutineDeclarations
};

enum class SemanticCompletionKind {
    User,
    Reg,
    Wire,
    Logic,
    Module,
    Task,
    Function,
    Interface,
    Package,
    Macro,
    Localparam,
    Parameter,
    AlwaysProcess,
    ContinuousAssign,
    Typedef,
    EnumValue,
    EnumType,
    EnumVariable,
    StructMember,
    PackedStructType,
    UnpackedStructType,
    PackedStructVariable,
    UnpackedStructVariable
};

struct SemanticMetadata {
    DeclarationKind declarationKind = DeclarationKind::Unknown;
    SymbolUsageRole usageRole = SymbolUsageRole::Unknown;
    SymbolOwnerScope ownerScope = SymbolOwnerScope::Unknown;
    SymbolVisibility visibility = SymbolVisibility::Unknown;
    SourceRole sourceRole = SourceRole::Unknown;
    CollectorKind collectorKind = CollectorKind::User;
    bool interfaceLikeOwner = false;
};

DeclarationGroup declarationGroup(const SemanticMetadata& metadata);

bool isDefinitionCandidate(const SemanticMetadata& metadata);
bool isGlobalDefinition(const SemanticMetadata& metadata);
bool isPackageVisibleDefinition(const SemanticMetadata& metadata);
QString interfaceTypeName(const QString& dataType);
QString interfaceModportName(const QString& dataType);
bool isModuleDeclaration(const SemanticMetadata& metadata);
bool isPackageDeclaration(const SemanticMetadata& metadata);
bool isPortDeclaration(const SemanticMetadata& metadata);
bool isSignalDeclaration(const SemanticMetadata& metadata);
bool isLogicDeclaration(const SemanticMetadata& metadata);
bool isInstanceDeclaration(const SemanticMetadata& metadata);
bool isPortConnectionPeer(const SemanticMetadata& metadata);
bool isFsmStateRegisterDeclaration(const SemanticMetadata& metadata);
bool isFsmStateValueDeclaration(const SemanticMetadata& metadata);
bool isSubroutineDeclaration(const SemanticMetadata& metadata);
bool isMemberScopeDefinitionCandidate(const SemanticMetadata& metadata);
bool isOutlineSymbol(const SemanticMetadata& metadata);
int definitionPriority(const SemanticMetadata& metadata);
QString symbolTypeLabel(const SemanticMetadata& metadata);
bool matchesSearchIntent(const SemanticMetadata& metadata, SymbolSearchIntent intent);

bool isInternalCompletionCandidate(const SemanticMetadata& metadata);
bool isGlobalCompletionCandidate(const SemanticMetadata& metadata);
bool isCommandGlobalCompletionType(const SemanticMetadata& metadata);
bool isGlobalSemanticSymbolType(const SemanticMetadata& metadata);
bool isDefinitionVisibleInContext(
    const SemanticMetadata& metadata,
    const QString& ownerName,
    const QString& moduleName);
bool semanticCompletionKindMatches(const SemanticMetadata& metadata,
                                   SemanticCompletionKind kind,
                                   const QString& rawTypeText = QString(),
                                   bool parameterAlias = false);

SourceRole sourceRoleForFileName(const QString& fileName);
QString sourceRoleDisplayName(SourceRole role);
bool isHeaderSourceRole(SourceRole role);

} // namespace SymbolTaxonomy

#endif // SYMBOLTAXONOMY_H
