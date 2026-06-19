#include "syminfo.h"

#include <QReadLocker>

extern thread_local bool s_holdingWriteLock;

QList<sym_list::SymbolInfo> sym_list::findSymbolsByName(const QString& symbolName)
{
    QReadLocker lock(&symbolDbLock);
    QList<SymbolInfo> result;

    if (symbolNameIndex.contains(symbolName)) {
        const QList<int>& indices = symbolNameIndex[symbolName];
        result.reserve(indices.size());

        for (int index : indices) {
            if (index < symbolDatabase.size()) {
                result.append(symbolDatabase[index]);
            }
        }
    }

    return result;
}

int sym_list::findSymbolIdByName(const QString& symbolName) const
{
    if (s_holdingWriteLock) {
        if (symbolNameIndex.contains(symbolName)) {
            const QList<int>& indices = symbolNameIndex[symbolName];
            if (!indices.isEmpty() && indices.first() < symbolDatabase.size()) {
                return symbolDatabase[indices.first()].symbolId;
            }
        }
        return -1;
    }
    QReadLocker lock(&symbolDbLock);
    if (symbolNameIndex.contains(symbolName)) {
        const QList<int>& indices = symbolNameIndex[symbolName];
        if (!indices.isEmpty() && indices.first() < symbolDatabase.size()) {
            return symbolDatabase[indices.first()].symbolId;
        }
    }
    return -1;
}

QList<sym_list::SymbolInfo> sym_list::findSymbolsByFileName(const QString& fileName)
{
    QList<SymbolInfo> result;
    if (s_holdingWriteLock) {
        if (fileNameIndex.contains(fileName)) {
            const QList<int>& indices = fileNameIndex[fileName];
            result.reserve(indices.size());
            for (int index : indices) {
                if (index < symbolDatabase.size()) {
                    result.append(symbolDatabase[index]);
                }
            }
        }
        return result;
    }
    QReadLocker lock(&symbolDbLock);
    if (fileNameIndex.contains(fileName)) {
        const QList<int>& indices = fileNameIndex[fileName];
        result.reserve(indices.size());
        for (int index : indices) {
            if (index < symbolDatabase.size()) {
                result.append(symbolDatabase[index]);
            }
        }
    }
    return result;
}

QList<sym_list::SymbolInfo> sym_list::getAllSymbols()
{
    if (s_holdingWriteLock) {
        return symbolDatabase;
    }
    QReadLocker lock(&symbolDbLock);
    return symbolDatabase;
}

void sym_list::rebuildAllIndexes()
{
    symbolNameIndex.clear();
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
    symbolNameIndex[symbol.symbolName].append(symbolIndex);
    fileNameIndex[symbol.fileName].append(symbolIndex);
}

void sym_list::removeFromIndexes(int symbolIndex)
{
    if (symbolIndex >= symbolDatabase.size())
        return;

    const SymbolInfo& symbol = symbolDatabase[symbolIndex];

    if (symbolNameIndex.contains(symbol.symbolName)) {
        symbolNameIndex[symbol.symbolName].removeAll(symbolIndex);
        if (symbolNameIndex[symbol.symbolName].isEmpty()) {
            symbolNameIndex.remove(symbol.symbolName);
        }
    }

    if (fileNameIndex.contains(symbol.fileName)) {
        fileNameIndex[symbol.fileName].removeAll(symbolIndex);
        if (fileNameIndex[symbol.fileName].isEmpty()) {
            fileNameIndex.remove(symbol.fileName);
        }
    }
}
