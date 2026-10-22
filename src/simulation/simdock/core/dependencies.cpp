#include "dependencies.h"
#include "workspace.h"
#include <zeroslack/semantic/workspaceconfigurationservice.h>
#include <QDir>
#include <QFileInfo>

namespace simdock {
BuildInputPlan planBuildInputs(const QString& root, const Project& project,
                              const std::atomic_bool* cancelled, const QMap<QString, QByteArray>& overrides)
{
    BuildInputPlan result;
    const auto configuration = WorkspaceConfigurationService().loadWithResult(root);
    if (!configuration.usable()) {
        result.order.diagnostics << configuration.message;
        return result;
    }
    CompilationOrderRequest request;
    request.workspace = root;
    request.defines = configuration.configuration.defines;
    result.defines = request.defines;
    QStringList selected = project.sources;
    if (!project.tbFile.isEmpty()) selected << project.tbFile;
    selected.removeDuplicates(); selected.sort();
    for (const auto& file : selected) request.files << QDir(root).filePath(file);
    request.includeDirectories << root;
    for (const auto& directory : project.includeDirectories + configuration.configuration.includeDirs) {
        const auto path = QDir(root).absoluteFilePath(directory);
        if (!insideWorkspace(root, path) || !QFileInfo(path).isDir())
            result.order.diagnostics << QStringLiteral("Include directory is missing or outside the workspace: %1").arg(directory);
        request.includeDirectories << path;
    }
    // Include precedence is independent of the derived compilation order.
    QStringList directories;
    for (const auto& file : request.files) directories << QFileInfo(file).absolutePath();
    directories.removeDuplicates(); directories.sort();
    request.includeDirectories += directories;
    request.includeDirectories.removeDuplicates();
    result.includeDirectories = request.includeDirectories;
    for (auto it = overrides.cbegin(); it != overrides.cend(); ++it)
        request.overrides.insert(QDir(root).filePath(it.key()), QString::fromUtf8(it.value()));
    if (!result.order.diagnostics.isEmpty()) return result;
    result.order = CompilationOrderService::plan(request, [cancelled] { return cancelled && cancelled->load(); });
    return result;
}
void BuildInputPlan::apply(Project& project, const QString& root) const
{
    if (!order.valid()) return;
    QStringList selected, units;
    for (const auto& file : order.files) {
        const auto relative = QDir(root).relativeFilePath(file);
        units << relative;
        if (project.sources.contains(relative)) selected << relative;
    }
    project.compilationUnits = units;
    for (const auto& file : order.headers) selected << QDir(root).relativeFilePath(file);
    project.sources = selected;
    project.compilationIncludeDirectories = includeDirectories;
    project.compilationDefines = defines;
}

}
