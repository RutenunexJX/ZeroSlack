#include "semanticindex.h"
#include "semanticindexcompletionfilters.h"

#include <QSet>

using namespace semantic_index_completion;

QList<sym_list::SymbolInfo> SemanticIndex::getModuleCompletionSymbols(
    const QString& moduleName,
    const QString& prefix) const
{
    QList<sym_list::SymbolInfo> result;
    QSet<QString> seenNames;
    if (moduleName.isEmpty())
        return result;

    const QList<sym_list::SymbolInfo> symbols = getSymbols();
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (symbol.moduleScope != moduleName
            || !internalCompletionSymbol(symbol)
            || !semanticCompletionNameMatches(symbol.symbolName, prefix)) {
            continue;
        }

        const QString key = symbol.symbolName.toCaseFolded();
        if (seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(symbol);
    }

    sortSymbolsByName(result);
    return result;
}

QList<sym_list::SymbolInfo> SemanticIndex::getGlobalCompletionSymbols(
    const QString& prefix) const
{
    QList<sym_list::SymbolInfo> result;
    QSet<QString> seenNames;
    const QList<sym_list::SymbolInfo> symbols = getSymbols();
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (!globalCompletionSymbol(symbol)
            || !semanticCompletionNameMatches(symbol.symbolName, prefix)) {
            continue;
        }

        const QString key = symbol.symbolName.toCaseFolded();
        if (seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(symbol);
    }

    sortSymbolsByName(result);
    return result;
}

QStringList SemanticIndex::getCompletionSymbolNames() const
{
    QSet<QString> uniqueNames;
    const QList<sym_list::SymbolInfo> symbols = getSymbols();
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (!symbol.symbolName.isEmpty())
            uniqueNames.insert(symbol.symbolName);
    }

    QStringList names(uniqueNames.begin(), uniqueNames.end());
    names.sort(Qt::CaseInsensitive);
    return names;
}
