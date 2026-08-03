#ifndef GRAPHEXPORTSERVICE_H
#define GRAPHEXPORTSERVICE_H

#include <QColor>
#include <QMargins>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QtGlobal>

#include <functional>

class QGraphicsScene;
class QPainter;
class QWidget;

enum class GraphExportFormat {
    Automatic,
    Svg,
    Pdf,
    Png
};

enum class GraphExportFailure {
    None,
    InvalidSource,
    EmptySource,
    InvalidOptions,
    UnsupportedFormat,
    RenderFailed,
    EncodeFailed,
    OutputOpenFailed,
    OutputWriteFailed,
    OutputCommitFailed
};

struct GraphExportOptions {
    // The complete output canvas. When empty, the source's logical size is
    // scaled from 96 DPI to dpi. Margins are always measured in output pixels.
    QSize targetPixelSize;
    qreal dpi = 192.0;
    QColor backgroundColor = QColor(Qt::white);
    QMargins marginPixels = QMargins(24, 24, 24, 24);
    GraphExportFormat format = GraphExportFormat::Automatic;
    bool antialiasing = true;
    bool textAntialiasing = true;
    bool smoothPixmapTransform = true;
};

struct GraphExportResult {
    bool success = false;
    GraphExportFormat format = GraphExportFormat::Automatic;
    GraphExportFailure failure = GraphExportFailure::None;
    QString failureReason;
    QString outputPath;
    QSize outputPixelSize;
    QRectF sourceRect;
    qint64 bytesWritten = 0;

    explicit operator bool() const { return success; }
};

// A non-owning adapter around a renderable graph surface. The source and any
// objects captured by a custom renderer must outlive exportSource().
class GraphExportSource
{
public:
    using BoundsProvider = std::function<QRectF()>;
    using ContentPredicate = std::function<bool()>;
    using RenderCallback = std::function<bool(
        QPainter&,
        const QRectF& targetRect,
        const QRectF& sourceRect,
        QString* failureReason)>;

    GraphExportSource() = default;

    static GraphExportSource fromGraphicsScene(QGraphicsScene* scene);
    static GraphExportSource fromWidget(QWidget* widget);
    static GraphExportSource fromRenderer(
        BoundsProvider boundsProvider,
        ContentPredicate contentPredicate,
        RenderCallback renderer);

    bool isValid() const;
    QRectF contentBounds() const;
    bool hasContent() const;
    bool render(QPainter& painter,
                const QRectF& targetRect,
                const QRectF& sourceRect,
                QString* failureReason) const;

private:
    BoundsProvider boundsProvider;
    ContentPredicate contentPredicate;
    RenderCallback renderer;

};

class GraphExportService
{
public:
    static GraphExportResult exportGraphicsScene(
        QGraphicsScene* scene,
        const QString& outputPath,
        const GraphExportOptions& options = {});

    static GraphExportResult exportWidget(
        QWidget* widget,
        const QString& outputPath,
        const GraphExportOptions& options = {});

    static GraphExportResult exportSource(
        const GraphExportSource& source,
        const QString& outputPath,
        const GraphExportOptions& options = {});

    static QString formatName(GraphExportFormat format);
    static QString failureName(GraphExportFailure failure);
};

#endif // GRAPHEXPORTSERVICE_H
