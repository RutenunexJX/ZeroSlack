#include "../../src/integrations/simdock/simdockstate.h"
#include "../../src/integrations/simdock/simdockcontextview.h"
#include "../../src/simulation/simdock/core/preparation.h"
#include "../../src/simulation/simdock/core/analyzer.h"
#include "../../src/simulation/simdock/ui/workbench.h"
#include "../../src/simulation/simdock/ui/stimulusdialog.h"
#include "mainwindow.h"
#include "tabmanager.h"
#include "usertemplateservice.h"
#include "workspacemanager.h"
#include "workspacesessioncoordinator.h"
#include "workspacesessionstateservice.h"
#include "contextworkspacecontroller.h"
#include "applicationthememanager.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QDirIterator>
#include <QDockWidget>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QProcess>
#include <QSettings>
#include <QSplitter>
#include <QToolButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

namespace {
int migrationWrites = 0;
bool rejectMigrationMarker = true;
QString fixtures() { return QString::fromUtf8(SIMDOCK_LEGACY_FIXTURES); }
QByteArray read(const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); }
bool write(const QString& path, const QByteArray& bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QJsonObject object(const QString& path) { return QJsonDocument::fromJson(read(path)).object(); }
QString sha(const QByteArray& bytes) { return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()); }
bool copyTree(const QString& source, const QString& target) {
    QDirIterator files(source, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (files.hasNext()) {
        const auto file = files.next();
        if (!write(QDir(target).filePath(QDir(source).relativeFilePath(file)), read(file))) return false;
    }
    return true;
}
void isolate(const QString& profile) {
    QCoreApplication::setOrganizationName("ZeroSlackSimDockTest");
    QCoreApplication::setApplicationName("LegacyCompatibility");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile);
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, profile + "/system");
    qputenv("ZEROSLACK_SESSION_STORAGE_PATH", (profile + "/workspace-sessions.ini").toUtf8());
    qputenv("ZEROSLACK_EDITING_TIME_STORAGE_PATH", (profile + "/editing-time.json").toUtf8());
    qputenv("ZEROSLACK_SIMDOCK_LEGACY_SETTINGS_PATH", (profile + "/legacy.ini").toUtf8());
    UserTemplateService::getInstance()->setGlobalTemplateFilePath(profile + "/templates.json");
}
int sessionPhase(const QString& mode, const QString& workspace, const QString& profile) {
    isolate(profile);
    auto& theme = ApplicationThemeManager::instance();
    if (!theme.selectBackend(UiStyleBackend::Ela)) return 10;
    theme.setAnimationsEnabled(false); theme.applyToApplication();
    MainWindow window;
    window.tabManager->setCrashRecoveryService(std::make_unique<CrashRecoveryService>(profile + "/recovery"));
    window.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
    window.resize(1700, 900); window.show();
    auto* sessions = window.findChild<WorkspaceSessionCoordinator*>();
    auto* controller = window.findChild<ContextWorkspaceController*>();
    if (!sessions || !controller || !sessions->openWorkspace(workspace)) return 11;
    auto* host = window.findChild<SimDockContextView*>();
    if (!host || !host->isReady()) return 12;
    auto* panel = host->workbench();
    QTest::qWait(150);
    const auto original = object(fixtures() + "/host-0.31.27/save-report.json").value("liveWorkbenchStateTyped").toObject();
    if (host->saveState().value("projectId").toString() != original.value("projectId").toString()) return 13;
    if (host->saveState().value("workspace").toString() != workspace) return 14;
    if (host->saveState().value("preferences").toMap().value("waveform/theme") != "nord") return 15;
    if (host->saveState().value("legacyLayout").toMap().size() != 2) return 16;
    window.resizeDocks({controller->dockWidget()}, {1100}, Qt::Horizontal);
    QTest::qWait(150);
    auto* options = panel->findChild<QToolButton*>("waveOptionsToggle");
    if (!options || panel->findChild<QWidget*>("workbenchSection")) return 17;
    if (mode == "save") {
        options->setChecked(true);
        QTest::qWait(60);
        const auto state = host->saveState();
        if (!write(profile + "/expected-state.json", QJsonDocument(QJsonObject::fromVariantMap(state)).toJson())) return 18;
    } else {
        const auto expected = SimDockState::decode(object(profile + "/expected-state.json").toVariantMap());
        const auto actual = SimDockState::decode(host->saveState());
        for (const auto& key : {"layout/waveOptionsExpanded", "projectId", "preferences", "legacyLayout"}) {
            if (actual.value(key) != expected.value(key)) {
                qCritical() << key << actual.value(key) << expected.value(key);
                return 19;
            }
        }
    }
    if (!window.grab().save(profile + "/host-" + mode + ".png")) return 20;
    if (sessions->saveSessionResult(false).status != WorkspaceSessionSaveStatus::Saved) return 21;
    if (!window.close()) return 22;
    return 0;
}
}

class LegacyCompatibilityTest final : public QObject {
    Q_OBJECT
private slots:
    void originalBytesHaveRecordedProvenance() {
        const auto manifest = object(fixtures() + "/provenance.json");
        const auto files = manifest.value("files").toArray();
        QCOMPARE(files.size(), 20);
        for (const auto& value : files) {
            const auto entry = value.toObject();
            QCOMPARE(sha(read(fixtures() + '/' + entry.value("path").toString())), entry.value("sha256").toString());
        }
    }
    void productionProjectsAndGeneratedFiles_data() {
        QTest::addColumn<QString>("id"); QTest::addColumn<QString>("kind");
        for (const auto& value : object(fixtures() + "/capture-0.6.1.json").value("projects").toArray()) {
            const auto row = value.toObject(); const auto kind = row.value("kind").toString();
            QTest::newRow(qPrintable(kind)) << row.value("id").toString() << kind;
        }
    }
    void productionProjectsAndGeneratedFiles() {
        QFETCH(QString, id); QFETCH(QString, kind);
        QTemporaryDir workspace; QVERIFY(copyTree(fixtures() + "/simdock-0.6.1", workspace.path()));
        const auto projectFile = workspace.filePath(".simdock/projects/" + id + ".json");
        const auto originalBytes = read(projectFile);
        QStringList errors; const auto projects = simdock::loadProjects(workspace.path(), &errors);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
        const auto found = std::find_if(projects.cbegin(), projects.cend(), [&](const auto& p) { return p.id == id; });
        QVERIFY(found != projects.cend()); auto project = *found;
        QCOMPARE(read(projectFile), originalBytes);
        QCOMPARE(project.stimulus.value("scoreboard").toObject().value("kind").toString(), kind);
        QCOMPARE(project.durationNs, qint64(2000));
        QString error; simdock::SourceCache cache;
        const auto prepared = simdock::prepareStimulus(workspace.path(), project, {}, &cache, nullptr, &error);
        QVERIFY2(prepared, qPrintable(error));
        const auto generated = simdock::stimulusTestbench(prepared->module, prepared->semantics, project.stimulus, &error);
        QVERIFY2(!generated.isEmpty(), qPrintable(error));
        QCOMPARE(generated.toUtf8(), read(workspace.filePath(project.tbFile)));
        QCOMPARE(prepared->semantics.inputs.size(), 9);
        const auto signedPort = std::find_if(prepared->semantics.inputs.cbegin(), prepared->semantics.inputs.cend(), [](const auto& p) { return p.name == "signed_data"; });
        QVERIFY(signedPort != prepared->semantics.inputs.cend() && signedPort->isSigned);
        const auto enumPort = std::find_if(prepared->semantics.inputs.cbegin(), prepared->semantics.inputs.cend(), [](const auto& p) { return p.name == "mode"; });
        QVERIFY(enumPort != prepared->semantics.inputs.cend() && enumPort->enumValues.size() == 3);
        const auto before = simdock::projectJson(project);
        QVERIFY2(simdock::saveGeneratedStimulus(workspace.path(), project, project.stimulus, generated, &error), qPrintable(error));
        QCOMPARE(simdock::projectJson(project), before);
        const auto edited = generated.toUtf8() + "\n// User-owned edit must survive.\n";
        QVERIFY(write(workspace.filePath(project.tbFile), edited));
        const auto savedProject = read(projectFile);
        QVERIFY(!simdock::saveGeneratedStimulus(workspace.path(), project, project.stimulus, generated, &error));
        QVERIFY(error.contains("modified"));
        QCOMPARE(read(workspace.filePath(project.tbFile)), edited);
        QCOMPARE(read(projectFile), savedProject); QCOMPARE(simdock::projectJson(project), before);
    }
    void legacyCounterUsesCompatibleDefaults() {
        QTemporaryDir workspace; QVERIFY(copyTree(fixtures() + "/legacy-counter", workspace.path()));
        QStringList errors; const auto projects = simdock::loadProjects(workspace.path(), &errors);
        QVERIFY(errors.isEmpty()); QCOMPARE(projects.size(), 1);
        auto project = projects.first(); QCOMPARE(project.id, QString("c30ee298-ae96-4d6a-98bc-80630d1c5407"));
        QVERIFY(project.stimulus.isEmpty()); QCOMPARE(project.waveScope, QString("interface")); QVERIFY(project.waveSignals.isEmpty());
        const auto tb = read(workspace.filePath(project.tbFile)); QString error;
        QVERIFY(simdock::saveProject(workspace.path(), project, &error));
        QCOMPARE(simdock::projectJson(simdock::loadProjects(workspace.path()).first()), simdock::projectJson(project));
        QVERIFY(!simdock::writeNewTb(workspace.path(), project.tbFile, "replacement", &error));
        QCOMPARE(read(workspace.filePath(project.tbFile)), tb);
    }
    void settingsMigrationIsReadOnlyAndRetryable() {
        QTemporaryDir profile;
        QSettings legacy(profile.filePath("legacy.ini"), QSettings::IniFormat);
        const auto captured = object(fixtures() + "/legacy-settings.json");
        QCOMPARE(captured.value("access").toString(), QStringLiteral("read-only"));
        QCOMPARE(captured.value("entries").toArray().size(), 5);
        QVariantMap originalValues;
        for (const auto& item : captured.value("entries").toArray()) {
            const auto entry = item.toObject();
            const auto key = entry.value("key").toString().mid(1) + '/' + entry.value("name").toString();
            QVariant value = entry.value("value").toVariant();
            if (entry.value("kind") == "Binary") {
                const auto bytes = QByteArray::fromBase64(entry.value("value").toString().toLatin1());
                QString encoded;
                for (qsizetype i = 0; i + 1 < bytes.size(); i += 2)
                    encoded.append(QChar(quint8(bytes[i]) | (quint16(quint8(bytes[i + 1])) << 8)));
                if (encoded.endsWith(QChar::Null)) encoded.chop(1);
                QVERIFY(encoded.startsWith("@ByteArray(") && encoded.endsWith(')'));
                value = encoded.mid(11, encoded.size() - 12).toLatin1();
            }
            originalValues.insert(key, value);
            legacy.setValue(key, value);
        }
        // Additional preference absent from the captured registry exercises precedence.
        legacy.setValue("waveform/theme", "nord"); legacy.sync();
        const auto original = read(legacy.fileName());
        QSettings host(profile.filePath("host.ini"), QSettings::IniFormat);
        host.setValue("integrations/simdock/preferences/simulator/path", "host-simulator");
        host.setValue("appearance/theme", "Light"); host.sync(); QString error;
        QVERIFY2(SimDockState::migrateLegacy(legacy, host, &error), qPrintable(error));
        QCOMPARE(SimDockState::preferences(host).value("simulator/path"), QVariant("host-simulator"));
        QCOMPARE(SimDockState::preferences(host).value("waveform/theme"), QVariant("nord"));
        QCOMPARE(host.value("appearance/theme"), QVariant("Light"));
        QVERIFY(!host.contains("workspace/last") && !host.contains("window/geometry"));
        for (auto it = originalValues.cbegin(); it != originalValues.cend(); ++it)
            QCOMPARE(host.value("integrations/simdock/legacySnapshot/" + it.key()), it.value());
        QCOMPARE(read(legacy.fileName()), original);
        const auto first = read(host.fileName()); QVERIFY(SimDockState::migrateLegacy(legacy, host, &error));
        QCOMPARE(read(host.fileName()), first);
        const auto blocked = profile.filePath("blocked"); QVERIFY(write(blocked, "file"));
        { QSettings failure(blocked + "/host.ini", QSettings::IniFormat);
          QVERIFY(!SimDockState::migrateLegacy(legacy, failure, &error));
          QVERIFY(!failure.value("integrations/simdock/legacyMigration/v1").toBool()); }
        QVERIFY(QFile::remove(blocked)); QVERIFY(QDir().mkpath(blocked));
        QSettings retry(blocked + "/host.ini", QSettings::IniFormat);
        QVERIFY2(SimDockState::migrateLegacy(legacy, retry, &error), qPrintable(error));
        QVERIFY(retry.value("integrations/simdock/legacyMigration/v1").toBool());
        QCOMPARE(read(legacy.fileName()), original);
    }
    void oldLossIsPreservedAndNewBytesRoundTrip() {
        const auto report = object(fixtures() + "/host-0.31.27/save-report.json");
        auto state = report.value("liveWorkbenchStateTyped").toObject().toVariantMap();
        for (const auto& key : {"layout/columns", "layout/workbench"})
            state.insert(key, QByteArray::fromBase64(state.value(key).toMap().value("base64").toByteArray()));
        const auto persisted = QJsonDocument(QJsonObject::fromVariantMap(SimDockState::encode(state))).toJson();
        const auto restored = SimDockState::decode(QJsonDocument::fromJson(persisted).object().toVariantMap());
        QCOMPARE(restored, state);
        const auto wrapper = object(fixtures() + "/host-0.31.27/session-decoded.json");
        const auto old = wrapper.begin().value().toObject().value("ui").toObject().value("contextWorkspace").toObject()
            .value("pinnedResources").toArray().first().toObject().value("state").toObject().toVariantMap();
        const auto recovered = SimDockState::decode(old);
        QCOMPARE(recovered.value("legacyLayout").toMap().size(), 2);
        QVERIFY(!recovered.contains("layout/columns") && !recovered.contains("layout/workbench"));
        QCOMPARE(recovered.value("projectId"), old.value("projectId"));
        QCOMPARE(recovered.value("preferences"), old.value("preferences"));
        for (const auto& key : {"layout/columns", "layout/workbench"}) QCOMPARE(recovered.value("legacyLayout").toMap().value(key), old.value(key));
    }
    void failedMigrationMarkerIsNotReportedComplete() {
        const auto format = QSettings::registerFormat("simdock-migration-test",
            [](QIODevice& device, QSettings::SettingsMap& map) {
                const auto values = QJsonDocument::fromJson(device.readAll()).object().toVariantMap();
                for (auto it = values.cbegin(); it != values.cend(); ++it) map.insert(it.key(), it.value());
                return true;
            }, [](QIODevice& device, const QSettings::SettingsMap& map) {
                if (++migrationWrites > 1 && rejectMigrationMarker) return false;
                const auto bytes = QJsonDocument(QJsonObject::fromVariantMap(map)).toJson();
                return device.write(bytes) == bytes.size();
            });
        QTemporaryDir profile;
        QSettings legacy(profile.filePath("legacy.ini"), QSettings::IniFormat);
        legacy.setValue("simulator/path", "captured-simulator"); legacy.sync();
        const auto original = read(legacy.fileName());
        const auto path = profile.filePath("host.simdock-migration-test");
        migrationWrites = 0; rejectMigrationMarker = true;
        {
            QSettings host(path, format); QString error;
            QVERIFY(!SimDockState::migrateLegacy(legacy, host, &error));
            QVERIFY(migrationWrites >= 2);
            QVERIFY(!SimDockState::migrateLegacy(legacy, host, &error));
            QVERIFY(!object(path).value("integrations/simdock/legacyMigration/v1").toBool());
        }
        rejectMigrationMarker = false;
        QSettings retry(path, format); QString error;
        QVERIFY2(SimDockState::migrateLegacy(legacy, retry, &error), qPrintable(error));
        QVERIFY(object(path).value("integrations/simdock/legacyMigration/v1").toBool());
        QCOMPARE(read(legacy.fileName()), original);
    }
    void actualHostSessionAcrossProcesses() {
        QTemporaryDir directory;
        directory.setAutoRemove(qEnvironmentVariable("SIMDOCK_LEGACY_REPORT").isEmpty());
        const auto workspace = directory.filePath("workspace"), profile = directory.filePath("profile");
        QVERIFY(copyTree(fixtures() + "/host-0.31.27/workspace", workspace)); QDir().mkpath(profile);
        const auto wrapper = object(fixtures() + "/host-0.31.27/session-decoded.json");
        auto original = wrapper.begin().value().toObject();
        const auto identity = WorkspaceSessionStateService::workspaceIdentity(workspace);
        original.insert("workspaceId", identity);
        { QSettings session(profile + "/workspace-sessions.ini", QSettings::IniFormat);
          session.setValue("workspaceSessions/v2/workspaces/" + identity + "/state", QJsonDocument(original).toJson(QJsonDocument::Compact)); session.sync(); }
        for (const auto& phase : {"save", "restart"}) {
            QProcess process; process.setProcessChannelMode(QProcess::MergedChannels);
            process.start(QCoreApplication::applicationFilePath(), {"--session-phase", phase, workspace, profile});
            QVERIFY(process.waitForStarted(5000));
            const bool complete = process.waitForFinished(60000);
            if (!complete) { process.kill(); process.waitForFinished(5000); }
            const auto log = process.readAll();
            QVERIFY(write(profile + '/' + phase + ".log", log));
            QVERIFY2(complete && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0, log.constData());
        }
        const auto report = qEnvironmentVariable("SIMDOCK_LEGACY_REPORT");
        if (!report.isEmpty()) { QVERIFY(copyTree(profile, report)); qInfo().noquote() << "Isolated session evidence:" << directory.path(); }
    }
    void missingWaveEditorReportsErrorInIsolatedProcess() {
        QTemporaryDir profile;
        const auto executable = profile.filePath("without-wave.exe");
        QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(), executable));
        QProcess child; child.setProcessChannelMode(QProcess::MergedChannels);
        child.start(executable, {"--missing-wave", profile.path()});
        QVERIFY(child.waitForStarted(5000));
        const bool completed = child.waitForFinished(15000);
        if (!completed) { child.kill(); child.waitForFinished(3000); }
        const auto log = child.readAll();
        const auto report = qEnvironmentVariable("SIMDOCK_LEGACY_REPORT");
        if (!report.isEmpty()) QVERIFY(write(report + "/missing-wave.log", log));
        QVERIFY2(completed && child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0, log.constData());
    }
};

int main(int argc, char** argv) {
    QStandardPaths::setTestModeEnabled(true);
    QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
    const auto args = app.arguments();
    if (args.value(1) == "--session-phase") return sessionPhase(args.value(2), args.value(3), args.value(4));
    if (args.value(1) == "--missing-wave") {
        isolate(args.value(2));
        ApplicationThemeManager::instance().selectBackend(UiStyleBackend::Ela);
        ApplicationThemeManager::instance().applyToApplication();
        simdock::Scan scan;
        scan.files.append(simdock::analyzeSource("module dut(input logic clk, data); endmodule", "dut.sv"));
        bool saved = false;
        simdock::StimulusDialog dialog(scan.files.first().modules.first(), scan, {}, 1000,
            [&saved](const QJsonObject &, QString *) { saved = true; return true; });
        dialog.show(); QTest::qWait(30);
        const auto* message = dialog.findChild<QLabel*>("stimulusError");
        auto* save = dialog.findChild<QPushButton*>("saveStimulus");
        if (dialog.ready() || !message || !message->isVisible() || !message->text().contains("component is missing")
            || !save) return 30;
        save->click();
        if (saved || dialog.result() == QDialog::Accepted || !message->isVisible()) return 31;
        qInfo().noquote() << message->text();
        return 0;
    }
    QTemporaryDir profile; isolate(profile.path());
    LegacyCompatibilityTest test; return QTest::qExec(&test, argc, argv);
}
#include "legacy_compatibility_test.moc"
