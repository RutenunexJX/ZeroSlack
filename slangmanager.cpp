#include "slangmanager.h"

#include <slang/ast/Compilation.h>
#include <slang/diagnostics/DiagnosticEngine.h>
#include <slang/diagnostics/Diagnostics.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>
#include <slang/util/Bag.h>

#include <QDir>
#include <QFileInfo>
#include <string>

using namespace slang::ast;

namespace {

QString normalizedSlangFileName(std::string_view fileName)
{
    const QString path = QString::fromStdString(std::string(fileName));
    if (path.isEmpty())
        return QString();
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()));
}

SemanticDiagnostic::Severity mapDiagnosticSeverity(slang::DiagnosticSeverity severity)
{
    switch (severity) {
    case slang::DiagnosticSeverity::Warning:
        return SemanticDiagnostic::Warning;
    case slang::DiagnosticSeverity::Error:
    case slang::DiagnosticSeverity::Fatal:
        return SemanticDiagnostic::Error;
    case slang::DiagnosticSeverity::Note:
    default:
        return SemanticDiagnostic::Info;
    }
}

QList<SemanticDiagnostic> collectDiagnostics(slang::ast::Compilation& compilation)
{
    QList<SemanticDiagnostic> result;
    const slang::SourceManager* sm = compilation.getSourceManager();
    if (!sm)
        return result;

    const slang::Diagnostics& diagnostics = compilation.getAllDiagnostics();
    slang::DiagnosticEngine engine(*sm);
    for (const slang::Diagnostic& diagnostic : diagnostics) {
        const slang::DiagnosticSeverity severity =
            engine.getSeverity(diagnostic.code, diagnostic.location);
        if (severity == slang::DiagnosticSeverity::Ignored)
            continue;
        if (!diagnostic.location.valid())
            continue;

        SemanticDiagnostic item;
        item.fileName = normalizedSlangFileName(sm->getFileName(diagnostic.location));
        const size_t line = sm->getLineNumber(diagnostic.location);
        const size_t column = sm->getColumnNumber(diagnostic.location);
        item.line = line == 0 ? 1 : static_cast<int>(line);
        item.column = column == 0 ? 1 : static_cast<int>(column);
        item.message = QString::fromStdString(engine.formatMessage(diagnostic));
        item.severity = mapDiagnosticSeverity(severity);
        result.append(item);
    }
    return result;
}

} // namespace

QList<SemanticDiagnostic> SlangManager::extractDiagnostics(const QString& fileName,
                                                           const QString& content)
{
    QList<SemanticDiagnostic> result;
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
        (void)compilation.getRoot();
        result = collectDiagnostics(compilation);
    } catch (const std::exception&) {
        result.clear();
    } catch (...) {
        result.clear();
    }
    return result;
}

QList<SemanticDiagnostic> SlangManager::extractWorkspaceDiagnostics(const QStringList& filePaths)
{
    QList<SemanticDiagnostic> result;
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
        (void)compilation.getRoot();
        result = collectDiagnostics(compilation);
    } catch (const std::exception&) {
        result.clear();
    } catch (...) {
        result.clear();
    }
    return result;
}
