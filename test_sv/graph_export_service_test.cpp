#include "graphexportservice.h"

#include <QApplication>
#include <QBrush>
#include <QByteArray>
#include <QFile>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QPen>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>

namespace {

int checks = 0;
int failures = 0;

void expect(const char* name, bool value)
{
    ++checks;
    if (!value)
        ++failures;
    std::printf("[%s] %s\n", value ? "PASS" : "FAIL", name);
}

QByteArray readAll(const QString& fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

bool writeAll(const QString& fileName, const QByteArray& bytes)
{
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(bytes) == bytes.size();
}

void populateScene(QGraphicsScene& scene)
{
    auto* rect = scene.addRect(
        QRectF(18.0, 24.0, 210.0, 96.0),
        QPen(QColor(QStringLiteral("#194f90")), 3.0),
        QBrush(QColor(QStringLiteral("#d6e9ff"))));
    rect->setData(0, QStringLiteral("export-content"));
    auto* text = scene.addSimpleText(
        QStringLiteral("ZeroSlack export marker"));
    text->setBrush(QColor(QStringLiteral("#102840")));
    text->setPos(42.0, 58.0);
}

}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QApplication app(argc, argv);

    QTemporaryDir temporary;
    expect("temporary export directory is available", temporary.isValid());
    if (!temporary.isValid())
        return 1;

    QGraphicsScene scene;
    populateScene(scene);
    GraphExportOptions options;
    options.targetPixelSize = QSize(1600, 900);
    options.dpi = 300.0;
    options.marginPixels = QMargins(80, 70, 80, 70);
    options.backgroundColor = QColor(QStringLiteral("#f3f6f8"));

    const QString pngPath =
        temporary.filePath(QStringLiteral("graph.png"));
    const GraphExportResult png =
        GraphExportService::exportGraphicsScene(
            &scene, pngPath, options);
    const QByteArray pngBytes = readAll(pngPath);
    expect("PNG export succeeds with explicit result metadata",
           png.success && png.failure == GraphExportFailure::None
               && png.format == GraphExportFormat::Png
               && png.outputPixelSize == QSize(1600, 900)
               && png.sourceRect.contains(QPointF(42.0, 58.0))
               && png.bytesWritten == pngBytes.size()
               && png.failureReason.isEmpty());
    expect("PNG output has the PNG signature",
           pngBytes.size() > 8
               && pngBytes.left(8)
                      == QByteArray::fromHex("89504e470d0a1a0a"));

    QImageReader pngReader(pngPath);
    const QSize pngSize = pngReader.size();
    const QImage pngImage = pngReader.read();
    const int expectedDotsPerMeter = qRound(300.0 / 0.0254);
    expect("PNG output preserves requested high-resolution dimensions",
           pngSize == QSize(1600, 900)
               && pngImage.size() == QSize(1600, 900));
    expect("PNG output records the requested DPI",
           std::abs(
               pngImage.dotsPerMeterX() - expectedDotsPerMeter) <= 1
               && std::abs(
                      pngImage.dotsPerMeterY() - expectedDotsPerMeter) <= 1);
    expect("PNG background option fills the margin canvas",
           pngImage.pixelColor(0, 0)
               == options.backgroundColor);

    GraphExportOptions derivedSizeOptions;
    derivedSizeOptions.dpi = 96.0;
    derivedSizeOptions.marginPixels = QMargins(10, 10, 10, 10);
    const QString derivedSizePath =
        temporary.filePath(QStringLiteral("graph-derived.png"));
    const GraphExportResult derivedSize =
        GraphExportService::exportGraphicsScene(
            &scene, derivedSizePath, derivedSizeOptions);
    expect("empty targetPixelSize derives a valid canvas from full content",
           derivedSize.success
               && derivedSize.outputPixelSize.width()
                      >= std::ceil(derivedSize.sourceRect.width()) + 20
               && derivedSize.outputPixelSize.height()
                      >= std::ceil(derivedSize.sourceRect.height()) + 20);

    const QString svgPath =
        temporary.filePath(QStringLiteral("graph.svg"));
    const GraphExportResult svg =
        GraphExportService::exportGraphicsScene(
            &scene, svgPath, options);
    const QByteArray svgBytes = readAll(svgPath);
    expect("SVG export succeeds and has an SVG signature",
           svg.success && svg.format == GraphExportFormat::Svg
               && svgBytes.contains("<svg")
               && (svgBytes.startsWith("<?xml")
                   || svgBytes.startsWith("<svg")));
    expect("SVG output declares the requested full-canvas viewBox",
           svgBytes.contains("viewBox=\"0 0 1600 900\""));
    expect("SVG output contains rendered scene content",
           svgBytes.contains("ZeroSlack export marker")
               && (svgBytes.contains("<rect")
                   || svgBytes.contains("<path")));

    const QString pdfPath =
        temporary.filePath(QStringLiteral("graph.pdf"));
    const GraphExportResult pdf =
        GraphExportService::exportGraphicsScene(
            &scene, pdfPath, options);
    const QByteArray pdfBytes = readAll(pdfPath);
    expect("PDF export succeeds with a non-empty PDF document",
           pdf.success && pdf.format == GraphExportFormat::Pdf
               && pdfBytes.size() > 256
               && pdfBytes.startsWith("%PDF-")
               && pdfBytes.contains("%%EOF"));

    QGraphicsScene emptyScene;
    const QString emptyPath =
        temporary.filePath(QStringLiteral("empty.svg"));
    const GraphExportResult empty =
        GraphExportService::exportGraphicsScene(
            &emptyScene, emptyPath, options);
    expect("empty scene fails with an explicit reason and no output",
           !empty.success
               && empty.failure == GraphExportFailure::EmptySource
               && !empty.failureReason.isEmpty()
               && !QFile::exists(emptyPath));
    const GraphExportResult nullScene =
        GraphExportService::exportGraphicsScene(
            nullptr,
            temporary.filePath(QStringLiteral("null.svg")),
            options);
    expect("null scene fails as an invalid structured source",
           !nullScene.success
               && nullScene.failure
                      == GraphExportFailure::InvalidSource);

    const QString invalidPath =
        temporary.filePath(QStringLiteral("missing/graph.svg"));
    const GraphExportResult invalidOutput =
        GraphExportService::exportGraphicsScene(
            &scene, invalidPath, options);
    expect("invalid output path fails without leaving a partial file",
           !invalidOutput.success
               && invalidOutput.failure
                      == GraphExportFailure::OutputOpenFailed
               && !invalidOutput.failureReason.isEmpty()
               && !QFile::exists(invalidPath));

    const QString protectedPath =
        temporary.filePath(QStringLiteral("protected.svg"));
    const QByteArray originalBytes("pre-existing-export");
    expect("original export fixture is written",
           writeAll(protectedPath, originalBytes));
    const GraphExportSource failingSource =
        GraphExportSource::fromRenderer(
            []() { return QRectF(0.0, 0.0, 120.0, 80.0); },
            []() { return true; },
            [](QPainter&,
               const QRectF&,
               const QRectF&,
               QString* failureReason) {
                if (failureReason) {
                    *failureReason =
                        QStringLiteral("controlled renderer failure");
                }
                return false;
            });
    const GraphExportResult protectedResult =
        GraphExportService::exportSource(
            failingSource, protectedPath, options);
    expect("render failure reports its structured failure reason",
           !protectedResult.success
               && protectedResult.failure
                      == GraphExportFailure::RenderFailed
               && protectedResult.failureReason
                      == QStringLiteral("controlled renderer failure"));
    expect("render failure preserves an existing destination byte-for-byte",
           readAll(protectedPath) == originalBytes);

    GraphExportOptions invalidOptions = options;
    invalidOptions.marginPixels = QMargins(900, 0, 900, 0);
    const GraphExportResult invalidOptionsResult =
        GraphExportService::exportGraphicsScene(
            &scene, protectedPath, invalidOptions);
    expect("invalid canvas options fail before touching the destination",
           !invalidOptionsResult.success
               && invalidOptionsResult.failure
                      == GraphExportFailure::InvalidOptions
               && !invalidOptionsResult.failureReason.isEmpty()
               && readAll(protectedPath) == originalBytes);

    const QString unknownPath =
        temporary.filePath(QStringLiteral("graph.unknown"));
    const GraphExportResult unknown =
        GraphExportService::exportGraphicsScene(
            &scene, unknownPath, options);
    expect("automatic format rejects an unsupported suffix",
           !unknown.success
               && unknown.failure
                      == GraphExportFailure::UnsupportedFormat
               && GraphExportService::failureName(unknown.failure)
                      == QStringLiteral("UnsupportedFormat")
               && !QFile::exists(unknownPath));

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
