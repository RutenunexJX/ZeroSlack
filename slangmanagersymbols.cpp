#include "slangmanager.h"
#include "slangparseoptions.h"
#include "slangsymbolcollector.h"

#include <slang/ast/Compilation.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/util/Bag.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace slang::ast;

QList<sym_list::SymbolInfo> SlangManager::extractSymbols(
    const QString& fileName,
    const QString& content,
    const QStringList& includeDirs,
    const QHash<QString, QString>& defines)
{
    QList<sym_list::SymbolInfo> result;
    try {
        std::string src = content.toStdString();
        std::string nameStr = fileName.toStdString();
        std::shared_ptr<slang::syntax::SyntaxTree> tree;
        if (includeDirs.isEmpty() && defines.isEmpty()) {
            tree = slang::syntax::SyntaxTree::fromText(
                std::string_view(src),
                std::string_view(nameStr),
                std::string_view{});
        } else {
            slang::Bag syntaxOptions =
                slang_parse_options::makeSyntaxOptions(includeDirs, defines);
            tree = slang::syntax::SyntaxTree::fromText(
                std::string_view(src),
                syntaxOptions,
                std::string_view(nameStr),
                std::string_view(nameStr));
        }

        if (!tree)
            return result;

        slang::Bag compilationOptions =
            slang_parse_options::makeCompilationOptions();
        Compilation compilation(compilationOptions);
        compilation.addSyntaxTree(tree);

        slang_symbols::collectSymbols(compilation, result);
    } catch (const std::exception&) {
        result.clear();
    } catch (...) {
        result.clear();
    }
    return result;
}

QList<sym_list::SymbolInfo> SlangManager::extractWorkspaceSymbols(
    const QStringList& filePaths,
    const QStringList& includeDirs,
    const QHash<QString, QString>& defines)
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

        slang::syntax::SyntaxTree::TreeOrError treeOrErr =
            includeDirs.isEmpty() && defines.isEmpty()
                ? slang::syntax::SyntaxTree::fromFiles(pathViews)
                : slang::syntax::SyntaxTree::fromFiles(
                      pathViews,
                      slang::syntax::SyntaxTree::getDefaultSourceManager(),
                      slang_parse_options::makeSyntaxOptions(includeDirs, defines));
        if (!treeOrErr)
            return result;

        std::shared_ptr<slang::syntax::SyntaxTree> tree = std::move(*treeOrErr);
        if (!tree)
            return result;

        slang::Bag compilationOptions =
            slang_parse_options::makeCompilationOptions();
        Compilation compilation(compilationOptions);
        compilation.addSyntaxTree(tree);

        slang_symbols::collectSymbols(compilation, result);
    } catch (const std::exception&) {
        result.clear();
    } catch (...) {
        result.clear();
    }
    return result;
}
