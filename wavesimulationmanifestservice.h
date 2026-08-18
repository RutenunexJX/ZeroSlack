#ifndef WAVESIMULATIONMANIFESTSERVICE_H
#define WAVESIMULATIONMANIFESTSERVICE_H

#include "projectmodel.h"
#include "semanticindex.h"
#include "wavesimulationmodulemanifest.h"

#include <QStringList>
#include <memory>

class SemanticIndexSnapshot;

enum class WaveSimulationManifestBuildStatus {
    Success,
    MissingSemanticSnapshot,
    WorkspaceNotOpen,
    InvalidModuleKey,
    ModuleNotFound,
    UnsupportedTarget,
    InstanceContextNotFound,
    ProjectPathOutsideWorkspace
};

struct WaveSimulationManifestBuildRequest {
    std::shared_ptr<const SemanticIndexSnapshot> semanticSnapshot;
    ProjectSnapshot project;
    SymbolStableKey moduleStableKey;
    QString instancePath;
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
