#ifndef LIVEINSIGHTSCONTEXTPROVIDER_H
#define LIVEINSIGHTSCONTEXTPROVIDER_H

#include "contextcontentprovider.h"
#include "liveinsighttypes.h"
#include "zeroslackexport.h"

#include <QObject>
#include <QPointer>

#include <functional>

class LiveInsightSession;
class LiveInsightsContextView;

class ZEROSLACK_API LiveInsightsContextProvider final
    : public QObject
    , public IContextContentProvider
{
    Q_OBJECT

public:
    using FullViewHandler =
        std::function<void(const ContextResource& resource)>;
    using PinRequestHandler = std::function<void(
        bool pinned,
        const ContextResource& resource)>;

    explicit LiveInsightsContextProvider(
        LiveInsightSession* session = nullptr,
        QObject* parent = nullptr);
    LiveInsightsContextProvider(
        LiveInsightKind fixedKind,
        LiveInsightSession* session = nullptr,
        QObject* parent = nullptr);

    static QString staticProviderId();
    static QString providerIdForKind(LiveInsightKind kind);
    static QString iconKeyForKind(LiveInsightKind kind);
    static bool isWorkbenchProviderId(const QString& providerId);
    static ContextResource resourceForKind(
        LiveInsightKind kind,
        const QString& workspaceId = {},
        const QVariantMap& state = {});
    static bool kindFromResource(
        const ContextResource& resource,
        LiveInsightKind* kind);

    LiveInsightSession* session() const;
    void setFullViewHandler(FullViewHandler handler);
    void setPinRequestHandler(PinRequestHandler handler);

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

signals:
    void openFullViewRequested(const ContextResource& resource);
    void pinStateChangeRequested(bool pinned,
                                 const ContextResource& resource);

private:
    QPointer<LiveInsightSession> sessionValue;
    LiveInsightKind fixedKind = LiveInsightKind::Kernel;
    bool fixedKindEnabled = false;
    FullViewHandler fullViewHandler;
    PinRequestHandler pinRequestHandler;

    ContextResource resourceForView(
        const LiveInsightsContextView* view) const;
};

#endif // LIVEINSIGHTSCONTEXTPROVIDER_H
