#ifndef CONTEXTWORKSPACECONTROLLER_H
#define CONTEXTWORKSPACECONTROLLER_H

#include "contextresource.h"
#include "contextplacement.h"
#include "contextworkspacestate.h"
#include "zeroslackexport.h"

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QSet>
#include <QHash>

#include <map>
#include <memory>

class ContextDockHost;
class ContextDockTransition;
class ContextPeekHost;
class ContextFloatingSurface;
class ContextFloatingWindow;
class ContextRail;
class ContextToolbox;
class QStackedWidget;
class IContextContentProvider;
class QDockWidget;
class QEvent;
class QMainWindow;
class QWidget;
class QMenu;
class QAction;
#ifdef ZEROSLACK_ENABLE_ELA
class ElaDrawerArea;
#endif

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
    QList<ContextFloatingWindow*> floatingWindows() const;
    bool focusResource(const QString& resourceKey);
    bool closeFloatingResource(const QString& resourceKey);
    bool pinFloatingResource(const QString& resourceKey, bool bottom = false, int index = -1);
    bool dragOutResource(const QString& resourceKey, const QPoint& globalPosition, QString* failureReason = nullptr);
    void setFloatingCollapsed(bool collapsed);
    bool floatingCollapsed() const;
    QMenu* createRailContextMenu(const QString& providerId);
    void setActiveDocument(const QString& filePath);
    void documentClosed(const QString& filePath);
    bool setResourceBinding(const QString& resourceKey, ContextBinding binding, QString* failureReason = nullptr);
    QString boundDocument(const QString& resourceKey) const;
    void setFloatingOpacity(int percentage);
    ContextDockHost* dockHost() const;
    QDockWidget* dockWidget() const;
    QDockWidget* bottomDockWidget() const;
    bool dockVisible() const;
    bool canShowDock() const;
    // The live view in either dock area or a floating window.
    QWidget* viewForResource(const QString& resourceKey) const;
    bool setDockVisible(bool visible, QString* failureReason = nullptr);

    bool registerProvider(
        std::unique_ptr<IContextContentProvider> provider);
    bool unregisterProvider(const QString& providerId);
    QStringList providerIds() const;
    bool isProviderPinned(const QString& providerId) const;
    bool setProviderPinned(const QString& providerId, bool pinned);
    bool toolboxVisible() const;
    bool openTool(const QString& providerId);

    bool openResource(
        const ContextResource& resource,
        ContextPlacement placement = {},
        QString* failureReason = nullptr);
    bool pinPeek(QString* failureReason = nullptr, bool bottom = false, int index = -1);
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
    QList<QPointer<ContextFloatingWindow>> floatingWindowValues;
    ContextWorkspaceState lastFloatingGeometry;
    QStringList focusOrder;
    QString focusedResourceKey;
    QSet<QString> hiddenFloatingKeys;
    bool floatingCollapsedValue = false;
    int floatingOpacity = 90;
    QPointer<QAction> collapseFloatingAction;
    QString activeDocumentPath;
    QHash<QString, QString> documentBindings;
    QSet<QString> keptFloatingKeys;
    QMap<QString, QList<ContextFloatingInstanceState>> documentLayouts;
    QStringList documentLayoutOrder;
    bool preservingDocumentLayout = false;
    ContextFloatingSurface* activeFloatingSurface = nullptr;
    QPointer<ContextDockHost> dockHostValue;
    QPointer<QStackedWidget> sidebarPages;
    QPointer<ContextToolbox> toolbox;
    QSet<QString> unpinnedProviders;
    QPointer<QDockWidget> dockValue;
#ifdef ZEROSLACK_ENABLE_ELA
    QPointer<ElaDrawerArea> sideDrawer;
#endif
    QPointer<QDockWidget> bottomDockValue;
    QPointer<ContextDockTransition> dockTransition;
    QPointer<ContextFloatingWindow> floatingDragSource;
    bool dockingTransition = false;
    int preferredBottomHeight = 300;
    bool previewingDocks = false;
    bool sideWasVisible = false;
    bool bottomWasVisible = false;
    std::map<QString,
             std::unique_ptr<IContextContentProvider>> providers;
    QString currentWorkspaceRoot;
    QString transientDockResourceKey;
    int preferredDockWidthValue =
        ContextWorkspaceState::kDefaultDockWidth;
    bool restoringState = false;
    bool applyingDockWidth = false;
    bool applyingBottomHeight = false;

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
    QString activeSidebarProviderId() const;
    void activateRailProvider(const QString& providerId);
    bool sidebarVisible() const;
    void showToolbox();
    static ContextPlacement defaultPlacementFor(const QString& providerId);
    static ContextPresentation presentationFor(const ContextPlacement& placement);
    ContextFloatingSurface* floatingSurfaceFor() const;
    ContextFloatingSurface* floatingSurfaceFor(const ContextViewCapabilities& capabilities);
    ContextFloatingWindow* availableFloatingWindow();
    ContextFloatingSurface* surfaceWithResource(const QString& key) const;
    QList<ContextFloatingSurface*> floatingSurfaces() const;
    void recordFocus(QString key);
    void applyFloatingVisibility();
    void captureLastFloatingGeometry(ContextFloatingWindow* host);
    ContextFloatingInstanceState captureFloatingInstance(ContextFloatingWindow* host) const;
    void restoreFloatingInstances(const QList<ContextFloatingInstanceState>& instances,
                                  ContextWorkspaceRestoreResult& result, const QString& documentPath = {});
    QString normalizedDocumentPath(const QString& path) const;
    bool floatingEligible(const QString& key) const;
    void rememberDocumentLayout(const QString& path);
    void touchDocumentLayout(const QString& path);
    void forgetStoredResource(const QString& key);
    void restoreDocumentLayout(const QString& path, ContextWorkspaceRestoreResult& result);
    QWidget* floatingWidget() const;
    bool openInFloatingSurface(const ContextResource& resource,
                               ContextPlacement placement,
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
    void showDock(bool applyPreferredWidth, bool animate = true);
    void showResourceDock(const QString& key);
    void hideEmptyDocks();
    void beginDockPreview();
    void endDockPreview();
    void applyDockVisibility(bool visible, bool applyPreferredWidth = false, bool animate = true);
    void notifyWorkspaceStateChanged();
};

#endif // CONTEXTWORKSPACECONTROLLER_H
