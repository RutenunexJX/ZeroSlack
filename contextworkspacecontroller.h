#ifndef CONTEXTWORKSPACECONTROLLER_H
#define CONTEXTWORKSPACECONTROLLER_H

#include "contextresource.h"
#include "contextworkspacestate.h"
#include "zeroslackexport.h"

#include <QObject>
#include <QPointer>
#include <QStringList>

#include <map>
#include <memory>

class ContextDockHost;
class ContextPeekHost;
class ContextRail;
class IContextContentProvider;
class QDockWidget;
class QEvent;
class QMainWindow;
class QWidget;

enum class ContextOpenMode {
    Peek,
    Pinned,
    TransientDock
};

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
    ContextDockHost* dockHost() const;
    QDockWidget* dockWidget() const;

    bool registerProvider(
        std::unique_ptr<IContextContentProvider> provider);
    bool unregisterProvider(const QString& providerId);
    QStringList providerIds() const;

    bool openResource(
        const ContextResource& resource,
        ContextOpenMode mode = ContextOpenMode::Peek,
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
                        ContextOpenMode mode);
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
    bool activatePinnedProvider(const QString& providerId);
    void resetPeekToProviderPreferredSize();
    int boundedDockWidthForWindow(int width) const;
    void showDock(bool applyPreferredWidth);
    void notifyWorkspaceStateChanged();
};

Q_DECLARE_METATYPE(ContextOpenMode)

#endif // CONTEXTWORKSPACECONTROLLER_H
