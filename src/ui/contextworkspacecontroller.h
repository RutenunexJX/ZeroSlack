#ifndef CONTEXTWORKSPACECONTROLLER_H
#define CONTEXTWORKSPACECONTROLLER_H

#include "contextresource.h"
#include "contextplacement.h"
#include "contextworkspacestate.h"
#include "zeroslackexport.h"

#include <QObject>
#include <QPointer>
#include <QStringList>

#include <map>
#include <memory>

class ContextDockHost;
class ContextPeekHost;
class ContextFloatingSurface;
class ContextFloatingWindow;
class ContextRail;
class IContextContentProvider;
class QDockWidget;
class QEvent;
class QMainWindow;
class QWidget;

class ZEROSLACK_API ContextWorkspaceController final : public QObject
{
    Q_OBJECT

public:
    ContextWorkspaceController(
        QMainWindow* mainWindow,
        QWidget* editorRegion,
        QObject* parent = nullptr);
    ~ContextWorkspaceController() override;

    ContextRail* rail() const;
    ContextPeekHost* peekHost() const;
    ContextFloatingSurface* floatingSurface() const;
    ContextFloatingWindow* floatingWindow() const;
    void setFloatingOpacity(int percentage);
    ContextDockHost* dockHost() const;
    QDockWidget* dockWidget() const;

    bool registerProvider(
        std::unique_ptr<IContextContentProvider> provider);
    bool unregisterProvider(const QString& providerId);
    QStringList providerIds() const;

    bool openResource(
        const ContextResource& resource,
        ContextPlacement placement = {},
        QString* failureReason = nullptr);
    bool pinPeek(QString* failureReason = nullptr);
    bool unpinResource(const QString& resourceKey,
                       QString* failureReason = nullptr);
    bool closePinnedResource(const QString& resourceKey);
    void closePeek();

    QString workspaceRoot() const;
    void setWorkspaceRoot(const QString& root);
    void clearResources();
    ContextWorkspaceState captureState() const;
    ContextWorkspaceRestoreResult restoreState(
        const ContextWorkspaceState& state,
        bool preserveRestoredDockGeometry = false);

signals:
    void providerActivationRequested(const QString& providerId);
    void resourceOpened(const ContextResource& resource,
                        ContextPlacement placement);
    void resourceClosed(const ContextResource& resource);
    void activeResourceChanged(const ContextResource& resource);
    void fullViewRequested(const ContextResource& resource);
    void workspaceStateChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QPointer<QMainWindow> window;
    QPointer<QWidget> editorRegionValue;
    QPointer<ContextRail> railValue;
    QPointer<ContextPeekHost> peekHostValue;
    QPointer<ContextFloatingWindow> floatingWindowValue;
    ContextFloatingSurface* activeFloatingSurface = nullptr;
    QPointer<ContextDockHost> dockHostValue;
    QPointer<QDockWidget> dockValue;
    std::map<QString,
             std::unique_ptr<IContextContentProvider>> providers;
    QString currentWorkspaceRoot;
    QString transientDockResourceKey;
    int preferredDockWidthValue =
        ContextWorkspaceState::kDefaultDockWidth;
    bool restoringState = false;
    bool applyingDockWidth = false;

    IContextContentProvider* providerFor(
        const ContextResource& resource) const;
    IContextContentProvider* providerForId(
        const QString& providerId) const;
    void disposeView(const ContextResource& resource,
                     QWidget* view);
    void handleViewResourceChanged(
        QWidget* view,
        const ContextResource& resource);
    void updateActiveRailEntry();
    void activateRailProvider(const QString& providerId);
    static ContextPlacement defaultPlacementFor(const QString& providerId);
    static ContextPresentation presentationFor(const ContextPlacement& placement);
    ContextFloatingSurface* floatingSurfaceFor(const ContextViewCapabilities* capabilities = nullptr) const;
    QWidget* floatingWidget() const;
    bool openInFloatingSurface(const ContextResource& resource,
                               IContextContentProvider& provider,
                               const ContextViewCapabilities& capabilities,
                               QString* failureReason);
    bool openInDockedSurface(const ContextResource& resource,
                             ContextPlacement placement,
                             IContextContentProvider& provider,
                             const ContextViewCapabilities& capabilities,
                             QString* failureReason);
    bool activateDockedResource(const ContextResource& resource,
                                IContextContentProvider& provider,
                                const ContextViewCapabilities& capabilities,
                                QString* failureReason);
    bool activateFloatingResource(const ContextResource& resource,
                                  IContextContentProvider& provider,
                                  const ContextViewCapabilities& capabilities,
                                  QString* failureReason);
    QWidget* createResourceView(const ContextResource& resource,
                                IContextContentProvider& provider,
                                QWidget* parent, QString* failureReason);
    bool addDockedResource(const ContextResource& resource,
                          IContextContentProvider& provider,
                          const ContextViewCapabilities& capabilities,
                          QString* failureReason);
    void announceResourceOpened(const ContextResource& resource, ContextPlacement placement);
    void resetPeekToProviderPreferredSize();
    void refreshProviderIcons();
    int boundedDockWidthForWindow(int width) const;
    void showDock(bool applyPreferredWidth);
    void notifyWorkspaceStateChanged();
};

#endif // CONTEXTWORKSPACECONTROLLER_H
