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
            // Width-only dock animation reuses one opaque, pre-blended image.
            // Keep the exact fit rectangle; cap the cache for unusually wide images.
            QSize raster(qMax(1, qRound(qreal(pixelSize.height()) * source.width() / source.height())), pixelSize.height());
            const qreal pixels = qreal(raster.width()) * raster.height();
            if (pixels > 8 * 1024 * 1024) raster *= qSqrt(8.0 * 1024 * 1024 / pixels);
            if (scaled.isNull() || raster != scaledSize || scaledDpr != devicePixelRatio
                || scaledBase != base || scaledOpacity != opacity) {
                const QString cacheKey = QStringLiteral("zeroslack.editorBackground:%1:%2:%3:%4:%5:%6")
                    .arg(source.cacheKey()).arg(raster.width()).arg(raster.height()).arg(devicePixelRatio)
                    .arg(base.rgba()).arg(opacity);
                if (!QPixmapCache::find(cacheKey, &scaled)) {
                    scaled = QPixmap(raster);
                    scaled.fill(base);
                    QPainter imagePainter(&scaled);
                    imagePainter.setRenderHint(QPainter::SmoothPixmapTransform);
                    imagePainter.setOpacity(opacity);
                    imagePainter.drawPixmap(scaled.rect(), source);
                    imagePainter.end();
                    scaled.setDevicePixelRatio(devicePixelRatio);
                    QPixmapCache::insert(cacheKey, scaled);
                }
                scaledSize = raster;
                scaledDpr = devicePixelRatio;
                scaledBase = base;
                scaledOpacity = opacity;
            }
            const QSizeF size = QSizeF(target) / devicePixelRatio;
            const QPointF origin(viewport.x() + viewport.width() - size.width(),
                                 viewport.y() + viewport.height() - size.height());
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.drawPixmap(QRectF(origin, size), scaled, QRectF(scaled.rect()));
        }
    }
    painter.restore();
}
