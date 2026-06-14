#ifndef SEMANTICINDEXLOOKUPHELPERS_H
#define SEMANTICINDEXLOOKUPHELPERS_H

#include "semanticindex.h"

namespace semantic_index_lookup {

QString normalizedLookupFileName(const QString& fileName);

bool symbolSearchTypeMatches(sym_list::sym_type_e type,
                             const QList<sym_list::sym_type_e>& types,
                             SymbolTaxonomy::SymbolSearchIntent intent);

bool semanticDefinitionSymbolMatches(const sym_list::SymbolInfo& symbol,
                                     const QString& searchWord);

int semanticDefinitionTypePriority(sym_list::sym_type_e type);

bool semanticDefinitionInScope(const sym_list::SymbolInfo& symbol,
                               const SemanticDefinitionQuery& query);

bool semanticDefinitionSkipForStructMemberType(
    const sym_list::SymbolInfo& symbol,
    const SemanticDefinitionQuery& query);

int symbolSearchMatchScore(const QString& symbolName,
                           const SemanticSymbolSearchQuery& query);

} // namespace semantic_index_lookup

#endif // SEMANTICINDEXLOOKUPHELPERS_H
