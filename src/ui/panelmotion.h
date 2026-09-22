#ifndef PANELMOTION_H
#define PANELMOTION_H

#include <QImage>
#include <QPointF>
#include <QSizeF>

// Images keep their original scale; only their position and visible area change.
struct PanelMotionLayer {
    QImage image;
    QPointF closedPosition;
    QPointF openPosition;
    QSizeF closedClip;
    QSizeF openClip;

    QPointF position(qreal progress) const {
        return closedPosition + (openPosition - closedPosition) * progress;
    }
    QSizeF clip(qreal progress) const {
        return closedClip + (openClip - closedClip) * progress;
    }
    static PanelMotionLayer stationary(const QImage& image) {
        const auto size = image.deviceIndependentSize();
        return {image, {}, {}, size, size};
    }
};

#endif
