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
    struct UriCase { const char* uri; PinloomHostIdentity identity; };
    const QList<UriCase> validUris{
        {"pinloom://entry/entry-a", {"entry-a", {}, {}, {}}},
        {"pinloom://entry/anchor:a?resource=r&anchor=a&clip=c", {"anchor:a", "r", "a", "c"}},
        {"pinloom://anchor/anchor-a", {{}, {}, "anchor-a", {}}},
        {"pinloom://anchor/anchor-a?resource=resource-a", {{}, "resource-a", "anchor-a", {}}},
        {"pinloom://clip/clip-a", {{}, {}, {}, "clip-a"}},
        {"PINLOOM://ANCHOR/anchor-a?unknown=ignored&clip=ignored", {{}, {}, "anchor-a", {}}},
        {"pinloom://clip/clip-a?resource=ignored&anchor=ignored", {{}, {}, {}, "clip-a"}},
        {"pinloom://anchor/%E7%AC%94%E8%AE%B0%20a%25+?resource=%E8%B5%84%E6%96%99%20r+", {{}, QString::fromUtf8("资料 r+"), QString::fromUtf8("笔记 a%+"), {}}},
        {"pinloom://entry/e?resource=%20r%20&anchor=%20a%20&clip=c", {"e", "r", "a", "c"}},
        {"pinloom://anchor/a?resource=first&resource=second", {{}, "first", "a", {}}},
    };
    for (const auto& row : validUris) {
        const auto identity = PinloomHostIdentity::fromUri(QUrl(QString::fromUtf8(row.uri), QUrl::StrictMode));
        check(identity.isValid() && identity.toJson() == row.identity.toJson(), qPrintable(QStringLiteral("URI identity: %1").arg(row.uri)));
        check(PinloomHostIdentity::fromUri(identity.toUri()).toJson() == row.identity.toJson(),
              qPrintable(QStringLiteral("URI round-trip: %1").arg(row.uri)));
    }
    for (const auto* uri : {"https://anchor/a", "pinloom://library", "pinloom://other/a", "pinloom://anchor/",
             "pinloom://clip/", "pinloom://entry/", "pinloom://entry/?resource=r", "pinloom://anchor/%20",
             "pinloom://anchor/a/b", "pinloom://clip/a%2Fb", "pinloom://entry/a/b", "pinloom://user@anchor/a",
             "pinloom://anchor:9/a", "pinloom://anchor/a#fragment", "pinloom://anchor/%ZZ"}) {
        check(!PinloomHostIdentity::fromUri(QUrl(QString::fromUtf8(uri), QUrl::StrictMode)).isValid(),
              qPrintable(QStringLiteral("Invalid URI rejected: %1").arg(uri)));
    }
    const PinloomHostIdentity resourceOnly{{}, "resource-a", {}, {}};
    check(resourceOnly.toUri() == QUrl("pinloom://entry/resource:resource-a?resource=resource-a")
          && PinloomHostIdentity::fromUri(resourceOnly.toUri()).resourceId == resourceOnly.resourceId,
          "resource-only identity uses the existing host resource entry spelling");
    const PinloomHostIdentity anchorOnly{{}, {}, "anchor-a", {}};
    check(anchorOnly.isValid() && anchorOnly.toUri() == QUrl("pinloom://anchor/anchor-a"),
          "anchor-only identity is valid and serializes without an empty entry identifier");
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
