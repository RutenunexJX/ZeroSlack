#include "slangmanager.h"
#include "slangparseoptions.h"
#include "slangsymbolcollector.h"
#include "svmacrosemantics.h"

#include <slang/ast/Compilation.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>
#include <slang/util/Bag.h>

#include <QFile>
#include <QTextStream>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace slang::ast;

namespace {
QString readMacroScanTextFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QFile::Text))
        return QString();
    QTextStream stream(&file);
    return stream.readAll();
}
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
        result.append(
            SvMacroSemantics::collectMacroDefinitionRecords(fileName, content));
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
    const QHash<QString, QString>& defines,
    std::function<bool()> isCancelled)
{
    QList<SemanticSymbolRecord> result;
    if (filePaths.isEmpty())
        return result;
    auto cancelled = [&]() {
        return isCancelled && isCancelled();
    };
    if (cancelled())
        return result;
    try {
        std::vector<std::string> pathStrs;
        pathStrs.reserve(filePaths.size());
        for (const QString& p : filePaths) {
            if (cancelled())
                return result;
            pathStrs.push_back(p.toStdString());
        }
        const QStringList effectiveIncludeDirs =
            slang_parse_options::effectiveIncludeDirsForFiles(filePaths, includeDirs);
        if (cancelled())
            return result;

        std::vector<std::string_view> pathViews;
        pathViews.reserve(pathStrs.size());
        for (const std::string& s : pathStrs) {
            if (cancelled())
                return result;
            pathViews.push_back(s);
        }
        if (cancelled())
            return result;

        slang::SourceManager sourceManager;
        slang::syntax::SyntaxTree::TreeOrError treeOrErr =
            effectiveIncludeDirs.isEmpty() && defines.isEmpty()
                ? slang::syntax::SyntaxTree::fromFiles(pathViews, sourceManager)
                : slang::syntax::SyntaxTree::fromFiles(
                      pathViews,
                      sourceManager,
                      slang_parse_options::makeSyntaxOptions(effectiveIncludeDirs, defines));
        if (cancelled())
            return result;
        if (!treeOrErr)
            return result;

        std::shared_ptr<slang::syntax::SyntaxTree> tree = std::move(*treeOrErr);
        if (!tree)
            return result;
        if (cancelled())
            return result;

        slang::Bag compilationOptions =
            slang_parse_options::makeCompilationOptions();
        Compilation compilation(compilationOptions);
        compilation.addSyntaxTree(tree);
        if (cancelled())
            return result;

        slang_symbols::collectSymbolRecords(compilation, result, isCancelled);
        if (cancelled())
            return result;
        for (const QString& filePath : filePaths) {
            if (cancelled())
                return result;
            const QString content = readMacroScanTextFile(filePath);
            if (content.isEmpty())
                continue;
            result.append(
                SvMacroSemantics::collectMacroDefinitionRecords(filePath, content));
        }
    } catch (const std::exception&) {
        result.clear();
    } catch (...) {
        result.clear();
    }
    return result;
}
