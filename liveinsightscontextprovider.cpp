#include "liveinsightscontextprovider.h"

#include "liveinsightscontextview.h"
#include "liveinsightsession.h"

#include <QUrl>

#include <memory>
#include <utility>

namespace {
const QString kPrimaryResourceId = QStringLiteral("primary");
const QString kUriScheme = QStringLiteral("zeroslack");
const QString kUriHost = QStringLiteral("live-insights");

QUrl uriForKind(LiveInsightKind kind)
{
    QUrl uri;
    uri.setScheme(kUriScheme);
    uri.setHost(kUriHost);
    uri.setPath(
        QStringLiteral("/%1").arg(liveInsightKindId(kind)));
    return uri;
}
}

LiveInsightsContextProvider::LiveInsightsContextProvider(
    LiveInsightSession* session,
    QObject* parent)
    : QObject(parent)
    , sessionValue(session)
{
    if (!sessionValue)
        sessionValue = new LiveInsightSession(this);
}

QString LiveInsightsContextProvider::staticProviderId()
{
    return QStringLiteral("liveInsights");
}

ContextResource LiveInsightsContextProvider::resourceForKind(
    LiveInsightKind kind,
    const QString& workspaceId,
    const QVariantMap& state)
{
    ContextResource resource;
    resource.providerId = staticProviderId();
    resource.resourceId = kPrimaryResourceId;
    resource.uri = uriForKind(kind);
    resource.title = QStringLiteral("Live Insights");
    resource.iconKey = QStringLiteral("view-statistics");
    resource.workspaceId = workspaceId;
    resource.state = state;
    resource.state.insert(
        QStringLiteral("kind"), liveInsightKindId(kind));
    if (!resource.state.contains(QStringLiteral("followEditor")))
        resource.state.insert(QStringLiteral("followEditor"), true);
    return resource;
}

bool LiveInsightsContextProvider::kindFromResource(
    const ContextResource& resource,
    LiveInsightKind* kind)
{
    if (resource.providerId != staticProviderId()
        || resource.resourceId != kPrimaryResourceId
        || resource.uri.scheme() != kUriScheme
        || resource.uri.host() != kUriHost) {
        return false;
    }
    QString path = resource.uri.path();
    if (path.startsWith(QLatin1Char('/')))
        path.remove(0, 1);
    return liveInsightKindFromId(path, kind);
}

LiveInsightSession* LiveInsightsContextProvider::session() const
{
    return sessionValue;
}

void LiveInsightsContextProvider::setFullViewHandler(
    FullViewHandler handler)
{
    fullViewHandler = std::move(handler);
}

void LiveInsightsContextProvider::setPinRequestHandler(
    PinRequestHandler handler)
{
    pinRequestHandler = std::move(handler);
}

QString LiveInsightsContextProvider::providerId() const
{
    return staticProviderId();
}

QString LiveInsightsContextProvider::displayName() const
{
    return QStringLiteral("Live Insights");
}

QString LiveInsightsContextProvider::iconKey() const
{
    return QStringLiteral("view-statistics");
}

ContextResource LiveInsightsContextProvider::activationResource(
    const QString& workspaceId) const
{
    return resourceForKind(
        LiveInsightKind::Module, workspaceId);
}

bool LiveInsightsContextProvider::canOpen(
    const ContextResource& resource) const
{
    LiveInsightKind kind = LiveInsightKind::Module;
    return sessionValue
        && kindFromResource(resource, &kind);
}

QWidget* LiveInsightsContextProvider::createView(
    const ContextResource&,
    QWidget* parent)
{
    if (!sessionValue)
        return nullptr;
    auto* view = new LiveInsightsContextView(
        sessionValue, parent);
    connect(
        view,
        &LiveInsightsContextView::openFullViewRequested,
        this,
        [this, view](LiveInsightKind) {
            const ContextResource current = resourceForView(view);
            if (fullViewHandler)
                fullViewHandler(current);
            emit openFullViewRequested(current);
        });
    connect(
        view,
        &LiveInsightsContextView::pinStateChangeRequested,
        this,
        [this, view](bool pinned) {
            const ContextResource current = resourceForView(view);
            if (pinRequestHandler)
                pinRequestHandler(pinned, current);
            emit pinStateChangeRequested(pinned, current);
        });
    return view;
}

bool LiveInsightsContextProvider::activateView(
    QWidget* view,
    const ContextResource& resource)
{
    auto* insightsView =
        qobject_cast<LiveInsightsContextView*>(view);
    LiveInsightKind kind = LiveInsightKind::Module;
    if (!insightsView || !kindFromResource(resource, &kind))
        return false;
    insightsView->setWorkspaceId(resource.workspaceId);
    insightsView->restoreState(resource.state);
    insightsView->setSelectedKind(kind);
    return true;
}

ContextViewCapabilities LiveInsightsContextProvider::capabilities(
    const ContextResource&) const
{
    ContextViewCapabilities result;
    result.presentations = ContextPresentation::Peek
        | ContextPresentation::Pinned
        | ContextPresentation::FullView;
    result.minimumWidth = 320;
    result.preferredWidth = 520;
    result.maximumWidth = 960;
    return result;
}

void LiveInsightsContextProvider::observeViewResourceChanges(
    QWidget* view,
    QObject* context,
    ResourceUpdateHandler handler)
{
    auto* insightsView =
        qobject_cast<LiveInsightsContextView*>(view);
    if (!insightsView || !context || !handler)
        return;
    auto sharedHandler =
        std::make_shared<ResourceUpdateHandler>(std::move(handler));
    auto announce = [insightsView, sharedHandler]() {
        (*sharedHandler)(resourceForView(insightsView));
    };
    connect(
        insightsView,
        &LiveInsightsContextView::selectedKindChanged,
        context,
        [announce](LiveInsightKind) { announce(); });
    connect(
        insightsView,
        &LiveInsightsContextView::followEditorChanged,
        context,
        [announce](bool) { announce(); });
    connect(
        insightsView,
        &LiveInsightsContextView::pinnedChanged,
        context,
        [announce](bool) { announce(); });
}

QVariantMap LiveInsightsContextProvider::saveViewState(
    QWidget* view) const
{
    auto* insightsView =
        qobject_cast<LiveInsightsContextView*>(view);
    return insightsView
        ? insightsView->saveState()
        : QVariantMap{};
}

void LiveInsightsContextProvider::restoreViewState(
    QWidget* view,
    const QVariantMap& state)
{
    auto* insightsView =
        qobject_cast<LiveInsightsContextView*>(view);
    if (insightsView)
        insightsView->restoreState(state);
}

ContextResource LiveInsightsContextProvider::resourceForPersistence(
    const ContextResource&,
    QWidget* view,
    const QString&) const
{
    auto* insightsView =
        qobject_cast<LiveInsightsContextView*>(view);
    if (!insightsView)
        return {};
    ContextResource persisted = resourceForKind(
        insightsView->selectedKind(), {}, insightsView->saveState());
    persisted.workspaceId.clear();
    return persisted;
}

ContextResource LiveInsightsContextProvider::resourceFromPersistence(
    const ContextResource& resource,
    const QString& workspaceRoot) const
{
    LiveInsightKind kind = LiveInsightKind::Module;
    if (!kindFromResource(resource, &kind))
        return {};
    ContextResource restored = resourceForKind(
        kind, workspaceRoot, resource.state);
    return canOpen(restored) ? restored : ContextResource{};
}

ContextResource LiveInsightsContextProvider::resourceForView(
    const LiveInsightsContextView* view)
{
    if (!view)
        return {};
    return resourceForKind(
        view->selectedKind(),
        view->workspaceId(),
        view->saveState());
}
