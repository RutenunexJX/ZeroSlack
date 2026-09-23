#ifndef CONTEXTDOCKTRANSITION_H
#define CONTEXTDOCKTRANSITION_H

#include "panelmotion.h"
#include "zeroslackexport.h"
#include <QElapsedTimer>
#include <QPointer>
#include <QTimer>
#include <QWidget>
#include <functional>
#include <memory>

class QMainWindow;
class QDockWidget;
class ContextDockHost;
class ContextFloatingWindow;
class NativePanelComposition;

struct ContextDockTarget {
    QRect rect;
    int index = -1;
    bool bottom = false;
    bool isValid() const { return !rect.isEmpty(); }
};

// A non-interactive presentation surface. Live widgets are transferred once.
class ZEROSLACK_API ContextDockTransition final : public QWidget
{
    Q_OBJECT
public:
    ContextDockTransition(QMainWindow* window, ContextDockHost* dockHost);
    ~ContextDockTransition() override;
    static constexpr int duration = 200;
    ContextDockTarget targetAt(const QPoint& position, QWidget* incoming,
                               QDockWidget* side, QDockWidget* bottom,
                               int preferredWidth, int preferredHeight) const;
    void preview(const ContextDockTarget& target);
    void clearPreview();
    bool transfer(ContextFloatingWindow* source, const QString& key, const std::function<bool()>& apply);
    void finish();
    bool isAnimating() const { return animating; }
    bool isPreviewing() const { return previewing; }
    QRect previewRect() const { return previewing ? geometry() : QRect(); }
    qint64 snapshotBytes() const;
    QString renderer() const { return nativeActive ? QStringLiteral("direct-composition") : QStringLiteral("raster"); }

signals:
    void started();
    void finished();

protected:
    void paintEvent(QPaintEvent*) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QPointer<QMainWindow> host;
    QPointer<ContextDockHost> dock;
    QPointer<QWidget> destination;
    QMetaObject::Connection destinationDestroyed;
    std::unique_ptr<NativePanelComposition> native;
    QList<PanelMotionLayer> layers;
    QElapsedTimer elapsed;
    QTimer completionTimer;
    QTimer rasterTimer;
    bool previewing = false;
    bool animating = false;
    bool preparing = false;
    bool nativeActive = false;
    qreal progress() const;
};

#endif
