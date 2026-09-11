#ifndef SEMANTICINDEXLOOKUPHELPERS_H
#define SEMANTICINDEXLOOKUPHELPERS_H

#include "semanticindex.h"

namespace semantic_index_lookup {

QString normalizedLookupFileName(const QString& fileName);

bool symbolSearchTypeMatches(const SemanticSymbolRecord& record,
                             const QList<SymbolTaxonomy::DeclarationKind>& declarationKinds,
                             SymbolTaxonomy::SymbolSearchIntent intent);

bool semanticDefinitionRecordMatches(const SemanticSymbolRecord& record,
                                     const QString& searchWord);

int semanticDefinitionTypePriority(const SemanticSymbolRecord& record);

bool semanticDefinitionSkipForStructMemberType(
    const SemanticSymbolRecord& record,
    const SemanticDefinitionQuery& query);

int symbolSearchMatchScore(const QString& symbolName,
                           const SemanticSymbolSearchQuery& query);

} // namespace semantic_index_lookup

#endif // SEMANTICINDEXLOOKUPHELPERS_H
