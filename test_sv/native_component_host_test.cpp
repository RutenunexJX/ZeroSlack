#include "../src/integrations/native/nativecontextview.h"
#include "../src/integrations/xips/xipscontextprovider.h"
#include "../src/integrations/simdock/simdockcontextprovider.h"
#include "../src/integrations/simdock/simdockcontextview.h"
#include <QDialog>
#include "contextworkspacecontroller.h"
#include "contextdockhost.h"
#include "testuistyle.h"
#include "applicationthememanager.h"
#include "workspacemanager.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMainWindow>
#include <QSettings>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

class NativeComponentHostTest : public QObject
{
    Q_OBJECT
    QTemporaryDir settings;
private slots:
    void initTestCase()
    {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        qputenv("ZEROSLACK_SIMDOCK_LEGACY_SETTINGS_PATH", settings.filePath("legacy.ini").toUtf8());
        QVERIFY(initializeUiStyleForTest());
    }
    void init()
    {
        qputenv("XIPS_BROWSER_LIBRARY", NATIVE_FIXTURE_GOOD);
        qputenv("SIMDOCK_WORKBENCH_LIBRARY", NATIVE_FIXTURE_GOOD);
        qunsetenv("NATIVE_FIXTURE_NULL");
        qunsetenv("NATIVE_FIXTURE_BAD_CONTRACT");
    }
    void missingComponentKeepsResourceAndRetriesLatestContext()
    {
        QTemporaryDir directory;
        qputenv("XIPS_BROWSER_LIBRARY", directory.filePath("missing.dll").toUtf8());
        QMainWindow window;
        auto *center = new QWidget(&window);
        window.setCentralWidget(center);
        ContextWorkspaceController controller(&window, center);
        QVERIFY(controller.registerProvider(std::make_unique<XipsContextProvider>(nullptr, nullptr)));
        QVERIFY(controller.openTool(QStringLiteral("xips")));
        auto *host = qobject_cast<NativeContextView *>(controller.viewForResource("xips:library"));
        QVERIFY(host);
        QVERIFY(!host->isReady());
        QVERIFY(!host->property("nativeComponentError").toString().isEmpty());
        QCOMPARE(controller.dockHost()->currentResource().providerId, QStringLiteral("xips"));
        host->setWorkspace(QStringLiteral("latest-workspace"));
        qputenv("XIPS_BROWSER_LIBRARY", NATIVE_FIXTURE_GOOD);
        host->retry();
        QVERIFY2(host->isReady(), qPrintable(host->property("nativeComponentError").toString()));
        QCOMPARE(host->component()->property("workspace").toString(), QStringLiteral("latest-workspace"));
        QVERIFY(controller.closePinnedResource("xips:library"));
    }
    void rejectsBrokenComponentsAndRecovers_data()
    {
        QTest::addColumn<QByteArray>("library");
        QTest::addColumn<QByteArray>("mode");
        QTest::newRow("abi") << QByteArray(NATIVE_FIXTURE_BAD) << QByteArray();
        QTest::newRow("factory-export") << QByteArray(NATIVE_FIXTURE_NO_FACTORY) << QByteArray();
        QTest::newRow("null-widget") << QByteArray(NATIVE_FIXTURE_GOOD) << QByteArray("NATIVE_FIXTURE_NULL");
        QTest::newRow("invokables") << QByteArray(NATIVE_FIXTURE_GOOD) << QByteArray("NATIVE_FIXTURE_BAD_CONTRACT");
    }
    void rejectsBrokenComponentsAndRecovers()
    {
        QFETCH(QByteArray, library);
        QFETCH(QByteArray, mode);
        qputenv("XIPS_BROWSER_LIBRARY", library);
        if (!mode.isEmpty()) qputenv(mode.constData(), "1");
        std::unique_ptr<QWidget> view;
        {
            XipsContextProvider provider(nullptr, nullptr);
            view.reset(provider.createView(provider.activationResource("workspace"), nullptr));
        }
        auto *host = qobject_cast<NativeContextView *>(view.get());
        QVERIFY(host && !host->isReady());
        QVERIFY(!host->property("nativeComponentError").toString().isEmpty());
        // The provider has died. Recovery callbacks are owned by the surface.
        qputenv("XIPS_BROWSER_LIBRARY", NATIVE_FIXTURE_GOOD);
        if (!mode.isEmpty()) qunsetenv(mode.constData());
        host->retry();
        QVERIFY2(host->isReady(), qPrintable(host->property("nativeComponentError").toString()));
    }
    void componentErrorsRetainStateUntilRecovery()
    {
        QTemporaryDir workspace;
        SimDockContextProvider provider;
        auto resource = provider.activationResource(workspace.filePath("missing"));
        resource.state = {{"version", 1}, {"workspace", "stale-root"}, {"projectId", QString()}};
        std::unique_ptr<QWidget> view(provider.createView(resource, nullptr));
        auto *host = qobject_cast<SimDockContextView *>(view.get());
        QVERIFY(host && !host->isReady());
        QCOMPARE(host->saveState(), resource.state);
        resource.workspaceId = workspace.path();
        QVERIFY(provider.activateView(host, resource));
        QVERIFY2(host->isReady(), qPrintable(host->property("nativeComponentError").toString()));
        QCOMPARE(host->saveState().value("workspace").toString(), workspace.path());
        QSignalSpy scans(host->component(), SIGNAL(scanFinished()));
        QTest::qWait(100);
        const int count = scans.size();
        QVERIFY(provider.activateView(host, resource));
        QTest::qWait(100);
        QCOMPARE(scans.size(), count);
        host->restoreState({{"version", 2}});
        QVERIFY(!host->isReady());
        QVERIFY(host->property("nativeComponentError").toString().contains("Unsupported"));
        host->resetSavedState();
        QVERIFY(host->isReady());
    }
    void componentDirectoryResolvesPrivateDependencyWithoutPathOrCwd()
    {
        QTemporaryDir unrelated;
        const auto oldPath = qgetenv("PATH");
        const auto oldDirectory = QDir::currentPath();
        const auto restore = qScopeGuard([oldPath, oldDirectory] {
            qputenv("PATH", oldPath);
            QDir::setCurrent(oldDirectory);
        });
        QVERIFY(QDir::setCurrent(unrelated.path()));
        // Windows launchers can supply both Path and PATH. The CRT removes
        // one case-insensitive environment entry per call.
        for (int i = 0; i < 4 && !qEnvironmentVariableIsEmpty("PATH"); ++i)
            QVERIFY(qunsetenv("PATH"));
        QVERIFY(qEnvironmentVariableIsEmpty("PATH"));
        qputenv("XIPS_BROWSER_LIBRARY", NATIVE_FIXTURE_ISOLATED);
        XipsContextProvider provider(nullptr, nullptr);
        std::unique_ptr<QWidget> view(provider.createView(provider.activationResource("workspace"), nullptr));
        auto *host = qobject_cast<NativeContextView *>(view.get());
        QVERIFY(host);
        QVERIFY2(host->isReady(), qPrintable(host->property("nativeComponentError").toString()));
        QCOMPARE(QDir::currentPath(), unrelated.path());
        QCOMPARE(qgetenv("PATH"), QByteArray());
    }
    void panelsTrackInitialThemeChangesAndRestore_data()
    {
        QTest::addColumn<bool>("simdock");
        QTest::newRow("xips") << false;
    }
    void panelsTrackInitialThemeChangesAndRestore()
    {
        QFETCH(bool, simdock);
        const auto initial = ApplicationThemeManager::instance().mode();
        const auto restore = qScopeGuard([initial] { ApplicationThemeManager::instance().setMode(initial); });
        ApplicationThemeManager::instance().setMode(ThemeMode::Dark);
        XipsContextProvider xips(nullptr, nullptr);
        SimDockContextProvider simulation;
        IContextContentProvider *provider = simdock ? static_cast<IContextContentProvider *>(&simulation) : &xips;
        std::unique_ptr<QWidget> view(provider->createView(provider->activationResource("workspace"), nullptr));
        auto *host = qobject_cast<NativeContextView *>(view.get());
        QVERIFY(host && host->isReady());
        QCOMPARE(host->component()->property("darkTheme").toBool(), true);
        if (simdock) QCOMPARE(host->component()->objectName(), QStringLiteral("SimDockWorkbench"));
        ApplicationThemeManager::instance().setMode(ThemeMode::Light);
        QCOMPARE(host->component()->property("darkTheme").toBool(), false);
        host->restoreState({{"darkTheme", true}});
        QCOMPARE(host->component()->property("darkTheme").toBool(), false);
        ApplicationThemeManager::instance().setMode(ThemeMode::CatppuccinMocha);
        QCOMPARE(host->component()->property("darkTheme").toBool(), true);
        host->restoreState({{"darkTheme", false}});
        QCOMPARE(host->component()->property("darkTheme").toBool(), true);
        host->retire();
        ApplicationThemeManager::instance().setMode(ThemeMode::Light);
        QCOMPARE(host->component()->property("darkTheme").toBool(), true);
    }
    void legacyLibraryOverrideCannotReplaceOwnedWorkbench()
    {
        QTemporaryDir directory;
        qputenv("SIMDOCK_WORKBENCH_LIBRARY", directory.filePath("missing.dll").toUtf8());
        QSettings().setValue("integrations/native/simdockLibrary", directory.filePath("other.dll"));
        SimDockContextProvider provider;
        std::unique_ptr<QWidget> view(provider.createView(provider.activationResource(directory.path()), nullptr));
        auto *host = qobject_cast<SimDockContextView *>(view.get());
        QVERIFY(host && host->isReady());
        QCOMPARE(QByteArray(host->component()->metaObject()->className()), QByteArray("simdock::Workbench"));
        QVERIFY(!host->property("nativeComponentLibrary").isValid());
    }
    void busyPanelSurvivesDockingWorkspaceChangesAndClose()
    {
        QTemporaryDir directory;
        const QString a = directory.filePath("a"), b = directory.filePath("b"), c = directory.filePath("c");
        QVERIFY(QDir().mkpath(a) && QDir().mkpath(b) && QDir().mkpath(c));
        WorkspaceManager manager;
        manager.setRecentWorkspacePersistenceEnabledForTesting(false);
        QVERIFY(manager.openWorkspace(a));
        QVERIFY(manager.openWorkspace(b));
        QMainWindow window;
        auto *center = new QWidget(&window);
        window.setCentralWidget(center);
        ContextWorkspaceController controller(&window, center);
        QVERIFY(controller.setWorkspaceRoot(b));
        connect(&manager, &WorkspaceManager::workspaceActivated, &controller,
                [&controller](int, const QString &, const QString &path) { controller.setWorkspaceRoot(path); });
        manager.setWorkspaceTransitionGuard([&controller] {
            QString error;
            controller.canCloseResources(&error);
            return error;
        });
        auto provider = std::make_unique<SimDockContextProvider>();
        const auto resource = provider->activationResource(b);
        QVERIFY(controller.registerProvider(std::move(provider)));
        QVERIFY(controller.openResource(resource, {ContextSurface::Docked, ContextPersistence::Kept}));
        QPointer<SimDockContextView> host = qobject_cast<SimDockContextView *>(controller.viewForResource(resource.stableKey()));
        QVERIFY(host && host->isReady());
        QPointer<QWidget> native = host->component();
        auto *dialog = new QDialog(native);
        dialog->show();
        QTest::qWait(30);
        QVERIFY(controller.unpinResource(resource.stableKey()));
        QCOMPARE(controller.viewForResource(resource.stableKey()), host.data());
        QVERIFY(!controller.closeFloatingResource(resource.stableKey()));
        QVERIFY(!controller.canCloseResources());
        QVERIFY(!controller.setWorkspaceRoot(a));
        QVERIFY(!manager.switchWorkspace(0));
        QVERIFY(!manager.openWorkspace(c));
        QVERIFY(!manager.closeWorkspace(1));
        QCOMPARE(manager.workspaceEntries().size(), 2);
        QCOMPARE(manager.getWorkspacePath(), b);
        const auto state = controller.captureState();
        QVERIFY(!controller.restoreState(state).warnings.isEmpty());
        QVERIFY(!controller.unregisterProvider("simdock"));
        controller.setActiveDocument("active.sv");
        QVERIFY(controller.setResourceBinding(resource.stableKey(), ContextBinding::DocumentBound));
        controller.documentClosed("active.sv");
        QVERIFY(controller.boundDocument(resource.stableKey()).isEmpty());
        QCOMPARE(controller.viewForResource(resource.stableKey()), host.data());
        QVERIFY(controller.pinFloatingResource(resource.stableKey()));
        QCOMPARE(host->component(), native.data());
        QVERIFY(!controller.closePinnedResource(resource.stableKey()));
        delete dialog;
        QVERIFY(manager.switchWorkspace(0));
        QCOMPARE(controller.workspaceRoot(), a);
        QVERIFY(host->property("nativeComponentRetired").toBool());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!host && !native);
        QVERIFY(controller.openTool("simdock"));
        QVERIFY(qobject_cast<SimDockContextView *>(controller.viewForResource(resource.stableKey()))->isReady());
    }
};
QTEST_MAIN(NativeComponentHostTest)
#include "native_component_host_test.moc"
