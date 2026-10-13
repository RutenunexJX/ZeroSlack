#include "../../src/simulation/simdock/ui/workbench.h"
#include "support/mainwindow.h"
#include "../../src/simulation/simdock/core/questasession.h"
#include <QTableWidget>
#include <algorithm>
#include <QMouseEvent>
#include <QJsonArray>
#include "../../src/simulation/simdock/ui/dialogs.h"
#include "../../src/simulation/simdock/core/analyzer.h"
#include "../../src/simulation/simdock/core/testbench.h"
#include "slangmanager.h"
#include "semanticindexsnapshot.h"
#include "../../src/simulation/simdock/core/workspace.h"
#include "../../src/simulation/simdock/ui/uistyle.h"
#include <ElaApplication.h>
#include "applicationthememanager.h"
#include <ElaComboBox.h>
#include <ElaLineEdit.h>
#include <ElaListView.h>
#include <ElaPushButton.h>
#include <ElaDrawerArea.h>
#include <ElaMenu.h>
#include <ElaPlainTextEdit.h>
#include <ElaScrollBar.h>
#include <ElaSpinBox.h>
#include <ElaToolButton.h>
#include <ElaToolTip.h>
#include <QHelpEvent>
#include <ElaTheme.h>
#include <QContextMenuEvent>
#include <QClipboard>
#include <QPointer>
#include <QSplitter>
#include <QTextCursor>
#include <QWheelEvent>
#include <QFile>
#include <QLabel>
#include <QFontInfo>
#include <QSettings>
#include <QScrollBar>
#include <QScrollArea>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QStyleOptionViewItem>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#ifdef SIMDOCK_WITH_APPSUITE
#include "support/suiteintegration.h"
#include <suiteapp/protocol.h>
#include <suiteapp/client.h>
#include <QProcess>
#include <QFutureWatcher>
#include <QEventLoop>
#include <QScopeGuard>
#include <QtConcurrentRun>
#include <QUuid>
#include <QUrlQuery>
#endif
using namespace simdock;

static simdock::Module semanticModule(const QByteArray& content, const QString& file, const QString& root = {})
{
    const auto absolute = QFileInfo(file).absoluteFilePath();
    QHash<QString, QString> contents{{absolute, QString::fromUtf8(content)}};
    if (!root.isEmpty()) for (const auto& source : simdock::scanWorkspace(root).files)
        contents.insert(QDir(root).filePath(source.path), QString::fromUtf8(source.content));
    SlangManager slang;
    SemanticIndex::getInstance()->setSnapshot(std::make_shared<const SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolRecords(slang.extractOverlayWorkspaceSymbolRecords(contents), {}, {}, contents)));
    auto module = simdock::analyzeSource(content, absolute).modules.first();
    QString error;
    if (!simdock::completeTestbenchTypes(module, *SemanticIndex::getInstance(), absolute, QString::fromUtf8(content), &error))
        module.limitations = {error};
    return module;
}

class UiTest : public QObject {
    Q_OBJECT
    QTemporaryDir m_settings;
private slots:
#ifdef SIMDOCK_WITH_APPSUITE
    void suiteMissingRuntimeFallsBack()
    {
        QTemporaryDir workspace;
        MainWindow w;
        SuiteIntegration suite(&w);
        SuiteApp::RuntimeStartOptions options;
        options.endpoint = QStringLiteral("simdock.missing.%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        options.executablePath = workspace.filePath(QStringLiteral("missing-runtime.exe"));
        options.probeTimeoutMs = 100;
        QString error;
        {
            const auto savedPath = qgetenv("PATH");
            const auto savedRuntime = qgetenv("SUITEAPP_RUNTIME_EXECUTABLE");
            const auto restore = qScopeGuard([&] {
                if (savedPath.isNull()) qunsetenv("PATH"); else qputenv("PATH", savedPath);
                if (savedRuntime.isNull()) qunsetenv("SUITEAPP_RUNTIME_EXECUTABLE"); else qputenv("SUITEAPP_RUNTIME_EXECUTABLE", savedRuntime);
            });
            qputenv("PATH", workspace.path().toLocal8Bit());
            qputenv("SUITEAPP_RUNTIME_EXECUTABLE", options.executablePath.toLocal8Bit());
            if (!SuiteApp::locateRuntimeExecutable(options.executablePath).isEmpty())
                QSKIP("A runtime beside the test binary prevents the missing-runtime fixture.");
            QVERIFY(!suite.start(&error, options));
        }
        QVERIFY2(error.contains(QStringLiteral("could not be located")), qPrintable(error));
        QVERIFY(!SuiteApp::sendRequest(SuiteApp::descriptorEndpoint(SuiteIntegration::descriptor()),
            SuiteApp::makeRequest(QStringLiteral("health.ping")), 100).hasResponse());
        QSignalSpy scanned(&w, &MainWindow::scanFinished);
        w.openWorkspace(workspace.path());
        QTRY_COMPARE_WITH_TIMEOUT(scanned.size(), 1, 10000);
        QCOMPARE(w.workspace(), QFileInfo(workspace.path()).canonicalFilePath());
        w.show();
        QVERIFY(!w.grab().isNull());
        w.close();
    }
    void suiteRuntimeIpc()
    {
        const auto executable = qEnvironmentVariable("SUITEAPP_RUNTIME_EXECUTABLE");
        if (executable.isEmpty()) QSKIP("Set SUITEAPP_RUNTIME_EXECUTABLE to run the real Runtime IPC test.");
        QVERIFY(QFileInfo::exists(executable));
        SuiteApp::RuntimeStartOptions options;
        options.endpoint = QStringLiteral("simdock.ipc.%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        options.startIfMissing = false;
        options.probeTimeoutMs = 100;
        QProcess runtime;
        runtime.setProcessChannelMode(QProcess::MergedChannels);
        runtime.start(executable, {QStringLiteral("--endpoint"), options.endpoint});
        QVERIFY2(runtime.waitForStarted(3000), qPrintable(runtime.errorString()));
        const SuiteApp::Client client(options.endpoint, 2000);
        QTRY_VERIFY2_WITH_TIMEOUT(client.request(QStringLiteral("health.ping")).hasResponse(),
            runtime.readAll().constData(), 5000);
        qInfo().noquote() << "Runtime IPC endpoint:" << options.endpoint << "pid:" << runtime.processId();
        QTemporaryDir workspace;
        QTemporaryDir otherWorkspace;
        auto project = newProject(QStringLiteral("IPC project"));
        QString error;
        QVERIFY(saveProject(workspace.path(), project, &error));
        MainWindow w;
        auto suite = std::make_unique<SuiteIntegration>(&w);
        QVERIFY2(suite->start(&error, options), qPrintable(error));
        QVERIFY(suite->start(&error, options));
        const auto registered = client.listProviders().response.value(QStringLiteral("result")).toObject()
            .value(QStringLiteral("providers")).toArray();
        QCOMPARE(registered.size(), 1);
        const auto descriptor = registered.first().toObject();
        QCOMPARE(descriptor.value(QStringLiteral("appId")).toString(), QStringLiteral("simdock"));
        QCOMPARE(descriptor.value(QStringLiteral("displayName")).toString(), QStringLiteral("SimDock"));
        QCOMPARE(descriptor.value(QStringLiteral("version")).toString(), QStringLiteral(SIMDOCK_VERSION));
        QCOMPARE(descriptor.value(QStringLiteral("processId")).toInteger(), QCoreApplication::applicationPid());
        const auto roundTrip = [](auto request) {
            auto future = QtConcurrent::run(std::move(request));
            QFutureWatcher<SuiteApp::TransportResult> watcher;
            QEventLoop loop;
            QObject::connect(&watcher, &QFutureWatcher<SuiteApp::TransportResult>::finished, &loop, &QEventLoop::quit);
            watcher.setFuture(future);
            if (!future.isFinished()) loop.exec();
            return future.result().response;
        };
        QUrl url(QStringLiteral("simdock://project"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("workspace"), workspace.path());
        query.addQueryItem(QStringLiteral("id"), project.id);
        url.setQuery(query);
        const auto uri = url.toString(QUrl::FullyEncoded);
        const auto resolved = roundTrip([&] { return client.resolveResource(uri); });
        QVERIFY(resolved.value(QStringLiteral("ok")).toBool());
        QCOMPARE(resolved.value(QStringLiteral("result")).toObject().value(QStringLiteral("project")).toObject()
            .value(QStringLiteral("id")).toString(), project.id);
        QVERIFY(w.workspace().isEmpty());
        const auto described = roundTrip([&] { return client.describeSurface(QStringLiteral("simdock.workbench"), uri); });
        QCOMPARE(described.value(QStringLiteral("result")).toObject().value(QStringLiteral("mode")).toString(), QStringLiteral("external"));
        QVERIFY(w.workspace().isEmpty());
        QSignalSpy scanned(&w, &MainWindow::scanFinished);
        QVERIFY(roundTrip([&] { return client.invokeAction(QStringLiteral("simdock.project.open"), {}, uri); })
            .value(QStringLiteral("ok")).toBool());
        QTRY_COMPARE_WITH_TIMEOUT(scanned.size(), 1, 10000);
        QCOMPARE(w.workspace(), QFileInfo(workspace.path()).canonicalFilePath());
        QCOMPARE(w.findChild<ElaListView*>(QStringLiteral("projectList"))->currentIndex().data().toString(), project.name);
        const auto invalid = roundTrip([&] { return client.resolveResource(QStringLiteral("simdock://workspace?path=../missing")); });
        QCOMPARE(invalid.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("workspace_not_found"));
        const auto rejected = roundTrip([&] { return client.invokeAction(QStringLiteral("simdock.simulation.run"), {}, uri, QStringLiteral("simdock")); });
        QCOMPARE(rejected.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("action_not_supported"));
        QCOMPARE(w.workspace(), QFileInfo(workspace.path()).canonicalFilePath());
        QUrl otherUrl(QStringLiteral("simdock://workspace"));
        QUrlQuery otherQuery;
        otherQuery.addQueryItem(QStringLiteral("path"), otherWorkspace.path());
        otherUrl.setQuery(otherQuery);
        QVERIFY(roundTrip([&] { return client.openSurface(QStringLiteral("simdock.workbench"), otherUrl.toString(QUrl::FullyEncoded)); })
            .value(QStringLiteral("ok")).toBool());
        QTRY_COMPARE_WITH_TIMEOUT(scanned.size(), 2, 10000);
        QCOMPARE(w.workspace(), QFileInfo(otherWorkspace.path()).canonicalFilePath());
        suite.reset();
        const auto unregistered = client.listProviders().response;
        QVERIFY(unregistered.value(QStringLiteral("ok")).toBool());
        QVERIFY(unregistered.value(QStringLiteral("result")).toObject().value(QStringLiteral("providers")).toArray().isEmpty());
        QVERIFY(!SuiteApp::sendRequest(SuiteApp::descriptorEndpoint(descriptor),
            SuiteApp::makeRequest(QStringLiteral("health.ping")), 100).hasResponse());
        QVERIFY2(runtime.waitForFinished(6000), "The isolated Runtime did not exit after provider cleanup.");
        QCOMPARE(runtime.exitStatus(), QProcess::NormalExit);
        QCOMPARE(runtime.exitCode(), 0);
        qInfo() << "Runtime IPC: registration, resource, action, surface, rejection, unregister and idle exit passed";
        w.close();
    }
    void suiteWorkspaceAndProjectResources()
    {
        QTemporaryDir workspace;
        auto first = newProject(QStringLiteral("First"));
        auto second = newProject(QStringLiteral("Second"));
        QString error;
        QVERIFY(saveProject(workspace.path(), first, &error));
        QVERIFY(saveProject(workspace.path(), second, &error));
        MainWindow w;
        SuiteIntegration suite(&w);
        QVERIFY2(SuiteApp::validateAppDescriptor(SuiteIntegration::descriptor(), &error), qPrintable(error));
        QCOMPARE(SuiteIntegration::descriptor().value(QStringLiteral("version")).toString(), QStringLiteral(SIMDOCK_VERSION));
        QUrl url(QStringLiteral("simdock://project"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("workspace"), workspace.path());
        query.addQueryItem(QStringLiteral("id"), second.id);
        url.setQuery(query);
        QJsonObject params{{QStringLiteral("resourceUri"), url.toString(QUrl::FullyEncoded)}};
        const auto resolved = suite.handle(SuiteApp::makeRequest(QStringLiteral("resource.resolve"), params));
        QVERIFY(resolved.value(QStringLiteral("ok")).toBool());
        QCOMPARE(resolved.value(QStringLiteral("result")).toObject().value(QStringLiteral("project")).toObject().value(QStringLiteral("id")).toString(), second.id);
        QVERIFY(w.workspace().isEmpty());
        params.insert(QStringLiteral("surfaceId"), QStringLiteral("simdock.workbench"));
        const auto surface = suite.handle(SuiteApp::makeRequest(QStringLiteral("surface.describe"), params));
        QCOMPARE(surface.value(QStringLiteral("result")).toObject().value(QStringLiteral("mode")).toString(), QStringLiteral("external"));
        QVERIFY(w.workspace().isEmpty());
        params.insert(QStringLiteral("actionId"), QStringLiteral("simdock.project.open"));
        QSignalSpy scanned(&w, &MainWindow::scanFinished);
        QVERIFY(suite.handle(SuiteApp::makeRequest(QStringLiteral("action.invoke"), params)).value(QStringLiteral("ok")).toBool());
        QTRY_COMPARE_WITH_TIMEOUT(scanned.size(), 1, 10000);
        QCOMPARE(w.workspace(), QFileInfo(workspace.path()).canonicalFilePath());
        auto* projects = w.findChild<ElaListView*>(QStringLiteral("projectList"));
        QCOMPARE(projects->currentIndex().data().toString(), QStringLiteral("Second"));
        params.insert(QStringLiteral("actionId"), QStringLiteral("simdock.simulation.run"));
        QVERIFY(!suite.handle(SuiteApp::makeRequest(QStringLiteral("action.invoke"), params)).value(QStringLiteral("ok")).toBool());
        params.insert(QStringLiteral("resourceUri"), QStringLiteral("simdock://project?workspace=../outside&id=missing"));
        QVERIFY(!suite.handle(SuiteApp::makeRequest(QStringLiteral("resource.resolve"), params)).value(QStringLiteral("ok")).toBool());
        params.insert(QStringLiteral("resourceUri"), QStringLiteral("zeroslack://source?file=anything"));
        QVERIFY(!suite.handle(SuiteApp::makeRequest(QStringLiteral("resource.resolve"), params)).value(QStringLiteral("ok")).toBool());
        QCOMPARE(projects->currentIndex().data().toString(), QStringLiteral("Second"));
        w.close();
    }
#endif
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("SimDockTests"));
        QCoreApplication::setApplicationName(QStringLiteral("UiTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
        ApplicationThemeManager::instance().selectBackend(UiStyleBackend::Ela);
        ApplicationThemeManager::instance().applyToApplication();
        Ui::initialize();
        ApplicationThemeManager::instance().setMode(ThemeMode::Light);
        qInfo() << "UI font:" << QFontInfo(Ui::font()).family()
                << "code font:" << QFontInfo(Ui::codeFont()).family();
    }
    void workspaceProjectAndTb()
    {
        QTemporaryDir workspace(QDir::tempPath() + QStringLiteral("/simdock-counter-XXXXXX"));
        const auto input = QByteArrayLiteral("module counter #(parameter int WIDTH=8)(input logic clk, rst_n, enable, output logic [WIDTH-1:0] count); always_ff @(posedge clk or negedge rst_n) if(!rst_n) count<='0; else if(enable) count<=count+1'b1; endmodule");
        QFile file(workspace.filePath(QStringLiteral("counter.sv")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(input); file.close();
        MainWindow w;
        w.show();
        QSignalSpy scanned(&w, &MainWindow::scanFinished);
        w.openWorkspace(workspace.path());
        QTRY_COMPARE_WITH_TIMEOUT(scanned.size(), 1, 15000);
        QVERIFY(w.createProject(QStringLiteral("Counter demo")));
        auto* list = w.findChild<QListView*>(QStringLiteral("sourceList"));
        QVERIFY(list);
        auto* model = qobject_cast<QStandardItemModel*>(list->model());
        QCOMPARE(model->rowCount(), 1);
        QTest::qWait(100);
        const auto firstRow = list->visualRect(model->index(0, 0));
        QStyleOptionViewItem itemOption;
        itemOption.initFrom(list);
        itemOption.rect = firstRow;
        itemOption.features = QStyleOptionViewItem::HasCheckIndicator | QStyleOptionViewItem::HasDisplay;
        itemOption.text = model->item(0)->text();
        const auto indicator = list->style()->subElementRect(QStyle::SE_ItemViewItemCheckIndicator, &itemOption, list);
        QVERIFY(indicator.isValid());
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, indicator.center());
        QCOMPARE(model->item(0)->checkState(), Qt::Checked);
        auto* dut = w.findChild<ElaComboBox*>(QStringLiteral("dutSelector"));
        QCOMPARE(dut->count(), 1);
        QString error;
        const auto module = semanticModule(input, workspace.filePath("counter.sv"), workspace.path());
        QVERIFY2(w.createDemo(suggestedTbOptions(module), &error), qPrintable(error));
        QVERIFY(w.findChild<ElaPushButton*>(QStringLiteral("startSimulation"))->isEnabled());
        QVERIFY(!w.createDemo(suggestedTbOptions(module), &error));
        w.openWorkspace(workspace.path());
        QTRY_COMPARE_WITH_TIMEOUT(scanned.size(), 2, 15000);
        QVERIFY(!w.findChild<ElaLineEdit*>(QStringLiteral("tbFile"))->text().isEmpty());
        const QString screenshots = qEnvironmentVariable("SIMDOCK_SCREENSHOT_DIR");
        if (!screenshots.isEmpty()) {
            QDir().mkpath(screenshots);
            QTest::qWait(400);
            QVERIFY(w.grab().save(QDir(screenshots).filePath(QStringLiteral("simdock-light.png"))));
            ApplicationThemeManager::instance().setMode(ThemeMode::Dark);
            QTest::qWait(400);
            QVERIFY(w.grab().save(QDir(screenshots).filePath(QStringLiteral("simdock-dark.png"))));
            ApplicationThemeManager::instance().setMode(ThemeMode::Light);
            w.resize(1000, 700);
            QTest::qWait(150);
            QVERIFY(w.grab().save(QDir(screenshots).filePath(QStringLiteral("simdock-compact.png"))));
            w.resize(1200, 780);
            TestbenchDialog tb(module, 1000, &w);
            tb.show();
            QTest::qWait(250);
            QVERIFY(tb.grab().save(QDir(screenshots).filePath(QStringLiteral("simdock-tb.png"))));
            tb.close();
            SettingsDialog setup(QStringLiteral("D:/questa/win64/vsim.exe"), &w);
            setup.show();
            QTest::qWait(250);
            QVERIFY(setup.grab().save(QDir(screenshots).filePath(QStringLiteral("simdock-setup.png"))));
            setup.close();
        }
        QTemporaryDir second;
        w.openWorkspace(second.path());
        QTRY_COMPARE_WITH_TIMEOUT(scanned.size(), 3, 15000);
        QCOMPARE(qobject_cast<QStandardItemModel*>(list->model())->rowCount(), 0);
        QVERIFY(!w.findChild<ElaPushButton*>(QStringLiteral("startSimulation"))->isEnabled());
        w.close();
    }
    void waveThemeSettings()
    {
        QSettings().remove(QStringLiteral("waveform/theme"));
        QTemporaryDir simulator;
        for (const auto& name : {QStringLiteral("vsim.exe"), QStringLiteral("vlog.exe")}) {
            QFile file(simulator.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        MainWindow window;
        window.setSimulator(simulator.filePath(QStringLiteral("vsim.exe")));
        auto* settingsButton = window.findChild<QToolButton*>(QStringLiteral("openSettings"));
        QVERIFY(settingsButton);
        bool changed = false;
        QTimer::singleShot(50, &window, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) return;
            auto* selector = dialog->findChild<ElaComboBox*>(QStringLiteral("waveThemeSelector"));
            auto* save = dialog->findChild<ElaPushButton*>(QStringLiteral("saveSettings"));
            if (selector && save) {
                changed = selector->count() == 4 && selector->currentData() == QStringLiteral("default");
                selector->setCurrentIndex(selector->findData(QStringLiteral("mocha")));
                save->click();
            } else dialog->reject();
        });
        settingsButton->click();
        QVERIFY(changed);
        QCOMPARE(QSettings().value(QStringLiteral("waveform/theme")).toString(), QStringLiteral("mocha"));

        SettingsDialog reopened(simulator.filePath(QStringLiteral("vsim.exe")), &window, true);
        auto* selector = reopened.findChild<ElaComboBox*>(QStringLiteral("waveThemeSelector"));
        auto* preview = reopened.findChild<QLabel*>(QStringLiteral("waveThemePreview"));
        QVERIFY(selector && preview);
        QCOMPARE(reopened.waveTheme(), WaveTheme::Mocha);
        QVERIFY(selector->isEnabled());
        const auto mochaImage = preview->pixmap().toImage();
        QCOMPARE(mochaImage.pixelColor(0, 0), QColor(QStringLiteral("#1e1e2e")));
        selector->setCurrentIndex(selector->findData(QStringLiteral("solarized-light")));
        QCOMPARE(reopened.waveTheme(), WaveTheme::SolarizedLight);
        QCOMPARE(preview->pixmap().toImage().pixelColor(0, 0), QColor(QStringLiteral("#fdf6e3")));
        reopened.reject();
        QCOMPARE(QSettings().value(QStringLiteral("waveform/theme")).toString(), QStringLiteral("mocha"));

        bool restored = false;
        QTimer::singleShot(50, &window, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) return;
            auto* combo = dialog->findChild<ElaComboBox*>(QStringLiteral("waveThemeSelector"));
            if (combo) {
                combo->setCurrentIndex(combo->findData(QStringLiteral("default")));
                restored = true;
                dialog->accept();
            } else dialog->reject();
        });
        settingsButton->click();
        QVERIFY(restored);
        QCOMPARE(QSettings().value(QStringLiteral("waveform/theme")).toString(), QStringLiteral("default"));
        QSettings().setValue(QStringLiteral("waveform/theme"), QStringLiteral("obsolete-theme"));
        SettingsDialog invalid(simulator.filePath(QStringLiteral("vsim.exe")));
        QCOMPARE(invalid.waveTheme(), WaveTheme::QuestaDefault);
        QSettings().remove(QStringLiteral("waveform/theme"));
    }
    void namedTypeTbPreview()
    {
        QTemporaryDir workspace;
        QFile package(workspace.filePath(QStringLiteral("types.sv")));
        QVERIFY(package.open(QIODevice::WriteOnly));
        package.write("package demo_types; typedef enum logic [1:0] { IDLE, ACTIVE } mode_e; endpackage\n");
        package.close();
        const auto input = QByteArrayLiteral("module typed_dut import demo_types::*; (input logic clk, rst, input mode_e mode); endmodule\n");
        QFile source(workspace.filePath(QStringLiteral("dut.sv")));
        QVERIFY(source.open(QIODevice::WriteOnly));
        source.write(input);
        source.close();
        MainWindow w;
        QSignalSpy scanned(&w, &MainWindow::scanFinished);
        w.openWorkspace(workspace.path());
        QTRY_COMPARE_WITH_TIMEOUT(scanned.size(), 1, 15000);
        QVERIFY(w.createProject(QStringLiteral("Named types")));
        auto* model = qobject_cast<QStandardItemModel*>(w.findChild<QListView*>(QStringLiteral("sourceList"))->model());
        QVERIFY(model);
        for (int row = 0; row < model->rowCount(); ++row)
            if (model->item(row)->text().contains(QStringLiteral("dut.sv"))) model->item(row)->setCheckState(Qt::Checked);
        const auto module = semanticModule(input, workspace.filePath("dut.sv"), workspace.path());
        TestbenchDialog dialog(module, 1000, &w);
        QVERIFY(dialog.content().contains(QStringLiteral("import demo_types::*;")));
        QVERIFY(dialog.content().contains(QStringLiteral("mode = demo_types::IDLE;")));
        for (const auto* box : dialog.findChildren<ElaComboBox*>()) QVERIFY(box->findData(QStringLiteral("mode")) < 0);
        QTest::mouseClick(dialog.findChild<ElaPushButton*>(QStringLiteral("confirmCreateTb")), Qt::LeftButton);
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QString error;
        QVERIFY2(w.createDemo(dialog.options(), &error), qPrintable(error));
        QVERIFY(w.findChild<ElaPushButton*>(QStringLiteral("startSimulation"))->isEnabled());
        const auto projects = loadProjects(workspace.path());
        QCOMPARE(projects.size(), 1);
        const auto& project = projects.first();
        QVERIFY(project.sources.indexOf(QStringLiteral("types.sv")) < project.sources.indexOf(QStringLiteral("dut.sv")));
        QFile generated(workspace.filePath(project.tbFile));
        QVERIFY(generated.open(QIODevice::ReadOnly));
        QVERIFY(generated.readAll().contains("mode_e mode;"));
    }
    void firstSourceClickCreatesProject()
    {
        QTemporaryDir workspace;
        QFile file(workspace.filePath(QStringLiteral("counter.sv")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("module counter(input logic clk, output logic ready); endmodule\n");
        file.close();
        MainWindow w;
        w.show();
        QSignalSpy scanned(&w, &MainWindow::scanFinished);
        w.openWorkspace(workspace.path());
        QTRY_COMPARE_WITH_TIMEOUT(scanned.size(), 1, 15000);
        auto* list = w.findChild<QListView*>(QStringLiteral("sourceList"));
        auto* projects = w.findChild<ElaListView*>(QStringLiteral("projectList"));
        QVERIFY(list && projects);
        auto* model = qobject_cast<QStandardItemModel*>(list->model());
        const auto clickSource = [&] {
            const auto rect = list->visualRect(model->index(0, 0));
            const auto point = list->viewport()->mapTo(&w, QPoint(rect.left() + 18, rect.center().y()));
            QTest::mouseClick(w.windowHandle(), Qt::LeftButton, Qt::NoModifier, point);
        };
        bool cancelPrompt = false;
        QTimer::singleShot(100, &w, [&] {
            auto* dialog = w.findChild<QDialog*>(QStringLiteral("newProjectDialog"));
            cancelPrompt = dialog && dialog->isVisible();
            if (dialog) dialog->reject();
        });
        clickSource();
        QTRY_VERIFY_WITH_TIMEOUT(cancelPrompt, 1000);
        QCOMPARE(projects->model()->rowCount(), 0);
        QCOMPARE(model->item(0)->checkState(), Qt::Unchecked);
        bool createPrompt = false;
        QTimer::singleShot(100, &w, [&] {
            auto* dialog = w.findChild<QDialog*>(QStringLiteral("newProjectDialog"));
            createPrompt = dialog && dialog->isVisible();
            if (!dialog) return;
            auto* name = dialog->findChild<ElaLineEdit*>(QStringLiteral("projectNameInput"));
            auto* create = dialog->findChild<ElaPushButton*>(QStringLiteral("confirmCreateProject"));
            if (!name || !create) { dialog->reject(); return; }
            name->setText(QStringLiteral("Click regression"));
            QTest::mouseClick(create, Qt::LeftButton);
        });
        clickSource();
        QTRY_COMPARE_WITH_TIMEOUT(projects->model()->rowCount(), 1, 1000);
        QVERIFY(createPrompt);
        QCOMPARE(model->item(0)->checkState(), Qt::Checked);
        QCOMPARE(loadProjects(workspace.path()).first().sources, QStringList{QStringLiteral("counter.sv")});
        clickSource();
        QCOMPARE(model->item(0)->checkState(), Qt::Unchecked);
        clickSource();
        QCOMPARE(model->item(0)->checkState(), Qt::Checked);
        w.openWorkspace(workspace.path());
        QTRY_COMPARE_WITH_TIMEOUT(scanned.size(), 2, 15000);
        QCOMPARE(model->item(0)->checkState(), Qt::Checked);
        QVERIFY(w.createProject(QStringLiteral("Second project")));
        QCOMPARE(model->item(0)->checkState(), Qt::Unchecked);
        projects->setCurrentIndex(projects->model()->index(0, 0));
        QCOMPARE(model->item(0)->checkState(), Qt::Checked);
        clickSource();
        QCOMPARE(model->item(0)->checkState(), Qt::Unchecked);
        w.close();
    }
    void selectingSourceAddsDependencies()
    {
        QTemporaryDir workspace;
        const QList<QPair<QString, QByteArray>> inputs{
            {QStringLiteral("a_top.sv"), QByteArrayLiteral("module top; child u(); endmodule")},
            {QStringLiteral("child.sv"), QByteArrayLiteral("module child; import types_pkg::*; endmodule")},
            {QStringLiteral("types.sv"), QByteArrayLiteral("package types_pkg; endpackage")},
            {QStringLiteral("other.sv"), QByteArrayLiteral("module other; endmodule")}};
        for (const auto& input : inputs) {
            QFile file(workspace.filePath(input.first));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(input.second);
        }
        MainWindow w;
        w.show();
        QSignalSpy scanned(&w, &MainWindow::scanFinished);
        w.openWorkspace(workspace.path());
        QTRY_COMPARE_WITH_TIMEOUT(scanned.size(), 1, 15000);
        auto* list = w.findChild<QListView*>(QStringLiteral("sourceList"));
        auto* model = qobject_cast<QStandardItemModel*>(list->model());
        auto* dut = w.findChild<ElaComboBox*>(QStringLiteral("dutSelector"));
        const auto findSource = [&](const QString& path) -> QStandardItem* {
            for (int row=0; row<model->rowCount(); ++row)
                if (model->item(row)->data(Qt::UserRole).toString() == path) return model->item(row);
            return nullptr;
        };
        QTimer::singleShot(100, &w, [&] {
            auto* dialog = w.findChild<QDialog*>(QStringLiteral("newProjectDialog"));
            if (!dialog) return;
            dialog->findChild<ElaLineEdit*>(QStringLiteral("projectNameInput"))->setText(QStringLiteral("Dependencies"));
            dialog->findChild<ElaPushButton*>(QStringLiteral("confirmCreateProject"))->click();
        });
        const auto rect = list->visualRect(findSource(QStringLiteral("a_top.sv"))->index());
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(rect.left()+18, rect.center().y()));
        QTRY_COMPARE_WITH_TIMEOUT(loadProjects(workspace.path()).size(), 1, 2000);
        const QStringList expected{QStringLiteral("types.sv"), QStringLiteral("child.sv"), QStringLiteral("a_top.sv")};
        QCOMPARE(loadProjects(workspace.path()).first().sources, expected);
        QCOMPARE(dut->currentData().toStringList(), (QStringList{QStringLiteral("a_top.sv"), QStringLiteral("top")}));
        for (const auto& path : expected) QCOMPARE(findSource(path)->checkState(), Qt::Checked);
        QCOMPARE(findSource(QStringLiteral("other.sv"))->checkState(), Qt::Unchecked);
        list->setCurrentIndex(findSource(QStringLiteral("child.sv"))->index());
        QTest::keyClick(list, Qt::Key_Space);
        QCOMPARE(findSource(QStringLiteral("child.sv"))->checkState(), Qt::Unchecked);
        findSource(QStringLiteral("other.sv"))->setCheckState(Qt::Checked);
        QCOMPARE(findSource(QStringLiteral("child.sv"))->checkState(), Qt::Unchecked);
        dut->setCurrentIndex(dut->findData(QStringList{QStringLiteral("other.sv"), QStringLiteral("other")}));
        dut->setCurrentIndex(dut->findData(QStringList{QStringLiteral("a_top.sv"), QStringLiteral("top")}));
        QCOMPARE(findSource(QStringLiteral("child.sv"))->checkState(), Qt::Checked);
        findSource(QStringLiteral("a_top.sv"))->setCheckState(Qt::Unchecked);
        QCOMPARE(findSource(QStringLiteral("child.sv"))->checkState(), Qt::Checked);
        w.openWorkspace(workspace.path());
        QTRY_COMPARE_WITH_TIMEOUT(scanned.size(), 2, 15000);
        QCOMPARE(findSource(QStringLiteral("child.sv"))->checkState(), Qt::Checked);
        QCOMPARE(findSource(QStringLiteral("a_top.sv"))->checkState(), Qt::Unchecked);
        w.close();
    }
    void sourceListWheelScrolling_data()
    {
        QTest::addColumn<bool>("hasProject");
        QTest::newRow("workspace-before-project") << false;
        QTest::newRow("workspace-with-project") << true;
    }
    void sourceListWheelScrolling()
    {
        QFETCH(bool, hasProject);
        QTemporaryDir workspace;
        for (int i = 0; i < 80; ++i) {
            QFile file(workspace.filePath(QStringLiteral("source_%1.sv").arg(i, 3, 10, QLatin1Char('0'))));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(QStringLiteral("module source_%1; endmodule\n").arg(i).toUtf8());
        }
        MainWindow w;
        w.show();
        QSignalSpy scanned(&w, &MainWindow::scanFinished);
        w.openWorkspace(workspace.path());
        QTRY_COMPARE_WITH_TIMEOUT(scanned.size(), 1, 15000);
        if (hasProject) QVERIFY(w.createProject(QStringLiteral("Wheel regression")));
        auto* list = w.findChild<QListView*>(QStringLiteral("sourceList"));
        QVERIFY(list);
        QTRY_VERIFY(list->verticalScrollBar()->maximum() > 0);
        w.raise(); w.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&w));
        QTest::qWait(50);
        auto* section = w.findChild<ElaComboBox*>(QStringLiteral("workbenchSection"));
        if (section && section->isVisible()) section->setCurrentIndex(section->findData(QStringLiteral("sources")));
        QTRY_VERIFY(list->isVisible());
        auto* bar = list->verticalScrollBar();
        bar->setValue(0);
        const int rowHeight = list->visualRect(list->model()->index(0, 0)).height();
        const QPoint point = list->viewport()->mapTo(&w, list->viewport()->rect().center());
        // Qt 6.10's QTest wheel helper passes logical coordinates to the
        // native event entry point, which halves them at DPR 2. Keep window
        // dispatch, but send a QWheelEvent with its documented logical units.
        const auto wheel = [&](int delta) {
            QWheelEvent event(point, w.mapToGlobal(point), {}, QPoint(0, delta),
                Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(w.windowHandle(), &event);
            QCoreApplication::processEvents();
        };
        wheel(-120);
        qInfo() << "source wheel:" << "row height" << rowHeight
                << "single step" << bar->singleStep() << "movement" << bar->value()
                << "enabled" << list->isEnabled();
        QTRY_VERIFY2(bar->value() >= rowHeight, "One wheel notch must move at least one source row");
        QTest::qWait(180);
        wheel(120);
        QTRY_COMPARE(bar->value(), 0);
        wheel(120);
        QCOMPARE(bar->value(), 0);
        bar->setValue(bar->maximum());
        const int bottom = bar->value();
        wheel(-120);
        QCOMPARE(bar->value(), bottom);
        wheel(120);
        QTRY_VERIFY(bar->value() < bottom);
        bar->setValue(0);
        QTest::qWait(180);
        QCOMPARE(bar->value(), 0);
        auto* smooth = qobject_cast<ElaScrollBar*>(bar);
        QVERIFY(smooth && smooth->smoothWheelEnabled());
        const auto center = list->viewport()->rect().center();
        QWheelEvent pixels(center, list->viewport()->mapToGlobal(center), QPoint(0, -13), QPoint(),
            Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
        QApplication::sendEvent(list->viewport(), &pixels);
        QCOMPARE(bar->value(), 13);
        QTest::qWait(180);
        QCOMPARE(bar->value(), 13);
        wheel(-120);
        QTest::qWait(32);
        QTest::keyClick(list, Qt::Key_Home, Qt::ControlModifier);
        const int afterKey = bar->value();
        QTest::qWait(180);
        QCOMPARE(bar->value(), afterKey);
        bar->setValue(0);
        auto* model = qobject_cast<QStandardItemModel*>(list->model());
        if (!hasProject) QVERIFY(w.createProject(QStringLiteral("Wheel selection")));
        const auto index = model->index(0, 0);
        QStyleOptionViewItem option;
        option.initFrom(list);
        option.rect = list->visualRect(index);
        option.features = QStyleOptionViewItem::HasCheckIndicator | QStyleOptionViewItem::HasDisplay;
        option.text = model->item(0)->text();
        const QRect indicator = list->style()->subElementRect(QStyle::SE_ItemViewItemCheckIndicator, &option, list);
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, indicator.center());
        QCOMPARE(model->item(0)->checkState(), Qt::Checked);
        list->setCurrentIndex(index);
        QTest::keyClick(list, Qt::Key_Space);
        QCOMPARE(model->item(0)->checkState(), Qt::Unchecked);
        w.close();
    }
    void init()
    {
        QSettings().clear();
        ApplicationThemeManager::instance().setMode(ThemeMode::Light);
    }
    void sourceNativeInputAndMissingFileRefresh()
    {
        QTemporaryDir workspace;
        const QString path = workspace.filePath(QStringLiteral("source.sv"));
        const auto writeSource = [&] { QFile f(path); return f.open(QIODevice::WriteOnly) && f.write("module source; endmodule") > 0; };
        QVERIFY(writeSource());
        MainWindow w;
        w.show();
        QSignalSpy scans(&w, &MainWindow::scanFinished);
        w.openWorkspace(workspace.path());
        QTRY_COMPARE(scans.size(), 1);
        QVERIFY(w.createProject(QStringLiteral("Native input")));
        auto* view = w.findChild<QListView*>(QStringLiteral("sourceList"));
        auto* model = qobject_cast<QStandardItemModel*>(view->model());
        const auto index = model->index(0, 0);
        const auto row = view->visualRect(index);
        const auto check = QPoint(row.left() + 18, row.center().y());
        QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, row.center());
        QCOMPARE(model->item(0)->checkState(), Qt::Unchecked);
        QTest::mouseClick(view->viewport(), Qt::RightButton, Qt::NoModifier, check);
        QCOMPARE(model->item(0)->checkState(), Qt::Unchecked);
        QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, check);
        QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, row.center());
        QCOMPARE(model->item(0)->checkState(), Qt::Unchecked);
        QVERIFY(!model->setData(index, Qt::PartiallyChecked, Qt::CheckStateRole));
        QVERIFY(model->setData(index, Qt::Checked, Qt::CheckStateRole));
        QCOMPARE(loadProjects(workspace.path()).first().sources, QStringList{QStringLiteral("source.sv")});
        QVERIFY(QFile::remove(path));
        w.openWorkspace(workspace.path());
        QTRY_COMPARE(scans.size(), 2);
        QVERIFY(model->item(0)->text().contains(QStringLiteral("File missing")));
        QVERIFY(writeSource());
        w.openWorkspace(workspace.path());
        QTRY_COMPARE(scans.size(), 3);
        QCOMPARE(model->item(0)->text(), QStringLiteral("source.sv"));
        QCOMPARE(model->item(0)->foreground().style(), Qt::NoBrush);
        QCOMPARE(model->item(0)->checkState(), Qt::Checked);
        w.close();
    }
    void formOptionsRestoreWithoutLegacySections()
    {
        QTemporaryDir workspace;
        MainWindow w; w.show();
        QSignalSpy scans(&w, &MainWindow::scanFinished);
        w.openWorkspace(workspace.path());
        QTRY_COMPARE(scans.size(), 1);
        QVERIFY(w.createProject(QStringLiteral("Form state")));
        QVERIFY(!w.findChild<QToolButton*>("waveOptionsToggle"));
        QVERIFY(!w.findChild<QWidget*>("workbenchSection"));
        QVERIFY(!w.findChild<QWidget*>("openWorkspace"));
        QVERIFY(!w.findChild<ElaComboBox*>("waveScope"));
        auto* field = w.findChild<ElaLineEdit*>("tbFile"); field->setText("tb/preserve.sv");
        QPointer<ElaLineEdit> identity(field);
        QCOMPARE(identity.data(), field); QCOMPARE(field->text(), QString("tb/preserve.sv"));
        auto* workbench = w.findChild<Workbench*>(); QVERIFY(workbench);
        auto state = workbench->saveState();
        state.insert(QStringLiteral("layout/waveOptionsExpanded"), true);
        QVERIFY(workbench->restoreState(state).isEmpty());
        ApplicationThemeManager::instance().setMode(ThemeMode::Dark);
        QCOMPARE(identity.data(), field);
        w.close();
    }
    void comboAndMenuInterruptions()
    {
        QWidget host;
        auto* combo = new ElaComboBox(&host);
        combo->addItems({QStringLiteral("One"), QStringLiteral("Two"), QStringLiteral("Three")});
        Ui::normalizeControls(&host);
        host.resize(400, 260);
        host.show();
        auto* base = static_cast<QComboBox*>(combo);
        for (int i = 0; i < 5; ++i) { base->showPopup(); base->hidePopup(); }
        QVERIFY(!combo->isPopupAnimating());
        base->showPopup();
        QTest::keyClick(combo->view(), Qt::Key_Down);
        QTest::keyClick(combo->view(), Qt::Key_Return);
        QTRY_VERIFY(!combo->view()->isVisible());
        QCOMPARE(combo->currentIndex(), 1);
        base->showPopup();
        QTest::keyClick(combo->view(), Qt::Key_Escape);
        QTRY_VERIFY(!combo->isPopupAnimating());
        QCOMPARE(combo->currentIndex(), 1);
        base->showPopup();
        ApplicationThemeManager::instance().setMode(ThemeMode::Dark);
        QVERIFY(!combo->isPopupAnimating());
        delete combo;
        auto* text = Ui::textView(&host);
        text->resize(host.size());
        text->setPlainText(QStringLiteral("selected text\nsecond line"));
        text->setReadOnly(true);
        text->show();
        QTextCursor cursor(text->document());
        cursor.setPosition(8, QTextCursor::KeepAnchor);
        text->setTextCursor(cursor);
        host.activateWindow();
        text->setFocus();
        QTest::qWait(50);
        const auto openMenu = [&] {
            QContextMenuEvent event(QContextMenuEvent::Mouse, QPoint(20, 20), text->viewport()->mapToGlobal(QPoint(20, 20)));
            QApplication::sendEvent(text->viewport(), &event);
            return text->findChild<ElaMenu*>(QStringLiteral("textContextMenu"));
        };
        QPointer<ElaMenu> menu = openMenu();
        QVERIFY(menu && menu->isVisible());
        QTRY_VERIFY(menu && !menu->isPopupAnimating());
        QAction* copy = nullptr;
        for (auto* action : menu->actions()) {
            const auto name = action->text().remove(QLatin1Char('&'));
            if (name.startsWith(QStringLiteral("Copy"))) copy = action;
            QVERIFY(!name.startsWith(QStringLiteral("Paste")));
        }
        QVERIFY(copy && copy->isEnabled());
        copy->trigger();
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("selected"));
        QPointer<ElaMenu> closing(menu);
        menu->close();
        QTRY_VERIFY(closing.isNull());
        menu = openMenu();
        QVERIFY(menu);
        menu->addAction(QStringLiteral("Late action"));
        QVERIFY(!menu->isPopupAnimating());
        QTest::keyClick(menu, Qt::Key_Escape);
        QTRY_VERIFY(!text->findChild<ElaMenu*>(QStringLiteral("textContextMenu")));
        openMenu();
        delete text;
    }
    void comboPopupFitsRows_data()
    {
        QTest::addColumn<int>("count");
        for (int count : {1, 3, 5}) QTest::newRow(qPrintable(QString::number(count))) << count;
    }
    void comboPopupFitsRows()
    {
        QFETCH(int, count);
        QWidget host;
        auto* combo = new ElaComboBox(&host);
        for (int i = 0; i < count; ++i) combo->addItem(QStringLiteral("Module %1").arg(i));
        Ui::normalizeControls(&host);
        host.resize(340, 240);
        host.move(30, 30);
        host.show();
        QTest::qWait(30);
        int popupHeight = -1;
        auto* base = static_cast<QComboBox*>(combo);
        for (int attempt = 0; attempt < 3; ++attempt) {
            combo->setCurrentIndex(attempt % 2 ? count - 1 : 0);
            base->showPopup();
            QTRY_VERIFY(!combo->isPopupAnimating());
            auto* view = combo->view();
            const auto first = view->visualRect(combo->model()->index(0, 0));
            const auto last = view->visualRect(combo->model()->index(count - 1, 0));
            QVERIFY(first.isValid() && last.isValid());
            QVERIFY2(first.top() >= 0, qPrintable(QStringLiteral("First row starts at %1").arg(first.top())));
            QVERIFY2(last.bottom() < view->viewport()->height(), qPrintable(QStringLiteral("Last row bottom %1, viewport %2").arg(last.bottom()).arg(view->viewport()->height())));
            const int height = view->window()->height();
            if (popupHeight >= 0) QCOMPARE(height, popupHeight);
            popupHeight = height;
            base->showPopup();
            QVERIFY(!combo->isPopupAnimating());
            QCOMPARE(view->window()->height(), height);
            QTest::keyClick(view, attempt % 2 ? Qt::Key_Home : Qt::Key_End);
            QTest::keyClick(view, Qt::Key_Return);
            QCOMPARE(combo->currentIndex(), attempt % 2 ? 0 : count - 1);
            base->hidePopup();
        }
    }
    void logBatchingPreservesReadingAndClear()
    {
        MainWindow w;
        w.show();
        auto* log = w.findChild<ElaPlainTextEdit*>(QStringLiteral("simulationLog"));
        auto* session = w.findChild<QuestaSession*>();
        auto* clear = w.findChild<ElaPushButton*>(QStringLiteral("clearLog"));
        QVERIFY(log && session && clear);
        const auto append = [session](const QString& text) { return QMetaObject::invokeMethod(session, "logText", Qt::DirectConnection, Q_ARG(QString, text)); };
        const QString lines = QStringLiteral("A line of simulation output\n").repeated(500);
        QVERIFY(append(lines));
        QVERIFY(log->toPlainText().isEmpty());
        QTRY_COMPARE(log->document()->blockCount(), 501);
        QTRY_COMPARE(log->verticalScrollBar()->value(), log->verticalScrollBar()->maximum());
        QTextCursor cursor(log->document());
        cursor.setPosition(6, QTextCursor::KeepAnchor);
        log->setTextCursor(cursor);
        log->verticalScrollBar()->setValue(0);
        QVERIFY(append(QStringLiteral("appended while reading\n")));
        QTRY_VERIFY(log->toPlainText().contains(QStringLiteral("appended while reading")));
        QCOMPARE(log->verticalScrollBar()->value(), 0);
        QCOMPARE(log->textCursor().selectedText(), QStringLiteral("A line"));
        QVERIFY(append(QStringLiteral("pending text")));
        clear->click();
        QTest::qWait(40);
        QVERIFY(log->toPlainText().isEmpty());
        w.close();
    }
    void focusedListDestruction()
    {
        for (int i = 0; i < 3; ++i) {
            auto* view = new ElaListView;
            auto* model = new QStandardItemModel(view);
            model->appendRow(new QStandardItem(QStringLiteral("Focus before destruction")));
            view->setModel(model);
            view->show();
            view->activateWindow();
            view->setFocus();
            QTRY_VERIFY(view->hasFocus());
            delete view;
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
    }
    void tooltipKeepsFocusAndDismisses()
    {
        QWidget host;
        auto* field = new ElaLineEdit(&host);
        field->setToolTip(QStringLiteral("Workspace path"));
        Ui::enableToolTip(field);
        host.show();
        host.activateWindow();
        field->setFocus();
        QTRY_VERIFY(field->hasFocus());
        QHelpEvent help(QEvent::ToolTip, QPoint(5, 5), field->mapToGlobal(QPoint(5, 5)));
        QApplication::sendEvent(field, &help);
        auto* tip = host.findChild<ElaToolTip*>(QStringLiteral("uiToolTip"));
        QVERIFY(tip && tip->isVisible());
        QCOMPARE(QApplication::focusWidget(), field);
        QTest::keyClick(field, Qt::Key_A);
        QVERIFY(!tip->isVisible());
        QCOMPARE(field->text(), QStringLiteral("a"));
        QApplication::sendEvent(field, &help);
        QPointer<ElaToolTip> destroyed(tip);
        delete field;
        QVERIFY(destroyed.isNull() || !destroyed->isVisible());
    }
    void textWheelUnitsMatchQt()
    {
        QWidget host;
        QPlainTextEdit reference(&host);
        auto* styled = Ui::textView(&host);
        for (auto* edit : {&reference, static_cast<QPlainTextEdit*>(styled)}) {
            edit->setFont(Ui::codeFont());
            edit->resize(350, 220);
            edit->setPlainText(QStringLiteral("A simulation transcript line\n").repeated(200));
        }
        styled->move(360, 0);
        host.resize(720, 240);
        host.show();
        QTest::qWait(50);
        QCOMPARE(styled->verticalScrollBar()->singleStep(), reference.verticalScrollBar()->singleStep());
        const auto wheel = [](QPlainTextEdit* edit, QPoint pixels, QPoint angle) {
            const QPoint pos(30, 30);
            QWheelEvent event(pos, edit->viewport()->mapToGlobal(pos), pixels, angle,
                Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
            QApplication::sendEvent(edit->viewport(), &event);
        };
        for (int delta : {-13, -43, 19}) {
            wheel(&reference, QPoint(0, delta), QPoint(0, -120));
            wheel(styled, QPoint(0, delta), QPoint(0, -120));
            QCOMPARE(styled->verticalScrollBar()->value(), reference.verticalScrollBar()->value());
            QTest::qWait(180);
            QCOMPARE(styled->verticalScrollBar()->value(), reference.verticalScrollBar()->value());
        }
        wheel(&reference, {}, QPoint(0, -120));
        wheel(styled, {}, QPoint(0, -120));
        QTRY_COMPARE(styled->verticalScrollBar()->value(), reference.verticalScrollBar()->value());
    }
    void overlayOriginReplacement()
    {
        QScrollArea area;
        auto* content = new QWidget;
        content->resize(400, 1600);
        area.setWidget(content);
        area.resize(300, 240);
        QPointer<QScrollBar> origin = area.verticalScrollBar();
        auto* overlay = new ElaScrollBar(origin, &area);
        overlay->setSmoothWheelEnabled(true);
        area.show();
        QTest::qWait(50);
        QWheelEvent event(QPoint(5, 5), overlay->mapToGlobal(QPoint(5, 5)), {}, QPoint(0, -120),
            Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(overlay, &event);
        area.setVerticalScrollBar(new QScrollBar(Qt::Vertical, &area));
        QTRY_VERIFY(origin.isNull());
        area.resize(320, 260);
        QVERIFY(!area.grab().isNull());
        overlay->setValue(5);
        QTest::qWait(200);
        QVERIFY(!overlay->isVisible());
    }
    void workspaceSwapDiscardsStaleScan()
    {
        QTemporaryDir first, second;
        for (int i = 0; i < 350; ++i) {
            QFile f(first.filePath(QStringLiteral("m%1.sv").arg(i)));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QStringLiteral("module m%1; endmodule").arg(i).toUtf8());
        }
        QFile f(second.filePath(QStringLiteral("chosen.sv")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("module chosen; endmodule");
        f.close();
        MainWindow w;
        QSignalSpy scans(&w, &MainWindow::scanFinished);
        w.openWorkspace(first.path());
        w.openWorkspace(second.path());
        QTRY_COMPARE(scans.size(), 1);
        auto* model = w.findChild<QListView*>(QStringLiteral("sourceList"))->model();
        QCOMPARE(model->rowCount(), 1);
        QCOMPARE(model->index(0, 0).data(Qt::UserRole).toString(), QStringLiteral("chosen.sv"));
        QTest::qWait(250);
        QCOMPARE(scans.size(), 1);
        auto* disposable = new MainWindow;
        disposable->openWorkspace(first.path());
        delete disposable;
    }
    void numericControlsAndDialogKeys()
    {
        const auto module = semanticModule(QByteArrayLiteral("module demo(input logic clk, rst_n); endmodule"), QStringLiteral("demo.sv"));
        TestbenchDialog dialog(module, 1000);
        dialog.show();
        for (auto* spin : dialog.findChildren<ElaSpinBox*>()) {
            spin->setValue(spin->maximum());
            auto* edit = spin->findChild<QLineEdit*>();
            QVERIFY(edit);
            QTRY_VERIFY(edit->width() >= edit->fontMetrics().horizontalAdvance(edit->text()) + edit->textMargins().left() + edit->textMargins().right());
        }
        auto* name = dialog.findChild<ElaLineEdit*>(QStringLiteral("tbNameInput"));
        name->setFocus();
        QTest::keyClick(name, Qt::Key_Return);
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        TestbenchDialog cancelled(module, 1000);
        cancelled.show();
        QTest::keyClick(&cancelled, Qt::Key_Escape);
        QCOMPARE(cancelled.result(), int(QDialog::Rejected));
        QVERIFY(!cancelled.isVisible());
    }
};
QTEST_MAIN(UiTest)
#include "ui_test.moc"
