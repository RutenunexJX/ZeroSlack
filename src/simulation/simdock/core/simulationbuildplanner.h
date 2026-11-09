#pragma once
#include "simulationtarget.h"
#include <zeroslack/semantic/compilationorderservice.h>

namespace simdock {
struct SimulationBuildRequest {
    ProjectSnapshot project;
    SimulationTarget target;
    QStringList includeDirectories;
    QMap<QString, QString> definitionSources;
    QHash<QString, QString> overrides;
    QHash<QString, std::uint64_t> documentRevisions;
    QHash<QString, std::uint64_t> documentInstances;
    bool refreshCatalog = false; // Saving participating inputs may create generated sources.
};
struct SimulationBuildPlan {
    SimulationTarget target;
    CompilationOrderResult compilation;
    QStringList includeDirectories, participatingFiles, bindCompilationUnits;
    QHash<QString, QString> defines;
    QMap<QString, QString> definitionSources;
    QMap<QString, QString> elaborationDependencies;
    // Includes candidate lookups, so introducing a competing definition invalidates the plan.
    std::shared_ptr<const SemanticAnalysisInput> input;
    QString error() const { return compilation.diagnostics.join('\n'); }
    bool valid() const { return compilation.valid(); }
};
class SimulationBuildPlanner {
public:
    static SimulationBuildRequest workspaceRequest(const QString& root, const SimulationTarget& target,
        QString* error, const std::function<bool()>& cancelled = {});
    static std::shared_ptr<const SimulationBuildPlan> plan(const SimulationBuildRequest& request,
        const std::function<bool()>& cancelled = {});
};
}
