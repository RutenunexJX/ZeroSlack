#include <zeroslack/semantic/compilationorderservice.h>
#include "slangparseoptions.h"
#include <slang/ZeroSlackCancellation.h>
#include <slang/diagnostics/DiagnosticEngine.h>
#include <slang/syntax/AllSyntax.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/syntax/SyntaxVisitor.h>
#include <slang/text/SourceManager.h>
#include <QDir>
#include <QFileInfo>
#include <algorithm>

namespace {
QString text(std::string_view value) { return QString::fromUtf8(value.data(), qsizetype(value.size())); }
QString pathText(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return QString::fromUtf8(reinterpret_cast<const char*>(value.data()), qsizetype(value.size()));
}
struct Facts : slang::syntax::SyntaxVisitor<Facts> {
    QSet<QString> packages, imports, scopes, classes, units, instances, forwardPackageUses;
    void handle(const slang::syntax::ModuleDeclarationSyntax& node) {
        if (node.kind == slang::syntax::SyntaxKind::PackageDeclaration) packages.insert(text(node.header->name.valueText()));
        else units.insert(text(node.header->name.valueText()));
        visitDefault(node);
    }
    void handle(const slang::syntax::PackageImportItemSyntax& node) {
        const auto name = text(node.package.valueText());
        imports.insert(name);
        if (!packages.contains(name)) forwardPackageUses.insert(name);
    }
    void handle(const slang::syntax::ScopedNameSyntax& node) {
        if (node.separator.kind == slang::parsing::TokenKind::DoubleColon
            && node.left->kind == slang::syntax::SyntaxKind::IdentifierName) {
            const auto name = text(node.left->getFirstToken().valueText());
            scopes.insert(name);
            if (!packages.contains(name)) forwardPackageUses.insert(name);
        }
        visitDefault(node);
    }
    void handle(const slang::syntax::ClassDeclarationSyntax& node) {
        classes.insert(text(node.name.valueText()));
        visitDefault(node);
    }
    void handle(const slang::syntax::TypedefDeclarationSyntax& node) {
        classes.insert(text(node.name.valueText()));
        visitDefault(node);
    }
    void handle(const slang::syntax::ForwardTypedefDeclarationSyntax& node) {
        classes.insert(text(node.name.valueText()));
    }
    void handle(const slang::syntax::HierarchyInstantiationSyntax& node) {
        instances.insert(text(node.type.valueText()));
        visitDefault(node);
    }
};
}

CompilationOrderResult CompilationOrderService::plan(const CompilationOrderRequest& request,
                                                     const std::function<bool()>& cancelled)
{
    CompilationOrderResult result;
    SemanticAnalysisRequest captureRequest;
    captureRequest.sourceOverrides = request.overrides;
    SemanticInputCapture capture(captureRequest);
    QMap<QString, Facts> facts;
    QStringList files = request.files;
    files.removeDuplicates();
    files.sort(Qt::CaseSensitive);
    const auto root = SemanticInputCapture::pathKey(request.workspace);
    auto inside = [&](const QString& path) {
        QFileInfo resolved(path);
        while (!resolved.exists() && resolved.absoluteFilePath() != resolved.absolutePath())
            resolved.setFile(resolved.absolutePath());
        const auto key = SemanticInputCapture::pathKey(resolved.canonicalFilePath());
        return key == root || key.startsWith(root + '/');
    };
    auto label = [&](const QString& path) { return QDir(request.workspace).relativeFilePath(path); };
    try {
        const slang::zeroslack::CancellationScope cancellationScope(cancelled);
        for (const auto& file : files) {
            if (cancelled && cancelled()) { result.diagnostics << QStringLiteral("Compilation analysis cancelled."); break; }
            if (!inside(file)) { result.diagnostics << QStringLiteral("Input is outside the workspace: %1").arg(file); continue; }
            const auto suffix = QFileInfo(file).suffix().toLower();
            const auto& source = capture.source(file);
            if (!source.readable) { result.diagnostics << QStringLiteral("Cannot read build input: %1").arg(label(file)); continue; }
            if (suffix == "vh" || suffix == "svh") { result.headers << file; continue; }
            slang::SourceManager manager;
            manager.setDisableProximatePaths(true);
            manager.setSourceLoader([&](const std::filesystem::path& path, slang::SmallVector<char>& buffer) {
                const auto name = pathText(path);
                if (!inside(name)) {
                    result.diagnostics << QStringLiteral("Include path is outside the workspace: %1").arg(name);
                    return std::make_error_code(std::errc::permission_denied);
                }
                const auto& included = capture.source(name);
                if (!included.readable) return std::make_error_code(std::errc::no_such_file_or_directory);
                const auto bytes = included.text.toUtf8();
                buffer.insert(buffer.end(), bytes.cbegin(), bytes.cend()); buffer.push_back('\0');
                return std::error_code{};
            });
            const auto bytes = source.text.toUtf8();
            auto buffer = manager.assignText(file.toUtf8().toStdString(), std::string_view(bytes.constData(), size_t(bytes.size())));
            const auto tree = slang::syntax::SyntaxTree::fromBuffer(buffer, manager,
                slang_parse_options::makeSyntaxOptions(request.includeDirectories, request.defines));
            slang::DiagnosticEngine engine(manager);
            for (const auto& diagnostic : tree->diagnostics()) {
                const auto severity = engine.getSeverity(diagnostic.code, diagnostic.location);
                if (severity != slang::DiagnosticSeverity::Error && severity != slang::DiagnosticSeverity::Fatal) continue;
                const auto location = manager.getFullyExpandedLoc(diagnostic.location);
                result.diagnostics << QStringLiteral("%1:%2: %3. Check this file, include paths and workspace macro definitions; each file is a separate compilation unit.")
                    .arg(label(pathText(manager.getFullPath(location.buffer())))).arg(manager.getLineNumber(location))
                    .arg(QString::fromStdString(engine.formatMessage(diagnostic)));
                if (result.diagnostics.size() >= 100) break;
            }
            Facts unit;
            tree->root().visit(unit);
            facts.insert(file, unit);
        }
        QMap<QString, QStringList> packages, modules;
        QSet<QString> declaredTypes;
        for (auto it = facts.cbegin(); it != facts.cend(); ++it) {
            for (const auto& name : it->packages) packages[name] << it.key();
            for (const auto& name : it->units) modules[name] << it.key();
            declaredTypes.unite(it->classes);
        }
        for (auto it = packages.cbegin(); it != packages.cend(); ++it)
            if (it->size() > 1) result.diagnostics << QStringLiteral("Package '%1' is compiled more than once: %2. Select a single provider; do not include its definition into multiple compilation units.").arg(it.key(), it->join(", "));
        for (auto it = modules.cbegin(); it != modules.cend(); ++it)
            if (it->size() > 1) result.diagnostics << QStringLiteral("Module/interface '%1' has multiple selected definitions: %2. Uncheck the unwanted definition.").arg(it.key(), it->join(", "));
        for (auto it = facts.cbegin(); it != facts.cend(); ++it) {
            auto required = it->imports;
            for (const auto& scope : it->scopes)
                if (!it->classes.contains(scope) && (packages.contains(scope) || !declaredTypes.contains(scope))
                    && scope != "std" && scope != "$unit") required.insert(scope);
            auto& dependencies = result.dependencies[it.key()];
            for (const auto& name : required) {
                if (name == "std" || name == "$unit") continue;
                if (it->packages.contains(name)) {
                    if (it->forwardPackageUses.contains(name))
                        result.diagnostics << QStringLiteral("%1: package '%2' is used before its declaration in the same compilation unit. Move the package definition before the use, or put it in a separate selected source.").arg(label(it.key()), name);
                    continue;
                }
                const auto providers = packages.value(name);
                if (providers.size() == 1) dependencies << providers.first();
                else if (providers.isEmpty()) result.diagnostics << QStringLiteral("%1: package/type scope '%2' has no selected provider. Add its package source, or correct the include/macro configuration.").arg(label(it.key()), name);
            }
            dependencies.removeDuplicates(); dependencies.sort();
            // Instantiation is an elaboration dependency, never a compile-order edge.
            for (const auto& name : it->instances)
                if (!modules.contains(name)) result.diagnostics << QStringLiteral("%1: module/interface '%2' has no selected definition. Select its source or simulation model.").arg(label(it.key()), name);
        }
        QSet<QString> done;
        while (done.size() < facts.size()) {
            QString next;
            for (auto it = facts.cbegin(); it != facts.cend(); ++it) {
                if (done.contains(it.key())) continue;
                bool ready = true;
                for (const auto& dependency : result.dependencies.value(it.key())) ready &= done.contains(dependency);
                if (ready) { next = it.key(); break; }
            }
            if (next.isEmpty()) {
                QStringList cycle;
                for (auto it = facts.cbegin(); it != facts.cend(); ++it)
                    if (!done.contains(it.key())) cycle << label(it.key());
                result.diagnostics << QStringLiteral("Package dependency cycle: %1. Move shared declarations to a separate package and remove circular imports.").arg(cycle.join(" -> "));
                break;
            }
            done.insert(next); result.files << next;
        }
        if (!capture.stillMatchesDisk(cancelled)) result.diagnostics << QStringLiteral("Build inputs changed during analysis; retry after the files are saved.");
    } catch (const std::exception& error) {
        result.diagnostics << QStringLiteral("Compilation analysis failed: %1").arg(QString::fromUtf8(error.what()));
    }
    result.diagnostics.removeDuplicates();
    result.input = capture.seal();
    if (!result.valid()) result.files.clear();
    return result;
}
