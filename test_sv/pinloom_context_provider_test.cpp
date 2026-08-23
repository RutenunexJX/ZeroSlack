#include "contextworkspacecontroller.h"
#include "contextdockhost.h"
#include "contextpeekhost.h"
#include "contextrail.h"
#include "pinloomcontextprovider.h"
#include "pinloomcontextview.h"
#include "pinloomhostclient.h"

#include <QApplication>
#include <QAction>
#include <QClipboard>
#include <QJsonArray>
#include <QListWidget>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QSignalSpy>
#include <QToolButton>
#include <QVector>

#include <iostream>
#include <utility>

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

QJsonObject entryJson()
{
    return {
        {QStringLiteral("identity"),
         QJsonObject{{QStringLiteral("entryId"),
                      QStringLiteral("anchor:clock-reset")},
                     {QStringLiteral("resourceId"),
                      QStringLiteral("resource-clock")},
                     {QStringLiteral("anchorId"),
                      QStringLiteral("clock-reset")},
                     {QStringLiteral("clipId"), QString()}}},
        {QStringLiteral("uri"),
         QStringLiteral(
             "pinloom://entry/anchor:clock-reset?resource=resource-clock&anchor=clock-reset")},
        {QStringLiteral("type"), QStringLiteral("anchor")},
        {QStringLiteral("title"), QStringLiteral("Clock reset note")},
        {QStringLiteral("aliases"), QJsonArray{QStringLiteral("cdc")}},
        {QStringLiteral("tags"), QJsonArray{QStringLiteral("rtl")}},
        {QStringLiteral("summary"), QStringLiteral("clock.md")},
        {QStringLiteral("location"), QStringLiteral("E:/notes/clock.md")},
        {QStringLiteral("matchedField"), QStringLiteral("anchor_name")},
        {QStringLiteral("matchSummary"), QStringLiteral("Match: anchor_name")},
        {QStringLiteral("pinned"), true},
        {QStringLiteral("deleted"), false},
        {QStringLiteral("metadata"), QJsonObject{}},
    };
}
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    const PinloomHostIdentity parsedIdentity =
        PinloomHostIdentity::fromUri(
            QUrl(entryJson().value(QStringLiteral("uri")).toString()));
    check(parsedIdentity.entryId == QStringLiteral("anchor:clock-reset")
              && parsedIdentity.resourceId == QStringLiteral("resource-clock")
              && parsedIdentity.anchorId == QStringLiteral("clock-reset")
              && parsedIdentity.toUri().scheme() == QStringLiteral("pinloom"),
          "stable Pinloom URI round-trips without cached content");

    int searchRequests = 0;
    int resolveRequests = 0;
    int openRequests = 0;
    PinloomHostClient client(
        [&searchRequests, &resolveRequests, &openRequests](
            const QJsonObject& request,
            PinloomHostClient::RawReplyHandler reply) {
            const QString method =
                request.value(QStringLiteral("method")).toString();
            if (method == QStringLiteral("search")) {
                ++searchRequests;
                reply({{QStringLiteral("entries"),
                        QJsonArray{entryJson()}}}, {});
                return;
            }
            if (method == QStringLiteral("resolve")) {
                ++resolveRequests;
                reply({{QStringLiteral("entry"), entryJson()},
                       {QStringLiteral("content"),
                        QStringLiteral("Reset deassertion is synchronized.")},
                       {QStringLiteral("contentType"),
                        QStringLiteral("text/plain")},
                       {QStringLiteral("details"),
                        QJsonObject{{QStringLiteral("locatorType"),
                                     QStringLiteral("text.line")}}}},
                      {});
                return;
            }
            if (method == QStringLiteral("open")) {
                ++openRequests;
                reply({{QStringLiteral("message"),
                        QStringLiteral("Opened anchor")}}, {});
                return;
            }
            reply({}, QStringLiteral("unexpected method"));
        });
    client.setExecutablePath(QStringLiteral("   "));
    check(client.executablePath().isEmpty(),
          "empty Pinloom executable setting keeps automatic discovery active");

    QMainWindow window;
    auto* editorRegion = new QWidget(&window);
    window.setCentralWidget(editorRegion);
    window.resize(1000, 700);
    window.show();
    QApplication::processEvents();

    ContextWorkspaceController controller(&window, editorRegion, &window);
    check(controller.registerProvider(
              std::make_unique<PinloomContextProvider>(&client)),
          "Pinloom provider registers in Context Workspace");
    check(controller.providerIds().contains(QStringLiteral("pinloom"))
              && controller.rail()->entryIds().contains(
                     QStringLiteral("pinloom")),
          "Pinloom provider exposes a Context Rail entry");

    controller.setWorkspaceRoot(QStringLiteral("workspace-a"));
    QString failureReason;
    QAction* pinloomAction = window.findChild<QAction*>(
        QStringLiteral("contextRail.pinloom"));
    if (pinloomAction)
        pinloomAction->trigger();
    QApplication::processEvents();
    check(pinloomAction && controller.peekHost()->hasResource(),
          "Pinloom rail activation opens its provider resource in Peek");
    auto* view = qobject_cast<PinloomContextView*>(
        controller.peekHost()->view());
    check(view && searchRequests == 1
              && view->resultList()->count() == 1,
          "opening Pinloom performs unified search through the host bridge");

    view->resultList()->setCurrentRow(0);
    QApplication::processEvents();
    check(resolveRequests >= 1
              && view->currentEntry().identity.anchorId
                     == QStringLiteral("clock-reset")
              && view->previewEditor()->toPlainText().contains(
                     QStringLiteral("synchronized")),
          "selecting a result resolves authoritative Pinloom content");
    check(controller.peekHost()->resource().resourceId
              == QStringLiteral("library")
              && controller.peekHost()->resource().uri.scheme()
                     == QStringLiteral("pinloom"),
          "selection updates the portable Pinloom URI without duplicating content");

    const ContextResource linkedResource =
        PinloomContextProvider::resourceForUri(
            view->currentEntry().uri,
            QStringLiteral("workspace-a"));
    check(linkedResource.isValid()
              && linkedResource.state
                     .value(QStringLiteral("identity"))
                     .toMap()
                     .value(QStringLiteral("anchorId"))
                     .toString()
                     == QStringLiteral("clock-reset"),
          "a code-held Pinloom URI reconstructs an openable context resource");

    view->copyLinkButton()->click();
    check(QApplication::clipboard()->text().startsWith(
              QStringLiteral("pinloom://entry/")),
          "Copy Link exposes the stable Pinloom URI");
    view->openButton()->click();
    check(openRequests == 1
              && view->statusText() == QStringLiteral("Opened anchor"),
          "Open delegates to Pinloom's authoritative primary action");

    check(controller.pinPeek(&failureReason),
          "resolved Pinloom view can be pinned without recreation");
    const ContextWorkspaceState state = controller.captureState();
    check(state.pinnedResources.size() == 1
              && !state.pinnedResources.constFirst()
                      .value(QStringLiteral("state")).toMap()
                      .contains(QStringLiteral("content")),
          "workspace persistence stores identity and query but no Pinloom content");

    controller.clearResources();
    controller.setWorkspaceRoot(QStringLiteral("workspace-a"));
    const ContextWorkspaceRestoreResult restored =
        controller.restoreState(state);
    check(restored.restoredResources == 1
              && controller.dockHost()->resourceCount() == 1,
          "pinned Pinloom identity restores through the provider");

    PinloomHostClient unavailable(
        [](const QJsonObject&,
           PinloomHostClient::RawReplyHandler reply) {
            reply({}, QStringLiteral("Pinloom unavailable"));
        });
    PinloomContextView unavailableView(&unavailable);
    unavailableView.restoreState({});
    check(unavailableView.statusText()
              == QStringLiteral("Pinloom unavailable"),
          "unavailable Pinloom state is explicit and non-fatal");

    QVector<PinloomHostClient::RawReplyHandler> delayedReplies;
    PinloomHostClient delayed(
        [&delayedReplies](
            const QJsonObject&,
            PinloomHostClient::RawReplyHandler reply) {
            delayedReplies.append(std::move(reply));
        });
    auto* closingView = new PinloomContextView(&delayed);
    closingView->restoreState({});
    check(delayedReplies.size() == 1,
          "Pinloom view can have an in-flight search");
    delete closingView;
    delayedReplies.takeFirst()(
        {{QStringLiteral("entries"), QJsonArray{entryJson()}}}, {});
    check(true,
          "closing Pinloom view invalidates in-flight callbacks safely");

    if (failures == 0) {
        std::cout << "pinloom_context_provider_test: "
                  << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "pinloom_context_provider_test: "
              << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
