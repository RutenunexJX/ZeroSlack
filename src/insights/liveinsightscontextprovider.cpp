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

LiveInsightsContextProvider::LiveInsightsContextProvider(
    LiveInsightKind kind,
    LiveInsightSession* session,
    QObject* parent)
    : QObject(parent)
    , sessionValue(session)
    , fixedKind(kind)
    , fixedKindEnabled(true)
{
    if (!sessionValue)
        sessionValue = new LiveInsightSession(this);
}

QString LiveInsightsContextProvider::staticProviderId()
{
    return QStringLiteral("liveInsights");
}

QString LiveInsightsContextProvider::providerIdForKind(
    LiveInsightKind kind)
{
    return QStringLiteral("rtlInsight.%1").arg(liveInsightKindId(kind));
}

QString LiveInsightsContextProvider::iconKeyForKind(
    LiveInsightKind kind)
{
    switch (kind) {
    case LiveInsightKind::Kernel:
        return QStringLiteral("rtl-insight-kernel");
    case LiveInsightKind::Module:
        return QStringLiteral("rtl-insight-block");
    case LiveInsightKind::Hotspot:
        return QStringLiteral("rtl-insight-hotspot");
    case LiveInsightKind::State:
        return QStringLiteral("rtl-insight-state");
    }
    return {};
}

bool LiveInsightsContextProvider::isWorkbenchProviderId(
    const QString& providerId)
{
    if (providerId == staticProviderId())
        return true;
    for (LiveInsightKind kind : {
             LiveInsightKind::Kernel,
             LiveInsightKind::Module,
             LiveInsightKind::Hotspot,
             LiveInsightKind::State}) {
        if (providerId == providerIdForKind(kind))
            return true;
    }
    return false;
}

ContextResource LiveInsightsContextProvider::resourceForKind(
    LiveInsightKind kind,
    const QString& workspaceId,
    const QVariantMap& state)
{
    ContextResource resource;
    resource.providerId = providerIdForKind(kind);
    resource.resourceId = liveInsightKindId(kind);
    resource.uri = uriForKind(kind);
    switch (kind) {
    case LiveInsightKind::Kernel:
        resource.title = QStringLiteral("Signal Kernel Graph");
        break;
    case LiveInsightKind::Module:
        resource.title = QStringLiteral("Module Block Diagram");
        break;
    case LiveInsightKind::Hotspot:
        resource.title = QStringLiteral("Signal Hotspot");
        break;
    case LiveInsightKind::State:
        resource.title = QStringLiteral("State Transition Graph");
        break;
    }
    resource.iconKey = iconKeyForKind(kind);
    resource.workspaceId = workspaceId;
    resource.state = state;
    resource.state.insert(
        QStringLiteral("kind"), liveInsightKindId(kind));
    resource.state.remove(QStringLiteral("followEditor"));
    resource.state.remove(QStringLiteral("pinned"));
    return resource;
}

bool LiveInsightsContextProvider::kindFromResource(
    const ContextResource& resource,
    LiveInsightKind* kind)
{
    if (!isWorkbenchProviderId(resource.providerId)
        || resource.uri.scheme() != kUriScheme
        || resource.uri.host() != kUriHost) {
        return false;
    }
    QString path = resource.uri.path();
    if (path.startsWith(QLatin1Char('/')))
        path.remove(0, 1);
    LiveInsightKind parsed = LiveInsightKind::Kernel;
    if (!liveInsightKindFromId(path, &parsed))
        return false;
    const bool legacy = resource.providerId == staticProviderId()
        && resource.resourceId == kPrimaryResourceId;
    if (!legacy
        && (resource.providerId != providerIdForKind(parsed)
            || resource.resourceId != liveInsightKindId(parsed))) {
        return false;
    }
    if (kind)
        *kind = parsed;
    return true;
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

void LiveInsightsContextProvider::setNavigationHandler(
    LiveInsightToolPage::NavigationHandler handler)
{
    navigationHandler = std::move(handler);
}

void LiveInsightsContextProvider::setToolContextSource(
    LiveInsightsContextView::ToolContextSource source)
{
    toolContextSource = std::move(source);
}

void LiveInsightsContextProvider::setTargetPickRequest(
    LiveInsightsContextView::TargetPickRequest request)
{
    targetPickRequest = std::move(request);
}

QString LiveInsightsContextProvider::providerId() const
{
    return fixedKindEnabled
        ? providerIdForKind(fixedKind)
        : staticProviderId();
}

QString LiveInsightsContextProvider::displayName() const
{
    return fixedKindEnabled
        ? liveInsightKindDisplayName(fixedKind)
        : QStringLiteral("RTL Insight Workbench");
}

QString LiveInsightsContextProvider::iconKey() const
{
    return fixedKindEnabled
        ? iconKeyForKind(fixedKind)
        : QStringLiteral("view-statistics");
}

ContextResource LiveInsightsContextProvider::activationResource(
    const QString& workspaceId) const
{
    ContextResource resource = resourceForKind(
        fixedKindEnabled ? fixedKind : LiveInsightKind::Kernel,
        workspaceId);
    if (!fixedKindEnabled) {
        resource.providerId = staticProviderId();
        resource.resourceId = kPrimaryResourceId;
    }
    return resource;
}

bool LiveInsightsContextProvider::canOpen(
    const ContextResource& resource) const
{
    LiveInsightKind kind = LiveInsightKind::Module;
    return sessionValue
        && kindFromResource(resource, &kind)
        && (!fixedKindEnabled || kind == fixedKind);
}

QWidget* LiveInsightsContextProvider::createView(
    const ContextResource& resource,
    QWidget* parent)
{
    if (!sessionValue)
        return nullptr;
    LiveInsightKind resourceKind = fixedKind;
    const bool hasResourceKind = kindFromResource(resource, &resourceKind);
    auto* view = fixedKindEnabled
        ? new LiveInsightsContextView(sessionValue, fixedKind, parent)
        : new LiveInsightsContextView(sessionValue, parent);
    Q_UNUSED(hasResourceKind)
    if (toolContextSource)
        view->setToolContextSource(toolContextSource);
    view->setNavigationHandler(navigationHandler);
    if (targetPickRequest)
        view->setTargetPickRequest(targetPickRequest);
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
    result.preferredSectionHeight = suggestedSectionHeight();
    return result;
}

int LiveInsightsContextProvider::suggestedSectionHeight()
{
    // Initial proportional weight relative to non-graph sections. The dock
    // fills its viewport and respects each view's minimum height.
    return 560;
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
    auto announce = [this, insightsView, sharedHandler]() {
        (*sharedHandler)(resourceForView(insightsView));
    };
    connect(
        insightsView,
        &LiveInsightsContextView::selectedKindChanged,
        context,
        [announce](LiveInsightKind) { announce(); });
    connect(
        insightsView,
        &LiveInsightsContextView::targetChanged,
        context,
        announce);
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
    ContextResource persisted = resourceForView(insightsView);
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
    if (!fixedKindEnabled) {
        restored.providerId = staticProviderId();
        restored.resourceId = kPrimaryResourceId;
    }
    return canOpen(restored) ? restored : ContextResource{};
}

ContextResource LiveInsightsContextProvider::resourceForView(
    const LiveInsightsContextView* view) const
{
    if (!view)
        return {};
    ContextResource resource = resourceForKind(
        view->selectedKind(),
        view->workspaceId(),
        view->saveState());
    if (!fixedKindEnabled) {
        resource.providerId = staticProviderId();
        resource.resourceId = kPrimaryResourceId;
    }
    return resource;
}
