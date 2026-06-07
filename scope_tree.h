#ifndef SCOPE_TREE_H
#define SCOPE_TREE_H

#include "syminfo.h"
#include <QString>
#include <QList>
#include <QHash>
#include <QStack>

/**
 */
enum class ScopeType {
    Global,
    Module,
    Task,
    Function,
    Block
};

/**
 */
struct ScopeNode {
    ScopeType type = ScopeType::Global;
    int startLine = 0;
    int endLine = 0;
    ScopeNode* parent = nullptr;
    QList<ScopeNode*> children;
    QHash<QString, sym_list::SymbolInfo> symbols;

    ScopeNode(ScopeType t, int start = 0) : type(t), startLine(start), endLine(start) {}
    ~ScopeNode() { qDeleteAll(children); }
};

/**
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

    void setFileRoot(const QString& fileName, ScopeNode* root) {
        if (fileRoots.contains(fileName)) {
            delete fileRoots[fileName];
        }
        fileRoots[fileName] = root;
    }

    void clearFile(const QString& fileName) {
        if (fileRoots.contains(fileName)) {
            delete fileRoots[fileName];
            fileRoots.remove(fileName);
        }
    }

    /**
     */
    ScopeNode* findScopeAt(const QString& fileName, int line) const {
        if (!fileRoots.contains(fileName)) return nullptr;
        ScopeNode* root = fileRoots[fileName];
        return findDeepestScopeContainingLine(root, line);
    }

    /**
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
