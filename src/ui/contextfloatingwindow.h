#ifndef CONTEXTFLOATINGWINDOW_H
#define CONTEXTFLOATINGWINDOW_H

#include "contextfloatingsurface.h"
#include "contextworkspacestate.h"
#include "zeroslackexport.h"
#include <QPointer>
#include <QWidget>

#ifdef ZEROSLACK_ENABLE_ELA
#include "ElaDockWidget.h"
using ContextFloatingWindowBase = ElaDockWidget;
#else
using ContextFloatingWindowBase = QWidget;
#endif

class QToolButton;
class QVBoxLayout;
class QScreen;
class QLabel;
class QScrollBar;

class ZEROSLACK_API ContextFloatingWindow final : public ContextFloatingWindowBase, public ContextFloatingSurface {
    Q_OBJECT
public:
    ContextFloatingWindow(QWidget* mainWindow, QWidget* editorRegion);
    bool hasResource() const override;
    ContextResource resource() const override;
    QWidget* view() const override;
    void setView(const ContextResource& resource, QWidget* view) override;
    QWidget* takeView() override;
    void clearView() override;
    bool updateResource(const ContextResource& resource) override;
    void setActionsAvailable(bool pinAvailable, bool fullViewAvailable) override;
    void setInitialSize(const QSize& size);
    void setBackgroundOpacity(int percentage);
    int backgroundOpacity() const;
    bool hasAcrylicBackdrop() const;
    void captureGeometry(ContextWorkspaceState& state) const;
    void restoreGeometry(const ContextWorkspaceState& state);
    QWidget* titleBar() const;
    bool canDock() const;
    void beginNativeDockDrag(const QPoint& position);

signals:
    void closeRequested();
    void geometryChanged();
    void titleDragStarted();
    void titleDragMoved(const QPoint& globalPosition);
    void titleDragFinished(const QPoint& globalPosition, bool cancelled);
    void nativeDocked(Qt::DockWidgetArea area);

protected:
    void closeEvent(QCloseEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool event(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    QPointer<QWidget> editorRegion;
    QPointer<QWidget> currentView;
    ContextResource currentResource;
    QVBoxLayout* contentLayout = nullptr;
    QToolButton* fitButton = nullptr;
    QWidget* titleActions = nullptr;
#ifdef ZEROSLACK_ENABLE_ELA
    QWidget* dockTitle = nullptr;
    QLabel* titleLabel = nullptr;
    quint64 dockGeneration = 0;
    QList<QPair<QPointer<QScrollBar>, int>> dockScrollPositions;
    void scheduleDockCommit();
    void captureDockScrollPositions();
    void restoreDockScrollPositions(QWidget* view);
#endif
    bool dockingAllowed = true;
    bool releasingView = false;
    bool dockDragActive = false;
    QSize initialSize{520, 440};
    QRect storedGeometry{0, 0, 520, 440};
    QString storedScreenName;
    bool geometryValid = false;
    bool applyingGeometry = false;
    int opacityPercentage = 90;
    bool backdropReady = false;
    bool acrylicBackdrop = false;
    bool backdropUpdatePending = false;

    void refreshBackdrop();
    void refreshTitle();
    void cancelDockDrag();
    void layoutTitleBar();
    void scheduleBackdropRefresh();
    void rememberGeometry();
    void applyGeometry();
    void watchScreen(QScreen* screen);
    void handleScreenChange();
};

#endif // CONTEXTFLOATINGWINDOW_H
