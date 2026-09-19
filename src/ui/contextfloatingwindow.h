#ifndef CONTEXTFLOATINGWINDOW_H
#define CONTEXTFLOATINGWINDOW_H

#include "contextfloatingsurface.h"
#include "contextworkspacestate.h"
#include "zeroslackexport.h"
#include <QPointer>
#include <QWidget>

class QToolButton;
class QVBoxLayout;
class QScreen;

class ZEROSLACK_API ContextFloatingWindow final : public QWidget, public ContextFloatingSurface {
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
    QWidget* sidebarDragHandle() const;

signals:
    void pinRequested();
    void closeRequested();
    void fullViewRequested();
    void geometryChanged();
    void sidebarDragStarted();
    void sidebarDragFinished(bool accepted);

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
    QToolButton* pinButton = nullptr;
    QToolButton* fullViewButton = nullptr;
    QToolButton* dragButton = nullptr;
    QPoint dragStart;
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
    void scheduleBackdropRefresh();
    void rememberGeometry();
    void applyGeometry();
    void watchScreen(QScreen* screen);
    void handleScreenChange();
};

#endif // CONTEXTFLOATINGWINDOW_H
