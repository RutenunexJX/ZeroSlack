#include "suiteappintegration.h"
#include "zeroslackcli.h"
#include "mainwindow.h"
#include "tabmanager.h"
#include "testuistyle.h"
#include "version.h"
#include "semanticindex.h"
#include "semanticindexsnapshot.h"
#include "semanticstableidentity.h"
#include "slangmanager.h"
#include "workspacemanager.h"
#include "workspacesessioncoordinator.h"
#include "activitylogservice.h"
#include <zeroslack/documents/documentfileread.h>
#include <zeroslack/ui/applicationwindow.h>

#include <suiteapp/client.h>
#include <suiteapp/provider.h>
#include <suiteapp/runtime.h>

#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QPlainTextEdit>
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
#include <limits>

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

QString sourceUri(const QString& path, int line = 2, int column = 3)
{
    QUrl uri;
    uri.setScheme(QStringLiteral("zeroslack"));
    uri.setHost(QStringLiteral("source"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("file"), path);
    query.addQueryItem(QStringLiteral("line"), QString::number(line));
    query.addQueryItem(QStringLiteral("column"), QString::number(column));
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

    void rejectsContradictoryTargets_data()
    {
        QTest::addColumn<int>("variant");
        QTest::newRow("foreign-uri-with-file") << 0;
        QTest::newRow("conflicting-file") << 1;
        QTest::newRow("missing-symbol-with-file") << 2;
        QTest::newRow("fractional-line") << 3;
        QTest::newRow("zero-uri-line") << 4;
        QTest::newRow("relative-file") << 5;
        QTest::newRow("conflicting-line") << 6;
        QTest::newRow("oversized-line") << 7;
        QTest::newRow("duplicate-line") << 8;
        QTest::newRow("string-column") << 9;
        QTest::newRow("empty-file-argument") << 10;
        QTest::newRow("fractional-uri-line") << 11;
    }

    void rejectsContradictoryTargets()
    {
        QFETCH(int, variant);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const auto previousDirectory = QDir::currentPath();
        const auto restoreDirectory = qScopeGuard([&] { QDir::setCurrent(previousDirectory); });
        QVERIFY(QDir::setCurrent(fixture.path()));
        const QString a = fixture.filePath("a.sv"), b = fixture.filePath("b.sv");
        QVERIFY(writeFile(a, "module a;\nendmodule\n"));
        QVERIFY(writeFile(b, "module b;\nendmodule\n"));
        MainWindow window;
        QVERIFY(window.tabManager->openFileInTab(a));
        auto* editor = window.tabManager->getCurrentEditor();
        QTextCursor cursor = editor->textCursor();
        cursor.setPosition(2); cursor.setPosition(5, QTextCursor::KeepAnchor);
        editor->setTextCursor(cursor);
        const auto before = window.tabManager->getDocumentForEditor(editor);
        const auto undoSteps = editor->document()->availableUndoSteps();
        const QString endpoint = uniqueEndpoint();
        OwnedRuntime runtime; QVERIFY(runtime.launch(endpoint));
        ZeroSlackSuiteIntegration integration(&window);
        SuiteApp::RuntimeStartOptions options; options.endpoint = endpoint; options.startIfMissing = false;
        QVERIFY(integration.start(options));
        QString uri = sourceUri(a);
        QJsonObject arguments;
        if (variant == 0) { uri = "foreign://source"; arguments.insert("filePath", b); }
        if (variant == 1) arguments.insert("filePath", b);
        if (variant == 2) { uri = "zeroslack://symbol/missing-symbol"; arguments.insert("filePath", b); }
        if (variant == 3) arguments.insert("line", 1.5);
        if (variant == 4) uri.replace("line=2", "line=0");
        if (variant == 5) { uri.clear(); arguments.insert("filePath", "b.sv"); }
        if (variant == 6) arguments.insert("line", 1);
        if (variant == 7) arguments.insert("line", 2147483648.0);
        if (variant == 8) uri += "&line=3";
        if (variant == 9) arguments.insert("column", "3");
        if (variant == 10) arguments.insert("filePath", "");
        if (variant == 11) uri.replace("line=2", "line=2.5");
        const auto reply = whileServing([&] {
            return SuiteApp::Client(endpoint, 1500).invokeAction("zeroslack.source.reveal", arguments, uri, "zeroslack");
        });
        QVERIFY2(reply.hasResponse(), qPrintable(reply.errorMessage));
        QVERIFY2(!reply.response.value("ok").toBool(), QJsonDocument(reply.response).toJson().constData());
        QCOMPARE(window.tabManager->getCurrentEditor(), editor);
        const auto after = window.tabManager->getDocumentForEditor(editor);
        QCOMPARE(after.text, before.text); QCOMPARE(after.textVersion, before.textVersion);
        QCOMPARE(after.dirty, before.dirty); QCOMPARE(window.tabManager->editorCount(), 1);
        QCOMPARE(editor->textCursor().anchor(), cursor.anchor());
        QCOMPARE(editor->textCursor().position(), cursor.position());
        QCOMPARE(editor->document()->availableUndoSteps(), undoSteps);
        if (variant != 2)
            QCOMPARE(reply.response.value("error").toObject().value("code").toString(),
                     QStringLiteral("invalid_resource"));
        QVERIFY(window.close());
    }

    void rejectsUnreachableCoordinates_data()
    {
        QTest::addColumn<int>("line"); QTest::addColumn<int>("column");
        QTest::newRow("past-last-line") << 100 << 1;
        QTest::newRow("past-line-column") << 1 << 100;
        QTest::newRow("maximum-line") << std::numeric_limits<int>::max() << 1;
    }

    void rejectsUnreachableCoordinates()
    {
        QFETCH(int, line); QFETCH(int, column);
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        const QString file = fixture.filePath("source.sv");
        QVERIFY(writeFile(file, "module example;\nendmodule\n"));
        MainWindow window;
        const QString endpoint = uniqueEndpoint();
        OwnedRuntime runtime; QVERIFY(runtime.launch(endpoint));
        ZeroSlackSuiteIntegration integration(&window);
        SuiteApp::RuntimeStartOptions options; options.endpoint = endpoint; options.startIfMissing = false;
        QVERIFY(integration.start(options));
        const auto reply = whileServing([&] {
            return SuiteApp::Client(endpoint, 1500).invokeAction("zeroslack.source.reveal",
                {{"filePath", file}, {"line", line}, {"column", column}});
        });
        QVERIFY2(reply.hasResponse(), qPrintable(reply.errorMessage));
        QCOMPARE(reply.response.value("error").toObject().value("code").toString(),
                 QStringLiteral("source_position_unavailable"));
        QCOMPARE(window.tabManager->editorCount(), 0);
        const auto query = whileServing([&] {
            return SuiteApp::Client(endpoint, 1500).resolveResource(sourceUri(file, line, column));
        });
        QVERIFY(query.response.value("ok").toBool());
        QVERIFY(!query.response.value("result").toObject().value("positionAvailable").toBool());
        QVERIFY(window.close());
    }

    void unavailableSourceDoesNotOpenOrCreate()
    {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        const QString file = fixture.filePath("missing.sv");
        const QString endpoint = uniqueEndpoint();
        OwnedRuntime runtime; QVERIFY(runtime.launch(endpoint));
        MainWindow window;
        ZeroSlackSuiteIntegration integration(&window);
        SuiteApp::RuntimeStartOptions options; options.endpoint = endpoint; options.startIfMissing = false;
        QVERIFY(integration.start(options));
        const SuiteApp::Client client(endpoint, 1500);
        for (const QString& method : {QStringLiteral("resource.resolve"), QStringLiteral("surface.describe"),
                                      QStringLiteral("action.invoke"), QStringLiteral("surface.open")}) {
            const auto reply = whileServing([&] {
                return client.request(method, {{"resourceUri", sourceUri(file, 1, 1)}, {"providerId", "zeroslack"},
                    {"actionId", "zeroslack.source.reveal"}, {"surfaceId", "zeroslack.source.preview"}});
            });
            QVERIFY2(reply.hasResponse(), qPrintable(reply.errorMessage));
            QCOMPARE(reply.response.value("error").toObject().value("code").toString(),
                     QStringLiteral("source_unavailable"));
            QCOMPARE(window.tabManager->editorCount(), 0);
            QVERIFY(!QFileInfo::exists(file));
        }
        QVERIFY(writeFile(file, "module recovered; endmodule\n"));
        const auto retry = whileServing([&] { return client.openSurface("zeroslack.source.preview", sourceUri(file, 1, 1)); });
        QVERIFY(retry.response.value("ok").toBool());
        QCOMPARE(window.tabManager->editorCount(), 1);
        QVERIFY(window.close());
    }

    void sourceIdentityAndEncodingFollowDocument()
    {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        const QString file = fixture.filePath("encoded.sv"), renamed = fixture.filePath("renamed.sv");
        const QString text = QString::fromUtf8("// 中文\nmodule encoded;\nendmodule\n");
        DocumentFileFormat format; format.encoding = QStringConverter::Utf16LE;
        format.byteOrderMark = true; format.lineEnding = "\r\n";
        const QByteArray bytes = encodeDocumentText(text, format);
        QVERIFY(writeFile(file, bytes));
        const QString endpoint = uniqueEndpoint();
        OwnedRuntime runtime; QVERIFY(runtime.launch(endpoint));
        MainWindow window;
        ZeroSlackSuiteIntegration integration(&window);
        SuiteApp::RuntimeStartOptions options; options.endpoint = endpoint; options.startIfMissing = false;
        QVERIFY(integration.start(options));
        const SuiteApp::Client client(endpoint, 1500);
        const auto resolve = [&](const QString& path) {
            return whileServing([&] { return client.resolveResource(sourceUri(path, 2, 1)); });
        };
        const auto disk = resolve(file);
        QVERIFY(disk.response.value("ok").toBool());
        const auto diskModel = disk.response.value("result").toObject();
        QCOMPARE(diskModel.value("snippet").toString(), text);
        QCOMPARE(diskModel.value("source").toString(), QStringLiteral("disk"));
        QCOMPARE(window.tabManager->editorCount(), 0);
        QString alias = fixture.path() + "/./encoded.sv";
#ifdef Q_OS_WIN
        alias = alias.toUpper();
#endif
        const auto open = whileServing([&] {
            return client.invokeAction("zeroslack.source.reveal", {{"filePath", alias}, {"line", 2}, {"column", 1}}, sourceUri(file, 2, 1));
        });
        QVERIFY2(open.response.value("ok").toBool(), QJsonDocument(open.response).toJson().constData());
        auto* editor = window.tabManager->getCurrentEditor(); QVERIFY(editor);
        const auto before = window.tabManager->getDocumentForEditor(editor);
        QCOMPARE(before.text, text);
        const auto document = resolve(alias).response.value("result").toObject();
        QCOMPARE(document.value("snippet").toString(), text);
        QCOMPARE(document.value("source").toString(), QStringLiteral("document"));
        QCOMPARE(document.value("contentSha256"), diskModel.value("contentSha256"));
        QCOMPARE(document.value("textVersion").toInt(), before.textVersion);
        for (int repeat = 0; repeat < 2; ++repeat) {
            const auto reply = whileServing([&] { return client.openSurface("zeroslack.source.preview", sourceUri(alias, 2, 1)); });
            QVERIFY(reply.response.value("ok").toBool());
        }
        QCOMPARE(window.tabManager->editorCount(), 1);
        QCOMPARE(window.tabManager->getDocumentForEditor(editor).textVersion, before.textVersion);
        QVERIFY(QFile::rename(file, renamed));
        editor->setDocumentFileName(renamed);
        QCOMPARE(resolve(file).response.value("error").toObject().value("code").toString(), QStringLiteral("source_unavailable"));
        QCOMPARE(resolve(renamed).response.value("result").toObject().value("snippet").toString(), text);
        QVERIFY(QFile::remove(renamed));
        const auto deletedOpen = resolve(renamed).response.value("result").toObject();
        QCOMPARE(deletedOpen.value("snippet").toString(), text);
        QVERIFY(!deletedOpen.value("exists").toBool());
        QVERIFY(!window.tabManager->getDocumentForEditor(editor).dirty);
        int closeDecisions = 0;
        window.tabManager->unsavedDocumentManagerForTesting()->setDecisionProvider(
            [&](const auto&, QWidget*) {
                ++closeDecisions;
                return UnsavedDocumentBatchDecision::DiscardAll;
            });
        QVERIFY(window.tabManager->closeAllTabs());
        qInfo() << "Deleted source remained readable until explicit fixture close; pending-document decisions:" << closeDecisions;
        QCOMPARE(resolve(renamed).response.value("error").toObject().value("code").toString(), QStringLiteral("source_unavailable"));
        QVERIFY(window.close());
    }

    void revealUsesLogicalSourceCoordinates()
    {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        const QString file = fixture.filePath("wrapped.sv");
        const QString text = "// " + QString(600, QLatin1Char('a')) + "\n" + QString::fromUtf8("// 😀abc") + "\nmodule sample; endmodule\n";
        QVERIFY(writeFile(file, text.toUtf8()));
        const QString endpoint = uniqueEndpoint();
        OwnedRuntime runtime; QVERIFY(runtime.launch(endpoint));
        auto applicationWindow = ZeroSlack::createApplicationWindow();
        auto* window = qobject_cast<MainWindow*>(applicationWindow.get()); QVERIFY(window);
        QVERIFY(ZeroSlack::openApplicationDocument(*applicationWindow, file));
        auto* editor = window->tabManager->getCurrentEditor(); QVERIFY(editor);
        QCOMPARE(editor->lineWrapMode(), QPlainTextEdit::NoWrap);
        window->resize(700, 400); window->show();
        QTest::qWait(10);
        const auto before = window->tabManager->getDocumentForEditor(editor);
        ZeroSlackSuiteIntegration integration(applicationWindow.get());
        SuiteApp::RuntimeStartOptions options; options.endpoint = endpoint; options.startIfMissing = false;
        QVERIFY(integration.start(options));
        const auto reply = whileServing([&] {
            return SuiteApp::Client(endpoint, 1500).invokeAction("zeroslack.source.reveal", {}, sourceUri(file, 2, 6));
        });
        QVERIFY2(reply.response.value("ok").toBool(), QJsonDocument(reply.response).toJson().constData());
        QCOMPARE(editor->textCursor().blockNumber(), 1);
        QCOMPARE(editor->textCursor().positionInBlock(), 5);
        const auto after = window->tabManager->getDocumentForEditor(editor);
        QCOMPARE(after.text, before.text); QCOMPARE(after.textVersion, before.textVersion);
        QCOMPARE(after.dirty, before.dirty);
        QVERIFY(window->close());
    }

    void rejectsStaleSymbolSource_data()
    {
        QTest::addColumn<int>("variant");
        QTest::newRow("dirty-source") << 0;
        QTest::newRow("changed-disk") << 1;
        QTest::newRow("deleted-disk") << 2;
    }

    void rejectsStaleSymbolSource()
    {
        QFETCH(int, variant);
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        const QString file = fixture.filePath("source.sv");
        const QString text = "module example;\nlogic payload;\nendmodule\n";
        QVERIFY(writeFile(file, text.toUtf8()));
        SlangManager slang;
        const auto records = slang.extractSymbolRecords(file, text);
        SemanticSymbolRecord payload;
        for (const auto& record : records) if (record.name == "payload") payload = record;
        QVERIFY(payload.isValid());
        MainWindow window;
        if (variant == 0) {
            QVERIFY(window.tabManager->openFileInTab(file));
            QTextCursor cursor(window.tabManager->getCurrentEditor()->document());
            cursor.insertText("// inserted\n");
        } else if (variant == 1) {
            QVERIFY(writeFile(file, ("// inserted\n" + text).toUtf8()));
        } else {
            QVERIFY(QFile::remove(file));
        }
        SemanticIndex::getInstance()->setSnapshot(std::make_shared<const SemanticIndexSnapshot>(
            SemanticIndexSnapshot::fromSymbolRecords(records, {}, {}, {{file, text}})));
        const auto clear = qScopeGuard([] { SemanticIndex::getInstance()->clearSemanticState(); });
        const QString endpoint = uniqueEndpoint();
        OwnedRuntime runtime; QVERIFY(runtime.launch(endpoint));
        ZeroSlackSuiteIntegration integration(&window);
        SuiteApp::RuntimeStartOptions options; options.endpoint = endpoint; options.startIfMissing = false;
        QVERIFY(integration.start(options));
        const auto reply = whileServing([&] {
            return SuiteApp::Client(endpoint, 1500).resolveResource(semanticStableSymbolUri(payload, fixture.path()));
        });
        QVERIFY2(reply.hasResponse(), qPrintable(reply.errorMessage));
        QCOMPARE(reply.response.value("error").toObject().value("code").toString(),
                 variant == 2 ? QStringLiteral("source_unavailable") : QStringLiteral("symbol_source_stale"));
        if (variant == 0) window.tabManager->getCurrentEditor()->undo();
        else QVERIFY(writeFile(file, text.toUtf8()));
        const auto recovered = whileServing([&] {
            return SuiteApp::Client(endpoint, 1500).resolveResource(semanticStableSymbolUri(payload, fixture.path()));
        });
        QVERIFY2(recovered.response.value("ok").toBool(), QJsonDocument(recovered.response).toJson().constData());
        if (variant == 0) {
            window.tabManager->getCurrentEditor()->redo();
            const QString changed = "// inserted\n" + text;
            const auto currentRecords = slang.extractSymbolRecords(file, changed);
            SemanticIndex::getInstance()->setSnapshot(std::make_shared<const SemanticIndexSnapshot>(
                SemanticIndexSnapshot::fromSymbolRecords(currentRecords, {}, {}, {{file, changed}})));
            const auto refreshed = whileServing([&] {
                return SuiteApp::Client(endpoint, 1500).invokeAction("zeroslack.symbol.reveal", {},
                    semanticStableSymbolUri(payload, fixture.path()));
            });
            QVERIFY2(refreshed.response.value("ok").toBool(), QJsonDocument(refreshed.response).toJson().constData());
            QCOMPARE(window.tabManager->getCurrentEditor()->textCursor().blockNumber(), 2);
            window.tabManager->getCurrentEditor()->document()->setModified(false);
        }
    }

    void registrationReflectsBrokerLossAndCanRetry()
    {
        const QString endpoint = uniqueEndpoint();
        OwnedRuntime runtime; QVERIFY(runtime.launch(endpoint));
        ZeroSlackSuiteIntegration integration(nullptr);
        SuiteApp::RuntimeStartOptions options; options.endpoint = endpoint; options.startIfMissing = false;
        QVERIFY(integration.start(options));
        runtime.kill(); QVERIFY(runtime.waitForFinished(2000));
        QVERIFY(!integration.isRegistered());
        QVERIFY(runtime.launch(endpoint));
        QVERIFY(integration.start(options));
        QVERIFY(integration.isRegistered());
        const auto reply = SuiteApp::Client(endpoint, 500).listProviders();
        QCOMPARE(reply.response.value("result").toObject().value("providers").toArray().size(), 1);
    }

    void symbolWorkspaceIsBoundToActiveWorkspace()
    {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        const QString workspace = fixture.filePath("workspace");
        const QString otherWorkspace = fixture.filePath("other");
        QVERIFY(QDir().mkpath(workspace)); QVERIFY(QDir().mkpath(otherWorkspace));
        const QString file = workspace + "/source.sv";
        const QString text = "module example; logic payload; endmodule\n";
        QVERIFY(writeFile(file, text.toUtf8()));
        const QString endpoint = uniqueEndpoint();
        OwnedRuntime runtime; QVERIFY(runtime.launch(endpoint));
        MainWindow window;
        window.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
        QVERIFY(window.workspaceManager->openWorkspace(workspace));
        QTRY_VERIFY_WITH_TIMEOUT(!window.workspaceManager->isWorkspaceScanActive(), 5000);
        SlangManager slang;
        const auto records = slang.extractSymbolRecords(file, text);
        SemanticSymbolRecord payload;
        for (const auto& record : records) if (record.name == "payload") payload = record;
        QVERIFY(payload.isValid());
        SemanticIndex::getInstance()->setSnapshot(std::make_shared<const SemanticIndexSnapshot>(
            SemanticIndexSnapshot::fromSymbolRecords(records, {}, {}, {{file, text}})));
        const auto clear = qScopeGuard([] { SemanticIndex::getInstance()->clearSemanticState(); });
        ZeroSlackSuiteIntegration integration(&window);
        SuiteApp::RuntimeStartOptions options; options.endpoint = endpoint; options.startIfMissing = false;
        QVERIFY(integration.start(options));
        const SuiteApp::Client client(endpoint, 1500);
        QUrl wrong(semanticStableSymbolUri(payload, workspace));
        wrong.setPath("/" + semanticStableIdentity(payload, workspace).exactId);
        QUrlQuery query; query.addQueryItem("workspace", otherWorkspace); wrong.setQuery(query);
        const auto rejected = whileServing([&] { return client.resolveResource(wrong.toString(QUrl::FullyEncoded)); });
        QCOMPARE(rejected.response.value("error").toObject().value("code").toString(), QStringLiteral("workspace_mismatch"));
        QCOMPARE(window.workspaceManager->getWorkspacePath(), workspace);
        const auto valid = whileServing([&] { return client.resolveResource(semanticStableSymbolUri(payload, workspace)); });
        QVERIFY2(valid.response.value("ok").toBool(), QJsonDocument(valid.response).toJson().constData());
        QVERIFY(window.close());
    }

    void queuedRequestsObserveWorkspaceAndShutdown()
    {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        const QString first = fixture.filePath("first"), second = fixture.filePath("second");
        const QString file = first + "/source.sv", otherFile = second + "/other.sv";
        const QString text = "module example; logic payload; endmodule\n";
        QVERIFY(writeFile(file, text.toUtf8()));
        QVERIFY(writeFile(otherFile, "module other; endmodule\n"));
        const QString endpoint = uniqueEndpoint();
        OwnedRuntime runtime; QVERIFY(runtime.launch(endpoint));
        auto window = std::make_unique<MainWindow>();
        window->workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
        auto* sessions = window->findChild<WorkspaceSessionCoordinator*>();
        QVERIFY(sessions && sessions->openWorkspace(first));
        QTRY_VERIFY_WITH_TIMEOUT(!window->workspaceManager->isWorkspaceScanActive(), 5000);
        SlangManager slang;
        const auto records = slang.extractSymbolRecords(file, text);
        SemanticSymbolRecord payload;
        for (const auto& record : records) if (record.name == "payload") payload = record;
        QVERIFY(payload.isValid());
        SemanticIndex::getInstance()->setSnapshot(std::make_shared<const SemanticIndexSnapshot>(
            SemanticIndexSnapshot::fromSymbolRecords(records, {}, {}, {{file, text}})));
        const auto clear = qScopeGuard([] { SemanticIndex::getInstance()->clearSemanticState(); });
        auto integration = std::make_unique<ZeroSlackSuiteIntegration>(window.get());
        SuiteApp::RuntimeStartOptions options; options.endpoint = endpoint; options.startIfMissing = false;
        QVERIFY(integration->start(options));
        const SuiteApp::Client client(endpoint, 1500);
        const auto descriptor = client.listProviders().response.value("result").toObject()
            .value("providers").toArray().first().toObject();
        const QString providerEndpoint = SuiteApp::descriptorEndpoint(descriptor);
        QVERIFY(!providerEndpoint.isEmpty());
        const QString symbolUri = semanticStableSymbolUri(payload, first);
        const auto initial = whileServing([&] { return client.resolveResource(symbolUri); });
        QVERIFY(initial.response.value("ok").toBool());

        const auto enqueue = [&](QLocalSocket& socket, const QJsonObject& request) {
            socket.connectToServer(providerEndpoint);
            if (!QTest::qWaitFor([&] { return socket.state() == QLocalSocket::ConnectedState; }, 1000)) return false;
            const QByteArray bytes = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
            if (socket.write(bytes) != bytes.size()) return false;
            socket.flush();
            // A Windows pipe write may remain pending until the peer reads.
            // Keep it queued while the GUI performs the lifecycle transition.
            return socket.state() == QLocalSocket::ConnectedState;
        };
        // Connect the real Provider pipe, then write without dispatching GUI events.
        // This fixes the interleaving at the public transport boundary.
        QLocalSocket switching;
        const auto request = SuiteApp::makeRequest("resource.resolve", {{"resourceUri", symbolUri}}, "r4-switch");
        QVERIFY2(enqueue(switching, request), qPrintable(switching.errorString()));
        QCOMPARE(switching.bytesAvailable(), 0);
        QVERIFY(sessions->openWorkspace(second));
        QCOMPARE(window->workspaceManager->getWorkspacePath(), second);
        QTRY_VERIFY_WITH_TIMEOUT(switching.canReadLine(), 3000);
        const auto reply = QJsonDocument::fromJson(switching.readLine()).object();
        QString reason;
        QVERIFY2(SuiteApp::validateResponse(reply, "r4-switch", &reason), qPrintable(reason));
        QCOMPARE(reply.value("error").toObject().value("code").toString(), QStringLiteral("workspace_mismatch"));
        const auto recovered = whileServing([&] { return client.resolveResource(sourceUri(otherFile, 1, 1)); });
        QVERIFY(recovered.response.value("ok").toBool());
        QVERIFY(recovered.response.value("result").toObject().value("snippet").toString().contains("module other"));

        window->show();
        auto* activityDock = window->findChild<QDockWidget*>("activityDock");
        QVERIFY(activityDock);
        activityDock->show();
        QCoreApplication::processEvents();
        QPointer<QPlainTextEdit> output = window->findChild<QPlainTextEdit*>("activityOutputText");
        QVERIFY(output);
        QLocalSocket closing;
        QVERIFY2(enqueue(closing, SuiteApp::makeRequest("resource.resolve", {{"resourceUri", sourceUri(otherFile, 1, 1)}})), qPrintable(closing.errorString()));
        QCOMPARE(closing.bytesAvailable(), 0);
        ActivityLogService::getInstance()->append("R4 lifecycle", ActivityLogLevel::Info, "queued before actual window teardown");
        QVERIFY(window->close());
        // Match main(): Provider is destroyed before its MainWindow.
        integration.reset();
        window.reset();
        QVERIFY(output.isNull());
        ActivityLogService::getInstance()->append("R4 lifecycle", ActivityLogLevel::Info, "after actual window teardown");
        QTRY_VERIFY_WITH_TIMEOUT(closing.state() == QLocalSocket::UnconnectedState, 3000);
        QVERIFY(runtime.waitForFinished(6000));
        QCOMPARE(runtime.exitCode(), 0);
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
            return client.openSurface("zeroslack.source.preview", sourceUri(file, 1, 1));
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
        descriptor.insert("displayName", "Tickx contract test fixture");
        descriptor.insert("resourceSchemes", QJsonArray{"wave"});
        descriptor.insert("actions", QJsonArray{});
        descriptor.insert("surfaces", QJsonArray{});
        int requests = 0;
        bool reject = false;
        int conflictingIdentity = 0;
        int delayMs = 0;
        SuiteApp::Provider fixtureProvider(descriptor, [&](const QJsonObject& request) {
            ++requests;
            if (delayMs) QTest::qSleep(delayMs);
            if (reject)
                return SuiteApp::errorResponse(request, "fixture_rejected", "Controlled provider failure");
            if (conflictingIdentity)
                return SuiteApp::successResponse(request, conflictingIdentity == 1
                    ? QJsonObject{{"appId", "unrelated"}, {"title", "Wrong owner"}}
                    : QJsonObject{{"appId", "wave"}, {"uri", "wave://project?file=unrelated"}, {"title", "Wrong resource"}});
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
        for (const int conflict : {1, 2}) {
            conflictingIdentity = conflict;
            const auto mismatched = execute();
            QCOMPARE(mismatched.exitCode, 0);
            QCOMPARE(onlyProviderPayload(mismatched).value("nativeResolvedCount").toInt(), 0);
            QVERIFY(QJsonDocument(mismatched.envelope).toJson().contains("provider_identity_mismatch"));
            const auto retained = onlyProviderPayload(mismatched).value("items").toArray().first().toObject();
            QCOMPARE(retained.value("title").toString(), QStringLiteral("Local timing"));
            QVERIFY(!retained.contains("nativeModel"));
        }
        conflictingIdentity = 0;
        reject = true;
        const auto rejected = execute();
        QCOMPARE(rejected.exitCode, 0);
        const auto rejectedPayload = onlyProviderPayload(rejected);
        QCOMPARE(rejectedPayload.value("nativeResolvedCount").toInt(), 0);
        QVERIFY(!rejectedPayload.value("items").toArray().isEmpty());
        QVERIFY(QJsonDocument(rejected.envelope).toJson().contains("fixture_rejected"));
        reject = false;
        delayMs = 1800;
        QElapsedTimer elapsed; elapsed.start();
        const auto timedOut = execute();
        QCOMPARE(timedOut.exitCode, 0);
        QCOMPARE(onlyProviderPayload(timedOut).value("nativeResolvedCount").toInt(), 0);
        QVERIFY(QJsonDocument(timedOut.envelope).toJson().contains("timeout"));
        QVERIFY(!onlyProviderPayload(timedOut).value("items").toArray().isEmpty());
        QVERIFY(elapsed.elapsed() < 4500);
        qInfo() << "Slow provider retained local metadata, test event loop elapsed ms:" << elapsed.elapsed();
        delayMs = 0;
        const auto recovered = execute();
        QCOMPARE(recovered.exitCode, 0);
        QCOMPARE(onlyProviderPayload(recovered).value("nativeResolvedCount").toInt(), 1);
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
