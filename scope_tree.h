#ifndef SCOPE_TREE_H
#define SCOPE_TREE_H

#include "syminfo.h"
#include <QString>
#include <QList>
#include <QHash>
#include <QStack>

/**
 * 作用域类型：对应 SystemVerilog 的层级
 */
enum class ScopeType {
    Global,
    Module,
    Task,
    Function,
    Block
};

/**
 * 作用域节点：表示一个词法作用域（全局/模块/task/function/begin块）
 */
struct ScopeNode {
    ScopeType type = ScopeType::Global;
    int startLine = 0;
    int endLine = 0;
    ScopeNode* parent = nullptr;
    QList<ScopeNode*> children;
    /** 本作用域内符号：名称 -> SymbolInfo，支持 O(1) 查找与遮蔽 */
    QHash<QString, sym_list::SymbolInfo> symbols;

    ScopeNode(ScopeType t, int start = 0) : type(t), startLine(start), endLine(start) {}
    ~ScopeNode() { qDeleteAll(children); }
};

/**
 * 作用域管理器：按文件维护作用域树，提供按行查找作用域与按名解析符号
 */
class ScopeManager
{
public:
    ScopeManager() = default;
    ~ScopeManager() {
        for (ScopeNode* root : fileRoots)
            delete root;
        fileRoots.clear();
    }

    /** 为某文件设置根作用域（通常为 Global），会接管并删除旧根 */
    void setFileRoot(const QString& fileName, ScopeNode* root) {
        if (fileRoots.contains(fileName)) {
            delete fileRoots[fileName];
        }
        fileRoots[fileName] = root;
    }

    /** 清除某文件的作用域树 */
    void clearFile(const QString& fileName) {
        if (fileRoots.contains(fileName)) {
            delete fileRoots[fileName];
            fileRoots.remove(fileName);
        }
    }

    /**
     * 查找 (fileName, line) 所在的最深层作用域
     * 返回 nullptr 表示该文件无树或该行不在任何作用域内
     */
    ScopeNode* findScopeAt(const QString& fileName, int line) const {
        if (!fileRoots.contains(fileName)) return nullptr;
        ScopeNode* root = fileRoots[fileName];
        return findDeepestScopeContainingLine(root, line);
    }

    /**
     * 从 startScope 起沿 parent 链向上查找名为 name 的符号（词法作用域/遮蔽）
     * 找到则返回该 SymbolInfo，否则返回 symbolId == -1 的空 SymbolInfo
     */
    sym_list::SymbolInfo resolveSymbol(const QString& name, ScopeNode* startScope) const {
        for (ScopeNode* scope = startScope; scope; scope = scope->parent) {
            auto it = scope->symbols.constFind(name);
            if (it != scope->symbols.constEnd())
                return it.value();
        }
        sym_list::SymbolInfo empty;
        empty.symbolId = -1;
        return empty;
    }

    /** 判断某文件是否已有作用域树 */
    bool hasScopeTree(const QString& fileName) const {
        return fileRoots.contains(fileName);
    }

private:
    QHash<QString, ScopeNode*> fileRoots;

    static ScopeNode* findDeepestScopeContainingLine(ScopeNode* node, int line) {
        if (!node || line < node->startLine) return nullptr;
        if (node->endLine > 0 && line > node->endLine) return nullptr;
        for (ScopeNode* child : node->children) {
            ScopeNode* candidate = findDeepestScopeContainingLine(child, line);
            if (candidate) return candidate;
        }
        return node;
    }
};

#endif // SCOPE_TREE_H
