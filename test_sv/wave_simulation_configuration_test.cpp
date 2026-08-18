#include "wavesimulationconfiguration.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFile>
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

    const QString toolDirectory =
        temporary.filePath(QStringLiteral("tools"));
    QDir().mkpath(toolDirectory);
#ifdef Q_OS_WIN
    const QString executableSuffix = QStringLiteral(".exe");
#else
    const QString executableSuffix;
#endif
    for (const QString& name : {
             QStringLiteral("wave-bridge"),
             QStringLiteral("wave-sim-runner"),
             QStringLiteral("wave-workbench")}) {
        QFile tool(QDir(toolDirectory).absoluteFilePath(
            name + executableSuffix));
        check(tool.open(QIODevice::WriteOnly),
              "fake WaveWorkbench tool can be created");
        tool.close();
    }
#ifdef Q_OS_WIN
    const QString widgetLibraryName = QStringLiteral("wavewidgets.dll");
#elif defined(Q_OS_MACOS)
    const QString widgetLibraryName = QStringLiteral("libwavewidgets.dylib");
#else
    const QString widgetLibraryName = QStringLiteral("libwavewidgets.so");
#endif
    QFile widgetLibrary(
        QDir(toolDirectory).absoluteFilePath(widgetLibraryName));
    check(widgetLibrary.open(QIODevice::WriteOnly),
          "fake WaveWorkbench widget library can be created");
    widgetLibrary.close();
    const WaveSimulationToolPaths toolPaths =
        WaveSimulationConfiguration(
            settingsPath,
            cacheRoot,
            toolDirectory)
            .toolPaths();
    check(toolPaths.isValid()
              && toolPaths.missingTools().isEmpty(),
          "an explicit WaveWorkbench tool directory resolves all executables");
    check(QFile::remove(toolPaths.application)
              && toolPaths.isValid()
              && toolPaths.missingTools().isEmpty(),
          "the standalone application is optional for embedded Wave tabs");

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
