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
#include <QPointer>
#include <QToolButton>
#include <QUrl>

#include <iostream>
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
              && counters.created == 2
              && counters.restored == 2,
          "opening another Peek resource replaces the preview");
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
              && controller.dockHost()->resourceCount() == 1,
          "a pinned resource can return to transient Peek mode");
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
                     == original.stableKey(),
          "session capture preserves pinned order and active tab but omits Peek");

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

    ContextResource foreign = resource(QStringLiteral("foreign"));
    foreign.workspaceId = QStringLiteral("workspace-b");
    check(!controller.openResource(foreign,
                                   ContextOpenMode::Pinned,
                                   &failureReason)
              && failureReason.contains(QStringLiteral("another workspace")),
          "resources from another workspace cannot leak into the active context");
    controller.clearResources();

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
