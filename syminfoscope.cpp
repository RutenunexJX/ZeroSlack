#include "syminfo.h"
#include "scope_tree.h"
#include "symbolrelationshipengine.h"

#include <QStack>
#include <algorithm>

namespace {
bool symbolIsInModule(const sym_list::SymbolInfo& symbol,
                      const sym_list::SymbolInfo& module)
{
    return symbol.fileName == module.fileName
        && symbol.startLine > module.startLine;
}
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
    if (!relationshipEngine)
        return;

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
    if (!relationshipEngine)
        return;

    QList<SymbolInfo> fileSymbols = symbolsForFileSnapshot(fileName);
    if (fileSymbols.isEmpty())
        return;

    analyzeModuleContainment(fileName);
    relationshipEngine->buildFileRelationships(fileName);
}

void sym_list::analyzeModuleContainment(const QString& fileName)
{
    if (!relationshipEngine)
        return;

    QList<SymbolInfo> fileSymbols = symbolsForFileSnapshot(fileName);

    QList<SymbolInfo> modules;
    for (const SymbolInfo& symbol : fileSymbols) {
        if (symbol.symbolType == sym_module) {
            modules.append(symbol);
        }
    }

    for (const SymbolInfo& module : modules) {
        for (const SymbolInfo& symbol : fileSymbols) {
            if (symbol.symbolId != module.symbolId &&
                symbolIsInModule(symbol, module)) {

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

void sym_list::rebuildScopeAndRelationshipsForFile(const QString& fileName)
{
    QList<SymbolInfo> fileSymbols = symbolsForFileSnapshot(fileName);
    if (fileSymbols.isEmpty())
        return;

    std::sort(fileSymbols.begin(), fileSymbols.end(), [](const SymbolInfo& a, const SymbolInfo& b) {
        if (a.startLine != b.startLine)
            return a.startLine < b.startLine;
        if (a.symbolType == sym_module)
            return true;
        if (b.symbolType == sym_module)
            return false;
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
            modNode->symbolNames.insert(sym.symbolName);
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
            subNode->symbolNames.insert(sym.symbolName);
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
                scopeStack.top()->symbolNames.insert(sym.symbolName);
            continue;
        }

        if (sym.symbolType == sym_packed_struct || sym.symbolType == sym_unpacked_struct) {
            if (relationshipEngine && !moduleStack.isEmpty())
                relationshipEngine->addRelationship(moduleStack.last(), sym.symbolId, SymbolRelationshipEngine::CONTAINS);
            if (!scopeStack.isEmpty())
                scopeStack.top()->symbolNames.insert(sym.symbolName);
            continue;
        }

        if (sym.symbolType == sym_package) {
            if (relationshipEngine && !moduleStack.isEmpty())
                relationshipEngine->addRelationship(moduleStack.last(), sym.symbolId, SymbolRelationshipEngine::CONTAINS);
            if (!scopeStack.isEmpty())
                scopeStack.top()->symbolNames.insert(sym.symbolName);
        }
    }

    buildSymbolRelationships(fileName);
}
