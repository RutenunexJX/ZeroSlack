#include "liveinsightsession.h"

#include <QElapsedTimer>
#include <QObject>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTest>

#include <thread>
#include <utility>
#include <vector>

namespace {
LiveInsightRequestKey requestKey(
    LiveInsightKind kind,
    quint64 documentRevision,
    quint64 semanticRevision,
    const QString& context = QStringLiteral("top/dut"))
{
    LiveInsightRequestKey key;
    key.kind = kind;
    key.workspaceId = QStringLiteral("workspace-a");
    key.documentId = QStringLiteral("document-a");
    key.documentRevision = documentRevision;
    key.semanticRevision = semanticRevision;
    key.contextKey = context;
    return key;
}

void runTask(LiveInsightSession::Task task)
{
    QVERIFY(task);
    task();
    QCoreApplication::processEvents();
}
}

class LiveInsightSessionTest final : public QObject
{
    Q_OBJECT

private slots:
    void requestKeyRoundTrips();
    void boundedDebounceCoalescesContinuousEdits();
    void idempotentVisibilityDoesNotRepublish();
    void latestGenerationWinsAgainstRunningWorker();
    void hiddenChannelStaysDirtyUntilVisible();
    void lastValidResultSurvivesFailureAndMalformedPayload();
    void destroyingHostCancelsQueuedWorkSafely();
};

void LiveInsightSessionTest::requestKeyRoundTrips()
{
    const LiveInsightRequestKey original = requestKey(
        LiveInsightKind::Hotspot, 41, 17, QStringLiteral("dut/u_rx"));
    bool valid = false;
    const LiveInsightRequestKey restored =
        LiveInsightRequestKey::fromVariantMap(
            original.toVariantMap(), &valid);
    QVERIFY(valid);
    QCOMPARE(restored, original);

    QVariantMap malformed = original.toVariantMap();
    malformed.remove(QStringLiteral("workspaceId"));
    LiveInsightRequestKey::fromVariantMap(malformed, &valid);
    QVERIFY(!valid);
}

void LiveInsightSessionTest::boundedDebounceCoalescesContinuousEdits()
{
    LiveInsightSession session;
    QObject visibleHost;
    session.setConsumerVisible(
        &visibleHost, LiveInsightKind::Module, true);

    QList<LiveInsightSession::Task> tasks;
    session.setTaskExecutor(
        [&tasks](LiveInsightSession::Task task) {
            tasks.append(std::move(task));
        });
    session.setBuilder(
        LiveInsightKind::Module,
        [](const LiveInsightBuildRequest& request,
           const LiveInsightCancellationToken&) {
            return LiveInsightBuildResult::success(
                request,
                {{QStringLiteral("summary"),
                  request.input.value(QStringLiteral("text"))}});
        });

    QElapsedTimer elapsed;
    elapsed.start();
    session.requestUpdate(
        requestKey(LiveInsightKind::Module, 1, 1),
        {{QStringLiteral("text"), QStringLiteral("first")}});
    QTest::qWait(100);
    session.requestUpdate(
        requestKey(LiveInsightKind::Module, 2, 2),
        {{QStringLiteral("text"), QStringLiteral("second")}});
    QTest::qWait(100);
    const quint64 latestGeneration = session.requestUpdate(
        requestKey(LiveInsightKind::Module, 3, 3),
        {{QStringLiteral("text"), QStringLiteral("latest")}});

    QTRY_COMPARE_WITH_TIMEOUT(tasks.size(), 1, 450);
    QVERIFY2(elapsed.elapsed() >= 240,
             "debounce dispatched before the 250 ms edit boundary");
    QVERIFY2(elapsed.elapsed() <= 420,
             "continuous edits exceeded the bounded debounce window");
    QCOMPARE(session.snapshot(LiveInsightKind::Module)
                 .requestedGeneration,
             latestGeneration);

    runTask(tasks.takeFirst());
    QTRY_COMPARE(session.snapshot(LiveInsightKind::Module).phase,
                 LiveInsightPhase::Ready);
    QCOMPARE(session.snapshot(LiveInsightKind::Module)
                 .payload.value(QStringLiteral("summary")).toString(),
             QStringLiteral("latest"));
}

void LiveInsightSessionTest::idempotentVisibilityDoesNotRepublish()
{
    LiveInsightSession session;
    QObject primaryHost;
    QObject secondaryHost;
    QSignalSpy snapshots(
        &session, &LiveInsightSession::snapshotChanged);

    session.setConsumerVisible(
        &primaryHost, LiveInsightKind::Kernel, true);
    QCOMPARE(snapshots.size(), 1);
    snapshots.clear();

    session.setConsumerVisible(
        &primaryHost, LiveInsightKind::Kernel, true);
    session.setConsumerVisible(
        &secondaryHost, LiveInsightKind::Kernel, true);
    session.setConsumerVisible(
        &primaryHost, LiveInsightKind::Kernel, false);
    QCOMPARE(snapshots.size(), 0);

    session.setConsumerVisible(
        &secondaryHost, LiveInsightKind::Kernel, false);
    QCOMPARE(snapshots.size(), 1);
}

void LiveInsightSessionTest::latestGenerationWinsAgainstRunningWorker()
{
    LiveInsightSession session;
    QObject visibleHost;
    session.setConsumerVisible(
        &visibleHost, LiveInsightKind::State, true);

    QSemaphore oldStarted;
    QSemaphore releaseOld;
    std::vector<std::thread> workers;
    session.setTaskExecutor(
        [&workers](LiveInsightSession::Task task) {
            workers.emplace_back(std::move(task));
        });
    session.setBuilder(
        LiveInsightKind::State,
        [&oldStarted, &releaseOld](
            const LiveInsightBuildRequest& request,
            const LiveInsightCancellationToken&) {
            const QString value =
                request.input.value(QStringLiteral("value")).toString();
            if (value == QStringLiteral("old")) {
                oldStarted.release();
                releaseOld.acquire();
            }
            return LiveInsightBuildResult::success(
                request,
                {{QStringLiteral("summary"), value}});
        });

    session.requestUpdate(
        requestKey(LiveInsightKind::State, 10, 10),
        {{QStringLiteral("value"), QStringLiteral("old")}});
    session.flushPending(LiveInsightKind::State);
    QVERIFY(oldStarted.tryAcquire(1, 1000));

    session.requestUpdate(
        requestKey(LiveInsightKind::State, 11, 11),
        {{QStringLiteral("value"), QStringLiteral("new")}});
    session.flushPending(LiveInsightKind::State);
    QTRY_COMPARE_WITH_TIMEOUT(
        session.snapshot(LiveInsightKind::State).phase,
        LiveInsightPhase::Ready,
        1000);
    QCOMPARE(session.snapshot(LiveInsightKind::State)
                 .payload.value(QStringLiteral("summary")).toString(),
             QStringLiteral("new"));

    releaseOld.release();
    for (std::thread& worker : workers) {
        if (worker.joinable())
            worker.join();
    }
    QCoreApplication::processEvents();
    QCOMPARE(session.snapshot(LiveInsightKind::State).phase,
             LiveInsightPhase::Ready);
    QCOMPARE(session.snapshot(LiveInsightKind::State)
                 .payload.value(QStringLiteral("summary")).toString(),
             QStringLiteral("new"));
    QCOMPARE(session.snapshot(LiveInsightKind::State)
                 .publishedKey.documentRevision,
             quint64(11));
}

void LiveInsightSessionTest::hiddenChannelStaysDirtyUntilVisible()
{
    LiveInsightSession session;
    QList<LiveInsightSession::Task> tasks;
    session.setTaskExecutor(
        [&tasks](LiveInsightSession::Task task) {
            tasks.append(std::move(task));
        });
    session.setBuilder(
        LiveInsightKind::Hotspot,
        [](const LiveInsightBuildRequest& request,
           const LiveInsightCancellationToken&) {
            return LiveInsightBuildResult::success(
                request,
                {{QStringLiteral("summary"), QStringLiteral("hotspot")}});
        });

    session.requestUpdate(
        requestKey(LiveInsightKind::Hotspot, 5, 5));
    QCOMPARE(session.snapshot(LiveInsightKind::Hotspot).phase,
             LiveInsightPhase::HiddenDirty);
    QVERIFY(session.hasPendingUpdate(LiveInsightKind::Hotspot));
    QTest::qWait(330);
    QVERIFY(tasks.isEmpty());

    QObject visibleHost;
    session.setConsumerVisible(
        &visibleHost, LiveInsightKind::Hotspot, true);
    QTRY_COMPARE_WITH_TIMEOUT(tasks.size(), 1, 400);
    runTask(tasks.takeFirst());
    QTRY_COMPARE(session.snapshot(LiveInsightKind::Hotspot).phase,
                 LiveInsightPhase::Ready);
    QVERIFY(!session.snapshot(LiveInsightKind::Hotspot).dirty);
}

void LiveInsightSessionTest::lastValidResultSurvivesFailureAndMalformedPayload()
{
    LiveInsightSession session;
    QObject visibleHost;
    session.setConsumerVisible(
        &visibleHost, LiveInsightKind::Kernel, true);
    QList<LiveInsightSession::Task> tasks;
    session.setTaskExecutor(
        [&tasks](LiveInsightSession::Task task) {
            tasks.append(std::move(task));
        });

    session.setBuilder(
        LiveInsightKind::Kernel,
        [](const LiveInsightBuildRequest& request,
           const LiveInsightCancellationToken&) {
            return LiveInsightBuildResult::success(
                request,
                {{QStringLiteral("summary"), QStringLiteral("last valid")}});
        });
    session.requestUpdate(requestKey(LiveInsightKind::Kernel, 1, 1));
    session.flushPending(LiveInsightKind::Kernel);
    runTask(tasks.takeFirst());
    QTRY_COMPARE(session.snapshot(LiveInsightKind::Kernel).phase,
                 LiveInsightPhase::Ready);

    session.setBuilder(
        LiveInsightKind::Kernel,
        [](const LiveInsightBuildRequest& request,
           const LiveInsightCancellationToken&) {
            return LiveInsightBuildResult::failure(
                request, QStringLiteral("syntax is incomplete"));
        });
    session.requestUpdate(requestKey(LiveInsightKind::Kernel, 2, 2));
    session.flushPending(LiveInsightKind::Kernel);
    runTask(tasks.takeFirst());
    QTRY_COMPARE(session.snapshot(LiveInsightKind::Kernel).phase,
                 LiveInsightPhase::Error);
    QVERIFY(session.snapshot(LiveInsightKind::Kernel).stale);
    QVERIFY(session.snapshot(LiveInsightKind::Kernel).hasLastValid);
    QCOMPARE(session.snapshot(LiveInsightKind::Kernel)
                 .payload.value(QStringLiteral("summary")).toString(),
             QStringLiteral("last valid"));
    QCOMPARE(session.snapshot(LiveInsightKind::Kernel).errorText,
             QStringLiteral("syntax is incomplete"));

    session.setBuilder(
        LiveInsightKind::Kernel,
        [](const LiveInsightBuildRequest&,
           const LiveInsightCancellationToken&) {
            return LiveInsightBuildResult{};
        });
    session.requestUpdate(requestKey(LiveInsightKind::Kernel, 3, 3));
    session.flushPending(LiveInsightKind::Kernel);
    runTask(tasks.takeFirst());
    QTRY_COMPARE(session.snapshot(LiveInsightKind::Kernel).phase,
                 LiveInsightPhase::Error);
    QVERIFY(session.snapshot(LiveInsightKind::Kernel).stale);
    QVERIFY(session.snapshot(LiveInsightKind::Kernel)
                .errorText.contains(QStringLiteral("malformed")));
    QCOMPARE(session.snapshot(LiveInsightKind::Kernel)
                 .payload.value(QStringLiteral("summary")).toString(),
             QStringLiteral("last valid"));
}

void LiveInsightSessionTest::destroyingHostCancelsQueuedWorkSafely()
{
    QList<LiveInsightSession::Task> tasks;
    bool builderCalled = false;
    LiveInsightSession session;
    auto* visibleHost = new QObject;
    session.setConsumerVisible(
        visibleHost, LiveInsightKind::Module, true);
    session.setTaskExecutor(
        [&tasks](LiveInsightSession::Task task) {
            tasks.append(std::move(task));
        });
    session.setBuilder(
        LiveInsightKind::Module,
        [&builderCalled](const LiveInsightBuildRequest& request,
                         const LiveInsightCancellationToken&) {
            builderCalled = true;
            return LiveInsightBuildResult::success(request, {});
        });
    session.requestUpdate(
        requestKey(LiveInsightKind::Module, 1, 1));
    session.flushPending(LiveInsightKind::Module);
    QCOMPARE(tasks.size(), 1);

    delete visibleHost;
    QCOMPARE(session.snapshot(LiveInsightKind::Module).phase,
             LiveInsightPhase::HiddenDirty);
    runTask(tasks.takeFirst());
    QVERIFY(!builderCalled);

    QObject secondHost;
    auto* disposableSession = new LiveInsightSession;
    disposableSession->setConsumerVisible(
        &secondHost, LiveInsightKind::Module, true);
    disposableSession->setTaskExecutor(
        [&tasks](LiveInsightSession::Task task) {
            tasks.append(std::move(task));
        });
    disposableSession->setBuilder(
        LiveInsightKind::Module,
        [&builderCalled](const LiveInsightBuildRequest& request,
                         const LiveInsightCancellationToken&) {
            builderCalled = true;
            return LiveInsightBuildResult::success(request, {});
        });
    disposableSession->requestUpdate(
        requestKey(LiveInsightKind::Module, 2, 2));
    disposableSession->flushPending(LiveInsightKind::Module);
    QCOMPARE(tasks.size(), 1);
    delete disposableSession;
    runTask(tasks.takeFirst());
    QVERIFY(!builderCalled);
}

QTEST_GUILESS_MAIN(LiveInsightSessionTest)

#include "live_insight_session_test.moc"
