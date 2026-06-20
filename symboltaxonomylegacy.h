#ifndef SYMBOLTAXONOMYLEGACY_H
#define SYMBOLTAXONOMYLEGACY_H

#include "symboltaxonomy.h"
#include "syminfo.h"

namespace SymbolTaxonomy {

RawCollectorKind rawCollectorKind(sym_list::sym_type_e type);
sym_list::sym_type_e legacySymbolType(RawCollectorKind kind);
DeclarationKind declarationKind(sym_list::sym_type_e type);
SymbolUsageRole usageRole(sym_list::sym_type_e type);
DeclarationGroup declarationGroup(sym_list::sym_type_e type);
SemanticMetadata semanticMetadata(
    const sym_list::SymbolInfo& symbol,
    const QSet<QString>& packageScopes = {});

bool isDefinitionCandidate(sym_list::sym_type_e type);
bool isGlobalDefinition(sym_list::sym_type_e type);
bool isPackageVisibleDefinition(sym_list::sym_type_e type);
bool isInterfaceLikeOwner(sym_list::sym_type_e type);
bool isModuleDeclaration(sym_list::sym_type_e type);
bool isPackageDeclaration(sym_list::sym_type_e type);
bool isPortDeclaration(sym_list::sym_type_e type);
bool isParameterDeclaration(sym_list::sym_type_e type);
bool isSignalDeclaration(sym_list::sym_type_e type);
bool isLogicDeclaration(sym_list::sym_type_e type);
bool isInstanceDeclaration(sym_list::sym_type_e type);
bool isPortConnectionPeer(sym_list::sym_type_e type);
bool isFsmStateRegisterDeclaration(sym_list::sym_type_e type);
bool isFsmStateValueDeclaration(sym_list::sym_type_e type);
bool isSubroutineDeclaration(sym_list::sym_type_e type);
bool isModuleRangeType(sym_list::sym_type_e type);
bool isMemberScopeDefinitionCandidate(sym_list::sym_type_e type);
bool isDirectModuleContextCompletionRequest(sym_list::sym_type_e requestedType);
int definitionPriority(sym_list::sym_type_e type);
QString symbolTypeLabel(sym_list::sym_type_e type);
bool matchesSearchIntent(sym_list::sym_type_e type, SymbolSearchIntent intent);

bool isInternalCompletionCandidate(sym_list::sym_type_e type);
bool isGlobalCompletionCandidate(sym_list::sym_type_e type);
bool isCommandGlobalCompletionType(sym_list::sym_type_e type);
bool isGlobalSymbolInfoType(sym_list::sym_type_e type);
bool isAlwaysGlobalSymbolInfoType(sym_list::sym_type_e type);
bool isAlwaysGlobalCommandSymbolType(sym_list::sym_type_e type);
bool isPackageVisibleCommandRequest(sym_list::sym_type_e requestedType);

} // namespace SymbolTaxonomy

#endif // SYMBOLTAXONOMYLEGACY_H
