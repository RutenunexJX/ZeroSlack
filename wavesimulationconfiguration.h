#ifndef WAVESIMULATIONCONFIGURATION_H
#define WAVESIMULATIONCONFIGURATION_H

#include <QString>

struct WaveSimulationCachePaths {
    QString root;
    QString sourceMirrors;
    QString buildCache;
    QString results;

    bool isValid() const;
};

class WaveSimulationConfiguration
{
public:
    static constexpr int kCacheLayoutVersion = 1;
    static constexpr const char* kExperimentalSettingKey =
        "experimental/ExperimentalWaveSimulation";

    explicit WaveSimulationConfiguration(
        const QString& settingsFilePath = QString(),
        const QString& cacheRootOverride = QString());

    bool experimentalWaveSimulationEnabled() const;
    WaveSimulationCachePaths cachePaths() const;

private:
    QString settingsFilePath;
    QString cacheRootOverride;
};

#endif // WAVESIMULATIONCONFIGURATION_H
