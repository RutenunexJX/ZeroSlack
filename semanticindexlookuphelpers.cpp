#include "semanticindexlookuphelpers.h"

#include "symboltaxonomy.h"

#include <QDir>
#include <QFileInfo>

namespace semantic_index_lookup {

QString normalizedLookupFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

bool symbolSearchTypeMatches(sym_list::sym_type_e type,
                             const QList<sym_list::sym_type_e>& types,
                             SymbolTaxonomy::SymbolSearchIntent intent)
{
    if (!types.isEmpty())
        return types.contains(type);
    return SymbolTaxonomy::matchesSearchIntent(type, intent);
}

bool semanticDefinitionSymbolMatches(const sym_list::SymbolInfo& symbol,
                                     const QString& searchWord)
{
    if (symbol.symbolName != searchWord)
        return false;

    return SymbolTaxonomy::isDefinitionCandidate(
        SymbolTaxonomy::semanticMetadata(symbol));
}

int semanticDefinitionTypePriority(sym_list::sym_type_e type)
{
    sym_list::SymbolInfo symbol;
    symbol.symbolType = type;
    return SymbolTaxonomy::definitionPriority(
        SymbolTaxonomy::semanticMetadata(symbol));
}

bool semanticDefinitionSkipForStructMemberType(
    const sym_list::SymbolInfo& symbol,
    const SemanticDefinitionQuery& query)
{
    if (query.structTypeNameForMember.isEmpty())
        return false;
    const SymbolTaxonomy::SemanticMetadata metadata =
        SymbolTaxonomy::semanticMetadata(symbol);
    return (metadata.ownerScope == SymbolTaxonomy::SymbolOwnerScope::Interface
            || metadata.ownerScope == SymbolTaxonomy::SymbolOwnerScope::Struct)
        && symbol.moduleScope != query.structTypeNameForMember;
}

int symbolSearchMatchScore(const QString& symbolName,
                           const SemanticSymbolSearchQuery& query)
{
    if (symbolName.isEmpty())
        return 0;
    if (query.text.isEmpty())
        return 1;

    const Qt::CaseSensitivity sensitivity =
        query.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;

    if (QString::compare(symbolName, query.text, sensitivity) == 0)
        return 100;
    if (query.exactMatch)
        return 0;
    if (symbolName.startsWith(query.text, sensitivity))
        return 75;
    if (symbolName.contains(query.text, sensitivity))
        return 50;
    return 0;
}

} // namespace semantic_index_lookup
