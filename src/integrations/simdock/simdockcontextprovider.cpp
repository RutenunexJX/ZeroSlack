#include "simdockcontextprovider.h"
#include "simdockcontextview.h"
#include "../../simulation/simdock/ui/workbench.h"
#include <QVBoxLayout>

namespace {
class SimulationSurface final : public QWidget {
public:
    SimulationSurface(SimDockContextView* value, QWidget* parking, QWidget* parent)
        : QWidget(parent), workspace(value), parkingParent(parking) {
        setObjectName(QStringLiteral("simulationSurface"));
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(value);
        value->show();
    }
    ~SimulationSurface() override { park(); }
    void park() {
        if (workspace && workspace->parentWidget() == this) {
            workspace->hide();
            workspace->setParent(parkingParent);
        }
    }
private:
    QPointer<SimDockContextView> workspace;
    QPointer<QWidget> parkingParent;
};
}

SimDockContextProvider::SimDockContextProvider(SimDockContextView* workspace)
    : sharedWorkspace(workspace), parkingParent(workspace ? workspace->parentWidget() : nullptr) {}
SimDockContextProvider::~SimDockContextProvider() { QObject::disconnect(stateConnection); }

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
    if (sharedWorkspace) {
        if (resource.workspaceId != sharedWorkspace->workbench()->workspace()) return nullptr;
        auto* surface = new SimulationSurface(sharedWorkspace, parkingParent, parent);
        if (!resource.state.isEmpty()) sharedWorkspace->restoreState(resource.state);
        return surface;
    }
    auto *host = new SimDockContextView(parent);
    host->activate(resource);
    return host;
}
bool SimDockContextProvider::activateView(QWidget *view, const ContextResource &resource)
{
    if (sharedWorkspace) {
        if (resource.workspaceId != sharedWorkspace->workbench()->workspace()) return false;
        if (!resource.state.isEmpty()) sharedWorkspace->restoreState(resource.state);
        return view != nullptr;
    }
    auto *host = qobject_cast<SimDockContextView *>(view);
    return host && host->activate(resource);
}
bool SimDockContextProvider::canCloseView(QWidget *view, QString *error) const
{
    if (sharedWorkspace) return sharedWorkspace->canClose(error);
    auto *host = qobject_cast<SimDockContextView *>(view);
    return !host || host->canClose(error);
}
void SimDockContextProvider::deactivateView(QWidget *view)
{
    if (auto* surface = dynamic_cast<SimulationSurface*>(view)) surface->park();
    else if (auto *host = qobject_cast<SimDockContextView *>(view)) host->retire();
}
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
    if (sharedWorkspace) return {}; // Shared state is persisted once, even with no Simulation surface.
    const auto *host = qobject_cast<SimDockContextView *>(view);
    return host ? host->saveState() : QVariantMap{};
}
void SimDockContextProvider::restoreViewState(QWidget *view, const QVariantMap &state)
{
    if (sharedWorkspace) { if (!state.isEmpty()) sharedWorkspace->restoreState(state); }
    else if (auto *host = qobject_cast<SimDockContextView *>(view)) host->restoreState(state);
}
QVariantMap SimDockContextProvider::saveProviderState() const
{ return sharedWorkspace ? sharedWorkspace->saveState() : QVariantMap{}; }
void SimDockContextProvider::restoreProviderState(const QVariantMap& state)
{ if (sharedWorkspace && !state.isEmpty()) sharedWorkspace->restoreState(state); }
void SimDockContextProvider::setProviderStateChangedHandler(ProviderStateChangedHandler handler)
{
    QObject::disconnect(stateConnection);
    if (sharedWorkspace)
        stateConnection = QObject::connect(sharedWorkspace->workbench(), &simdock::Workbench::stateChanged,
            sharedWorkspace, [handler] { if (handler) handler(); });
}
ContextResource SimDockContextProvider::resourceForPersistence(const ContextResource& resource, QWidget* view, const QString& root) const
{
    if (!sharedWorkspace) return IContextContentProvider::resourceForPersistence(resource, view, root);
    auto persisted = resource;
    persisted.state.clear();
    return persisted;
}
