#include "mainwindow.h"
#include "activitylogservice.h"
#include "analysisscheduler.h"
#include "applicationthememanager.h"
#include "contextworkspacecontroller.h"
#include "crashrecoveryservice.h"
#include "liveinsightscontextprovider.h"
#include "mycodeeditor.h"
#include "panellayoutcontroller.h"
#include "semanticdockcoordinator.h"
#include "semanticpanelrefreshcoordinator.h"
#include "signaltracepanel.h"
#include "signaltracemodels.h"
#include "navigationmanager.h"
#include <QTableView>
#include "semanticindexsnapshot.h"
#include "projectmodel.h"
#include "tabmanager.h"
#include "testuistyle.h"
#include "usertemplateservice.h"
#include "workspacemanager.h"
#include "workspacesessioncoordinator.h"

#include <QtTest>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>

namespace {
bool write(const QString& path, const QByteArray& text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(text) == text.size();
}
}

class BottomPanelsUiTest : public QObject {
    Q_OBJECT
    void capture(MainWindow& window, const QString& name) {
        const QString dir = qEnvironmentVariable("ZEROSLACK_BOTTOM_UI_REPORT");
        if (dir.isEmpty()) return;
        QDir().mkpath(dir); QTest::qWait(60);
        const auto expectedTheme = name.contains("dark") ? ThemeMode::Dark : ThemeMode::Light;
        QCOMPARE(ApplicationThemeManager::instance().mode(), expectedTheme);
        const auto image = window.grab();
        QVERIFY(image.save(dir + '/' + name + ".png"));
        QVERIFY(write(dir + '/' + name + ".json", QJsonDocument(QJsonObject{
            {"platform", QGuiApplication::platformName()}, {"dpr", window.devicePixelRatioF()},
            {"theme", expectedTheme == ThemeMode::Dark ? "dark" : "light"},
            {"width", window.width()}, {"height", window.height()},
            {"pixelWidth", image.width()}, {"pixelHeight", image.height()}}).toJson()));
    }
private slots:
    void realHost_data() {
        QTest::addColumn<bool>("dark"); QTest::addColumn<int>("width");
        QTest::newRow("light") << false << 1280;
        QTest::newRow("dark") << true << 1280;
        QTest::newRow("narrow-dark") << true << 780;
    }
    void realHost() {
        QFETCH(bool, dark); QFETCH(int, width);
        QTemporaryDir data;
        const QString workspace = data.filePath("workspace");
        const QString file = workspace + "/scope.sv";
        QVERIFY(write(file,
            "module scope_mod(input logic clk, input logic enable, input logic data, output logic sig, output logic observed);\n"
            "  always_ff @(posedge clk) begin\n"
            "    if (enable) sig <= data;\n"
            "  end\n"
            "  assign observed = sig;\n"
            "endmodule\n"));
        qputenv("ZEROSLACK_SESSION_STORAGE_PATH", data.filePath("session.ini").toUtf8());
        qputenv("ZEROSLACK_EDITING_TIME_STORAGE_PATH", data.filePath("editing.json").toUtf8());
        ApplicationThemeManager::instance().setMode(dark ? ThemeMode::Dark : ThemeMode::Light);
        MainWindow window;
        window.tabManager->setCrashRecoveryService(std::make_unique<CrashRecoveryService>(data.filePath("recovery")));
        window.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
        window.analysisScheduler->setSemanticAnalysisRuntimePolicy(SemanticAnalysisRuntimePolicy{});
        window.resize(width, 760); window.show();
        qInfo() << "host created";
        auto* sessions = window.findChild<WorkspaceSessionCoordinator*>(); QVERIFY(sessions);
        QVERIFY(sessions->openWorkspace(workspace));
        qInfo() << "workspace opened";
        QVERIFY(window.tabManager->openFileInTab(file));
        window.analysisScheduler->requestDocumentSemanticRefresh({file});
        qInfo() << "file opened";
        auto* layout = window.panelLayoutController.get(); QVERIFY(layout);
        auto* context = window.findChild<ContextWorkspaceController*>(); QVERIFY(context);
        auto* hotspot = window.findChild<SignalTracePanel*>(); QVERIFY(hotspot);
        QTRY_VERIFY_WITH_TIMEOUT(SemanticIndex::getInstance()->snapshotToken().snapshot
            && !SemanticIndex::getInstance()->snapshotToken().snapshot->getSymbolRecordsByName("sig",file).isEmpty(),20000);
        QVERIFY(window.navigationManager->setDesignTop("scope_mod",file).isEmpty());
        QVERIFY(layout->bottomPanelIds().contains("hotspot"));
        QVERIFY(layout->buttonForPanel("hotspot")->isVisible());
        // Use the real source-action adapter, service snapshot and editor.
        auto* refresh = window.semanticDocks->refreshCoordinator(); QVERIFY(refresh);
        refresh->showSignalUsageHotspotForSymbol("sig", file, "scope_mod");
        qInfo() << "source action dispatched";
        QTRY_VERIFY_WITH_TIMEOUT(!hotspot->busy() && hotspot->heatModel()->rowCount()>0, 10000);
        const auto expectedTheme = dark ? ThemeMode::Dark : ThemeMode::Light;
        ApplicationThemeManager::instance().setMode(expectedTheme);
        QApplication::processEvents();
        QCOMPARE(ApplicationThemeManager::instance().mode(), expectedTheme);
        QCOMPARE(layout->activeBottomPanelId(), QStringLiteral("hotspot"));
        QVERIFY(!layout->isBottomCollapsed());
        QVERIFY(!context->viewForResource(LiveInsightsContextProvider::resourceForKind(LiveInsightKind::Hotspot, workspace).stableKey()));
        hotspot->setHeatmapMode(true);
        const QString prefix = QString::fromLatin1(QTest::currentDataTag());
        capture(window, prefix + "-matrix");
        const auto first=hotspot->heatModel()->references().first();
        auto* table=hotspot->heatTable();QVERIFY(table && table->model()==hotspot->heatModel());
        table->setCurrentIndex(table->model()->index(0,SignalTraceHeatModel::Total));
        auto* details=hotspot->findChild<QTreeWidget*>("traceReferences");QVERIFY(details && details->topLevelItemCount()>0);
        const auto selection=hotspot->currentObject().id();
        details->itemActivated(details->topLevelItem(0),0);
        QTRY_COMPARE(window.tabManager->getCurrentEditor()->textCursor().blockNumber()+1,first.range.line);
        auto markerCount=[](MyCodeEditor* editor){int count=0;for(const auto& mark:editor->annotationLayerReportForTest().annotations)
            count+=mark.annotation.kind==EditorAnnotationKind::SignalTrace;return count;};
        QTRY_VERIFY(markerCount(window.tabManager->getCurrentEditor())>0);
        hotspot->setFocusSearchText("sig");
        capture(window,prefix+"-heatmap-details");
        auto* originalParent = hotspot->parentWidget();
        auto* expand = hotspot->findChild<QToolButton*>("traceMainArea"); QVERIFY(expand);
        expand->click();
        QTRY_VERIFY(hotspot->isInMainArea());
        QVERIFY(hotspot->parentWidget() != originalParent);
        QVERIFY(window.tabManager->toolPage("hotspot"));
        QCOMPARE(hotspot->focusSearchText(), QStringLiteral("sig"));
        QCOMPARE(hotspot->currentObject().id(), selection);
        capture(window, prefix + "-main-area");
        expand->click();
        QTRY_VERIFY(!hotspot->isInMainArea());
        QTRY_COMPARE(hotspot->parentWidget(), originalParent);
        QTRY_VERIFY(!layout->isBottomCollapsed());
        QCOMPARE(hotspot->currentObject().id(), selection);
        QVERIFY(!window.tabManager->toolPage("hotspot"));
        // Persist only the panel's presentation projection and target.
        const auto saved = layout->layoutState();
        hotspot->setFocusSearchText("no-match");
        layout->restoreLayoutState(saved);
        QTRY_VERIFY(!hotspot->busy());
        QCOMPARE(hotspot->focusSearchText(), QStringLiteral("sig"));
        QCOMPARE(hotspot->currentObject().id(), selection);
        const auto legacy = LiveInsightsContextProvider::resourceForKind(LiveInsightKind::Hotspot, workspace,
            {{"target", QVariantMap{{"fileName", file}, {"moduleName", "scope_mod"}, {"signalName", "sig"}}}});
        QVERIFY(context->openResource(legacy));
        QVERIFY(!context->viewForResource(legacy.stableKey()));
        QCOMPARE(window.findChild<SignalTracePanel*>(), hotspot);
        hotspot->clearTarget();
        ContextWorkspaceState legacyState;
        legacyState.valid = true;
        legacyState.pinnedResources = {legacy.toVariantMap()};
        const auto migrated = context->restoreState(legacyState);
        QCOMPARE(migrated.restoredResources, 1);
        QCOMPARE(migrated.skippedResources, 0);
        QTRY_VERIFY(!hotspot->busy() && hotspot->heatModel()->rowCount()>0);
        QVERIFY(context->captureState().pinnedResources.isEmpty());
        QCOMPARE(layout->layoutState().bottomPanelViewStates.value("hotspot").value("signal").toString(), QStringLiteral("sig"));
        const QString otherFile = workspace + "/other.sv";
        QVERIFY(write(otherFile, "module other; endmodule\n"));
        QVERIFY(window.tabManager->openFileInTab(otherFile));
        QApplication::processEvents();
        QCOMPARE(markerCount(window.tabManager->getCurrentEditor()),0);
        QVERIFY(window.tabManager->activateOpenFile(file));

        auto* clear = window.findChild<QToolButton*>("activityPanelActionButton"); QVERIFY(clear);
        QVERIFY(!clear->isVisible());
        QVERIFY(layout->restorePanel("activity"));
        QTRY_VERIFY(clear->isVisible());
        auto* output = window.findChild<QPlainTextEdit*>("activityOutputText"); QVERIFY(output);
        ActivityLogService::getInstance()->append("UI test", ActivityLogLevel::Info, "pending clear contract");
        clear->click(); QApplication::processEvents();
        QVERIFY(!output->toPlainText().contains("pending clear contract"));
        for (const auto& entry : ActivityLogService::getInstance()->events()) QVERIFY(entry.message != "pending clear contract");
        ActivityLogService::getInstance()->append("UI test", ActivityLogLevel::Info, "after clear contract");
        QTRY_VERIFY(output->toPlainText().contains("after clear contract"));
        QVERIFY(clear->mapTo(layout->buttonBar(), QPoint()).x() > layout->buttonBar()->width() - 90);
        QVERIFY(layout->buttonForPanel("simulationLog")->geometry().right() < layout->buttonForPanel("hotspot")->geometry().left());
        capture(window, prefix + "-activity");
        layout->setBottomCollapsed(true); QVERIFY(!clear->isVisible());
        QVERIFY(layout->restorePanel("problems")); QVERIFY(!clear->isVisible());
        capture(window, prefix + "-problems");
        QVERIFY(layout->restorePanel("simulationLog")); QVERIFY(!clear->isVisible());
        auto* runLog = window.findChild<QPlainTextEdit*>("simulationLog"); QVERIFY(runLog && runLog->isVisible());
        // A new workspace invalidates the old target and any delayed report.
        const QString other = data.filePath("other"); QDir().mkpath(other);
        QVERIFY(sessions->openWorkspace(other));
        QTRY_VERIFY(!hotspot->currentObject().isValid());
        QTRY_VERIFY(!hotspot->busy());
        QCOMPARE(hotspot->heatModel()->rowCount(),0);
        QVERIFY(window.close());
    }
};

int main(int argc, char** argv)
{
    QStandardPaths::setTestModeEnabled(true);
    QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
    QCoreApplication::setOrganizationName("ZeroSlackBottomPanelsTest");
    QCoreApplication::setApplicationName("ZeroSlackBottomPanelsTest");
    QTemporaryDir profile;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, profile.path());
    qputenv("ZEROSLACK_SIMDOCK_LEGACY_SETTINGS_PATH", profile.filePath("legacy.ini").toUtf8());
    UserTemplateService::getInstance()->setGlobalTemplateFilePath(profile.filePath("templates.json"));
    if (!initializeUiStyleForTest()) return 2;
    ApplicationThemeManager::instance().setAnimationsEnabled(false);
    BottomPanelsUiTest test; return QTest::qExec(&test, argc, argv);
}
#include "bottom_panels_ui_test.moc"
