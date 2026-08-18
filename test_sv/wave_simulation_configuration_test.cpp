#include "wavesimulationconfiguration.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <iostream>

namespace {
int checks = 0;
int failures = 0;

void check(bool condition, const char* message)
{
    ++checks;
    if (condition)
        return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

bool isChildPath(const QString& parent, const QString& child)
{
    const QString prefix = QDir::cleanPath(parent)
        + QLatin1Char('/');
    return QDir::cleanPath(child).startsWith(prefix);
}
}

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setOrganizationName(
        QStringLiteral("ZeroSlackTest"));
    QCoreApplication::setApplicationName(
        QStringLiteral("WaveSimulationConfigurationTest"));
    QStandardPaths::setTestModeEnabled(true);

    QTemporaryDir temporary;
    check(temporary.isValid(),
          "temporary fixture is valid");
    if (!temporary.isValid())
        return 1;

    const QString settingsPath =
        temporary.filePath(QStringLiteral("settings.ini"));
    const QString cacheRoot =
        temporary.filePath(
            QStringLiteral("wave-simulation-cache"));
    WaveSimulationConfiguration configuration(
        settingsPath,
        cacheRoot);

    check(!configuration.experimentalWaveSimulationEnabled(),
          "experimental wave simulation defaults to disabled");
    {
        QSettings settings(settingsPath, QSettings::IniFormat);
        settings.setValue(
            QString::fromLatin1(
                WaveSimulationConfiguration::
                    kExperimentalSettingKey),
            true);
        settings.sync();
    }
    check(configuration.experimentalWaveSimulationEnabled(),
          "the hidden setting explicitly enables the experiment");

    const WaveSimulationCachePaths paths =
        configuration.cachePaths();
    check(paths.isValid(),
          "all generated artifact roots are resolved");
    check(paths.root == QDir::cleanPath(cacheRoot)
              && isChildPath(paths.root, paths.sourceMirrors)
              && isChildPath(paths.root, paths.buildCache)
              && isChildPath(paths.root, paths.results),
          "source mirrors, builds, and results share one cache root");
    check(paths.sourceMirrors != paths.buildCache
              && paths.sourceMirrors != paths.results
              && paths.buildCache != paths.results,
          "generated artifact classes use separate directories");
    check(!QFileInfo::exists(paths.root),
          "reading configuration does not create cache directories");

    WaveSimulationConfiguration productionDefaults(settingsPath);
    const WaveSimulationCachePaths defaultPaths =
        productionDefaults.cachePaths();
    const QString standardCache = QDir::cleanPath(
        QStandardPaths::writableLocation(
            QStandardPaths::CacheLocation));
    check(defaultPaths.isValid()
              && !standardCache.isEmpty()
              && isChildPath(standardCache, defaultPaths.root),
          "production artifacts live under the application cache location");

    std::cout << "wave simulation configuration checks: "
              << checks << ", failures: " << failures << '\n';
    return failures == 0 ? 0 : 1;
}
