#include <insightgraph/render.h>
#include <QGuiApplication>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>
#include <cstdio>

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) return 1;
    auto source = insightgraph::RenderSource::fromRenderer([] { return QRectF(0,0,160,80); },
        [] { return true; }, [](QPainter& painter, const QRectF& target, const QRectF&, QString*) {
            painter.fillRect(target, QColor(25,55,120));return true;
        });
    for (const auto& extension : {"png", "svg", "pdf"}) {
        const auto result = insightgraph::ExportService::exportSource(source, directory.filePath(QString("graph.")+extension));
        if (!result.success || result.bytesWritten <= 0 || result.outputPixelSize != QSize(368,208)) return 2;
    }
    const QString path = directory.filePath("keep.png");
    { QFile output(path);if(!output.open(QIODevice::WriteOnly))return 3;output.write("sentinel"); }
    const auto rejected = insightgraph::RenderSource::fromRenderer([] { return QRectF(0,0,160,80); },
        [] { return true; }, [](QPainter&,const QRectF&,const QRectF&,QString* reason) {
            *reason="injected render failure";return false;
        });
    const auto failure = insightgraph::ExportService::exportSource(rejected,path);
    QFile preserved(path);if(!preserved.open(QIODevice::ReadOnly))return 4;
    if(failure.failure!=GraphExportFailure::RenderFailed || preserved.readAll()!="sentinel")return 5;
    std::puts("PASS: QtGui offscreen callback-only PNG/SVG/PDF and atomic failure preservation; no QtWidgets");
    return 0;
}
