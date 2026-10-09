#include "../src/integrations/native/nativecontextview.h"
#include "../src/integrations/xips/xipscontextprovider.h"
#include "../src/integrations/simdock/simdockcontextprovider.h"
#include "../src/integrations/simdock/simdockcontextview.h"
#include "testuistyle.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

// This is deliberately a separate executable. It fails without actual DLLs;
// the controllable fixture cannot count as real integration evidence.
class NativeComponentIntegrationTest : public QObject
{
    Q_OBJECT
    QTemporaryDir settings;
private slots:
    void initTestCase()
    {
        QVERIFY2(!qEnvironmentVariable("XIPS_BROWSER_LIBRARY").isEmpty(), "Set the real xIPs DLL path");
        qputenv("ZEROSLACK_SIMDOCK_LEGACY_SETTINGS_PATH", settings.filePath("legacy.ini").toUtf8());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        QVERIFY(initializeUiStyleForTest());
    }
    void realChildrenPreserveHostAndSurviveRepeatedContexts()
    {
        QTemporaryDir fixture;
        const QString a = fixture.filePath("a"), b = fixture.filePath("b"), library = fixture.filePath("library");
        QVERIFY(QDir().mkpath(a) && QDir().mkpath(b) && QDir().mkpath(library));
        const QByteArray previousPath = qgetenv("PATH");
        const QString previousDirectory = QDir::currentPath();
        const auto restoreProcess = qScopeGuard([previousPath, previousDirectory] {
            qputenv("PATH", previousPath);
            QDir::setCurrent(previousDirectory);
        });
        for (int i = 0; i < 4 && !qEnvironmentVariableIsEmpty("PATH"); ++i)
            QVERIFY(qunsetenv("PATH"));
        QVERIFY(qEnvironmentVariableIsEmpty("PATH"));
        QVERIFY(QDir::setCurrent(fixture.path()));
        qputenv("XIPS_LIBRARY", library.toUtf8());
        QFile source(a + "/uart.sv");
        QVERIFY(source.open(QIODevice::WriteOnly));
        source.write("module uart(input logic clk); endmodule\n");
        source.close();
        const auto font = QApplication::font();
        const auto palette = QApplication::palette();
        const auto appName = QCoreApplication::applicationName();
        const auto organization = QCoreApplication::organizationName();
        XipsContextProvider xips(nullptr, nullptr);
        SimDockContextProvider simdock;
        QWidget parent;
        for (auto *provider : {static_cast<IContextContentProvider *>(&xips), static_cast<IContextContentProvider *>(&simdock)}) {
            std::unique_ptr<QWidget> view(provider->createView(provider->activationResource(a), &parent));
            auto *host = view.get();
            const auto component = [&]() -> QWidget* {
                if (auto* external = qobject_cast<NativeContextView*>(view.get())) return external->component();
                return qobject_cast<SimDockContextView*>(view.get())->component();
            };
            QVERIFY(host);
            QVERIFY2(host->property("nativeComponentReady").toBool(), qPrintable(host->property("nativeComponentError").toString()));
            QVERIFY(component() && component()->parentWidget() == host && !component()->isWindow());
            QCOMPARE(QApplication::font(), font);
            QCOMPARE(QApplication::palette(), palette);
            QCOMPARE(QCoreApplication::applicationName(), appName);
            QCOMPARE(QCoreApplication::organizationName(), organization);
            QTRY_VERIFY_WITH_TIMEOUT(provider->canCloseView(view.get(), nullptr), 30000);
            const auto state = provider->saveViewState(view.get());
            for (const QString &path : {b, a, b, a}) {
                auto resource = provider->activationResource(path);
                resource.state = state;
                QVERIFY(provider->activateView(host, resource));
                QVERIFY2(host->property("nativeComponentReady").toBool(), qPrintable(host->property("nativeComponentError").toString()));
                QTRY_VERIFY_WITH_TIMEOUT(provider->canCloseView(view.get(), nullptr), 30000);
            }
            QVERIFY(provider->activateView(host, provider->activationResource(QString())));
            QVERIFY(host->property("nativeComponentReady").toBool());
            QTRY_VERIFY_WITH_TIMEOUT(provider->canCloseView(view.get(), nullptr), 30000);
            view.reset();
            view.reset(provider->createView(provider->activationResource(a), &parent));
            QVERIFY(view->property("nativeComponentReady").toBool());
            QCOMPARE(QApplication::font(), font);
            QCOMPARE(QApplication::palette(), palette);
        }
        QCOMPARE(qgetenv("PATH"), QByteArray());
        QCOMPARE(QDir::currentPath(), fixture.path());
    }
};
QTEST_MAIN(NativeComponentIntegrationTest)
#include "native_component_integration_test.moc"
