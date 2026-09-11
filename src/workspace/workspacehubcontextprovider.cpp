#include "workspacehubcontextprovider.h"

#include "semanticstateview.h"
#include "workspacehubsession.h"
#include "workspacehubview.h"

#include <QObject>

#include <utility>

namespace {
const QString kPrimaryResourceId = QStringLiteral("primary");
}

WorkspaceHubContextProvider::WorkspaceHubContextProvider(
    WorkspaceHubSession* session,
    QObject* parent)
    : QObject(parent)
    , sessionValue(session)
{
}

QString WorkspaceHubContextProvider::staticProviderId()
{
    return QStringLiteral("workspaceHub");
}

ContextResource WorkspaceHubContextProvider::homeResource(
    const QString& workspaceId)
{
    ContextResource resource;
    resource.providerId = staticProviderId();
    resource.resourceId = kPrimaryResourceId;
    resource.uri = QUrl(QStringLiteral("zeroslack://workspace-hub"));
    resource.title = QStringLiteral("Workspace Hub");
    resource.iconKey = QStringLiteral("workspace-hub");
    resource.workspaceId = workspaceId;
    return resource;
}

void WorkspaceHubContextProvider::setOpenHandler(OpenHandler handler)
{
    openHandler = std::move(handler);
}

void WorkspaceHubContextProvider::setRefreshHandler(
    RefreshHandler handler)
{
    refreshHandler = std::move(handler);
}

void WorkspaceHubContextProvider::setStatusHandler(StatusHandler handler)
{
    statusHandler = std::move(handler);
}

QString WorkspaceHubContextProvider::providerId() const
{
    return staticProviderId();
}

QString WorkspaceHubContextProvider::displayName() const
{
    return QStringLiteral("Workspace Hub");
}

QString WorkspaceHubContextProvider::iconKey() const
{
    return QStringLiteral("workspace-hub");
}

ContextResource WorkspaceHubContextProvider::activationResource(
    const QString& workspaceId) const
{
    ContextResource resource = homeResource(workspaceId);
    resource.state = retainedViewState;
    return resource;
}

bool WorkspaceHubContextProvider::canOpen(
    const ContextResource& resource) const
{
    return sessionValue
        && resource.providerId == staticProviderId()
        && resource.resourceId == kPrimaryResourceId
        && resource.uri == QUrl(QStringLiteral(
            "zeroslack://workspace-hub"));
}

QWidget* WorkspaceHubContextProvider::createView(
    const ContextResource&,
    QWidget* parent)
{
    if (!sessionValue)
        return nullptr;
    auto* view = new WorkspaceHubView(parent);
    connectView(view);
    view->setSnapshot(sessionValue->snapshot());
    if (!retainedViewState.isEmpty())
        view->restoreState(retainedViewState);
    return view;
}

bool WorkspaceHubContextProvider::activateView(
    QWidget* widget,
    const ContextResource& resource)
{
    auto* view = qobject_cast<WorkspaceHubView*>(widget);
    if (!view || !canOpen(resource))
        return false;
    const QVariantMap state = resource.state.isEmpty()
        ? retainedViewState : resource.state;
    if (!state.isEmpty())
        view->restoreState(state);
    view->setSnapshot(sessionValue->snapshot());
    return true;
}

ContextViewCapabilities WorkspaceHubContextProvider::capabilities(
    const ContextResource&) const
{
    ContextViewCapabilities result;
    result.presentations = ContextPresentation::Peek
        | ContextPresentation::Pinned;
    result.minimumWidth = 340;
    result.preferredWidth = 520;
    result.maximumWidth = 900;
    result.minimumHeight = 320;
    result.preferredHeight = 620;
    result.maximumHeight = 1200;
    return result;
}

QVariantMap WorkspaceHubContextProvider::saveViewState(
    QWidget* widget) const
{
    auto* view = qobject_cast<WorkspaceHubView*>(widget);
    if (view)
        retainedViewState = view->saveState();
    return retainedViewState;
}

void WorkspaceHubContextProvider::restoreViewState(
    QWidget* widget,
    const QVariantMap& state)
{
    retainedViewState = state;
    auto* view = qobject_cast<WorkspaceHubView*>(widget);
    if (view)
        view->restoreState(state);
}

QVariantMap WorkspaceHubContextProvider::saveProviderState() const
{
    return retainedViewState;
}

void WorkspaceHubContextProvider::restoreProviderState(
    const QVariantMap& state)
{
    retainedViewState = state;
}

void WorkspaceHubContextProvider::setProviderStateChangedHandler(
    ProviderStateChangedHandler handler)
{
    providerStateChangedHandler = std::move(handler);
}

ContextResource WorkspaceHubContextProvider::resourceForPersistence(
    const ContextResource& resource,
    QWidget* view,
    const QString&) const
{
    ContextResource persisted = resource;
    persisted.workspaceId.clear();
    persisted.state = saveViewState(view);
    return persisted;
}

ContextResource WorkspaceHubContextProvider::resourceFromPersistence(
    const ContextResource& resource,
    const QString& workspaceRoot) const
{
    ContextResource restored = homeResource(workspaceRoot);
    restored.state = resource.state;
    return canOpen(restored) ? restored : ContextResource{};
}

void WorkspaceHubContextProvider::connectView(
    WorkspaceHubView* view)
{
    if (!view || !sessionValue)
        return;
    connect(sessionValue, &WorkspaceHubSession::snapshotChanged,
            view, &WorkspaceHubView::setSnapshot);
    connect(view, &WorkspaceHubView::itemActivated,
            this,
            [this](const WorkspaceHubItem& item) {
                QString failure;
                if (!openHandler || !openHandler(item, &failure)) {
                    const QString message = failure.trimmed().isEmpty()
                        ? QStringLiteral("The selected workspace resource could not be opened.")
                        : failure;
                    if (statusHandler)
                        statusHandler(message, 5000);
                }
            });
    connect(view, &WorkspaceHubView::refreshRequested,
            this, [this]() {
                if (refreshHandler)
                    refreshHandler();
            });
    connect(view->stateView(), &SemanticStateView::actionRequested,
            this, [this]() {
                if (refreshHandler)
                    refreshHandler();
            });
    connect(view, &WorkspaceHubView::viewStateChanged,
            this, [this, view]() {
                retainedViewState = view->saveState();
                if (providerStateChangedHandler)
                    providerStateChangedHandler();
            });
}
