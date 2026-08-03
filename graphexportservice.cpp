#include "graphexportservice.h"

#include <QBuffer>
#include <QFileInfo>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QImage>
#include <QImageWriter>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPointer>
#include <QSaveFile>
#include <QSvgGenerator>
#include <QThread>
#include <QWidget>
#include <QRegion>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace {

constexpr qreal kReferenceDpi = 96.0;
constexpr qreal kMaximumDpi = 9600.0;
constexpr qint64 kMaximumRasterPixels = 100000000;
constexpr int kMaximumOutputDimension = 100000;

struct EncodeResult {
    bool success = false;
    GraphExportFailure failure = GraphExportFailure::EncodeFailed;
    QString reason;
    QByteArray bytes;
};

bool finiteRect(const QRectF& rect)
{
    return qIsFinite(rect.left()) && qIsFinite(rect.top())
        && qIsFinite(rect.width()) && qIsFinite(rect.height());
}

QRectF visibleSceneBounds(QGraphicsScene* scene)
{
    if (!scene)
        return {};

    QRectF bounds;
    bool found = false;
    const QList<QGraphicsItem*> items = scene->items(Qt::AscendingOrder);
    for (QGraphicsItem* item : items) {
        if (!item || !item->isVisible() || item->effectiveOpacity() <= 0.0)
            continue;
        QRectF itemBounds = item->sceneBoundingRect().normalized();
        if (!finiteRect(itemBounds))
            continue;
        if (itemBounds.width() <= 0.0)
            itemBounds.adjust(-0.5, 0.0, 0.5, 0.0);
        if (itemBounds.height() <= 0.0)
            itemBounds.adjust(0.0, -0.5, 0.0, 0.5);
        bounds = found ? bounds.united(itemBounds) : itemBounds;
        found = true;
    }
    return found ? bounds : QRectF();
}

bool sceneHasVisibleContent(QGraphicsScene* scene)
{
    return scene && !visibleSceneBounds(scene).isEmpty();
}

GraphExportFormat formatFromPath(const QString& outputPath)
{
    const QString suffix = QFileInfo(outputPath).suffix().toLower();
    if (suffix == QStringLiteral("svg"))
        return GraphExportFormat::Svg;
    if (suffix == QStringLiteral("pdf"))
        return GraphExportFormat::Pdf;
    if (suffix == QStringLiteral("png"))
        return GraphExportFormat::Png;
    return GraphExportFormat::Automatic;
}

GraphExportResult failureResult(
    const QString& outputPath,
    GraphExportFormat format,
    GraphExportFailure failure,
    const QString& reason,
    const QSize& outputPixelSize = {},
    const QRectF& sourceRect = {})
{
    GraphExportResult result;
    result.outputPath = outputPath;
    result.format = format;
    result.failure = failure;
    result.failureReason = reason;
    result.outputPixelSize = outputPixelSize;
    result.sourceRect = sourceRect;
    return result;
}

QString validateOptions(const GraphExportOptions& options)
{
    if (!qIsFinite(options.dpi) || options.dpi <= 0.0
        || options.dpi > kMaximumDpi) {
        return QStringLiteral(
            "DPI must be finite, greater than zero, and no more than %1")
            .arg(kMaximumDpi, 0, 'f', 0);
    }

    const bool defaultSize =
        options.targetPixelSize == QSize()
        || options.targetPixelSize == QSize(0, 0);
    const bool explicitSize =
        options.targetPixelSize.width() > 0
        && options.targetPixelSize.height() > 0;
    if (!defaultSize && !explicitSize) {
        return QStringLiteral(
            "targetPixelSize must be empty or contain two positive dimensions");
    }

    const QMargins margins = options.marginPixels;
    if (margins.left() < 0 || margins.top() < 0
        || margins.right() < 0 || margins.bottom() < 0) {
        return QStringLiteral("marginPixels cannot contain negative values");
    }
    return {};
}

QSize resolvedOutputSize(const QRectF& sourceRect,
                         const GraphExportOptions& options,
                         QString* failureReason)
{
    QSize size = options.targetPixelSize;
    if (size.isEmpty()) {
        const qreal scale = options.dpi / kReferenceDpi;
        const qreal width =
            std::ceil(sourceRect.width() * scale)
            + options.marginPixels.left() + options.marginPixels.right();
        const qreal height =
            std::ceil(sourceRect.height() * scale)
            + options.marginPixels.top() + options.marginPixels.bottom();
        if (!qIsFinite(width) || !qIsFinite(height)
            || width > std::numeric_limits<int>::max()
            || height > std::numeric_limits<int>::max()) {
            if (failureReason)
                *failureReason = QStringLiteral("derived output size is invalid");
            return {};
        }
        size = QSize(std::max(1, static_cast<int>(width)),
                     std::max(1, static_cast<int>(height)));
    }

    const qint64 horizontalMargins =
        static_cast<qint64>(options.marginPixels.left())
        + options.marginPixels.right();
    const qint64 verticalMargins =
        static_cast<qint64>(options.marginPixels.top())
        + options.marginPixels.bottom();
    if (size.width() <= horizontalMargins
        || size.height() <= verticalMargins) {
        if (failureReason) {
            *failureReason = QStringLiteral(
                "targetPixelSize must leave a positive area inside the margins");
        }
        return {};
    }
    if (size.width() > kMaximumOutputDimension
        || size.height() > kMaximumOutputDimension) {
        if (failureReason) {
            *failureReason = QStringLiteral(
                "output dimensions exceed the supported maximum of %1 pixels")
                .arg(kMaximumOutputDimension);
        }
        return {};
    }
    return size;
}

QRectF fittedTargetRect(const QSizeF& canvasSize,
                        const QMargins& margins,
                        const QRectF& sourceRect)
{
    const QRectF available(
        margins.left(),
        margins.top(),
        canvasSize.width() - margins.left() - margins.right(),
        canvasSize.height() - margins.top() - margins.bottom());
    const qreal scale = std::min(
        available.width() / sourceRect.width(),
        available.height() / sourceRect.height());
    const QSizeF renderedSize(
        sourceRect.width() * scale,
        sourceRect.height() * scale);
    return QRectF(
        QPointF(
            available.center().x() - renderedSize.width() / 2.0,
            available.center().y() - renderedSize.height() / 2.0),
        renderedSize);
}

void configurePainter(QPainter& painter,
                      const GraphExportOptions& options)
{
    painter.setRenderHint(
        QPainter::Antialiasing, options.antialiasing);
    painter.setRenderHint(
        QPainter::TextAntialiasing, options.textAntialiasing);
    painter.setRenderHint(
        QPainter::SmoothPixmapTransform, options.smoothPixmapTransform);
}

bool renderSource(const GraphExportSource& source,
                  QPainter& painter,
                  const QSizeF& canvasSize,
                  const QRectF& sourceRect,
                  const GraphExportOptions& options,
                  QString* failureReason)
{
    configurePainter(painter, options);
    if (options.backgroundColor.isValid()) {
        painter.fillRect(
            QRectF(QPointF(0.0, 0.0), canvasSize),
            options.backgroundColor);
    }
    const QRectF targetRect =
        fittedTargetRect(canvasSize, options.marginPixels, sourceRect);
    if (targetRect.isEmpty()) {
        if (failureReason)
            *failureReason = QStringLiteral("resolved render target is empty");
        return false;
    }
    return source.render(
        painter, targetRect, sourceRect, failureReason);
}

EncodeResult encodePng(const GraphExportSource& source,
                       const QSize& outputSize,
                       const QRectF& sourceRect,
                       const GraphExportOptions& options)
{
    const qint64 pixelCount =
        static_cast<qint64>(outputSize.width()) * outputSize.height();
    if (pixelCount <= 0 || pixelCount > kMaximumRasterPixels) {
        return {
            false,
            GraphExportFailure::InvalidOptions,
            QStringLiteral(
                "PNG output contains %1 pixels; the supported maximum is %2")
                .arg(pixelCount)
                .arg(kMaximumRasterPixels),
            {}};
    }

    QImage image(outputSize, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) {
        return {
            false,
            GraphExportFailure::EncodeFailed,
            QStringLiteral("could not allocate the PNG image"),
            {}};
    }
    image.fill(Qt::transparent);
    const int dotsPerMeter =
        qRound(options.dpi / 0.0254);
    image.setDotsPerMeterX(dotsPerMeter);
    image.setDotsPerMeterY(dotsPerMeter);

    QString renderFailure;
    {
        QPainter painter(&image);
        if (!painter.isActive()) {
            return {
                false,
                GraphExportFailure::EncodeFailed,
                QStringLiteral("could not initialize the PNG painter"),
                {}};
        }
        if (!renderSource(
                source,
                painter,
                QSizeF(outputSize),
                sourceRect,
                options,
                &renderFailure)) {
            painter.end();
            return {
                false,
                GraphExportFailure::RenderFailed,
                renderFailure.isEmpty()
                    ? QStringLiteral("the graph source rejected PNG rendering")
                    : renderFailure,
                {}};
        }
    }

    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly)) {
        return {
            false,
            GraphExportFailure::EncodeFailed,
            QStringLiteral("could not initialize the PNG output buffer"),
            {}};
    }
    QImageWriter writer(&buffer, QByteArrayLiteral("png"));
    writer.setQuality(100);
    if (!writer.write(image)) {
        return {
            false,
            GraphExportFailure::EncodeFailed,
            writer.errorString().isEmpty()
                ? QStringLiteral("PNG encoding failed")
                : writer.errorString(),
            {}};
    }
    buffer.close();
    return {true, GraphExportFailure::None, {}, bytes};
}

EncodeResult encodeSvg(const GraphExportSource& source,
                       const QSize& outputSize,
                       const QRectF& sourceRect,
                       const GraphExportOptions& options)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly)) {
        return {
            false,
            GraphExportFailure::EncodeFailed,
            QStringLiteral("could not initialize the SVG output buffer"),
            {}};
    }

    QString renderFailure;
    {
        QSvgGenerator generator;
        generator.setOutputDevice(&buffer);
        generator.setSize(outputSize);
        generator.setViewBox(QRect(QPoint(0, 0), outputSize));
        generator.setResolution(qRound(options.dpi));
        generator.setTitle(QStringLiteral("ZeroSlack graph export"));
        generator.setDescription(
            QStringLiteral("Complete graph content exported by ZeroSlack"));

        QPainter painter;
        if (!painter.begin(&generator)) {
            return {
                false,
                GraphExportFailure::EncodeFailed,
                QStringLiteral("could not initialize the SVG painter"),
                {}};
        }
        if (!renderSource(
                source,
                painter,
                QSizeF(outputSize),
                sourceRect,
                options,
                &renderFailure)) {
            painter.end();
            return {
                false,
                GraphExportFailure::RenderFailed,
                renderFailure.isEmpty()
                    ? QStringLiteral("the graph source rejected SVG rendering")
                    : renderFailure,
                {}};
        }
        if (!painter.end()) {
            return {
                false,
                GraphExportFailure::EncodeFailed,
                QStringLiteral("SVG painter finalization failed"),
                {}};
        }
    }
    buffer.close();
    if (bytes.isEmpty()) {
        return {
            false,
            GraphExportFailure::EncodeFailed,
            QStringLiteral("SVG encoding produced no data"),
            {}};
    }
    return {true, GraphExportFailure::None, {}, bytes};
}

EncodeResult encodePdf(const GraphExportSource& source,
                       const QSize& outputSize,
                       const QRectF& sourceRect,
                       const GraphExportOptions& options)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly)) {
        return {
            false,
            GraphExportFailure::EncodeFailed,
            QStringLiteral("could not initialize the PDF output buffer"),
            {}};
    }

    QString renderFailure;
    {
        QPdfWriter writer(&buffer);
        writer.setTitle(QStringLiteral("ZeroSlack graph export"));
        writer.setCreator(QStringLiteral("ZeroSlack"));
        writer.setResolution(qRound(options.dpi));
        const QSizeF pageMillimeters(
            outputSize.width() / options.dpi * 25.4,
            outputSize.height() / options.dpi * 25.4);
        writer.setPageSize(
            QPageSize(
                pageMillimeters,
                QPageSize::Millimeter,
                QStringLiteral("ZeroSlack graph export"),
                QPageSize::ExactMatch));
        writer.setPageMargins(
            QMarginsF(0.0, 0.0, 0.0, 0.0),
            QPageLayout::Millimeter);

        QPainter painter;
        if (!painter.begin(&writer)) {
            return {
                false,
                GraphExportFailure::EncodeFailed,
                QStringLiteral("could not initialize the PDF painter"),
                {}};
        }
        const QSizeF deviceSize(writer.width(), writer.height());
        if (!renderSource(
                source,
                painter,
                deviceSize,
                sourceRect,
                options,
                &renderFailure)) {
            painter.end();
            return {
                false,
                GraphExportFailure::RenderFailed,
                renderFailure.isEmpty()
                    ? QStringLiteral("the graph source rejected PDF rendering")
                    : renderFailure,
                {}};
        }
        if (!painter.end()) {
            return {
                false,
                GraphExportFailure::EncodeFailed,
                QStringLiteral("PDF painter finalization failed"),
                {}};
        }
    }
    buffer.close();
    if (bytes.isEmpty()) {
        return {
            false,
            GraphExportFailure::EncodeFailed,
            QStringLiteral("PDF encoding produced no data"),
            {}};
    }
    return {true, GraphExportFailure::None, {}, bytes};
}

GraphExportResult writeAtomically(
    const QString& outputPath,
    GraphExportFormat format,
    const QSize& outputSize,
    const QRectF& sourceRect,
    const QByteArray& bytes)
{
    QSaveFile output(outputPath);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly)) {
        return failureResult(
            outputPath,
            format,
            GraphExportFailure::OutputOpenFailed,
            QStringLiteral("could not open the atomic output: %1")
                .arg(output.errorString()),
            outputSize,
            sourceRect);
    }
    if (output.write(bytes) != bytes.size()) {
        const QString reason = output.errorString();
        output.cancelWriting();
        return failureResult(
            outputPath,
            format,
            GraphExportFailure::OutputWriteFailed,
            QStringLiteral("could not write the complete output: %1")
                .arg(reason),
            outputSize,
            sourceRect);
    }
    if (!output.commit()) {
        return failureResult(
            outputPath,
            format,
            GraphExportFailure::OutputCommitFailed,
            QStringLiteral("could not atomically replace the output: %1")
                .arg(output.errorString()),
            outputSize,
            sourceRect);
    }

    GraphExportResult result;
    result.success = true;
    result.format = format;
    result.failure = GraphExportFailure::None;
    result.outputPath = outputPath;
    result.outputPixelSize = outputSize;
    result.sourceRect = sourceRect;
    result.bytesWritten = bytes.size();
    return result;
}

}

GraphExportSource GraphExportSource::fromGraphicsScene(
    QGraphicsScene* scene)
{
    if (!scene)
        return {};
    QPointer<QGraphicsScene> guardedScene(scene);
    return fromRenderer(
        [guardedScene]() {
            return visibleSceneBounds(guardedScene.data());
        },
        [guardedScene]() {
            return sceneHasVisibleContent(guardedScene.data());
        },
        [guardedScene](
            QPainter& painter,
            const QRectF& targetRect,
            const QRectF& sourceRect,
            QString* failureReason) {
            QGraphicsScene* currentScene = guardedScene.data();
            if (!currentScene) {
                if (failureReason)
                    *failureReason = QStringLiteral("graphics scene was destroyed");
                return false;
            }
            if (currentScene->thread() != QThread::currentThread()) {
                if (failureReason) {
                    *failureReason = QStringLiteral(
                        "graphics scene export must run on its owning thread");
                }
                return false;
            }
            currentScene->render(
                &painter,
                targetRect,
                sourceRect,
                Qt::IgnoreAspectRatio);
            return painter.isActive();
        });
}

GraphExportSource GraphExportSource::fromWidget(QWidget* widget)
{
    if (!widget)
        return {};
    QPointer<QWidget> guardedWidget(widget);
    return fromRenderer(
        [guardedWidget]() {
            const QWidget* currentWidget = guardedWidget.data();
            return currentWidget
                ? QRectF(currentWidget->rect())
                : QRectF();
        },
        [guardedWidget]() {
            const QWidget* currentWidget = guardedWidget.data();
            return currentWidget && !currentWidget->size().isEmpty();
        },
        [guardedWidget](
            QPainter& painter,
            const QRectF& targetRect,
            const QRectF& sourceRect,
            QString* failureReason) {
            QWidget* currentWidget = guardedWidget.data();
            if (!currentWidget) {
                if (failureReason)
                    *failureReason = QStringLiteral("widget was destroyed");
                return false;
            }
            if (currentWidget->thread() != QThread::currentThread()) {
                if (failureReason) {
                    *failureReason = QStringLiteral(
                        "widget export must run on its owning thread");
                }
                return false;
            }
            const qreal xScale =
                targetRect.width() / sourceRect.width();
            const qreal yScale =
                targetRect.height() / sourceRect.height();
            painter.save();
            painter.setClipRect(targetRect);
            painter.translate(targetRect.topLeft());
            painter.scale(xScale, yScale);
            painter.translate(-sourceRect.topLeft());
            currentWidget->render(
                &painter,
                QPoint(),
                QRegion(sourceRect.toAlignedRect()),
                QWidget::DrawWindowBackground | QWidget::DrawChildren);
            painter.restore();
            return painter.isActive();
        });
}

GraphExportSource GraphExportSource::fromRenderer(
    BoundsProvider boundsProvider,
    ContentPredicate contentPredicate,
    RenderCallback renderer)
{
    GraphExportSource source;
    source.boundsProvider = std::move(boundsProvider);
    source.contentPredicate = std::move(contentPredicate);
    source.renderer = std::move(renderer);
    return source;
}

bool GraphExportSource::isValid() const
{
    return static_cast<bool>(boundsProvider)
        && static_cast<bool>(contentPredicate)
        && static_cast<bool>(renderer);
}

QRectF GraphExportSource::contentBounds() const
{
    return boundsProvider ? boundsProvider() : QRectF();
}

bool GraphExportSource::hasContent() const
{
    return contentPredicate && contentPredicate();
}

bool GraphExportSource::render(
    QPainter& painter,
    const QRectF& targetRect,
    const QRectF& sourceRect,
    QString* failureReason) const
{
    if (!renderer) {
        if (failureReason)
            *failureReason = QStringLiteral("graph renderer is not available");
        return false;
    }
    return renderer(painter, targetRect, sourceRect, failureReason);
}

GraphExportResult GraphExportService::exportGraphicsScene(
    QGraphicsScene* scene,
    const QString& outputPath,
    const GraphExportOptions& options)
{
    return exportSource(
        GraphExportSource::fromGraphicsScene(scene),
        outputPath,
        options);
}

GraphExportResult GraphExportService::exportWidget(
    QWidget* widget,
    const QString& outputPath,
    const GraphExportOptions& options)
{
    return exportSource(
        GraphExportSource::fromWidget(widget),
        outputPath,
        options);
}

GraphExportResult GraphExportService::exportSource(
    const GraphExportSource& source,
    const QString& outputPath,
    const GraphExportOptions& options)
{
    GraphExportFormat format = options.format;
    if (format == GraphExportFormat::Automatic)
        format = formatFromPath(outputPath);

    if (outputPath.trimmed().isEmpty()) {
        return failureResult(
            outputPath,
            format,
            GraphExportFailure::InvalidOptions,
            QStringLiteral("output path cannot be empty"));
    }
    if (format == GraphExportFormat::Automatic) {
        return failureResult(
            outputPath,
            format,
            GraphExportFailure::UnsupportedFormat,
            QStringLiteral(
                "output format must be SVG, PDF, or PNG, or use a matching suffix"));
    }
    if (!source.isValid()) {
        return failureResult(
            outputPath,
            format,
            GraphExportFailure::InvalidSource,
            QStringLiteral("graph export source is not valid"));
    }

    const QString optionsFailure = validateOptions(options);
    if (!optionsFailure.isEmpty()) {
        return failureResult(
            outputPath,
            format,
            GraphExportFailure::InvalidOptions,
            optionsFailure);
    }
    if (!source.hasContent()) {
        return failureResult(
            outputPath,
            format,
            GraphExportFailure::EmptySource,
            QStringLiteral("graph export source has no visible content"));
    }

    const QRectF sourceRect = source.contentBounds().normalized();
    if (!finiteRect(sourceRect) || sourceRect.isEmpty()) {
        return failureResult(
            outputPath,
            format,
            GraphExportFailure::EmptySource,
            QStringLiteral("graph export source bounds are empty or invalid"));
    }

    QString sizeFailure;
    const QSize outputSize =
        resolvedOutputSize(sourceRect, options, &sizeFailure);
    if (outputSize.isEmpty()) {
        return failureResult(
            outputPath,
            format,
            GraphExportFailure::InvalidOptions,
            sizeFailure,
            {},
            sourceRect);
    }

    EncodeResult encoded;
    switch (format) {
    case GraphExportFormat::Svg:
        encoded = encodeSvg(source, outputSize, sourceRect, options);
        break;
    case GraphExportFormat::Pdf:
        encoded = encodePdf(source, outputSize, sourceRect, options);
        break;
    case GraphExportFormat::Png:
        encoded = encodePng(source, outputSize, sourceRect, options);
        break;
    case GraphExportFormat::Automatic:
        break;
    }
    if (!encoded.success) {
        return failureResult(
            outputPath,
            format,
            encoded.failure,
            encoded.reason,
            outputSize,
            sourceRect);
    }
    return writeAtomically(
        outputPath,
        format,
        outputSize,
        sourceRect,
        encoded.bytes);
}

QString GraphExportService::formatName(GraphExportFormat format)
{
    switch (format) {
    case GraphExportFormat::Automatic:
        return QStringLiteral("Automatic");
    case GraphExportFormat::Svg:
        return QStringLiteral("SVG");
    case GraphExportFormat::Pdf:
        return QStringLiteral("PDF");
    case GraphExportFormat::Png:
        return QStringLiteral("PNG");
    }
    return QStringLiteral("Unknown");
}

QString GraphExportService::failureName(GraphExportFailure failure)
{
    switch (failure) {
    case GraphExportFailure::None:
        return QStringLiteral("None");
    case GraphExportFailure::InvalidSource:
        return QStringLiteral("InvalidSource");
    case GraphExportFailure::EmptySource:
        return QStringLiteral("EmptySource");
    case GraphExportFailure::InvalidOptions:
        return QStringLiteral("InvalidOptions");
    case GraphExportFailure::UnsupportedFormat:
        return QStringLiteral("UnsupportedFormat");
    case GraphExportFailure::RenderFailed:
        return QStringLiteral("RenderFailed");
    case GraphExportFailure::EncodeFailed:
        return QStringLiteral("EncodeFailed");
    case GraphExportFailure::OutputOpenFailed:
        return QStringLiteral("OutputOpenFailed");
    case GraphExportFailure::OutputWriteFailed:
        return QStringLiteral("OutputWriteFailed");
    case GraphExportFailure::OutputCommitFailed:
        return QStringLiteral("OutputCommitFailed");
    }
    return QStringLiteral("Unknown");
}
