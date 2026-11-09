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
#include <QCache>
#include <QCryptographicHash>
#include <QDataStream>
#include <QMutex>
#include <QMutexLocker>
#include <algorithm>

namespace {
QString text(std::string_view value) { return QString::fromUtf8(value.data(), qsizetype(value.size())); }
QString pathText(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return QString::fromUtf8(reinterpret_cast<const char*>(value.data()), qsizetype(value.size()));
}
struct Facts : CompilationUnitFacts, slang::syntax::SyntaxVisitor<Facts> {
    int depth = 0;
    void handle(const slang::syntax::ModuleDeclarationSyntax& node) {
        if (node.kind == slang::syntax::SyntaxKind::PackageDeclaration) packages.insert(text(node.header->name.valueText()));
        else if (!depth) units.insert(text(node.header->name.valueText()));
        if (!depth && (node.kind == slang::syntax::SyntaxKind::ModuleDeclaration
            || node.kind == slang::syntax::SyntaxKind::ProgramDeclaration))
            simulationTops.insert(text(node.header->name.valueText()));
        ++depth;
        visitDefault(node);
        --depth;
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
    void handle(const slang::syntax::BindDirectiveSyntax& node) {
        if (!depth) bindTargets.insert(text(node.target->getFirstToken().valueText()));
        visitDefault(node);
    }
};
bool inside(const QString& workspace, const QString& path)
{
    QFileInfo resolved(path);
    while (!resolved.exists() && resolved.absoluteFilePath() != resolved.absolutePath())
        resolved.setFile(resolved.absolutePath());
    const auto key = SemanticInputCapture::pathKey(resolved.canonicalFilePath());
    const auto root = SemanticInputCapture::pathKey(workspace);
    return key == root || key.startsWith(root + '/');
}
struct CachedUnit {
    CompilationUnitFacts facts;
    QHash<QString, SemanticCapturedSource> inputs;
};
QMutex cacheMutex;
QCache<QByteArray, CachedUnit> unitCache(16 * 1024); // KiB, including captured include text.
}

CompilationUnitFacts CompilationOrderService::inspectUnit(const CompilationOrderRequest& request,
    const QString& file, SemanticInputCapture& capture, const std::function<bool()>& cancelled)
{
    CompilationUnitFacts result;
    if (cancelled && cancelled()) { result.diagnostics << QStringLiteral("Compilation analysis cancelled."); return result; }
    if (!inside(request.workspace, file)) { result.diagnostics << QStringLiteral("Input is outside the workspace: %1").arg(file); return result; }
    QByteArray key;
    QDataStream stream(&key, QIODevice::WriteOnly);
    QMap<QString, QString> defines;
    for (auto it = request.defines.cbegin(); it != request.defines.cend(); ++it) defines.insert(it.key(), it.value());
    stream << request.workspace << file << request.includeDirectories << defines << request.useFileLanguage;
    key = QCryptographicHash::hash(key, QCryptographicHash::Sha256);
    std::optional<CachedUnit> cached;
    {
        QMutexLocker guard(&cacheMutex);
        if (const auto* entry = unitCache.object(key)) cached = *entry;
    }
    if (cached) {
        bool current = true;
        for (auto it = cached->inputs.cbegin(); it != cached->inputs.cend(); ++it) {
            if (cancelled && cancelled()) { result.diagnostics << QStringLiteral("Compilation analysis cancelled."); return result; }
            if (capture.source(it.key()) != it.value()) current = false;
        }
        if (current) return cached->facts;
    }
    CachedUnit entry;
    auto read = [&](const QString& path) -> const SemanticCapturedSource& {
        const auto& source = capture.source(path);
        entry.inputs.insert(SemanticInputCapture::pathKey(path), source);
        return source;
    };
    auto label = [&](const QString& path) { return QDir(request.workspace).relativeFilePath(path); };
    const auto source = read(file);
    if (!source.readable) { result.diagnostics << QStringLiteral("Cannot read build input: %1").arg(label(file)); return result; }
    const slang::zeroslack::CancellationScope cancellationScope(cancelled);
    slang::SourceManager manager;
    manager.setDisableProximatePaths(true);
    manager.setSourceLoader([&](const std::filesystem::path& path, slang::SmallVector<char>& buffer) {
        const auto name = pathText(path);
        if (!inside(request.workspace, name)) {
            result.diagnostics << QStringLiteral("Include path is outside the workspace: %1").arg(name);
            return std::make_error_code(std::errc::permission_denied);
        }
        const auto& included = read(name);
        if (!included.readable) return std::make_error_code(std::errc::no_such_file_or_directory);
        const auto bytes = included.text.toUtf8();
        buffer.insert(buffer.end(), bytes.cbegin(), bytes.cend()); buffer.push_back('\0');
        return std::error_code{};
    });
    const auto bytes = source.text.toUtf8();
    auto buffer = manager.assignText(file.toUtf8().toStdString(), std::string_view(bytes.constData(), size_t(bytes.size())));
    const auto tree = slang::syntax::SyntaxTree::fromBuffer(buffer, manager,
        request.useFileLanguage ? slang_parse_options::makeUnitSyntaxOptions(file, request.includeDirectories, request.defines)
                                : slang_parse_options::makeSyntaxOptions(request.includeDirectories, request.defines));
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
    Facts facts;
    tree->root().visit(facts);
    facts.diagnostics = result.diagnostics;
    facts.inputs = entry.inputs.keys();
    facts.inputs.sort();
    entry.facts = facts;
    qsizetype cost = 4096;
    for (auto it = entry.inputs.cbegin(); it != entry.inputs.cend(); ++it) cost += it->text.size() * 2 + it.key().size() * 2 + 128;
    {
        QMutexLocker guard(&cacheMutex);
        unitCache.insert(key, new CachedUnit(entry), int(qMin<qsizetype>(INT_MAX, (cost + 1023) / 1024)));
    }
    return entry.facts;
}

CompilationOrderResult CompilationOrderService::plan(const CompilationOrderRequest& request,
                                                     const std::function<bool()>& cancelled)
{
    SemanticAnalysisRequest captureRequest;
    captureRequest.sourceOverrides = request.overrides;
    SemanticInputCapture capture(captureRequest);
    return plan(request, capture, cancelled);
}

CompilationOrderResult CompilationOrderService::plan(const CompilationOrderRequest& request,
    SemanticInputCapture& capture, const std::function<bool()>& cancelled)
{
    CompilationOrderResult result;
    QMap<QString, CompilationUnitFacts> facts;
    QStringList files = request.files;
    files.removeDuplicates();
    files.sort(Qt::CaseSensitive);
    auto label = [&](const QString& path) { return QDir(request.workspace).relativeFilePath(path); };
    try {
        const slang::zeroslack::CancellationScope cancellationScope(cancelled);
        for (const auto& file : files) {
            if (cancelled && cancelled()) { result.diagnostics << QStringLiteral("Compilation analysis cancelled."); break; }
            if (!inside(request.workspace, file)) { result.diagnostics << QStringLiteral("Input is outside the workspace: %1").arg(file); continue; }
            const auto suffix = QFileInfo(file).suffix().toLower();
            const auto& source = capture.source(file);
            if (!source.readable) { result.diagnostics << QStringLiteral("Cannot read build input: %1").arg(label(file)); continue; }
            if (suffix == "vh" || suffix == "svh") { result.headers << file; continue; }
            const auto unit = inspectUnit(request, file, capture, cancelled);
            result.diagnostics += unit.diagnostics;
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
            if (request.checkInstanceDefinitions)
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
