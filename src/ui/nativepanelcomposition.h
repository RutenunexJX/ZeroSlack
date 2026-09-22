#ifndef NATIVEPANELCOMPOSITION_H
#define NATIVEPANELCOMPOSITION_H

#include <QImage>
#include <QFuture>
#include <QRect>
#include <memory>
#include "panelmotion.h"

class QWidget;

// The implementation uses system DirectComposition on Windows; no new Qt runtime.
class NativePanelComposition final
{
public:
    NativePanelComposition();
    ~NativePanelComposition();
    void warmUp();
    bool prepare(QWidget* parent, const QRect& geometry,
                 const QList<PanelMotionLayer>& layers);
    bool animate(const QList<PanelMotionLayer>& layers, qreal from, qreal to, int durationMs);
    void clear();
    double refreshRate() const;

private:
    class State;
    std::shared_ptr<State> state;
    QFuture<bool> initialization;
};

#endif
