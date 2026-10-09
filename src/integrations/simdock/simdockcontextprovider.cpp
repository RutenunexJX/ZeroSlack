#include "simdockcontextprovider.h"
#include "simdockcontextview.h"

QString SimDockContextProvider::providerId() const { return QStringLiteral("simdock"); }
QString SimDockContextProvider::displayName() const { return QStringLiteral("SimDock"); }
QString SimDockContextProvider::iconKey() const { return QStringLiteral("media-playback-start"); }
ContextResource SimDockContextProvider::activationResource(const QString &workspace) const
{
    ContextResource resource;
    resource.providerId = providerId();
    resource.resourceId = QStringLiteral("workbench");
    resource.uri = QUrl(QStringLiteral("simdock://workbench"));
    resource.title = displayName();
    resource.iconKey = iconKey();
    resource.workspaceId = workspace;
    return resource;
}
QWidget *SimDockContextProvider::createView(const ContextResource &resource, QWidget *parent)
{
    auto *host = new SimDockContextView(parent);
    host->activate(resource);
    return host;
}
bool SimDockContextProvider::activateView(QWidget *view, const ContextResource &resource)
{
    auto *host = qobject_cast<SimDockContextView *>(view);
    return host && host->activate(resource);
}
bool SimDockContextProvider::canCloseView(QWidget *view, QString *error) const
{
    auto *host = qobject_cast<SimDockContextView *>(view);
    return !host || host->canClose(error);
}
void SimDockContextProvider::deactivateView(QWidget *view)
{ if (auto *host = qobject_cast<SimDockContextView *>(view)) host->retire(); }
ContextViewCapabilities SimDockContextProvider::capabilities(const ContextResource &) const
{
    ContextViewCapabilities result;
    result.minimumWidth = 400;
    result.preferredWidth = 680;
    result.maximumWidth = 1600;
    result.minimumHeight = 380;
    result.preferredHeight = 680;
    return result;
}
QVariantMap SimDockContextProvider::saveViewState(QWidget *view) const
{
    const auto *host = qobject_cast<SimDockContextView *>(view);
    return host ? host->saveState() : QVariantMap{};
}
void SimDockContextProvider::restoreViewState(QWidget *view, const QVariantMap &state)
{ if (auto *host = qobject_cast<SimDockContextView *>(view)) host->restoreState(state); }
