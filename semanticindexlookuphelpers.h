#ifndef SEMANTICINDEXLOOKUPHELPERS_H
#define SEMANTICINDEXLOOKUPHELPERS_H

#include "semanticindex.h"

namespace semantic_index_lookup {

QString normalizedLookupFileName(const QString& fileName);

bool symbolSearchTypeMatches(const sym_list::SymbolInfo& symbol,
                             const QList<sym_list::sym_type_e>& types,
                             SymbolTaxonomy::SymbolSearchIntent intent);

bool semanticDefinitionSymbolMatches(const sym_list::SymbolInfo& symbol,
                                     const QString& searchWord);

int semanticDefinitionTypePriority(const sym_list::SymbolInfo& symbol);

bool semanticDefinitionSkipForStructMemberType(
    const sym_list::SymbolInfo& symbol,
    const SemanticDefinitionQuery& query);

int symbolSearchMatchScore(const QString& symbolName,
                           const SemanticSymbolSearchQuery& query);

} // namespace semantic_index_lookup

#endif // SEMANTICINDEXLOOKUPHELPERS_H
