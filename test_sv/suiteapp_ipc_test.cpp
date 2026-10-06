#include "suiteappintegration.h"
#include "zeroslackcli.h"
#include "mainwindow.h"
#include "tabmanager.h"
#include "testuistyle.h"
#include "version.h"

#include <suiteapp/client.h>
#include <suiteapp/provider.h>
#include <suiteapp/runtime.h>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QScopeGuard>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>
#include <QtConcurrent>
#include <QtTest>

namespace {
QString uniqueEndpoint()
{
    return QStringLiteral("zeroslack.ipc.%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
}

bool writeFile(const QString& path, const QByteArray& text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly)
        && file.write(text) == text.size();
}

QString sourceUri(const QString& path)
{
    QUrl uri;
    uri.setScheme(QStringLiteral("zeroslack"));
    uri.setHost(QStringLiteral("source"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("file"), path);
    query.addQueryItem(QStringLiteral("line"), QStringLiteral("2"));
    query.addQueryItem(QStringLiteral("column"), QStringLiteral("3"));
    uri.setQuery(query);
    return uri.toString(QUrl::FullyEncoded);
}

// Keep the real GUI/provider event loop responsive while the synchronous
// client waits for the separate Runtime process to route its request.
template<typename Function>
auto whileServing(Function function)
{
    auto future = QtConcurrent::run(std::move(function));
    while (!future.isFinished())
        QTest::qWait(5);
    return future.result();
}

class OwnedRuntime final : public QProcess
{
public:
    ~OwnedRuntime() override
    {
        if (state() != QProcess::NotRunning) {
            terminate();
            if (!waitForFinished(2000)) {
                kill();
                waitForFinished(2000);
            }
        }
    }

    bool launch(const QString& endpoint)
    {
        start(qEnvironmentVariable("SUITEAPP_RUNTIME_EXECUTABLE"),
              {QStringLiteral("--endpoint"), endpoint});
        if (!waitForStarted(3000))
            return false;
        return QTest::qWaitFor([&] {
            const auto reply = SuiteApp::Client(endpoint, 100).listProviders();
            return reply.hasResponse() && reply.response.value("ok").toBool();
        }, 3000);
    }
};

QJsonObject onlyProviderPayload(const ZeroSlackCliResult& result)
{
    const auto providers = result.envelope.value("data").toObject().value("payload")
        .toObject().value("providers").toArray();
    return providers.isEmpty() ? QJsonObject{} : providers.first().toObject();
}
} // namespace

class SuiteAppIpcTest final : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QVERIFY2(QFileInfo::exists(qEnvironmentVariable("SUITEAPP_RUNTIME_EXECUTABLE")),
                 "Set SUITEAPP_RUNTIME_EXECUTABLE to the SDK Runtime executable");
        QVERIFY(initializeUiStyleForTest());
    }

    void providerRoutesLiveDocumentsAndUnregisters()
    {
        const QString endpoint = uniqueEndpoint();
        OwnedRuntime runtime;
        QVERIFY2(runtime.launch(endpoint), qPrintable(runtime.errorString()));
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString file = fixture.filePath("source.sv");
        const QByteArray diskText = "module example;\n  logic signal_a;\nendmodule\n";
        QVERIFY(writeFile(file, diskText));
        const QString uri = sourceUri(file);
        MainWindow window;
        auto integration = std::make_unique<ZeroSlackSuiteIntegration>(&window);
        SuiteApp::RuntimeStartOptions options;
        options.endpoint = endpoint;
        options.startIfMissing = false;
        QString reason;
        QVERIFY2(integration->start(options, &reason), qPrintable(reason));
        QVERIFY(integration->isRegistered());
        QVERIFY(integration->start(options, &reason));
        const SuiteApp::Client client(endpoint, 1500);
        const auto registered = client.listProviders();
        QVERIFY(registered.hasResponse());
        const QJsonArray providers = registered.response.value("result")
            .toObject().value("providers").toArray();
        QCOMPARE(providers.size(), 1);
        const QJsonObject descriptor = providers.first().toObject();
        QCOMPARE(descriptor.value("appId").toString(), QStringLiteral("zeroslack"));
        QCOMPARE(descriptor.value("displayName").toString(), QStringLiteral("ZeroSlack"));
        QCOMPARE(descriptor.value("version").toString(), QString::fromLatin1(APP_VERSION));
        qInfo().noquote() << "Registered actual ZeroSlack provider:" << endpoint
                          << QJsonDocument(descriptor).toJson(QJsonDocument::Compact);

        const auto resolve = [&] {
            return whileServing([&] { return client.resolveResource(uri); });
        };
        const auto disk = resolve();
        QVERIFY(disk.hasResponse());
        QVERIFY(disk.response.value("ok").toBool());
        QCOMPARE(disk.response.value("result").toObject().value("snippet").toString(),
                 QString::fromUtf8(diskText));
        const auto action = whileServing([&] {
            return client.invokeAction("zeroslack.source.reveal", {}, uri);
        });
        QVERIFY2(action.response.value("ok").toBool(),
                 QJsonDocument(action.response).toJson().constData());
        auto* editor = window.tabManager->getDocumentModel()->editorForFile(file);
        QVERIFY(editor);
        QCOMPARE(editor->textCursor().blockNumber(), 1);
        QCOMPARE(editor->textCursor().positionInBlock(), 2);

        QTextCursor cursor(editor->document());
        cursor.select(QTextCursor::Document);
        cursor.removeSelectedText();
        QVERIFY(window.tabManager->getDocumentForEditor(editor).dirty);
        QCOMPARE(resolve().response.value("result").toObject().value("snippet").toString(), QString());
        editor->undo();
        QCOMPARE(resolve().response.value("result").toObject().value("snippet").toString(),
                 QString::fromUtf8(diskText));
        editor->redo();
        const auto surface = whileServing([&] {
            return client.describeSurface("zeroslack.source.preview", uri);
        });
        QVERIFY(surface.response.value("ok").toBool());
        const auto model = surface.response.value("result").toObject();
        QCOMPARE(model.value("mode").toString(), QStringLiteral("model"));
        QCOMPARE(model.value("fallback").toString(), QStringLiteral("external"));
        QCOMPARE(model.value("model").toObject().value("snippet").toString(), QString());
        QVERIFY(window.tabManager->saveEditorView(editor));
        QVERIFY(window.tabManager->closeAllTabs());
        const auto reopened = whileServing([&] {
            return client.openSurface("zeroslack.source.preview", uri);
        });
        QVERIFY(reopened.response.value("ok").toBool());
        QVERIFY(window.tabManager->getDocumentModel()->editorForFile(file));
        QCOMPARE(window.tabManager->getPlainTextFromOpenFile(file), QString());
        const auto invalid = whileServing([&] {
            return client.resolveResource(QStringLiteral("zeroslack://source"));
        });
        QCOMPARE(invalid.response.value("error").toObject().value("code").toString(),
                 QStringLiteral("invalid_resource"));
        const auto unknownAction = whileServing([&] {
            return client.invokeAction("zeroslack.unknown", {}, uri, "zeroslack");
        });
        QVERIFY(!unknownAction.response.value("ok").toBool());
        QVERIFY(!unknownAction.response.value("error").toObject().value("code").toString().isEmpty());
        qInfo() << "IPC resource/action/surface, empty dirty text, undo/redo, save/reopen and rejection passed";
        QVERIFY(window.close());
        integration.reset();
        const auto afterClose = client.listProviders();
        QVERIFY(afterClose.hasResponse());
        QCOMPARE(afterClose.response.value("result").toObject().value("providers").toArray().size(), 0);
        QVERIFY2(runtime.waitForFinished(6000), "Owned Runtime did not exit after provider unregister");
        QCOMPARE(runtime.exitCode(), 0);
    }

    void missingRuntimeDoesNotDisableEditing()
    {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        MainWindow window;
        ZeroSlackSuiteIntegration integration(&window);
        SuiteApp::RuntimeStartOptions options;
        options.endpoint = uniqueEndpoint();
        options.startIfMissing = false;
        options.probeTimeoutMs = 100;
        QString reason;
        QVERIFY(!integration.start(options, &reason));
        QVERIFY(!reason.isEmpty());
        QVERIFY(!integration.isRegistered());
        {
            const QByteArray previousRuntime = qgetenv("SUITEAPP_RUNTIME_EXECUTABLE");
            const QByteArray previousPath = qgetenv("PATH");
            const auto restoreEnvironment = qScopeGuard([&] {
                qputenv("SUITEAPP_RUNTIME_EXECUTABLE", previousRuntime);
                qputenv("PATH", previousPath);
            });
            qputenv("SUITEAPP_RUNTIME_EXECUTABLE", "missing-suite-runtime.exe");
            qputenv("PATH", "");
            options.executablePath = QStringLiteral("missing-suite-runtime.exe");
            options.startIfMissing = true;
            QVERIFY(SuiteApp::locateRuntimeExecutable(options.executablePath).isEmpty());
            QVERIFY(!integration.start(options, &reason));
            QVERIFY(reason.contains(QStringLiteral("could not be located")));
            QVERIFY(!integration.isRegistered());
        }
        const QString file = fixture.filePath("offline.sv");
        QVERIFY(writeFile(file, "module offline; endmodule\n"));
        QVERIFY(window.tabManager->openFileInTab(file));
        QVERIFY(window.tabManager->getCurrentEditor());
        QVERIFY(window.close());
    }

    void startsRuntimeAndReleasesLastProvider()
    {
        const QString endpoint = uniqueEndpoint();
        SuiteApp::RuntimeStartOptions options;
        options.endpoint = endpoint;
        options.executablePath = qEnvironmentVariable("SUITEAPP_RUNTIME_EXECUTABLE");
        auto integration = std::make_unique<ZeroSlackSuiteIntegration>(nullptr);
        QString reason;
        QVERIFY2(integration->start(options, &reason), qPrintable(reason));
        const SuiteApp::Client client(endpoint, 100);
        const auto providers = client.listProviders();
        QVERIFY(providers.hasResponse());
        QCOMPARE(providers.response.value("result").toObject().value("providers").toArray().size(), 1);
        integration.reset();
        QTRY_VERIFY_WITH_TIMEOUT(!client.listProviders().hasResponse(), 6000);
    }

    void cliUsesRuntimeAndPreservesLocalMetadataOnFailure()
    {
        const QString endpoint = uniqueEndpoint();
        OwnedRuntime runtime;
        QVERIFY(runtime.launch(endpoint));
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QVERIFY(writeFile(fixture.filePath("top.sv"), "module top; logic q; endmodule\n"));
        const QByteArray waveText = R"({"schemaVersion":1,"projectId":"ipc-wave","name":"Local timing","scenarios":[]})";
        QVERIFY(writeFile(fixture.filePath("timing.wave.json"), waveText));
        QVERIFY(writeFile(fixture.filePath(".zeroslack/suite-references.json"),
            R"({"schema":"zeroslack.suite-references/v1","resources":[{"id":"ipc-wave","provider":"wave","file":"timing.wave.json","symbols":["q"]}]})"));
        auto descriptor = ZeroSlackSuiteIntegration::appDescriptor("1.0.0", endpoint + ".wave");
        descriptor.insert("appId", "wave");
        descriptor.insert("displayName", "Wave contract test fixture");
        descriptor.insert("resourceSchemes", QJsonArray{"wave"});
        descriptor.insert("actions", QJsonArray{});
        descriptor.insert("surfaces", QJsonArray{});
        int requests = 0;
        bool reject = false;
        SuiteApp::Provider fixtureProvider(descriptor, [&](const QJsonObject& request) {
            ++requests;
            if (reject)
                return SuiteApp::errorResponse(request, "fixture_rejected", "Controlled provider failure");
            return SuiteApp::successResponse(request, {
                {"appId", "wave"}, {"kind", "project"}, {"projectId", "ipc-wave"},
                {"title", "Runtime snapshot"}, {"content", "private fixture content"},
                {"scenarios", QJsonArray{}}});
        });
        QString reason;
        QVERIFY2(fixtureProvider.start(endpoint, &reason), qPrintable(reason));
        ZeroSlackCliRequest request;
        request.command = "suite-context";
        request.workspaceRoot = fixture.path();
        request.cacheDirectory = fixture.filePath("cache");
        request.includedProviders = {QStringLiteral("wave")};
        request.maxTokens = 4000;
        const auto execute = [&] {
            return whileServing([&] { return ZeroSlackCliService({}, endpoint).execute(request); });
        };
        const auto enriched = execute();
        QCOMPARE(enriched.exitCode, 0);
        const auto payload = onlyProviderPayload(enriched);
        QCOMPARE(payload.value("transport").toString(), QStringLiteral("suite-app/v1"));
        QVERIFY(payload.value("nativeResolvedCount").toInt() > 0);
        QVERIFY(requests > 0);
        const auto item = payload.value("items").toArray().first().toObject();
        QCOMPARE(item.value("nativeModel").toObject().value("title").toString(), QStringLiteral("Runtime snapshot"));
        QVERIFY(!item.value("nativeModel").toObject().contains("content"));
        qInfo().noquote() << "Actual CLI service via separate Runtime, fixture foreign provider:"
                          << QJsonDocument(payload).toJson(QJsonDocument::Compact);
        reject = true;
        const auto rejected = execute();
        QCOMPARE(rejected.exitCode, 0);
        const auto rejectedPayload = onlyProviderPayload(rejected);
        QCOMPARE(rejectedPayload.value("nativeResolvedCount").toInt(), 0);
        QVERIFY(!rejectedPayload.value("items").toArray().isEmpty());
        QVERIFY(QJsonDocument(rejected.envelope).toJson().contains("fixture_rejected"));
        fixtureProvider.stop();
        QVERIFY(runtime.waitForFinished(6000));
        const auto offline = execute();
        QCOMPARE(offline.exitCode, 0);
        const auto offlinePayload = onlyProviderPayload(offline);
        QCOMPARE(offlinePayload.value("availability").toString(), QStringLiteral("unavailable"));
        QCOMPARE(offlinePayload.value("transport").toString(), QStringLiteral("local-metadata"));
        QCOMPARE(offlinePayload.value("nativeResolvedCount").toInt(), 0);
        QVERIFY(!offlinePayload.value("items").toArray().isEmpty());
        QVERIFY(!offlinePayload.value("availabilityCode").toString().isEmpty());
        QFile wave(fixture.filePath("timing.wave.json"));
        QVERIFY(wave.open(QIODevice::ReadOnly));
        QCOMPARE(wave.readAll(), waveText);
    }
};

int main(int argc, char** argv)
{
    QStandardPaths::setTestModeEnabled(true);
    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);
    QCoreApplication::setOrganizationName("ZeroSlack");
    QCoreApplication::setApplicationName("ZeroSlack");
    QCoreApplication::setApplicationVersion(QString::fromLatin1(APP_VERSION));
    QTemporaryDir settings;
    if (!settings.isValid())
        return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settings.path());
    qputenv("ZEROSLACK_SESSION_STORAGE_PATH", settings.filePath("sessions.ini").toUtf8());
    SuiteAppIpcTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "suiteapp_ipc_test.moc"
