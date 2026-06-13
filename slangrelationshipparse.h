#ifndef SLANGRELATIONSHIPPARSE_H
#define SLANGRELATIONSHIPPARSE_H

#include <slang/ast/Compilation.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>
#include <slang/util/Bag.h>

#include <QString>

#include <exception>
#include <string>
#include <string_view>

namespace slang_relationship {

template <typename Result, typename VisitorFactory>
Result extractFromText(const QString& fileName,
                       const QString& content,
                       VisitorFactory makeRelationshipVisitor)
{
    Result result;
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
        auto& opts = bag.insertOrGet<slang::ast::CompilationOptions>();
        opts.flags |= slang::ast::CompilationFlags::IgnoreUnknownModules;

        slang::ast::Compilation compilation(bag);
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
