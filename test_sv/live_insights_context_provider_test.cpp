#include "applicationthememanager.h"
#include "contextdockhost.h"
#include "contextresource.h"
#include "contextworkspacecontroller.h"
#include "liveinsightscontextprovider.h"
#include "liveinsightscontextview.h"
#include "liveinsightsession.h"

#include <QApplication>
#include <QCheckBox>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QSet>
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
    void legacyWaveSessionMigratesToDedicatedProvider();
    void compactViewExposesFollowPinAndFullViewSemantics();
    void inactiveCardStaysDirtyAndStatusTracksTheme();
    void followEditorFreezesAndCatchesUpPerView();
    void fixedKindSectionRendersRealSurface();
    void fixedKindSectionPublishesStatusAndTrimsChrome();
    void emptyFixedKindSectionOffersReachableTargets();
    void pickedTargetReachesTheSurfaceAndTheSectionHeader();
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
    QCOMPARE(provider.displayName(), QStringLiteral("RTL Insight Workbench"));
    QVERIFY(provider.session());

    const ContextResource activation =
        provider.activationResource(QStringLiteral("workspace-a"));
    QVERIFY(activation.isValid());
    QCOMPARE(activation.workspaceId, QStringLiteral("workspace-a"));
    QCOMPARE(activation.resourceId, QStringLiteral("primary"));
    LiveInsightKind parsed = LiveInsightKind::Wave;
    QVERIFY(LiveInsightsContextProvider::kindFromResource(
        activation, &parsed));
    QCOMPARE(parsed, LiveInsightKind::Kernel);

    const QList<LiveInsightKind> kinds = {
        LiveInsightKind::Kernel,
        LiveInsightKind::Module,
        LiveInsightKind::State,
        LiveInsightKind::Hotspot,
        LiveInsightKind::Wave
    };
    QSet<QString> stableKeys;
    for (LiveInsightKind kind : kinds) {
        const ContextResource resource =
            LiveInsightsContextProvider::resourceForKind(
                kind, QStringLiteral("workspace-a"));
        QVERIFY(provider.canOpen(resource));
        stableKeys.insert(resource.stableKey());
        QVERIFY(LiveInsightsContextProvider::kindFromResource(
            resource, &parsed));
        QCOMPARE(parsed, kind);
    }
    QCOMPARE(stableKeys.size(), 5);

    const ContextResource wave =
        LiveInsightsContextProvider::resourceForKind(
            LiveInsightKind::Wave,
            QStringLiteral("workspace-a"));
    QVERIFY(provider.canOpen(wave));
    QCOMPARE(wave.providerId, QStringLiteral("rtlInsight.wave"));
    QCOMPARE(wave.resourceId, QStringLiteral("wave"));
    QVERIFY(wave.stableKey() != activation.stableKey());
    QVERIFY(LiveInsightsContextProvider::kindFromResource(wave, &parsed));
    QCOMPARE(parsed, LiveInsightKind::Wave);

    LiveInsightsContextProvider waveProvider(
        LiveInsightKind::Wave, provider.session());
    QCOMPARE(waveProvider.providerId(), QStringLiteral("rtlInsight.wave"));
    QCOMPARE(waveProvider.iconKey(), QStringLiteral("rtl-insight-wave"));
    QVERIFY(waveProvider.canOpen(wave));
    QWidget* waveViewWidget = waveProvider.createView(wave, nullptr);
    auto* waveView =
        qobject_cast<LiveInsightsContextView*>(waveViewWidget);
    QVERIFY(waveView);
    QVERIFY(waveView->hasFixedKind());
    QCOMPARE(waveView->selectedKind(), LiveInsightKind::Wave);
    QVERIFY(waveView->kindButton(LiveInsightKind::Wave));
    delete waveView;

    ContextResource legacyWave = wave;
    legacyWave.providerId = LiveInsightsContextProvider::staticProviderId();
    legacyWave.resourceId = QStringLiteral("primary");
    QVERIFY(provider.canOpen(legacyWave));
    QVERIFY(LiveInsightsContextProvider::kindFromResource(
        legacyWave, &parsed));
    QCOMPARE(parsed, LiveInsightKind::Wave);

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

void LiveInsightsContextProviderTest::
legacyWaveSessionMigratesToDedicatedProvider()
{
    QMainWindow window;
    auto* editorRegion = new QWidget(&window);
    window.setCentralWidget(editorRegion);
    ContextWorkspaceController controller(
        &window, editorRegion, &window);
    LiveInsightSession session;
    QVERIFY(controller.registerProvider(
        std::make_unique<LiveInsightsContextProvider>(
            LiveInsightKind::Wave, &session)));
    controller.setWorkspaceRoot(QStringLiteral("workspace-a"));

    ContextResource legacyWave =
        LiveInsightsContextProvider::resourceForKind(
            LiveInsightKind::Wave);
    legacyWave.providerId =
        LiveInsightsContextProvider::staticProviderId();
    legacyWave.resourceId = QStringLiteral("primary");
    ContextWorkspaceState state;
    state.valid = true;
    state.dockVisible = true;
    state.pinnedResources.append(legacyWave.toVariantMap());
    state.activePinnedResourceKey = legacyWave.stableKey();

    const ContextWorkspaceRestoreResult restored =
        controller.restoreState(state);
    const ContextResource active =
        controller.dockHost()->currentResource();
    QCOMPARE(restored.restoredResources, 1);
    QCOMPARE(restored.skippedResources, 0);
    QCOMPARE(active.providerId, QStringLiteral("rtlInsight.wave"));
    QCOMPARE(active.resourceId, QStringLiteral("wave"));
    QCOMPARE(controller.providerIds(),
             QStringList{QStringLiteral("rtlInsight.wave")});
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
    QCOMPARE(restored.providerId, provider.providerId());
    QCOMPARE(restored.resourceId, QStringLiteral("primary"));
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

void LiveInsightsContextProviderTest::fixedKindSectionRendersRealSurface()
{
    LiveInsightSession session;
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
                {{QStringLiteral("summary"),
                  request.input.value(QStringLiteral("summary"))}});
        });
    const ContextResource resource =
        LiveInsightsContextProvider::resourceForKind(
            LiveInsightKind::Kernel,
            QStringLiteral("workspace-a"));

    // Without a host that can build an editor context the section keeps the
    // compact summary card.
    LiveInsightsContextProvider bare(LiveInsightKind::Kernel, &session);
    auto* bareView = qobject_cast<LiveInsightsContextView*>(
        bare.createView(resource, nullptr));
    QVERIFY(bareView);
    bareView->show();
    QCoreApplication::processEvents();
    QVERIFY(!bareView->surfaceForTest());
    QVERIFY(bareView->kindSummaryLabel(LiveInsightKind::Kernel)
                ->isVisibleTo(bareView));
    delete bareView;

    LiveInsightsContextProvider provider(LiveInsightKind::Kernel, &session);
    int contextRequests = 0;
    provider.setToolContextSource(
        [&contextRequests]() {
            ++contextRequests;
            LiveInsightToolContext context;
            context.workspaceId = QStringLiteral("workspace-a");
            context.documentId = QStringLiteral("document-a");
            context.fileName = QStringLiteral("uart.sv");
            context.moduleName = QStringLiteral("uart");
            context.signalName = QStringLiteral("byte_data");
            context.documentRevision = 9;
            context.semanticRevision = 7;
            return context;
        });
    auto* view = qobject_cast<LiveInsightsContextView*>(
        provider.createView(resource, nullptr));
    QVERIFY(view);
    QVERIFY(provider.activateView(view, resource));
    view->show();
    QCoreApplication::processEvents();

    LiveInsightToolPage* surface = view->surfaceForTest();
    QVERIFY(surface);
    QCOMPARE(surface->kind(), LiveInsightKind::Kernel);
    QVERIFY(surface->workbenchForTest());
    QVERIFY(!view->kindSummaryLabel(LiveInsightKind::Kernel)
                 ->isVisibleTo(view));
    const int afterCreate = contextRequests;
    QVERIFY(afterCreate > 0);

    session.requestUpdate(
        requestKey(LiveInsightKind::Kernel),
        {{QStringLiteral("summary"), QStringLiteral("revision 9")}});
    session.flushPending(LiveInsightKind::Kernel);
    QCOMPARE(tasks.size(), 1);
    tasks.takeFirst()();
    QTRY_VERIFY(contextRequests > afterCreate);
    QCOMPARE(view->surfaceForTest(), surface);

    // Freezing the card freezes the surface with it.
    view->setFollowEditor(false);
    const int frozen = contextRequests;
    LiveInsightRequestKey newer = requestKey(LiveInsightKind::Kernel);
    newer.documentRevision = 10;
    session.requestUpdate(
        newer,
        {{QStringLiteral("summary"), QStringLiteral("revision 10")}});
    session.flushPending(LiveInsightKind::Kernel);
    QCOMPARE(contextRequests, frozen);
    QCOMPARE(view->surfaceForTest(), surface);
    delete view;
}

namespace {
LiveInsightToolContext stubContext(
    const QString& moduleName,
    const QString& signalName)
{
    LiveInsightToolContext context;
    context.workspaceId = QStringLiteral("workspace-a");
    context.documentId = QStringLiteral("document-a");
    context.fileName = QStringLiteral("uart.sv");
    context.moduleName = moduleName;
    context.signalName = signalName;
    context.documentRevision = 9;
    context.semanticRevision = 7;
    return context;
}
}

void LiveInsightsContextProviderTest::
    fixedKindSectionPublishesStatusAndTrimsChrome()
{
    LiveInsightSession session;
    QList<LiveInsightSession::Task> tasks;
    session.setTaskExecutor(
        [&tasks](LiveInsightSession::Task task) {
            tasks.append(std::move(task));
        });
    session.setBuilder(
        LiveInsightKind::Wave,
        [](const LiveInsightBuildRequest& request,
           const LiveInsightCancellationToken&) {
            return LiveInsightBuildResult::success(
                request,
                {{QStringLiteral("summary"), QStringLiteral("scope summary")},
                 {QStringLiteral("provenance"),
                  QStringLiteral("Symbolic Preview")}});
        });
    LiveInsightsContextProvider provider(LiveInsightKind::Wave, &session);
    provider.setToolContextSource(
        []() { return stubContext(QStringLiteral("uart"), QString()); });
    const ContextResource resource =
        LiveInsightsContextProvider::resourceForKind(
            LiveInsightKind::Wave, QStringLiteral("workspace-a"));
    auto* view = qobject_cast<LiveInsightsContextView*>(
        provider.createView(resource, nullptr));
    QVERIFY(view);
    view->show();
    QCoreApplication::processEvents();

    // The section header owns the name, the full-view entry and freshness, so
    // the body drops its copies and keeps only what the header has no room for.
    QVERIFY(!view->kindButton(LiveInsightKind::Wave)->isVisibleTo(view));
    QVERIFY(!view->kindStatusLabel(LiveInsightKind::Wave)->isVisibleTo(view));
    QVERIFY(!view->openFullViewButton()->isVisibleTo(view));
    QVERIFY(view->followEditorCheckBox()->isVisibleTo(view));
    QVERIFY(view->pinButton()->isVisibleTo(view));

    session.requestUpdate(
        requestKey(LiveInsightKind::Wave),
        {{QStringLiteral("summary"), QStringLiteral("scope summary")}});
    session.flushPending(LiveInsightKind::Wave);
    QCOMPARE(tasks.size(), 1);
    tasks.takeFirst()();
    QTRY_VERIFY(view->property("contextStatusTooltip").toString()
                    .contains(QStringLiteral("Symbolic Preview")));
    QVERIFY(!view->property("contextStatusText").toString().isEmpty());
    delete view;
}

void LiveInsightsContextProviderTest::
    emptyFixedKindSectionOffersReachableTargets()
{
    LiveInsightSession session;
    LiveInsightsContextProvider provider(LiveInsightKind::Kernel, &session);
    QString editorSignal = QStringLiteral("byte_data");
    provider.setToolContextSource(
        [&editorSignal]() {
            return stubContext(QStringLiteral("uart"), editorSignal);
        });
    const ContextResource resource =
        LiveInsightsContextProvider::resourceForKind(
            LiveInsightKind::Kernel, QStringLiteral("workspace-a"));
    auto* view = qobject_cast<LiveInsightsContextView*>(
        provider.createView(resource, nullptr));
    QVERIFY(view);
    view->show();
    QCoreApplication::processEvents();
    QVERIFY(view->surfaceForTest());
    QVERIFY(!view->emptyStateForTest()
            || !view->emptyStateForTest()->isVisibleTo(view));

    // The editor loses its signal: the section says what it can still reach
    // instead of leaving an empty frame behind.
    editorSignal.clear();
    view->setFollowEditor(false);
    view->setFollowEditor(true);
    QCoreApplication::processEvents();
    QWidget* empty = view->emptyStateForTest();
    QVERIFY(empty);
    QVERIFY(empty->isVisibleTo(view));
    QVERIFY(!view->surfaceForTest()->isVisibleTo(view));
    const auto candidates = view->candidateTargets();
    QCOMPARE(candidates.size(), 1);
    QCOMPARE(candidates.constFirst().label, QStringLiteral("byte_data"));
    QCOMPARE(empty->findChildren<QPushButton*>(
                 QStringLiteral("liveInsightCandidate")).size(), 1);

    // Choosing a target pins the section to it instead of subscribing to the
    // cursor, so a later editor move cannot silently replace it.
    QVERIFY(view->applyTargetCandidate(candidates.constFirst()));
    QCoreApplication::processEvents();
    QVERIFY(!view->followEditor());
    QVERIFY(view->surfaceForTest()->isVisibleTo(view));
    QVERIFY(!view->emptyStateForTest()->isVisibleTo(view));
    delete view;
}

void LiveInsightsContextProviderTest::
    pickedTargetReachesTheSurfaceAndTheSectionHeader()
{
    LiveInsightSession session;
    LiveInsightsContextProvider provider(LiveInsightKind::Wave, &session);
    provider.setToolContextSource([]() {
        LiveInsightToolContext context =
            stubContext(QStringLiteral("uart"), QString());
        context.scopeLabel = QStringLiteral("always_ff @(posedge clk)");
        context.scopeStartPosition = 10;
        context.scopeEndPosition = 40;
        return context;
    });

    // Stands in for the host: records the request and replies with the target
    // an editor pick would have produced.
    int pickRequests = 0;
    LiveInsightKind requestedKind = LiveInsightKind::Kernel;
    std::function<void(const LiveInsightsContextView::TargetCandidate&)> reply;
    provider.setTargetPickRequest(
        [&](LiveInsightKind kind,
            std::function<void(const LiveInsightsContextView::TargetCandidate&)>
                picked) {
            ++pickRequests;
            requestedKind = kind;
            reply = std::move(picked);
            return true;
        });

    ContextDockHost host;
    const ContextResource resource =
        LiveInsightsContextProvider::resourceForKind(
            LiveInsightKind::Wave, QStringLiteral("workspace-a"));
    auto* view = qobject_cast<LiveInsightsContextView*>(
        provider.createView(resource, nullptr));
    QVERIFY(view);
    QVERIFY(host.addResource(resource, view, true));
    host.resize(520, 600);
    host.show();
    QCoreApplication::processEvents();

    const QString key = resource.stableKey();
    QAbstractButton* chip = host.sectionScope(key);
    QVERIFY(chip);
    QVERIFY(chip->isVisible());
    QCOMPARE(chip->text(), QStringLiteral("always_ff @(posedge clk)"));

    // The header chip is generic: it invokes the view's own slot.
    chip->click();
    QCoreApplication::processEvents();
    QCOMPARE(pickRequests, 1);
    QCOMPARE(requestedKind, LiveInsightKind::Wave);
    QVERIFY(reply);

    LiveInsightsContextView::TargetCandidate picked;
    picked.label = QStringLiteral("always_comb");
    picked.moduleName = QStringLiteral("uart");
    picked.scopeLabel = QStringLiteral("always_comb");
    picked.scopeStartPosition = 120;
    picked.scopeEndPosition = 180;
    picked.scopeStartLineZeroBased = 12;
    reply(picked);
    QCoreApplication::processEvents();

    // A picked scope has to reach the surface: accepting it and rendering the
    // previous scope would be a silent no-op.
    auto* surface = view->surfaceForTest();
    QVERIFY(surface);
    const LiveInsightToolContext rendered = surface->contextForTest();
    QCOMPARE(rendered.scopeLabel, QStringLiteral("always_comb"));
    QCOMPARE(rendered.scopeStartPosition, 120);
    QCOMPARE(rendered.scopeEndPosition, 180);
    QCOMPARE(rendered.scopeStartLineZeroBased, 12);
    QVERIFY(!view->followEditor());
    QCOMPARE(host.sectionScope(key)->text(), QStringLiteral("always_comb"));

    // A section whose host cannot pick shows no chip at all, so providers
    // without an editor behind them keep the header they had.
    LiveInsightsContextProvider hostless(LiveInsightKind::Wave, &session);
    hostless.setToolContextSource(
        []() { return stubContext(QStringLiteral("uart"), QString()); });
    ContextDockHost plainHost;
    const ContextResource plainResource =
        LiveInsightsContextProvider::resourceForKind(
            LiveInsightKind::Wave, QStringLiteral("workspace-b"));
    auto* plainView = qobject_cast<LiveInsightsContextView*>(
        hostless.createView(plainResource, nullptr));
    QVERIFY(plainView);
    QVERIFY(plainHost.addResource(plainResource, plainView, true));
    plainHost.resize(520, 600);
    plainHost.show();
    QCoreApplication::processEvents();
    QVERIFY(!plainHost.sectionScope(plainResource.stableKey())->isVisible());
}

QTEST_MAIN(LiveInsightsContextProviderTest)

#include "live_insights_context_provider_test.moc"
