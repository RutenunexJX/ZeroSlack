#ifndef PINLOOMCONTEXTPROVIDER_H
#define PINLOOMCONTEXTPROVIDER_H

#include "contextcontentprovider.h"
#include "pinloomhostclient.h"

#include <QPointer>

#include <functional>

class PinloomContextView;

class ZEROSLACK_API PinloomContextProvider final
    : public IContextContentProvider
{
public:
    using LinkHandler = std::function<bool(
        const QVariantMap&,
        const PinloomHostEntry&,
        QString*)>;

    explicit PinloomContextProvider(PinloomHostClient* client);
    void setLinkHandler(LinkHandler handler);

    static QString staticProviderId();
    static ContextResource homeResource(
        const QString& workspaceId = QString());
    static ContextResource resourceForEntry(
        const PinloomHostEntry& entry,
        const QString& workspaceId = QString(),
        const QString& query = QString());
    static ContextResource resourceForUri(
        const QUrl& uri,
        const QString& workspaceId = QString());

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
    void observeViewResourceChanges(
        QWidget* view,
        QObject* context,
        ResourceUpdateHandler handler) override;
    QVariantMap saveViewState(QWidget* view) const override;
    void restoreViewState(QWidget* view,
                          const QVariantMap& state) override;
    ContextResource resourceForPersistence(
        const ContextResource& resource,
        QWidget* view,
        const QString& workspaceRoot) const override;
    ContextResource resourceFromPersistence(
        const ContextResource& resource,
        const QString& workspaceRoot) const override;

private:
    QPointer<PinloomHostClient> clientValue;
    LinkHandler linkHandler;
};

#endif // PINLOOMCONTEXTPROVIDER_H
