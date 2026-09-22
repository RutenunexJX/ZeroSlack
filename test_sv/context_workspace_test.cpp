#include "contextcontentprovider.h"
#include "contextdockhost.h"
#include "contextpeekhost.h"
#include "contextrail.h"
#include "contextworkspacecontroller.h"
#include "workspacesessionstateservice.h"
#include "contextfloatingwindow.h"
#include "temporaryeditorcontextprovider.h"
#include "settingscenterkeys.h"
#include "settingscenterpanel.h"
#include "settingscenterservice.h"
#include "applicationthememanager.h"
#include "insightvisualstyle.h"
#include "testuistyle.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDockWidget>
#include <QEvent>
#include <QDir>
#include <QTemporaryDir>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QScreen>
#include <QSlider>
#include <QLabel>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>
#include <QToolButton>
#include <QUrl>
#include <QWindow>
#include <QMenu>
#include <QPainter>
#include <QImage>

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
    bool detachable = false;
    // 0 keeps the dock's own viewport split; a provider opts into a suggested
    // section height by publishing a positive value.
    int preferredSectionHeight = 0;
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
        result.detachable = counters && counters->detachable;
        result.presentations =
            ContextPresentation::Peek
            | ContextPresentation::Pinned
            | ContextPresentation::FullView;
        result.preferredWidth = 460;
        result.preferredHeight = 510;
        result.preferredSectionHeight =
            counters ? counters->preferredSectionHeight : 0;
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
        && a.bottomDockHeight == b.bottomDockHeight && a.bottomDockVisible == b.bottomDockVisible
        && a.railVisible == b.railVisible && a.valid == b.valid
        && a.floatingX == b.floatingX && a.floatingY == b.floatingY
        && a.floatingWidth == b.floatingWidth && a.floatingHeight == b.floatingHeight
        && a.floatingScreenName == b.floatingScreenName
        && a.floatingGeometryValid == b.floatingGeometryValid
        && a.floatingInstances == b.floatingInstances && a.floatingCollapsed == b.floatingCollapsed
        && a.documentFloatingLayouts == b.documentFloatingLayouts
        && a.documentFloatingOrder == b.documentFloatingOrder && a.dockSections == b.dockSections;
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
        initial.dockSections.append({item.stableKey(), id != "compat-a", 0});
    }
    initial.activePinnedResourceKey = QStringLiteral("mock:compat-a");
    const auto restored = controller.restoreState(initial);
    QApplication::processEvents();
    const auto captured = controller.captureState();
    check(ContextWorkspaceState::kVersion == 7 && restored.restoredResources == 2
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
    QWidget* button = controller.rail()->widgetForAction(action);
    QApplication::processEvents();
    check(button && button->isVisible(), "rail entry exposes a clickable widget");
    QTest::mouseClick(button, Qt::LeftButton);
    QWidget* firstView = controller.dockHost()->viewForResource(fixture.counters.activation.stableKey());
    check(firstView && controller.dockWidget()->isVisible()
              && controller.dockHost()->resourceCount() == 1
              && !controller.peekHost()->hasResource(),
          "rail_three_states: first click opens a transient dock instance");
    QTest::mouseClick(button, Qt::LeftButton);
    check(controller.dockWidget()->isVisible() && controller.dockHost()->isSectionCollapsed(controller.dockHost()->currentResource().stableKey()) && controller.dockHost()->resourceCount() == 1,
          "rail_three_states: second click collapses without disposing the view");
    QTest::mouseClick(button, Qt::LeftButton);
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

void verifyDetachableRoutingAndMovement()
{
    PlacementFixture fixture;
    auto& controller = *fixture.controller;
    const auto item = resource(QStringLiteral("movable"));
    check(ContextViewCapabilities{}.detachable,
          "detachable_default: providers are detachable unless explicitly opted out");
    controller.openResource(item, floatingPlacement);
    QWidget* original = controller.floatingSurface()->view();
    check(original && !controller.peekHost()->isWindow()
              && controller.peekHost()->parentWidget() == fixture.window.centralWidget()
              && controller.peekHost()->hasResource() && !controller.floatingWindow()->hasResource(),
          "detachable_routing: non-detachable view remains an editor-region overlay");
    const int created = fixture.counters.created;
    check(controller.pinPeek() && controller.dockHost()->viewForResource(item.stableKey()) == original,
          "floating_movement: overlay to dock transports the same QWidget");
    fixture.counters.detachable = true;
    check(controller.unpinResource(item.stableKey())
              && controller.floatingWindow()->view() == original
              && controller.floatingWindow()->isWindow()
              && controller.floatingWindow()->windowType() == Qt::Tool
              && controller.floatingWindow()->parentWidget() == &fixture.window
              && !(controller.floatingWindow()->windowFlags() & Qt::FramelessWindowHint)
              && !controller.peekHost()->hasResource() && fixture.counters.created == created,
          "floating_movement: dock to native Tool window transports without reconstruction");
    check(controller.pinPeek() && controller.dockHost()->viewForResource(item.stableKey()) == original,
          "floating_movement: native window to dock preserves identity");
    fixture.counters.detachable = false;
    check(controller.unpinResource(item.stableKey())
              && controller.peekHost()->view() == original && !controller.floatingWindow()->hasResource()
              && fixture.counters.created == created,
          "floating_movement: returning to overlay keeps the original view and creation count");
    TemporaryEditorContextProvider editorProvider(nullptr);
    const auto capabilities = editorProvider.capabilities({});
    check(!capabilities.detachable && capabilities.preferredSize() == QSize(580, 480)
              && capabilities.minimumWidth == 360 && capabilities.maximumWidth == 900
              && capabilities.minimumHeight == 260 && capabilities.maximumHeight == 920
              && !capabilities.supports(ContextPresentation::FullView),
          "temporary_editor_detachable: real provider opts out without changing existing capabilities");
}

void verifyFloatingGeometryRoundtrip()
{
    PlacementFixture fixture;
    fixture.counters.detachable = true;
    auto& controller = *fixture.controller;
    controller.openResource(resource(QStringLiteral("geometry")), floatingPlacement);
    auto* window = controller.floatingWindow();
    using State = ContextWorkspaceState;
    const QRect available = window->screen()->availableGeometry();
    std::cout << "floating_geometry screen: " << available.x() << ',' << available.y()
              << ' ' << available.width() << 'x' << available.height()
              << " name='" << window->screen()->name().toStdString() << "'\n";
    if (available.width() < State::kMinimumPeekWidth || available.height() < State::kMinimumPeekHeight) {
        std::cout << "SKIP: floating geometry roundtrip: available screen is smaller than the minimum window size\n";
        window->close();
        check(!controller.floatingSurface()->hasResource() && !window->isVisible(),
              "native_close: native close action routes through controller disposal");
        return;
    }
    const QSize size(State::boundedPeekWidth(available.width() / 2),
                     State::boundedPeekHeight(available.height() / 2));
    const QRect target(available.x() + (available.width() - size.width()) / 2,
                       available.y() + (available.height() - size.height()) / 2,
                       size.width(), size.height());
    const QMargins margins = window->windowHandle()->frameMargins();
    window->resize(target.width() - margins.left() - margins.right(),
                   target.height() - margins.top() - margins.bottom());
    window->move(target.topLeft());
    QApplication::processEvents();
    const auto saved = controller.captureState();
    check(available.contains(target) && window->frameGeometry() == target,
          "floating_geometry: derived outer rectangle is fully visible and applied exactly");
    check(saved.floatingScreenName == window->screen()->name(),
          "floating_geometry: captured screen name matches the current screen without substitution");
    check(saved.floatingGeometryValid && saved.floatingWidth == window->frameGeometry().width()
              && saved.floatingHeight == window->frameGeometry().height()
              && saved.floatingX == window->frameGeometry().x()
              && saved.floatingY == window->frameGeometry().y()
              && saved.floatingScreenName == window->screen()->name(),
          "floating_geometry: native frame rectangle and screen name are captured");
    controller.restoreState(saved);
    check(ContextWorkspaceState::kVersion == 7 && sameState(saved, controller.captureState()),
          "floating_geometry: all fields survive capture and restore independently of overlay size");
    controller.openResource(resource(QStringLiteral("geometry")), floatingPlacement);
    QApplication::processEvents();
    check(sameState(saved, controller.captureState()),
          "floating_geometry: reopening uses saved geometry rather than provider preferred size");
    window->close();
    check(!controller.floatingSurface()->hasResource() && !window->isVisible(),
          "native_close: native close action routes through controller disposal");
}

void verifyFloatingScreenFallback()
{
    using State = ContextWorkspaceState;
    const QRect primary(0, 0, 1920, 1040);
    const QRect second(1920, 0, 1280, 1000);
    const QList<QRect> screens{primary, second};
    const QList<QString> names{QStringLiteral("primary"), QStringLiteral("second")};
    const QRect valid(2010, 100, 500, 400);
    check(State::resolvedFloatingGeometry(valid, "second", screens, primary, names) == valid,
          "screen_fallback_a: existing screen and accessible title preserve geometry");
    check(primary.contains(State::resolvedFloatingGeometry(valid, "missing", screens, primary, names)),
          "screen_fallback_b: disconnected screen falls fully inside primary");
    check(primary.contains(State::resolvedFloatingGeometry(QRect(3180, -390, 500, 400), "second", screens, primary, names)),
          "screen_fallback_c: off-screen title and insufficient horizontal overlap fall back");
    check(State::resolvedFloatingGeometry(QRect(0, 0, 10, 5), "primary", screens, primary, names).size()
              == QSize(State::kMinimumPeekWidth, State::kMinimumPeekHeight)
              && State::resolvedFloatingGeometry(QRect(0, 0, 40000, 40000), "primary", screens, primary, names).size()
              == QSize(State::kMaximumPeekWidth, State::kMaximumStoredPeekHeight),
          "screen_fallback_d: sizes are bounded by existing minimum and maximum constants");
    const auto empty = State::resolvedFloatingGeometry(QRect(-900, -900, 8000, 8000), "second", {}, primary, {});
    check(!empty.isEmpty() && empty.x() >= 0 && empty.y() >= 0 && primary.contains(empty),
          "screen_fallback_e: empty screen lists still produce a visible primary rectangle");
    const QRect partial(1920 - State::kMinimumPeekWidth / 2, 0, 500, 400);
    check(State::resolvedFloatingGeometry(partial, "primary", screens, primary, names) == partial
              && primary.contains(State::resolvedFloatingGeometry(partial.translated(1, 0), "primary", screens, primary, names))
              && primary.contains(State::resolvedFloatingGeometry(QRect(100, -1, 500, 400), "primary", screens, primary, names)),
          "screen_fallback_thresholds: horizontal boundary and entire title strip are enforced");
    const QRect tiny(0, 0, 120, 100);
    check(tiny.contains(State::resolvedFloatingGeometry(valid, "missing", {}, tiny, {})),
          "screen_fallback_tiny: available screen size takes precedence over logical minima");
}

void verifyV3ContextInput()
{
    QTemporaryDir temporary;
    const QString root = temporary.path();
    const QString path = temporary.filePath(QStringLiteral("session.ini"));
    WorkspaceSessionStateService service(path);
    WorkspaceSessionState session;
    session.workspaceRoot = root;
    auto& expected = session.ui.contextWorkspace;
    expected.valid = true;
    expected.peekWidth = 610;
    expected.peekHeight = 480;
    expected.dockWidth = 410;
    expected.dockVisible = true;
    expected.railVisible = false;
    expected.activePinnedResourceKey = "mock:old";
    expected.pinnedResources = {resource("old").toVariantMap()};
    expected.dockSections = {{"mock:old", false, 0}};
    expected.providerStates = {{"mock", QVariantMap{{"expanded", QStringList{"source"}}}}};
    check(service.save(session).saved, "v3_input: session fixture is available");
    QSettings settings(path, QSettings::IniFormat);
    const QString key = settings.allKeys().constFirst();
    QJsonObject document = QJsonDocument::fromJson(settings.value(key).toByteArray()).object();
    QJsonObject ui = document.value("ui").toObject();
    QJsonObject context = ui.value("contextWorkspace").toObject();
    context.insert("version", 3);
    context.insert("floatingX", 1234);
    context.insert("floatingWidth", 777);
    context.insert("floatingGeometryValid", true);
    ui.insert("contextWorkspace", context);
    document.insert("ui", ui);
    settings.setValue(key, QJsonDocument(document).toJson(QJsonDocument::Compact));
    settings.sync();
    const auto loaded = service.load(root);
    check(loaded.loaded && sameState(expected, loaded.state.ui.contextWorkspace)
              && !loaded.state.ui.contextWorkspace.floatingGeometryValid
              && loaded.state.ui.contextWorkspace.floatingWidth == ContextWorkspaceState::kDefaultPeekWidth,
          "v3_input: all old fields survive and v4 geometry keys are ignored with defaults");
}

void verifyFloatingOpacityAndSettings()
{
    ApplicationThemeManager::instance().applyToApplication();
    PlacementFixture fixture;
    auto* window = fixture.controller->floatingWindow();
    window->setView(resource("acrylic"), new QLabel(QStringLiteral("Opaque content")));
    QApplication::processEvents();
    check(window->backgroundOpacity() == 90, "floating_opacity: default background opacity is 90 percent");
    window->setBackgroundOpacity(73);
    for (const auto type : {QEvent::WindowDeactivate, QEvent::WindowActivate, QEvent::Leave}) {
        QEvent event(type);
        QApplication::sendEvent(window, &event);
        check(window->windowOpacity() == 1.0,
              "floating_opacity: focus and hover never fade text or icons");
    }
    if (QGuiApplication::platformName() != QStringLiteral("windows"))
        check(!window->hasAcrylicBackdrop(), "floating_acrylic: unsupported platforms use the solid fallback");
    const auto initialTheme = ApplicationThemeManager::instance().mode();
    const auto initialFlags = window->windowFlags();
    const auto initialHandle = window->winId();
    const QRect initialGeometry = window->geometry();
    for (const auto mode : {ThemeMode::Light, ThemeMode::Dark, ThemeMode::CatppuccinMocha}) {
        ApplicationThemeManager::instance().setMode(mode);
        if (!window->hasAcrylicBackdrop()) {
            QImage image(window->size(), QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            window->render(&painter);
            painter.end();
            const QColor pixel = image.pixelColor(2, 2);
            check(pixel == InsightVisualStyle::theme().panelBackground && pixel.alpha() == 255,
                  "floating_acrylic_fallback: unsupported composition paints an opaque themed background");
        }
        check(window->windowFlags() == initialFlags && window->winId() == initialHandle
                  && window->geometry() == initialGeometry,
              "floating_acrylic_theme: theme refresh preserves native identity and geometry");
    }
    ApplicationThemeManager::instance().setMode(initialTheme);
    window->setBackgroundOpacity(0);
    check(window->backgroundOpacity() == 60, "floating_opacity: runtime lower bound is enforced");
    window->setBackgroundOpacity(101);
    check(window->backgroundOpacity() == 100, "floating_opacity: runtime upper bound is enforced");
    QTemporaryDir temporary;
    SettingsCenterService service(temporary.filePath("settings.ini"));
    SettingsCenterPanel panel(&service, {});
    auto* slider = qobject_cast<QSlider*>(panel.fieldEditor("appearance.floatingContextOpacity"));
    QObject::connect(&panel, &SettingsCenterPanel::settingsApplied, window, [&] {
        window->setBackgroundOpacity(panel.snapshot().value("appearance.floatingContextOpacity").toInt());
    });
    check(slider && slider->minimum() == 60 && slider->maximum() == 100 && slider->value() == 90,
          "floating_opacity_setting: Settings exposes the bounded global slider with default value");
    if (slider) slider->setValue(81);
    check(service.load().value("appearance.floatingContextOpacity").toInt() == 81 && window->backgroundOpacity() == 81
              && !SettingsCenterSchema::field("appearance.floatingContextOpacity")->workspaceAllowed
              && SettingsCenterSchema::field("appearance.floatingContextOpacity")->storageKey
                     == QString::fromLatin1(SettingsCenterKeys::FloatingContextOpacity),
          "floating_opacity_setting: slider immediately persists the global preference and updates runtime");
}

void verifySingleFloatingInstance()
{
    PlacementFixture fixture;
    auto& controller = *fixture.controller;
    int nativeCount = 0;
    QPointer<QWidget> previousOverlay;
    for (bool detachable : {false, true, true, false}) {
        fixture.counters.detachable = detachable;
        const auto item = resource(QString::number(fixture.counters.created));
        controller.openResource(item, floatingPlacement);
        processDeferredDeletes();
        if (detachable) ++nativeCount;
        else {
            check(previousOverlay.isNull(), "single_overlay: replacement disposes only the previous overlay view");
            previousOverlay = controller.peekHost()->view();
        }
        check(controller.peekHost()->hasResource() && controller.floatingWindows().size() == nativeCount
                  && controller.floatingSurface()->resource() == item && !previousOverlay.isNull(),
              "multiple_floating_instances: native views coexist with one unchanged overlay");
    }
    const auto saved = controller.captureState();
    const QRect overlayGeometry = controller.peekHost()->geometry();
    QSignalSpy closed(&controller, &ContextWorkspaceController::resourceClosed);
    controller.setFloatingCollapsed(true);
    check(!controller.peekHost()->isVisible() && controller.peekHost()->hasResource()
              && controller.peekHost()->view() == previousOverlay && closed.isEmpty(),
          "collapse_overlay: one toggle hides the live overlay without disposing it");
    controller.setFloatingCollapsed(false);
    check(controller.peekHost()->isVisible() && controller.peekHost()->geometry() == overlayGeometry
              && sameState(saved, controller.captureState()) && closed.isEmpty(),
          "expand_overlay: the overlay and native layouts survive the same toggle exactly");
    controller.closeFloatingResource(controller.floatingWindows().first()->resource().stableKey());
    controller.peekHost()->setPreferredSize(QSize(500, 550));
    emit controller.peekHost()->preferredSizeResetRequested();
    check(controller.peekHost()->preferredSize() == QSize(460, 510)
              && controller.peekHost()->view() == previousOverlay,
          "multi_overlay_reset: overlay resets its own resource size after another native surface closes");
}

void verifyMultipleFloatingWindows()
{
    PlacementFixture fixture;
    fixture.counters.detachable = true;
    auto& controller = *fixture.controller;
    for (const QString& id : {QStringLiteral("one"), QStringLiteral("two"), QStringLiteral("three")})
        controller.openResource(resource(id), floatingPlacement);
    const auto hosts = controller.floatingWindows();
    check(hosts.size() == 3, "multi_instances: three native windows coexist for distinct resources");
    if (hosts.size() != 3) return;
    check(hosts[0]->isWindow() && hosts[1]->isWindow() && hosts[2]->isWindow()
              && hosts[0]->view() != hosts[1]->view() && hosts[1]->view() != hosts[2]->view()
              && hosts[0]->view() != hosts[2]->view()
              && hosts[0]->frameGeometry() != hosts[1]->frameGeometry()
              && hosts[1]->frameGeometry() != hosts[2]->frameGeometry()
              && hosts[0]->frameGeometry() != hosts[2]->frameGeometry()
              && hosts[1]->screen()->availableGeometry().contains(hosts[1]->frameGeometry())
              && hosts[2]->screen()->availableGeometry().contains(hosts[2]->frameGeometry()),
          "multi_cascade: native instances have distinct views and distinct visible rectangles");
    QWidget* original = hosts[0]->view();
    const int created = fixture.counters.created;
    controller.openResource(resource("one"), floatingPlacement);
    check(controller.floatingWindows().size() == 3 && hosts[0]->view() == original && fixture.counters.created == created,
          "multi_unique_resource: opening an existing resource focuses without creating a view");
    const auto saved = controller.captureState();
    QSignalSpy closed(&controller, &ContextWorkspaceController::resourceClosed);
    controller.setFloatingCollapsed(true);
    check(!hosts[0]->isVisible() && !hosts[1]->isVisible() && !hosts[2]->isVisible()
              && hosts[0]->hasResource() && hosts[1]->hasResource() && hosts[2]->hasResource() && closed.isEmpty(),
          "multi_collapse: hiding preserves every resource and emits no close signal");
    controller.setFloatingCollapsed(false);
    check(hosts[0]->isVisible() && hosts[1]->isVisible() && hosts[2]->isVisible()
              && sameState(saved, controller.captureState()) && closed.isEmpty(),
          "multi_expand: all captured geometry fields survive hide and restore exactly");
    QApplication::processEvents();
    controller.focusResource(resource("two").stableKey());
    QApplication::processEvents();
    controller.dockWidget()->hide();
    fixture.window.centralWidget()->setFocusPolicy(Qt::StrongFocus);
    fixture.window.activateWindow();
    fixture.window.centralWidget()->setFocus();
    QApplication::processEvents();
    controller.rail()->actions().constFirst()->trigger();
    check(controller.floatingSurface()->view() == hosts[1]->view() && hosts[1]->isVisible() && fixture.counters.created == created,
          "multi_rail_focus: left click selects the most recently focused instance without cycling");
    controller.rail()->actions().constFirst()->trigger();
    check(!hosts[1]->isVisible() && hosts[1]->hasResource() && closed.isEmpty(),
          "multi_rail_hide: left click hides the active native instance without closing");
    controller.rail()->actions().constFirst()->trigger();
    check(hosts[1]->isVisible() && fixture.counters.created == created,
          "multi_rail_restore: left click restores the same most recent native instance");
    std::unique_ptr<QMenu> menu(controller.createRailContextMenu("mock"));
    auto* collect = menu->findChild<QAction*>("contextCollectFloating");
    check(collect && collect->isEnabled(), "multi_menu: controller offers collecting existing floating views");
    if (collect) collect->trigger();
    check(controller.floatingWindows().isEmpty() && controller.dockHost()->resourceCount() == 3
              && controller.dockHost()->viewForResource(resource("one").stableKey()) == original && fixture.counters.created == created,
          "multi_collect: every native view moves to the sidebar without reconstruction");
}

void verifyFloatingRestoreLimit()
{
    PlacementFixture fixture;
    fixture.counters.detachable = true;
    auto& controller = *fixture.controller;
    ContextWorkspaceState saved;
    saved.valid = true;
    saved.floatingCollapsed = true;
    const QRect available = fixture.window.screen()->availableGeometry();
    for (int i = 0; i < 20; ++i) {
        ContextFloatingInstanceState instance;
        instance.resource = resource(QString::number(i)).toVariantMap();
        instance.x = available.x(); instance.y = available.y();
        instance.width = ContextWorkspaceState::boundedPeekWidth(available.width() / 2);
        instance.height = ContextWorkspaceState::boundedPeekHeight(available.height() / 2);
        instance.geometryValid = true;
        instance.screenName = fixture.window.screen()->name();
        saved.floatingInstances.append(instance);
    }
    const auto result = controller.restoreState(saved);
    check(controller.floatingWindows().size() == 16 && result.restoredResources == 16
              && result.skippedResources == 4 && !result.warnings.isEmpty(),
          "multi_restore_limit: restores exactly 16 instances and reports all four excess entries");
    bool hidden = true;
    for (auto* host : controller.floatingWindows()) hidden &= !host->isVisible();
    check(hidden && controller.floatingCollapsed(), "multi_restore_collapsed: persisted collapse state keeps all restored views hidden");
    auto* toggle = controller.rail()->findChild<QAction*>("contextFloatingCollapse");
    check(toggle && toggle->isChecked() && toggle->text() == "Restore floating views",
          "multi_restore_toggle: persisted collapse state restores the matching toggle label");
    QTemporaryDir temporary;
    WorkspaceSessionStateService service(temporary.filePath("instances.ini"));
    WorkspaceSessionState session;
    session.workspaceRoot = controller.workspaceRoot();
    session.ui.contextWorkspace = controller.captureState();
    check(service.save(session).saved && sameState(session.ui.contextWorkspace, service.load(session.workspaceRoot).state.ui.contextWorkspace),
          "v5_wire_roundtrip: all 16 resource geometries and collapsed state survive production serialization");
    controller.setFloatingCollapsed(false);
    auto* host = controller.floatingWindows().first();
    const auto& first = saved.floatingInstances.first();
    check(host->frameGeometry() == QRect(first.x, first.y, first.width, first.height),
          "native_frame_invariant: setView returns the exact restored outer rectangle after native show");
}

void verifyV4ContextInput()
{
    QTemporaryDir temporary;
    WorkspaceSessionStateService service(temporary.filePath("v4.ini"));
    WorkspaceSessionState session;
    session.workspaceRoot = temporary.path();
    auto& expected = session.ui.contextWorkspace;
    expected.valid = true;
    expected.peekWidth = 620; expected.peekHeight = 530; expected.dockWidth = 490;
    expected.dockVisible = true; expected.railVisible = false;
    expected.pinnedResources = {resource("v4").toVariantMap()};
    expected.activePinnedResourceKey = "mock:v4";
    expected.dockSections = {{"mock:v4", false, 0}};
    expected.providerStates = {{"mock", QVariantMap{{"query", "v4"}}}};
    expected.floatingX = 80; expected.floatingY = 90;
    expected.floatingWidth = 600; expected.floatingHeight = 500;
    expected.floatingScreenName = "saved screen"; expected.floatingGeometryValid = true;
    service.save(session);
    QSettings settings(temporary.filePath("v4.ini"), QSettings::IniFormat);
    const QString key = settings.allKeys().first();
    auto document = QJsonDocument::fromJson(settings.value(key).toByteArray()).object();
    auto ui = document.value("ui").toObject();
    auto context = ui.value("contextWorkspace").toObject();
    context.insert("version", 4);
    context.insert("floatingCollapsed", true);
    context.insert("floatingInstances", QJsonArray{QJsonObject{{"resource", QJsonObject::fromVariantMap(resource("ignored").toVariantMap())}}});
    ui.insert("contextWorkspace", context); document.insert("ui", ui);
    settings.setValue(key, QJsonDocument(document).toJson(QJsonDocument::Compact)); settings.sync();
    const auto loaded = service.load(temporary.path());
    check(loaded.loaded && sameState(expected, loaded.state.ui.contextWorkspace)
              && loaded.state.ui.contextWorkspace.floatingInstances.isEmpty() && !loaded.state.ui.contextWorkspace.floatingCollapsed,
          "v4_input: every legacy field survives while v5 instance and collapse keys are ignored");
}

void verifyRailContextMenuSignal()
{
    ContextRail rail;
    rail.addEntry({"provider", "Provider", {}, {}});
    rail.resize(60, 200); rail.show(); QApplication::processEvents();
    QSignalSpy requested(&rail, &ContextRail::entryContextMenuRequested);
    const QPoint point = rail.actionGeometry(rail.actions().first()).center();
    QMetaObject::invokeMethod(&rail, "customContextMenuRequested", Qt::DirectConnection, Q_ARG(QPoint, point));
    check(requested.size() == 1 && requested.first().at(0).toString() == "provider"
              && requested.first().at(1).toPoint() == rail.mapToGlobal(point),
          "rail_context_menu_signal: rail translates the requested action position into id and global coordinates");
}

void verifyDocumentBindingVisibility()
{
    PlacementFixture fixture;
    fixture.counters.detachable = true;
    auto& controller = *fixture.controller;
    controller.setActiveDocument("rtl/a.sv");
    const ContextPlacement bound{ContextSurface::Floating, ContextPersistence::Transient, ContextBinding::DocumentBound};
    check(controller.openResource(resource("bound-a"), bound), "binding_open: native transient document binding is supported");
    auto* host = controller.floatingWindow();
    const auto original = host->resource();
    QWidget* view = host->view();
    const QRect rectangle = host->frameGeometry();
    controller.openResource(resource("global"), floatingPlacement);
    auto* global = controller.floatingWindow();
    QSignalSpy closed(&controller, &ContextWorkspaceController::resourceClosed);
    controller.setActiveDocument("rtl/b.sv");
    check(!host->isVisible() && host->hasResource() && global->isVisible() && closed.isEmpty(),
          "binding_visibility: switching documents hides bound views and retains global views");
    check(host->resource() == original && host->view() == view,
          "binding_content: changing active documents never replaces resource identity or contents");
    controller.setActiveDocument("rtl/a.sv");
    check(host->isVisible() && host->frameGeometry() == rectangle && host->resource() == original && host->view() == view,
          "binding_return: switching back restores the original view and exact outer geometry");
    controller.setFloatingCollapsed(true);
    controller.setActiveDocument("rtl/b.sv");
    controller.setActiveDocument("rtl/a.sv");
    check(!host->isVisible() && !global->isVisible() && host->hasResource() && closed.isEmpty(),
          "binding_collapse_precedence: active bound views remain hidden while all views are collapsed");
    controller.setFloatingCollapsed(false);
    const int created = fixture.counters.created;
    check(controller.setResourceBinding(original.stableKey(), ContextBinding::Global)
              && controller.boundDocument(original.stableKey()).isEmpty() && host->view() == view,
          "binding_to_global: binding changes keep the same live QWidget");
    controller.setActiveDocument("rtl/b.sv");
    check(host->isVisible() && controller.setResourceBinding(original.stableKey(), ContextBinding::DocumentBound)
              && controller.boundDocument(original.stableKey()) == "rtl/b.sv" && host->view() == view
              && fixture.counters.created == created,
          "binding_to_document: global to document changes preserve identity and creation count");
    controller.setActiveDocument("rtl/a.sv");
    check(!host->isVisible() && controller.captureState().documentFloatingLayouts.value("rtl/a.sv").isEmpty(),
          "binding_reassignment: previous document records cannot resurrect a moved resource");
    QString reason;
    check(!controller.openResource(resource("docked-bound"), {ContextSurface::Docked, ContextPersistence::Kept, ContextBinding::DocumentBound}, &reason)
              && !reason.isEmpty() && fixture.counters.created == created,
          "binding_docked_rejected: sidebar document binding remains unsupported without creating a view");
}

void verifyNoActiveDocumentBinding()
{
    PlacementFixture fixture;
    fixture.counters.detachable = true;
    fixture.counters.activation = resource("menu-bound");
    auto& controller = *fixture.controller;
    controller.setActiveDocument("");
    QString reason;
    check(!controller.openResource(resource("no-document"), {ContextSurface::Floating, ContextPersistence::Transient, ContextBinding::DocumentBound}, &reason)
              && !reason.isEmpty() && fixture.counters.created == 0 && controller.floatingWindows().isEmpty(),
          "binding_no_active_document: binding fails explicitly without falling back or constructing views");
    std::unique_ptr<QMenu> menu(controller.createRailContextMenu("mock"));
    auto* bound = menu->findChild<QAction*>("contextNewDocumentFloating");
    check(bound && !bound->isEnabled() && !bound->toolTip().isEmpty(),
          "binding_menu_disabled: no-document creation has a disabled action and explanatory tooltip");
    controller.setActiveDocument("rtl/a.sv");
    menu.reset(controller.createRailContextMenu("mock"));
    bound = menu->findChild<QAction*>("contextNewDocumentFloating");
    check(bound && bound->isEnabled(), "binding_menu_enabled: native provider can create a document-bound view");
    if (bound) bound->trigger();
    QWidget* view = controller.floatingWindow()->view();
    menu.reset(controller.createRailContextMenu("mock"));
    auto* global = menu->findChild<QAction*>("contextBindingGlobal");
    if (global) global->trigger();
    check(global && controller.boundDocument(resource("menu-bound").stableKey()).isEmpty()
              && controller.floatingWindow()->view() == view && fixture.counters.created == 1,
          "binding_menu_switch: controller-built binding actions reuse the existing native view");
}

void verifyDocumentLayoutRoundtrip()
{
    PlacementFixture fixture;
    fixture.counters.detachable = true;
    auto& controller = *fixture.controller;
    controller.setActiveDocument("rtl/a.sv");
    controller.openResource(resource("a-one"), {ContextSurface::Floating, ContextPersistence::Transient, ContextBinding::DocumentBound});
    check(controller.openResource(resource("a-two"), {ContextSurface::Floating, ContextPersistence::Kept, ContextBinding::DocumentBound}),
          "binding_kept_open: kept native document-bound placements are supported");
    const auto saved = controller.captureState();
    QPointer<QWidget> first = controller.floatingWindows()[0]->view();
    QPointer<QWidget> second = controller.floatingWindows()[1]->view();
    controller.setActiveDocument("rtl/b.sv");
    controller.documentClosed("rtl/a.sv");
    processDeferredDeletes();
    check(first.isNull() && second.isNull() && controller.floatingWindows().isEmpty()
              && controller.captureState().documentFloatingLayouts.value("rtl/a.sv") == saved.documentFloatingLayouts.value("rtl/a.sv"),
          "document_close_archives: closing an inactive document releases views while retaining its exact layout");
    const int created = fixture.counters.created;
    controller.setActiveDocument("rtl/a.sv");
    check(controller.floatingWindows().size() == 2 && fixture.counters.created == created + 2
              && controller.captureState().documentFloatingLayouts.value("rtl/a.sv") == saved.documentFloatingLayouts.value("rtl/a.sv"),
          "document_reopen_layout: reopening restores both resources, persistence and exact geometry fields");
    const auto restored = controller.captureState();
    QTemporaryDir temporary;
    WorkspaceSessionStateService service(temporary.filePath("documents.ini"));
    WorkspaceSessionState session;
    session.workspaceRoot = controller.workspaceRoot(); session.ui.contextWorkspace = restored;
    check(service.save(session).saved && sameState(restored, service.load(session.workspaceRoot).state.ui.contextWorkspace),
          "document_layout_wire: path records and explicit recent-use order survive production serialization");
    controller.restoreState(restored);
    check(sameState(restored, controller.captureState()) && controller.floatingWindows().size() == 2,
          "document_layout_restore: restoring workspace state rebuilds the active document layout exactly");
    auto* host = controller.floatingWindows().last();
    QWidget* view = host->view();
    check(controller.setResourceBinding(host->resource().stableKey(), ContextBinding::Global)
              && host->view() == view && controller.captureState().floatingInstances.first().kept,
          "binding_kept_global: changing kept binding retains persistence and the live QWidget");
    const auto keptGlobal = controller.captureState();
    controller.restoreState(keptGlobal);
    check(sameState(keptGlobal, controller.captureState()),
          "binding_kept_global_restore: changing to global leaves a supported persisted placement");
    controller.closeFloatingResource(resource("a-one").stableKey());
    controller.setActiveDocument("rtl/b.sv"); controller.setActiveDocument("rtl/a.sv");
    check(controller.floatingWindows().size() == 1,
          "document_explicit_close: closing a view removes its record so switching back does not resurrect it");
}

void verifyDocumentLayoutEviction()
{
    PlacementFixture fixture;
    fixture.counters.detachable = true;
    auto& controller = *fixture.controller;
    for (int i = 0; i < 33; ++i) {
        const QString path = QStringLiteral("rtl/%1.sv").arg(i);
        controller.setActiveDocument(path);
        controller.openResource(resource(QString::number(i)), {ContextSurface::Floating, ContextPersistence::Transient, ContextBinding::DocumentBound});
        controller.documentClosed(path);
    }
    auto saved = controller.captureState();
    check(saved.documentFloatingLayouts.size() == 32 && saved.documentFloatingOrder.size() == 32
              && !saved.documentFloatingLayouts.contains("rtl/0.sv") && saved.documentFloatingOrder.first() == "rtl/1.sv",
          "document_layout_limit: 33 archived documents retain 32 and evict the oldest explicit-use entry");
    controller.setActiveDocument("rtl/1.sv"); controller.documentClosed("rtl/1.sv");
    controller.setActiveDocument("rtl/33.sv");
    controller.openResource(resource("33"), {ContextSurface::Floating, ContextPersistence::Transient, ContextBinding::DocumentBound});
    controller.documentClosed("rtl/33.sv");
    saved = controller.captureState();
    check(saved.documentFloatingLayouts.size() == 32 && saved.documentFloatingLayouts.contains("rtl/1.sv")
              && !saved.documentFloatingLayouts.contains("rtl/2.sv") && saved.documentFloatingOrder.last() == "rtl/33.sv",
          "document_layout_recency: revisiting an older document updates eviction order deterministically");
    controller.restoreState(saved);
    check(sameState(saved, controller.captureState()), "document_layout_lru_restore: all 32 archived layouts and their order restore exactly");
}

void verifySidebarStack()
{
    PlacementFixture fixture;
    auto& controller = *fixture.controller;
    auto* host = controller.dockHost();
    for (const auto& id : {"stack-a", "stack-b", "stack-c"}) controller.openResource(resource(id), keptPlacement);
    QApplication::processEvents();
    const auto keys = host->resourceKeys();
    bool visible = keys.size() == 3;
    for (const auto& key : keys)
        visible &= host->viewForResource(key)->isVisible() && host->sectionWidget(key)->height() > host->sectionHeader(key)->height();
    check(visible && host->viewForResource(keys[0]) != host->viewForResource(keys[1])
              && host->viewForResource(keys[1]) != host->viewForResource(keys[2])
              && host->viewForResource(keys[0]) != host->viewForResource(keys[2]),
          "stack_visible: three distinct content views are visible in one dock");
    QWidget* middle = host->viewForResource(keys[1]);
    const int created = fixture.counters.created;
    const QRect geometry = host->sectionWidget(keys[1])->geometry();
    host->setSectionCollapsed(keys[1], true);
    check(!middle->isVisible() && host->viewForResource(keys[1]) == middle && fixture.counters.created == created,
          "stack_collapse: hiding the middle content retains the view");
    host->setSectionCollapsed(keys[1], false);
    check(host->sectionWidget(keys[1])->geometry() == geometry && middle->isVisible(),
          "stack_expand: expanded geometry returns exactly");
    for (const auto& key : keys) host->setSectionCollapsed(key, true);
    check(controller.dockWidget()->isVisible() && host->resourceCount() == 3,
          "stack_all_collapsed: the dock remains a visible column of headers");
    for (const auto& key : keys) host->setSectionCollapsed(key, false);
    QSignalSpy reordered(host, &ContextDockHost::resourceOrderChanged);
    const QPoint start = host->sectionHeader(keys[2])->mapToGlobal(QPoint(80, 10));
    const QPoint end = host->sectionHeader(keys[0])->mapToGlobal(QPoint(80, 1));
    sendMouseEvent(host->sectionHeader(keys[2]), QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    sendMouseEvent(host->sectionHeader(keys[2]), QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
    sendMouseEvent(host->sectionHeader(keys[2]), QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
    check(reordered.count() == 1 && host->resourceKeys().first() == keys[2]
              && ContextResource::fromVariantMap(controller.captureState().pinnedResources.first()).stableKey() == keys[2],
          "stack_reorder: title drag changes order, emits signal and persists resource order");
    controller.focusResource(keys[1]);
    fixture.window.activateWindow();
    fixture.window.centralWidget()->setFocusPolicy(Qt::StrongFocus);
    fixture.window.centralWidget()->setFocus();
    QApplication::processEvents();
    controller.rail()->actions().first()->trigger();
    controller.rail()->actions().first()->trigger();
    check(host->isSectionCollapsed(keys[1]) && !host->isSectionCollapsed(keys[0]) && controller.dockWidget()->isVisible(),
          "stack_rail: two clicks focus then collapse only the MRU section");
    host->setSectionHeight(keys[0], fixture.window.screen()->availableGeometry().height() / 3);
    const auto saved = controller.captureState();
    QTemporaryDir temporary;
    WorkspaceSessionStateService service(temporary.filePath("v6.ini"));
    WorkspaceSessionState session; session.workspaceRoot = controller.workspaceRoot(); session.ui.contextWorkspace = saved;
    service.save(session);
    controller.restoreState(service.load(session.workspaceRoot).state.ui.contextWorkspace);
    check(ContextWorkspaceState::kVersion == 7 && sameState(saved, controller.captureState()),
          "stack_v6_roundtrip: order, collapsed states and requested heights survive the production serializer");
    controller.dockWidget()->toggleViewAction()->trigger();
    check(!controller.dockWidget()->isVisible(), "stack_hide_whole: the dock toggle still hides the entire sidebar");
}

void verifySectionPreferredHeight()
{
    // Heights are derived from the screen: the offscreen platform reports only
    // 400x400 logical pixels at 200%, where any hard-coded height would lie.
    const int available =
        QApplication::primaryScreen()->availableGeometry().height();
    const int suggested = available / 2;
    {
        PlacementFixture fixture;
        auto& controller = *fixture.controller;
        const ContextResource item = resource(QStringLiteral("height-default"));
        check(controller.openResource(item, keptPlacement),
              "section_height_default_open: kept resource opens");
        QApplication::processEvents();
        check(controller.dockHost()->sectionHeight(item.stableKey()) == 0,
              "section_height_default: a provider suggesting nothing leaves the "
              "section on the dock's own split");
    }
    {
        PlacementFixture fixture;
        fixture.counters.preferredSectionHeight = suggested;
        auto& controller = *fixture.controller;
        auto* host = controller.dockHost();
        const ContextResource item =
            resource(QStringLiteral("height-suggested"));
        const QString key = item.stableKey();
        check(controller.openResource(item, keptPlacement),
              "section_height_suggested_open: kept resource opens");
        QApplication::processEvents();
        check(host->sectionHeight(key) == suggested,
              "section_height_suggested: the section starts at the height its "
              "provider suggested");
        // The suggestion is a starting point, not a floor.
        const int dragged = suggested / 2;
        check(host->setSectionHeight(key, dragged)
                  && host->sectionHeight(key) == dragged,
              "section_height_draggable: the user can still drag the section "
              "below the suggested height");
        const ContextWorkspaceState saved = controller.captureState();
        bool persisted = false;
        for (const auto& section : saved.dockSections) {
            if (section.resourceKey == key && section.height == dragged)
                persisted = true;
        }
        check(persisted,
              "section_height_persisted: the dragged height is what state "
              "capture records");
        controller.restoreState(saved);
        QApplication::processEvents();
        check(controller.dockHost()->sectionHeight(key) == dragged,
              "section_height_restore_user: a stored height the user chose wins "
              "over the suggestion");
    }
    {
        // State written before section heights were suggested stores 0, which
        // must not reset the section the provider just sized.
        PlacementFixture fixture;
        fixture.counters.preferredSectionHeight = suggested;
        auto& controller = *fixture.controller;
        const ContextResource item = resource(QStringLiteral("height-legacy"));
        const QString key = item.stableKey();
        check(controller.openResource(item, keptPlacement),
              "section_height_legacy_open: kept resource opens");
        QApplication::processEvents();
        ContextWorkspaceState state = controller.captureState();
        for (auto& section : state.dockSections) {
            if (section.resourceKey == key)
                section.height = 0;
        }
        controller.restoreState(state);
        QApplication::processEvents();
        check(controller.dockHost()->sectionHeight(key) == suggested,
              "section_height_restore_legacy: a stored height of zero keeps the "
              "suggested height instead of clearing it");
    }
}

void verifySectionStatusFromView()
{
    PlacementFixture fixture;
    auto& controller = *fixture.controller;
    const ContextResource item = resource(QStringLiteral("status-a"));
    const QString key = item.stableKey();
    check(controller.openResource(item, keptPlacement), "section_status_open: kept resource opens");
    QApplication::processEvents();
    auto* host = controller.dockHost();
    QLabel* status = host->sectionStatus(key);
    check(status && !status->isVisible(),
          "section_status_absent: a view publishing no status leaves the header unchanged");
    QWidget* view = host->viewForResource(key);
    view->setProperty("contextStatusText", QStringLiteral("Stale"));
    view->setProperty("contextStatusTooltip", QStringLiteral("last valid result"));
    QApplication::processEvents();
    check(status->isVisible() && status->text() == QStringLiteral("Stale")
              && status->toolTip() == QStringLiteral("last valid result"),
          "section_status_published: the header shows status published by the view");
    view->setProperty("contextStatusText", QString());
    QApplication::processEvents();
    check(!status->isVisible(),
          "section_status_cleared: clearing the property hides the header chip");
}

void verifySidebarVisibilityToggle()
{
    PlacementFixture fixture;
    auto& controller = *fixture.controller;
    QString reason;
    check(!controller.dockVisible() && controller.canShowDock()
              && !controller.setDockVisible(true, &reason) && !reason.isEmpty(),
          "sidebar_visibility_no_activation: a provider without an activation resource fails with a reason");
    const ContextResource item = resource(QStringLiteral("visibility-a"));
    const QString key = item.stableKey();
    check(controller.openResource(item, keptPlacement) && controller.dockVisible()
              && controller.canShowDock(),
          "sidebar_visibility_open: opening a kept resource shows the sidebar");
    QApplication::processEvents();
    fixture.window.resizeDocks({controller.dockWidget()}, {520}, Qt::Horizontal);
    QApplication::processEvents();
    const int width = controller.dockWidget()->width();
    QWidget* view = controller.dockHost()->viewForResource(key);
    const int created = fixture.counters.created;
    check(controller.setDockVisible(false) && !controller.dockVisible()
              && controller.dockHost()->resourceCount() == 1
              && !controller.captureState().dockVisible,
          "sidebar_visibility_hide: hiding keeps the section and persists the hidden state");
    check(controller.setDockVisible(false) && !controller.dockVisible(),
          "sidebar_visibility_idempotent: hiding an already hidden sidebar succeeds");
    check(controller.setDockVisible(true, &reason) && controller.dockVisible()
              && controller.dockHost()->viewForResource(key) == view
              && fixture.counters.created == created
              && qAbs(controller.dockWidget()->width() - width) <= 2,
          "sidebar_visibility_show: showing again reuses the view and its width");
    controller.rail()->hide();
    check(controller.setDockVisible(false) && !controller.rail()->isVisible()
              && controller.dockHost()->resourceCount() == 1,
          "sidebar_visibility_hide_keeps_rail_state: hiding the sidebar does not touch the rail");
    check(controller.setDockVisible(true, &reason) && controller.dockVisible()
              && controller.rail()->isVisible()
              && controller.captureState().railVisible,
          "sidebar_visibility_restores_rail: showing the sidebar restores a hidden rail");
    check(controller.closePinnedResource(key) && !controller.dockVisible()
              && controller.canShowDock(),
          "sidebar_visibility_last_close: closing the last section hides the sidebar but keeps it reachable");
    fixture.counters.activation = resource(QStringLiteral("visibility-default"));
    controller.rail()->hide();
    const int createdBeforeRecovery = fixture.counters.created;
    check(controller.setDockVisible(true, &reason) && controller.dockVisible()
              && controller.rail()->isVisible()
              && controller.dockHost()->resourceCount() == 1
              && controller.dockHost()->resourceAt(0).stableKey()
                     == resource(QStringLiteral("visibility-default")).stableKey()
              && fixture.counters.created == createdBeforeRecovery + 1,
          "sidebar_visibility_recovers_empty: an empty sidebar opens the first rail provider instead of refusing");
}
void verifySidebarCompression()
{
    ContextDockHost host;
    const QRect available = QGuiApplication::primaryScreen()->availableGeometry();
    host.resize(available.width() / 2, available.height() / 2);
    for (const auto& id : {"compress-a", "compress-b", "compress-c"}) host.addResource(resource(id), new QLabel(id));
    host.show(); QApplication::processEvents();
    const auto keys = host.resourceKeys();
    const int desired = available.height() / 3;
    for (const auto& key : keys) host.setSectionHeight(key, desired);
    host.activateResource(keys[1]);
    host.resize(host.width(), qMax(260, available.height() * 3 / 5));
    QApplication::processEvents();
    check(qAbs(host.sectionWidget(keys[0])->height() - host.sectionWidget(keys[1])->height()) <= 1
              && qAbs(host.sectionWidget(keys[2])->height() - host.sectionWidget(keys[1])->height()) <= 1
              && host.sectionWidget(keys[0])->height() < host.sectionHeight(keys[0]),
          "stack_compression: equal section weights share available height regardless of focus");
    auto* handle = host.sectionWidget(keys[1])->findChild<QWidget*>("contextSectionResize");
    const int before = host.sectionHeight(keys[1]);
    dragHandle(handle, QPoint(0, available.height() / 10));
    check(host.sectionHeight(keys[1]) != before, "stack_resize: the section boundary changes its persisted height");
}

void verifyV5SidebarMigration()
{
    QTemporaryDir temporary;
    WorkspaceSessionStateService service(temporary.filePath("v5.ini"));
    WorkspaceSessionState session; session.workspaceRoot = temporary.path();
    auto& expected = session.ui.contextWorkspace;
    expected.valid = true; expected.dockVisible = true;
    expected.activePinnedResourceKey = resource("migration-b").stableKey();
    const QRect available = QGuiApplication::primaryScreen()->availableGeometry();
    expected.floatingX = available.left() + available.width() / 20;
    expected.floatingY = available.top() + available.height() / 20;
    expected.floatingCollapsed = true;
    expected.floatingWidth = ContextWorkspaceState::boundedPeekWidth(available.width() / 2);
    expected.floatingHeight = ContextWorkspaceState::boundedPeekHeight(available.height() / 2);
    expected.floatingScreenName = "v5 screen"; expected.floatingGeometryValid = true;
    expected.providerStates = {{"mock", QVariantMap{{"v5-query", "saved"}}}};
    ContextFloatingInstanceState global;
    global.resource = resource("migration-global").toVariantMap();
    global.x = expected.floatingX; global.y = expected.floatingY;
    global.width = expected.floatingWidth; global.height = expected.floatingHeight;
    global.screenName = "v5 instance screen"; global.geometryValid = true; global.kept = true;
    expected.floatingInstances.append(global);
    auto bound = global; bound.resource = resource("migration-bound").toVariantMap();
    expected.documentFloatingLayouts.insert("rtl/archived.sv", {bound});
    expected.documentFloatingOrder = {"rtl/archived.sv"};
    for (const auto& id : {"migration-a", "migration-b", "migration-c"}) expected.pinnedResources.append(resource(id).toVariantMap());
    service.save(session);
    QSettings settings(temporary.filePath("v5.ini"), QSettings::IniFormat);
    const QString storageKey = settings.allKeys().first();
    auto json = QJsonDocument::fromJson(settings.value(storageKey).toByteArray()).object();
    auto ui = json.value("ui").toObject(); auto context = ui.value("contextWorkspace").toObject();
    context.insert("version", 5); context.remove("dockSections");
    ui.insert("contextWorkspace", context); json.insert("ui", ui);
    settings.setValue(storageKey, QJsonDocument(json).toJson(QJsonDocument::Compact)); settings.sync();
    for (const auto& id : {"migration-a", "migration-b", "migration-c"})
        expected.dockSections.append({resource(id).stableKey(), QString(id) != "migration-b", 0});
    const auto loaded = service.load(session.workspaceRoot);
    check(loaded.loaded && sameState(expected, loaded.state.ui.contextWorkspace),
          "stack_v5_migration: every v5 field survives with only the former active section expanded");
    PlacementFixture fixture;
    fixture.controller->restoreState(loaded.state.ui.contextWorkspace);
    auto* host = fixture.controller->dockHost();
    check(host->isSectionCollapsed("mock:migration-a") && !host->isSectionCollapsed("mock:migration-b")
              && host->isSectionCollapsed("mock:migration-c"), "stack_v5_live: migrated collapse states apply to all three sections");
}

void verifySidebarDragOut()
{
    PlacementFixture fixture;
    fixture.counters.detachable = true;
    auto& controller = *fixture.controller;
    auto* host = controller.dockHost();
    for (const auto& id : {"drag-a", "drag-b", "drag-c"}) controller.openResource(resource(id), keptPlacement);
    QApplication::processEvents();
    const auto keys = host->resourceKeys();
    host->setSectionCollapsed(keys[1], true);
    QWidget* view = host->viewForResource(keys[2]);
    const int created = fixture.counters.created;
    const int previousHeight = host->sectionHeight(keys[0]);
    const QSize previousSize = host->sectionWidget(keys[0])->size();
    const QRect available = fixture.window.screen()->availableGeometry();
    const QPoint destination(available.left() - available.width(), available.bottom() + available.height() / 4);
    QWidget* handle = host->sectionDragHandle(keys[2]);
    check(handle && handle->isEnabled(), "stack_drag_handle: detachable section has an enabled explicit grip");
    const QPoint start = handle->mapToGlobal(handle->rect().center());
    sendMouseEvent(handle, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    sendMouseEvent(handle, QEvent::MouseMove, destination, Qt::NoButton, Qt::LeftButton);
    sendMouseEvent(handle, QEvent::MouseButtonRelease, destination, Qt::LeftButton, Qt::NoButton);
    auto* floating = controller.floatingWindow();
    check(floating->view() == view && fixture.counters.created == created && !host->containsResource(keys[2])
              && host->sectionHeight(keys[0]) == previousHeight && host->sectionWidget(keys[0])->height() >= previousSize.height()
              && host->isSectionCollapsed(keys[1]), "stack_drag_out_identity: title gesture transports the view and preserves neighboring sections");
    check(floating->hasResource() && available.contains(floating->frameGeometry()),
          "stack_drag_out_geometry: outside-screen drop resolves to a fully visible native rectangle");
    fixture.counters.detachable = false;
    const auto blocked = resource("drag-blocked");
    controller.openResource(blocked, keptPlacement);
    QString reason;
    check(!host->sectionDragHandle(blocked.stableKey())->isEnabled()
              && !host->sectionDragHandle(blocked.stableKey())->toolTip().isEmpty()
              && !controller.dragOutResource(blocked.stableKey(), destination, &reason) && !reason.isEmpty()
              && controller.floatingWindows().size() == 1 && host->containsResource(blocked.stableKey()),
          "stack_drag_opt_out: non-detachable grip is disabled and forced detach is rejected without moving the resource");
}

void verifySidebarDragBack()
{
    PlacementFixture fixture;
    fixture.counters.detachable = true;
    auto& controller = *fixture.controller;
    auto* host = controller.dockHost();
    for (const auto& id : {"drop-a", "drop-b", "drop-c"}) controller.openResource(resource(id), keptPlacement);
    const auto floatingResource = resource("drop-floating");
    controller.openResource(floatingResource, floatingPlacement);
    auto* floating = controller.floatingWindow();
    QWidget* original = floating->view();
    const int created = fixture.counters.created;
    check(floating->sidebarDragHandle() && floating->sidebarDragHandle()->isEnabled()
              && floating->sidebarDragHandle()->parentWidget() == floating,
          "stack_drag_client_handle: drag back begins inside the native window client area");
    QApplication::processEvents();
    for (const auto& key : host->resourceKeys()) host->setSectionCollapsed(key, true);
    QWidget* second = host->sectionWidget("mock:drop-b");
    const QPoint upper = second->mapToGlobal(QPoint(second->width() / 2, second->height() / 4));
    check(host->acceptFloatingDrop(floating, floatingResource.stableKey(), upper)
              && host->resourceKeys().indexOf(floatingResource.stableKey()) == 1,
          "stack_drop_upper: upper half inserts before the second section");
    check(host->viewForResource(floatingResource.stableKey()) == original && fixture.counters.created == created
              && !floating->isVisible() && !floating->hasResource() && controller.floatingWindows().isEmpty(),
          "stack_drop_identity: drag back retains QWidget and closes the empty native surface with no duplicate resource");
    controller.unpinResource(floatingResource.stableKey()); floating = controller.floatingWindow();
    second = host->sectionWidget("mock:drop-b");
    const QPoint lower = second->mapToGlobal(QPoint(second->width() / 2, second->height() * 3 / 4));
    check(host->acceptFloatingDrop(floating, floatingResource.stableKey(), lower)
              && host->resourceKeys().indexOf(floatingResource.stableKey()) == 2,
          "stack_drop_lower: lower half inserts after the second section");
    controller.unpinResource(floatingResource.stableKey()); floating = controller.floatingWindow();
    const QPoint blank = host->mapToGlobal(QPoint(host->width() / 2, host->height() - 5));
    check(host->acceptFloatingDrop(floating, floatingResource.stableKey(), blank)
              && host->resourceKeys().last() == floatingResource.stableKey()
              && host->viewForResource(floatingResource.stableKey()) == original && fixture.counters.created == created,
          "stack_drop_blank: empty area appends without reconstructing the view");
    PlacementFixture foreign;
    foreign.counters.detachable = true;
    foreign.controller->openResource(resource("foreign"), floatingPlacement);
    check(!host->acceptFloatingDrop(foreign.controller->floatingWindow(), "mock:foreign", blank)
              && foreign.controller->floatingWindow()->hasResource(), "stack_drop_scope: another workspace window cannot donate a resource");
    for (const auto& key : host->resourceKeys()) controller.closePinnedResource(key);
    controller.openResource(resource("empty-target"), floatingPlacement);
    floating = controller.floatingWindow();
    emit floating->sidebarDragStarted();
    check(controller.dockWidget()->isVisible(), "stack_drop_empty_target: a drag exposes even an empty hidden sidebar");
    emit floating->sidebarDragFinished(false);
    check(!controller.dockWidget()->isVisible() && floating->hasResource(), "stack_drop_cancel: canceled drag restores the previous sidebar visibility");
}

void verifyTiledBottomAndSidebar()
{
    PlacementFixture fixture;
    fixture.counters.detachable = true;
    fixture.window.resize(1600, 900);
    auto& controller = *fixture.controller;
    auto* host = controller.dockHost();
    check(controller.openResource(resource("side"), keptPlacement), "tiles_side_open");
    QWidget* firstView = nullptr;
    for (const auto& id : {"bottom-a", "bottom-b"}) {
        const auto item = resource(id);
        check(controller.openResource(item, floatingPlacement), "tiles_float_open");
        auto* floating = controller.floatingWindow();
        QWidget* original = floating->view();
        if (!firstView) firstView = original;
        emit floating->sidebarDragStarted();
        QApplication::processEvents();
        QWidget* target = host->bottomWidget();
        check(controller.bottomDockWidget()->isVisible(), "tiles_bottom_drop_target_visible");
        const QPoint destination = target->mapToGlobal(QPoint(target->width() - 8, target->height() / 2));
        const bool accepted = host->acceptFloatingDrop(floating, item.stableKey(), destination);
        emit floating->sidebarDragFinished(accepted);
        QApplication::processEvents();
        check(accepted && host->isBottomResource(item.stableKey())
                  && host->viewForResource(item.stableKey()) == original && !floating->hasResource(),
              "tiles_drop_retains_view_identity");
    }
    QWidget* first = host->sectionWidget("mock:bottom-a");
    QWidget* second = host->sectionWidget("mock:bottom-b");
    check(first && second && first->isVisible() && second->isVisible() && first->y() == second->y()
              && first->geometry().right() < second->geometry().left()
              && controller.dockWidget()->isVisible(), "tiles_bottom_simultaneous_horizontal_layout");
    if (!first || !second) return;
    const int oldFirst = first->width();
    const int oldSecond = second->width();
    dragHandle(first->findChild<QWidget*>("contextSectionResize"), QPoint(35, 0));
    check(qAbs(first->width() - oldFirst - 35) <= 1 && qAbs(second->width() - oldSecond + 35) <= 1,
          "tiles_divider_moves_adjacent_boundary_by_pointer_distance");
    check(host->viewForResource("mock:bottom-a") == firstView, "tiles_resize_retains_content");
    const auto saved = controller.captureState();
    QTemporaryDir temporary;
    WorkspaceSessionStateService service(temporary.filePath("tiled.ini"));
    WorkspaceSessionState session;
    session.workspaceRoot = controller.workspaceRoot();
    session.ui.contextWorkspace = saved;
    check(service.save(session).saved, "tiles_save");
    const auto loaded = service.load(session.workspaceRoot);
    check(loaded.loaded && sameState(saved, loaded.state.ui.contextWorkspace), "tiles_wire_roundtrip");
    controller.restoreState(loaded.state.ui.contextWorkspace);
    QApplication::processEvents();
    check(host->areaResourceCount(false) == 1 && host->areaResourceCount(true) == 2
              && controller.bottomDockWidget()->isVisible() && controller.dockWidget()->isVisible()
              && sameState(saved, controller.captureState()), "tiles_live_roundtrip");
    QWidget* restored = host->viewForResource("mock:bottom-a");
    check(controller.unpinResource("mock:bottom-a") && controller.floatingWindow()->view() == restored,
          "tiles_bottom_to_float_identity");
    check(controller.pinFloatingResource("mock:bottom-a") && !host->isBottomResource("mock:bottom-a")
              && host->viewForResource("mock:bottom-a") == restored, "tiles_float_to_side_identity");
    controller.closePinnedResource("mock:bottom-b");
    check(!controller.bottomDockWidget()->isVisible() && controller.dockWidget()->isVisible(),
          "tiles_empty_bottom_hides_independently");
}

void verifyNativeFrameCorrection()
{
    PlacementFixture fixture;
    ContextFloatingWindow host(&fixture.window, fixture.window.centralWidget());
    const QRect available = fixture.window.screen()->availableGeometry();
    ContextWorkspaceState geometry;
    geometry.floatingGeometryValid = true;
    geometry.floatingWidth = ContextWorkspaceState::boundedPeekWidth(available.width() / 2);
    geometry.floatingHeight = ContextWorkspaceState::boundedPeekHeight(available.height() / 2);
    geometry.floatingX = available.x() + (available.width() - geometry.floatingWidth) / 2;
    geometry.floatingY = available.y() + (available.height() - geometry.floatingHeight) / 2;
    geometry.floatingScreenName = host.screen()->name();
    host.restoreGeometry(geometry);
    host.setView(resource("frame"), new QLabel("frame"));
    check(host.frameGeometry() == QRect(geometry.floatingX, geometry.floatingY, geometry.floatingWidth, geometry.floatingHeight),
          "native_frame_immediate: setView returns the exact saved outer frame before any test event pumping");
}
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    if (!initializeUiStyleForTest()) return 2;
    verifyStateCompatibility(app.arguments());
    verifyPlacementMapping();
    verifyUnsupportedPlacements();
    verifyRailThreeStates();
    verifyRailFocusExisting();
    verifyDetachableRoutingAndMovement();
    verifyFloatingGeometryRoundtrip();
    verifyFloatingScreenFallback();
    verifyV3ContextInput();
    verifyFloatingOpacityAndSettings();
    verifySingleFloatingInstance();
    verifyMultipleFloatingWindows();
    verifyFloatingRestoreLimit();
    verifyV4ContextInput();
    verifyRailContextMenuSignal();
    verifyDocumentBindingVisibility();
    verifyNoActiveDocumentBinding();
    verifyDocumentLayoutRoundtrip();
    verifyDocumentLayoutEviction();
    verifyNativeFrameCorrection();
    verifySidebarStack();
    verifySidebarCompression();
    verifySidebarVisibilityToggle();
    verifySectionPreferredHeight();
    verifySectionStatusFromView();
    verifyV5SidebarMigration();
    verifySidebarDragOut();
    verifySidebarDragBack();
    verifyTiledBottomAndSidebar();

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
    check(controller.dockWidget()->isVisible() && controller.dockHost()->isSectionCollapsed(controller.dockHost()->currentResource().stableKey()), "rail collapses the active pinned section while retaining the sidebar");
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
        check(sidebar.dockWidget()->isVisible() && sidebar.dockHost()->isSectionCollapsed(original.stableKey()), "second rail click collapses transient section");
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
        check(sidebar.dockWidget()->isVisible() && sidebar.dockHost()->isSectionCollapsed(original.stableKey())
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
