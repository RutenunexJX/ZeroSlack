#include "workspaceconfigurationservice.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QSettings>

namespace {
constexpr const char* kWorkspaceConfigGroup = "workspaceConfiguration";
constexpr const char* kWorkspaceConfigVersion = "v1";
constexpr const char* kWorkspaceConfigWorkspaces = "workspaces";
constexpr const char* kWorkspaceConfigRoot = "workspaceRoot";
constexpr const char* kWorkspaceConfigIncludeDirs = "includeDirs";
constexpr const char* kWorkspaceConfigIgnoredDirs = "ignoredDirs";
constexpr const char* kWorkspaceConfigFileExtensions = "fileExtensions";
constexpr const char* kWorkspaceConfigTopModule = "topModule";
constexpr const char* kWorkspaceConfigDefines = "defines";
constexpr const char* kWorkspaceConfigDefineKey = "key";
constexpr const char* kWorkspaceConfigDefineValue = "value";

QString normalizePath(const QString& path)
{
    if (path.isEmpty())
        return QString();
    return QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()));
}

QString workspaceScopeKey(const QString& workspaceRoot)
{
    const QByteArray bytes = normalizePath(workspaceRoot).toUtf8().toBase64(
        QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    return QString::fromLatin1(bytes);
}

QStringList uniquePreservingOrder(const QStringList& values)
{
    QStringList result;
    QSet<QString> seen;
    result.reserve(values.size());
    for (const QString& value : values) {
        const QString key = value.toCaseFolded();
        if (value.isEmpty() || seen.contains(key))
            continue;
        seen.insert(key);
        result.append(value);
    }
    return result;
}

QStringList normalizePaths(const QStringList& paths)
{
    QStringList normalized;
    normalized.reserve(paths.size());
    for (const QString& path : paths) {
        const QString clean = normalizePath(path);
        if (!clean.isEmpty())
            normalized.append(clean);
    }
    return uniquePreservingOrder(normalized);
}

QStringList normalizeFileExtensions(const QStringList& extensions)
{
    QStringList normalized;
    normalized.reserve(extensions.size());
    for (QString extension : extensions) {
        extension = extension.trimmed().toLower();
        if (extension.isEmpty())
            continue;
        if (!extension.startsWith(QLatin1Char('.')))
            extension.prepend(QLatin1Char('.'));
        normalized.append(extension);
    }
    normalized = uniquePreservingOrder(normalized);
    return normalized.isEmpty()
        ? WorkspaceConfigurationService::defaultFileExtensions()
        : normalized;
}

QHash<QString, QString> normalizeDefines(const QHash<QString, QString>& defines)
{
    QHash<QString, QString> normalized;
    for (auto it = defines.cbegin(); it != defines.cend(); ++it) {
        const QString key = it.key().trimmed();
        if (key.isEmpty())
            continue;
        normalized.insert(key, it.value().trimmed());
    }
    return normalized;
}

void beginWorkspaceGroup(QSettings* settings, const QString& workspaceRoot)
{
    settings->beginGroup(QString::fromLatin1(kWorkspaceConfigGroup));
    settings->beginGroup(QString::fromLatin1(kWorkspaceConfigVersion));
    settings->beginGroup(QString::fromLatin1(kWorkspaceConfigWorkspaces));
    settings->beginGroup(workspaceScopeKey(workspaceRoot));
}

void endWorkspaceGroup(QSettings* settings)
{
    settings->endGroup();
    settings->endGroup();
    settings->endGroup();
    settings->endGroup();
}
}

std::unique_ptr<WorkspaceConfigurationService>
    WorkspaceConfigurationService::instance = nullptr;

WorkspaceConfigurationService::WorkspaceConfigurationService(
    const QString& path)
    : settingsFilePath(path)
{
}

WorkspaceConfigurationService* WorkspaceConfigurationService::getInstance()
{
    if (!instance)
        instance = std::make_unique<WorkspaceConfigurationService>();
    return instance.get();
}

QStringList WorkspaceConfigurationService::defaultFileExtensions()
{
    return {QStringLiteral(".sv"),
            QStringLiteral(".svh"),
            QStringLiteral(".v"),
            QStringLiteral(".vh")};
}

WorkspaceConfiguration WorkspaceConfigurationService::defaultConfiguration(
    const QString& workspaceRoot) const
{
    WorkspaceConfiguration configuration;
    configuration.workspaceRoot = normalizePath(workspaceRoot);
    if (!configuration.workspaceRoot.isEmpty())
        configuration.includeDirs = {configuration.workspaceRoot};
    configuration.fileExtensions = defaultFileExtensions();
    return configuration;
}

WorkspaceConfiguration WorkspaceConfigurationService::load(
    const QString& workspaceRoot) const
{
    WorkspaceConfiguration configuration = defaultConfiguration(workspaceRoot);
    if (!configuration.isValid())
        return configuration;

    std::unique_ptr<QSettings> settings = makeSettings();
    beginWorkspaceGroup(settings.get(), configuration.workspaceRoot);
    const QString storedRoot =
        normalizePath(settings->value(
                           QString::fromLatin1(kWorkspaceConfigRoot))
                          .toString());
    if (storedRoot.isEmpty()) {
        endWorkspaceGroup(settings.get());
        return configuration;
    }

    configuration.includeDirs =
        normalizePaths(settings->value(
                            QString::fromLatin1(kWorkspaceConfigIncludeDirs),
                            configuration.includeDirs)
                           .toStringList());
    configuration.ignoredDirs =
        normalizePaths(settings->value(
                            QString::fromLatin1(kWorkspaceConfigIgnoredDirs))
                           .toStringList());
    configuration.fileExtensions =
        normalizeFileExtensions(settings->value(
                                     QString::fromLatin1(
                                         kWorkspaceConfigFileExtensions),
                                     configuration.fileExtensions)
                                    .toStringList());
    configuration.topModule =
        settings->value(QString::fromLatin1(kWorkspaceConfigTopModule))
            .toString()
            .trimmed();

    QHash<QString, QString> defines;
    const int defineCount =
        settings->beginReadArray(QString::fromLatin1(kWorkspaceConfigDefines));
    for (int i = 0; i < defineCount; ++i) {
        settings->setArrayIndex(i);
        defines.insert(
            settings->value(QString::fromLatin1(kWorkspaceConfigDefineKey))
                .toString(),
            settings->value(QString::fromLatin1(kWorkspaceConfigDefineValue))
                .toString());
    }
    settings->endArray();
    configuration.defines = normalizeDefines(defines);
    endWorkspaceGroup(settings.get());
    return normalized(configuration);
}

bool WorkspaceConfigurationService::save(
    const WorkspaceConfiguration& configuration) const
{
    const WorkspaceConfiguration clean = normalized(configuration);
    if (!clean.isValid())
        return false;

    std::unique_ptr<QSettings> settings = makeSettings();
    beginWorkspaceGroup(settings.get(), clean.workspaceRoot);
    settings->remove(QString());
    settings->setValue(QString::fromLatin1(kWorkspaceConfigRoot),
                       clean.workspaceRoot);
    settings->setValue(QString::fromLatin1(kWorkspaceConfigIncludeDirs),
                       clean.includeDirs);
    settings->setValue(QString::fromLatin1(kWorkspaceConfigIgnoredDirs),
                       clean.ignoredDirs);
    settings->setValue(QString::fromLatin1(kWorkspaceConfigFileExtensions),
                       clean.fileExtensions);
    settings->setValue(QString::fromLatin1(kWorkspaceConfigTopModule),
                       clean.topModule);

    QStringList defineKeys = clean.defines.keys();
    defineKeys.sort(Qt::CaseInsensitive);
    settings->beginWriteArray(QString::fromLatin1(kWorkspaceConfigDefines));
    for (int i = 0; i < defineKeys.size(); ++i) {
        const QString& key = defineKeys.at(i);
        settings->setArrayIndex(i);
        settings->setValue(QString::fromLatin1(kWorkspaceConfigDefineKey),
                           key);
        settings->setValue(QString::fromLatin1(kWorkspaceConfigDefineValue),
                           clean.defines.value(key));
    }
    settings->endArray();
    endWorkspaceGroup(settings.get());
    settings->sync();
    return settings->status() == QSettings::NoError;
}

bool WorkspaceConfigurationService::clear(const QString& workspaceRoot) const
{
    const QString normalizedRoot = normalizePath(workspaceRoot);
    if (normalizedRoot.isEmpty())
        return false;
    std::unique_ptr<QSettings> settings = makeSettings();
    beginWorkspaceGroup(settings.get(), normalizedRoot);
    settings->remove(QString());
    endWorkspaceGroup(settings.get());
    settings->sync();
    return settings->status() == QSettings::NoError;
}

QString WorkspaceConfigurationService::storageDescription() const
{
    if (!settingsFilePath.isEmpty())
        return settingsFilePath;
    return QStringLiteral(
        "QSettings:ZeroSlack/ZeroSlack/workspaceConfiguration/v1");
}

WorkspaceConfiguration WorkspaceConfigurationService::normalized(
    const WorkspaceConfiguration& configuration) const
{
    WorkspaceConfiguration clean = configuration;
    clean.workspaceRoot = normalizePath(configuration.workspaceRoot);
    clean.includeDirs = normalizePaths(configuration.includeDirs);
    clean.ignoredDirs = normalizePaths(configuration.ignoredDirs);
    clean.fileExtensions =
        normalizeFileExtensions(configuration.fileExtensions);
    clean.defines = normalizeDefines(configuration.defines);
    clean.topModule = configuration.topModule.trimmed();
    if (clean.includeDirs.isEmpty() && !clean.workspaceRoot.isEmpty())
        clean.includeDirs = {clean.workspaceRoot};
    return clean;
}

std::unique_ptr<QSettings> WorkspaceConfigurationService::makeSettings() const
{
    if (!settingsFilePath.isEmpty())
        return std::make_unique<QSettings>(settingsFilePath,
                                           QSettings::IniFormat);
    return std::make_unique<QSettings>(QStringLiteral("ZeroSlack"),
                                       QStringLiteral("ZeroSlack"));
}
