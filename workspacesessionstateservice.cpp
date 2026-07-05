#include "workspacesessionstateservice.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <algorithm>

namespace {
constexpr const char* kSchema = "ZeroSlack.WorkspaceSessionState";
constexpr const char* kSessionFileName = ".zs";

QString pathKey(const QString& path)
{
#ifdef Q_OS_WIN
    return path.toCaseFolded();
#else
    return path;
#endif
}

QStringList uniqueSortedPaths(QStringList values)
{
    for (QString& value : values)
        value = QDir::cleanPath(QDir::fromNativeSeparators(value));
    values.removeAll(QString());
    values.removeDuplicates();
    values.sort(Qt::CaseInsensitive);
    return values;
}

QStringList sessionFileExtensions(const WorkspaceConfiguration& configuration)
{
    QStringList extensions = configuration.fileExtensions;
    if (extensions.isEmpty())
        extensions = WorkspaceConfigurationService::defaultFileExtensions();
    for (QString& extension : extensions) {
        extension = extension.trimmed().toLower();
        if (!extension.startsWith(QLatin1Char('.')))
            extension.prepend(QLatin1Char('.'));
    }
    extensions.removeDuplicates();
    return extensions;
}

bool isSessionSystemVerilogFile(const QString& filePath,
                                const WorkspaceConfiguration& configuration)
{
    if (filePath.isEmpty())
        return false;
    const QString suffix =
        QStringLiteral(".%1").arg(QFileInfo(filePath).suffix().toLower());
    return sessionFileExtensions(configuration)
        .contains(suffix, Qt::CaseInsensitive);
}

QString workspaceIdForRoot(const QString& root)
{
    const QByteArray digest =
        QCryptographicHash::hash(pathKey(root).toUtf8(),
                                 QCryptographicHash::Sha1)
            .toHex();
    return QString::fromLatin1(digest.left(16));
}

QJsonObject storedPathObject(const QString& root, const QString& path)
{
    QJsonObject object;
    const QString cleanPath =
        QDir::cleanPath(QDir::fromNativeSeparators(path));
    const QString normalizedRoot =
        QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(root).absoluteFilePath()));
    const QString normalizedPath =
        QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(cleanPath).absoluteFilePath()));
    const QString rootPrefix = normalizedRoot.endsWith(QLatin1Char('/'))
        ? normalizedRoot
        : normalizedRoot + QLatin1Char('/');
    const bool relative =
        pathKey(normalizedPath) == pathKey(normalizedRoot)
        || pathKey(normalizedPath).startsWith(pathKey(rootPrefix));
    object.insert(QStringLiteral("path"),
                  relative
                      ? QDir(normalizedRoot).relativeFilePath(normalizedPath)
                      : normalizedPath);
    object.insert(QStringLiteral("relative"), relative);
    if (!relative)
        object.insert(QStringLiteral("external"), true);
    return object;
}

QJsonArray storedPathArray(const QString& root, const QStringList& paths)
{
    QJsonArray array;
    for (const QString& path : paths) {
        if (!path.trimmed().isEmpty())
            array.append(storedPathObject(root, path));
    }
    return array;
}

QJsonObject definesObject(const QHash<QString, QString>& defines)
{
    QJsonObject object;
    QStringList keys = defines.keys();
    keys.sort(Qt::CaseInsensitive);
    for (const QString& key : keys)
        object.insert(key, defines.value(key));
    return object;
}

QHash<QString, QString> definesFromObject(const QJsonObject& object)
{
    QHash<QString, QString> defines;
    for (auto it = object.begin(); it != object.end(); ++it) {
        const QString key = it.key().trimmed();
        if (!key.isEmpty())
            defines.insert(key, it.value().toString().trimmed());
    }
    return defines;
}

QJsonObject fileFingerprint(const QString& filePath)
{
    QFileInfo info(filePath);
    QJsonObject object;
    if (info.exists()) {
        object.insert(QStringLiteral("size"),
                      QString::number(info.size()));
        object.insert(QStringLiteral("lastModified"),
                      info.lastModified().toUTC().toString(Qt::ISODate));
    }
    return object;
}
}

QString WorkspaceSessionStateService::sessionFilePath(
    const QString& workspaceRoot)
{
    const QString root = normalizePath(workspaceRoot);
    return root.isEmpty()
        ? QString()
        : QDir(root).absoluteFilePath(QString::fromLatin1(kSessionFileName));
}

bool WorkspaceSessionStateService::sessionFileExists(
    const QString& workspaceRoot)
{
    const QString path = sessionFilePath(workspaceRoot);
    return !path.isEmpty() && QFileInfo(path).isFile();
}

WorkspaceSessionSaveResult WorkspaceSessionStateService::save(
    const WorkspaceSessionState& state) const
{
    WorkspaceSessionSaveResult result;
    const QString root = normalizePath(state.workspaceRoot);
    result.sessionFilePath = sessionFilePath(root);
    if (root.isEmpty() || result.sessionFilePath.isEmpty()) {
        result.message = QStringLiteral("No workspace is open.");
        return result;
    }

    QJsonObject rootObject;
    rootObject.insert(QStringLiteral("schema"), QString::fromLatin1(kSchema));
    rootObject.insert(QStringLiteral("version"), kVersion);
    rootObject.insert(QStringLiteral("savedAt"),
                      QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    rootObject.insert(QStringLiteral("originalRoot"), root);
    rootObject.insert(QStringLiteral("workspaceId"),
                      state.workspaceId.isEmpty()
                          ? workspaceIdForRoot(root)
                          : state.workspaceId);

    const WorkspaceConfiguration configuration = state.configuration;
    QJsonObject configObject;
    configObject.insert(QStringLiteral("includeDirs"),
                        storedPathArray(root, configuration.includeDirs));
    configObject.insert(QStringLiteral("ignoredDirs"),
                        storedPathArray(root, configuration.ignoredDirs));
    configObject.insert(QStringLiteral("fileExtensions"),
                        QJsonArray::fromStringList(configuration.fileExtensions));
    configObject.insert(QStringLiteral("topModule"), configuration.topModule);
    configObject.insert(QStringLiteral("defines"),
                        definesObject(configuration.defines));
    rootObject.insert(QStringLiteral("workspaceConfiguration"), configObject);

    QJsonArray tabArray;
    for (const WorkspaceSessionTabState& tab : state.tabs) {
        const QString filePath = normalizePath(tab.filePath);
        if (!isInsideRoot(root, filePath)
            || !isSessionSystemVerilogFile(filePath, configuration)) {
            continue;
        }
        QJsonObject tabObject;
        tabObject.insert(QStringLiteral("path"), relativePath(root, filePath));
        tabObject.insert(QStringLiteral("cursorLine"), qMax(1, tab.cursorLine));
        tabObject.insert(QStringLiteral("cursorColumn"), qMax(1, tab.cursorColumn));
        tabObject.insert(QStringLiteral("verticalScroll"),
                         qMax(0, tab.verticalScrollValue));
        tabObject.insert(QStringLiteral("active"), tab.active);
        tabArray.append(tabObject);
    }
    rootObject.insert(QStringLiteral("tabs"), tabArray);

    QJsonObject uiObject;
    uiObject.insert(QStringLiteral("mainWindowGeometry"),
                    QString::fromLatin1(
                        state.ui.mainWindowGeometry.toBase64()));
    uiObject.insert(QStringLiteral("mainWindowState"),
                    QString::fromLatin1(state.ui.mainWindowState.toBase64()));
    rootObject.insert(QStringLiteral("ui"), uiObject);

    QJsonArray scannedArray;
    QJsonObject fingerprintObject;
    for (const QString& file : uniqueSortedPaths(state.scannedFiles)) {
        const QString filePath = normalizePath(file);
        if (!isInsideRoot(root, filePath))
            continue;
        const QString relative = relativePath(root, filePath);
        scannedArray.append(relative);
        fingerprintObject.insert(relative, fileFingerprint(filePath));
    }
    QJsonObject scanObject;
    scanObject.insert(QStringLiteral("files"), scannedArray);
    scanObject.insert(QStringLiteral("scanComplete"), state.scanComplete);
    scanObject.insert(QStringLiteral("fingerprints"), fingerprintObject);
    rootObject.insert(QStringLiteral("workspaceScan"), scanObject);

    QFile file(result.sessionFilePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        result.message = QStringLiteral("Failed to write workspace session.");
        return result;
    }
    file.write(QJsonDocument(rootObject).toJson(QJsonDocument::Indented));
    file.close();
    result.saved = true;
    result.message = QStringLiteral("Workspace session saved.");
    return result;
}

WorkspaceSessionRestoreResult WorkspaceSessionStateService::load(
    const QString& workspaceRoot) const
{
    WorkspaceSessionRestoreResult result;
    const QString root = normalizePath(workspaceRoot);
    result.sessionFilePath = sessionFilePath(root);
    if (root.isEmpty() || result.sessionFilePath.isEmpty()) {
        result.message = QStringLiteral("No workspace is open.");
        return result;
    }
    QFile file(result.sessionFilePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.message = QStringLiteral("No workspace session file found.");
        return result;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!document.isObject()) {
        result.message = QStringLiteral("Workspace session file is invalid.");
        return result;
    }

    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("schema")).toString()
            != QString::fromLatin1(kSchema)
        || object.value(QStringLiteral("version")).toInt() != kVersion) {
        result.message = QStringLiteral("Workspace session schema is unsupported.");
        return result;
    }

    WorkspaceSessionState state;
    state.workspaceRoot = root;
    state.originalRoot =
        normalizePath(object.value(QStringLiteral("originalRoot")).toString());
    state.workspaceId = object.value(QStringLiteral("workspaceId")).toString();
    state.savedAtUtc = object.value(QStringLiteral("savedAt")).toString();

    WorkspaceConfiguration configuration;
    configuration.workspaceRoot = root;
    const QJsonObject configObject =
        object.value(QStringLiteral("workspaceConfiguration")).toObject();
    auto restorePathArray = [&](const QJsonArray& array) {
        QStringList paths;
        for (const QJsonValue& value : array) {
            const QString path = resolveStoredPath(root,
                                                   value,
                                                   &result.externalPaths);
            if (!path.isEmpty())
                paths.append(path);
        }
        return uniqueSortedPaths(paths);
    };
    configuration.includeDirs =
        restorePathArray(configObject.value(QStringLiteral("includeDirs")).toArray());
    configuration.ignoredDirs =
        restorePathArray(configObject.value(QStringLiteral("ignoredDirs")).toArray());
    configuration.fileExtensions =
        configObject.value(QStringLiteral("fileExtensions")).toVariant().toStringList();
    configuration.topModule =
        configObject.value(QStringLiteral("topModule")).toString().trimmed();
    configuration.defines =
        definesFromObject(configObject.value(QStringLiteral("defines")).toObject());
    if (configuration.includeDirs.isEmpty())
        configuration.includeDirs = {root};
    if (configuration.fileExtensions.isEmpty())
        configuration.fileExtensions =
            WorkspaceConfigurationService::defaultFileExtensions();
    state.configuration = configuration;

    const QJsonArray tabArray = object.value(QStringLiteral("tabs")).toArray();
    for (const QJsonValue& value : tabArray) {
        const QJsonObject tabObject = value.toObject();
        const QString filePath =
            normalizePath(QDir(root).absoluteFilePath(
                tabObject.value(QStringLiteral("path")).toString()));
        if (!isInsideRoot(root, filePath)
            || !QFileInfo(filePath).isFile()
            || !isSessionSystemVerilogFile(filePath, configuration)) {
            result.skippedTabs.append(filePath);
            continue;
        }
        WorkspaceSessionTabState tab;
        tab.filePath = filePath;
        tab.cursorLine =
            qMax(1, tabObject.value(QStringLiteral("cursorLine")).toInt(1));
        tab.cursorColumn =
            qMax(1, tabObject.value(QStringLiteral("cursorColumn")).toInt(1));
        tab.verticalScrollValue =
            qMax(0, tabObject.value(QStringLiteral("verticalScroll")).toInt(0));
        tab.active = tabObject.value(QStringLiteral("active")).toBool(false);
        state.tabs.append(tab);
    }

    const QJsonObject uiObject = object.value(QStringLiteral("ui")).toObject();
    state.ui.mainWindowGeometry =
        QByteArray::fromBase64(
            uiObject.value(QStringLiteral("mainWindowGeometry"))
                .toString()
                .toLatin1());
    state.ui.mainWindowState =
        QByteArray::fromBase64(
            uiObject.value(QStringLiteral("mainWindowState"))
                .toString()
                .toLatin1());

    const QJsonObject scanObject =
        object.value(QStringLiteral("workspaceScan")).toObject();
    state.scanComplete =
        scanObject.value(QStringLiteral("scanComplete")).toBool(false);
    const QJsonArray scannedArray =
        scanObject.value(QStringLiteral("files")).toArray();
    for (const QJsonValue& value : scannedArray) {
        const QString filePath =
            normalizePath(QDir(root).absoluteFilePath(value.toString()));
        if (isInsideRoot(root, filePath) && QFileInfo(filePath).isFile())
            state.scannedFiles.append(filePath);
        else
            result.skippedScannedFiles.append(filePath);
    }
    state.scannedFiles = uniqueSortedPaths(state.scannedFiles);

    result.loaded = true;
    result.state = state;
    result.message = QStringLiteral("Workspace session loaded.");
    return result;
}

QString WorkspaceSessionStateService::normalizePath(const QString& path)
{
    if (path.trimmed().isEmpty())
        return QString();
    return QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()));
}

bool WorkspaceSessionStateService::isInsideRoot(const QString& root,
                                                const QString& path)
{
    const QString cleanRoot = normalizePath(root);
    const QString cleanPath = normalizePath(path);
    if (cleanRoot.isEmpty() || cleanPath.isEmpty())
        return false;
    const QString rootPrefix = cleanRoot.endsWith(QLatin1Char('/'))
        ? cleanRoot
        : cleanRoot + QLatin1Char('/');
    return pathKey(cleanPath) == pathKey(cleanRoot)
        || pathKey(cleanPath).startsWith(pathKey(rootPrefix));
}

QString WorkspaceSessionStateService::relativePath(const QString& root,
                                                   const QString& path)
{
    const QString cleanRoot = normalizePath(root);
    const QString cleanPath = normalizePath(path);
    if (cleanRoot.isEmpty() || cleanPath.isEmpty())
        return QString();
    return QDir(cleanRoot).relativeFilePath(cleanPath);
}

QString WorkspaceSessionStateService::resolveStoredPath(
    const QString& root,
    const QJsonValue& value,
    QStringList* externalPaths)
{
    QString path;
    bool relative = true;
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        path = object.value(QStringLiteral("path")).toString();
        relative = object.value(QStringLiteral("relative")).toBool(true);
    } else {
        path = value.toString();
        relative = !QFileInfo(path).isAbsolute();
    }
    if (path.trimmed().isEmpty())
        return QString();
    const QString resolved =
        relative ? QDir(root).absoluteFilePath(path) : path;
    const QString clean = normalizePath(resolved);
    if (!relative && externalPaths && !isInsideRoot(root, clean))
        externalPaths->append(clean);
    return clean;
}
