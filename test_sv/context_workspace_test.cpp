#include "contextcontentprovider.h"
#include "contextdockhost.h"
#include "contextpeekhost.h"
#include "contextrail.h"
#include "contextworkspacecontroller.h"
#include "workspacesessionstateservice.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDockWidget>
#include <QEvent>
#include <QDir>
#include <QTemporaryDir>
#include <QLabel>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>
#include <QToolButton>
#include <QUrl>

#include <iostream>
#include <limits>
#include <memory>

namespace {
int checks = 0;
int failures = 0;

void check(bool condition, const char* message)
{
    ++checks;
    if (condition)
        return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

void processDeferredDeletes()
{
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QApplication::processEvents();
}

void sendMouseEvent(QWidget* target,
                    QEvent::Type type,
                    const QPoint& globalPosition,
                    Qt::MouseButton button,
                    Qt::MouseButtons buttons)
{
    if (!target)
        return;
    QMouseEvent event(
        type,
        QPointF(target->mapFromGlobal(globalPosition)),
        QPointF(globalPosition),
        button,
        buttons,
        Qt::NoModifier);
    QApplication::sendEvent(target, &event);
}

void dragHandle(QWidget* handle, const QPoint& globalDelta)
{
    if (!handle)
        return;
    const QPoint start =
        handle->mapToGlobal(handle->rect().center());
    const QPoint finish = start + globalDelta;
    sendMouseEvent(handle,
                   QEvent::MouseButtonPress,
                   start,
                   Qt::LeftButton,
                   Qt::LeftButton);
    sendMouseEvent(handle,
                   QEvent::MouseMove,
                   finish,
                   Qt::NoButton,
                   Qt::LeftButton);
    sendMouseEvent(handle,
                   QEvent::MouseButtonRelease,
                   finish,
                   Qt::LeftButton,
                   Qt::NoButton);
    QApplication::processEvents();
}

void doubleClickHandle(QWidget* handle)
{
    if (!handle)
        return;
    const QPoint global =
        handle->mapToGlobal(handle->rect().center());
    sendMouseEvent(handle,
                   QEvent::MouseButtonDblClick,
                   global,
                   Qt::LeftButton,
                   Qt::LeftButton);
    QApplication::processEvents();
}

bool anchoredToBottomRight(const QWidget* host,
                           const QWidget* region)
{
    return host && region
        && host->geometry().right()
               == region->contentsRect().right()
        && host->geometry().bottom()
               == region->contentsRect().bottom();
}

struct ProviderCounters {
    ContextResource activation;
    int created = 0;
    int restored = 0;
    int saved = 0;
    int providerStateSaved = 0;
    int providerStateRestored = 0;
    int providerStateCleared = 0;
    std::function<void()> providerStateChanged;
};

class MockProvider final : public IContextContentProvider
{
public:
    explicit MockProvider(ProviderCounters* countersValue)
        : counters(countersValue)
    {
    }

    QString providerId() const override
    {
        return QStringLiteral("mock");
    }

    ContextResource activationResource(const QString&) const override
    {
        return counters ? counters->activation : ContextResource{};
    }

    QString displayName() const override
    {
        return QStringLiteral("Mock Context");
    }

    QWidget* createView(
        const ContextResource& resource,
        QWidget* parent) override
    {
        if (counters)
            ++counters->created;
        auto* label = new QLabel(resource.title, parent);
        label->setObjectName(
            QStringLiteral("mockView.%1").arg(resource.resourceId));
        return label;
    }

    ContextViewCapabilities capabilities(
        const ContextResource&) const override
    {
        ContextViewCapabilities result;
        result.presentations =
            ContextPresentation::Peek
            | ContextPresentation::Pinned
            | ContextPresentation::FullView;
        result.preferredWidth = 460;
        result.preferredHeight = 510;
        return result;
    }

    QVariantMap saveViewState(QWidget*) const override
    {
        if (counters)
            ++counters->saved;
        return {{QStringLiteral("saved"), true}};
    }

    void restoreViewState(
        QWidget*,
        const QVariantMap&) override
    {
        if (counters)
            ++counters->restored;
    }

    QVariantMap saveProviderState() const override
    {
        if (counters)
            ++counters->providerStateSaved;
        return {{QStringLiteral("expanded"),
                 QStringList{QStringLiteral("source"),
                             QStringLiteral("wave")}}};
    }

    void restoreProviderState(const QVariantMap& state) override
    {
        if (!counters)
            return;
        if (state.isEmpty())
            ++counters->providerStateCleared;
        else
            ++counters->providerStateRestored;
    }

    void setProviderStateChangedHandler(
        ProviderStateChangedHandler handler) override
    {
        if (counters)
            counters->providerStateChanged = std::move(handler);
    }

private:
    ProviderCounters* counters = nullptr;
};

ContextResource resource(const QString& id)
{
    ContextResource result;
    result.providerId = QStringLiteral("mock");
    result.resourceId = id;
    result.uri = QUrl(QStringLiteral("mock://resource/%1").arg(id));
    result.title = QStringLiteral("Resource %1").arg(id);
    result.workspaceId = QStringLiteral("workspace-a");
    result.state.insert(QStringLiteral("cursor"), 7);
    return result;
}

bool sameState(const ContextWorkspaceState& a, const ContextWorkspaceState& b)
{
    return a.pinnedResources == b.pinnedResources
        && a.providerStates == b.providerStates
        && a.activePinnedResourceKey == b.activePinnedResourceKey
        && a.peekWidth == b.peekWidth && a.peekHeight == b.peekHeight
        && a.dockWidth == b.dockWidth && a.dockVisible == b.dockVisible
        && a.railVisible == b.railVisible && a.valid == b.valid;
}

void verifyStateCompatibility(const QStringList& arguments)
{
    QTemporaryDir temporary;
    QMainWindow window;
    window.resize(1400, 900);
    auto* region = new QWidget(&window);
    window.setCentralWidget(region);
    window.show();
    QApplication::processEvents();
    ProviderCounters counters;
    ContextWorkspaceController controller(&window, region, &window);
    controller.registerProvider(std::make_unique<MockProvider>(&counters));
    const QString root = QDir::current().absoluteFilePath(QStringLiteral("compatibility-workspace"));
    controller.setWorkspaceRoot(root);
    ContextWorkspaceState initial;
    initial.valid = true;
    initial.peekWidth = 610;
    initial.peekHeight = 470;
    initial.dockWidth = 430;
    initial.dockVisible = true;
    initial.railVisible = false;
    initial.providerStates.insert(QStringLiteral("mock"),
        QVariantMap{{QStringLiteral("expanded"),
                    QStringList{QStringLiteral("source"), QStringLiteral("wave")}}});
    for (const QString& id : {QStringLiteral("compat-a"), QStringLiteral("compat-b")}) {
        auto item = resource(id);
        item.workspaceId.clear();
        item.state = {{QStringLiteral("saved"), true}};
        initial.pinnedResources.append(item.toVariantMap());
    }
    initial.activePinnedResourceKey = QStringLiteral("mock:compat-a");
    const auto restored = controller.restoreState(initial);
    QApplication::processEvents();
    const auto captured = controller.captureState();
    check(ContextWorkspaceState::kVersion == 3 && restored.restoredResources == 2
              && restored.skippedResources == 0 && sameState(initial, captured),
          "state_all_fields_roundtrip: multiple kept resources and all v3 fields survive restore");

    const bool importing = arguments.size() == 4 && arguments.at(1) == QStringLiteral("--state-import");
    const bool exporting = arguments.size() == 3 && arguments.at(1) == QStringLiteral("--state-export");
    const QString storage = importing || exporting ? arguments.at(2) : temporary.filePath("state.ini");
    WorkspaceSessionStateService service(storage);
    if (importing) {
        const auto loaded = service.load(root);
        check(loaded.loaded && sameState(initial, loaded.state.ui.contextWorkspace),
              "cross_build_import: every serialized context field matches the old/new fixture");
        controller.restoreState(loaded.state.ui.contextWorkspace);
        QApplication::processEvents();
        check(sameState(initial, controller.captureState()),
              "cross_build_restore: imported state restores to identical live context state");
    }
    WorkspaceSessionState session;
    session.workspaceRoot = root;
    session.ui.contextWorkspace = controller.captureState();
    WorkspaceSessionStateService output(importing ? arguments.at(3) : storage);
    check(output.save(session).saved
              && sameState(session.ui.contextWorkspace, output.load(root).state.ui.contextWorkspace),
          "state_wire_roundtrip: production session serializer preserves every context field");
}

struct PlacementFixture {
    QMainWindow window;
    ProviderCounters counters;
    std::unique_ptr<ContextWorkspaceController> controller;

    PlacementFixture()
    {
        window.resize(1200, 800);
        auto* region = new QWidget(&window);
        window.setCentralWidget(region);
        window.show();
        QApplication::processEvents();
        controller = std::make_unique<ContextWorkspaceController>(&window, region, &window);
        controller->setWorkspaceRoot(QStringLiteral("workspace-a"));
        controller->registerProvider(std::make_unique<MockProvider>(&counters));
    }
};

constexpr ContextPlacement floatingPlacement{
    ContextSurface::Floating, ContextPersistence::Transient, ContextBinding::Global};
constexpr ContextPlacement keptPlacement{
    ContextSurface::Docked, ContextPersistence::Kept, ContextBinding::Global};

void verifyPlacementMapping()
{
    PlacementFixture fixture;
    auto& controller = *fixture.controller;
    const auto floating = resource(QStringLiteral("floating"));
    const auto transient = resource(QStringLiteral("transient"));
    const auto kept = resource(QStringLiteral("kept"));
    check(controller.openResource(floating, floatingPlacement)
              && controller.peekHost()->resource() == floating
              && controller.dockHost()->resourceCount() == 0
              && controller.captureState().pinnedResources.isEmpty(),
          "placement_mapping: floating transient opens only a non-persisted preview");
    check(controller.openResource(transient)
              && controller.dockHost()->currentResource() == transient
              && controller.dockWidget()->isVisible()
              && controller.peekHost()->resource() == floating
              && controller.captureState().pinnedResources.isEmpty(),
          "placement_mapping: default docked transient coexists with a non-persisted preview");
    check(controller.openResource(kept, keptPlacement)
              && controller.dockHost()->resourceCount() == 2
              && controller.dockHost()->currentResource() == kept
              && controller.captureState().pinnedResources.size() == 1
              && ContextResource::fromVariantMap(controller.captureState().pinnedResources.first())
                     .stableKey() == kept.stableKey(),
          "placement_mapping: only the kept resource is captured among all three placements");
    QWidget* keptView = controller.dockHost()->viewForResource(kept.stableKey());
    QWidget* floatingView = controller.peekHost()->view();
    check(controller.openResource(kept, floatingPlacement)
              && controller.dockHost()->viewForResource(kept.stableKey()) == keptView
              && controller.peekHost()->view() == floatingView,
          "placement_reuse: floating requests retain an already docked instance");
    check(controller.openResource(floating)
              && controller.peekHost()->view() == floatingView
              && controller.dockHost()->resourceCount() == 1,
          "placement_reuse: transient dock requests retain an existing preview and replace the old transient tab");
    check(controller.openResource(floating, keptPlacement)
              && !controller.peekHost()->hasResource()
              && controller.dockHost()->viewForResource(floating.stableKey()) == floatingView
              && controller.captureState().pinnedResources.size() == 2,
          "placement_reuse: keeping a preview moves the live view and persists it");
    check(ContextPlacement{} == ContextPlacement{ContextSurface::Docked,
              ContextPersistence::Transient, ContextBinding::Global}
              && floatingPlacement != keptPlacement
              && contextPlacementName(floatingPlacement) == QStringLiteral("Floating/Transient/Global")
              && contextPlacementName(keptPlacement) == QStringLiteral("Docked/Kept/Global"),
          "placement_value: defaults, equality and diagnostic names preserve the three dimensions");
}

void verifyUnsupportedPlacements()
{
    PlacementFixture fixture;
    auto& controller = *fixture.controller;
    controller.openResource(resource(QStringLiteral("floating")), floatingPlacement);
    controller.openResource(resource(QStringLiteral("kept")), keptPlacement);
    controller.openResource(resource(QStringLiteral("transient")));
    const ContextPlacement unsupported[] = {
        {ContextSurface::Floating, ContextPersistence::Kept, ContextBinding::Global},
        {ContextSurface::Floating, ContextPersistence::Kept, ContextBinding::DocumentBound},
        {ContextSurface::Floating, ContextPersistence::Transient, ContextBinding::DocumentBound},
        {ContextSurface::Docked, ContextPersistence::Kept, ContextBinding::DocumentBound},
        {ContextSurface::Docked, ContextPersistence::Transient, ContextBinding::DocumentBound}
    };
    const auto before = controller.captureState();
    const auto floatingBefore = controller.peekHost()->resource();
    QWidget* viewBefore = controller.peekHost()->view();
    const int createdBefore = fixture.counters.created;
    const int activatedBefore = fixture.counters.restored;
    QSignalSpy opened(&controller, &ContextWorkspaceController::resourceOpened);
    QSignalSpy closed(&controller, &ContextWorkspaceController::resourceClosed);
    QSignalSpy changed(&controller, &ContextWorkspaceController::workspaceStateChanged);
    QSignalSpy active(&controller, &ContextWorkspaceController::activeResourceChanged);
    for (const auto placement : unsupported) {
        QString reason;
        check(!controller.openResource(resource(QStringLiteral("unsupported")), placement, &reason)
                  && !reason.isEmpty() && reason.contains(contextPlacementName(placement))
                  && controller.peekHost()->hasResource()
                  && controller.peekHost()->resource() == floatingBefore
                  && controller.peekHost()->view() == viewBefore
                  && controller.dockHost()->resourceCount() == 2
                  && sameState(before, controller.captureState())
                  && fixture.counters.created == createdBefore
                  && fixture.counters.restored == activatedBefore
                  && opened.isEmpty() && closed.isEmpty() && changed.isEmpty() && active.isEmpty(),
              "unsupported_placements: rejected combination creates, activates, closes and persists nothing");
    }
}

void verifyRailThreeStates()
{
    PlacementFixture fixture;
    auto& controller = *fixture.controller;
    fixture.counters.activation = resource(QStringLiteral("rail"));
    QAction* action = controller.rail()->actions().constFirst();
    action->trigger();
    QWidget* firstView = controller.dockHost()->viewForResource(fixture.counters.activation.stableKey());
    check(firstView && controller.dockWidget()->isVisible()
              && controller.dockHost()->resourceCount() == 1
              && !controller.peekHost()->hasResource(),
          "rail_three_states: first click opens a transient dock instance");
    action->trigger();
    check(!controller.dockWidget()->isVisible() && controller.dockHost()->resourceCount() == 1,
          "rail_three_states: second click collapses without disposing the view");
    action->trigger();
    check(controller.dockWidget()->isVisible()
              && controller.dockHost()->viewForResource(fixture.counters.activation.stableKey()) == firstView
              && fixture.counters.created == 1 && controller.captureState().pinnedResources.isEmpty(),
          "rail_three_states: third click reopens the same transient view");
    controller.clearResources();
    controller.openResource(fixture.counters.activation, floatingPlacement);
    QPointer<QWidget> preview = controller.peekHost()->view();
    action->trigger();
    processDeferredDeletes();
    check(!controller.peekHost()->hasResource() && preview.isNull(),
          "rail_three_states: an active floating instance retains close-and-dispose behavior");
}

void verifyRailFocusExisting()
{
    PlacementFixture fixture;
    auto& controller = *fixture.controller;
    const auto first = resource(QStringLiteral("first"));
    const auto second = resource(QStringLiteral("second"));
    fixture.counters.activation = resource(QStringLiteral("must-not-create"));
    controller.openResource(first, keptPlacement);
    controller.openResource(second, keptPlacement);
    controller.dockHost()->activateResource(first.stableKey());
    QWidget* firstView = controller.dockHost()->viewForResource(first.stableKey());
    controller.dockWidget()->hide();
    controller.rail()->actions().constFirst()->trigger();
    check(controller.dockWidget()->isVisible()
              && controller.dockHost()->currentResource() == first
              && controller.dockHost()->viewForResource(first.stableKey()) == firstView
              && controller.dockHost()->resourceCount() == 2 && fixture.counters.created == 2,
          "rail_focus_existing: hidden sidebar restores its active resource without creating another view");
}
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    verifyStateCompatibility(app.arguments());
    verifyPlacementMapping();
    verifyUnsupportedPlacements();
    verifyRailThreeStates();
    verifyRailFocusExisting();

    const ContextResource original = resource(QStringLiteral("a"));
    QString parseFailure;
    const ContextResource restored = ContextResource::fromVariantMap(
        original.toVariantMap(), &parseFailure);
    check(parseFailure.isEmpty() && restored == original,
          "context resource round-trips through portable variant data");
    QVariantMap unsupported = original.toVariantMap();
    unsupported.insert(QStringLiteral("schema"),
                       QStringLiteral("future/v9"));
    check(!ContextResource::fromVariantMap(
               unsupported, &parseFailure).isValid()
              && !parseFailure.isEmpty(),
          "unknown context resource schemas fail explicitly");

    QMainWindow window;
    window.resize(1000, 700);
    auto* editorRegion = new QWidget(&window);
    editorRegion->setObjectName(QStringLiteral("editorRegion"));
    window.setCentralWidget(editorRegion);
    window.show();
    QApplication::processEvents();

    ProviderCounters counters;
    ContextWorkspaceController controller(
        &window, editorRegion, &window);
    check(controller.registerProvider(
              std::make_unique<MockProvider>(&counters)),
          "provider registers once");
    check(!controller.registerProvider(
              std::make_unique<MockProvider>(&counters)),
          "duplicate provider identity is rejected");
    check(controller.providerIds()
              == QStringList{QStringLiteral("mock")}
              && controller.rail()->isVisible()
              && controller.rail()->entryIds()
                     == QStringList{QStringLiteral("mock")},
          "provider registration creates one explicit rail entry");

    int activationRequests = 0;
    int fullViewRequests = 0;
    QObject::connect(
        &controller,
        &ContextWorkspaceController::providerActivationRequested,
        &window,
        [&activationRequests](const QString& providerId) {
            if (providerId == QStringLiteral("mock"))
                ++activationRequests;
        });
    QObject::connect(
        &controller,
        &ContextWorkspaceController::fullViewRequested,
        &window,
        [&fullViewRequests](const ContextResource&) {
            ++fullViewRequests;
        });
    check(!controller.rail()->actions().isEmpty(),
          "rail exposes an activatable action");
    controller.rail()->actions().constFirst()->trigger();
    check(activationRequests == 1,
          "empty provider rail activation delegates resource selection");

    QString failureReason;
    check(controller.openResource(original,
                                  ContextPlacement{ContextSurface::Floating, ContextPersistence::Transient, ContextBinding::Global},
                                  &failureReason)
              && failureReason.isEmpty()
              && controller.peekHost()->hasResource()
              && controller.peekHost()->resource() == original
              && !controller.dockWidget()->isVisible(),
          "resource opens in the single transient Peek host");
    QApplication::processEvents();
    QWidget* leftHandle =
        controller.peekHost()->findChild<QWidget*>(
            QStringLiteral("contextPeekResizeLeft"));
    QWidget* bottomHandle =
        controller.peekHost()->findChild<QWidget*>(
            QStringLiteral("contextPeekResizeBottom"));
    QWidget* cornerHandle =
        controller.peekHost()->findChild<QWidget*>(
            QStringLiteral("contextPeekResizeCorner"));
    check(leftHandle && bottomHandle && cornerHandle
              && leftHandle->isVisible()
              && bottomHandle->isVisible()
              && cornerHandle->isVisible()
              && leftHandle->cursor().shape()
                     == Qt::SizeHorCursor
              && bottomHandle->cursor().shape()
                     == Qt::SizeVerCursor
              && cornerHandle->cursor().shape()
                     == Qt::SizeBDiagCursor
              && controller.peekHost()->rect().contains(
                     leftHandle->geometry())
              && controller.peekHost()->rect().contains(
                     bottomHandle->geometry())
              && controller.peekHost()->rect().contains(
                     cornerHandle->geometry()),
          "Peek exposes reachable logical-DPI left, bottom, and corner handles");

    QSignalSpy peekSizeSpy(
        controller.peekHost(),
        &ContextPeekHost::preferredSizeChanged);
    const QSize initialPeekSize =
        controller.peekHost()->preferredSize();
    dragHandle(leftHandle, QPoint(-50, 0));
    const QSize widthResized =
        controller.peekHost()->preferredSize();
    check(widthResized.width() == initialPeekSize.width() + 50
              && widthResized.height() == initialPeekSize.height()
              && anchoredToBottomRight(
                     controller.peekHost(), editorRegion),
          "left handle resizes width independently while preserving the bottom-right anchor");
    dragHandle(bottomHandle, QPoint(0, -40));
    const QSize heightResized =
        controller.peekHost()->preferredSize();
    check(heightResized.width() == widthResized.width()
              && heightResized.height() == widthResized.height() + 40
              && anchoredToBottomRight(
                     controller.peekHost(), editorRegion),
          "bottom handle resizes height independently while preserving the bottom-right anchor");
    dragHandle(cornerHandle, QPoint(30, 20));
    const QSize cornerResized =
        controller.peekHost()->preferredSize();
    check(cornerResized.width() == heightResized.width() - 30
              && cornerResized.height() == heightResized.height() - 20
              && peekSizeSpy.count() == 3
              && anchoredToBottomRight(
                     controller.peekHost(), editorRegion),
          "corner handle resizes both dimensions and publishes completed user preferences");

    controller.peekHost()->setPreferredSize(
        QSize(std::numeric_limits<int>::max(), -1));
    check(controller.peekHost()->preferredSize()
              == QSize(ContextWorkspaceState::kMaximumPeekWidth,
                       ContextWorkspaceState::kMinimumPeekHeight)
              && controller.peekHost()->geometry().width()
                     <= editorRegion->contentsRect().width()
              && controller.peekHost()->geometry().height()
                     <= editorRegion->contentsRect().height()
              && anchoredToBottomRight(
                     controller.peekHost(), editorRegion),
          "Peek applies a second clamp to malformed or external dimensions");

    controller.peekHost()->setPreferredSize(QSize(700, 600));
    doubleClickHandle(cornerHandle);
    check(controller.peekHost()->preferredSize()
              == QSize(460, 510),
          "double-click resets both dimensions to the active provider preference");

    const QSize retainedUserSize(610, 390);
    controller.peekHost()->setPreferredSize(retainedUserSize);
    window.resize(560, 500);
    QApplication::processEvents();
    check(editorRegion->contentsRect().contains(
              controller.peekHost()->geometry())
              && editorRegion->contentsRect().width()
                     - controller.peekHost()->width() > 0
              && anchoredToBottomRight(
                     controller.peekHost(), editorRegion)
              && leftHandle->isVisible()
              && bottomHandle->isVisible()
              && cornerHandle->isVisible(),
          "narrow windows keep Peek and every handle reachable while retaining editor width");
    window.resize(1000, 700);
    QApplication::processEvents();
    check(controller.peekHost()->preferredSize()
              == retainedUserSize
              && controller.peekHost()->size()
                     == retainedUserSize,
          "temporary window constraints do not overwrite the stored user size");
    QToolButton* peekFullView =
        controller.peekHost()->findChild<QToolButton*>(
            QStringLiteral("contextPeekFullView"));
    if (peekFullView)
        peekFullView->click();
    check(peekFullView && peekFullView->isVisible()
              && fullViewRequests == 1,
          "Peek exposes full view only through provider capability");
    QPointer<QWidget> firstView = controller.peekHost()->view();

    const ContextResource second = resource(QStringLiteral("b"));
    check(controller.openResource(second,
                                  ContextPlacement{ContextSurface::Floating, ContextPersistence::Transient, ContextBinding::Global},
                                  &failureReason)
              && controller.peekHost()->resource() == second
              && controller.peekHost()->preferredSize()
                     == retainedUserSize
              && counters.created == 2
              && counters.restored == 2,
          "opening another Peek resource replaces the preview without overwriting user size");
    processDeferredDeletes();
    check(firstView.isNull() && counters.saved == 1,
          "replaced Peek view is disposed through its provider");

    QPointer<QWidget> secondView = controller.peekHost()->view();
    check(controller.pinPeek(&failureReason)
              && failureReason.isEmpty()
              && !controller.peekHost()->hasResource()
              && controller.dockHost()->resourceCount() == 1
              && controller.dockHost()->currentResource() == second
              && controller.dockHost()->viewForResource(
                     second.stableKey()) == secondView
              && controller.dockWidget()->isVisible(),
          "pinning moves the existing view without recreating it");
    QToolButton* dockFullView =
        controller.dockHost()->findChild<QToolButton*>(
            QStringLiteral("contextDockFullView"));
    if (dockFullView)
        dockFullView->click();
    check(dockFullView && dockFullView->isVisible()
              && fullViewRequests == 2,
          "Pinned host reuses the same provider full-view action");

    check(controller.openResource(original,
                                  ContextPlacement{ContextSurface::Docked, ContextPersistence::Kept, ContextBinding::Global},
                                  &failureReason)
              && controller.dockHost()->resourceCount() == 2
              && controller.dockHost()->currentResource() == original,
          "pinned host supports multiple independent resource tabs");
    controller.closePeek();
    controller.rail()->actions().constFirst()->trigger();
    QApplication::processEvents();
    check(!controller.dockWidget()->isVisible(), "rail hides the active pinned provider");
    controller.rail()->actions().constFirst()->trigger();
    QApplication::processEvents();
    check(controller.dockWidget()->isVisible()
              && controller.dockHost()->currentResource() == original,
          "reopening a provider preserves its active tab among multiple resources");

    check(controller.unpinResource(second.stableKey(), &failureReason)
              && failureReason.isEmpty()
              && controller.peekHost()->resource() == second
              && controller.peekHost()->preferredSize()
                     == retainedUserSize
              && controller.dockHost()->resourceCount() == 1,
          "a pinned resource returns to Peek without overwriting user size");
    controller.closePeek();
    processDeferredDeletes();
    check(secondView.isNull(),
          "closing Peek releases the provider view");

    check(controller.closePinnedResource(original.stableKey())
              && controller.dockHost()->resourceCount() == 0
              && !controller.dockWidget()->isVisible(),
          "closing the final pinned resource hides its dock");
    processDeferredDeletes();

    ContextResource unavailable = original;
    unavailable.providerId = QStringLiteral("missing");
    check(!controller.openResource(unavailable,
                                   ContextPlacement{ContextSurface::Floating, ContextPersistence::Transient, ContextBinding::Global},
                                   &failureReason)
              && !failureReason.isEmpty(),
          "unavailable providers fail without creating fallback content");

    check(counters.created == 3
              && counters.restored == 3
              && counters.saved == 3,
          "provider lifecycle hooks cover every created view exactly once");

    controller.setWorkspaceRoot(QStringLiteral("workspace-a"));
    check(controller.openResource(original,
                                  ContextPlacement{ContextSurface::Docked, ContextPersistence::Kept, ContextBinding::Global},
                                  &failureReason)
              && controller.openResource(second,
                                         ContextPlacement{ContextSurface::Docked, ContextPersistence::Kept, ContextBinding::Global},
                                         &failureReason),
          "workspace-scoped resources can be pinned before capture");
    window.resizeDocks(
        {controller.dockWidget()}, {420}, Qt::Horizontal);
    QApplication::processEvents();
    const int userDockWidth = controller.dockWidget()->width();
    controller.dockHost()->activateResource(original.stableKey());
    const ContextResource transient = resource(QStringLiteral("transient"));
    check(controller.openResource(transient,
                                  ContextPlacement{ContextSurface::Floating, ContextPersistence::Transient, ContextBinding::Global},
                                  &failureReason),
          "a transient preview can coexist with pinned resources");
    ContextWorkspaceState savedState = controller.captureState();
    check(savedState.valid
              && savedState.pinnedResources.size() == 2
              && savedState.activePinnedResourceKey
                     == original.stableKey()
              && savedState.peekWidth == retainedUserSize.width()
              && savedState.providerStates.value(
                     QStringLiteral("mock")).toMap().value(
                     QStringLiteral("expanded")).toStringList()
                     == QStringList({QStringLiteral("source"),
                                     QStringLiteral("wave")})
              && savedState.peekHeight == retainedUserSize.height()
              && savedState.dockWidth == userDockWidth,
          "session capture preserves both Peek dimensions, actual Dock width, pinned order, and active tab");
    QSignalSpy providerStateSignalSpy(
        &controller, &ContextWorkspaceController::workspaceStateChanged);
    check(static_cast<bool>(counters.providerStateChanged),
          "providers receive a state-change bridge from the Context controller");
    if (counters.providerStateChanged)
        counters.providerStateChanged();
    QApplication::processEvents();
    check(providerStateSignalSpy.count() == 1,
          "provider-only state changes request automatic workspace persistence");

    QVariantMap unavailableMap = original.toVariantMap();
    unavailableMap.insert(QStringLiteral("providerId"),
                          QStringLiteral("missing"));
    savedState.pinnedResources.append(unavailableMap);
    controller.clearResources();
    const ContextWorkspaceRestoreResult restoreResult =
        controller.restoreState(savedState);
    check(restoreResult.restoredResources == 2
              && restoreResult.skippedResources == 1
              && controller.dockHost()->resourceCount() == 2
              && controller.dockHost()->currentResource().stableKey()
                     == original.stableKey()
              && counters.providerStateRestored > 0
              && !controller.peekHost()->hasResource(),
          "restore skips unavailable providers without blocking valid pinned tabs");

    const int clearedBeforeInvalidRestore =
        counters.providerStateCleared;
    controller.restoreState(ContextWorkspaceState{});
    check(counters.providerStateCleared
              == clearedBeforeInvalidRestore + 1,
          "an unsaved workspace clears retained provider state instead of inheriting another workspace");
    controller.restoreState(savedState);

    window.resizeDocks(
        {controller.dockWidget()}, {350}, Qt::Horizontal);
    QApplication::processEvents();
    const int qtRestoredWidth = controller.dockWidget()->width();
    ContextWorkspaceState alternateDockState = savedState;
    alternateDockState.dockWidth = 650;
    const ContextWorkspaceRestoreResult preservedDockRestore =
        controller.restoreState(alternateDockState, true);
    QApplication::processEvents();
    const int preservedDockWidth = controller.dockWidget()->width();
    check(preservedDockRestore.restoredResources == 2
              && qAbs(preservedDockWidth - qtRestoredWidth) <= 12,
          "Context restore can preserve geometry already restored by QMainWindow");
    const ContextWorkspaceRestoreResult portableDockRestore =
        controller.restoreState(alternateDockState, false);
    QApplication::processEvents();
    const int portableDockWidth = controller.dockWidget()->width();
    check(portableDockRestore.restoredResources == 2
              && portableDockWidth > preservedDockWidth
              && qAbs(portableDockWidth - 650) <= 12,
          "Context restore applies the portable Dock width only when requested");

    ContextResource foreign = resource(QStringLiteral("foreign"));
    foreign.workspaceId = QStringLiteral("workspace-b");
    check(!controller.openResource(foreign,
                                   ContextPlacement{ContextSurface::Docked, ContextPersistence::Kept, ContextBinding::Global},
                                   &failureReason)
              && failureReason.contains(QStringLiteral("another workspace")),
          "resources from another workspace cannot leak into the active context");
    controller.clearResources();

    ContextWorkspaceState malformedState;
    malformedState.valid = true;
    malformedState.peekWidth = std::numeric_limits<int>::max();
    malformedState.peekHeight = std::numeric_limits<int>::min();
    malformedState.dockWidth = std::numeric_limits<int>::max();
    controller.restoreState(malformedState);
    const ContextWorkspaceState sanitizedState =
        controller.captureState();
    check(sanitizedState.peekWidth
                  == ContextWorkspaceState::kMaximumPeekWidth
              && sanitizedState.peekHeight
                     == ContextWorkspaceState::kMinimumPeekHeight
              && sanitizedState.dockWidth
                     == ContextWorkspaceState::
                            kMaximumStoredDockWidth,
          "controller clamps malformed dimensions again when bypassing JSON persistence");

    QWidget resizeLifecycleRegion;
    resizeLifecycleRegion.resize(820, 620);
    resizeLifecycleRegion.show();
    auto* lifecycleHost =
        new ContextPeekHost(&resizeLifecycleRegion);
    lifecycleHost->setView(
        original,
        new QLabel(QStringLiteral("Resize lifecycle"),
                   lifecycleHost));
    lifecycleHost->setPreferredSize(QSize(520, 440));
    lifecycleHost->setFocus();
    QApplication::processEvents();
    QWidget* lifecycleHandle =
        lifecycleHost->findChild<QWidget*>(
            QStringLiteral("contextPeekResizeCorner"));
    QSignalSpy closeSpy(
        lifecycleHost,
        &ContextPeekHost::closeRequested);
    const QPoint lifecycleStart = lifecycleHandle
        ? lifecycleHandle->mapToGlobal(
              lifecycleHandle->rect().center())
        : QPoint();
    sendMouseEvent(lifecycleHandle,
                   QEvent::MouseButtonPress,
                   lifecycleStart,
                   Qt::LeftButton,
                   Qt::LeftButton);
    sendMouseEvent(lifecycleHandle,
                   QEvent::MouseMove,
                   lifecycleStart + QPoint(-40, -30),
                   Qt::NoButton,
                   Qt::LeftButton);
    QTest::keyClick(lifecycleHost, Qt::Key_Escape);
    QApplication::processEvents();
    check(closeSpy.count() == 1
              && lifecycleHost->preferredSize()
                     == QSize(520, 440)
              && QWidget::mouseGrabber() != lifecycleHandle,
          "Escape cancels an in-progress resize and releases pointer ownership");

    QPointer<QWidget> lifecycleHandleGuard(lifecycleHandle);
    sendMouseEvent(lifecycleHandle,
                   QEvent::MouseButtonPress,
                   lifecycleStart,
                   Qt::LeftButton,
                   Qt::LeftButton);
    delete lifecycleHost;
    QApplication::processEvents();
    check(lifecycleHandleGuard.isNull()
              && QWidget::mouseGrabber() == nullptr,
          "destroying a resizing host safely releases and destroys its local handles");

    {
        QMainWindow sidebarWindow;
        sidebarWindow.resize(1400, 800);
        auto* center = new QWidget(&sidebarWindow);
        sidebarWindow.setCentralWidget(center);
        sidebarWindow.show();
        ProviderCounters sidebarCounters;
        sidebarCounters.activation = original;
        ContextWorkspaceController sidebar(&sidebarWindow, center, &sidebarWindow);
        sidebar.registerProvider(std::make_unique<MockProvider>(&sidebarCounters));
        sidebar.rail()->actions().constFirst()->trigger();
        QApplication::processEvents();
        check(sidebar.dockWidget()->isVisible() && !sidebar.peekHost()->hasResource(),
              "rail opens an independent sidebar instead of an editor overlay");
        QWidget* graphView = sidebar.dockHost()->viewForResource(original.stableKey());
        sidebarWindow.resizeDocks({sidebar.dockWidget()}, {750}, Qt::Horizontal);
        QApplication::processEvents();
        const int widened = sidebar.dockWidget()->width();
        check(widened >= 700 && center->width() >= 240,
              "sidebar expands for charts while retaining editor space");
        check(sidebar.dockHost()->viewForResource(original.stableKey()) == graphView,
              "resizing preserves the chart view instance");
        sidebar.rail()->actions().constFirst()->trigger();
        QApplication::processEvents();
        check(!sidebar.dockWidget()->isVisible(), "second rail click closes transient sidebar");
        sidebar.rail()->actions().constFirst()->trigger();
        QApplication::processEvents();
        check(qAbs(sidebar.dockWidget()->width() - widened) <= 2,
              "reopening sidebar preserves its resized width");
        check(sidebar.dockHost()->viewForResource(original.stableKey()) == graphView,
              "sidebar toggling retains the existing view and its state");
        check(sidebar.openResource(original, ContextPlacement{ContextSurface::Docked, ContextPersistence::Kept, ContextBinding::Global}),
              "current sidebar resource can be pinned");
        sidebar.rail()->actions().constFirst()->trigger();
        QApplication::processEvents();
        check(!sidebar.dockWidget()->isVisible()
                  && sidebar.dockHost()->resourceCount() == 1,
              "second click also hides pinned content without removing it");
        sidebar.rail()->actions().constFirst()->trigger();
        QApplication::processEvents();
        check(sidebar.dockWidget()->isVisible()
                  && sidebar.dockHost()->viewForResource(original.stableKey()) == graphView,
              "pinned content reopens with its original view");
    }

    if (failures == 0) {
        std::cout << "context_workspace_test: "
                  << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "context_workspace_test: "
              << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
