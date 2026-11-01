#pragma once
#include <insightgraph/render.h>
#include <utility>
class QGraphicsScene;
class QWidget;

class GraphExportSource : public insightgraph::RenderSource
{
public:
    GraphExportSource() = default;
    static GraphExportSource fromGraphicsScene(QGraphicsScene* scene);
    static GraphExportSource fromWidget(QWidget* widget);
    static GraphExportSource fromRenderer(BoundsProvider bounds,
        ContentPredicate content, RenderCallback renderer)
    {
        return GraphExportSource(insightgraph::RenderSource::fromRenderer(
            std::move(bounds), std::move(content), std::move(renderer)));
    }
private:
    explicit GraphExportSource(insightgraph::RenderSource source)
        : insightgraph::RenderSource(std::move(source)) {}
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
