#include "workspacesessionstateservice.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

namespace {
constexpr const char* kLocalSchema =
    "ZeroSlack.LocalWorkspaceSession";
constexpr const char* kLegacySchema =
    "ZeroSlack.WorkspaceSessionState";
constexpr const char* kLegacyFile = ".zs";
constexpr const char* kSettingsRoot =
    "workspaceSessions";
constexpr const char* kSettingsVersion = "v2";
constexpr const char* kSettingsWorkspaces =
    "workspaces";
constexpr const char* kSettingsState = "state";

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
    for (QString& value : values) {
        value = QDir::cleanPath(
            QDir::fromNativeSeparators(value));
    }
    values.removeAll(QString());
    values.removeDuplicates();
    values.sort(Qt::CaseInsensitive);
    return values;
}

QJsonObject tabObject(
    const QString& root,
    const WorkspaceSessionTabState& tab)
{
    QJsonObject object;
    object.insert(
        QStringLiteral("path"),
        QDir(root).relativeFilePath(
            tab.filePath));
    object.insert(
        QStringLiteral("cursorLine"),
        qMax(1, tab.cursorLine));
    object.insert(
        QStringLiteral("cursorColumn"),
        qMax(1, tab.cursorColumn));
    object.insert(
        QStringLiteral("verticalScroll"),
        qMax(0, tab.verticalScrollValue));
    object.insert(
        QStringLiteral("active"),
        tab.active);
    return object;
}

QJsonObject sessionObject(
    const WorkspaceSessionState& state,
    const QString& root,
    const QString& identity)
{
    QJsonObject object;
    object.insert(
        QStringLiteral("schema"),
        QString::fromLatin1(kLocalSchema));
    object.insert(
        QStringLiteral("version"),
        WorkspaceSessionStateService::kVersion);
    object.insert(
        QStringLiteral("workspaceId"),
        identity);
    object.insert(
        QStringLiteral("savedAt"),
        QDateTime::currentDateTimeUtc()
            .toString(Qt::ISODate));

    QJsonArray tabs;
    for (const WorkspaceSessionTabState& tab :
         state.tabs) {
        const QString filePath =
            QDir::cleanPath(
                QDir::fromNativeSeparators(
                    QFileInfo(tab.filePath)
                        .absoluteFilePath()));
        const QString prefix =
            root.endsWith(QLatin1Char('/'))
            ? root
            : root + QLatin1Char('/');
        if (pathKey(filePath)
                != pathKey(root)
            && !pathKey(filePath)
                    .startsWith(
                        pathKey(prefix))) {
            continue;
        }
        tabs.append(
            tabObject(root, tab));
    }
    object.insert(
        QStringLiteral("tabs"), tabs);

    QJsonObject ui;
    ui.insert(
        QStringLiteral("mainWindowGeometry"),
        QString::fromLatin1(
            state.ui.mainWindowGeometry
                .toBase64()));
    ui.insert(
        QStringLiteral("mainWindowState"),
        QString::fromLatin1(
            state.ui.mainWindowState
                .toBase64()));
    object.insert(QStringLiteral("ui"), ui);

    QJsonArray files;
    for (const QString& file :
         uniqueSortedPaths(state.scannedFiles)) {
        const QString filePath =
            QDir::cleanPath(
                QDir::fromNativeSeparators(
                    QFileInfo(file)
                        .absoluteFilePath()));
        const QString prefix =
            root.endsWith(QLatin1Char('/'))
            ? root
            : root + QLatin1Char('/');
        if (pathKey(filePath)
                == pathKey(root)
            || pathKey(filePath)
                   .startsWith(
                       pathKey(prefix))) {
            files.append(
                QDir(root)
                    .relativeFilePath(
                        filePath));
        }
    }
    QJsonObject scan;
    scan.insert(
        QStringLiteral("files"), files);
    scan.insert(
        QStringLiteral("scanComplete"),
        state.scanComplete);
    object.insert(
        QStringLiteral("workspaceScan"),
        scan);
    return object;
}

void restoreTabs(
    const QString& root,
    const QJsonArray& array,
    QList<WorkspaceSessionTabState>* tabs,
    QStringList* skipped)
{
    if (!tabs || !skipped)
        return;
    for (const QJsonValue& value : array) {
        const QJsonObject object =
            value.toObject();
        const QString path =
            QDir::cleanPath(
                QDir::fromNativeSeparators(
                    QFileInfo(
                        QDir(root)
                            .absoluteFilePath(
                                object.value(
                                    QStringLiteral(
                                        "path"))
                                    .toString()))
                        .absoluteFilePath()));
        const QString prefix =
            root.endsWith(QLatin1Char('/'))
            ? root
            : root + QLatin1Char('/');
        if ((pathKey(path) != pathKey(root)
             && !pathKey(path).startsWith(
                 pathKey(prefix)))
            || !QFileInfo(path).isFile()) {
            skipped->append(path);
            continue;
        }
        WorkspaceSessionTabState tab;
        tab.filePath = path;
        tab.cursorLine =
            qMax(1,
                 object.value(
                     QStringLiteral(
                         "cursorLine"))
                     .toInt(1));
        tab.cursorColumn =
            qMax(1,
                 object.value(
                     QStringLiteral(
                         "cursorColumn"))
                     .toInt(1));
        tab.verticalScrollValue =
            qMax(0,
                 object.value(
                     QStringLiteral(
                         "verticalScroll"))
                     .toInt(0));
        tab.active =
            object.value(
                QStringLiteral("active"))
                .toBool(false);
        tabs->append(tab);
    }
}

void restoreUi(
    const QJsonObject& object,
    WorkspaceSessionUiState* ui)
{
    if (!ui)
        return;
    ui->mainWindowGeometry =
        QByteArray::fromBase64(
            object.value(
                QStringLiteral(
                    "mainWindowGeometry"))
                .toString()
                .toLatin1());
    ui->mainWindowState =
        QByteArray::fromBase64(
            object.value(
                QStringLiteral(
                    "mainWindowState"))
                .toString()
                .toLatin1());
}

void restoreScan(
    const QString& root,
    const QJsonObject& object,
    WorkspaceSessionState* state,
    QStringList* skipped)
{
    if (!state || !skipped)
        return;
    state->scanComplete =
        object.value(
            QStringLiteral("scanComplete"))
            .toBool(false);
    for (const QJsonValue& value :
         object.value(
             QStringLiteral("files"))
             .toArray()) {
        const QString path =
            QDir::cleanPath(
                QDir::fromNativeSeparators(
                    QFileInfo(
                        QDir(root)
                            .absoluteFilePath(
                                value.toString()))
                        .absoluteFilePath()));
        const QString prefix =
            root.endsWith(QLatin1Char('/'))
            ? root
            : root + QLatin1Char('/');
        if ((pathKey(path) == pathKey(root)
             || pathKey(path).startsWith(
                 pathKey(prefix)))
            && QFileInfo(path).isFile()) {
            state->scannedFiles.append(path);
        } else {
            skipped->append(path);
        }
    }
    state->scannedFiles =
        uniqueSortedPaths(
            state->scannedFiles);
}
}

WorkspaceSessionStateService::
    WorkspaceSessionStateService(
        const QString& path)
    : settingsFilePath(path)
{
}

QString WorkspaceSessionStateService::
    workspaceIdentity(
        const QString& workspaceRoot)
{
    const QString root =
        normalizePath(workspaceRoot);
    if (root.isEmpty())
        return QString();
    const QByteArray digest =
        QCryptographicHash::hash(
            pathKey(root).toUtf8(),
            QCryptographicHash::Sha256)
            .toHex();
    return QString::fromLatin1(
        digest.left(24));
}

QString WorkspaceSessionStateService::
    legacySessionFilePath(
        const QString& workspaceRoot)
{
    const QString root =
        normalizePath(workspaceRoot);
    return root.isEmpty()
        ? QString()
        : QDir(root).absoluteFilePath(
              QString::fromLatin1(
                  kLegacyFile));
}

bool WorkspaceSessionStateService::
    legacySessionFileExists(
        const QString& workspaceRoot)
{
    const QString path =
        legacySessionFilePath(
            workspaceRoot);
    return !path.isEmpty()
        && QFileInfo(path).isFile();
}

QString WorkspaceSessionStateService::
    localStoragePath() const
{
    if (!settingsFilePath.isEmpty())
        return normalizePath(settingsFilePath);
    const QString environmentOverride =
        qEnvironmentVariable(
            "ZEROSLACK_SESSION_STORAGE_PATH");
    if (!environmentOverride.isEmpty())
        return normalizePath(environmentOverride);
    const QString base =
        QStandardPaths::writableLocation(
            QStandardPaths::
                GenericDataLocation);
    if (base.isEmpty())
        return QString();
    return normalizePath(
        QDir(base).absoluteFilePath(
            QStringLiteral(
                "ZeroSlack/ZeroSlack/"
                "workspace-sessions.ini")));
}

bool WorkspaceSessionStateService::
    sessionExists(
        const QString& workspaceRoot) const
{
    const QString group =
        settingsGroup(workspaceRoot);
    if (group.isEmpty())
        return false;
    std::unique_ptr<QSettings> settings =
        makeSettings();
    if (!settings)
        return false;
    settings->beginGroup(group);
    const bool exists =
        settings->contains(
            QString::fromLatin1(
                kSettingsState));
    settings->endGroup();
    return exists;
}

WorkspaceSessionSaveResult
WorkspaceSessionStateService::save(
    const WorkspaceSessionState& state) const
{
    WorkspaceSessionSaveResult result;
    const QString root =
        normalizePath(state.workspaceRoot);
    result.storagePath =
        localStoragePath();
    result.workspaceIdentity =
        workspaceIdentity(root);
    if (root.isEmpty()
        || result.storagePath.isEmpty()
        || result.workspaceIdentity.isEmpty()) {
        result.message =
            QStringLiteral(
                "No workspace is open.");
        return result;
    }

    std::unique_ptr<QSettings> settings =
        makeSettings();
    if (!settings) {
        result.message =
            QStringLiteral(
                "Local session storage is unavailable.");
        return result;
    }
    settings->beginGroup(
        settingsGroup(root));
    settings->remove(QString());
    settings->setValue(
        QString::fromLatin1(kSettingsState),
        QJsonDocument(
            sessionObject(
                state,
                root,
                result.workspaceIdentity))
            .toJson(QJsonDocument::Compact));
    settings->endGroup();
    settings->sync();
    result.saved =
        settings->status()
        == QSettings::NoError;
    result.message =
        result.saved
        ? QStringLiteral(
              "Local workspace session saved; "
              "portable project configuration "
              "is unchanged.")
        : QStringLiteral(
              "Failed to save local workspace session.");
    return result;
}

WorkspaceSessionRestoreResult
WorkspaceSessionStateService::load(
    const QString& workspaceRoot) const
{
    WorkspaceSessionRestoreResult result;
    const QString root =
        normalizePath(workspaceRoot);
    result.storagePath =
        localStoragePath();
    if (root.isEmpty()
        || result.storagePath.isEmpty()) {
        result.message =
            QStringLiteral(
                "No workspace is open.");
        return result;
    }

    std::unique_ptr<QSettings> settings =
        makeSettings();
    if (!settings) {
        result.message =
            QStringLiteral(
                "Local session storage is unavailable.");
        return result;
    }
    settings->beginGroup(
        settingsGroup(root));
    const QByteArray bytes =
        settings->value(
            QString::fromLatin1(
                kSettingsState))
            .toByteArray();
    settings->endGroup();
    if (bytes.isEmpty()) {
        result.message =
            QStringLiteral(
                "No local workspace session found.");
        return result;
    }
    const QJsonDocument document =
        QJsonDocument::fromJson(bytes);
    const QJsonObject object =
        document.object();
    if (!document.isObject()
        || object.value(
               QStringLiteral("schema"))
                   .toString()
               != QString::fromLatin1(
                   kLocalSchema)
        || object.value(
               QStringLiteral("version"))
                   .toInt()
               != kVersion) {
        result.message =
            QStringLiteral(
                "Local workspace session schema "
                "is unsupported.");
        return result;
    }

    WorkspaceSessionState state;
    state.workspaceRoot = root;
    state.workspaceId =
        object.value(
            QStringLiteral("workspaceId"))
            .toString();
    state.savedAtUtc =
        object.value(
            QStringLiteral("savedAt"))
            .toString();
    restoreTabs(
        root,
        object.value(
            QStringLiteral("tabs"))
            .toArray(),
        &state.tabs,
        &result.skippedTabs);
    restoreUi(
        object.value(
            QStringLiteral("ui"))
            .toObject(),
        &state.ui);
    restoreScan(
        root,
        object.value(
            QStringLiteral(
                "workspaceScan"))
            .toObject(),
        &state,
        &result.skippedScannedFiles);
    result.loaded = true;
    result.state = state;
    result.message =
        QStringLiteral(
            "Local workspace session loaded.");
    return result;
}

bool WorkspaceSessionStateService::clear(
    const QString& workspaceRoot) const
{
    const QString group =
        settingsGroup(workspaceRoot);
    if (group.isEmpty())
        return false;
    std::unique_ptr<QSettings> settings =
        makeSettings();
    if (!settings)
        return false;
    settings->beginGroup(group);
    settings->remove(QString());
    settings->endGroup();
    settings->sync();
    return settings->status()
        == QSettings::NoError;
}

WorkspaceLegacyImportResult
WorkspaceSessionStateService::loadLegacy(
    const QString& workspaceRoot) const
{
    WorkspaceLegacyImportResult result;
    const QString root =
        normalizePath(workspaceRoot);
    result.legacyFilePath =
        legacySessionFilePath(root);
    if (root.isEmpty()
        || result.legacyFilePath.isEmpty()) {
        result.message =
            QStringLiteral(
                "No workspace is open.");
        return result;
    }

    QFile file(result.legacyFilePath);
    if (!file.open(QIODevice::ReadOnly
                   | QIODevice::Text)) {
        result.message =
            QStringLiteral(
                "No legacy .zs session found.");
        return result;
    }
    const QJsonDocument document =
        QJsonDocument::fromJson(
            file.readAll());
    file.close();
    const QJsonObject object =
        document.object();
    if (!document.isObject()
        || object.value(
               QStringLiteral("schema"))
                   .toString()
               != QString::fromLatin1(
                   kLegacySchema)
        || object.value(
               QStringLiteral("version"))
                   .toInt()
               != 1) {
        result.message =
            QStringLiteral(
                "Legacy .zs session schema "
                "is unsupported.");
        return result;
    }

    WorkspaceSessionState state;
    state.workspaceRoot = root;
    state.originalRoot =
        normalizePath(
            object.value(
                QStringLiteral(
                    "originalRoot"))
                .toString());
    state.workspaceId =
        object.value(
            QStringLiteral("workspaceId"))
            .toString();
    state.savedAtUtc =
        object.value(
            QStringLiteral("savedAt"))
            .toString();
    restoreTabs(
        root,
        object.value(
            QStringLiteral("tabs"))
            .toArray(),
        &state.tabs,
        &result.skippedTabs);
    restoreUi(
        object.value(
            QStringLiteral("ui"))
            .toObject(),
        &state.ui);
    restoreScan(
        root,
        object.value(
            QStringLiteral(
                "workspaceScan"))
            .toObject(),
        &state,
        &result.skippedScannedFiles);
    result.loaded = true;
    result.state = state;
    result.message =
        QStringLiteral(
            "Legacy .zs session imported read-only.");
    return result;
}

QString WorkspaceSessionStateService::
    normalizePath(const QString& path)
{
    if (path.trimmed().isEmpty())
        return QString();
    return QDir::cleanPath(
        QDir::fromNativeSeparators(
            QFileInfo(path).absoluteFilePath()));
}

bool WorkspaceSessionStateService::
    isInsideRoot(const QString& root,
                 const QString& path)
{
    const QString cleanRoot =
        normalizePath(root);
    const QString cleanPath =
        normalizePath(path);
    if (cleanRoot.isEmpty()
        || cleanPath.isEmpty()) {
        return false;
    }
    const QString prefix =
        cleanRoot.endsWith(QLatin1Char('/'))
        ? cleanRoot
        : cleanRoot + QLatin1Char('/');
    return pathKey(cleanPath)
               == pathKey(cleanRoot)
        || pathKey(cleanPath)
               .startsWith(
                   pathKey(prefix));
}

QString WorkspaceSessionStateService::
    relativePath(const QString& root,
                 const QString& path)
{
    const QString cleanRoot =
        normalizePath(root);
    const QString cleanPath =
        normalizePath(path);
    if (cleanRoot.isEmpty()
        || cleanPath.isEmpty()) {
        return QString();
    }
    return QDir(cleanRoot)
        .relativeFilePath(cleanPath);
}

QString WorkspaceSessionStateService::
    settingsGroup(
        const QString& workspaceRoot)
{
    const QString identity =
        workspaceIdentity(workspaceRoot);
    if (identity.isEmpty())
        return QString();
    return QStringLiteral("%1/%2/%3/%4")
        .arg(QString::fromLatin1(
                 kSettingsRoot),
             QString::fromLatin1(
                 kSettingsVersion),
             QString::fromLatin1(
                 kSettingsWorkspaces),
             identity);
}

std::unique_ptr<QSettings>
WorkspaceSessionStateService::makeSettings() const
{
    const QString path = localStoragePath();
    if (path.isEmpty())
        return nullptr;
    if (!QDir().mkpath(
            QFileInfo(path)
                .absolutePath())) {
        return nullptr;
    }
    return std::make_unique<QSettings>(
        path, QSettings::IniFormat);
}
