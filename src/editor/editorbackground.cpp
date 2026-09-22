#include "editorbackground.h"

#include <QPainter>
#include <QPixmapCache>

void EditorBackground::setOptions(const QString& preset, const QString& customPath, int opacity)
{
    QString path;
    if (preset == QStringLiteral("Custom image")) path = customPath.trimmed();
    else if (preset == QStringLiteral("Resting")) path = QStringLiteral(":/backgrounds/resting.png");
    else if (preset == QStringLiteral("Peekaboo")) path = QStringLiteral(":/backgrounds/peekaboo.png");
    else if (preset == QStringLiteral("Balancing")) path = QStringLiteral(":/backgrounds/balancing.png");
    if (path != imagePath) {
        imagePath = path;
        loadAttempted = false;
        source = {};
        scaled = {};
        scaledSize = {};
    }
    imageOpacity = qBound(0, opacity, 100);
}

bool EditorBackground::enabled() const
{
    return !imagePath.isEmpty() && imageOpacity > 0;
}

void EditorBackground::paint(QPainter& painter, const QRect& viewport, const QRect& dirty,
                             qreal devicePixelRatio, const QColor& base)
{
    painter.save();
    painter.setClipRect(dirty);
    painter.fillRect(dirty, base);
    if (enabled()) {
        if (!loadAttempted) {
            source.load(imagePath);
            if (!source.isNull()) sourceBackground = source.toImage().pixelColor(0, 0);
            loadAttempted = true;
        }
        if (!source.isNull() && !viewport.isEmpty()) {
            const QSize pixelSize(qRound(viewport.width() * devicePixelRatio),
                                  qRound(viewport.height() * devicePixelRatio));
            const QSize target = source.size().scaled(pixelSize, Qt::KeepAspectRatio);
            if (scaled.isNull() || target != scaledSize || scaledDpr != devicePixelRatio) {
                const QString cacheKey = QStringLiteral("zeroslack.editorBackground:%1:%2:%3:%4")
                    .arg(source.cacheKey()).arg(target.width()).arg(target.height()).arg(devicePixelRatio);
                if (!QPixmapCache::find(cacheKey, &scaled)) {
                    scaled = source.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    scaled.setDevicePixelRatio(devicePixelRatio);
                    QPixmapCache::insert(cacheKey, scaled);
                }
                scaledSize = target;
                scaledDpr = devicePixelRatio;
            }
            // Keep the complete illustration anchored to the viewport, below all text.
            const QSizeF size = scaled.deviceIndependentSize();
            const QPointF origin(viewport.x() + viewport.width() - size.width(),
                                 viewport.y() + viewport.height() - size.height());
            const qreal themeAttenuation = base.lightnessF() < 0.5 ? 0.35 : 1.0;
            const qreal opacity = imageOpacity / 100.0 * themeAttenuation;
            const qreal surroundOpacity = opacity * sourceBackground.alphaF();
            const auto blend = [surroundOpacity](int background, int foreground) {
                return qRound(background * (1.0 - surroundOpacity) + foreground * surroundOpacity);
            };
            const QColor surround(blend(base.red(), sourceBackground.red()),
                                   blend(base.green(), sourceBackground.green()),
                                   blend(base.blue(), sourceBackground.blue()));
            painter.fillRect(dirty, surround);
            painter.fillRect(QRectF(origin, size), base);
            painter.setOpacity(opacity);
            painter.drawPixmap(origin, scaled);
        }
    }
    painter.restore();
}
