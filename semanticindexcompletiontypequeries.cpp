#include "semanticindex.h"
#include "semanticindexcompletionfilters.h"
#include "symboltaxonomy.h"

#include <QSet>

using namespace semantic_index_completion;

namespace {

QSet<QString> packageScopeNames(const QList<sym_list::SymbolInfo>& symbols)
{
    QSet<QString> names;
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (SymbolTaxonomy::isPackageDeclaration(symbol.symbolType)
            && !symbol.symbolName.isEmpty()) {
            names.insert(symbol.symbolName);
        }
    }
    return names;
}

} // namespace

QList<sym_list::SymbolInfo> SemanticIndex::getCommandCompletionSymbols(
    const QString& moduleName,
    sym_list::sym_type_e symbolType,
    const QString& prefix) const
{
    QList<sym_list::SymbolInfo> result;
    QSet<QString> seenNames;
    if (moduleName.isEmpty() && !commandGlobalCompletionSymbolType(symbolType))
        return result;

    const QList<sym_list::SymbolInfo> symbols = getSymbols();
    const QSet<QString> packages = packageScopeNames(symbols);
    for (const sym_list::SymbolInfo& symbol : symbols) {
        const bool useGlobalScope = moduleName.isEmpty()
            || alwaysGlobalCommandSymbolType(symbolType);
        const bool scopeMatches = useGlobalScope
            ? symbol.moduleScope.isEmpty()
            : symbol.moduleScope == moduleName
                || SymbolTaxonomy::isPackageScopeVisibleCompletion(
                    symbol,
                    symbolType,
                    packages);
        if (!scopeMatches
            || !commandSymbolTypeMatches(symbol.symbolType,
                                        symbolType,
                                        symbol.dataType)
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

QList<sym_list::SymbolInfo> SemanticIndex::getTypedCompletionSymbols(
    sym_list::sym_type_e symbolType,
    const QString& prefix) const
{
    QList<sym_list::SymbolInfo> result;
    QSet<int> seenIds;

    const QList<sym_list::SymbolInfo> symbols = getSymbols();
    auto appendIfMatches = [&](const sym_list::SymbolInfo& symbol) {
        if (seenIds.contains(symbol.symbolId))
            return;
        if (!semanticCompletionNameMatches(symbol.symbolName, prefix))
            return;
        seenIds.insert(symbol.symbolId);
        result.append(symbol);
    };

    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (symbol.symbolType == symbolType)
            appendIfMatches(symbol);
    }

    if (symbolType == sym_list::sym_enum) {
        for (const sym_list::SymbolInfo& symbol : symbols) {
            if (symbol.symbolType == sym_list::sym_typedef
                && symbol.dataType == QLatin1String("enum")) {
                appendIfMatches(symbol);
            }
        }
    }

    return result;
}

QList<sym_list::SymbolInfo> SemanticIndex::getGlobalSymbolInfosByType(
    sym_list::sym_type_e symbolType,
    const QString& prefix) const
{
    QList<sym_list::SymbolInfo> result;
    if (!globalSymbolInfoType(symbolType))
        return result;

    const QList<sym_list::SymbolInfo> symbols = getSymbols();
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (!commandSymbolTypeMatches(symbol.symbolType,
                                      symbolType,
                                      symbol.dataType)
            || !semanticCompletionNameMatches(symbol.symbolName, prefix)) {
            continue;
        }

        const bool global = alwaysGlobalSymbolInfoType(symbolType)
            || symbol.moduleScope.isEmpty();
        if (global)
            result.append(symbol);
    }

    return result;
}
