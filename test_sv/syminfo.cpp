#include "syminfo.h"

#include <QMutex>
#include <algorithm>
#include <memory>

std::unique_ptr<sym_list> sym_list::instance = nullptr;

sym_list::sym_list()
{
    commentRegions.reserve(100);
}

sym_list::~sym_list() = default;

sym_list* sym_list::getInstance()
{
    static QMutex instanceMutex;
    QMutexLocker lock(&instanceMutex);
    if (!instance) {
        instance = std::unique_ptr<sym_list>(new sym_list());
    }
    return instance.get();
}

void sym_list::setCachedFileContent(const QString& fileName,
                                    const QString& content)
{
    previousFileContents.insert(fileName, content);
}

QList<sym_list::SymbolInfo> sym_list::getAllSymbols(const QString& fileName) const
{
    if (!fileName.isEmpty())
        return symbolsByFile.value(fileName);

    QList<SymbolInfo> symbols;
    for (const QList<SymbolInfo>& fileSymbols : symbolsByFile)
        symbols.append(fileSymbols);
    return symbols;
}

void sym_list::setSymbolsForFile(const QString& fileName,
                                 const QList<SymbolInfo>& symbols)
{
    QList<SymbolInfo> normalizedSymbols = symbols;
    int nextSymbolId = 1;
    for (const QList<SymbolInfo>& fileSymbols : symbolsByFile) {
        for (const SymbolInfo& symbol : fileSymbols)
            nextSymbolId = std::max(nextSymbolId, symbol.symbolId + 1);
    }
    for (const SymbolInfo& symbol : normalizedSymbols)
        nextSymbolId = std::max(nextSymbolId, symbol.symbolId + 1);

    for (SymbolInfo& symbol : normalizedSymbols)
    {
        symbol.fileName = fileName;
        if (symbol.symbolId <= 0)
            symbol.symbolId = nextSymbolId++;
    }
    symbolsByFile.insert(fileName, normalizedSymbols);
}
