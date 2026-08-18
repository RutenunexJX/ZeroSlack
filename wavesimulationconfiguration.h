#ifndef WAVESIMULATIONCONFIGURATION_H
#define WAVESIMULATIONCONFIGURATION_H

#include <QString>
#include <QStringList>

struct WaveSimulationCachePaths {
    QString root;
    QString sourceMirrors;
    QString buildCache;
    QString results;

    bool isValid() const;
};

struct WaveSimulationToolPaths {
    QString bridge;
    QString runner;
    QString application;

    bool isValid() const;
    QStringList missingTools() const;
};

class WaveSimulationConfiguration
{
public:
    static constexpr int kCacheLayoutVersion = 1;
    static constexpr const char* kToolDirectorySettingKey =
        "experimental/WaveWorkbenchDirectory";

    explicit WaveSimulationConfiguration(
        const QString& settingsFilePath = QString(),
        const QString& cacheRootOverride = QString(),
        const QString& toolDirectoryOverride = QString());

    WaveSimulationCachePaths cachePaths() const;
    WaveSimulationToolPaths toolPaths() const;

private:
    QString settingsFilePath;
    QString cacheRootOverride;
    QString toolDirectoryOverride;
};

#endif // WAVESIMULATIONCONFIGURATION_H
