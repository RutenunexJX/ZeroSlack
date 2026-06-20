#include "syminfo.h"
#include "scope_tree.h"
#include "symbolrelationshipengine.h"

#include <QWriteLocker>
#include <QMutex>
#include <algorithm>
#include <memory>
#include <utility>

std::unique_ptr<sym_list> sym_list::instance = nullptr;

thread_local bool s_holdingWriteLock = false;

sym_list::sym_list()
{
    symbolDatabase.reserve(1000);
    commentRegions.reserve(100);

    fileNameIndex.reserve(50);
    symbolIdToIndex.reserve(1000);
}

sym_list* sym_list::getInstance()
{
    static QMutex instanceMutex;
    QMutexLocker lock(&instanceMutex);
    if (!instance) {
        instance = std::unique_ptr<sym_list>(new sym_list());
    }
    return instance.get();
}

int sym_list::allocateSymbolId()
{
    return nextSymbolId++;
}


void sym_list::addSymbol(const SymbolInfo& symbol)
{
    SymbolInfo newSymbol = symbol;
    if (newSymbol.symbolId <= 0) {
        newSymbol.symbolId = allocateSymbolId();
    }

    symbolDatabase.append(newSymbol);
    int newIndex = symbolDatabase.size() - 1;
    symbolIdToIndex[newSymbol.symbolId] = newIndex;
    addToIndexes(newIndex);
    updateLineBasedSymbols(newSymbol);
}

void sym_list::clearSymbolsForFile(const QString& fileName)
{
    getScopeManager()->clearFile(fileName);

    if (relationshipEngine) {
        relationshipEngine->invalidateFileRelationships(fileName);
    }
    if (fileNameIndex.contains(fileName)) {
        QList<int> indicesToRemove = fileNameIndex[fileName];
        for (int index : indicesToRemove) {
            if (index < symbolDatabase.size()) {
                int symbolId = symbolDatabase[index].symbolId;
                symbolIdToIndex.remove(symbolId);
            }
        }
        std::sort(indicesToRemove.begin(), indicesToRemove.end(), std::greater<int>());
        for (int index : indicesToRemove) {
            if (index < symbolDatabase.size()) {
                removeFromIndexes(index);
                symbolDatabase.removeAt(index);
            }
        }

        rebuildAllIndexes();
    }
}

void sym_list::setSymbolsForFile(const QString& fileName, const QList<SymbolInfo>& symbols)
{
    setSymbolsForFile(fileName, symbols, QString());
}

void sym_list::setSymbolsForFile(const QString& fileName, const QList<SymbolInfo>& symbols, const QString& content)
{
    QWriteLocker lock(&symbolDbLock);
    s_holdingWriteLock = true;

    currentFileName = fileName;
    clearSymbolsForFile(fileName);

    for (const SymbolInfo& sym : symbols) {
        SymbolInfo s = sym;
        s.fileName = fileName;
        addSymbol(s);
    }

    rebuildScopeAndRelationshipsForFile(fileName);

    if (!content.isEmpty()) {
        FileState& state = fileStates[fileName];
        state.contentHash = calculateContentHash(content);
        state.symbolRelevantHash = calculateSymbolRelevantHash(content);
        state.lastAnalyzedLineCount = content.count('\n') + 1;
        state.lastModified = QDateTime::currentDateTime();
        state.needsFullAnalysis = false;
        previousFileContents[fileName] = content;
    }

    s_holdingWriteLock = false;
}

