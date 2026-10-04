#include "slangmanager.h"
#include "slangcompilationcollectors.h"
#include "semanticanalysisinput.h"
#include "slangparseoptions.h"
#include "slangpreprocessorfacts.h"
#include "slangsymbolcollector.h"
#include "slangsymbolcollectorhelpers.h"

#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>
#include <slang/ZeroSlackCancellation.h>
#include <QSet>
#include <QDir>
#include <QFileInfo>
#include <QScopeGuard>
#include <stdexcept>
#include <vector>

SlangWorkspaceAnalysis SlangManager::analyzeCapturedWorkspace(
    SemanticInputCapture& input, const QStringList& orderedFiles,
    const QStringList& includeDirs, const QHash<QString, QString>& defines,
    const QString& topModule, const std::function<bool()>& isCancelled,
    SlangAnalysisOutputs outputs)
{
    SlangWorkspaceAnalysis result;
    auto cancelled = [&] { return isCancelled && isCancelled(); };
    if (cancelled()) {
        result.cancelled = true;
        return result;
    }
    try {
        const slang::zeroslack::CancellationScope cancellationScope(isCancelled);
        // The entire Slang object graph belongs to this stack frame. In-memory
        // sources and all (including macro-generated) include lookups pass
        // through the same capture; extractors cannot reread disk independently.
        input.beginCompilationObservation();
        slang::SourceManager sourceManager;
        slang_symbols::detail::resetQTextDocumentSourcePositionCache(&sourceManager);
        const auto releasePositions = qScopeGuard([] {
            slang_symbols::detail::resetQTextDocumentSourcePositionCache(nullptr);
        });
        sourceManager.setDisableProximatePaths(true);
        sourceManager.setSourceLoader([&input](const std::filesystem::path& path,
                                               slang::SmallVector<char>& buffer) {
            const auto utf8 = path.generic_u8string();
            const auto& source = input.source(QString::fromUtf8(
                reinterpret_cast<const char*>(utf8.data()), qsizetype(utf8.size())));
            if (!source.readable)
                return std::make_error_code(std::errc::no_such_file_or_directory);
            const QByteArray bytes = source.text.toUtf8();
            buffer.insert(buffer.end(), bytes.cbegin(), bytes.cend());
            buffer.push_back('\0');
            return std::error_code{};
        });
        std::vector<slang::SourceBuffer> buffers;
        QSet<QString> seen;
        for (const QString& file : orderedFiles) {
            if (cancelled()) {
                result.cancelled = true;
                return result;
            }
            const QString key = SemanticInputCapture::pathKey(file);
            if (seen.contains(key))
                continue;
            seen.insert(key);
            const auto& source = input.source(key);
            if (!source.readable)
                throw std::runtime_error(QStringLiteral("Unable to read semantic source: %1").arg(file).toStdString());
            // Capture keys are case folded on Windows, but source locations
            // retain the spelling supplied by the workspace / editor.
            const std::string path = QDir::cleanPath(QDir::fromNativeSeparators(
                QFileInfo(file).absoluteFilePath())).toUtf8().toStdString();
            const QByteArray bytes = source.text.toUtf8();
            auto buffer = sourceManager.assignText(path,
                std::string_view(bytes.constData(), size_t(bytes.size())));
            sourceManager.addLineDirective(slang::SourceLocation(buffer.id, 0), 2, path, 0);
            buffers.push_back(buffer);
        }
        if (buffers.empty())
            return result;
        const auto tree = slang::syntax::SyntaxTree::fromBuffers(buffers, sourceManager,
            slang_parse_options::makeSyntaxOptions(
                slang_parse_options::effectiveIncludeDirsForFiles(orderedFiles, includeDirs), defines));
        if (!tree)
            throw std::runtime_error("Slang did not produce a syntax tree");
        auto bufferPath = [&](slang::BufferID id) {
            const auto path = sourceManager.getFullPath(id).generic_u8string();
            return SemanticInputCapture::pathKey(QString::fromUtf8(
                reinterpret_cast<const char*>(path.data()), qsizetype(path.size())));
        };
        for (auto id : sourceManager.getAllBuffers()) {
            if (!sourceManager.isFileLoc(slang::SourceLocation(id, 0)))
                continue;
            const QString file = bufferPath(id);
            if (file.isEmpty() || !input.input().sources.contains(file)
                || !input.input().sources.value(file).readable)
                continue;
            result.includesByFile[file];
            if (outputs.relationships)
                result.relationships[file];
            const auto from = sourceManager.getIncludedFrom(id);
            if (from.valid()) {
                const QString parent = bufferPath(sourceManager.getFullyExpandedLoc(from).buffer());
                if (input.input().sources.contains(parent)
                    && !result.includesByFile[parent].contains(file))
                    result.includesByFile[parent].append(file);
            }
        }
        auto options = slang_parse_options::makeCompilationOptions();
        const std::string top = topModule.toUtf8().toStdString();
        if (!top.empty())
            options.insertOrGet<slang::ast::CompilationOptions>().topModules.emplace(top);
        slang::ast::Compilation compilation(options);
        compilation.addSyntaxTree(tree);
        if (outputs.symbols && !cancelled()) {
            slang_symbols::collectSymbolRecords(compilation, result.symbols, isCancelled, &result.effectiveFacts);
            result.symbols.append(slang_preprocessor_facts::collect(*tree));
            for (auto& record : result.symbols)
                record.compilationUnitFileName = orderedFiles.first();
        }
        if (outputs.diagnostics && !cancelled())
            result.diagnostics = slang_collectors::diagnostics(compilation, *tree, isCancelled);
        if (outputs.relationships && !cancelled()) {
            const auto extracted = slang_collectors::relationships(compilation, isCancelled);
            for (auto it = extracted.cbegin(); it != extracted.cend(); ++it) {
                auto& target = result.relationships[SemanticInputCapture::pathKey(it.key())];
                target.moduleInstantiations += it->moduleInstantiations;
                target.subroutineCalls += it->subroutineCalls;
                target.assignments += it->assignments;
                target.conditionReferences += it->conditionReferences;
                target.timingSignals += it->timingSignals;
            }
        }
    } catch (const std::exception& error) {
        result = {};
        result.error = QString::fromUtf8(error.what());
    } catch (...) {
        result = {};
        result.error = QStringLiteral("Slang analysis failed");
    }
    if (cancelled()) {
        result = {};
        result.cancelled = true;
    }
    return result;
}

SlangWorkspaceAnalysis SlangManager::analyzeOverlayWorkspace(
    const QHash<QString, QString>& contents, const QStringList& orderedFiles,
    const QStringList& includeDirs, const QHash<QString, QString>& defines,
    const std::function<bool()>& cancelled, SlangAnalysisOutputs outputs)
{
    SemanticAnalysisRequest request;
    request.sourceOverrides = contents;
    SemanticInputCapture input(request);
    QStringList files;
    QSet<QString> seen;
    for (const QString& file : orderedFiles) {
        const QString key = SemanticInputCapture::pathKey(file);
        if (!seen.contains(key)) {
            seen.insert(key);
            files.append(file);
        }
    }
    QStringList extras;
    for (auto it = contents.cbegin(); it != contents.cend(); ++it)
        if (!seen.contains(SemanticInputCapture::pathKey(it.key())))
            extras.append(it.key());
    extras.sort(Qt::CaseSensitive);
    files.append(extras);
    return analyzeCapturedWorkspace(input, files, includeDirs, defines, {}, cancelled, outputs);
}
