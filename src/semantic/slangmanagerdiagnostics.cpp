#include "slangmanager.h"
#include "slangcompilationcollectors.h"
#include "semanticanalysisinput.h"
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

QList<SemanticDiagnostic> slang_collectors::diagnostics(
    Compilation& compilation, slang::syntax::SyntaxTree& tree,
    const std::function<bool()>& cancelled)
{
    QList<SemanticDiagnostic> result;
    QSet<QString> seen;
    if (!appendDiagnostics(tree.sourceManager(), tree.diagnostics(), &result, &seen, cancelled))
        return {};
    (void)compilation.getRoot();
    const auto* manager = compilation.getSourceManager();
    if (manager && !appendDiagnostics(*manager, compilation.getAllDiagnostics(),
        &result, &seen, cancelled, packageDeclarationLocations(compilation)))
        return {};
    return result;
}

QList<SemanticDiagnostic> SlangManager::extractDiagnostics(
    const QString& fileName, const QString& content,
    const QStringList& includeDirs, const QHash<QString, QString>& defines)
{
    return extractOverlayWorkspaceDiagnostics({{fileName, content}}, includeDirs,
                                               defines, {}, {fileName});
}

QList<SemanticDiagnostic> SlangManager::extractWorkspaceDiagnostics(
    const QStringList& filePaths, const QStringList& includeDirs,
    const QHash<QString, QString>& defines, std::function<bool()> isCancelled)
{
    SemanticInputCapture input(SemanticAnalysisRequest{});
    return analyzeCapturedWorkspace(input, filePaths, includeDirs, defines, {},
                                     isCancelled, {false, true, false}).diagnostics;
}

QList<SemanticDiagnostic> SlangManager::extractOverlayWorkspaceDiagnostics(
    const QHash<QString, QString>& contents, const QStringList& includeDirs,
    const QHash<QString, QString>& defines, std::function<bool()> isCancelled,
    const QStringList& orderedFilePaths)
{
    return analyzeOverlayWorkspace(contents, orderedFilePaths, includeDirs, defines,
                                    isCancelled, {false, true, false}).diagnostics;
}
