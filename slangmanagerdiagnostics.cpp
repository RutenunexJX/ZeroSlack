#include "slangmanager.h"
#include "slangparseoptions.h"

#include <slang/ast/Compilation.h>
#include <slang/diagnostics/DiagnosticEngine.h>
#include <slang/diagnostics/Diagnostics.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>
#include <slang/util/Bag.h>

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <functional>
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

bool appendDiagnostics(const slang::SourceManager& sourceManager,
                       const slang::Diagnostics& diagnostics,
                       QList<SemanticDiagnostic>* result,
                       QSet<QString>* seen,
                       const std::function<bool()>& isCancelled = nullptr)
{
    if (!result || !seen)
        return true;

    slang::DiagnosticEngine engine(sourceManager);
    for (const slang::Diagnostic& diagnostic : diagnostics) {
        if (isCancelled && isCancelled())
            return false;
        const slang::DiagnosticSeverity severity =
            engine.getSeverity(diagnostic.code, diagnostic.location);
        if (severity == slang::DiagnosticSeverity::Ignored)
            continue;
        if (!diagnostic.location.valid())
            continue;

        SemanticDiagnostic item;
        item.fileName =
            normalizedSlangFileName(sourceManager.getFileName(diagnostic.location));
        const size_t line = sourceManager.getLineNumber(diagnostic.location);
        const size_t column = sourceManager.getColumnNumber(diagnostic.location);
        item.line = line == 0 ? 1 : static_cast<int>(line);
        item.column = column == 0 ? 1 : static_cast<int>(column);
        item.message = QString::fromStdString(engine.formatMessage(diagnostic));
        item.severity = mapDiagnosticSeverity(severity);
        item.owner = SemanticDiagnostic::SlangCompiler;

        const QString key = QStringLiteral("%1:%2:%3:%4:%5")
                                .arg(item.fileName)
                                .arg(item.line)
                                .arg(item.column)
                                .arg(static_cast<int>(item.severity))
                                .arg(item.message);
        if (seen->contains(key))
            continue;
        seen->insert(key);
        result->append(item);
    }
    return true;
}

QList<SemanticDiagnostic> collectDiagnostics(slang::ast::Compilation& compilation)
{
    QList<SemanticDiagnostic> result;
    const slang::SourceManager* sm = compilation.getSourceManager();
    if (!sm)
        return result;

    QSet<QString> seen;
    appendDiagnostics(*sm, compilation.getAllDiagnostics(), &result, &seen);
    return result;
}

} // namespace

QList<SemanticDiagnostic> SlangManager::extractDiagnostics(const QString& fileName,
                                                           const QString& content,
                                                           const QStringList& includeDirs,
                                                           const QHash<QString, QString>& defines)
{
    QList<SemanticDiagnostic> result;
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

        QSet<QString> seen;
        appendDiagnostics(tree->sourceManager(), tree->diagnostics(), &result, &seen);

        slang::Bag compilationOptions =
            slang_parse_options::makeCompilationOptions();
        Compilation compilation(compilationOptions);
        compilation.addSyntaxTree(tree);
        (void)compilation.getRoot();
        const slang::SourceManager* sm = compilation.getSourceManager();
        if (sm)
            appendDiagnostics(*sm, compilation.getAllDiagnostics(), &result, &seen);
    } catch (const std::exception&) {
        result.clear();
    } catch (...) {
        result.clear();
    }
    return result;
}

QList<SemanticDiagnostic> SlangManager::extractWorkspaceDiagnostics(
    const QStringList& filePaths,
    const QStringList& includeDirs,
    const QHash<QString, QString>& defines,
    std::function<bool()> isCancelled)
{
    QList<SemanticDiagnostic> result;
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

        QSet<QString> seen;
        if (!appendDiagnostics(tree->sourceManager(),
                               tree->diagnostics(),
                               &result,
                               &seen,
                               isCancelled)) {
            result.clear();
            return result;
        }
        if (cancelled()) {
            result.clear();
            return result;
        }

        slang::Bag compilationOptions =
            slang_parse_options::makeCompilationOptions();
        Compilation compilation(compilationOptions);
        compilation.addSyntaxTree(tree);
        (void)compilation.getRoot();
        if (cancelled()) {
            result.clear();
            return result;
        }
        const slang::SourceManager* sm = compilation.getSourceManager();
        if (sm
            && !appendDiagnostics(*sm,
                                  compilation.getAllDiagnostics(),
                                  &result,
                                  &seen,
                                  isCancelled)) {
            result.clear();
            return result;
        }
        if (cancelled())
            result.clear();
    } catch (const std::exception&) {
        result.clear();
    } catch (...) {
        result.clear();
    }
    return result;
}
