#include "contextresource.h"
#include "workspaceconfigurationservice.h"
#include "workspacesessionstateservice.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>

#include <iostream>
#include <limits>

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

QString cleanPath(const QString& path)
{
    return QDir::cleanPath(
        QDir::fromNativeSeparators(
            QFileInfo(path).absoluteFilePath()));
}

bool writeFile(const QString& path, const QByteArray& bytes)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly
                   | QIODevice::Truncate)) {
        return false;
    }
    return file.write(bytes) == bytes.size();
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

QByteArray fileDigest(const QString& path)
{
    return QCryptographicHash::hash(
        readFile(path), QCryptographicHash::Sha256);
}

QJsonObject pathObject(const QString& path,
                       bool relative = true)
{
    return {{QStringLiteral("path"), path},
            {QStringLiteral("relative"), relative}};
}
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    QTemporaryDir temp;
    check(temp.isValid(), "temporary root is valid");
    if (!temp.isValid())
        return 1;

    const QString workspaceA =
        QDir(temp.path()).absoluteFilePath(
            QStringLiteral("workspace-a"));
    const QString workspaceB =
        QDir(temp.path()).absoluteFilePath(
            QStringLiteral("workspace-b"));
    const QString shared =
        QDir(temp.path()).absoluteFilePath(
            QStringLiteral("shared"));
    const QString localStore =
        QDir(temp.path()).absoluteFilePath(
            QStringLiteral("local/workspace-sessions.ini"));
    const QStringList directories = {
        QDir(workspaceA).absoluteFilePath(
            QStringLiteral("rtl")),
        QDir(workspaceA).absoluteFilePath(
            QStringLiteral("include")),
        QDir(workspaceA).absoluteFilePath(
            QStringLiteral("generated")),
        QDir(workspaceB).absoluteFilePath(
            QStringLiteral("rtl")),
        QDir(workspaceB).absoluteFilePath(
            QStringLiteral("include")),
        QDir(workspaceB).absoluteFilePath(
            QStringLiteral("generated")),
        shared,
    };
    bool directoriesCreated = true;
    for (const QString& directory : directories)
        directoriesCreated =
            QDir().mkpath(directory)
            && directoriesCreated;
    check(directoriesCreated,
          "fixture directories are created");

    const QString topA =
        QDir(workspaceA).absoluteFilePath(
            QStringLiteral("rtl/top.sv"));
    const QString helperA =
        QDir(workspaceA).absoluteFilePath(
            QStringLiteral("rtl/helper.svh"));
    const QString topB =
        QDir(workspaceB).absoluteFilePath(
            QStringLiteral("rtl/top.sv"));
    const QString helperB =
        QDir(workspaceB).absoluteFilePath(
            QStringLiteral("rtl/helper.svh"));
    check(writeFile(topA,
                    "module portable_top; endmodule\n")
              && writeFile(helperA,
                           "`define PORTABLE\n")
              && writeFile(topB,
                           "module portable_top; endmodule\n")
              && writeFile(helperB,
                           "`define PORTABLE\n"),
          "fixture RTL files are created");

    WorkspaceConfigurationService projectService;
    WorkspaceConfiguration configuration =
        projectService.defaultConfiguration(workspaceA);
    configuration.includeDirs = {
        workspaceA,
        QDir(workspaceA).absoluteFilePath(
            QStringLiteral("include")),
        shared,
    };
    configuration.ignoredDirs = {
        QDir(workspaceA).absoluteFilePath(
            QStringLiteral("generated")),
    };
    configuration.fileExtensions = {
        QStringLiteral(".sv"),
        QStringLiteral(".svh"),
    };
    configuration.defines.insert(
        QStringLiteral("WIDTH"),
        QStringLiteral("32"));
    configuration.defines.insert(
        QStringLiteral("FEATURE"),
        QString());
    configuration.topModule =
        QStringLiteral("portable_top");
    configuration.virtualSourceGroups = {
        WorkspaceVirtualSourceGroup{
            QStringLiteral(" RTL "),
            {topA, topA,
             QDir(shared).absoluteFilePath(
                 QStringLiteral("external.sv"))}},
        WorkspaceVirtualSourceGroup{
            QStringLiteral("Headers"),
            {helperA}},
        WorkspaceVirtualSourceGroup{
            QStringLiteral("rtl"),
            {helperA}},
        WorkspaceVirtualSourceGroup{
            QStringLiteral("   "),
            {topA}},
    };

    check(projectService.save(configuration),
          "portable project configuration saves");
    const QString projectA =
        WorkspaceConfigurationService::projectFilePath(
            workspaceA);
    const QByteArray projectBytes = readFile(projectA);
    check(QFileInfo(projectA).isFile()
              && projectBytes.contains(
                  "ZeroSlack.ProjectConfiguration")
              && !projectBytes.contains(
                  cleanPath(workspaceA).toUtf8())
              && !projectBytes.contains(
                  "\"workspaceRoot\"")
              && !projectBytes.contains("\"tabs\"")
              && !projectBytes.contains(
                  "\"mainWindowGeometry\"")
              && !projectBytes.contains(
                  "\"workspaceScan\""),
          "project.json contains only portable semantics");
    const QJsonObject projectObject =
        QJsonDocument::fromJson(projectBytes).object();
    const QJsonArray includeDirs =
        projectObject.value(
            QStringLiteral("includeDirs")).toArray();
    bool allProjectPathsRelative = true;
    for (const QJsonValue& value : includeDirs) {
        allProjectPathsRelative =
            !QFileInfo(value.toString()).isAbsolute()
            && allProjectPathsRelative;
    }
    for (const QJsonValue& value :
         projectObject.value(
             QStringLiteral("ignoredDirs")).toArray()) {
        allProjectPathsRelative =
            !QFileInfo(value.toString()).isAbsolute()
            && allProjectPathsRelative;
    }
    const QJsonArray sourceGroups =
        projectObject.value(
            QStringLiteral(
                "virtualSourceGroups"))
            .toArray();
    bool allSourceGroupPathsRelative = true;
    for (const QJsonValue& groupValue :
         sourceGroups) {
        for (const QJsonValue& fileValue :
             groupValue.toObject()
                 .value(QStringLiteral("files"))
                 .toArray()) {
            allSourceGroupPathsRelative =
                !QFileInfo(
                     fileValue.toString())
                     .isAbsolute()
                && allSourceGroupPathsRelative;
        }
    }
    check(allProjectPathsRelative
              && allSourceGroupPathsRelative
              && includeDirs.contains(
                  QStringLiteral("."))
              && includeDirs.contains(
                  QStringLiteral("include"))
              && includeDirs.contains(
                  QStringLiteral("../shared")),
          "every project path is stored relatively");
    check(sourceGroups.size() == 2
              && sourceGroups.at(0)
                     .toObject()
                     .value(QStringLiteral("name"))
                     .toString()
                     == QStringLiteral("RTL")
              && sourceGroups.at(0)
                     .toObject()
                     .value(QStringLiteral("files"))
                     .toArray()
                     == QJsonArray{
                         QStringLiteral("rtl/top.sv")}
              && sourceGroups.at(1)
                     .toObject()
                     .value(QStringLiteral("files"))
                     .toArray()
                     == QJsonArray{
                         QStringLiteral(
                             "rtl/helper.svh")},
          "virtual source groups normalize names, paths, duplicates, and workspace scope");

    const QString projectB =
        WorkspaceConfigurationService::projectFilePath(
            workspaceB);
    check(QDir().mkpath(
              QFileInfo(projectB).absolutePath())
              && QFile::copy(projectA, projectB),
          "portable project file moves with workspace");
    const WorkspaceConfigurationLoadResult movedLoad =
        projectService.loadWithResult(workspaceB);
    check(movedLoad.loaded
              && movedLoad.source
                     == WorkspaceConfigurationSource::ProjectFile
              && movedLoad.configuration.workspaceRoot
                     == cleanPath(workspaceB)
              && movedLoad.configuration.includeDirs
                     == QStringList{
                         cleanPath(workspaceB),
                         cleanPath(
                             QDir(workspaceB)
                                 .absoluteFilePath(
                                     QStringLiteral(
                                         "include"))),
                         cleanPath(shared)}
              && movedLoad.configuration.ignoredDirs
                     == QStringList{
                         cleanPath(
                             QDir(workspaceB)
                                 .absoluteFilePath(
                                     QStringLiteral(
                                         "generated")))}
              && movedLoad.configuration.topModule
                     == QStringLiteral("portable_top")
              && movedLoad.configuration
                     .virtualSourceGroups
                     == QList<
                         WorkspaceVirtualSourceGroup>{
                         WorkspaceVirtualSourceGroup{
                             QStringLiteral("RTL"),
                             {topB}},
                         WorkspaceVirtualSourceGroup{
                             QStringLiteral("Headers"),
                             {helperB}}},
          "moved workspace rebuilds absolute config from relatives");

    WorkspaceSessionStateService sessionService(localStore);
    WorkspaceSessionState sessionA;
    sessionA.workspaceRoot = workspaceA;
    sessionA.tabs = {
        WorkspaceSessionTabState{
            topA, 7, 9, 23, 41, true},
        WorkspaceSessionTabState{
            helperA, 2, 3, 4, 6, false},
    };
    sessionA.tabs[0].viewId =
        QStringLiteral("view-top-a");
    sessionA.tabs[0].groupIndex = 1;
    sessionA.tabs[0].tabIndex = 2;
    sessionA.tabs[0].locked = true;
    sessionA.ui.mainWindowGeometry =
        QByteArrayLiteral("geometry-a");
    sessionA.ui.mainWindowState =
        QByteArrayLiteral("dock-state-a");
    sessionA.ui.navigationFilesQuery =
        QStringLiteral("top");
    sessionA.ui.navigationDesignQuery =
        QStringLiteral("u_stage");
    sessionA.ui.tabGroupingMode =
        QStringLiteral("module");
    sessionA.ui.panelLayout.bottomPanelOrder = {
        QStringLiteral("problems"),
        QStringLiteral("scopedSearch"),
        QStringLiteral("activity"),
        QStringLiteral("rtlHighRiskEdit"),
        QStringLiteral("connections"),
        QStringLiteral("foldShelf"),
    };
    sessionA.ui.panelLayout.activeBottomPanel =
        QStringLiteral("problems");
    sessionA.ui.panelLayout.lastBottomPanel =
        QStringLiteral("connections");
    sessionA.ui.panelLayout.bottomPanelHeights = {
        {QStringLiteral("problems"), 312},
        {QStringLiteral("connections"), 344},
    };
    sessionA.ui.panelLayout.bottomPanelViewStates.insert(
        QStringLiteral("connections"),
        QVariantMap{
            {QStringLiteral("tabs"),
             QVariantMap{
                 {QStringLiteral("connectionsWorkflowTabs"), 1}}},
            {QStringLiteral("scrolls"),
             QVariantMap{
                 {QStringLiteral("connectionPreview"),
                  QVariantMap{
                      {QStringLiteral("vertical"), 37},
                      {QStringLiteral("horizontal"), 4}}}}}});
    sessionA.ui.panelLayout.expandedBottomHeight = 312;
    sessionA.ui.panelLayout.bottomCollapsed = true;
    sessionA.ui.panelLayout.navigationVisible = false;
    sessionA.ui.panelLayout.valid = true;
    ContextResource pinnedContext;
    pinnedContext.providerId = QStringLiteral("temporaryEditor");
    pinnedContext.resourceId = QStringLiteral("primary");
    pinnedContext.uri = QUrl(
        QStringLiteral("zeroslack://temporary-editor/primary"));
    pinnedContext.title = QStringLiteral("top.sv : 7");
    pinnedContext.state.insert(
        QStringLiteral("location"),
        QVariantMap{{QStringLiteral("workspaceRelativePath"),
                     QStringLiteral("top.sv")},
                    {QStringLiteral("line"), 7},
                    {QStringLiteral("column"), 3}});
    sessionA.ui.contextWorkspace.pinnedResources = {
        pinnedContext.toVariantMap(),
    };
    sessionA.ui.contextWorkspace.activePinnedResourceKey =
        pinnedContext.stableKey();
    sessionA.ui.contextWorkspace.peekWidth = 604;
    sessionA.ui.contextWorkspace.peekHeight = 477;
    sessionA.ui.contextWorkspace.dockWidth = 588;
    sessionA.ui.contextWorkspace.dockVisible = true;
    sessionA.ui.contextWorkspace.railVisible = true;
    sessionA.ui.contextWorkspace.valid = true;
    sessionA.scannedFiles = {topA, helperA};
    sessionA.scanComplete = true;
    const QByteArray projectDigestBeforeSession =
        fileDigest(projectA);
    const WorkspaceSessionSaveResult saveA =
        sessionService.save(sessionA);
    check(saveA.saved
              && saveA.storagePath
                     == cleanPath(localStore)
              && QFileInfo(localStore).isFile()
              && !QFileInfo(
                      QDir(workspaceA)
                          .absoluteFilePath(
                              QStringLiteral(".zs")))
                      .exists()
              && fileDigest(projectA)
                     == projectDigestBeforeSession,
          "session saves only to local storage");
    check(sessionService.sessionExists(workspaceA)
              && sessionService.localStoragePath()
                     == cleanPath(localStore)
              && !sessionService.localStoragePath()
                      .startsWith(
                          cleanPath(workspaceA)
                              + QLatin1Char('/')),
          "local sessions are partitioned outside workspace");

    const WorkspaceSessionRestoreResult loadA =
        sessionService.load(workspaceA);
    check(loadA.loaded
              && loadA.state.tabs.size() == 2
              && loadA.state.tabs.first().filePath
                     == cleanPath(topA)
              && loadA.state.tabs.first().cursorLine
                     == 7
              && loadA.state.tabs.first().verticalScrollValue == 23
              && loadA.state.tabs.first().horizontalScrollValue == 41
              && loadA.state.tabs.first().viewId
                     == QStringLiteral("view-top-a")
              && loadA.state.tabs.first().groupIndex == 1
              && loadA.state.tabs.first().tabIndex == 2
              && loadA.state.tabs.first().locked
              && loadA.state.ui.mainWindowState
                     == QByteArrayLiteral(
                         "dock-state-a")
              && loadA.state.ui.navigationFilesQuery
                     == QStringLiteral("top")
              && loadA.state.ui.navigationDesignQuery
                     == QStringLiteral("u_stage")
              && loadA.state.ui.tabGroupingMode
                     == QStringLiteral("module")
              && loadA.state.ui.panelLayout.valid
              && loadA.state.ui.panelLayout.bottomPanelOrder
                     == sessionA.ui.panelLayout.bottomPanelOrder
              && loadA.state.ui.panelLayout.activeBottomPanel
                     == QStringLiteral("problems")
              && loadA.state.ui.panelLayout.lastBottomPanel
                     == QStringLiteral("connections")
              && loadA.state.ui.panelLayout.bottomPanelHeights
                     == sessionA.ui.panelLayout.bottomPanelHeights
              && loadA.state.ui.panelLayout.bottomPanelViewStates
                     == sessionA.ui.panelLayout.bottomPanelViewStates
              && loadA.state.ui.panelLayout.expandedBottomHeight == 312
              && loadA.state.ui.panelLayout.bottomCollapsed
              && !loadA.state.ui.panelLayout.navigationVisible
              && loadA.state.ui.contextWorkspace.valid
              && loadA.state.ui.contextWorkspace.pinnedResources
                     == sessionA.ui.contextWorkspace.pinnedResources
              && loadA.state.ui.contextWorkspace.activePinnedResourceKey
                     == pinnedContext.stableKey()
              && loadA.state.ui.contextWorkspace.peekWidth == 604
              && loadA.state.ui.contextWorkspace.peekHeight == 477
              && loadA.state.ui.contextWorkspace.dockWidth == 588
              && loadA.state.ui.contextWorkspace.dockVisible
              && loadA.state.ui.contextWorkspace.railVisible
              && loadA.state.scannedFiles
                     == QStringList{
                         cleanPath(helperA),
                         cleanPath(topA)}
              && loadA.state.scanComplete,
          "local session restores tabs, context workspace, UI, and scan cache");

    QSettings rawSessionSettings(localStore, QSettings::IniFormat);
    QString workspaceAStateKey;
    for (const QString& key : rawSessionSettings.allKeys()) {
        if (key.endsWith(QStringLiteral("/state"))) {
            workspaceAStateKey = key;
            break;
        }
    }
    const QByteArray savedSessionBytes =
        rawSessionSettings.value(workspaceAStateKey).toByteArray();
    QJsonDocument editableSession =
        QJsonDocument::fromJson(savedSessionBytes);
    QJsonObject editableRoot = editableSession.object();
    QJsonObject editableUi =
        editableRoot.value(QStringLiteral("ui")).toObject();
    QJsonObject editableContext =
        editableUi.value(QStringLiteral("contextWorkspace")).toObject();
    check(!workspaceAStateKey.isEmpty()
              && editableSession.isObject()
              && editableContext.value(QStringLiteral("version")).toInt()
                     == ContextWorkspaceState::kVersion
              && editableContext.value(QStringLiteral("peekHeight")).toInt()
                     == 477,
          "saved Context Workspace state uses v2 and contains Peek height");

    editableContext.insert(
        QStringLiteral("version"),
        ContextWorkspaceState::kLegacyVersion);
    editableContext.remove(QStringLiteral("peekHeight"));
    editableUi.insert(QStringLiteral("contextWorkspace"), editableContext);
    editableRoot.insert(QStringLiteral("ui"), editableUi);
    rawSessionSettings.setValue(
        workspaceAStateKey,
        QJsonDocument(editableRoot).toJson(QJsonDocument::Compact));
    rawSessionSettings.sync();
    const WorkspaceSessionRestoreResult legacyContextLoad =
        sessionService.load(workspaceA);
    check(legacyContextLoad.loaded
              && legacyContextLoad.state.ui.contextWorkspace.valid
              && legacyContextLoad.state.ui.contextWorkspace.peekWidth == 604
              && legacyContextLoad.state.ui.contextWorkspace.peekHeight
                     == ContextWorkspaceState::kDefaultPeekHeight
              && legacyContextLoad.state.ui.contextWorkspace.dockWidth == 588,
          "v1 Context Workspace state restores with a version-compatible height default");

    editableContext.insert(
        QStringLiteral("version"),
        ContextWorkspaceState::kVersion);
    editableContext.insert(QStringLiteral("peekWidth"), -4000);
    editableContext.insert(
        QStringLiteral("peekHeight"),
        std::numeric_limits<int>::max());
    editableContext.insert(
        QStringLiteral("dockWidth"),
        std::numeric_limits<int>::max());
    editableUi.insert(QStringLiteral("contextWorkspace"), editableContext);
    editableRoot.insert(QStringLiteral("ui"), editableUi);
    rawSessionSettings.setValue(
        workspaceAStateKey,
        QJsonDocument(editableRoot).toJson(QJsonDocument::Compact));
    rawSessionSettings.sync();
    const WorkspaceSessionRestoreResult malformedContextLoad =
        sessionService.load(workspaceA);
    check(malformedContextLoad.loaded
              && malformedContextLoad.state.ui.contextWorkspace.peekWidth
                     == ContextWorkspaceState::kMinimumPeekWidth
              && malformedContextLoad.state.ui.contextWorkspace.peekHeight
                     == ContextWorkspaceState::
                            kMaximumStoredPeekHeight
              && malformedContextLoad.state.ui.contextWorkspace.dockWidth
                     == ContextWorkspaceState::
                            kMaximumStoredDockWidth,
          "malformed persisted Context dimensions are clamped during JSON restore");
    rawSessionSettings.setValue(workspaceAStateKey, savedSessionBytes);
    rawSessionSettings.sync();

    QJsonDocument legacyPanelDocument =
        QJsonDocument::fromJson(savedSessionBytes);
    QJsonObject legacyPanelRoot = legacyPanelDocument.object();
    QJsonObject legacyPanelUi =
        legacyPanelRoot.value(QStringLiteral("ui")).toObject();
    QJsonObject legacyPanel =
        legacyPanelUi.value(QStringLiteral("panelLayout")).toObject();
    legacyPanel.remove(QStringLiteral("version"));
    legacyPanel.remove(QStringLiteral("last"));
    legacyPanel.remove(QStringLiteral("heights"));
    legacyPanel.remove(QStringLiteral("viewStates"));
    legacyPanel.insert(
        QStringLiteral("active"),
        QStringLiteral("instancePairConnection"));
    legacyPanel.insert(QStringLiteral("expandedHeight"), 245);
    legacyPanelUi.insert(QStringLiteral("panelLayout"), legacyPanel);
    legacyPanelRoot.insert(QStringLiteral("ui"), legacyPanelUi);
    rawSessionSettings.setValue(
        workspaceAStateKey,
        QJsonDocument(legacyPanelRoot).toJson(QJsonDocument::Compact));
    rawSessionSettings.sync();
    const WorkspaceSessionRestoreResult legacyPanelLoad =
        sessionService.load(workspaceA);
    check(legacyPanelLoad.loaded
              && legacyPanelLoad.state.ui.panelLayout.version == 1
              && legacyPanelLoad.state.ui.panelLayout.activeBottomPanel
                     == QStringLiteral("instancePairConnection")
              && legacyPanelLoad.state.ui.panelLayout.lastBottomPanel
                     == QStringLiteral("instancePairConnection")
              && legacyPanelLoad.state.ui.panelLayout.bottomPanelHeights
                     .isEmpty()
              && legacyPanelLoad.state.ui.panelLayout.expandedBottomHeight
                     == 245,
          "legacy panel sessions preserve enough state for drawer migration");
    rawSessionSettings.setValue(workspaceAStateKey, savedSessionBytes);
    rawSessionSettings.sync();

    WorkspaceSessionState sessionB;
    sessionB.workspaceRoot = workspaceB;
    sessionB.tabs = {
        WorkspaceSessionTabState{
            topB, 3, 4, 5, 8, true},
    };
    sessionB.ui.mainWindowGeometry =
        QByteArrayLiteral("geometry-b");
    check(sessionService.save(sessionB).saved
              && sessionService.sessionExists(
                  workspaceA)
              && sessionService.sessionExists(
                  workspaceB)
              && WorkspaceSessionStateService::
                     workspaceIdentity(workspaceA)
                     != WorkspaceSessionStateService::
                            workspaceIdentity(
                                workspaceB),
          "local sessions use independent workspace identities");
    check(sessionService.load(workspaceA)
                  .state.tabs.first().cursorLine
              == 7
              && sessionService.load(workspaceB)
                     .state.tabs.first().cursorLine
                     == 3,
          "workspace session partitions do not leak");
    check(sessionService.clear(workspaceA)
              && !sessionService.sessionExists(
                  workspaceA)
              && sessionService.sessionExists(
                  workspaceB)
              && QFileInfo(projectA).isFile()
              && QFileInfo(projectB).isFile(),
          "session clean removes only one local partition");

    const QByteArray localBytes = readFile(localStore);
    check(!localBytes.contains(
              "workspaceConfiguration")
              && !localBytes.contains("topModule")
              && !localBytes.contains("includeDirs")
              && !localBytes.contains("\"defines\""),
          "local session does not duplicate project semantics");

    const QString legacyRoot =
        QDir(temp.path()).absoluteFilePath(
            QStringLiteral("legacy-moved"));
    const QString legacyTop =
        QDir(legacyRoot).absoluteFilePath(
            QStringLiteral("rtl/legacy_top.sv"));
    const QString legacyInclude =
        QDir(legacyRoot).absoluteFilePath(
            QStringLiteral("include"));
    const QString legacyIgnored =
        QDir(legacyRoot).absoluteFilePath(
            QStringLiteral("generated"));
    check(QDir().mkpath(legacyInclude)
              && QDir().mkpath(legacyIgnored)
              && writeFile(
                  legacyTop,
                  "module legacy_top; endmodule\n"),
          "legacy fixture is created");

    QJsonObject legacyConfiguration;
    legacyConfiguration.insert(
        QStringLiteral("includeDirs"),
        QJsonArray{
            pathObject(QStringLiteral(".")),
            pathObject(
                QStringLiteral("include"))});
    legacyConfiguration.insert(
        QStringLiteral("ignoredDirs"),
        QJsonArray{
            pathObject(
                QStringLiteral("generated"))});
    legacyConfiguration.insert(
        QStringLiteral("fileExtensions"),
        QJsonArray{QStringLiteral(".sv")});
    legacyConfiguration.insert(
        QStringLiteral("topModule"),
        QStringLiteral("legacy_top"));
    legacyConfiguration.insert(
        QStringLiteral("defines"),
        QJsonObject{
            {QStringLiteral("LEGACY"),
             QStringLiteral("1")}});
    QJsonObject legacyObject;
    legacyObject.insert(
        QStringLiteral("schema"),
        QStringLiteral(
            "ZeroSlack.WorkspaceSessionState"));
    legacyObject.insert(
        QStringLiteral("version"), 1);
    legacyObject.insert(
        QStringLiteral("savedAt"),
        QStringLiteral("2026-07-01T00:00:00Z"));
    legacyObject.insert(
        QStringLiteral("originalRoot"),
        QStringLiteral("D:/old/location"));
    legacyObject.insert(
        QStringLiteral("workspaceId"),
        QStringLiteral("legacy-id"));
    legacyObject.insert(
        QStringLiteral("workspaceConfiguration"),
        legacyConfiguration);
    legacyObject.insert(
        QStringLiteral("tabs"),
        QJsonArray{
            QJsonObject{
                {QStringLiteral("path"),
                 QStringLiteral(
                     "rtl/legacy_top.sv")},
                {QStringLiteral("cursorLine"), 11},
                {QStringLiteral("cursorColumn"), 6},
                {QStringLiteral("verticalScroll"), 8},
                {QStringLiteral("active"), true}}});
    legacyObject.insert(
        QStringLiteral("ui"),
        QJsonObject{
            {QStringLiteral(
                 "mainWindowGeometry"),
             QString::fromLatin1(
                 QByteArrayLiteral(
                     "legacy-geometry")
                     .toBase64())},
            {QStringLiteral(
                 "mainWindowState"),
             QString::fromLatin1(
                 QByteArrayLiteral(
                     "legacy-state")
                     .toBase64())}});
    legacyObject.insert(
        QStringLiteral("workspaceScan"),
        QJsonObject{
            {QStringLiteral("files"),
             QJsonArray{
                 QStringLiteral(
                     "rtl/legacy_top.sv")}},
            {QStringLiteral("scanComplete"),
             true}});
    const QString legacyPath =
        WorkspaceSessionStateService::
            legacySessionFilePath(legacyRoot);
    check(writeFile(
              legacyPath,
              QJsonDocument(legacyObject)
                  .toJson(QJsonDocument::Indented)),
          "legacy .zs fixture is written");
    const QByteArray legacyDigest =
        fileDigest(legacyPath);
    const QDateTime legacyTimestamp =
        QFileInfo(legacyPath).lastModified();

    const WorkspaceConfigurationLoadResult legacyConfigLoad =
        projectService.loadWithResult(legacyRoot);
    check(legacyConfigLoad.loaded
              && legacyConfigLoad.source
                     == WorkspaceConfigurationSource::
                            LegacySession
              && legacyConfigLoad.configuration
                     .workspaceRoot
                     == cleanPath(legacyRoot)
              && legacyConfigLoad.configuration
                     .includeDirs
                     == QStringList{
                         cleanPath(legacyRoot),
                         cleanPath(legacyInclude)}
              && legacyConfigLoad.configuration
                     .ignoredDirs
                     == QStringList{
                         cleanPath(legacyIgnored)}
              && legacyConfigLoad.configuration
                     .topModule
                     == QStringLiteral("legacy_top")
              && !QFileInfo(
                      WorkspaceConfigurationService::
                          projectFilePath(legacyRoot))
                      .exists(),
          "legacy project semantics import in memory");

    const WorkspaceLegacyImportResult legacySession =
        sessionService.loadLegacy(legacyRoot);
    check(legacySession.loaded
              && legacySession.state.tabs.size() == 1
              && legacySession.state.tabs.first()
                     .filePath
                     == cleanPath(legacyTop)
              && legacySession.state.tabs.first()
                     .cursorLine
                     == 11
              && legacySession.state.ui
                     .mainWindowGeometry
                     == QByteArrayLiteral(
                         "legacy-geometry")
              && legacySession.state.scannedFiles
                     == QStringList{
                         cleanPath(legacyTop)}
              && legacySession.state.scanComplete,
          "legacy local session imports relative state");

    check(sessionService.save(
              legacySession.state).saved
              && projectService.save(
                  legacyConfigLoad.configuration)
              && fileDigest(legacyPath)
                     == legacyDigest
              && QFileInfo(legacyPath)
                     .lastModified()
                     == legacyTimestamp,
          "legacy import never modifies or deletes .zs");
    const WorkspaceConfigurationLoadResult migratedLoad =
        projectService.loadWithResult(legacyRoot);
    check(migratedLoad.loaded
              && migratedLoad.source
                     == WorkspaceConfigurationSource::
                            ProjectFile
              && QFileInfo(legacyPath).isFile()
              && sessionService.sessionExists(
                  legacyRoot),
          "legacy import creates separated project and local state");

    bool realLegacyFixturesReadOnly = argc == 3;
    bool realLegacyFixturesImported = argc == 3;
    for (int argumentIndex = 1;
         argumentIndex < argc;
         ++argumentIndex) {
        const QString fixtureRoot =
            cleanPath(QString::fromLocal8Bit(
                argv[argumentIndex]));
        const QString fixturePath =
            WorkspaceSessionStateService::
                legacySessionFilePath(fixtureRoot);
        const QFileInfo beforeInfo(fixturePath);
        const QByteArray beforeDigest =
            fileDigest(fixturePath);
        const qint64 beforeSize = beforeInfo.size();
        const QDateTime beforeTimestamp =
            beforeInfo.lastModified();

        const WorkspaceConfigurationLoadResult
            fixtureConfiguration =
                projectService.loadWithResult(
                    fixtureRoot);
        const WorkspaceLegacyImportResult
            fixtureSession =
                sessionService.loadLegacy(
                    fixtureRoot);

        const QFileInfo afterInfo(fixturePath);
        realLegacyFixturesImported =
            realLegacyFixturesImported
            && beforeInfo.isFile()
            && fixtureConfiguration.loaded
            && fixtureConfiguration.source
                   == WorkspaceConfigurationSource::
                          LegacySession
            && fixtureSession.loaded;
        realLegacyFixturesReadOnly =
            realLegacyFixturesReadOnly
            && afterInfo.isFile()
            && afterInfo.size() == beforeSize
            && afterInfo.lastModified()
                   == beforeTimestamp
            && fileDigest(fixturePath)
                   == beforeDigest;
    }
    check(realLegacyFixturesImported,
          "real new and huge_prj legacy fixtures import");
    check(realLegacyFixturesReadOnly,
          "real legacy fixtures remain byte and timestamp identical");

    std::cout << (checks - failures) << "/"
              << checks
              << " workspace persistence checks passed\n";
    return failures == 0 ? 0 : 1;
}
