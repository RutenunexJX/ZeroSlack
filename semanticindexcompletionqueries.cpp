#include "semanticindex.h"
#include "semanticindexcompletionfilters.h"

#include <QSet>
#include <algorithm>

using namespace semantic_index_completion;

namespace {
QString displayNameForCompletionQuerySymbol(
    const sym_list::SymbolInfo& symbol)
{
    const SemanticSymbolRecord record = semanticSymbolRecordForSymbol(symbol);
    if (!record.name.isEmpty())
        return record.name;
    return symbol.symbolName;
}

QString ownerNameForCompletionQuerySymbol(
    const sym_list::SymbolInfo& symbol)
{
    return semanticSymbolRecordForSymbol(symbol).owner.name;
}

void sortCompletionQuerySymbolsByRecordName(
    QList<sym_list::SymbolInfo>& symbols)
{
    std::sort(symbols.begin(), symbols.end(),
              [](const sym_list::SymbolInfo& left,
                 const sym_list::SymbolInfo& right) {
        return QString::compare(displayNameForCompletionQuerySymbol(left),
                                displayNameForCompletionQuerySymbol(right),
                                Qt::CaseInsensitive) < 0;
    });
}
}

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
        const QString displayName = displayNameForCompletionQuerySymbol(symbol);
        if (ownerNameForCompletionQuerySymbol(symbol) != moduleName
            || !internalCompletionSymbol(symbol)
            || !semanticCompletionNameMatches(displayName, prefix)) {
            continue;
        }

        const QString key = displayName.toCaseFolded();
        if (seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(symbol);
    }

    sortCompletionQuerySymbolsByRecordName(result);
    return result;
}

QList<sym_list::SymbolInfo> SemanticIndex::getGlobalCompletionSymbols(
    const QString& prefix) const
{
    QList<sym_list::SymbolInfo> result;
    QSet<QString> seenNames;
    const QList<sym_list::SymbolInfo> symbols = getSymbols();
    for (const sym_list::SymbolInfo& symbol : symbols) {
        const QString displayName = displayNameForCompletionQuerySymbol(symbol);
        if (!globalCompletionSymbol(symbol)
            || !semanticCompletionNameMatches(displayName, prefix)) {
            continue;
        }

        const QString key = displayName.toCaseFolded();
        if (seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(symbol);
    }

    sortCompletionQuerySymbolsByRecordName(result);
    return result;
}

QStringList SemanticIndex::getCompletionSymbolNames() const
{
    QSet<QString> uniqueNames;
    const QList<sym_list::SymbolInfo> symbols = getSymbols();
    for (const sym_list::SymbolInfo& symbol : symbols) {
        const QString displayName = displayNameForCompletionQuerySymbol(symbol);
        if (!displayName.isEmpty())
            uniqueNames.insert(displayName);
    }

    QStringList names(uniqueNames.begin(), uniqueNames.end());
    names.sort(Qt::CaseInsensitive);
    return names;
}
