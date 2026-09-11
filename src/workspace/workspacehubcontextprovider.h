#ifndef WORKSPACEHUBCONTEXTPROVIDER_H
#define WORKSPACEHUBCONTEXTPROVIDER_H

#include "contextcontentprovider.h"
#include "workspacehubtypes.h"
#include "zeroslackexport.h"

#include <QObject>
#include <QPointer>

#include <functional>

class WorkspaceHubSession;
class WorkspaceHubView;

class ZEROSLACK_API WorkspaceHubContextProvider final
    : public QObject
    , public IContextContentProvider
{
    Q_OBJECT

public:
    using OpenHandler = std::function<bool(
        const WorkspaceHubItem&, QString*)>;
    using RefreshHandler = std::function<void()>;
    using StatusHandler = std::function<void(
        const QString&, int)>;

    explicit WorkspaceHubContextProvider(
        WorkspaceHubSession* session,
        QObject* parent = nullptr);

    static QString staticProviderId();
    static ContextResource homeResource(
        const QString& workspaceId = {});

    void setOpenHandler(OpenHandler handler);
    void setRefreshHandler(RefreshHandler handler);
    void setStatusHandler(StatusHandler handler);

    QString providerId() const override;
    QString displayName() const override;
    QString iconKey() const override;
    ContextResource activationResource(
        const QString& workspaceId) const override;
    bool canOpen(const ContextResource& resource) const override;
    QWidget* createView(const ContextResource& resource,
                        QWidget* parent) override;
    bool activateView(QWidget* view,
                      const ContextResource& resource) override;
    ContextViewCapabilities capabilities(
        const ContextResource& resource) const override;
    QVariantMap saveViewState(QWidget* view) const override;
    void restoreViewState(QWidget* view,
                          const QVariantMap& state) override;
    QVariantMap saveProviderState() const override;
    void restoreProviderState(const QVariantMap& state) override;
    void setProviderStateChangedHandler(
        ProviderStateChangedHandler handler) override;
    ContextResource resourceForPersistence(
        const ContextResource& resource,
        QWidget* view,
        const QString& workspaceRoot) const override;
    ContextResource resourceFromPersistence(
        const ContextResource& resource,
        const QString& workspaceRoot) const override;

private:
    QPointer<WorkspaceHubSession> sessionValue;
    OpenHandler openHandler;
    RefreshHandler refreshHandler;
    StatusHandler statusHandler;
    ProviderStateChangedHandler providerStateChangedHandler;
    mutable QVariantMap retainedViewState;

    void connectView(WorkspaceHubView* view);
};

#endif // WORKSPACEHUBCONTEXTPROVIDER_H
