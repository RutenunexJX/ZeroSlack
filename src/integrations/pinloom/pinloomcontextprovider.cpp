#include "pinloomcontextprovider.h"

#include "pinloomcontextview.h"
#include "pinloomcodelinkstore.h"

#include <QLineEdit>
#include <QObject>

#include <utility>

namespace {
const QString kLibraryResourceId = QStringLiteral("library");
}

PinloomContextProvider::PinloomContextProvider(
    PinloomHostClient* client)
    : clientValue(client)
{
}

void PinloomContextProvider::setLinkHandler(LinkHandler handler)
{
    linkHandler = std::move(handler);
}

QString PinloomContextProvider::staticProviderId()
{
    return QStringLiteral("pinloom");
}

ContextResource PinloomContextProvider::homeResource(
    const QString& workspaceId)
{
    ContextResource resource;
    resource.providerId = staticProviderId();
    resource.resourceId = kLibraryResourceId;
    resource.uri = QUrl(QStringLiteral("pinloom://library"));
    resource.title = QStringLiteral("Pinloom");
    resource.iconKey = QStringLiteral("bookmark-new");
    resource.workspaceId = workspaceId;
    return resource;
}

ContextResource PinloomContextProvider::resourceForEntry(
    const PinloomHostEntry& entry,
    const QString& workspaceId,
    const QString& query)
{
    ContextResource resource = homeResource(workspaceId);
    if (!entry.isValid())
        return resource;
    resource.uri = entry.uri;
    resource.title = entry.title;
    resource.state.insert(QStringLiteral("identity"),
                          entry.identity.toVariantMap());
    resource.state.insert(QStringLiteral("entry"),
                          entry.toVariantMap());
    resource.state.insert(QStringLiteral("query"), query);
    return resource;
}

ContextResource PinloomContextProvider::resourceForUri(
    const QUrl& uri,
    const QString& workspaceId)
{
    const PinloomHostIdentity identity =
        PinloomHostIdentity::fromUri(uri);
    if (!identity.isValid())
        return {};
    ContextResource resource = homeResource(workspaceId);
    resource.uri = uri;
    resource.title = QStringLiteral("Pinloom Link");
    resource.state.insert(QStringLiteral("identity"),
                          identity.toVariantMap());
    return resource;
}

ContextResource PinloomContextProvider::resourceForBindings(
    const PinloomCodeLinkAnchorRecord& anchor,
    const QString& workspaceId)
{
    if (!anchor.isValid())
        return {};
    ContextResource resource = homeResource(workspaceId);
    resource.resourceId = QStringLiteral("bindings/%1").arg(anchor.id);
    resource.title = anchor.source.suggestedTitle();
    resource.uri = QUrl(QStringLiteral("pinloom://library"));
    QVariantList entries;
    for (const PinloomCodeLinkRecord& link : anchor.links) {
        PinloomHostEntry entry;
        entry.identity = PinloomHostIdentity::fromVariantMap(link.identity);
        if (!entry.identity.isValid())
            entry.identity = PinloomHostIdentity::fromUri(link.uri);
        entry.uri = link.uri;
        entry.type = QStringLiteral("binding");
        entry.title = link.title.trimmed().isEmpty()
            ? anchor.source.suggestedTitle()
            : link.title;
        entry.summary = QStringLiteral("Linked from %1")
            .arg(anchor.source.suggestedTitle());
        if (entry.isValid())
            entries.append(entry.toVariantMap());
    }
    if (entries.isEmpty())
        return {};
    resource.state.insert(QStringLiteral("boundEntries"), entries);
    resource.state.insert(QStringLiteral("bindingAnchorId"), anchor.id);
    resource.state.insert(QStringLiteral("bindingTitle"), resource.title);
    return resource;
}

QString PinloomContextProvider::providerId() const
{
    return staticProviderId();
}

QString PinloomContextProvider::displayName() const
{
    return QStringLiteral("Pinloom");
}

QString PinloomContextProvider::iconKey() const
{
    return QStringLiteral("bookmark-new");
}

ContextResource PinloomContextProvider::activationResource(
    const QString& workspaceId) const
{
    return homeResource(workspaceId);
}

bool PinloomContextProvider::canOpen(
    const ContextResource& resource) const
{
    const bool libraryResource =
        resource.uri == QUrl(QStringLiteral("pinloom://library"));
    const bool linkedResource =
        PinloomHostIdentity::fromUri(resource.uri).isValid();
    return clientValue
        && resource.providerId == providerId()
        && (resource.resourceId == kLibraryResourceId
            || resource.resourceId.startsWith(
                QStringLiteral("bindings/")))
        && (libraryResource || linkedResource);
}

QWidget* PinloomContextProvider::createView(
    const ContextResource&,
    QWidget* parent)
{
    if (!clientValue)
        return nullptr;
    auto* view = new PinloomContextView(clientValue, parent);
    view->setLinkHandler(linkHandler);
    return view;
}

bool PinloomContextProvider::activateView(
    QWidget* view,
    const ContextResource& resource)
{
    auto* pinloomView = qobject_cast<PinloomContextView*>(view);
    if (!pinloomView)
        return false;
    pinloomView->setProperty(
        "contextWorkspaceId", resource.workspaceId);
    QVariantMap state = resource.state;
    if (!state.value(QStringLiteral("identity")).toMap().isEmpty()) {
        pinloomView->restoreState(state);
        return true;
    }
    const PinloomHostIdentity identity =
        PinloomHostIdentity::fromUri(resource.uri);
    if (identity.isValid())
        state.insert(QStringLiteral("identity"), identity.toVariantMap());
    pinloomView->restoreState(state);
    return true;
}

ContextViewCapabilities PinloomContextProvider::capabilities(
    const ContextResource&) const
{
    ContextViewCapabilities result;
    result.presentations = ContextPresentation::Peek
        | ContextPresentation::Pinned;
    result.minimumWidth = 380;
    result.preferredWidth = 540;
    result.maximumWidth = 900;
    result.minimumHeight = 300;
    result.preferredHeight = 560;
    result.maximumHeight = 920;
    return result;
}

void PinloomContextProvider::observeViewResourceChanges(
    QWidget* view,
    QObject* context,
    ResourceUpdateHandler handler)
{
    auto* pinloomView = qobject_cast<PinloomContextView*>(view);
    if (!pinloomView || !context || !handler)
        return;
    QObject::connect(
        pinloomView,
        &PinloomContextView::currentEntryChanged,
        context,
        [pinloomView, handler = std::move(handler)](
            const PinloomHostEntry& entry) {
            if (pinloomView->boundModeActive())
                return;
            handler(resourceForEntry(
                entry,
                pinloomView->property("contextWorkspaceId").toString(),
                pinloomView->searchField()->text()));
        });
}

QVariantMap PinloomContextProvider::saveViewState(
    QWidget* view) const
{
    auto* pinloomView = qobject_cast<PinloomContextView*>(view);
    return pinloomView ? pinloomView->saveState() : QVariantMap{};
}

void PinloomContextProvider::restoreViewState(
    QWidget* view,
    const QVariantMap& state)
{
    auto* pinloomView = qobject_cast<PinloomContextView*>(view);
    if (pinloomView)
        pinloomView->restoreState(state);
}

ContextResource PinloomContextProvider::resourceForPersistence(
    const ContextResource& resource,
    QWidget* view,
    const QString&) const
{
    ContextResource persisted = resource;
    persisted.workspaceId.clear();
    persisted.state = saveViewState(view);
    return persisted;
}

ContextResource PinloomContextProvider::resourceFromPersistence(
    const ContextResource& resource,
    const QString& workspaceRoot) const
{
    ContextResource restored = resource;
    restored.workspaceId = workspaceRoot;
    return canOpen(restored) ? restored : ContextResource{};
}
