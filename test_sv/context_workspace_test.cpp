#include "contextcontentprovider.h"
#include "contextdockhost.h"
#include "contextpeekhost.h"
#include "contextrail.h"
#include "contextworkspacecontroller.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDockWidget>
#include <QEvent>
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
    int created = 0;
    int restored = 0;
    int saved = 0;
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
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

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
                                  ContextOpenMode::Peek,
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
                                  ContextOpenMode::Peek,
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
                                  ContextOpenMode::Pinned,
                                  &failureReason)
              && controller.dockHost()->resourceCount() == 2
              && controller.dockHost()->currentResource() == original,
          "pinned host supports multiple independent resource tabs");

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
                                   ContextOpenMode::Peek,
                                   &failureReason)
              && !failureReason.isEmpty(),
          "unavailable providers fail without creating fallback content");

    check(counters.created == 3
              && counters.restored == 3
              && counters.saved == 3,
          "provider lifecycle hooks cover every created view exactly once");

    controller.setWorkspaceRoot(QStringLiteral("workspace-a"));
    check(controller.openResource(original,
                                  ContextOpenMode::Pinned,
                                  &failureReason)
              && controller.openResource(second,
                                         ContextOpenMode::Pinned,
                                         &failureReason),
          "workspace-scoped resources can be pinned before capture");
    window.resizeDocks(
        {controller.dockWidget()}, {420}, Qt::Horizontal);
    QApplication::processEvents();
    const int userDockWidth = controller.dockWidget()->width();
    controller.dockHost()->activateResource(original.stableKey());
    const ContextResource transient = resource(QStringLiteral("transient"));
    check(controller.openResource(transient,
                                  ContextOpenMode::Peek,
                                  &failureReason),
          "a transient preview can coexist with pinned resources");
    ContextWorkspaceState savedState = controller.captureState();
    check(savedState.valid
              && savedState.pinnedResources.size() == 2
              && savedState.activePinnedResourceKey
                     == original.stableKey()
              && savedState.peekWidth == retainedUserSize.width()
              && savedState.peekHeight == retainedUserSize.height()
              && savedState.dockWidth == userDockWidth,
          "session capture preserves both Peek dimensions, actual Dock width, pinned order, and active tab");

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
              && !controller.peekHost()->hasResource(),
          "restore skips unavailable providers without blocking valid pinned tabs");

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
                                   ContextOpenMode::Pinned,
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
