// Compile the actual private transport in this isolated target so IPC tests
// can use a unique local endpoint without starting the installed Pinloom.
#include "../src/integrations/pinloom/pinloomhostclient.cpp"
#include <QLocalServer>
#include <QtTest/QTest>
#include <cstdio>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    int failures = 0;
    auto check = [&](bool condition, const char* name) {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", name);
        if (!condition) ++failures;
    };
    const QString serverName = "zeroslack-test-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject envelope{{"protocol", kProtocol}, {"requestId", "test-request"}, {"method", "search"}, {"params", QJsonObject{}}};
    auto disconnected = exchangeRequest(envelope, {}, {}, serverName);
    check(disconnected.error.contains("not running"), "missing endpoint reports no connection without launching an app");
    QLocalServer server;
    check(server.listen(serverName), "isolated local endpoint opens");
    int connections = 0;
    enum class Mode { Silent, Mismatch, Oversize, Valid, Delayed } mode = Mode::Silent;
    QObject::connect(&server, &QLocalServer::newConnection, &server, [&] {
        auto* socket = server.nextPendingConnection();
        ++connections;
        QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        auto received = std::make_shared<QByteArray>();
        QObject::connect(socket, &QLocalSocket::readyRead, socket, [&, socket, received] {
            received->append(socket->readAll());
            if (!received->contains('\n')) return;
            if (mode == Mode::Silent) return;
            if (mode == Mode::Oversize) { socket->write(QByteArray(kMaximumResponseBytes + 1, 'x')); return; }
            const auto request = QJsonDocument::fromJson(received->trimmed()).object();
            QJsonObject reply{{"protocol", kProtocol}, {"requestId", mode == Mode::Mismatch ? QJsonValue("wrong") : request.value("requestId")},
                {"ok", true}, {"result", QJsonObject{{"complete", true}}}};
            const auto bytes = QJsonDocument(reply).toJson(QJsonDocument::Compact) + '\n';
            if (mode == Mode::Delayed)
                QTimer::singleShot(2100, socket, [socket, bytes] { socket->write(bytes); });
            else socket->write(bytes);
        });
    });
    auto token = std::make_shared<std::atomic_bool>(false);
    auto future = QtConcurrent::run([&] { return exchangeRequest(envelope, {}, token, serverName); });
    check(QTest::qWaitFor([&] { return connections == 1; }, 1000), "read enters real socket wait");
    QElapsedTimer cancellation;
    cancellation.start();
    token->store(true);
    check(QTest::qWaitFor([&] { return future.isFinished(); }, 1000)
        && future.result().error.contains("cancelled") && cancellation.elapsed() < 1000,
        "cancellation interrupts the response wait before its full deadline");
    auto run = [&] {
        auto task = QtConcurrent::run([&] { return exchangeRequest(envelope, {}, {}, serverName); });
        check(QTest::qWaitFor([&] { return task.isFinished(); }, 4000), "transport returns within its deadline");
        return task.result();
    };
    const int beforeTimeout = connections;
    QElapsedTimer timeout;
    timeout.start();
    check(run().error.contains("in time") && timeout.elapsed() >= 1500 && connections == beforeTimeout + 1,
        "silent endpoint times out without resending a transmitted request");
    mode = Mode::Mismatch;
    check(run().error.contains("mismatched"), "response identity is checked");
    mode = Mode::Oversize;
    check(run().error.contains("size limit"), "response size remains bounded");
    mode = Mode::Valid;
    check(run().result.value("complete").toBool(), "matching protocol response is accepted");
    mode = Mode::Delayed;
    for (const auto& method : {QStringLiteral("resolve"), QStringLiteral("open")}) {
        envelope.insert(QStringLiteral("method"), method);
        timeout.restart();
        check(run().result.value("complete").toBool() && timeout.elapsed() >= 2000,
              "PDF resolve/open survives the former 1800 ms response deadline");
    }
    mode = Mode::Silent;
    token = std::make_shared<std::atomic_bool>(false);
    const int beforeCancel = connections;
    future = QtConcurrent::run([&] { return exchangeRequest(envelope, {}, token, serverName); });
    check(QTest::qWaitFor([&] { return connections > beforeCancel; }, 1000), "long response request reaches the socket");
    token->store(true);
    check(QTest::qWaitFor([&] { return future.isFinished(); }, 1000)
          && future.result().error.contains("cancelled"), "cancellation interrupts the 30 second response window");
    mode = Mode::Valid;
    const QByteArray previousEndpoint = qgetenv("ZEROSLACK_PINLOOM_HOST_SERVER");
    qputenv("ZEROSLACK_PINLOOM_HOST_SERVER", serverName.toUtf8());
    PinloomHostClient routedClient;
    bool routed = false;
    routedClient.capabilities([&](const QJsonObject& result, const QString& error) {
        routed = error.isEmpty() && result.value("complete").toBool();
    });
    check(QTest::qWaitFor([&] { return routed; }, 2000), "client routes to an explicitly isolated library endpoint");
    if (previousEndpoint.isNull()) qunsetenv("ZEROSLACK_PINLOOM_HOST_SERVER");
    else qputenv("ZEROSLACK_PINLOOM_HOST_SERVER", previousEndpoint);
    server.close();
    return failures == 0 ? 0 : 1;
}
