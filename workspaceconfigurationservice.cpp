#include "workspaceconfigurationservice.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSaveFile>
#include <QSet>

namespace {
constexpr const char* kProjectSchema =
    "ZeroSlack.ProjectConfiguration";
constexpr const char* kLegacySchema =
    "ZeroSlack.WorkspaceSessionState";
constexpr const char* kProjectDirectory =
    ".zeroslack";
constexpr const char* kProjectFile =
    "project.json";
constexpr const char* kLegacyFile = ".zs";

QString pathKey(const QString& path)
{
#ifdef Q_OS_WIN
    return path.toCaseFolded();
#else
    return path;
#endif
}

QString normalizePath(const QString& path)
{
    if (path.trimmed().isEmpty())
        return QString();
    return QDir::cleanPath(
        QDir::fromNativeSeparators(
            QFileInfo(path).absoluteFilePath()));
}

bool isInsideRoot(const QString& root,
                  const QString& path)
{
    const QString cleanRoot = normalizePath(root);
    const QString cleanPath = normalizePath(path);
    if (cleanRoot.isEmpty() || cleanPath.isEmpty())
        return false;
    const QString prefix =
        cleanRoot.endsWith(QLatin1Char('/'))
        ? cleanRoot
        : cleanRoot + QLatin1Char('/');
    return pathKey(cleanPath) == pathKey(cleanRoot)
        || pathKey(cleanPath).startsWith(
            pathKey(prefix));
}

QStringList uniquePreservingOrder(
    const QStringList& values)
{
    QStringList result;
    QSet<QString> seen;
    result.reserve(values.size());
    for (const QString& value : values) {
        const QString key = pathKey(value);
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

QStringList normalizeFileExtensions(
    const QStringList& extensions)
{
    QStringList normalized;
    normalized.reserve(extensions.size());
    for (QString extension : extensions) {
        extension = extension.trimmed().toLower();
        if (extension.isEmpty())
            continue;
        if (!extension.startsWith(
                QLatin1Char('.'))) {
            extension.prepend(QLatin1Char('.'));
        }
        normalized.append(extension);
    }
    normalized = uniquePreservingOrder(normalized);
    return normalized.isEmpty()
        ? WorkspaceConfigurationService::
              defaultFileExtensions()
        : normalized;
}

QHash<QString, QString> normalizeDefines(
    const QHash<QString, QString>& defines)
{
    QHash<QString, QString> normalized;
    for (auto it = defines.cbegin();
         it != defines.cend();
         ++it) {
        const QString key = it.key().trimmed();
        if (!key.isEmpty()) {
            normalized.insert(
                key, it.value().trimmed());
        }
    }
    return normalized;
}

QJsonObject definesObject(
    const QHash<QString, QString>& defines)
{
    QJsonObject object;
    QStringList keys = defines.keys();
    keys.sort(Qt::CaseInsensitive);
    for (const QString& key : keys)
        object.insert(key, defines.value(key));
    return object;
}

QHash<QString, QString> definesFromObject(
    const QJsonObject& object)
{
    QHash<QString, QString> defines;
    for (auto it = object.begin();
         it != object.end();
         ++it) {
        const QString key = it.key().trimmed();
        if (!key.isEmpty()) {
            defines.insert(
                key,
                it.value().toString().trimmed());
        }
    }
    return normalizeDefines(defines);
}

QString relativeProjectPath(const QString& root,
                            const QString& path)
{
    const QString cleanRoot = normalizePath(root);
    const QString cleanPath = normalizePath(path);
    if (cleanRoot.isEmpty() || cleanPath.isEmpty())
        return QString();
    QString relative =
        QDir(cleanRoot).relativeFilePath(cleanPath);
    relative =
        QDir::cleanPath(
            QDir::fromNativeSeparators(relative));
    return relative.isEmpty()
        ? QStringLiteral(".")
        : relative;
}

QJsonArray relativePathArray(
    const QString& root,
    const QStringList& paths)
{
    QJsonArray array;
    for (const QString& path : paths) {
        const QString relative =
            relativeProjectPath(root, path);
        if (!relative.isEmpty())
            array.append(relative);
    }
    return array;
}

QString resolveRelativePath(const QString& root,
                            const QString& stored)
{
    if (stored.trimmed().isEmpty())
        return QString();
    return normalizePath(
        QDir(root).absoluteFilePath(stored));
}

QStringList pathsFromPortableArray(
    const QString& root,
    const QJsonArray& array)
{
    QStringList paths;
    for (const QJsonValue& value : array) {
        const QString path =
            resolveRelativePath(root,
                                value.toString());
        if (!path.isEmpty())
            paths.append(path);
    }
    return uniquePreservingOrder(paths);
}

QString resolveLegacyPath(
    const QString& root,
    const QJsonValue& value,
    QStringList* externalPaths)
{
    QString path;
    bool relative = true;
    if (value.isObject()) {
        const QJsonObject object =
            value.toObject();
        path = object.value(
            QStringLiteral("path")).toString();
        relative = object.value(
            QStringLiteral("relative"))
                       .toBool(true);
    } else {
        path = value.toString();
        relative = !QFileInfo(path).isAbsolute();
    }
    if (path.trimmed().isEmpty())
        return QString();
    const QString clean = normalizePath(
        relative
            ? QDir(root).absoluteFilePath(path)
            : path);
    if (!relative
        && externalPaths
        && !isInsideRoot(root, clean)) {
        externalPaths->append(clean);
    }
    return clean;
}

QStringList pathsFromLegacyArray(
    const QString& root,
    const QJsonArray& array,
    QStringList* externalPaths)
{
    QStringList paths;
    for (const QJsonValue& value : array) {
        const QString path =
            resolveLegacyPath(
                root, value, externalPaths);
        if (!path.isEmpty())
            paths.append(path);
    }
    return uniquePreservingOrder(paths);
}
}

std::unique_ptr<WorkspaceConfigurationService>
    WorkspaceConfigurationService::instance = nullptr;

WorkspaceConfigurationService::
    WorkspaceConfigurationService(
        const QString& pathOverride)
    : projectFilePathOverride(pathOverride)
{
}

WorkspaceConfigurationService*
WorkspaceConfigurationService::getInstance()
{
    if (!instance) {
        instance = std::make_unique<
            WorkspaceConfigurationService>();
    }
    return instance.get();
}

QStringList
WorkspaceConfigurationService::
    defaultFileExtensions()
{
    return {QStringLiteral(".sv"),
            QStringLiteral(".svh"),
            QStringLiteral(".v"),
            QStringLiteral(".vh")};
}

QString WorkspaceConfigurationService::
    projectDirectoryPath(
        const QString& workspaceRoot)
{
    const QString root = normalizePath(workspaceRoot);
    return root.isEmpty()
        ? QString()
        : QDir(root).absoluteFilePath(
              QString::fromLatin1(
                  kProjectDirectory));
}

QString WorkspaceConfigurationService::
    projectFilePath(
        const QString& workspaceRoot)
{
    const QString directory =
        projectDirectoryPath(workspaceRoot);
    return directory.isEmpty()
        ? QString()
        : QDir(directory).absoluteFilePath(
              QString::fromLatin1(kProjectFile));
}

QString WorkspaceConfigurationService::
    legacyFilePath(
        const QString& workspaceRoot)
{
    const QString root = normalizePath(workspaceRoot);
    return root.isEmpty()
        ? QString()
        : QDir(root).absoluteFilePath(
              QString::fromLatin1(kLegacyFile));
}

WorkspaceConfiguration
WorkspaceConfigurationService::
    defaultConfiguration(
        const QString& workspaceRoot) const
{
    WorkspaceConfiguration configuration;
    configuration.workspaceRoot =
        normalizePath(workspaceRoot);
    if (!configuration.workspaceRoot.isEmpty()) {
        configuration.includeDirs = {
            configuration.workspaceRoot};
    }
    configuration.fileExtensions =
        defaultFileExtensions();
    return configuration;
}

WorkspaceConfigurationLoadResult
WorkspaceConfigurationService::loadWithResult(
    const QString& workspaceRoot) const
{
    WorkspaceConfigurationLoadResult result;
    result.configuration =
        defaultConfiguration(workspaceRoot);
    result.projectFilePath =
        effectiveProjectFilePath(workspaceRoot);
    result.legacyFilePath =
        legacyFilePath(workspaceRoot);
    if (!result.configuration.isValid()) {
        result.message =
            QStringLiteral(
                "No workspace is open.");
        return result;
    }

    QFile project(result.projectFilePath);
    if (project.open(QIODevice::ReadOnly
                     | QIODevice::Text)) {
        const QJsonDocument document =
            QJsonDocument::fromJson(
                project.readAll());
        project.close();
        const QJsonObject object =
            document.object();
        if (document.isObject()
            && object.value(
                   QStringLiteral("schema"))
                       .toString()
                   == QString::fromLatin1(
                       kProjectSchema)
            && object.value(
                   QStringLiteral("version"))
                       .toInt()
                   == kVersion) {
            WorkspaceConfiguration configuration;
            configuration.workspaceRoot =
                result.configuration.workspaceRoot;
            configuration.includeDirs =
                pathsFromPortableArray(
                    configuration.workspaceRoot,
                    object.value(
                        QStringLiteral(
                            "includeDirs"))
                        .toArray());
            configuration.ignoredDirs =
                pathsFromPortableArray(
                    configuration.workspaceRoot,
                    object.value(
                        QStringLiteral(
                            "ignoredDirs"))
                        .toArray());
            configuration.fileExtensions =
                object.value(
                    QStringLiteral(
                        "fileExtensions"))
                    .toVariant()
                    .toStringList();
            configuration.defines =
                definesFromObject(
                    object.value(
                        QStringLiteral("defines"))
                        .toObject());
            configuration.topModule =
                object.value(
                    QStringLiteral("topModule"))
                    .toString()
                    .trimmed();
            result.configuration =
                normalized(configuration);
            result.loaded = true;
            result.source =
                WorkspaceConfigurationSource::
                    ProjectFile;
            result.message =
                QStringLiteral(
                    "Portable project configuration loaded.");
            return result;
        }
        result.message =
            QStringLiteral(
                "Portable project configuration is invalid.");
        return result;
    }

    QFile legacy(result.legacyFilePath);
    if (!legacy.open(QIODevice::ReadOnly
                     | QIODevice::Text)) {
        result.message =
            QStringLiteral(
                "Using default workspace configuration.");
        return result;
    }
    const QJsonDocument legacyDocument =
        QJsonDocument::fromJson(
            legacy.readAll());
    legacy.close();
    const QJsonObject legacyObject =
        legacyDocument.object();
    if (!legacyDocument.isObject()
        || legacyObject.value(
               QStringLiteral("schema"))
                   .toString()
               != QString::fromLatin1(
                   kLegacySchema)
        || legacyObject.value(
               QStringLiteral("version"))
                   .toInt()
               != 1) {
        result.message =
            QStringLiteral(
                "Legacy .zs configuration is unsupported.");
        return result;
    }

    const QJsonObject object =
        legacyObject.value(
            QStringLiteral(
                "workspaceConfiguration"))
            .toObject();
    WorkspaceConfiguration configuration;
    configuration.workspaceRoot =
        result.configuration.workspaceRoot;
    configuration.includeDirs =
        pathsFromLegacyArray(
            configuration.workspaceRoot,
            object.value(
                QStringLiteral("includeDirs"))
                .toArray(),
            &result.externalPaths);
    configuration.ignoredDirs =
        pathsFromLegacyArray(
            configuration.workspaceRoot,
            object.value(
                QStringLiteral("ignoredDirs"))
                .toArray(),
            &result.externalPaths);
    configuration.fileExtensions =
        object.value(
            QStringLiteral("fileExtensions"))
            .toVariant()
            .toStringList();
    configuration.defines =
        definesFromObject(
            object.value(
                QStringLiteral("defines"))
                .toObject());
    configuration.topModule =
        object.value(
            QStringLiteral("topModule"))
            .toString()
            .trimmed();
    result.configuration =
        normalized(configuration);
    result.loaded = true;
    result.source =
        WorkspaceConfigurationSource::
            LegacySession;
    result.message =
        QStringLiteral(
            "Legacy .zs project configuration imported read-only.");
    return result;
}

WorkspaceConfiguration
WorkspaceConfigurationService::load(
    const QString& workspaceRoot) const
{
    return loadWithResult(workspaceRoot)
        .configuration;
}

bool WorkspaceConfigurationService::save(
    const WorkspaceConfiguration& configuration) const
{
    const WorkspaceConfiguration clean =
        normalized(configuration);
    if (!clean.isValid())
        return false;

    const QString filePath =
        effectiveProjectFilePath(
            clean.workspaceRoot);
    if (filePath.isEmpty()
        || !QDir().mkpath(
            QFileInfo(filePath)
                .absolutePath())) {
        return false;
    }

    QJsonObject object;
    object.insert(
        QStringLiteral("schema"),
        QString::fromLatin1(kProjectSchema));
    object.insert(
        QStringLiteral("version"),
        kVersion);
    object.insert(
        QStringLiteral("includeDirs"),
        relativePathArray(
            clean.workspaceRoot,
            clean.includeDirs));
    object.insert(
        QStringLiteral("ignoredDirs"),
        relativePathArray(
            clean.workspaceRoot,
            clean.ignoredDirs));
    object.insert(
        QStringLiteral("fileExtensions"),
        QJsonArray::fromStringList(
            clean.fileExtensions));
    object.insert(
        QStringLiteral("topModule"),
        clean.topModule);
    object.insert(
        QStringLiteral("defines"),
        definesObject(clean.defines));

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly
                   | QIODevice::Text)) {
        return false;
    }
    if (file.write(
            QJsonDocument(object).toJson(
                QJsonDocument::Indented))
        < 0) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

bool WorkspaceConfigurationService::clear(
    const QString& workspaceRoot) const
{
    const QString path =
        effectiveProjectFilePath(workspaceRoot);
    return !path.isEmpty()
        && (!QFileInfo(path).exists()
            || QFile::remove(path));
}

WorkspaceConfiguration
WorkspaceConfigurationService::normalized(
    const WorkspaceConfiguration& configuration) const
{
    WorkspaceConfiguration clean =
        configuration;
    clean.workspaceRoot =
        normalizePath(
            configuration.workspaceRoot);
    clean.includeDirs =
        normalizePaths(
            configuration.includeDirs);
    clean.ignoredDirs =
        normalizePaths(
            configuration.ignoredDirs);
    clean.fileExtensions =
        normalizeFileExtensions(
            configuration.fileExtensions);
    clean.defines =
        normalizeDefines(configuration.defines);
    clean.topModule =
        configuration.topModule.trimmed();
    if (clean.includeDirs.isEmpty()
        && !clean.workspaceRoot.isEmpty()) {
        clean.includeDirs = {
            clean.workspaceRoot};
    }
    return clean;
}

QString WorkspaceConfigurationService::
    effectiveProjectFilePath(
        const QString& workspaceRoot) const
{
    if (!projectFilePathOverride.isEmpty()) {
        return normalizePath(
            projectFilePathOverride);
    }
    return projectFilePath(workspaceRoot);
}
