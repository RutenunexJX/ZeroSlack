#include "slangmanager.h"

#include <slang/ast/ASTVisitor.h>
#include <slang/ast/Compilation.h>
#include <slang/ast/symbols/InstanceSymbols.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>
#include <slang/util/Bag.h>

#include <string>

QVector<ModuleInstantiationInfo> SlangManager::extractModuleInstantiations(const QString& fileName,
                                                                           const QString& content)
{
    QVector<ModuleInstantiationInfo> result;
    try {
        std::string src = content.toStdString();
        std::string nameStr = fileName.toStdString();
        // Parse: single-file in-memory content (same pattern as mainwindow verification)
        auto tree = slang::syntax::SyntaxTree::fromText(
            std::string_view(src),
            std::string_view(nameStr),
            std::string_view{});

        if (!tree)
            return result;

        // Elaboration with IgnoreUnknownModules so undefined modules don't abort the run
        slang::Bag bag;
        auto& opts = bag.insertOrGet<slang::ast::CompilationOptions>();
        opts.flags |= slang::ast::CompilationFlags::IgnoreUnknownModules;

        slang::ast::Compilation compilation(bag);
        compilation.addSyntaxTree(tree);
        const slang::ast::RootSymbol& root = compilation.getRoot();
        const slang::SourceManager* sm = compilation.getSourceManager();
        if (!sm)
            return result;

        // Visit all InstanceSymbols (module/interface/program instances), extract name + def name + line
        using namespace slang::ast;
        auto visitor = slang::ast::makeVisitor(
            [&](auto& v, const InstanceSymbol& inst) {
                ModuleInstantiationInfo info;
                info.instanceName = QString::fromStdString(std::string(inst.name));
                info.moduleName = QString::fromStdString(std::string(inst.getDefinition().name));
                size_t line = sm->getLineNumber(inst.location);
                info.lineNumber = (line == 0) ? 1 : static_cast<int>(line);  // Slang is 1-based; 0 = invalid
                result.append(info);
                v.visitDefault(inst);  // recurse into instance body for nested instances
            });

        root.visit(visitor);
    } catch (const std::exception&) {
        // Parsing or elaboration failed; return empty list (caller can still use other analysis)
        result.clear();
    } catch (...) {
        result.clear();
    }
    return result;
}
