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
#include "signalusagehotspotpanel.h"
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
        auto* hotspot = dynamic_cast<SignalUsageHotspotPanel*>(window.findChild<QWidget*>("signalUsageHotspotPanel")); QVERIFY(hotspot);
        SignalUsageHotspotQuery query;
        query.signalName = "sig"; query.fileName = file; query.moduleName = "scope_mod";
        SignalUsageHotspotReport model;
        qInfo() << "waiting for real semantic report";
        QTRY_VERIFY_WITH_TIMEOUT((model = SignalUsageHotspotService::getInstance()->buildSignalUsageHotspot(query)).found
                                && !model.items.isEmpty(), 10000);
        qInfo() << "semantic report ready" << model.items.size();
        QVERIFY(layout->bottomPanelIds().contains("hotspot"));
        QVERIFY(layout->buttonForPanel("hotspot")->isVisible());
        // Use the real source-action adapter, service snapshot and editor.
        auto* refresh = window.semanticDocks->refreshCoordinator(); QVERIFY(refresh);
        refresh->showSignalUsageHotspotForSymbol("sig", file, "scope_mod");
        qInfo() << "source action dispatched";
        QTRY_VERIFY_WITH_TIMEOUT(!hotspot->reportBuildInFlightForTest() && hotspot->matrixItemCountForTest() > 0, 10000);
        const auto expectedTheme = dark ? ThemeMode::Dark : ThemeMode::Light;
        ApplicationThemeManager::instance().setMode(expectedTheme);
        QApplication::processEvents();
        QCOMPARE(ApplicationThemeManager::instance().mode(), expectedTheme);
        QCOMPARE(layout->activeBottomPanelId(), QStringLiteral("hotspot"));
        QVERIFY(!layout->isBottomCollapsed());
        QVERIFY(!context->viewForResource(LiveInsightsContextProvider::resourceForKind(LiveInsightKind::Hotspot, workspace).stableKey()));
        hotspot->setMatrixModeForTest(true);
        const QString prefix = QString::fromLatin1(QTest::currentDataTag());
        capture(window, prefix + "-matrix");
        const auto& first = model.items.first();
        QVERIFY(first.snippet.contains("sig") && (first.snippet.contains("<=") || first.snippet.contains("assign")));
        auto* matrix = hotspot->findChild<QGraphicsView*>("signalUsageHotspotMatrixView"); QVERIFY(matrix);
        QGraphicsItem* cell = nullptr;
        for (auto* item : matrix->scene()->items())
            if (item->toolTip().startsWith(SignalUsageHotspotService::roleDisplayName(first.role) + QString::fromUtf8(" · ")))
                cell = item;
        QVERIFY(cell);
        QTest::mouseClick(matrix->viewport(), Qt::LeftButton, Qt::NoModifier,
                          matrix->mapFromScene(cell->sceneBoundingRect().center()));
        QTRY_VERIFY(hotspot->inlineItemsForTest());
        const int selection = hotspot->selectedItemIndexForTest();
        hotspot->setFocusSearchText("sig");
        hotspot->setMatrixModeForTest(false);
        QCOMPARE(hotspot->selectedItemIndexForTest(), selection);
        QVERIFY(hotspot->inlineItemsForTest());
        auto* details = hotspot->inlineItemsForTest();
        QApplication::processEvents();
        const QPoint rowCenter = details->visualItemRect(details->topLevelItem(0)).center();
        QTest::mouseClick(details->viewport(), Qt::LeftButton, Qt::NoModifier, rowCenter);
        QTest::mouseDClick(details->viewport(), Qt::LeftButton, Qt::NoModifier, rowCenter);
        QTRY_COMPARE(window.tabManager->getCurrentEditor()->textCursor().blockNumber() + 1, first.line);
        QCOMPARE(window.tabManager->getCurrentEditor()->textCursor().positionInBlock() + 1, first.column);
        auto* track = hotspot->findChild<QGraphicsView*>("signalUsageHotspotTrackView"); QVERIFY(track);
        bool markerFound = false;
        for (auto* item : track->scene()->items())
            if (item->data(Qt::UserRole).toString() == "currentEditorLine" && item->isVisible()) markerFound = true;
        QVERIFY(markerFound);
        capture(window, prefix + "-track-inline");
        auto* originalParent = hotspot->parentWidget();
        auto* expand = hotspot->findChild<QToolButton*>("signalUsageHotspotMainAreaButton"); QVERIFY(expand);
        expand->click();
        QTRY_VERIFY(hotspot->isInMainArea());
        QVERIFY(hotspot->parentWidget() != originalParent);
        QVERIFY(window.tabManager->toolPage("hotspot"));
        QCOMPARE(hotspot->focusSearchText(), QStringLiteral("sig"));
        QCOMPARE(hotspot->selectedItemIndexForTest(), selection);
        capture(window, prefix + "-main-area");
        expand->click();
        QTRY_VERIFY(!hotspot->isInMainArea());
        QTRY_COMPARE(hotspot->parentWidget(), originalParent);
        QTRY_VERIFY(!layout->isBottomCollapsed());
        QCOMPARE(hotspot->selectedItemIndexForTest(), selection);
        QVERIFY(!window.tabManager->toolPage("hotspot"));
        // Persist only the panel's presentation projection and target.
        const auto saved = layout->layoutState();
        hotspot->setFocusSearchText("no-match");
        layout->restoreLayoutState(saved);
        QTRY_VERIFY(!hotspot->reportBuildInFlightForTest());
        QCOMPARE(hotspot->focusSearchText(), QStringLiteral("sig"));
        QCOMPARE(hotspot->selectedItemIndexForTest(), selection);
        const auto legacy = LiveInsightsContextProvider::resourceForKind(LiveInsightKind::Hotspot, workspace,
            {{"target", QVariantMap{{"fileName", file}, {"moduleName", "scope_mod"}, {"signalName", "sig"}}}});
        QVERIFY(context->openResource(legacy));
        QVERIFY(!context->viewForResource(legacy.stableKey()));
        QCOMPARE(dynamic_cast<SignalUsageHotspotPanel*>(window.findChild<QWidget*>("signalUsageHotspotPanel")), hotspot);
        hotspot->clearTarget();
        ContextWorkspaceState legacyState;
        legacyState.valid = true;
        legacyState.pinnedResources = {legacy.toVariantMap()};
        const auto migrated = context->restoreState(legacyState);
        QCOMPARE(migrated.restoredResources, 1);
        QCOMPARE(migrated.skippedResources, 0);
        QTRY_VERIFY(!hotspot->reportBuildInFlightForTest() && hotspot->matrixItemCountForTest() > 0);
        QVERIFY(context->captureState().pinnedResources.isEmpty());
        QCOMPARE(layout->layoutState().bottomPanelViewStates.value("hotspot").value("signal").toString(), QStringLiteral("sig"));
        const QString otherFile = workspace + "/other.sv";
        QVERIFY(write(otherFile, "module other; endmodule\n"));
        QVERIFY(window.tabManager->openFileInTab(otherFile));
        QApplication::processEvents();
        for (auto* item : track->scene()->items())
            if (item->data(Qt::UserRole).toString() == "currentEditorLine") QVERIFY(!item->isVisible());
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
        QTRY_VERIFY(hotspot->currentDeclarationDisplayNameForTest().isEmpty());
        QTRY_VERIFY(!hotspot->reportBuildInFlightForTest());
        QVERIFY(hotspot->trackClustersForTest().isEmpty());
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
