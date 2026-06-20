#include "syminfo.h"
#include "scope_tree.h"

#include <algorithm>
#include <QStack>

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
    scopeStack.push(fileRoot);

    for (const SymbolInfo& sym : std::as_const(fileSymbols)) {
        while (scopeStack.size() > 1 && scopeStack.top()->endLine > 0 && sym.startLine > scopeStack.top()->endLine) {
            scopeStack.pop();
        }

        if (sym.symbolType == sym_module) {
            ScopeNode* modNode = new ScopeNode(ScopeType::Module, sym.startLine);
            modNode->endLine = sym.endLine > 0 ? sym.endLine : sym.startLine;
            modNode->parent = scopeStack.top();
            scopeStack.top()->children.append(modNode);
            modNode->symbolNames.insert(sym.symbolName);
            scopeStack.push(modNode);
            continue;
        }

        if (sym.symbolType == sym_task || sym.symbolType == sym_function) {
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
            if (!scopeStack.isEmpty())
                scopeStack.top()->symbolNames.insert(sym.symbolName);
            continue;
        }

        if (sym.symbolType == sym_packed_struct || sym.symbolType == sym_unpacked_struct) {
            if (!scopeStack.isEmpty())
                scopeStack.top()->symbolNames.insert(sym.symbolName);
            continue;
        }

        if (sym.symbolType == sym_package) {
            if (!scopeStack.isEmpty())
                scopeStack.top()->symbolNames.insert(sym.symbolName);
        }
    }

}
