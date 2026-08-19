#ifndef WAVESIMULATIONMANIFESTSERVICE_H
#define WAVESIMULATIONMANIFESTSERVICE_H

#include "projectmodel.h"
#include "semanticindex.h"
#include "wavesimulationmodulemanifest.h"

#include <QStringList>
#include <memory>

class SemanticIndexSnapshot;
class SemanticDependencyGraph;

enum class WaveSimulationManifestBuildStatus {
    Success,
    MissingSemanticSnapshot,
    WorkspaceNotOpen,
    InvalidModuleKey,
    ModuleNotFound,
    UnsupportedTarget,
    InstanceContextNotFound,
    InvalidObservationScope,
    ProjectPathOutsideWorkspace
};

struct WaveSimulationObservationScopeRequest {
    QString mode;
    QString label;
    QString fileName;
    int startLine = 0;
    int endLine = 0;
};

struct WaveSimulationObservationRequest {
    QString name;
    QString accessPath;
    QString fileName;
    int line = 0;
    int column = 0;
};

struct WaveSimulationManifestBuildRequest {
    std::shared_ptr<const SemanticIndexSnapshot> semanticSnapshot;
    ProjectSnapshot project;
    SymbolStableKey moduleStableKey;
    QString instancePath;
    WaveSimulationObservationScopeRequest observationScope;
    QList<WaveSimulationObservationRequest> explicitObservations;
    std::shared_ptr<const SemanticDependencyGraph> dependencyGraph;
};

struct WaveSimulationManifestBuildResult {
    WaveSimulationManifestBuildStatus status =
        WaveSimulationManifestBuildStatus::MissingSemanticSnapshot;
    WaveSimulationModuleManifest manifest;
    QString message;
    QStringList warnings;

    bool succeeded() const
    {
        return status == WaveSimulationManifestBuildStatus::Success;
    }
};

class WaveSimulationManifestService
{
public:
    WaveSimulationManifestBuildResult build(
        const WaveSimulationManifestBuildRequest& request) const;

    static QString statusCode(WaveSimulationManifestBuildStatus status);
};

#endif // WAVESIMULATIONMANIFESTSERVICE_H
