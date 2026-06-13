#include "syminfo.h"

#include <QReadLocker>

extern thread_local bool s_holdingWriteLock;

sym_list::SymbolInfo sym_list::getSymbolById(int symbolId) const
{
    if (s_holdingWriteLock) {
        if (symbolIdToIndex.contains(symbolId)) {
            int index = symbolIdToIndex[symbolId];
            if (index < symbolDatabase.size()) {
                return symbolDatabase[index];
            }
        }
        SymbolInfo emptySymbol;
        emptySymbol.symbolId = -1;
        return emptySymbol;
    }
    QReadLocker lock(&symbolDbLock);
    if (symbolIdToIndex.contains(symbolId)) {
        int index = symbolIdToIndex[symbolId];
        if (index < symbolDatabase.size()) {
            return symbolDatabase[index];
        }
    }

    SymbolInfo emptySymbol;
    emptySymbol.symbolId = -1;
    return emptySymbol;
}

bool sym_list::hasSymbol(int symbolId) const
{
    return symbolIdToIndex.contains(symbolId);
}

QList<sym_list::SymbolInfo> sym_list::findSymbolsByType(sym_type_e symbolType)
{
    QList<SymbolInfo> result;

    if (symbolTypeIndex.contains(symbolType)) {
        const QList<int>& indices = symbolTypeIndex[symbolType];
        result.reserve(indices.size());

        for (int index : indices) {
            if (index < symbolDatabase.size()) {
                result.append(symbolDatabase[index]);
            }
        }
    }

    return result;
}

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

QStringList sym_list::getSymbolNamesByType(sym_type_e symbolType)
{
    updateCachedData();

    if (cachedSymbolNamesByType.contains(symbolType)) {
        return cachedSymbolNamesByType[symbolType];
    }

    return QStringList();
}

QSet<QString> sym_list::getUniqueSymbolNames()
{
    updateCachedData();
    return cachedUniqueNames;
}

int sym_list::getSymbolCountByType(sym_type_e symbolType)
{
    if (symbolTypeIndex.contains(symbolType)) {
        return symbolTypeIndex[symbolType].size();
    }
    return 0;
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
    symbolTypeIndex.clear();
    symbolNameIndex.clear();
    fileNameIndex.clear();
    symbolIdToIndex.clear();
    for (int i = 0; i < symbolDatabase.size(); ++i) {
        addToIndexes(i);
        symbolIdToIndex[symbolDatabase[i].symbolId] = i;
    }

    invalidateCache();
}

void sym_list::addToIndexes(int symbolIndex)
{
    if (symbolIndex >= symbolDatabase.size())
        return;

    const SymbolInfo& symbol = symbolDatabase[symbolIndex];
    symbolTypeIndex[symbol.symbolType].append(symbolIndex);
    symbolNameIndex[symbol.symbolName].append(symbolIndex);
    fileNameIndex[symbol.fileName].append(symbolIndex);
}

void sym_list::removeFromIndexes(int symbolIndex)
{
    if (symbolIndex >= symbolDatabase.size())
        return;

    const SymbolInfo& symbol = symbolDatabase[symbolIndex];

    if (symbolTypeIndex.contains(symbol.symbolType)) {
        symbolTypeIndex[symbol.symbolType].removeAll(symbolIndex);
        if (symbolTypeIndex[symbol.symbolType].isEmpty()) {
            symbolTypeIndex.remove(symbol.symbolType);
        }
    }

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

void sym_list::invalidateCache()
{
    cachedSymbolNamesByType.clear();
    cachedUniqueNames.clear();
    indexesDirty = true;
}

void sym_list::updateCachedData() const
{
    if (!indexesDirty)
        return;

    cachedSymbolNamesByType.clear();
    cachedUniqueNames.clear();

    for (auto it = symbolTypeIndex.begin(); it != symbolTypeIndex.end(); ++it) {
        sym_type_e symbolType = it.key();
        const QList<int>& indices = it.value();

        QStringList names;
        names.reserve(indices.size());

        for (int index : indices) {
            if (index < symbolDatabase.size()) {
                const QString& name = symbolDatabase[index].symbolName;
                names.append(name);
                cachedUniqueNames.insert(name);
            }
        }

        names.removeDuplicates();
        names.sort();

        cachedSymbolNamesByType[symbolType] = names;
    }

    indexesDirty = false;
}
