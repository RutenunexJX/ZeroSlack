#include "slangmanager.h"
#include "slangsymbolcollector.h"

#include <slang/ast/Compilation.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/util/Bag.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace slang::ast;

QList<sym_list::SymbolInfo> SlangManager::extractSymbols(const QString& fileName, const QString& content)
{
    QList<sym_list::SymbolInfo> result;
    try {
        std::string src = content.toStdString();
        std::string nameStr = fileName.toStdString();
        auto tree = slang::syntax::SyntaxTree::fromText(
            std::string_view(src),
            std::string_view(nameStr),
            std::string_view{});

        if (!tree)
            return result;

        slang::Bag bag;
        auto& opts = bag.insertOrGet<CompilationOptions>();
        opts.flags |= CompilationFlags::IgnoreUnknownModules;

        Compilation compilation(bag);
        compilation.addSyntaxTree(tree);

        slang_symbols::collectSymbols(compilation, result);
    } catch (const std::exception&) {
        result.clear();
    } catch (...) {
        result.clear();
    }
    return result;
}

QList<sym_list::SymbolInfo> SlangManager::extractWorkspaceSymbols(const QStringList& filePaths)
{
    QList<sym_list::SymbolInfo> result;
    if (filePaths.isEmpty())
        return result;
    try {
        std::vector<std::string> pathStrs;
        pathStrs.reserve(filePaths.size());
        for (const QString& p : filePaths)
            pathStrs.push_back(p.toStdString());

        std::vector<std::string_view> pathViews;
        pathViews.reserve(pathStrs.size());
        for (const std::string& s : pathStrs)
            pathViews.push_back(s);

        auto treeOrErr = slang::syntax::SyntaxTree::fromFiles(pathViews);
        if (!treeOrErr)
            return result;

        std::shared_ptr<slang::syntax::SyntaxTree> tree = std::move(*treeOrErr);
        if (!tree)
            return result;

        slang::Bag bag;
        auto& opts = bag.insertOrGet<CompilationOptions>();
        opts.flags |= CompilationFlags::IgnoreUnknownModules;

        Compilation compilation(bag);
        compilation.addSyntaxTree(tree);

        slang_symbols::collectSymbols(compilation, result);
    } catch (const std::exception&) {
        result.clear();
    } catch (...) {
        result.clear();
    }
    return result;
}
