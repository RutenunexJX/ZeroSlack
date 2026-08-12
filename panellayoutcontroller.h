#ifndef PANELLAYOUTCONTROLLER_H
#define PANELLAYOUTCONTROLLER_H

#include "panellayoutstate.h"
#include "zeroslackexport.h"

#include <QDockWidget>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QVector>

#include <functional>

class QMainWindow;
class QEvent;
class QPoint;
class QTabBar;
class QWidget;

struct BottomPanelContextAction {
    QString actionId;
    QString label;
    QString executionRoute;
    bool enabled = false;
};

class ZEROSLACK_API PanelLayoutController : public QObject
{
public:
    using RegisteredPanelActionRequestHandler =
        std::function<bool(const QString&,
                           const QString&,
                           QString*)>;

    explicit PanelLayoutController(QMainWindow* mainWindow,
                                   QObject* parent = nullptr);
    ~PanelLayoutController() override;

    void setNavigationDock(QDockWidget* dock);
    bool registerBottomPanel(const QString& panelId,
                             QDockWidget* dock);
    void finalize();

    PanelLayoutState layoutState() const;
    void restoreLayoutState(const PanelLayoutState& state);
    void resetLayout();

    QStringList bottomPanelIds() const;
    QString activeBottomPanelId() const;
    bool isBottomPanel(const QDockWidget* dock) const;
    QString panelIdForDock(const QDockWidget* dock) const;
    bool isPanelOpen(const QString& panelId) const;
    bool isPanelPinned(const QString& panelId) const;

    bool closePanel(const QString& panelId);
    bool restorePanel(const QString& panelId);
    bool setPanelPinned(const QString& panelId, bool pinned);
    bool movePanel(const QString& panelId, int destinationIndex);
    QList<BottomPanelContextAction>
    bottomPanelContextActions(
        const QString& panelId) const;
    void setRegisteredPanelActionRequestHandler(
        RegisteredPanelActionRequestHandler handler);
    bool requestBottomPanelAction(
        const QString& actionId,
        const QString& panelId,
        QString* failureReason = nullptr);

    bool isBottomCollapsed() const;
    void setBottomCollapsed(bool collapsed);
    void toggleBottomCollapsed();

    bool isFocusModeActive() const;
    void setFocusModeActive(bool active);
    void toggleFocusMode();

    void bindManagedTabBars();
    void setStateChangedHandler(std::function<void()> handler);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct PanelEntry {
        QString id;
        QString initialTitle;
        QPointer<QDockWidget> dock;
        QPointer<QWidget> content;
        QDockWidget::DockWidgetFeatures dockFeatures;
    };

    QPointer<QMainWindow> window;
    QPointer<QDockWidget> navigationDock;
    QVector<PanelEntry> panels;
    QStringList defaultOrder;
    QStringList order;
    QHash<QString, bool> panelOpen;
    QSet<QString> pinnedPanels;
    QList<QPointer<QTabBar>> managedTabBars;
    QString activePanel;
    int expandedHeight = 240;
    int expandedGeometrySamples = 0;
    bool collapsed = false;
    bool applying = false;
    bool bottomGeometrySyncPending = false;
    bool initialBottomGeometryPending = false;
    bool resizingBottomPanel = false;
    bool finalized = false;
    bool focusMode = false;
    bool navigationOpenBeforeFocus = false;
    QHash<QString, bool> panelOpenBeforeFocus;
    std::function<void()> stateChangedHandler;
    RegisteredPanelActionRequestHandler
        registeredPanelActionRequestHandler;

    PanelEntry* entryForId(const QString& panelId);
    const PanelEntry* entryForId(const QString& panelId) const;
    QString idForTab(const QTabBar* bar, int index) const;
    int currentExpandedBottomHeight() const;
    PanelEntry* activeBottomEntry();
    const PanelEntry* activeBottomEntry() const;
    QTabBar* managedBottomTabBar() const;
    int visibleBottomContentHeight() const;
    void prepareContentForManualResize(PanelEntry& entry);
    void scheduleBottomGeometrySync();
    void synchronizeBottomStateFromGeometry();
    void setBottomCollapsedState(bool shouldCollapse,
                                 bool resizeDock,
                                 bool notify);
    void applyOrder();
    void applyTabBarOrder();
    void applyPinnedFeatures(PanelEntry& entry);
    void applyBottomPanelGeometry(bool shouldCollapse);
    void updateTabCloseButtons();
    void synchronizeOrderFromTabBar(QTabBar* bar);
    void showTabContextMenu(QTabBar* bar, const QPoint& position);
    void notifyStateChanged();
};

#endif // PANELLAYOUTCONTROLLER_H
