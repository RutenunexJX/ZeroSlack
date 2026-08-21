#ifndef WAVESIMULATIONCONFIGURATION_H
#define WAVESIMULATIONCONFIGURATION_H

#include "wavetoolchainbundleservice.h"

#include <QString>
#include <QStringList>

class QProcessEnvironment;

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
    QString widgetLibrary;
    QString fstReader;
    QString verilator;
    QString cxxCompiler;
    QString verilatorRoot;
    QString makeProgram;
    WaveToolchainBundleDescriptor toolchainBundle;

    bool isValid() const;
    QStringList missingTools() const;
    bool requiresBundledToolchain() const;
    QProcessEnvironment processEnvironment() const;
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
    static void applyToolchainRoot(WaveSimulationToolPaths* paths,
                                   const QString& root);

private:
    QString settingsFilePath;
    QString cacheRootOverride;
    QString toolDirectoryOverride;
};

#endif // WAVESIMULATIONCONFIGURATION_H
