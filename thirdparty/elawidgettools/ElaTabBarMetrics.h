#pragma once

#include <QFontMetricsF>
#include <QIcon>
#include <QImage>
#include <QPainter>

// Tab contents share a visual centre even when the font has asymmetric leading
// or an icon has transparent padding. Geometry and hit areas remain logical pixels.
namespace ElaTabBarMetrics {
inline constexpr int CloseExtent = 20;
inline void drawText(QPainter& painter, qreal x, qreal centerY, const QString& text) {
    const QRectF ink = QFontMetricsF(painter.font()).tightBoundingRect(text);
    painter.drawText(QPointF(x, centerY - ink.center().y()), text);
}
inline void drawIcon(QPainter& painter, const QRectF& bounds, const QPixmap& pixmap) {
    const QImage image = pixmap.toImage();
    int left=image.width(), top=image.height(), right=-1, bottom=-1;
    for (int y=0;y<image.height();++y)
        for (int x=0;x<image.width();++x)
            if (qAlpha(image.pixel(x,y))>16) {
                left=qMin(left,x); right=qMax(right,x);
                top=qMin(top,y); bottom=qMax(bottom,y);
            }
    const qreal dpr=pixmap.devicePixelRatioF();
    const QPointF inkCenter=bottom>=top
        ? QPointF((left+right+1)/(2*dpr),(top+bottom+1)/(2*dpr))
        : QPointF(pixmap.width()/(2*dpr),pixmap.height()/(2*dpr));
    painter.drawPixmap(bounds.center()-inkCenter,pixmap);
}
inline void drawClose(QPainter& painter, const QRectF& bounds, const QColor& color) {
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(color,1.2,Qt::SolidLine,Qt::RoundCap));
    const QPointF center=bounds.center();
    painter.drawLine(center+QPointF(-3,-3),center+QPointF(3,3));
    painter.drawLine(center+QPointF(3,-3),center+QPointF(-3,3));
}
}
