#include "applicationthememanager.h"
#include "contextresource.h"
#include "liveinsightscontextprovider.h"
#include "liveinsightscontextview.h"
#include "liveinsightsession.h"

#include <QApplication>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>

#include <utility>

namespace {
LiveInsightRequestKey requestKey(LiveInsightKind kind)
{
    LiveInsightRequestKey key;
    key.kind = kind;
    key.workspaceId = QStringLiteral("workspace-a");
    key.documentId = QStringLiteral("document-a");
    key.documentRevision = 9;
    key.semanticRevision = 7;
    key.contextKey = QStringLiteral("top/dut");
    return key;
}
}

class LiveInsightsContextProviderTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void providerResourcesAreStableAndSwitchable();
    void compactViewExposesFollowPinAndFullViewSemantics();
    void inactiveCardStaysDirtyAndStatusTracksTheme();
    void followEditorFreezesAndCatchesUpPerView();
};

void LiveInsightsContextProviderTest::initTestCase()
{
    qRegisterMetaType<ContextResource>();
    qRegisterMetaType<LiveInsightKind>();
}

void LiveInsightsContextProviderTest::providerResourcesAreStableAndSwitchable()
{
    LiveInsightsContextProvider provider;
    QCOMPARE(provider.providerId(), QStringLiteral("liveInsights"));
    QCOMPARE(provider.displayName(), QStringLiteral("Live Insights"));
    QVERIFY(provider.session());

    const ContextResource activation =
        provider.activationResource(QStringLiteral("workspace-a"));
    QVERIFY(activation.isValid());
    QCOMPARE(activation.workspaceId, QStringLiteral("workspace-a"));
    QCOMPARE(activation.resourceId, QStringLiteral("primary"));
    LiveInsightKind parsed = LiveInsightKind::Wave;
    QVERIFY(LiveInsightsContextProvider::kindFromResource(
        activation, &parsed));
    QCOMPARE(parsed, LiveInsightKind::Module);

    const QString stableKey = activation.stableKey();
    const QList<LiveInsightKind> kinds = {
        LiveInsightKind::Module,
        LiveInsightKind::State,
        LiveInsightKind::Hotspot,
        LiveInsightKind::Wave
    };
    for (LiveInsightKind kind : kinds) {
        const ContextResource resource =
            LiveInsightsContextProvider::resourceForKind(
                kind, QStringLiteral("workspace-a"));
        QVERIFY(provider.canOpen(resource));
        QCOMPARE(resource.stableKey(), stableKey);
        QVERIFY(LiveInsightsContextProvider::kindFromResource(
            resource, &parsed));
        QCOMPARE(parsed, kind);
    }

    ContextResource malformed = activation;
    malformed.uri = QUrl(
        QStringLiteral("zeroslack://live-insights/unknown"));
    QVERIFY(!provider.canOpen(malformed));
    const ContextViewCapabilities capabilities =
        provider.capabilities(activation);
    QVERIFY(capabilities.supports(ContextPresentation::Peek));
    QVERIFY(capabilities.supports(ContextPresentation::Pinned));
    QVERIFY(capabilities.supports(ContextPresentation::FullView));
    QVERIFY(capabilities.minimumWidth <= capabilities.preferredWidth);
    QVERIFY(capabilities.preferredWidth <= capabilities.maximumWidth);
}

void LiveInsightsContextProviderTest::compactViewExposesFollowPinAndFullViewSemantics()
{
    LiveInsightSession session;
    LiveInsightsContextProvider provider(&session);
    int fullViewCallbacks = 0;
    int pinCallbacks = 0;
    ContextResource callbackResource;
    provider.setFullViewHandler(
        [&fullViewCallbacks, &callbackResource](
            const ContextResource& resource) {
            ++fullViewCallbacks;
            callbackResource = resource;
        });
    provider.setPinRequestHandler(
        [&pinCallbacks, &callbackResource](
            bool,
            const ContextResource& resource) {
            ++pinCallbacks;
            callbackResource = resource;
        });
    QSignalSpy fullViewSpy(
        &provider,
        &LiveInsightsContextProvider::openFullViewRequested);
    QSignalSpy pinSpy(
        &provider,
        &LiveInsightsContextProvider::pinStateChangeRequested);

    ContextResource initial =
        LiveInsightsContextProvider::resourceForKind(
            LiveInsightKind::Module,
            QStringLiteral("workspace-a"));
    QWidget host;
    auto* view = qobject_cast<LiveInsightsContextView*>(
        provider.createView(initial, &host));
    QVERIFY(view);
    QVERIFY(provider.activateView(view, initial));
    view->show();
    QCoreApplication::processEvents();
    QCOMPARE(view->selectedKind(), LiveInsightKind::Module);
    QVERIFY(view->followEditor());
    QVERIFY(!view->pinned());

    ContextResource observed;
    int resourceUpdates = 0;
    QObject observer;
    provider.observeViewResourceChanges(
        view,
        &observer,
        [&observed, &resourceUpdates](
            const ContextResource& resource) {
            observed = resource;
            ++resourceUpdates;
        });

    view->kindButton(LiveInsightKind::Hotspot)->click();
    QCOMPARE(view->selectedKind(), LiveInsightKind::Hotspot);
    QVERIFY(resourceUpdates >= 1);
    LiveInsightKind observedKind = LiveInsightKind::Module;
    QVERIFY(LiveInsightsContextProvider::kindFromResource(
        observed, &observedKind));
    QCOMPARE(observedKind, LiveInsightKind::Hotspot);

    view->followEditorCheckBox()->setChecked(false);
    QVERIFY(!view->followEditor());
    QVERIFY(resourceUpdates >= 2);

    view->pinButton()->click();
    QVERIFY(view->pinned());
    QCOMPARE(view->pinButton()->text(), QStringLiteral("Pinned"));
    QCOMPARE(pinCallbacks, 1);
    QCOMPARE(pinSpy.size(), 1);
    QCOMPARE(callbackResource.workspaceId,
             QStringLiteral("workspace-a"));

    view->openFullViewButton()->click();
    QCOMPARE(fullViewCallbacks, 1);
    QCOMPARE(fullViewSpy.size(), 1);
    QVERIFY(LiveInsightsContextProvider::kindFromResource(
        callbackResource, &observedKind));
    QCOMPARE(observedKind, LiveInsightKind::Hotspot);

    const QVariantMap saved = provider.saveViewState(view);
    QCOMPARE(saved.value(QStringLiteral("kind")).toString(),
             QStringLiteral("hotspot"));
    QCOMPARE(saved.value(QStringLiteral("followEditor")).toBool(),
             false);
    QCOMPARE(saved.value(QStringLiteral("pinned")).toBool(), true);

    const ContextResource persisted =
        provider.resourceForPersistence(
            observed, view, QStringLiteral("workspace-a"));
    QVERIFY(persisted.workspaceId.isEmpty());
    const ContextResource restored =
        provider.resourceFromPersistence(
            persisted, QStringLiteral("workspace-b"));
    QCOMPARE(restored.workspaceId, QStringLiteral("workspace-b"));
    QVERIFY(provider.canOpen(restored));
}

void LiveInsightsContextProviderTest::inactiveCardStaysDirtyAndStatusTracksTheme()
{
    LiveInsightSession session;
    QList<LiveInsightSession::Task> tasks;
    session.setTaskExecutor(
        [&tasks](LiveInsightSession::Task task) {
            tasks.append(std::move(task));
        });
    session.setBuilder(
        LiveInsightKind::State,
        [](const LiveInsightBuildRequest& request,
           const LiveInsightCancellationToken&) {
            return LiveInsightBuildResult::success(
                request,
                {{QStringLiteral("summary"),
                  QStringLiteral("Four states · six transitions")}});
        });

    LiveInsightsContextProvider provider(&session);
    const ContextResource initial =
        LiveInsightsContextProvider::resourceForKind(
            LiveInsightKind::Module,
            QStringLiteral("workspace-a"));
    auto* view = qobject_cast<LiveInsightsContextView*>(
        provider.createView(initial, nullptr));
    QVERIFY(view);
    QVERIFY(provider.activateView(view, initial));
    view->resize(420, 500);
    view->show();
    QTest::qWait(10);
    QVERIFY(session.isVisible(LiveInsightKind::Module));
    QVERIFY(!session.isVisible(LiveInsightKind::State));

    session.requestUpdate(requestKey(LiveInsightKind::State));
    QCOMPARE(session.snapshot(LiveInsightKind::State).phase,
             LiveInsightPhase::HiddenDirty);
    QCOMPARE(view->kindStatusLabel(LiveInsightKind::State)->text(),
             QStringLiteral("Update pending"));
    QVERIFY(tasks.isEmpty());

    view->kindButton(LiveInsightKind::State)->click();
    QVERIFY(session.isVisible(LiveInsightKind::State));
    QVERIFY(!session.isVisible(LiveInsightKind::Module));
    session.flushPending(LiveInsightKind::State);
    QCOMPARE(tasks.size(), 1);
    LiveInsightSession::Task task = tasks.takeFirst();
    task();
    QTRY_COMPARE(session.snapshot(LiveInsightKind::State).phase,
                 LiveInsightPhase::Ready);
    QCOMPARE(view->kindStatusLabel(LiveInsightKind::State)->text(),
             QStringLiteral("Current"));
    QCOMPARE(view->kindSummaryLabel(LiveInsightKind::State)->text(),
             QStringLiteral("Four states · six transitions"));

    ApplicationThemeManager& themes =
        ApplicationThemeManager::instance();
    const ThemeMode originalMode = themes.mode();
    themes.setMode(ThemeMode::Light);
    QCoreApplication::processEvents();
    const QString lightStyle =
        view->kindStatusLabel(LiveInsightKind::State)->styleSheet();
    themes.setMode(ThemeMode::Dark);
    QCoreApplication::processEvents();
    const QString darkStyle =
        view->kindStatusLabel(LiveInsightKind::State)->styleSheet();
    QVERIFY(!lightStyle.isEmpty());
    QVERIFY(!darkStyle.isEmpty());
    QVERIFY(lightStyle != darkStyle);
    themes.setMode(originalMode);

    delete view;
    QVERIFY(!session.isVisible(LiveInsightKind::State));
}

void LiveInsightsContextProviderTest::followEditorFreezesAndCatchesUpPerView()
{
    LiveInsightSession session;
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
                  request.input.value(QStringLiteral("summary"))}});
        });

    LiveInsightsContextProvider provider(&session);
    const ContextResource resource =
        LiveInsightsContextProvider::resourceForKind(
            LiveInsightKind::Module,
            QStringLiteral("workspace-a"));
    auto* view = qobject_cast<LiveInsightsContextView*>(
        provider.createView(resource, nullptr));
    QVERIFY(view);
    QVERIFY(provider.activateView(view, resource));
    view->show();
    QCoreApplication::processEvents();

    session.requestUpdate(
        requestKey(LiveInsightKind::Module),
        {{QStringLiteral("summary"), QStringLiteral("revision 9")}});
    session.flushPending(LiveInsightKind::Module);
    QCOMPARE(tasks.size(), 1);
    tasks.takeFirst()();
    QTRY_COMPARE(
        view->kindSummaryLabel(LiveInsightKind::Module)->text(),
        QStringLiteral("revision 9"));

    view->setFollowEditor(false);
    LiveInsightRequestKey newer = requestKey(LiveInsightKind::Module);
    newer.documentRevision = 10;
    session.requestUpdate(
        newer,
        {{QStringLiteral("summary"), QStringLiteral("revision 10")}});
    session.flushPending(LiveInsightKind::Module);
    QVERIFY(tasks.isEmpty());
    QCOMPARE(session.snapshot(LiveInsightKind::Module).phase,
             LiveInsightPhase::HiddenDirty);
    QCOMPARE(
        view->kindSummaryLabel(LiveInsightKind::Module)->text(),
        QStringLiteral("revision 9"));

    view->setFollowEditor(true);
    session.flushPending(LiveInsightKind::Module);
    QCOMPARE(tasks.size(), 1);
    tasks.takeFirst()();
    QTRY_COMPARE(session.snapshot(LiveInsightKind::Module).phase,
                 LiveInsightPhase::Ready);
    QCOMPARE(
        view->kindSummaryLabel(LiveInsightKind::Module)->text(),
        QStringLiteral("revision 10"));
    delete view;
}

QTEST_MAIN(LiveInsightsContextProviderTest)

#include "live_insights_context_provider_test.moc"
