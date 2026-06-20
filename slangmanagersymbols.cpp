#include "slangmanager.h"
#include "slangparseoptions.h"
#include "slangsymbolcollector.h"

#include <slang/ast/Compilation.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>
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
        const QStringList effectiveIncludeDirs =
            slang_parse_options::effectiveIncludeDirsForFile(fileName, includeDirs);
        slang::SourceManager sourceManager;
        std::shared_ptr<slang::syntax::SyntaxTree> tree;
        if (effectiveIncludeDirs.isEmpty() && defines.isEmpty()) {
            tree = slang::syntax::SyntaxTree::fromText(
                std::string_view(src),
                sourceManager,
                std::string_view(nameStr),
                std::string_view{});
        } else {
            slang::Bag syntaxOptions =
                slang_parse_options::makeSyntaxOptions(effectiveIncludeDirs, defines);
            tree = slang::syntax::SyntaxTree::fromText(
                std::string_view(src),
                sourceManager,
                std::string_view(nameStr),
                std::string_view(nameStr),
                syntaxOptions);
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

QList<SemanticSymbolRecord> SlangManager::extractSymbolRecords(
    const QString& fileName,
    const QString& content,
    const QStringList& includeDirs,
    const QHash<QString, QString>& defines)
{
    QList<SemanticSymbolRecord> result;
    try {
        std::string src = content.toStdString();
        std::string nameStr = fileName.toStdString();
        const QStringList effectiveIncludeDirs =
            slang_parse_options::effectiveIncludeDirsForFile(fileName, includeDirs);
        slang::SourceManager sourceManager;
        std::shared_ptr<slang::syntax::SyntaxTree> tree;
        if (effectiveIncludeDirs.isEmpty() && defines.isEmpty()) {
            tree = slang::syntax::SyntaxTree::fromText(
                std::string_view(src),
                sourceManager,
                std::string_view(nameStr),
                std::string_view{});
        } else {
            slang::Bag syntaxOptions =
                slang_parse_options::makeSyntaxOptions(effectiveIncludeDirs, defines);
            tree = slang::syntax::SyntaxTree::fromText(
                std::string_view(src),
                sourceManager,
                std::string_view(nameStr),
                std::string_view(nameStr),
                syntaxOptions);
        }

        if (!tree)
            return result;

        slang::Bag compilationOptions =
            slang_parse_options::makeCompilationOptions();
        Compilation compilation(compilationOptions);
        compilation.addSyntaxTree(tree);

        slang_symbols::collectSymbolRecords(compilation, result);
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
        const QStringList effectiveIncludeDirs =
            slang_parse_options::effectiveIncludeDirsForFiles(filePaths, includeDirs);

        std::vector<std::string_view> pathViews;
        pathViews.reserve(pathStrs.size());
        for (const std::string& s : pathStrs)
            pathViews.push_back(s);

        slang::SourceManager sourceManager;
        slang::syntax::SyntaxTree::TreeOrError treeOrErr =
            effectiveIncludeDirs.isEmpty() && defines.isEmpty()
                ? slang::syntax::SyntaxTree::fromFiles(pathViews, sourceManager)
                : slang::syntax::SyntaxTree::fromFiles(
                      pathViews,
                      sourceManager,
                      slang_parse_options::makeSyntaxOptions(effectiveIncludeDirs, defines));
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

QList<SemanticSymbolRecord> SlangManager::extractWorkspaceSymbolRecords(
    const QStringList& filePaths,
    const QStringList& includeDirs,
    const QHash<QString, QString>& defines)
{
    QList<SemanticSymbolRecord> result;
    if (filePaths.isEmpty())
        return result;
    try {
        std::vector<std::string> pathStrs;
        pathStrs.reserve(filePaths.size());
        for (const QString& p : filePaths)
            pathStrs.push_back(p.toStdString());
        const QStringList effectiveIncludeDirs =
            slang_parse_options::effectiveIncludeDirsForFiles(filePaths, includeDirs);

        std::vector<std::string_view> pathViews;
        pathViews.reserve(pathStrs.size());
        for (const std::string& s : pathStrs)
            pathViews.push_back(s);

        slang::SourceManager sourceManager;
        slang::syntax::SyntaxTree::TreeOrError treeOrErr =
            effectiveIncludeDirs.isEmpty() && defines.isEmpty()
                ? slang::syntax::SyntaxTree::fromFiles(pathViews, sourceManager)
                : slang::syntax::SyntaxTree::fromFiles(
                      pathViews,
                      sourceManager,
                      slang_parse_options::makeSyntaxOptions(effectiveIncludeDirs, defines));
        if (!treeOrErr)
            return result;

        std::shared_ptr<slang::syntax::SyntaxTree> tree = std::move(*treeOrErr);
        if (!tree)
            return result;

        slang::Bag compilationOptions =
            slang_parse_options::makeCompilationOptions();
        Compilation compilation(compilationOptions);
        compilation.addSyntaxTree(tree);

        slang_symbols::collectSymbolRecords(compilation, result);
    } catch (const std::exception&) {
        result.clear();
    } catch (...) {
        result.clear();
    }
    return result;
}
