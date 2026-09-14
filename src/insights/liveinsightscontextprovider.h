#ifndef LIVEINSIGHTSCONTEXTPROVIDER_H
#define LIVEINSIGHTSCONTEXTPROVIDER_H

#include "contextcontentprovider.h"
#include "liveinsightscontextview.h"
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
    // Suggested initial height of an insight section, published through
    // ContextViewCapabilities::preferredSectionHeight. Measured, not guessed:
    // see compact_layout_test::insightSectionHeights.
    static int suggestedSectionHeight();

    LiveInsightSession* session() const;
    void setFullViewHandler(FullViewHandler handler);
    void setPinRequestHandler(PinRequestHandler handler);
    // Installed by the host that can build an editor context. Views created
    // by this provider then render the real insight surface instead of the
    // compact summary card.
    void setToolContextSource(
        LiveInsightsContextView::ToolContextSource source);
    void setWaveformLibraryPathSource(
        LiveInsightsContextView::WaveformLibraryPathSource source);
    // Runs the editor-side target picker for a section of this kind.
    void setTargetPickRequest(
        LiveInsightsContextView::TargetPickRequest request);

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
    LiveInsightsContextView::ToolContextSource toolContextSource;
    LiveInsightsContextView::TargetPickRequest targetPickRequest;
    LiveInsightsContextView::WaveformLibraryPathSource
        waveformLibraryPathSource;

    ContextResource resourceForView(
        const LiveInsightsContextView* view) const;
};

#endif // LIVEINSIGHTSCONTEXTPROVIDER_H
