#include "slangmanager.h"
#include "slangparseoptions.h"
#include "slangsymbolcollectorhelpers.h"

#include <slang/ast/Compilation.h>
#include <slang/ast/symbols/CompilationUnitSymbols.h>
#include <slang/diagnostics/CompilationDiags.h>
#include <slang/diagnostics/DiagnosticEngine.h>
#include <slang/diagnostics/Diagnostics.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>
#include <slang/util/Bag.h>

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <algorithm>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace slang::ast;

namespace {

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
                       const std::function<bool()>& isCancelled = nullptr,
                       std::span<const slang::SourceLocation> packageLocations = {})
{
    if (!result || !seen)
        return true;

    slang::DiagnosticEngine engine(sourceManager);
    for (const slang::Diagnostic& diagnostic : diagnostics) {
        if (isCancelled && isCancelled())
            return false;
        if (diagnostic.code == slang::diag::MissingTimeScale
            && std::find(packageLocations.begin(), packageLocations.end(),
                         diagnostic.location) != packageLocations.end()) {
            continue;
        }
        const slang::DiagnosticSeverity severity =
            engine.getSeverity(diagnostic.code, diagnostic.location);
        if (severity == slang::DiagnosticSeverity::Ignored)
            continue;
        if (!diagnostic.location.valid())
            continue;

        SemanticDiagnostic item;
        const slang_symbols::detail::QTextDocumentSourcePosition location =
            slang_symbols::detail::qTextDocumentSourcePosition(
                &sourceManager, diagnostic.location);
        item.fileName = normalizedDiagnosticSourcePath(location.fileName);
        item.line = location.line > 0 ? location.line : 1;
        item.column = location.column > 0 ? location.column : 1;
        item.message = QString::fromStdString(engine.formatMessage(diagnostic));
        item.codeName = QString::fromStdString(
            std::string(slang::toString(diagnostic.code)));
        item.severity = mapDiagnosticSeverity(severity);
        item.owner = SemanticDiagnostic::SlangCompiler;

        slang::SmallVector<slang::SourceRange> mappedRanges;
        engine.mapSourceRanges(diagnostic.location,
                               diagnostic.ranges,
                               mappedRanges);
        for (const slang::SourceRange& mappedRange : mappedRanges) {
            if (!mappedRange.start().valid()
                || !mappedRange.end().valid()) {
                continue;
            }
            const auto start =
                slang_symbols::detail::qTextDocumentSourcePosition(
                    &sourceManager, mappedRange.start());
            const auto end =
                slang_symbols::detail::qTextDocumentSourcePosition(
                    &sourceManager, mappedRange.end());
            if (!start.isValid()
                || !end.isValid()
                || end.position <= start.position) {
                continue;
            }
            const QString rangeFile =
                normalizedDiagnosticSourcePath(start.fileName);
            if (diagnosticSourceLookupKey(rangeFile)
                != diagnosticSourceLookupKey(item.fileName)) {
                continue;
            }

            SemanticSourceRange range;
            range.fileName = rangeFile;
            range.line = start.line;
            range.column = start.column;
            range.endLine = end.line;
            range.endColumn = end.column;
            range.position = start.position;
            range.length = end.position - start.position;
            item.ranges.append(range);
        }

        if (!item.ranges.isEmpty()) {
            const SemanticSourceRange* primary = &item.ranges.first();
            for (const SemanticSourceRange& range : item.ranges) {
                if (location.position >= range.position
                    && location.position
                        < range.position + range.length) {
                    primary = &range;
                    break;
                }
            }
            item.line = primary->line;
            item.column = primary->column;
        }

        QStringList rangeKeys;
        rangeKeys.reserve(item.ranges.size());
        for (const SemanticSourceRange& range : item.ranges) {
            rangeKeys.append(
                QStringLiteral("%1+%2")
                    .arg(range.position)
                    .arg(range.length));
        }
        const QString key = QStringLiteral("%1:%2:%3:%4:%5:%6:%7")
                                .arg(item.fileName)
                                .arg(item.line)
                                .arg(item.column)
                                .arg(static_cast<int>(item.severity))
                                .arg(item.codeName)
                                .arg(rangeKeys.join(QLatin1Char(',')))
                                .arg(item.message);
        if (seen->contains(key))
            continue;
        seen->insert(key);
        result->append(item);
    }
    return true;
}

std::vector<slang::SourceLocation> packageDeclarationLocations(
    const Compilation& compilation)
{
    std::vector<slang::SourceLocation> locations;
    for (const PackageSymbol* package : compilation.getPackages()) {
        if (package && package->location.valid())
            locations.push_back(package->location);
    }
    return locations;
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
            appendDiagnostics(*sm, compilation.getAllDiagnostics(), &result, &seen,
                              nullptr, packageDeclarationLocations(compilation));
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
                                  isCancelled,
                                  packageDeclarationLocations(compilation))) {
            result.clear();
            return result;
        }
        if (cancelled()) {
            result.clear();
            return result;
        }
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
                                  isCancelled,
                                  packageDeclarationLocations(compilation))) {
            return {};
        }
        if (cancelled())
            return {};

    } catch (const std::exception&) {
        result.clear();
    } catch (...) {
        result.clear();
    }
    return result;
}
