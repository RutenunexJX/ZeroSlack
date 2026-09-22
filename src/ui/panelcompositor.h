#ifndef PANELCOMPOSITOR_H
#define PANELCOMPOSITOR_H

#include <QElapsedTimer>
#include <QImage>
#include <QPointer>
#include <QTimer>
#include <QWidget>
#include <functional>
#include <memory>
#include "panelmotion.h"
#include "zeroslackexport.h"

class QMainWindow;
class QDockWidget;
class NativePanelComposition;
class QMouseEvent;

// Short-lived presentation only. The actual widgets stay at a settled size.
class ZEROSLACK_API PanelCompositor final : public QWidget
{
    Q_OBJECT
public:
    explicit PanelCompositor(QMainWindow* window);
    ~PanelCompositor() override;
    static PanelCompositor* forWindow(QMainWindow* window);
    static constexpr int duration = 255;
    bool begin(QDockWidget* dock, QWidget* content, int expandedWidth,
               int targetWidth, int duration, std::function<void()> complete);
    bool present(QObject* owner, const QRect& area, QList<PanelMotionLayer> layers,
                 bool fromOpen, bool toOpen);
    bool reverse(QObject* owner, bool open, const std::function<void()>& apply);
    void reveal(QObject* owner, Qt::Edge edge, bool open,
                const std::function<QRect()>& panelRect, const std::function<void()>& apply,
                int stationaryBottom = 0);
    void settle();
    void settleFor(QObject* owner);
    bool isActiveFor(const QObject* owner) const { return active && motionOwner == owner; }
    QRect workspaceRect() const;
    void finish();
    bool isActive() const { return active; }
    qreal extent() const;
    QString renderer() const { return lastRenderer; }
    qint64 snapshotBytes() const;
    qreal progress() const;
    double preparationMs() const { return lastPreparationMs; }
    double compositionRefreshRate() const;

signals:
    void started();
    void finished();

protected:
    void paintEvent(QPaintEvent*) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void completeNow();
    bool startMotion(qreal from, qreal to, int duration, std::function<void()> complete);
    void forwardMouseEvent(QWidget* target, QMouseEvent* event);
    QMainWindow* host;
    QPointer<QDockWidget> navigationDock;
    QPointer<QWidget> priorFocus;
    QPointer<QWidget> pointerTarget;
    Qt::MouseButton pointerButton = Qt::NoButton;
    QList<PanelMotionLayer> layers;
    QPointer<QObject> motionOwner;
    QMetaObject::Connection ownerDestroyed;
    std::unique_ptr<NativePanelComposition> native;
    QTimer completionTimer;
    QTimer rasterTimer;
    QElapsedTimer elapsed;
    std::function<void()> completion;
    qreal startExtent = 0;
    qreal targetExtent = 0;
    qreal fullExtent = 0;
    int durationMs = 255;
    bool active = false;
    bool preparing = false;
    bool forwardingInput = false;
    QString lastRenderer;
    double lastPreparationMs = 0;
};

#endif
