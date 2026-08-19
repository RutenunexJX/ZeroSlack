#include "wavesimulationconfiguration.h"

#include <QDir>
#include <QCoreApplication>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QSettings>
#include <QSet>
#include <QStandardPaths>

#include <memory>

namespace {
QString cleanAbsolutePath(const QString& path)
{
    if (path.trimmed().isEmpty())
        return QString();
    return QDir::cleanPath(
        QDir::fromNativeSeparators(
            QFileInfo(path).absoluteFilePath()));
}

std::unique_ptr<QSettings> makeSettings(const QString& path)
{
    if (!path.isEmpty()) {
        return std::make_unique<QSettings>(
            path,
            QSettings::IniFormat);
    }
    return std::make_unique<QSettings>(
        QSettings::defaultFormat(),
        QSettings::UserScope,
        QStringLiteral("ZeroSlack"),
        QStringLiteral("ZeroSlack"));
}

QString defaultCacheRoot()
{
    QString base = QStandardPaths::writableLocation(
        QStandardPaths::CacheLocation);
    if (base.isEmpty()) {
        base = QStandardPaths::writableLocation(
            QStandardPaths::AppLocalDataLocation);
        if (!base.isEmpty()) {
            base = QDir(base).absoluteFilePath(
                QStringLiteral("cache"));
        }
    }
    if (base.isEmpty())
        return QString();
    return QDir::cleanPath(
        QDir(base).absoluteFilePath(
            QStringLiteral("wave-simulation/v%1")
                .arg(WaveSimulationConfiguration::
                         kCacheLayoutVersion)));
}

QString executableName(const QString& baseName)
{
#ifdef Q_OS_WIN
    return baseName + QStringLiteral(".exe");
#else
    return baseName;
#endif
}

QString libraryName(const QString& baseName)
{
#ifdef Q_OS_WIN
    return baseName + QStringLiteral(".dll");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("lib") + baseName + QStringLiteral(".dylib");
#else
    return QStringLiteral("lib") + baseName + QStringLiteral(".so");
#endif
}

WaveSimulationToolPaths toolsInDirectory(const QString& directory)
{
    WaveSimulationToolPaths paths;
    if (directory.trimmed().isEmpty())
        return paths;
    const QDir root(cleanAbsolutePath(directory));
    paths.bridge = root.absoluteFilePath(
        executableName(QStringLiteral("wave-bridge")));
    paths.runner = root.absoluteFilePath(
        executableName(QStringLiteral("wave-sim-runner")));
    paths.application = root.absoluteFilePath(
        executableName(QStringLiteral("wave-workbench")));
    paths.widgetLibrary = root.absoluteFilePath(
        libraryName(QStringLiteral("wavewidgets")));
    paths.fstReader = root.absoluteFilePath(
        executableName(QStringLiteral("wave-wellen-reader")));
    return paths;
}

QString existingExecutable(const QString& path)
{
    const QFileInfo info(path);
    return info.exists() && info.isFile()
        ? info.absoluteFilePath()
        : QString();
}

QString existingFile(const QString& path)
{
    const QFileInfo info(path);
    return info.exists() && info.isFile()
        ? info.absoluteFilePath()
        : QString();
}

QString libraryOnPath(const QString& name)
{
    const QString pathValue = QProcessEnvironment::systemEnvironment()
        .value(QStringLiteral("PATH"));
    for (const QString& directory :
         pathValue.split(QDir::listSeparator(), Qt::SkipEmptyParts)) {
        const QString candidate = QDir(directory).absoluteFilePath(name);
        if (!existingFile(candidate).isEmpty())
            return candidate;
    }
    return QString();
}
}

bool WaveSimulationCachePaths::isValid() const
{
    return !root.isEmpty()
        && !sourceMirrors.isEmpty()
        && !buildCache.isEmpty()
        && !results.isEmpty();
}

bool WaveSimulationToolPaths::isValid() const
{
    return !existingExecutable(bridge).isEmpty()
        && !existingExecutable(runner).isEmpty()
        && !existingFile(widgetLibrary).isEmpty();
}

QStringList WaveSimulationToolPaths::missingTools() const
{
    QStringList missing;
    if (existingExecutable(bridge).isEmpty())
        missing.append(QStringLiteral("wave-bridge"));
    if (existingExecutable(runner).isEmpty())
        missing.append(QStringLiteral("wave-sim-runner"));
    if (existingFile(widgetLibrary).isEmpty())
        missing.append(QStringLiteral("wavewidgets"));
    return missing;
}

WaveSimulationConfiguration::WaveSimulationConfiguration(
    const QString& newSettingsFilePath,
    const QString& newCacheRootOverride,
    const QString& newToolDirectoryOverride)
    : settingsFilePath(
          cleanAbsolutePath(newSettingsFilePath))
    , cacheRootOverride(
          cleanAbsolutePath(newCacheRootOverride))
    , toolDirectoryOverride(
          cleanAbsolutePath(newToolDirectoryOverride))
{
}

WaveSimulationToolPaths
WaveSimulationConfiguration::toolPaths() const
{
    QStringList candidateDirectories;
    if (!toolDirectoryOverride.isEmpty())
        candidateDirectories.append(toolDirectoryOverride);

    const std::unique_ptr<QSettings> settings =
        makeSettings(settingsFilePath);
    const QString configuredDirectory = cleanAbsolutePath(
        settings->value(
            QString::fromLatin1(kToolDirectorySettingKey))
            .toString());
    if (!configuredDirectory.isEmpty())
        candidateDirectories.append(configuredDirectory);

    const QString environmentDirectory = cleanAbsolutePath(
        QProcessEnvironment::systemEnvironment()
            .value(QStringLiteral("WAVEWORKBENCH_HOME")));
    if (!environmentDirectory.isEmpty())
        candidateDirectories.append(environmentDirectory);

    const QString applicationDirectory =
        QCoreApplication::applicationDirPath();
    if (!applicationDirectory.isEmpty()) {
        candidateDirectories.append(applicationDirectory);
        candidateDirectories.append(
            QDir(applicationDirectory)
                .absoluteFilePath(QStringLiteral("WaveWorkbench")));
        candidateDirectories.append(
            QDir(applicationDirectory)
                .absoluteFilePath(QStringLiteral("../WaveWorkbench")));
    }

    QSet<QString> visited;
    for (const QString& directory : candidateDirectories) {
        const QString normalized = cleanAbsolutePath(directory);
        if (normalized.isEmpty() || visited.contains(normalized))
            continue;
        visited.insert(normalized);
        const WaveSimulationToolPaths candidate =
            toolsInDirectory(normalized);
        if (candidate.isValid())
            return candidate;
    }

    WaveSimulationToolPaths pathTools;
    pathTools.bridge = QStandardPaths::findExecutable(
        executableName(QStringLiteral("wave-bridge")));
    pathTools.runner = QStandardPaths::findExecutable(
        executableName(QStringLiteral("wave-sim-runner")));
    pathTools.application = QStandardPaths::findExecutable(
        executableName(QStringLiteral("wave-workbench")));
    pathTools.widgetLibrary = libraryOnPath(
        libraryName(QStringLiteral("wavewidgets")));
    pathTools.fstReader = QStandardPaths::findExecutable(
        executableName(QStringLiteral("wave-wellen-reader")));
    return pathTools;
}

WaveSimulationCachePaths
WaveSimulationConfiguration::cachePaths() const
{
    WaveSimulationCachePaths paths;
    paths.root = cacheRootOverride.isEmpty()
        ? defaultCacheRoot()
        : cacheRootOverride;
    if (paths.root.isEmpty())
        return paths;

    const QDir root(paths.root);
    paths.sourceMirrors = QDir::cleanPath(
        root.absoluteFilePath(
            QStringLiteral("source-mirrors")));
    paths.buildCache = QDir::cleanPath(
        root.absoluteFilePath(
            QStringLiteral("build-cache")));
    paths.results = QDir::cleanPath(
        root.absoluteFilePath(
            QStringLiteral("results")));
    return paths;
}
