#include "pinloomcodelinkcoordinator.h"

#include "contextworkspacecontroller.h"
#include "pinloomcontextprovider.h"

namespace {
const QString kLinkSelectionRoute =
    QStringLiteral("ui.pinloom.linkSelection");
const QString kOpenLinkedContentRoute =
    QStringLiteral("ui.pinloom.openLinkedContent");
}

PinloomCodeLinkCoordinator::PinloomCodeLinkCoordinator(
    ContextWorkspaceController* contextWorkspaceValue)
    : contextWorkspace(contextWorkspaceValue)
{
}

void PinloomCodeLinkCoordinator::setWorkspaceRoot(
    const QString& workspaceRoot)
{
    linkStore.setWorkspaceRoot(workspaceRoot);
}

PinloomCodeLinkStore* PinloomCodeLinkCoordinator::store()
{
    return &linkStore;
}

const PinloomCodeLinkStore* PinloomCodeLinkCoordinator::store() const
{
    return &linkStore;
}

bool PinloomCodeLinkCoordinator::attachLink(
    const QVariantMap& sourceMap,
    const PinloomHostEntry& entry,
    QString* failureReason)
{
    return linkStore.addLink(
        PinloomSourceSelection::fromVariantMap(sourceMap),
        entry.uri,
        entry.title,
        entry.identity.toVariantMap(),
        failureReason);
}

bool PinloomCodeLinkCoordinator::handlesRoute(const QString& route)
{
    return route == kLinkSelectionRoute
        || route == kOpenLinkedContentRoute;
}

ActionExecutionResult PinloomCodeLinkCoordinator::execute(
    const ActionDescriptor& descriptor,
    const ActionInvocation& invocation)
{
    ActionExecutionResult result;
    result.handled = handlesRoute(descriptor.executionRoute);
    if (!result.handled)
        return result;

    const auto fail =
        [&result](const QString& reason) {
            result.failureReason = reason;
            return result;
        };
    if (!contextWorkspace) {
        return fail(QStringLiteral(
            "Pinloom code linking is unavailable."));
    }

    ContextResource resource;
    if (descriptor.executionRoute == kLinkSelectionRoute) {
        const QVariantMap sourceMap = invocation.parameters
            .value(QStringLiteral("linkSource"))
            .toMap();
        const PinloomSourceSelection source =
            PinloomSourceSelection::fromVariantMap(sourceMap);
        if (!source.isValid()) {
            return fail(QStringLiteral(
                "Select code inside the active workspace."));
        }
        resource = PinloomContextProvider::homeResource(
            linkStore.workspaceRoot());
        resource.state.insert(
            QStringLiteral("linkSource"), sourceMap);
        result.message = QStringLiteral(
            "Choose a Pinloom entry or create a source anchor.");
    } else {
        const QUrl uri(
            invocation.parameters
                .value(QStringLiteral("pinloomUri"))
                .toString(),
            QUrl::StrictMode);
        resource = PinloomContextProvider::resourceForUri(
            uri,
            linkStore.workspaceRoot());
        if (!resource.isValid()) {
            return fail(QStringLiteral(
                "The Pinloom link is invalid."));
        }
        result.message = QStringLiteral(
            "Pinloom content opened in Context Workspace.");
    }

    QString failureReason;
    if (!contextWorkspace->openResource(
            resource,
            ContextOpenMode::Peek,
            &failureReason)) {
        return fail(failureReason);
    }
    result.succeeded = true;
    return result;
}
