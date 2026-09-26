#include "editorbackground.h"

#include <QPainter>
#include <QApplication>
#include <QCache>
#include <QPointer>

namespace {
class BackgroundCache final : public QObject {
public:
    BackgroundCache() : QObject(qApp), pixmaps(64 * 1024) {}
    // Shared across editors. A DPR 2 background can exceed QPixmapCache's
    // default total limit by itself; retain both scrollbar-height variants.
    QCache<QString, QPixmap> pixmaps;
};

QCache<QString, QPixmap>& backgroundCache()
{
    static QPointer<BackgroundCache> cache;
    if (!cache) cache = new BackgroundCache;
    return cache->pixmaps;
}
}

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
    bool painted = false;
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
            painted = true;
            // Width-only dock animation reuses one opaque, pre-blended image.
            // Keep the exact fit rectangle; cap the cache for unusually wide images.
            QSize raster = source.size().scaled(
                QSize(qMax(pixelSize.width(), qCeil(qreal(pixelSize.height()) * source.width() / source.height())),
                      pixelSize.height()), Qt::KeepAspectRatio);
            const qreal pixels = qreal(raster.width()) * raster.height();
            if (pixels > 8 * 1024 * 1024) raster *= qSqrt(8.0 * 1024 * 1024 / pixels);
            if (scaled.isNull() || raster != scaledSize || scaledDpr != devicePixelRatio
                || scaledBase != base || scaledOpacity != opacity) {
                const QString cacheKey = QStringLiteral("zeroslack.editorBackground:%1:%2:%3:%4:%5:%6")
                    .arg(source.cacheKey()).arg(raster.width()).arg(raster.height()).arg(devicePixelRatio)
                    .arg(base.rgba()).arg(opacity);
                auto& cache = backgroundCache();
                if (const auto* cached = cache.object(cacheKey)) {
                    scaled = *cached;
                } else {
                    scaled = QPixmap(raster);
                    scaled.fill(base);
                    QPainter imagePainter(&scaled);
                    imagePainter.setRenderHint(QPainter::SmoothPixmapTransform);
                    imagePainter.setOpacity(opacity);
                    imagePainter.drawPixmap(scaled.rect(), source);
                    imagePainter.end();
                    scaled.setDevicePixelRatio(devicePixelRatio);
                    const int costKiB = qCeil(qreal(raster.width()) * raster.height() * scaled.depth() / (8 * 1024));
                    cache.insert(cacheKey, new QPixmap(scaled), costKiB);
                }
                scaledSize = raster;
                scaledDpr = devicePixelRatio;
                scaledBase = base;
                scaledOpacity = opacity;
            }
            const QSizeF size = QSizeF(target) / devicePixelRatio;
            const QPointF origin(viewport.x() + viewport.width() - size.width(),
                                 viewport.y() + viewport.height() - size.height());
            if (scaled.hasAlphaChannel()) {
                painter.fillRect(dirty, surround);
            } else {
                // The pre-blended raster covers its fit rectangle completely.
                // Fill only the two exposed margins, avoiding another full pass.
                painter.fillRect(QRectF(viewport.x(), viewport.y(), viewport.width(),
                                        origin.y() - viewport.y()), surround);
                painter.fillRect(QRectF(viewport.x(), origin.y(), origin.x() - viewport.x(),
                                        size.height()), surround);
            }
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.drawPixmap(QRectF(origin, size), scaled, QRectF(scaled.rect()));
        }
    }
    if (!painted) painter.fillRect(dirty, base);
    painter.restore();
}
