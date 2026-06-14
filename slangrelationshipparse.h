#ifndef SLANGRELATIONSHIPPARSE_H
#define SLANGRELATIONSHIPPARSE_H

#include <slang/ast/Compilation.h>
#include <slang/ast/Symbol.h>
#include <slang/ast/symbols/CompilationUnitSymbols.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>
#include <slang/util/Bag.h>

#include "slangparseoptions.h"

#include <QHash>
#include <QString>
#include <QStringList>

#include <exception>
#include <string>
#include <string_view>

namespace slang_relationship {

template <typename Result, typename VisitorFactory>
Result extractFromText(const QString& fileName,
                       const QString& content,
                       VisitorFactory makeRelationshipVisitor,
                       const QStringList& includeDirs = {},
                       const QHash<QString, QString>& defines = {})
{
    Result result;
    try {
        std::string src = content.toStdString();
        std::string nameStr = fileName.toStdString();
        slang::SourceManager sourceManager;
        std::shared_ptr<slang::syntax::SyntaxTree> tree;
        if (includeDirs.isEmpty() && defines.isEmpty()) {
            tree = slang::syntax::SyntaxTree::fromText(
                std::string_view(src),
                sourceManager,
                std::string_view(nameStr),
                std::string_view{});
        } else {
            slang::Bag syntaxOptions =
                slang_parse_options::makeSyntaxOptions(includeDirs, defines);
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
        slang::ast::Compilation compilation(compilationOptions);
        compilation.addSyntaxTree(tree);
        const slang::ast::RootSymbol& root = compilation.getRoot();
        const slang::SourceManager* sm = compilation.getSourceManager();
        if (!sm)
            return result;

        auto visitor = makeRelationshipVisitor(result, sm);
        root.visit(visitor);
    } catch (const std::exception&) {
        result = Result();
    } catch (...) {
        result = Result();
    }
    return result;
}

} // namespace slang_relationship

#endif // SLANGRELATIONSHIPPARSE_H
