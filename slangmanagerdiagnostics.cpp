#include "slangmanager.h"
#include "slangparseoptions.h"
#include "slangsymbolcollectorhelpers.h"
#include "svmacrosemantics.h"

#include <slang/ast/Compilation.h>
#include <slang/diagnostics/DiagnosticEngine.h>
#include <slang/diagnostics/Diagnostics.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>
#include <slang/util/Bag.h>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QTextStream>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace slang::ast;

namespace {

QString readMacroDiagnosticTextFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QFile::Text))
        return QString();
    QTextStream stream(&file);
    return stream.readAll();
}

QString normalizedDiagnosticSourcePath(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

QString diagnosticSourceLookupKey(const QString& fileName)
{
    QString key = normalizedDiagnosticSourcePath(fileName);
#ifdef Q_OS_WIN
    key = key.toCaseFolded();
#endif
    return key;
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
        item.fileName = normalizedDiagnosticSourcePath(
            slang_symbols::detail::sourceIdentityFileName(
                &sourceManager, diagnostic.location));
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

QList<SemanticDiagnostic> workspaceUndefinedMacroDiagnostics(
    const QStringList& filePaths,
    const QHash<QString, QString>& defines,
    const std::function<bool()>& isCancelled)
{
    QList<SemanticDiagnostic> diagnostics;
    QHash<QString, QString> contentsByFile;
    QSet<QString> visibleMacros = SvMacroSemantics::configuredDefineNames(defines);

    for (const QString& filePath : filePaths) {
        if (isCancelled && isCancelled())
            return {};
        const QString content = readMacroDiagnosticTextFile(filePath);
        if (content.isEmpty())
            continue;
        contentsByFile.insert(filePath, content);
        for (const SemanticSymbolRecord& record :
             SvMacroSemantics::collectMacroDefinitionRecords(filePath, content)) {
            if (!record.name.isEmpty())
                visibleMacros.insert(record.name);
        }
    }

    for (auto it = contentsByFile.constBegin();
         it != contentsByFile.constEnd();
         ++it) {
        if (isCancelled && isCancelled())
            return {};
        diagnostics.append(
            SvMacroSemantics::undefinedMacroDiagnostics(it.key(),
                                                        it.value(),
                                                        visibleMacros));
    }
    return diagnostics;
}

QList<SemanticDiagnostic> overlayWorkspaceUndefinedMacroDiagnostics(
    const QStringList& filePaths,
    const QHash<QString, QString>& contentsByKey,
    const QHash<QString, QString>& defines,
    const std::function<bool()>& isCancelled)
{
    QList<SemanticDiagnostic> diagnostics;
    QSet<QString> visibleMacros =
        SvMacroSemantics::configuredDefineNames(defines);

    for (const QString& filePath : filePaths) {
        if (isCancelled && isCancelled())
            return {};
        const QString content = contentsByKey.value(
            diagnosticSourceLookupKey(filePath));
        for (const SemanticSymbolRecord& record :
             SvMacroSemantics::collectMacroDefinitionRecords(filePath,
                                                              content)) {
            if (!record.name.isEmpty())
                visibleMacros.insert(record.name);
        }
    }

    for (const QString& filePath : filePaths) {
        if (isCancelled && isCancelled())
            return {};
        diagnostics.append(
            SvMacroSemantics::undefinedMacroDiagnostics(
                filePath,
                contentsByKey.value(diagnosticSourceLookupKey(filePath)),
                visibleMacros));
    }
    return diagnostics;
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
        result.append(SvMacroSemantics::undefinedMacroDiagnostics(
            fileName,
            content,
            SvMacroSemantics::configuredDefineNames(defines)));
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
        if (cancelled()) {
            result.clear();
            return result;
        }
        result.append(workspaceUndefinedMacroDiagnostics(filePaths,
                                                        defines,
                                                        isCancelled));
        if (cancelled())
            result.clear();
    } catch (const std::exception&) {
        result.clear();
    } catch (...) {
        result.clear();
    }
    return result;
}

QList<SemanticDiagnostic> SlangManager::extractOverlayWorkspaceDiagnostics(
    const QHash<QString, QString>& fileContents,
    const QStringList& includeDirs,
    const QHash<QString, QString>& defines,
    std::function<bool()> isCancelled,
    const QStringList& orderedFilePaths)
{
    QList<SemanticDiagnostic> result;
    if (fileContents.isEmpty())
        return result;
    auto cancelled = [&]() {
        return isCancelled && isCancelled();
    };
    if (cancelled())
        return result;

    try {
        QHash<QString, QString> contentsByKey;
        QHash<QString, QString> pathsByKey;
        for (auto it = fileContents.constBegin();
             it != fileContents.constEnd();
             ++it) {
            if (cancelled())
                return {};
            const QString path = normalizedDiagnosticSourcePath(it.key());
            const QString key = diagnosticSourceLookupKey(path);
            if (key.isEmpty())
                continue;
            pathsByKey.insert(key, path);
            contentsByKey.insert(key, it.value());
        }

        QStringList fileNames;
        QSet<QString> added;
        for (const QString& requestedPath : orderedFilePaths) {
            if (cancelled())
                return {};
            const QString key = diagnosticSourceLookupKey(requestedPath);
            if (key.isEmpty() || added.contains(key)
                || !contentsByKey.contains(key)) {
                continue;
            }
            added.insert(key);
            fileNames.append(normalizedDiagnosticSourcePath(requestedPath));
        }
        QStringList remainingKeys = contentsByKey.keys();
        remainingKeys.sort(Qt::CaseInsensitive);
        for (const QString& key : std::as_const(remainingKeys)) {
            if (cancelled())
                return {};
            if (added.contains(key))
                continue;
            added.insert(key);
            fileNames.append(pathsByKey.value(key));
        }
        if (fileNames.isEmpty() || cancelled())
            return {};

        const QStringList effectiveIncludeDirs =
            slang_parse_options::effectiveIncludeDirsForFiles(fileNames,
                                                              includeDirs);
        const slang::Bag syntaxOptions =
            slang_parse_options::makeSyntaxOptions(effectiveIncludeDirs,
                                                   defines);
        slang::SourceManager sourceManager;
        sourceManager.setDisableProximatePaths(true);
        std::vector<std::string> pathStrings;
        std::vector<slang::SourceBuffer> sourceBuffers;
        pathStrings.reserve(static_cast<std::size_t>(fileNames.size()));
        sourceBuffers.reserve(static_cast<std::size_t>(fileNames.size()));
        for (const QString& fileName : std::as_const(fileNames)) {
            if (cancelled())
                return {};
            const QByteArray sourceBytes = contentsByKey.value(
                diagnosticSourceLookupKey(fileName)).toUtf8();
            pathStrings.push_back(fileName.toUtf8().toStdString());
            slang::SourceBuffer sourceBuffer = sourceManager.assignText(
                std::string_view(pathStrings.back()),
                std::string_view(
                    sourceBytes.constData(),
                    static_cast<std::size_t>(sourceBytes.size())));
            sourceManager.addLineDirective(
                slang::SourceLocation(sourceBuffer.id, 0),
                2,
                std::string_view(pathStrings.back()),
                0);
            sourceBuffers.push_back(sourceBuffer);
        }
        if (sourceBuffers.empty() || cancelled())
            return {};

        std::shared_ptr<slang::syntax::SyntaxTree> tree =
            slang::syntax::SyntaxTree::fromBuffers(sourceBuffers,
                                                   sourceManager,
                                                   syntaxOptions);
        if (!tree || cancelled())
            return {};

        QSet<QString> seen;
        if (!appendDiagnostics(tree->sourceManager(),
                               tree->diagnostics(),
                               &result,
                               &seen,
                               isCancelled)) {
            return {};
        }
        if (cancelled())
            return {};

        slang::Bag compilationOptions =
            slang_parse_options::makeCompilationOptions();
        Compilation compilation(compilationOptions);
        compilation.addSyntaxTree(tree);
        (void)compilation.getRoot();
        if (cancelled())
            return {};
        const slang::SourceManager* sm = compilation.getSourceManager();
        if (sm
            && !appendDiagnostics(*sm,
                                  compilation.getAllDiagnostics(),
                                  &result,
                                  &seen,
                                  isCancelled)) {
            return {};
        }
        if (cancelled())
            return {};

        result.append(overlayWorkspaceUndefinedMacroDiagnostics(
            fileNames,
            contentsByKey,
            defines,
            isCancelled));
        if (cancelled())
            return {};
    } catch (const std::exception&) {
        result.clear();
    } catch (...) {
        result.clear();
    }
    return result;
}
