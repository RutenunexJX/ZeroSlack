#pragma once
#include "../../ui/contextcontentprovider.h"
#include "zeroslackexport.h"
#include <QPointer>
#include <QMetaObject>
class SimDockContextView;

class ZEROSLACK_API SimDockContextProvider final : public IContextContentProvider
{
public:
    explicit SimDockContextProvider(SimDockContextView* workspace = nullptr);
    ~SimDockContextProvider() override;
    QString providerId() const override;
    QString displayName() const override;
    QString iconKey() const override;
    ContextResource activationResource(const QString &workspace) const override;
    QWidget *createView(const ContextResource &, QWidget *parent) override;
    bool activateView(QWidget *view, const ContextResource &resource) override;
    bool canCloseView(QWidget *view, QString *error) const override;
    void deactivateView(QWidget *view) override;
    ContextViewCapabilities capabilities(const ContextResource &) const override;
    QVariantMap saveViewState(QWidget *view) const override;
    void restoreViewState(QWidget *view, const QVariantMap &state) override;
    QVariantMap saveProviderState() const override;
    void restoreProviderState(const QVariantMap& state) override;
    void setProviderStateChangedHandler(ProviderStateChangedHandler handler) override;
    ContextResource resourceForPersistence(const ContextResource& resource, QWidget* view, const QString& root) const override;
private:
    QPointer<SimDockContextView> sharedWorkspace;
    QPointer<QWidget> parkingParent;
    QMetaObject::Connection stateConnection;
};
