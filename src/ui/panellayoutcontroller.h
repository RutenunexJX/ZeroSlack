#ifndef PANELLAYOUTCONTROLLER_H
#define PANELLAYOUTCONTROLLER_H

#include "panellayoutstate.h"
#include "zeroslackexport.h"

#include <QDockWidget>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVector>

#include <functional>

class QEvent;
class QFrame;
class QMainWindow;
class QStackedWidget;
class QToolButton;
class QVariantAnimation;
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

    static constexpr int kDefaultContentHeight = 280;
    static constexpr int kMinimumContentHeight = 160;
    static constexpr int kMaximumHeightPercent = 55;
    static constexpr int kAnimationDurationMs = 140;

    explicit PanelLayoutController(QMainWindow* mainWindow,
                                   QObject* parent = nullptr);
    ~PanelLayoutController() override;

    void setNavigationDock(QDockWidget* dock);
    bool registerSidePanel(const QString& panelId,
                           QDockWidget* dock);
    bool registerBottomPanel(const QString& panelId,
                             QDockWidget* dock);
    bool registerBottomPanelAlias(const QString& alias,
                                  const QString& panelId);
    void finalize();
    void setMainAreaRequestHandler(std::function<void(QWidget*, QWidget*)> handler) { mainAreaRequest = std::move(handler); }

    PanelLayoutState layoutState() const;
    void restoreLayoutState(const PanelLayoutState& state);
    void resetLayout();

    QStringList bottomPanelIds() const;
    QString activeBottomPanelId() const;
    QString lastBottomPanelId() const;
    bool isBottomPanel(const QDockWidget* dock) const;
    QString panelIdForDock(const QDockWidget* dock) const;
    bool isPanelOpen(const QString& panelId) const;
    bool isPanelPinned(const QString& panelId) const;

    bool closePanel(const QString& panelId);
    bool restorePanel(const QString& panelId);
    bool setPanelPinned(const QString& panelId, bool pinned);
    bool movePanel(const QString& panelId, int destinationIndex);
    QList<BottomPanelContextAction>
    bottomPanelContextActions(const QString& panelId) const;
    void setRegisteredPanelActionRequestHandler(
        RegisteredPanelActionRequestHandler handler);
    bool requestBottomPanelAction(const QString& actionId,
                                  const QString& panelId,
                                  QString* failureReason = nullptr);

    bool isBottomCollapsed() const;
    void setBottomCollapsed(bool collapsed);
    void toggleBottomCollapsed();

    int panelHeight(const QString& panelId) const;
    bool setPanelHeight(const QString& panelId, int height);
    bool resetPanelHeight(const QString& panelId);
    int maximumContentHeight() const;

    void setPanelBadge(const QString& panelId,
                       const QString& text,
                       const QString& tone = QString());
    QString panelBadgeText(const QString& panelId) const;
    QString panelBadgeTone(const QString& panelId) const;


    void bindManagedTabBars();
    void setStateChangedHandler(std::function<void()> handler);
    void setAnimationsEnabled(bool enabled);
    bool animationsEnabled() const;

    QDockWidget* drawerDock() const;
    QWidget* drawerContent() const;
    QWidget* resizeHandle() const;
    QWidget* buttonBar() const;
    QToolButton* buttonForPanel(const QString& panelId) const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct PanelEntry {
        QString id;
        QString label;
        QString initialTitle;
        QPointer<QDockWidget> dock;
        QPointer<QWidget> content;
        QPointer<QToolButton> button;
        int height = kDefaultContentHeight;
        QVariantMap viewState;
        QString badgeText;
        QString badgeTone;
    };

    struct SidePanelEntry {
        QString id;
        QPointer<QDockWidget> dock;
    };

    QPointer<QMainWindow> window;
    QPointer<QDockWidget> navigationDock;
    QPointer<QDockWidget> bottomDrawerDock;
    QPointer<QWidget> bottomDrawerRoot;
    QPointer<QWidget> bottomResizeHandle;
    QPointer<QStackedWidget> bottomContentStack;
    QPointer<QFrame> bottomButtonBar;
    QPointer<QVariantAnimation> heightAnimation;
    QVector<PanelEntry> panels;
    QVector<SidePanelEntry> sidePanels;
    QHash<QString, QString> aliases;
    QStringList defaultOrder;
    QString activePanel;
    QString lastPanel;
    std::function<void(QWidget*, QWidget*)> mainAreaRequest;
    bool collapsed = false;
    bool applying = false;
    bool finalized = false;
    bool animationsEnabledValue = true;
    bool dragging = false;
    bool applyingDrawerGeometry = false;
    bool applyingDrawerStyle = false;
    int dragStartGlobalY = 0;
    int dragStartHeight = kDefaultContentHeight;
    QPointer<QWidget> focusBeforeDrawer;
    std::function<void()> stateChangedHandler;
    RegisteredPanelActionRequestHandler
        registeredPanelActionRequestHandler;

    PanelEntry* entryForId(const QString& panelId);
    const PanelEntry* entryForId(const QString& panelId) const;
    QString canonicalPanelId(const QString& panelId) const;
    int boundedContentHeight(int height) const;
    int visibleContentHeight() const;
    void buildDrawer();
    void buildButton(PanelEntry& entry);
    void activatePanel(PanelEntry& entry, bool moveFocus);
    void applyDrawerState(bool animate);
    void animateContentHeight(int start, int end);
    void applyContentHeight(int height, bool settleDock = true);
    void updateButtons();
    void updateDrawerStyle();
    void capturePanelViewState(PanelEntry& entry) const;
    void restorePanelViewState(PanelEntry& entry);
    QVariantMap captureWidgetState(QWidget* root) const;
    void restoreWidgetState(QWidget* root, const QVariantMap& state);
    void restoreEditorFocus();
    bool focusIsInsideDrawer() const;
    void notifyStateChanged();
};

#endif // PANELLAYOUTCONTROLLER_H
