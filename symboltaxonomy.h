#ifndef SYMBOLTAXONOMY_H
#define SYMBOLTAXONOMY_H

#include "syminfo.h"
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

enum class SymbolSearchIntent {
    Any,
    DefinitionCandidates,
    ModuleDeclarations,
    GlobalDefinitions,
    TypeDeclarations,
    OutlineSymbols,
    SubroutineDeclarations
};

struct SemanticMetadata {
    DeclarationKind declarationKind = DeclarationKind::Unknown;
    SymbolUsageRole usageRole = SymbolUsageRole::Unknown;
    SymbolOwnerScope ownerScope = SymbolOwnerScope::Unknown;
    SymbolVisibility visibility = SymbolVisibility::Unknown;
    SourceRole sourceRole = SourceRole::Unknown;
    sym_list::sym_type_e rawCollectorKind = sym_list::sym_user;
    bool interfaceLikeOwner = false;
};

DeclarationKind declarationKind(sym_list::sym_type_e type);
SymbolUsageRole usageRole(sym_list::sym_type_e type);
DeclarationGroup declarationGroup(sym_list::sym_type_e type);
DeclarationGroup declarationGroup(const SemanticMetadata& metadata);
SymbolOwnerScope ownerScope(
    const sym_list::SymbolInfo& symbol,
    const QSet<QString>& packageScopes = {});
SymbolVisibility visibility(
    const sym_list::SymbolInfo& symbol,
    const QSet<QString>& packageScopes = {});
SemanticMetadata semanticMetadata(
    const sym_list::SymbolInfo& symbol,
    const QSet<QString>& packageScopes = {});
void attachSemanticMetadata(
    sym_list::SymbolInfo* symbol,
    const QSet<QString>& packageScopes = {});
sym_list::SymbolInfo withSemanticMetadata(
    const sym_list::SymbolInfo& symbol,
    const QSet<QString>& packageScopes = {});

bool isDefinitionCandidate(sym_list::sym_type_e type);
bool isDefinitionCandidate(const SemanticMetadata& metadata);
bool isGlobalDefinition(sym_list::sym_type_e type);
bool isGlobalDefinition(const SemanticMetadata& metadata);
bool isPackageVisibleDefinition(sym_list::sym_type_e type);
bool isPackageVisibleDefinition(const SemanticMetadata& metadata);
bool isInterfaceLikeOwner(sym_list::sym_type_e type);
QString interfaceTypeName(const QString& dataType);
QString interfaceTypeName(const sym_list::SymbolInfo& symbol);
QString interfaceModportName(const QString& dataType);
QString interfaceModportName(const sym_list::SymbolInfo& symbol);
QString interfaceScopeFromOwner(const sym_list::SymbolInfo& symbol);
bool isModuleDeclaration(sym_list::sym_type_e type);
bool isPackageDeclaration(sym_list::sym_type_e type);
bool isModuleDeclaration(const SemanticMetadata& metadata);
bool isPackageDeclaration(const SemanticMetadata& metadata);
bool isModuleDeclaration(const sym_list::SymbolInfo& symbol);
bool isPackageDeclaration(const sym_list::SymbolInfo& symbol);
bool isPortDeclaration(sym_list::sym_type_e type);
bool isPortDeclaration(const SemanticMetadata& metadata);
bool isParameterDeclaration(sym_list::sym_type_e type);
bool isSignalDeclaration(sym_list::sym_type_e type);
bool isSignalDeclaration(const SemanticMetadata& metadata);
bool isLogicDeclaration(sym_list::sym_type_e type);
bool isLogicDeclaration(const SemanticMetadata& metadata);
bool isInstanceDeclaration(sym_list::sym_type_e type);
bool isInstanceDeclaration(const SemanticMetadata& metadata);
bool isInstanceDeclaration(const sym_list::SymbolInfo& symbol);
bool isPortConnectionPeer(sym_list::sym_type_e type);
bool isPortConnectionPeer(const SemanticMetadata& metadata);
bool isFsmStateRegisterDeclaration(sym_list::sym_type_e type);
bool isFsmStateRegisterDeclaration(const SemanticMetadata& metadata);
bool isFsmStateValueDeclaration(sym_list::sym_type_e type);
bool isFsmStateValueDeclaration(const SemanticMetadata& metadata);
bool isSubroutineDeclaration(sym_list::sym_type_e type);
bool isSubroutineDeclaration(const SemanticMetadata& metadata);
bool isModuleRangeType(sym_list::sym_type_e type);
bool isMemberScopeDefinitionCandidate(sym_list::sym_type_e type);
bool isMemberScopeDefinitionCandidate(const SemanticMetadata& metadata);
bool isDirectModuleContextCompletionRequest(sym_list::sym_type_e requestedType);
bool isPackageScopeVisibleCompletion(
    const sym_list::SymbolInfo& symbol,
    sym_list::sym_type_e requestedType,
    const QSet<QString>& packageScopes);
bool isOutlineSymbol(sym_list::sym_type_e type);
bool isOutlineSymbol(const SemanticMetadata& metadata);
sym_list::sym_type_e outlineGroupType(const SemanticMetadata& metadata);
bool matchesRequestedSymbolType(const SemanticMetadata& metadata,
                                sym_list::sym_type_e requestedType,
                                const QString& dataType = QString());
int definitionPriority(sym_list::sym_type_e type);
int definitionPriority(const SemanticMetadata& metadata);
QList<sym_list::sym_type_e> outlineSymbolTypes();
QString symbolTypeLabel(sym_list::sym_type_e type);
QString symbolTypeLabel(const SemanticMetadata& metadata);
bool matchesSearchIntent(sym_list::sym_type_e type, SymbolSearchIntent intent);
bool matchesSearchIntent(const SemanticMetadata& metadata, SymbolSearchIntent intent);

bool commandSymbolTypeMatches(sym_list::sym_type_e symbolType,
                              sym_list::sym_type_e commandType,
                              const QString& dataType = QString());
bool commandSymbolTypeMatches(const SemanticMetadata& metadata,
                              sym_list::sym_type_e commandType,
                              const QString& dataType = QString());
bool isInternalCompletionCandidate(sym_list::sym_type_e type);
bool isInternalCompletionCandidate(const SemanticMetadata& metadata);
bool isGlobalCompletionCandidate(sym_list::sym_type_e type);
bool isGlobalCompletionCandidate(const SemanticMetadata& metadata);
bool isCommandGlobalCompletionType(sym_list::sym_type_e type);
bool isCommandGlobalCompletionType(const SemanticMetadata& metadata);
bool isGlobalSymbolInfoType(sym_list::sym_type_e type);
bool isGlobalSymbolInfoType(const SemanticMetadata& metadata);
bool isAlwaysGlobalSymbolInfoType(sym_list::sym_type_e type);
bool isAlwaysGlobalCommandSymbolType(sym_list::sym_type_e type);
bool isPackageVisibleCommandRequest(sym_list::sym_type_e requestedType);
QSet<QString> packageScopeNames(const QList<sym_list::SymbolInfo>& symbols);
bool isPackageScopeVisibleDefinition(
    const sym_list::SymbolInfo& symbol,
    const QSet<QString>& packageScopes);
bool isDefinitionVisibleInContext(
    const sym_list::SymbolInfo& symbol,
    const QString& moduleName,
    const QSet<QString>& packageScopes);
bool isDefinitionVisibleInContext(
    const SemanticMetadata& metadata,
    const QString& ownerName,
    const QString& moduleName);
int definitionContextPriorityAdjustment(
    const sym_list::SymbolInfo& symbol,
    const QString& moduleName,
    const QSet<QString>& packageScopes);
bool isSymbolInModuleScope(
    const sym_list::SymbolInfo& symbol,
    const QString& moduleName);
bool isSymbolInModuleContext(
    const sym_list::SymbolInfo& symbol,
    const QString& moduleName);
bool isCommandCompletionScopeVisible(
    const sym_list::SymbolInfo& symbol,
    sym_list::sym_type_e requestedType,
    const QString& moduleName,
    const QSet<QString>& packageScopes);
bool isGlobalSymbolInfoVisible(
    const sym_list::SymbolInfo& symbol,
    sym_list::sym_type_e requestedType);
bool typedCompletionSymbolTypeMatches(
    sym_list::sym_type_e symbolType,
    sym_list::sym_type_e requestedType,
    const QString& dataType = QString());
bool typedCompletionSymbolTypeMatches(
    const SemanticMetadata& metadata,
    sym_list::sym_type_e requestedType,
    const QString& dataType = QString());

SourceRole sourceRoleForFileName(const QString& fileName);
QString sourceRoleDisplayName(SourceRole role);
bool isHeaderSourceRole(SourceRole role);

} // namespace SymbolTaxonomy

#endif // SYMBOLTAXONOMY_H
