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
QJsonArray floatingInstancesToJson(const QList<ContextFloatingInstanceState>& instances)
{
    QJsonArray result;
    for (const auto& instance : instances) {
        result.append(QJsonObject{
            {"resource", QJsonObject::fromVariantMap(instance.resource)},
            {"x", instance.x}, {"y", instance.y},
            {"width", ContextWorkspaceState::boundedPeekWidth(instance.width)},
            {"height", ContextWorkspaceState::boundedPeekHeight(instance.height)},
            {"screenName", instance.screenName}, {"geometryValid", instance.geometryValid}, {"kept", instance.kept}});
    }
    return result;
}

QList<ContextFloatingInstanceState> floatingInstancesFromJson(const QJsonArray& array)
{
    QList<ContextFloatingInstanceState> result;
    for (const auto& entry : array) {
        const auto object = entry.toObject();
        ContextFloatingInstanceState instance;
        instance.resource = object.value("resource").toObject().toVariantMap();
        instance.x = object.value("x").toInt();
        instance.y = object.value("y").toInt();
        instance.width = ContextWorkspaceState::boundedPeekWidth(object.value("width").toInt(ContextWorkspaceState::kDefaultPeekWidth));
        instance.height = ContextWorkspaceState::boundedPeekHeight(object.value("height").toInt(ContextWorkspaceState::kDefaultPeekHeight));
        instance.screenName = object.value("screenName").toString();
        instance.geometryValid = object.value("geometryValid").toBool();
        instance.kept = object.value("kept").toBool();
        result.append(instance);
    }
    return result;
}

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

QJsonArray stringArray(const QStringList& values)
{
    QJsonArray array;
    for (const QString& value : values)
        array.append(value);
    return array;
}

QStringList stringList(const QJsonArray& array)
{
    QStringList values;
    for (const QJsonValue& value : array) {
        const QString text = value.toString();
        if (!text.isEmpty() && !values.contains(text))
            values.append(text);
    }
    return values;
}

QVariant providerStateValue(const QJsonValue& value)
{
    if (value.isObject()) {
        QVariantMap result;
        const QJsonObject object = value.toObject();
        for (auto it = object.constBegin(); it != object.constEnd(); ++it)
            result.insert(it.key(), providerStateValue(it.value()));
        return result;
    }
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        const bool stringsOnly = std::all_of(
            array.constBegin(),
            array.constEnd(),
            [](const QJsonValue& entry) { return entry.isString(); });
        if (stringsOnly) {
            QStringList result;
            result.reserve(array.size());
            for (const QJsonValue& entry : array)
                result.append(entry.toString());
            return result;
        }
        QVariantList result;
        result.reserve(array.size());
        for (const QJsonValue& entry : array)
            result.append(providerStateValue(entry));
        return result;
    }
    return value.toVariant();
}

QVariantMap providerStatesFromJson(const QJsonObject& object)
{
    QVariantMap result;
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
        result.insert(it.key(), providerStateValue(it.value()));
    return result;
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
        QStringLiteral("horizontalScroll"),
        qMax(0, tab.horizontalScrollValue));
    object.insert(
        QStringLiteral("active"),
        tab.active);
    object.insert(
        QStringLiteral("viewId"),
        tab.viewId);
    object.insert(
        QStringLiteral("groupIndex"),
        qMax(0, tab.groupIndex));
    object.insert(
        QStringLiteral("tabIndex"),
        qMax(0, tab.tabIndex));
    object.insert(
        QStringLiteral("locked"),
        tab.locked);
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
    ui.insert(
        QStringLiteral("navigationFilesQuery"),
        state.ui.navigationFilesQuery);
    ui.insert(
        QStringLiteral("navigationDesignQuery"),
        state.ui.navigationDesignQuery);
    ui.insert(
        QStringLiteral("tabGroupingMode"),
        state.ui.tabGroupingMode);
    QJsonObject panelLayout;
    panelLayout.insert(
        QStringLiteral("version"),
        PanelLayoutState::kVersion);
    panelLayout.insert(
        QStringLiteral("order"),
        stringArray(state.ui.panelLayout.bottomPanelOrder));
    panelLayout.insert(
        QStringLiteral("closed"),
        stringArray(state.ui.panelLayout.closedBottomPanels));
    panelLayout.insert(
        QStringLiteral("pinned"),
        stringArray(state.ui.panelLayout.pinnedBottomPanels));
    panelLayout.insert(
        QStringLiteral("active"),
        state.ui.panelLayout.activeBottomPanel);
    panelLayout.insert(
        QStringLiteral("last"),
        state.ui.panelLayout.lastBottomPanel);
    QJsonObject panelHeights;
    for (auto iterator =
             state.ui.panelLayout.bottomPanelHeights.cbegin();
         iterator != state.ui.panelLayout.bottomPanelHeights.cend();
         ++iterator) {
        panelHeights.insert(
            iterator.key(), qMax(64, iterator.value()));
    }
    panelLayout.insert(
        QStringLiteral("heights"), panelHeights);
    QJsonObject panelViewStates;
    for (auto iterator =
             state.ui.panelLayout.bottomPanelViewStates.cbegin();
         iterator != state.ui.panelLayout.bottomPanelViewStates.cend();
         ++iterator) {
        panelViewStates.insert(
            iterator.key(),
            QJsonObject::fromVariantMap(iterator.value()));
    }
    panelLayout.insert(
        QStringLiteral("viewStates"), panelViewStates);
    panelLayout.insert(
        QStringLiteral("expandedHeight"),
        qMax(64, state.ui.panelLayout.expandedBottomHeight));
    panelLayout.insert(
        QStringLiteral("collapsed"),
        state.ui.panelLayout.bottomCollapsed);
    panelLayout.insert(
        QStringLiteral("navigationVisible"),
        state.ui.panelLayout.navigationVisible);
    panelLayout.insert(
        QStringLiteral("valid"),
        state.ui.panelLayout.valid);
    ui.insert(QStringLiteral("panelLayout"), panelLayout);

    QJsonObject contextWorkspace;
    contextWorkspace.insert(
        QStringLiteral("version"),
        ContextWorkspaceState::kVersion);
    QJsonArray pinnedResources;
    for (const QVariantMap& resource :
         state.ui.contextWorkspace.pinnedResources) {
        pinnedResources.append(QJsonObject::fromVariantMap(resource));
    }
    contextWorkspace.insert(
        QStringLiteral("pinnedResources"), pinnedResources);
    contextWorkspace.insert(
        QStringLiteral("providerStates"),
        QJsonObject::fromVariantMap(
            state.ui.contextWorkspace.providerStates));
    contextWorkspace.insert(
        QStringLiteral("activePinnedResourceKey"),
        state.ui.contextWorkspace.activePinnedResourceKey);
    contextWorkspace.insert(
        QStringLiteral("peekWidth"),
        ContextWorkspaceState::boundedPeekWidth(
            state.ui.contextWorkspace.peekWidth));
    contextWorkspace.insert(
        QStringLiteral("peekHeight"),
        ContextWorkspaceState::boundedPeekHeight(
            state.ui.contextWorkspace.peekHeight));
    contextWorkspace.insert(
        QStringLiteral("dockWidth"),
        ContextWorkspaceState::boundedDockWidth(
            state.ui.contextWorkspace.dockWidth));
    contextWorkspace.insert(
        QStringLiteral("dockVisible"),
        state.ui.contextWorkspace.dockVisible);
    contextWorkspace.insert(
        QStringLiteral("railVisible"),
        state.ui.contextWorkspace.railVisible);
    contextWorkspace.insert(
        QStringLiteral("valid"),
        state.ui.contextWorkspace.valid);
    contextWorkspace.insert(QStringLiteral("floatingX"), state.ui.contextWorkspace.floatingX);
    contextWorkspace.insert(QStringLiteral("floatingY"), state.ui.contextWorkspace.floatingY);
    contextWorkspace.insert(QStringLiteral("floatingWidth"),
        ContextWorkspaceState::boundedPeekWidth(state.ui.contextWorkspace.floatingWidth));
    contextWorkspace.insert(QStringLiteral("floatingHeight"),
        ContextWorkspaceState::boundedPeekHeight(state.ui.contextWorkspace.floatingHeight));
    contextWorkspace.insert(QStringLiteral("floatingScreenName"), state.ui.contextWorkspace.floatingScreenName);
    contextWorkspace.insert(QStringLiteral("floatingGeometryValid"), state.ui.contextWorkspace.floatingGeometryValid);
    contextWorkspace.insert(QStringLiteral("floatingInstances"), floatingInstancesToJson(state.ui.contextWorkspace.floatingInstances));
    contextWorkspace.insert(QStringLiteral("floatingCollapsed"), state.ui.contextWorkspace.floatingCollapsed);
    QJsonObject documentLayouts;
    for (auto it = state.ui.contextWorkspace.documentFloatingLayouts.cbegin(); it != state.ui.contextWorkspace.documentFloatingLayouts.cend(); ++it)
        documentLayouts.insert(it.key(), floatingInstancesToJson(it.value()));
    contextWorkspace.insert(QStringLiteral("documentFloatingLayouts"), documentLayouts);
    contextWorkspace.insert(QStringLiteral("documentFloatingOrder"), QJsonArray::fromStringList(state.ui.contextWorkspace.documentFloatingOrder));
    ui.insert(QStringLiteral("contextWorkspace"), contextWorkspace);
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
        tab.horizontalScrollValue =
            qMax(0,
                 object.value(
                     QStringLiteral(
                         "horizontalScroll"))
                     .toInt(0));
        tab.active =
            object.value(
                QStringLiteral("active"))
                .toBool(false);
        tab.viewId =
            object.value(
                QStringLiteral("viewId"))
                .toString();
        tab.groupIndex =
            qMax(0,
                 object.value(
                     QStringLiteral("groupIndex"))
                     .toInt(0));
        tab.tabIndex =
            qMax(0,
                 object.value(
                     QStringLiteral("tabIndex"))
                     .toInt(0));
        tab.locked =
            object.value(
                QStringLiteral("locked"))
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
    ui->navigationFilesQuery =
        object.value(
            QStringLiteral("navigationFilesQuery"))
            .toString();
    ui->navigationDesignQuery =
        object.value(
            QStringLiteral("navigationDesignQuery"))
            .toString();
    ui->tabGroupingMode =
        object.value(
            QStringLiteral("tabGroupingMode"))
            .toString(QStringLiteral("none"));
    if (object.contains(QStringLiteral("panelLayout"))) {
        const QJsonObject panelLayout =
            object.value(QStringLiteral("panelLayout")).toObject();
        ui->panelLayout.version =
            panelLayout.value(QStringLiteral("version"))
                .toInt(1);
        ui->panelLayout.bottomPanelOrder =
            stringList(
                panelLayout.value(QStringLiteral("order")).toArray());
        ui->panelLayout.closedBottomPanels =
            stringList(
                panelLayout.value(QStringLiteral("closed")).toArray());
        ui->panelLayout.pinnedBottomPanels =
            stringList(
                panelLayout.value(QStringLiteral("pinned")).toArray());
        ui->panelLayout.activeBottomPanel =
            panelLayout.value(QStringLiteral("active")).toString();
        ui->panelLayout.lastBottomPanel =
            panelLayout.value(QStringLiteral("last"))
                .toString(ui->panelLayout.activeBottomPanel);
        const QJsonObject panelHeights =
            panelLayout.value(QStringLiteral("heights")).toObject();
        for (auto iterator = panelHeights.begin();
             iterator != panelHeights.end(); ++iterator) {
            ui->panelLayout.bottomPanelHeights.insert(
                iterator.key(), qMax(64, iterator.value().toInt()));
        }
        const QJsonObject panelViewStates =
            panelLayout.value(QStringLiteral("viewStates")).toObject();
        for (auto iterator = panelViewStates.begin();
             iterator != panelViewStates.end(); ++iterator) {
            if (iterator.value().isObject()) {
                ui->panelLayout.bottomPanelViewStates.insert(
                    iterator.key(),
                    iterator.value().toObject().toVariantMap());
            }
        }
        ui->panelLayout.expandedBottomHeight =
            qMax(64,
                 panelLayout.value(
                     QStringLiteral("expandedHeight"))
                     .toInt(PanelLayoutState::kDefaultBottomHeight));
        ui->panelLayout.bottomCollapsed =
            panelLayout.value(QStringLiteral("collapsed")).toBool(false);
        ui->panelLayout.navigationVisible =
            panelLayout.value(
                QStringLiteral("navigationVisible"))
                .toBool(true);
        ui->panelLayout.valid =
            panelLayout.value(QStringLiteral("valid")).toBool(true);
    }
    if (object.contains(QStringLiteral("contextWorkspace"))) {
        const QJsonObject contextWorkspace =
            object.value(QStringLiteral("contextWorkspace")).toObject();
        const int contextVersion =
            contextWorkspace.value(QStringLiteral("version")).toInt();
        if (contextVersion >= ContextWorkspaceState::kLegacyVersion
            && contextVersion <= ContextWorkspaceState::kVersion) {
            for (const QJsonValue& value :
                 contextWorkspace.value(
                     QStringLiteral("pinnedResources")).toArray()) {
                if (value.isObject()) {
                    ui->contextWorkspace.pinnedResources.append(
                        value.toObject().toVariantMap());
                }
            }
            ui->contextWorkspace.activePinnedResourceKey =
                contextWorkspace.value(
                    QStringLiteral("activePinnedResourceKey")).toString();
            ui->contextWorkspace.peekWidth =
                ContextWorkspaceState::boundedPeekWidth(
                    contextWorkspace.value(
                        QStringLiteral("peekWidth"))
                        .toInt(
                            ContextWorkspaceState::
                                kDefaultPeekWidth));
            ui->contextWorkspace.peekHeight =
                contextVersion >= ContextWorkspaceState::kResizableVersion
                ? ContextWorkspaceState::boundedPeekHeight(
                      contextWorkspace.value(
                          QStringLiteral("peekHeight"))
                          .toInt(
                              ContextWorkspaceState::
                                  kDefaultPeekHeight))
                : ContextWorkspaceState::kDefaultPeekHeight;
            ui->contextWorkspace.dockWidth =
                ContextWorkspaceState::boundedDockWidth(
                    contextWorkspace.value(
                        QStringLiteral("dockWidth"))
                        .toInt(
                            ContextWorkspaceState::
                                kDefaultDockWidth));
            ui->contextWorkspace.dockVisible =
                contextWorkspace.value(
                    QStringLiteral("dockVisible")).toBool(false);
            ui->contextWorkspace.railVisible =
                contextWorkspace.value(
                    QStringLiteral("railVisible")).toBool(true);
            if (contextVersion >= ContextWorkspaceState::kProviderStateVersion) {
                ui->contextWorkspace.providerStates =
                    providerStatesFromJson(
                        contextWorkspace.value(
                            QStringLiteral("providerStates"))
                            .toObject());
            }
            if (contextVersion >= ContextWorkspaceState::kFloatingGeometryVersion) {
                auto& state = ui->contextWorkspace;
                state.floatingX = contextWorkspace.value(QStringLiteral("floatingX")).toInt();
                state.floatingY = contextWorkspace.value(QStringLiteral("floatingY")).toInt();
                state.floatingWidth = ContextWorkspaceState::boundedPeekWidth(
                    contextWorkspace.value(QStringLiteral("floatingWidth")).toInt(ContextWorkspaceState::kDefaultPeekWidth));
                state.floatingHeight = ContextWorkspaceState::boundedPeekHeight(
                    contextWorkspace.value(QStringLiteral("floatingHeight")).toInt(ContextWorkspaceState::kDefaultPeekHeight));
                state.floatingScreenName = contextWorkspace.value(QStringLiteral("floatingScreenName")).toString();
                state.floatingGeometryValid = contextWorkspace.value(QStringLiteral("floatingGeometryValid")).toBool(false);
            }
            if (contextVersion >= ContextWorkspaceState::kFloatingInstancesVersion) {
                ui->contextWorkspace.floatingInstances = floatingInstancesFromJson(contextWorkspace.value("floatingInstances").toArray());
                ui->contextWorkspace.floatingCollapsed = contextWorkspace.value("floatingCollapsed").toBool();
                const auto layouts = contextWorkspace.value("documentFloatingLayouts").toObject();
                for (auto it = layouts.begin(); it != layouts.end(); ++it)
                    ui->contextWorkspace.documentFloatingLayouts.insert(it.key(), floatingInstancesFromJson(it.value().toArray()));
                for (const auto& entry : contextWorkspace.value("documentFloatingOrder").toArray())
                    ui->contextWorkspace.documentFloatingOrder.append(entry.toString());
            }
            ui->contextWorkspace.valid =
                contextWorkspace.value(
                    QStringLiteral("valid")).toBool(true);
        }
    }
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
    const int version = object.value(
        QStringLiteral("version")).toInt();
    if (!document.isObject()
        || object.value(
               QStringLiteral("schema"))
                   .toString()
               != QString::fromLatin1(
                   kLocalSchema)
        || version < 2
        || version > kVersion) {
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
