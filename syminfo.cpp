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

    symbolTypeIndex.reserve(50);
    symbolNameIndex.reserve(500);
    fileNameIndex.reserve(50);
    symbolIdToIndex.reserve(1000);
}

sym_list::~sym_list()
{
    delete m_scopeManager;
    m_scopeManager = nullptr;
}

ScopeManager* sym_list::getScopeManager() const
{
    if (!m_scopeManager)
        m_scopeManager = new ScopeManager();
    return m_scopeManager;
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

    if (newSymbol.moduleScope.isEmpty() &&
        (newSymbol.symbolType == sym_reg ||
         newSymbol.symbolType == sym_wire ||
         newSymbol.symbolType == sym_logic)) {
        newSymbol.moduleScope = getCurrentModuleScope(newSymbol.fileName, newSymbol.startLine);
    }

    symbolDatabase.append(newSymbol);
    int newIndex = symbolDatabase.size() - 1;
    symbolIdToIndex[newSymbol.symbolId] = newIndex;
    addToIndexes(newIndex);
    updateLineBasedSymbols(newSymbol);
    indexesDirty = true;
}

SymbolRelationshipEngine* sym_list::getRelationshipEngine() const
{
    return relationshipEngine;
}

void sym_list::setRelationshipEngine(SymbolRelationshipEngine* engine)
{
    relationshipEngine = engine;

    if (engine && !symbolDatabase.isEmpty()) {
        rebuildAllRelationships();
    }
}

void sym_list::rebuildAllRelationships()
{
    if (!relationshipEngine) return;

    relationshipEngine->clearAllRelationships();
    QHash<QString, QList<SymbolInfo>> symbolsByFile;
    for (const SymbolInfo& symbol : symbolDatabase) {
        symbolsByFile[symbol.fileName].append(symbol);
    }

    for (auto it = symbolsByFile.begin(); it != symbolsByFile.end(); ++it) {
        buildSymbolRelationships(it.key());
    }
}

void sym_list::buildSymbolRelationships(const QString& fileName)
{
    if (!relationshipEngine) return;

    QList<SymbolInfo> fileSymbols = findSymbolsByFileName(fileName);
    if (fileSymbols.isEmpty()) return;

    analyzeModuleContainment(fileName);
    relationshipEngine->buildFileRelationships(fileName);
}

void sym_list::analyzeModuleContainment(const QString& fileName)
{
    if (!relationshipEngine) return;

    QList<SymbolInfo> fileSymbols = findSymbolsByFileName(fileName);

    QList<SymbolInfo> modules;
    for (const SymbolInfo& symbol : fileSymbols) {
        if (symbol.symbolType == sym_module) {
            modules.append(symbol);
        }
    }

    for (const SymbolInfo& module : modules) {
        for (const SymbolInfo& symbol : fileSymbols) {
            if (symbol.symbolId != module.symbolId &&
                isSymbolInModule(symbol, module)) {

                relationshipEngine->addRelationship(
                    module.symbolId,
                    symbol.symbolId,
                    SymbolRelationshipEngine::CONTAINS
                );

                int symbolIndex = symbolIdToIndex[symbol.symbolId];
                if (symbolIndex < symbolDatabase.size()) {
                    if (symbolDatabase[symbolIndex].moduleScope.isEmpty()) {
                        symbolDatabase[symbolIndex].moduleScope = module.symbolName;
                        symbolDatabase[symbolIndex].scopeLevel = 1;
                    }
                }
            }
        }
    }
}

void sym_list::clearSymbolsForFile(const QString& fileName)
{
    int beforeCount = symbolDatabase.size();

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
    int afterCount = symbolDatabase.size();
    if (beforeCount != afterCount) {
        invalidateCache();
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

    invalidateCache();
    s_holdingWriteLock = false;
}

void sym_list::rebuildScopeAndRelationshipsForFile(const QString& fileName)
{
    if (!fileNameIndex.contains(fileName))
        return;

    QList<SymbolInfo> fileSymbols;
    for (int index : fileNameIndex[fileName]) {
        if (index < symbolDatabase.size())
            fileSymbols.append(symbolDatabase[index]);
    }
    if (fileSymbols.isEmpty())
        return;

    std::sort(fileSymbols.begin(), fileSymbols.end(), [](const SymbolInfo& a, const SymbolInfo& b) {
        if (a.startLine != b.startLine) return a.startLine < b.startLine;
        if (a.symbolType == sym_module) return true;
        if (b.symbolType == sym_module) return false;
        return a.symbolId < b.symbolId;
    });

    ScopeManager* scopeMgr = getScopeManager();
    scopeMgr->clearFile(fileName);
    ScopeNode* fileRoot = new ScopeNode(ScopeType::Global, 0);
    fileRoot->endLine = 0;
    scopeMgr->setFileRoot(fileName, fileRoot);
    QStack<ScopeNode*> scopeStack;
    QStack<int> moduleStack;
    scopeStack.push(fileRoot);

    for (const SymbolInfo& sym : std::as_const(fileSymbols)) {
        while (scopeStack.size() > 1 && scopeStack.top()->endLine > 0 && sym.startLine > scopeStack.top()->endLine) {
            ScopeNode* node = scopeStack.pop();
            if (node->type == ScopeType::Module && !moduleStack.isEmpty())
                moduleStack.pop();
        }

        if (sym.symbolType == sym_module) {
            moduleStack.push(sym.symbolId);
            ScopeNode* modNode = new ScopeNode(ScopeType::Module, sym.startLine);
            modNode->endLine = sym.endLine > 0 ? sym.endLine : sym.startLine;
            modNode->parent = scopeStack.top();
            scopeStack.top()->children.append(modNode);
            modNode->symbols[sym.symbolName] = sym;
            scopeStack.push(modNode);
            continue;
        }

        if (sym.symbolType == sym_task || sym.symbolType == sym_function) {
            if (relationshipEngine && !moduleStack.isEmpty())
                relationshipEngine->addRelationship(moduleStack.last(), sym.symbolId, SymbolRelationshipEngine::CONTAINS);
            ScopeType st = (sym.symbolType == sym_task) ? ScopeType::Task : ScopeType::Function;
            ScopeNode* subNode = new ScopeNode(st, sym.startLine);
            subNode->endLine = sym.endLine > 0 ? sym.endLine : sym.startLine;
            subNode->parent = scopeStack.top();
            scopeStack.top()->children.append(subNode);
            subNode->symbols[sym.symbolName] = sym;
            scopeStack.push(subNode);
            continue;
        }

        if (sym.symbolType == sym_port_input || sym.symbolType == sym_port_output
            || sym.symbolType == sym_port_inout || sym.symbolType == sym_port_ref
            || sym.symbolType == sym_port_interface || sym.symbolType == sym_port_interface_modport
            || sym.symbolType == sym_reg || sym.symbolType == sym_wire || sym.symbolType == sym_logic
            || sym.symbolType == sym_parameter || sym.symbolType == sym_localparam
            || sym.symbolType == sym_typedef || sym.symbolType == sym_enum
            || sym.symbolType == sym_enum_value || sym.symbolType == sym_enum_var
            || sym.symbolType == sym_struct_member
            || sym.symbolType == sym_packed_struct_var || sym.symbolType == sym_unpacked_struct_var
            || sym.symbolType == sym_inst || sym.symbolType == sym_inst_pin) {
            if (relationshipEngine && !moduleStack.isEmpty())
                relationshipEngine->addRelationship(moduleStack.last(), sym.symbolId, SymbolRelationshipEngine::CONTAINS);
            if (!scopeStack.isEmpty())
                scopeStack.top()->symbols[sym.symbolName] = sym;
            continue;
        }

        if (sym.symbolType == sym_packed_struct || sym.symbolType == sym_unpacked_struct) {
            if (relationshipEngine && !moduleStack.isEmpty())
                relationshipEngine->addRelationship(moduleStack.last(), sym.symbolId, SymbolRelationshipEngine::CONTAINS);
            if (!scopeStack.isEmpty())
                scopeStack.top()->symbols[sym.symbolName] = sym;
            continue;
        }

        if (sym.symbolType == sym_package) {
            if (relationshipEngine && !moduleStack.isEmpty())
                relationshipEngine->addRelationship(moduleStack.last(), sym.symbolId, SymbolRelationshipEngine::CONTAINS);
            if (!scopeStack.isEmpty())
                scopeStack.top()->symbols[sym.symbolName] = sym;
        }
    }

    buildSymbolRelationships(fileName);
}

void sym_list::clearStructTypedefEnumSymbolsForFile(const QString &fileName)
{
    if (!fileNameIndex.contains(fileName)) return;

    static const QList<sym_type_e> typesToClear = {
        sym_packed_struct, sym_unpacked_struct,
        sym_packed_struct_var, sym_unpacked_struct_var, sym_struct_member,
        sym_typedef, sym_enum, sym_enum_var, sym_enum_value
    };
    QSet<sym_type_e> typeSet;
    for (sym_type_e t : typesToClear) typeSet.insert(t);

    QList<int> indicesToRemove;
    const QList<int> &fileIndices = fileNameIndex[fileName];
    for (int index : fileIndices) {
        if (index < symbolDatabase.size() && typeSet.contains(symbolDatabase[index].symbolType)) {
            indicesToRemove.append(index);
        }
    }
    if (indicesToRemove.isEmpty()) return;

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

bool isSymbolInModule(const sym_list::SymbolInfo& symbol, const sym_list::SymbolInfo& module)
{
    return symbol.fileName == module.fileName &&
           symbol.startLine > module.startLine;
}

QString getModuleNameContainingSymbol(const sym_list::SymbolInfo& symbol,
                                     const QList<sym_list::SymbolInfo>& allSymbols)
{
    for (const sym_list::SymbolInfo& moduleSymbol : allSymbols) {
        if (moduleSymbol.symbolType == sym_list::sym_module &&
            isSymbolInModule(symbol, moduleSymbol)) {
            return moduleSymbol.symbolName;
        }
    }
    return QString();
}

