#ifndef WAVESIMULATIONPREPARATIONSERVICE_H
#define WAVESIMULATIONPREPARATIONSERVICE_H

#include "projectmodel.h"
#include "wavesimulationconfiguration.h"
#include "wavesimulationmodulemanifest.h"
#include "zeroslackexport.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <cstdint>
#include <functional>

struct WaveSimulationSourceOverride {
    QString fileName;
    QString content;
    std::uint64_t revision = 0;
};

struct WaveSimulationTargetContext {
    QString fileName;
    QString moduleName;
    QString instancePath;
};

struct WaveSimulationPreparationRequest {
    ProjectSnapshot project;
    WaveSimulationTargetContext target;
    QList<WaveSimulationSourceOverride> sourceOverrides;
    WaveSimulationCachePaths cachePaths;
};

enum class WaveSimulationPreparationStatus {
    Success,
    Cancelled,
    InvalidRequest,
    SourceReadFailed,
    SemanticAnalysisFailed,
    TargetNotFound,
    TargetAmbiguous,
    UnsupportedTarget,
    ArtifactWriteFailed
};

struct WaveSimulationPreparationResult {
    WaveSimulationPreparationStatus status =
        WaveSimulationPreparationStatus::InvalidRequest;
    QString message;
    QStringList warnings;
    WaveSimulationModuleManifest manifest;
    QString runId;
    QString mirrorWorkspaceRoot;
    QString manifestPath;
    QString stimulusProjectPath;
    QString stimulusPath;
    QString scenarioDirectory;
    QString defaultScenarioPath;
    QString resultRoot;
    QString resultProjectPath;

    bool succeeded() const
    {
        return status == WaveSimulationPreparationStatus::Success;
    }
};

class ZEROSLACK_API WaveSimulationPreparationService
{
public:
    static WaveSimulationPreparationResult prepare(
        const WaveSimulationPreparationRequest& request,
        const std::function<bool()>& isCancelled = {});

    static QString statusCode(WaveSimulationPreparationStatus status);
};

#endif // WAVESIMULATIONPREPARATIONSERVICE_H
