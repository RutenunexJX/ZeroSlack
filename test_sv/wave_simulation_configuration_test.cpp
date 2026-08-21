#include "wavesimulationconfiguration.h"
#include "settingscenterkeys.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QSettings>
#include <QTemporaryDir>

#include <iostream>
#include <future>

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

bool writeFixtureFile(const QString& path,
                      const QByteArray& content = {})
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly)
        && file.write(content) == content.size();
}

QString sha256(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd())
        hash.addData(file.read(1024 * 1024));
    return QString::fromLatin1(hash.result().toHex());
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
             QStringLiteral("wave-workbench"),
             QStringLiteral("wave-wellen-reader")}) {
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
    const QString portableVerilatorDirectory =
        QDir(toolDirectory).absoluteFilePath(
            QStringLiteral("toolchain/verilator/bin"));
    const QString portableVerilatorRoot =
        QFileInfo(portableVerilatorDirectory).absolutePath();
    const QString portableCompilerDirectory =
        QDir(toolDirectory).absoluteFilePath(
            QStringLiteral("toolchain/mingw/bin"));
    QDir().mkpath(portableVerilatorDirectory);
    QDir().mkpath(QDir(portableVerilatorRoot).absoluteFilePath(
        QStringLiteral("include")));
    QDir().mkpath(portableCompilerDirectory);
    for (const auto& toolSpec : {
             qMakePair(portableVerilatorDirectory,
                       QStringLiteral("verilator")),
             qMakePair(portableVerilatorDirectory,
                       QStringLiteral("verilator_bin")),
             qMakePair(portableCompilerDirectory,
                       QStringLiteral("g++")),
             qMakePair(portableCompilerDirectory,
                       QStringLiteral("mingw32-make"))}) {
        QFile tool(QDir(toolSpec.first).absoluteFilePath(
            toolSpec.second + executableSuffix));
        check(tool.open(QIODevice::WriteOnly),
              "fake portable compiler can be created");
        tool.close();
    }
    QFile verilatedMakefile(
        QDir(portableVerilatorRoot).absoluteFilePath(
            QStringLiteral("include/verilated.mk")));
    check(verilatedMakefile.open(QIODevice::WriteOnly),
          "fake Verilator runtime support can be created");
    verilatedMakefile.close();
    const QByteArray oldVerilator = qgetenv("VERILATOR");
    const QByteArray oldCxx = qgetenv("CXX");
    qputenv("VERILATOR", QByteArrayLiteral("ambient-verilator"));
    qputenv("CXX", QByteArrayLiteral("ambient-cxx"));
    const WaveSimulationToolPaths toolPaths =
        WaveSimulationConfiguration(
            settingsPath,
            cacheRoot,
            toolDirectory)
            .toolPaths();
    check(toolPaths.isValid()
              && toolPaths.missingTools().isEmpty()
              && QFileInfo(toolPaths.fstReader).isFile()
              && QFileInfo(toolPaths.verilator).isFile()
              && QFileInfo(toolPaths.cxxCompiler).isFile(),
          "an explicit WaveWorkbench directory resolves core tools and its portable toolchain");
    const QProcessEnvironment portableEnvironment =
        toolPaths.processEnvironment();
    const QStringList portablePathEntries =
        portableEnvironment.value(QStringLiteral("PATH"))
            .split(QDir::listSeparator(), Qt::SkipEmptyParts);
    check(QFileInfo(toolPaths.verilator).fileName()
                  == QStringLiteral("verilator") + executableSuffix
              && toolPaths.verilatorRoot
                  == QDir::cleanPath(portableVerilatorRoot),
          "the portable Verilator launcher resolves its runtime root");
    check(QFileInfo(toolPaths.makeProgram).fileName()
                  == QStringLiteral("mingw32-make") + executableSuffix
              && portableEnvironment.value(QStringLiteral("MAKE"))
                  == QFileInfo(toolPaths.makeProgram).fileName(),
          "portable GNU Make is selected explicitly");
    check(portablePathEntries.size() >= 2
              && QDir::cleanPath(portablePathEntries.at(0))
                  == QDir::cleanPath(portableCompilerDirectory)
              && QDir::cleanPath(portablePathEntries.at(1))
                  == QDir::cleanPath(portableVerilatorDirectory)
              && portableEnvironment.value(
                     QStringLiteral("VERILATOR_ROOT"))
                  == QDir::cleanPath(portableVerilatorRoot),
          "portable compiler and Verilator directories lead the child PATH");
    if (oldVerilator.isNull())
        qunsetenv("VERILATOR");
    else
        qputenv("VERILATOR", oldVerilator);
    if (oldCxx.isNull())
        qunsetenv("CXX");
    else
        qputenv("CXX", oldCxx);
    check(QFile::remove(toolPaths.application)
              && toolPaths.isValid()
              && toolPaths.missingTools().isEmpty(),
          "the standalone application is optional for embedded Wave tabs");

    const QString explicitDirectory = temporary.filePath(
        QStringLiteral("explicit-toolchain"));
    QDir().mkpath(explicitDirectory);
    const QString explicitVerilator = QDir(explicitDirectory)
        .absoluteFilePath(QStringLiteral("custom-verilator")
                          + executableSuffix);
    const QString explicitCompiler = QDir(explicitDirectory)
        .absoluteFilePath(QStringLiteral("custom-cxx")
                          + executableSuffix);
    for (const QString& path : {explicitVerilator, explicitCompiler}) {
        QFile tool(path);
        check(tool.open(QIODevice::WriteOnly),
              "explicit toolchain fixture can be created");
        tool.close();
    }
    {
        QSettings settings(settingsPath, QSettings::IniFormat);
        settings.setValue(
            QString::fromLatin1(
                SettingsCenterKeys::SimulationVerilatorPath),
            explicitVerilator);
        settings.setValue(
            QString::fromLatin1(
                SettingsCenterKeys::SimulationCxxCompilerPath),
            explicitCompiler);
        settings.sync();
    }
    const WaveSimulationToolPaths explicitTools =
        WaveSimulationConfiguration(
            settingsPath,
            cacheRoot,
            toolDirectory)
            .toolPaths();
    check(explicitTools.verilator
                  == QFileInfo(explicitVerilator).absoluteFilePath()
              && explicitTools.cxxCompiler
                     == QFileInfo(explicitCompiler).absoluteFilePath(),
          "explicit Simulation settings override portable toolchain discovery");

    const QString bundleWaveDirectory = temporary.filePath(
        QStringLiteral("bundle-wave"));
    QDir().mkpath(bundleWaveDirectory);
    for (const QString& name : {
             QStringLiteral("wave-bridge"),
             QStringLiteral("wave-sim-runner"),
             QStringLiteral("wave-workbench"),
             QStringLiteral("wave-wellen-reader")}) {
        check(writeFixtureFile(QDir(bundleWaveDirectory).absoluteFilePath(
                  name + executableSuffix)),
              "bundle fixture WaveWorkbench tool can be created");
    }
    check(writeFixtureFile(QDir(bundleWaveDirectory).absoluteFilePath(
              widgetLibraryName)),
          "bundle fixture widget library can be created");

    const QString bundleSource = temporary.filePath(
        QStringLiteral("bundle-source"));
    for (const QString& relative : {
             QStringLiteral("toolchain-manifest.json"),
             QStringLiteral("verilator/bin/verilator") + executableSuffix,
             QStringLiteral("verilator/bin/verilator_bin") + executableSuffix,
             QStringLiteral("verilator/include/verilated.mk"),
             QStringLiteral("mingw/bin/g++") + executableSuffix,
             QStringLiteral("mingw/bin/mingw32-make") + executableSuffix}) {
        const QByteArray content = relative
                == QStringLiteral("toolchain-manifest.json")
            ? QByteArrayLiteral(
                  "{\"schema\":\"zeroslack.wave-toolchain/v1\"}")
            : QByteArrayLiteral("fixture");
        check(writeFixtureFile(
                  QDir(bundleSource).absoluteFilePath(relative),
                  content),
              "bundle toolchain member can be created");
    }
    const QString bundleDirectory = QDir(bundleWaveDirectory)
        .absoluteFilePath(QStringLiteral("Toolchain"));
    QDir().mkpath(bundleDirectory);
    const QString archivePath = QDir(bundleDirectory).absoluteFilePath(
        QStringLiteral("wave-toolchain-v1.zip"));
    QString tar = QStandardPaths::findExecutable(
        QStringLiteral("tar") + executableSuffix);
#ifdef Q_OS_WIN
    if (tar.isEmpty()) {
        const QString candidate = QDir(qEnvironmentVariable("SystemRoot"))
            .absoluteFilePath(QStringLiteral("System32/tar.exe"));
        if (QFileInfo(candidate).isFile())
            tar = candidate;
    }
#endif
    check(!tar.isEmpty(),
          "a platform archive tool is available for the bundle fixture");
    if (!tar.isEmpty()) {
        check(QProcess::execute(
                  tar,
                  {QStringLiteral("-a"), QStringLiteral("-cf"), archivePath,
                   QStringLiteral("-C"), bundleSource, QStringLiteral(".")}) == 0,
              "bundle fixture archive can be created");
    }
    const QString archiveHash = sha256(archivePath);
    QJsonObject bundleManifest;
    bundleManifest.insert(
        QStringLiteral("schema"),
        QStringLiteral("zeroslack.wave-toolchain-bundle/v1"));
    bundleManifest.insert(QStringLiteral("schemaVersion"), 1);
    bundleManifest.insert(QStringLiteral("bundleId"),
                          QStringLiteral("sha256-") + archiveHash);
    bundleManifest.insert(QStringLiteral("archive"),
                          QFileInfo(archivePath).fileName());
    bundleManifest.insert(QStringLiteral("archiveSha256"), archiveHash);
    bundleManifest.insert(QStringLiteral("archiveBytes"),
                          static_cast<double>(QFileInfo(archivePath).size()));
    const QString bundleManifestPath = QDir(bundleDirectory).absoluteFilePath(
        QString::fromLatin1(
            WaveToolchainBundleService::kManifestFileName));
    check(writeFixtureFile(
              bundleManifestPath,
              QJsonDocument(bundleManifest).toJson(QJsonDocument::Indented)),
          "bundle manifest can be created");

    const QString bundleSettingsPath = temporary.filePath(
        QStringLiteral("bundle-settings.ini"));
    {
        QSettings settings(bundleSettingsPath, QSettings::IniFormat);
        settings.setValue(
            QString::fromLatin1(
                SettingsCenterKeys::SimulationVerilatorPath),
            temporary.filePath(QStringLiteral("missing-verilator")
                               + executableSuffix));
        settings.setValue(
            QString::fromLatin1(
                SettingsCenterKeys::SimulationCxxCompilerPath),
            temporary.filePath(QStringLiteral("missing-cxx")
                               + executableSuffix));
    }
    WaveSimulationToolPaths bundleTools =
        WaveSimulationConfiguration(
            bundleSettingsPath,
            cacheRoot,
            bundleWaveDirectory)
            .toolPaths();
    check(bundleTools.isValid()
              && bundleTools.toolchainBundle.isValid()
              && bundleTools.requiresBundledToolchain(),
          "a compact bundle is discovered when configured and expanded tools are unavailable");
    const QString bundleCache = temporary.filePath(
        QStringLiteral("bundle-cache"));
    const WaveToolchainBundleResult preparedBundle =
        WaveToolchainBundleService::prepare(
            bundleTools.toolchainBundle, bundleCache);
    check(preparedBundle.succeeded() && !preparedBundle.reused,
          "the compact bundle is verified and extracted into the local cache");
    WaveSimulationConfiguration::applyToolchainRoot(
        &bundleTools, preparedBundle.toolchainRoot);
    check(!bundleTools.requiresBundledToolchain()
              && QFileInfo(bundleTools.verilator).isFile()
              && QFileInfo(bundleTools.cxxCompiler).isFile()
              && QFileInfo(bundleTools.makeProgram).isFile(),
          "cached bundle tools populate the simulation process environment");
    const WaveToolchainBundleResult reusedBundle =
        WaveToolchainBundleService::prepare(
            bundleTools.toolchainBundle, bundleCache);
    check(reusedBundle.succeeded() && reusedBundle.reused
              && reusedBundle.toolchainRoot == preparedBundle.toolchainRoot,
          "a prepared bundle is reused without extraction");

    const QString concurrentCache = temporary.filePath(
        QStringLiteral("concurrent-bundle-cache"));
    auto firstPreparation = std::async(
        std::launch::async,
        [descriptor = bundleTools.toolchainBundle, concurrentCache]() {
            return WaveToolchainBundleService::prepare(
                descriptor, concurrentCache);
        });
    auto secondPreparation = std::async(
        std::launch::async,
        [descriptor = bundleTools.toolchainBundle, concurrentCache]() {
            return WaveToolchainBundleService::prepare(
                descriptor, concurrentCache);
        });
    const WaveToolchainBundleResult firstConcurrent =
        firstPreparation.get();
    const WaveToolchainBundleResult secondConcurrent =
        secondPreparation.get();
    check(firstConcurrent.succeeded() && secondConcurrent.succeeded()
              && firstConcurrent.toolchainRoot
                     == secondConcurrent.toolchainRoot
              && firstConcurrent.reused != secondConcurrent.reused,
          "concurrent first use publishes one cache and reuses it in the other process path");

    WaveToolchainBundleDescriptor wrongHash = bundleTools.toolchainBundle;
    wrongHash.archiveSha256[0] = wrongHash.archiveSha256.at(0)
            == QLatin1Char('0') ? QLatin1Char('1') : QLatin1Char('0');
    check(WaveToolchainBundleService::prepare(
              wrongHash,
              temporary.filePath(QStringLiteral("wrong-hash-cache"))).status
              == WaveToolchainBundleStatus::ArchiveHashMismatch,
          "a bundle with a mismatched SHA-256 is rejected");

    QJsonObject unsafeManifest = bundleManifest;
    unsafeManifest.insert(QStringLiteral("archive"),
                          QStringLiteral("../outside.zip"));
    const QString unsafeManifestPath = temporary.filePath(
        QStringLiteral("unsafe-bundle.json"));
    check(writeFixtureFile(
              unsafeManifestPath,
              QJsonDocument(unsafeManifest).toJson(QJsonDocument::Compact))
              && !WaveToolchainBundleService::fromManifest(
                      unsafeManifestPath).isValid(),
          "a bundle manifest cannot escape its own directory");

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
