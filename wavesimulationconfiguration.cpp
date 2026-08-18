#include "wavesimulationconfiguration.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
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
}

bool WaveSimulationCachePaths::isValid() const
{
    return !root.isEmpty()
        && !sourceMirrors.isEmpty()
        && !buildCache.isEmpty()
        && !results.isEmpty();
}

WaveSimulationConfiguration::WaveSimulationConfiguration(
    const QString& newSettingsFilePath,
    const QString& newCacheRootOverride)
    : settingsFilePath(
          cleanAbsolutePath(newSettingsFilePath))
    , cacheRootOverride(
          cleanAbsolutePath(newCacheRootOverride))
{
}

bool WaveSimulationConfiguration::
    experimentalWaveSimulationEnabled() const
{
    const std::unique_ptr<QSettings> settings =
        makeSettings(settingsFilePath);
    return settings->value(
                       QString::fromLatin1(
                           kExperimentalSettingKey),
                       false)
        .toBool();
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
