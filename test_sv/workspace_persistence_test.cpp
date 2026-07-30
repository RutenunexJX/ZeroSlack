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
    check(allProjectPathsRelative
              && includeDirs.contains(
                  QStringLiteral("."))
              && includeDirs.contains(
                  QStringLiteral("include"))
              && includeDirs.contains(
                  QStringLiteral("../shared")),
          "every project path is stored relatively");

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
                     == QStringLiteral("portable_top"),
          "moved workspace rebuilds absolute config from relatives");

    WorkspaceSessionStateService sessionService(localStore);
    WorkspaceSessionState sessionA;
    sessionA.workspaceRoot = workspaceA;
    sessionA.tabs = {
        WorkspaceSessionTabState{
            topA, 7, 9, 23, true},
        WorkspaceSessionTabState{
            helperA, 2, 3, 4, false},
    };
    sessionA.ui.mainWindowGeometry =
        QByteArrayLiteral("geometry-a");
    sessionA.ui.mainWindowState =
        QByteArrayLiteral("dock-state-a");
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
              && loadA.state.ui.mainWindowState
                     == QByteArrayLiteral(
                         "dock-state-a")
              && loadA.state.scannedFiles
                     == QStringList{
                         cleanPath(helperA),
                         cleanPath(topA)}
              && loadA.state.scanComplete,
          "local session restores tabs UI and scan cache");

    WorkspaceSessionState sessionB;
    sessionB.workspaceRoot = workspaceB;
    sessionB.tabs = {
        WorkspaceSessionTabState{
            topB, 3, 4, 5, true},
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
