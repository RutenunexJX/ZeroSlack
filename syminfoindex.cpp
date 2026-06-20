#include "syminfo.h"

#include <QReadLocker>

extern thread_local bool s_holdingWriteLock;

QList<sym_list::SymbolInfo> sym_list::getAllSymbols()
{
    if (s_holdingWriteLock) {
        return symbolDatabase;
    }
    QReadLocker lock(&symbolDbLock);
    return symbolDatabase;
}

QList<sym_list::SymbolInfo> sym_list::symbolsForFileSnapshot(const QString& fileName) const
{
    QList<SymbolInfo> result;
    if (s_holdingWriteLock) {
        if (fileNameIndex.contains(fileName)) {
            const QList<int>& indices = fileNameIndex[fileName];
            result.reserve(indices.size());
            for (int index : indices) {
                if (index < symbolDatabase.size())
                    result.append(symbolDatabase[index]);
            }
        }
        return result;
    }

    QReadLocker lock(&symbolDbLock);
    if (fileNameIndex.contains(fileName)) {
        const QList<int>& indices = fileNameIndex[fileName];
        result.reserve(indices.size());
        for (int index : indices) {
            if (index < symbolDatabase.size())
                result.append(symbolDatabase[index]);
        }
    }
    return result;
}

void sym_list::rebuildAllIndexes()
{
    fileNameIndex.clear();
    symbolIdToIndex.clear();
    for (int i = 0; i < symbolDatabase.size(); ++i) {
        addToIndexes(i);
        symbolIdToIndex[symbolDatabase[i].symbolId] = i;
    }
}

void sym_list::addToIndexes(int symbolIndex)
{
    if (symbolIndex >= symbolDatabase.size())
        return;

    const SymbolInfo& symbol = symbolDatabase[symbolIndex];
    fileNameIndex[symbol.fileName].append(symbolIndex);
}

void sym_list::removeFromIndexes(int symbolIndex)
{
    if (symbolIndex >= symbolDatabase.size())
        return;

    const SymbolInfo& symbol = symbolDatabase[symbolIndex];

    if (fileNameIndex.contains(symbol.fileName)) {
        fileNameIndex[symbol.fileName].removeAll(symbolIndex);
        if (fileNameIndex[symbol.fileName].isEmpty()) {
            fileNameIndex.remove(symbol.fileName);
        }
    }
}
