#include "pinloomcodelinkcoordinator.h"

#include "contextworkspacecontroller.h"
#include "pinloomcontextprovider.h"

#include <algorithm>

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
                "Select a symbol, an always block, or a continuous assign block inside the active workspace."));
        }
        resource = PinloomContextProvider::homeResource(
            linkStore.workspaceRoot());
        resource.state.insert(
            QStringLiteral("linkSource"), sourceMap);
        result.message = QStringLiteral(
            "Choose a Pinloom entry or create a source anchor.");
    } else {
        const QString anchorId = invocation.parameters
            .value(QStringLiteral("pinloomAnchorId"))
            .toString().trimmed();
        if (!anchorId.isEmpty()) {
            const QList<PinloomCodeLinkAnchorRecord> anchors =
                linkStore.anchors();
            const auto anchor = std::find_if(
                anchors.cbegin(), anchors.cend(),
                [&anchorId](const PinloomCodeLinkAnchorRecord& candidate) {
                    return candidate.id == anchorId;
                });
            if (anchor != anchors.cend()) {
                resource = PinloomContextProvider::resourceForBindings(
                    *anchor, linkStore.workspaceRoot());
            }
        } else {
            const QUrl uri(
                invocation.parameters
                    .value(QStringLiteral("pinloomUri"))
                    .toString(),
                QUrl::StrictMode);
            resource = PinloomContextProvider::resourceForUri(
                uri,
                linkStore.workspaceRoot());
        }
        if (!resource.isValid()) {
            return fail(QStringLiteral(
                "The Pinloom binding is invalid or unavailable."));
        }
        result.message = QStringLiteral(
            "Pinloom bindings opened in the Context sidebar.");
    }

    QString failureReason;
    if (!contextWorkspace->openResource(
            resource,
            descriptor.executionRoute == kLinkSelectionRoute
                ? ContextPlacement{ContextSurface::Floating, ContextPersistence::Transient, ContextBinding::Global}
                : ContextPlacement{ContextSurface::Docked, ContextPersistence::Transient, ContextBinding::Global},
            &failureReason)) {
        return fail(failureReason);
    }
    result.succeeded = true;
    return result;
}
