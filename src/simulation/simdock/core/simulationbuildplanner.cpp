#include "simulationbuildplanner.h"
#include "workspace.h"
#include "slangparseoptions.h"
#include "projectmodel.h"
#include <zeroslack/semantic/workspaceconfigurationservice.h>
#include <slang/ZeroSlackCancellation.h>
#include <slang/ast/ASTVisitor.h>
#include <slang/ast/Compilation.h>
#include <slang/diagnostics/AllDiags.h>
#include <slang/diagnostics/DiagnosticEngine.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>
#include <QDir>
#include <QFileInfo>

namespace simdock {
namespace {
QString text(std::string_view value) { return QString::fromUtf8(value.data(), qsizetype(value.size())); }
QString pathText(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return QString::fromUtf8(reinterpret_cast<const char*>(value.data()), qsizetype(value.size()));
}
bool header(const QString& file) {
    const auto suffix = QFileInfo(file).suffix().toLower();
    return suffix == "vh" || suffix == "svh";
}
struct Hierarchy : slang::ast::ASTVisitor<Hierarchy, false, false> {
    const slang::SourceManager& manager;
    explicit Hierarchy(const slang::SourceManager& manager) : manager(manager) {}
    QMap<QString, QString> instances, definitionFiles;
    QSet<QString> lexicalInstances;
    void handle(const slang::ast::GenerateBlockSymbol& block) {
        if (!block.isUninstantiated) visitDefault(block);
    }
    void handle(const slang::ast::InstanceSymbol& instance) {
        const auto path = QString::fromStdString(instance.getHierarchicalPath());
        instances.insert(path, text(instance.getDefinition().name));
        if (const auto* scope = instance.getDefinition().getParentScope(); scope
            && scope->asSymbol().kind != slang::ast::SymbolKind::CompilationUnit
            && scope->asSymbol().kind != slang::ast::SymbolKind::Root)
            lexicalInstances.insert(path);
        const auto location = manager.getFullyExpandedLoc(instance.getDefinition().location);
        if (location.valid()) definitionFiles.insert(path, SemanticInputCapture::pathKey(pathText(manager.getFullPath(location.buffer()))));
        visitDefault(instance);
    }
};
}
SimulationBuildRequest SimulationBuildPlanner::workspaceRequest(const QString& root, const SimulationTarget& target,
    QString* error, const std::function<bool()>& cancelled)
{
    SimulationBuildRequest request;
    request.target = target;
    const auto loaded = WorkspaceConfigurationService().loadWithResult(root);
    if (!loaded.usable()) { *error = loaded.message; return request; }
    const auto& config = loaded.configuration;
    const auto files = WorkspaceConfigurationService::discoverSourceFiles(config, error, cancelled);
    if (!error->isEmpty()) return request;
    ProjectModel model;
    model.setWorkspaceRoot(config.workspaceRoot);
    model.setWorkspaceConfiguration(config.includeDirs, config.defines, config.fileExtensions, config.topModule, config.ignoredDirs);
    model.setScannedFiles(files);
    request.project = model.snapshot();
    return request;
}
std::shared_ptr<const SimulationBuildPlan> SimulationBuildPlanner::plan(
    const SimulationBuildRequest& request, const std::function<bool()>& cancelled)
{
    auto result = std::make_shared<SimulationBuildPlan>();
    result->target = request.target;
    result->defines = request.project.defines;
    result->definitionSources = request.definitionSources;
    auto& errors = result->compilation.diagnostics;
    const auto& root = request.project.workspaceRoot;
    const QDir directory(root);
    auto absolute = [&](const QString& file) { return SemanticInputCapture::pathKey(directory.absoluteFilePath(file)); };
    auto label = [&](const QString& file) { return directory.relativeFilePath(file); };
    auto ignored = [&](const QString& file) {
        const auto key = SemanticInputCapture::pathKey(file);
        for (const auto& ignored : request.project.ignoredPaths) {
            const auto prefix = SemanticInputCapture::pathKey(absolute(ignored));
            if (key == prefix || key.startsWith(prefix + '/')) return true;
        }
        return false;
    };
    if (root.isEmpty() || !request.project.sourceDiscoveryComplete) {
        errors << QStringLiteral("Wait for the workspace file catalog to finish loading."); return result;
    }
    const auto topFile = absolute(request.target.file);
    if (request.target.isEmpty() || !insideWorkspace(root, topFile) || ignored(topFile) || header(topFile)) {
        errors << QStringLiteral("Select a module or program TB in an enabled workspace source file."); return result;
    }
    CompilationOrderRequest order;
    order.workspace = root;
    order.checkInstanceDefinitions = false;
    order.useFileLanguage = true;
    order.defines = request.project.defines;
    for (const auto& path : request.project.includeDirs + request.includeDirectories)
        order.includeDirectories << absolute(path);
    order.includeDirectories << root;
    order.includeDirectories = slang_parse_options::uniqueOrderedIncludeDirs(order.includeDirectories);
    result->includeDirectories = order.includeDirectories;
    for (const auto& path : order.includeDirectories) {
        if (!insideWorkspace(root, path) || !QFileInfo(path).isDir())
            errors << QStringLiteral("Include directory is missing or outside the workspace: %1").arg(path);
    }
    if (!errors.isEmpty()) return result;
    SemanticAnalysisRequest captureRequest;
    captureRequest.project = request.project;
    captureRequest.documentRevisions = request.documentRevisions;
    for (auto it = request.overrides.cbegin(); it != request.overrides.cend(); ++it)
        captureRequest.sourceOverrides.insert(absolute(it.key()), it.value());
    SemanticInputCapture capture(captureRequest);
    capture.source(WorkspaceConfigurationService::projectFilePath(root));
    capture.source(WorkspaceConfigurationService::legacyFilePath(root));
    QMap<QString, CompilationUnitFacts> catalog;
    QMap<QString, QStringList> units, packages;
    QSet<QString> selected{topFile};
    auto choose = [&](const QString& name, bool package, const QString& origin) -> QString {
        const auto candidates = (package ? packages : units).value(name);
        const auto mapped = request.definitionSources.value(name);
        if (!mapped.isEmpty()) {
            const auto path = absolute(mapped);
            if (candidates.contains(path)) return path;
            errors << QStringLiteral("%1: mapping for '%2' does not name an enabled source declaring it: %3.")
                .arg(origin, name, mapped);
            return {};
        }
        if (candidates.size() == 1) return candidates.first();
        if (candidates.isEmpty()) {
            errors << QStringLiteral("%1: no workspace definition for '%2'. Check macro/include settings or provide its simulation model. External libraries and non-Verilog implementations are not supported by this planner.").arg(origin, name);
        } else {
            QStringList labels;
            for (const auto& file : candidates) labels << label(file);
            errors << QStringLiteral("%1: '%2' has multiple definitions: %3. Select its source in Simulation settings.").arg(origin, name, labels.join(", "));
        }
        return {};
    };
    try {
        const slang::zeroslack::CancellationScope cancellationScope(cancelled);
        auto files = request.project.systemVerilogFiles;
        files << topFile;
        QSet<QString> seen;
        for (const auto& path : files) {
            const auto file = absolute(path), key = SemanticInputCapture::pathKey(file);
            if (cancelled && cancelled()) { errors << QStringLiteral("Simulation planning cancelled."); break; }
            if (seen.contains(key) || header(file) || ignored(file)) continue;
            seen.insert(key);
            if (!insideWorkspace(root, file)) continue;
            const auto facts = CompilationOrderService::inspectUnit(order, file, capture, cancelled);
            catalog.insert(file, facts);
            for (const auto& name : facts.units) units[name] << file;
            for (const auto& name : facts.packages) packages[name] << file;
        }
        if (!catalog.value(topFile).simulationTops.contains(request.target.top))
            errors << QStringLiteral("%1: top '%2' is absent or is not a module/program. Select the TB top again.").arg(label(topFile), request.target.top);
        for (int iteration = 0; errors.isEmpty() && iteration <= catalog.size(); ++iteration) {
            if (cancelled && cancelled()) { errors << QStringLiteral("Simulation planning cancelled."); break; }
            const auto count = selected.size();
            QSet<QString> declaredTypes;
            for (const auto& file : selected) declaredTypes.unite(catalog.value(file).classes);
            const auto current = selected;
            for (const auto& file : current) {
                const auto& facts = catalog[file];
                errors += facts.diagnostics;
                auto required = facts.imports;
                for (const auto& scope : facts.scopes)
                    if (!facts.classes.contains(scope) && (packages.contains(scope) || !declaredTypes.contains(scope))) required.insert(scope);
                for (const auto& name : required) {
                    if (name == "std" || name == "$unit" || facts.packages.contains(name)) continue;
                    const auto provider = choose(name, true, label(file));
                    if (!provider.isEmpty()) selected.insert(provider);
                }
            }
            if (!errors.isEmpty()) break;
            if (selected.size() != count) continue;
            order.files = selected.values();
            auto ordered = CompilationOrderService::plan(order, capture, cancelled);
            if (!ordered.valid()) { errors += ordered.diagnostics; break; }
            slang::SourceManager manager;
            manager.setDisableProximatePaths(true);
            manager.setSourceLoader([&](const std::filesystem::path& path, slang::SmallVector<char>& buffer) {
                const auto file = pathText(path);
                if (!insideWorkspace(root, file)) return std::make_error_code(std::errc::permission_denied);
                const auto& source = capture.source(file);
                if (!source.readable) return std::make_error_code(std::errc::no_such_file_or_directory);
                const auto bytes = source.text.toUtf8();
                buffer.insert(buffer.end(), bytes.cbegin(), bytes.cend()); buffer.push_back('\0');
                return std::error_code{};
            });
            slang::Bag options;
            auto& compilationOptions = options.insertOrGet<slang::ast::CompilationOptions>();
            compilationOptions.flags = slang::ast::CompilationFlags::None;
            const auto top = request.target.top.toStdString();
            compilationOptions.topModules.emplace(top);
            compilationOptions.defaultTimeScale = slang::TimeScale::fromString("1ns / 1ps");
            slang::ast::Compilation compilation(options);
            for (const auto& file : ordered.files) {
                const auto bytes = capture.source(file).text.toUtf8();
                auto buffer = manager.assignText(file.toUtf8().toStdString(), std::string_view(bytes.constData(), size_t(bytes.size())));
                compilation.addSyntaxTree(slang::syntax::SyntaxTree::fromBuffer(buffer, manager,
                    slang_parse_options::makeUnitSyntaxOptions(file, order.includeDirectories, order.defines)));
            }
            slang::DiagnosticEngine engine(manager);
            QStringList pendingErrors;
            for (const auto& diagnostic : compilation.getAllDiagnostics()) {
                if (!diagnostic.isError()) continue;
                const auto loc = manager.getFullyExpandedLoc(diagnostic.location);
                const auto origin = loc.valid() ? QStringLiteral("%1:%2").arg(label(pathText(manager.getFullPath(loc.buffer()))))
                    .arg(manager.getLineNumber(loc)) : label(topFile);
                const bool package = diagnostic.code == slang::diag::UnknownPackage
                    || diagnostic.code == slang::diag::UnknownClassOrPackage;
                const bool missing = package || diagnostic.code == slang::diag::UnknownModule
                    || diagnostic.code == slang::diag::UnknownInterface;
                if (missing && !diagnostic.args.empty()) {
                    if (const auto* name = std::get_if<std::string>(&diagnostic.args.front())) {
                        const auto provider = choose(QString::fromStdString(*name), package, origin);
                        if (!provider.isEmpty()) { selected.insert(provider); continue; }
                    }
                }
                pendingErrors << QStringLiteral("%1: %2").arg(origin, QString::fromStdString(engine.formatMessage(diagnostic)));
            }
            Hierarchy hierarchy(manager);
            for (const auto* instance : compilation.getRoot().topInstances) instance->visit(hierarchy);
            QSet<QString> instantiated{request.target.top};
            QMap<QString, QString> dependencies;
            for (auto it = hierarchy.instances.cbegin(); it != hierarchy.instances.cend(); ++it) {
                instantiated.insert(it.value());
                QString provider;
                if (it.value() == request.target.top) provider = topFile;
                else if (!hierarchy.lexicalInstances.contains(it.key()) && units.contains(it.value()))
                    provider = choose(it.value(), false, it.key());
                else {
                    // Nested definitions have lexical scope, not workspace-global providers.
                    const auto definitionFile = hierarchy.definitionFiles.value(it.key());
                    for (const auto& file : selected)
                        if (file == definitionFile || catalog.value(file).inputs.contains(definitionFile)) { provider = file; break; }
                    if (provider.isEmpty()) errors << QStringLiteral("%1: cannot locate the compilation unit of nested definition '%2'.").arg(it.key(), it.value());
                }
                if (!provider.isEmpty()) { selected.insert(provider); dependencies.insert(it.key(), provider); }
            }
            for (auto it = catalog.cbegin(); it != catalog.cend(); ++it) {
                if (selected.contains(it.key())) continue;
                for (const auto& target : it->bindTargets)
                    if (instantiated.contains(target)) { selected.insert(it.key()); break; }
            }
            if (selected.size() != count && errors.isEmpty()) continue;
            errors += pendingErrors;
            if (errors.isEmpty()) {
                result->compilation = std::move(ordered);
                result->elaborationDependencies = std::move(dependencies);
                for (const auto& file : selected) {
                    if (!catalog.value(file).bindTargets.isEmpty()) result->bindCompilationUnits << file;
                    result->participatingFiles << file;
                    for (const auto& input : catalog.value(file).inputs)
                        if (capture.source(input).readable) result->participatingFiles << input;
                }
                result->participatingFiles.removeDuplicates();
                result->participatingFiles.sort();
                result->bindCompilationUnits.sort();
            }
            break;
        }
        if (errors.isEmpty() && result->compilation.files.isEmpty()) errors << QStringLiteral("Simulation dependencies did not converge.");
        if (!capture.stillMatchesDisk(cancelled)) errors << QStringLiteral("Workspace inputs changed during planning; capture and plan again before running.");
    } catch (const std::exception& error) {
        errors << QStringLiteral("Simulation planning failed: %1").arg(QString::fromUtf8(error.what()));
    }
    errors.removeDuplicates();
    result->input = capture.seal();
    if (!errors.isEmpty()) result->compilation.files.clear();
    return result;
}
}
