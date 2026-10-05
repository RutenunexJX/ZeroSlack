#include "pinloomcodelinkcoordinator.h"

#include "contextworkspacecontroller.h"
#include "pinloomcontextprovider.h"

#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QUuid>
#include <algorithm>
#include <atomic>
#include <memory>

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
    const auto source = PinloomSourceSelection::fromVariantMap(sourceMap);
    if (!source.isValid() || !QFileInfo(source.workspaceRoot).isDir()) {
        if (failureReason) *failureReason = QStringLiteral("The original source workspace is unavailable.");
        return false;
    }
    // A completed remote operation belongs to its captured source, even if
    // another workspace is now displayed. Reuse the same store protocol.
    PinloomCodeLinkStore originalWorkspace;
    PinloomCodeLinkStore* target = &linkStore;
    if (QDir::cleanPath(source.workspaceRoot).compare(linkStore.workspaceRoot(), Qt::CaseInsensitive) != 0) {
        originalWorkspace.setWorkspaceRoot(source.workspaceRoot);
        target = &originalWorkspace;
    }
    return target->addLink(source,
        entry.uri,
        entry.title,
        entry.identity.toVariantMap(),
        failureReason);
}

void PinloomCodeLinkCoordinator::setCompletionNotice(
    std::function<void(const QString&)> notice)
{
    completionNotice = std::move(notice);
}

void PinloomCodeLinkCoordinator::createSourceAnchor(
    PinloomHostClient* client, const QVariantMap& sourceMap,
    const QString& title, CreateReply reply)
{
    PinloomSourceLinkResult operation;
    operation.requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    operation.source = sourceMap;
    const auto source = PinloomSourceSelection::fromVariantMap(sourceMap);
    if (!client || !source.isValid()
        || source.anchorKind == PinloomCodeAnchorKind::LegacySelection
        || QDir::cleanPath(source.workspaceRoot).compare(linkStore.workspaceRoot(), Qt::CaseInsensitive) != 0
        || !linkStore.loadFailureReason().isEmpty()) {
        operation.message = linkStore.loadFailureReason().isEmpty()
            ? QStringLiteral("The source anchor or active workspace is unavailable.") : linkStore.loadFailureReason();
        if (reply) reply(operation);
        return;
    }
    const QPointer<PinloomCodeLinkCoordinator> owner(this);
    const auto completed = std::make_shared<std::atomic_bool>(false);
    client->createSourceAnchor(sourceMap, title,
        [owner, operation, source, completed, reply = std::move(reply)](
            const PinloomHostEntry& entry, const QString& error) mutable {
            if (!owner || completed->exchange(true)) return;
            operation.entry = entry;
            if (!error.isEmpty() || !entry.isValid()) {
                operation.message = error.isEmpty()
                    ? QStringLiteral("Pinloom did not return the created anchor.") : error;
            } else {
                QString failure;
                operation.linked = owner->attachLink(operation.source, entry, &failure);
                const QString origin = QStringLiteral("%1:%2 (%3)")
                    .arg(source.relativeFilePath).arg(source.startLine).arg(source.workspaceRoot);
                operation.message = operation.linked
                    ? QStringLiteral("Pinloom anchor attached to %1.").arg(origin)
                    : QStringLiteral("Pinloom anchor was created at %1, but could not be attached to %2: %3")
                        .arg(entry.uri.toString(), origin, failure);
            }
            // Persist and announce through the operation owner before touching
            // its optional, possibly closed/reused presentation.
            if (!owner) return;
            const auto notice = owner->completionNotice;
            if (notice) notice(operation.message);
            if (reply) reply(operation);
        }, operation.requestId);
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
