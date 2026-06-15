#ifndef SEMANTICINDEXCOMPLETIONFILTERS_H
#define SEMANTICINDEXCOMPLETIONFILTERS_H

#include "symboltaxonomy.h"

#include <QList>
#include <QString>
#include <algorithm>

namespace semantic_index_completion {

inline bool commandSymbolTypeMatches(sym_list::sym_type_e symbolType,
                                     sym_list::sym_type_e commandType,
                                     const QString& dataType = QString())
{
    return SymbolTaxonomy::commandSymbolTypeMatches(symbolType, commandType, dataType);
}

inline bool semanticCompletionNameMatches(const QString& name, const QString& prefix)
{
    if (prefix.isEmpty())
        return true;
    if (name.isEmpty())
        return false;

    const QString lowerName = name.toLower();
    const QString lowerPrefix = prefix.toLower();
    if (lowerName.startsWith(lowerPrefix))
        return true;

    int namePos = 0;
    int prefixPos = 0;
    while (prefixPos < lowerPrefix.length() && namePos < lowerName.length()) {
        if (lowerPrefix.at(prefixPos) == lowerName.at(namePos))
            ++prefixPos;
        ++namePos;
    }
    return prefixPos == lowerPrefix.length();
}

inline bool internalCompletionSymbolType(sym_list::sym_type_e type)
{
    return SymbolTaxonomy::isInternalCompletionCandidate(type);
}

inline bool internalCompletionSymbol(
    const sym_list::SymbolInfo& symbol)
{
    return SymbolTaxonomy::isInternalCompletionCandidate(
        SymbolTaxonomy::semanticMetadata(symbol));
}

inline bool globalCompletionSymbolType(sym_list::sym_type_e type)
{
    return SymbolTaxonomy::isGlobalCompletionCandidate(type);
}

inline bool globalCompletionSymbol(
    const sym_list::SymbolInfo& symbol)
{
    return SymbolTaxonomy::isGlobalCompletionCandidate(
        SymbolTaxonomy::semanticMetadata(symbol));
}

inline bool commandGlobalCompletionSymbolType(sym_list::sym_type_e type)
{
    return SymbolTaxonomy::isCommandGlobalCompletionType(type);
}

inline bool globalSymbolInfoType(sym_list::sym_type_e type)
{
    return SymbolTaxonomy::isGlobalSymbolInfoType(type);
}

inline bool globalSymbolInfoMetadata(
    const SymbolTaxonomy::SemanticMetadata& metadata)
{
    return SymbolTaxonomy::isGlobalSymbolInfoType(metadata);
}

inline bool alwaysGlobalSymbolInfoType(sym_list::sym_type_e type)
{
    return SymbolTaxonomy::isAlwaysGlobalSymbolInfoType(type);
}

inline bool alwaysGlobalCommandSymbolType(sym_list::sym_type_e type)
{
    return SymbolTaxonomy::isAlwaysGlobalCommandSymbolType(type);
}

inline void sortSymbolsByName(QList<sym_list::SymbolInfo>& symbols)
{
    std::sort(symbols.begin(), symbols.end(),
              [](const sym_list::SymbolInfo& left,
                 const sym_list::SymbolInfo& right) {
                  return QString::compare(left.symbolName,
                                          right.symbolName,
                                          Qt::CaseInsensitive) < 0;
              });
}

} // namespace semantic_index_completion

#endif // SEMANTICINDEXCOMPLETIONFILTERS_H
