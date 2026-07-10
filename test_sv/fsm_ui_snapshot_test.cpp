#include "fsmgraphservice.h"
#include "fsmgraphlayout.h"
#include "projectmodel.h"
#include "rtlinsightspanelcoordinator.h"
#include "semanticindex.h"
#include "semanticindexsnapshot.h"
#include "slangmanager.h"
#include "statetransitiongraphservice.h"

#include <QApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QSet>
#include <QString>

#include <cstdio>
#include <memory>

namespace {

int g_failures = 0;

QString normalizedPath(const QString& path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString loadTextFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(file.readAll());
}

void expect(bool condition, const char* message)
{
    printf("[%s] %s\n", condition ? "PASS" : "FAIL", message);
    if (!condition)
        ++g_failures;
}

bool imageLooksNonBlank(const QImage& image)
{
    if (image.width() < 400 || image.height() < 300)
        return false;
    QSet<QRgb> colors;
    int visiblePixels = 0;
    const int stepX = qMax(1, image.width() / 160);
    const int stepY = qMax(1, image.height() / 120);
    for (int y = 0; y < image.height(); y += stepY) {
        for (int x = 0; x < image.width(); x += stepX) {
            const QColor color = image.pixelColor(x, y);
            if (color.alpha() > 8)
                ++visiblePixels;
            colors.insert(color.rgba());
            if (colors.size() > 32 && visiblePixels > 500)
                return true;
        }
    }
    return colors.size() > 32 && visiblePixels > 500;
}

bool saveSceneCrop(RtlInsightsPanelCoordinator& panel,
                   const QString& outputPath)
{
    QGraphicsView* view = panel.graphView();
    if (!view || !view->scene())
        return false;
    QRectF crop = panel.graphLastFitRectForTest();
    if (!crop.isValid() || crop.isEmpty())
        crop = view->scene()->itemsBoundingRect();
    crop = crop.adjusted(-120.0, -120.0, 120.0, 120.0);

    const qreal scale = 2.0;
    QSize imageSize(qCeil(crop.width() * scale),
                    qCeil(crop.height() * scale));
    imageSize.setWidth(qBound(1200, imageSize.width(), 4200));
    imageSize.setHeight(qBound(900, imageSize.height(), 4200));

    QImage image(imageSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(248, 250, 252));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    view->scene()->render(&painter,
                          QRectF(QPointF(0, 0), QSizeF(imageSize)),
                          crop,
                          Qt::KeepAspectRatio);
    painter.end();

    QDir().mkpath(QFileInfo(outputPath).absolutePath());
    const bool saved = image.save(outputPath, "PNG");
    printf("snapshot %s %dx%d\n",
           QFileInfo(outputPath).absoluteFilePath().toLocal8Bit().constData(),
           image.width(),
           image.height());
    expect(saved && imageLooksNonBlank(image), "snapshot png exists and is nonblank");
    return saved;
}

bool renderSignalSnapshot(SemanticIndex& index,
                          const QString& fileName,
                          const QString& moduleName,
                          const QString& signalName,
                          const QString& outputPath,
                          const QString& selectedFrom = {},
                          const QString& selectedTo = {})
{
    FsmGraphService::getInstance()->setSemanticIndex(&index);
    StateTransitionGraphService::getInstance()->setSemanticIndex(&index);
    QWidget host;
    RtlInsightsPanelCoordinator panel(&host);
    panel.showStateTransitionGraphForSignal(fileName,
                                            moduleName,
                                            signalName);
    QCoreApplication::processEvents();
    expect(panel.graphNodeItemCountForTest() > 0, "snapshot graph has nodes");
    expect(panel.graphEdgeItemCountForTest() > 0, "snapshot graph has edges");
    if (!selectedFrom.isEmpty()) {
        expect(panel.selectGraphItemForTest(QStringLiteral("transition"),
                                            selectedFrom,
                                            selectedTo),
               "selected transition found");
        QCoreApplication::processEvents();
    }
    return saveSceneCrop(panel, outputPath);
}

QString graphVisualSummary(const RtlInsightsPanelCoordinator& panel,
                           const QString& kind,
                           const QString& primary,
                           const QString& secondary)
{
    const QString prefix = QStringLiteral("%1|%2|%3|")
                               .arg(kind, primary, secondary);
    for (const QString& summary : panel.graphElementVisualSummariesForTest()) {
        if (summary.startsWith(prefix))
            return summary;
    }
    return {};
}

bool tooltipContainsAnyCondition(const QString& tooltip,
                                 const QStringList& conditions)
{
    for (const QString& condition : conditions) {
        if (!condition.isEmpty() && tooltip.contains(condition.simplified()))
            return true;
    }
    return false;
}

bool renderHoverSnapshot(SemanticIndex& index,
                         const QString& fileName,
                         const QString& moduleName,
                         const QString& signalName,
                         const QString& outputPath,
                         const QString& elementKind,
                         const QString& primary,
                         const QString& secondary,
                         int expectedStateItems,
                         int expectedEdgeItems,
                         const QStringList& expectedConditions = {})
{
    FsmGraphService::getInstance()->setSemanticIndex(&index);
    StateTransitionGraphService::getInstance()->setSemanticIndex(&index);
    QWidget host;
    RtlInsightsPanelCoordinator panel(&host);
    panel.showStateTransitionGraphForSignal(fileName,
                                            moduleName,
                                            signalName);
    QCoreApplication::processEvents();
    expect(panel.setGraphItemHoveredForTest(elementKind,
                                            primary,
                                            secondary,
                                            true),
           "hover target found");
    QCoreApplication::processEvents();
    const QStringList hovered = panel.graphHoveredElementSummariesForTest();
    int stateItems = 0;
    int edgeItems = 0;
    for (const QString& summary : hovered) {
        stateItems += summary.startsWith(QStringLiteral("state|"));
        edgeItems += summary.startsWith(QStringLiteral("transition|"));
    }
    expect(stateItems == expectedStateItems,
           "hover state set matches canonical and aliases");
    expect(edgeItems == expectedEdgeItems,
           "hover edge set matches related transitions");

    if (elementKind == QStringLiteral("transition")) {
        const QString tooltip = panel.graphItemToolTipForTest(elementKind,
                                                               primary,
                                                               secondary);
        printf("hover_tooltip %s\n",
               tooltip.toLocal8Bit().constData());
        expect(tooltip.startsWith(QLatin1Char('C'))
                   && tooltip.contains(QStringLiteral(": ")),
               "hover edge tooltip uses C-number prefix");
        expect(tooltipContainsAnyCondition(tooltip, expectedConditions),
               "hover edge tooltip contains report condition");
    }

    const bool saved = saveSceneCrop(panel, outputPath);
    expect(panel.setGraphItemHoveredForTest(elementKind,
                                            primary,
                                            secondary,
                                            false),
           "hover target cleared");
    QCoreApplication::processEvents();
    expect(panel.graphHoveredElementSummariesForTest().isEmpty(),
           "hover leave restores all graph items");
    return saved;
}

void verifyHoverDoesNotChangeSelection(SemanticIndex& index,
                                       const QString& fileName,
                                       const QString& moduleName,
                                       const QString& signalName,
                                       const QString& hoverState,
                                       const QString& selectedFrom,
                                       const QString& selectedTo)
{
    FsmGraphService::getInstance()->setSemanticIndex(&index);
    StateTransitionGraphService::getInstance()->setSemanticIndex(&index);
    QWidget host;
    RtlInsightsPanelCoordinator panel(&host);
    panel.showStateTransitionGraphForSignal(fileName,
                                            moduleName,
                                            signalName);
    QCoreApplication::processEvents();
    expect(panel.selectGraphItemForTest(QStringLiteral("transition"),
                                        selectedFrom,
                                        selectedTo),
           "selection isolation transition found");
    const int selectedBefore = panel.graphSelectedItemCountForTest();
    const QString visualBefore = graphVisualSummary(panel,
                                                     QStringLiteral("transition"),
                                                     selectedFrom,
                                                     selectedTo);
    expect(panel.setGraphItemHoveredForTest(QStringLiteral("state"),
                                            hoverState,
                                            QString(),
                                            true),
           "selection isolation hover state found");
    QCoreApplication::processEvents();
    expect(panel.graphSelectedItemCountForTest() == selectedBefore,
           "hover does not alter selected item count");
    expect(graphVisualSummary(panel,
                              QStringLiteral("transition"),
                              selectedFrom,
                              selectedTo)
               == visualBefore,
           "selected transition visual has priority over hover");
    panel.setGraphItemHoveredForTest(QStringLiteral("state"),
                                     hoverState,
                                     QString(),
                                     false);
    QCoreApplication::processEvents();
    expect(panel.graphSelectedItemCountForTest() == selectedBefore,
           "hover leave preserves selection");
}

FsmLayoutOptions legacyFsmLayoutOptions()
{
    FsmLayoutOptions options;
    options.minNodeWidth = 252.0;
    options.maxNodeWidth = 252.0;
    options.nodeHorizontalPadding = 0.0;
    options.nodeHeight = 62.0;
    options.aliasNodeHeight = 46.0;
    options.layerSpacing = 152.0;
    options.nodeSpacing = 96.0;
    options.parallelEdgeSpacing = 34.0;
    options.gridStep = 48.0;
    options.orthogonalStraightThreshold = 18.0;
    options.labelEdgeDistance = 30.0;
    options.labelSlideStep = 18.0;
    options.labelClearance = 10.0;
    options.sceneMargin = 64.0;
    options.selfLoopHeight = 42.0;
    options.fallbackAliasPercentLimit = 0;
    return options;
}

qreal rectArea(const QRectF& rect)
{
    return qMax<qreal>(0.0, rect.width())
        * qMax<qreal>(0.0, rect.height());
}

void printLayoutMetric(const QString& label, const FsmGraph& graph)
{
    const FsmGraphLayout oldLayout =
        layoutFsmGraph(graph, legacyFsmLayoutOptions());
    const FsmGraphLayout newLayout = layoutFsmGraph(graph, FsmLayoutOptions{});
    const qreal oldArea = rectArea(oldLayout.sceneBounds);
    const qreal newArea = rectArea(newLayout.sceneBounds);
    const qreal ratio = oldArea > 0.0 ? newArea / oldArea : 0.0;
    printf("layout_metric %s crossings=%d oldArea=%.0f newArea=%.0f ratio=%.3f fallbackAliases=%d warnings=%s\n",
           label.toLocal8Bit().constData(),
           newLayout.renderedCrossingCount,
           oldArea,
           newArea,
           ratio,
           newLayout.fallbackAliasCount,
           newLayout.warnings.join(QLatin1Char('|')).toLocal8Bit().constData());
}

bool printLayoutMetricForSignal(const FsmGraphReport& report,
                                const QString& signalName,
                                const QString& label)
{
    for (const FsmGraph& graph : report.graphs) {
        if (graph.nextStateSignalDisplayName == signalName) {
            printLayoutMetric(label, graph);
            return true;
        }
    }
    return false;
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    const QString sourceRoot = argc > 1
        ? normalizedPath(QString::fromLocal8Bit(argv[1]))
        : normalizedPath(QFileInfo(QString::fromLocal8Bit(__FILE__))
                             .dir()
                             .filePath(QStringLiteral("..")));
    const QString workspaceRoot =
        normalizedPath(QDir(sourceRoot).filePath(QStringLiteral("test_sv/new")));
    const QString chlCtrlPath = normalizedPath(
        QDir(workspaceRoot).filePath(
            QStringLiteral("elec_phy_import/ctrl/chl_ctrl.sv")));
    const QString vendorCtxFsmPath = normalizedPath(
        QDir(sourceRoot).filePath(
            QStringLiteral("test_sv/huge_prj/Edma/vendor_ip_edma_ctx_fsm.sv")));
    expect(QFileInfo(chlCtrlPath).isFile(), "chl_ctrl fixture exists");
    expect(QFileInfo(vendorCtxFsmPath).isFile(), "VENDOR ctx fsm fixture exists");
    if (!QFileInfo(chlCtrlPath).isFile() || !QFileInfo(vendorCtxFsmPath).isFile())
        return 1;

    QStringList files;
    QDirIterator it(workspaceRoot,
                    QStringList{"*.sv", "*.svh", "*.v"},
                    QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext())
        files.append(normalizedPath(it.next()));

    ProjectModel project;
    project.setWorkspaceRoot(workspaceRoot);
    project.setScannedFiles(files);
    const ProjectSnapshot snapshot = project.snapshot();

    SlangManager slang;
    QStringList analysisFiles = snapshot.systemVerilogFiles;
    analysisFiles.append(vendorCtxFsmPath);
    analysisFiles.removeDuplicates();
    QStringList includeDirs = snapshot.includeDirs;
    includeDirs.append(normalizedPath(QDir(sourceRoot).filePath(
        QStringLiteral("test_sv/huge_prj"))));
    includeDirs.removeDuplicates();
    QList<SemanticSymbolRecord> records =
        slang.extractWorkspaceSymbolRecords(analysisFiles,
                                            includeDirs,
                                            snapshot.defines);
    int nextLocalHandle = 1;
    for (SemanticSymbolRecord& record : records) {
        if (record.localHandle <= 0)
            record.localHandle = nextLocalHandle;
        if (nextLocalHandle <= record.localHandle)
            nextLocalHandle = record.localHandle + 1;
    }
    QHash<QString, QString> fileContents;
    for (const QString& fileName : analysisFiles)
        fileContents.insert(fileName, loadTextFile(fileName));

    SemanticIndex index;
    index.setSnapshot(std::make_shared<SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolRecords(records,
                                                 {},
                                                 {},
                                                 fileContents)));

    FsmGraphService fsmService(&index);
    FsmGraphQuery query;
    query.moduleName = QStringLiteral("chl_ctrl");
    query.fileName = chlCtrlPath;
    const FsmGraphReport report = fsmService.buildFsmGraph(query);
    expect(report.found, "chl_ctrl fsm graphs found");
    expect(printLayoutMetricForSignal(report,
                                      QStringLiteral("elec_cfg_ns"),
                                      QStringLiteral("elec_cfg_ns")),
           "elec_cfg_ns layout metric found");
    expect(printLayoutMetricForSignal(report,
                                      QStringLiteral("phy_cfg_ns"),
                                      QStringLiteral("phy_cfg_ns")),
           "phy_cfg_ns layout metric found");
    expect(printLayoutMetricForSignal(report,
                                      QStringLiteral("phy_pass_thrg_cfg_ns"),
                                      QStringLiteral("phy_pass_thrg_cfg_ns")),
           "phy_pass_thrg_cfg_ns layout metric found");

    FsmGraphQuery vendorQuery;
    vendorQuery.moduleName = QStringLiteral("vendor_ip_edma_ctx_fsm");
    vendorQuery.fileName = vendorCtxFsmPath;
    const FsmGraphReport vendorReport = fsmService.buildFsmGraph(vendorQuery);
    expect(vendorReport.found, "VENDOR ctx fsm graph found");
    expect(printLayoutMetricForSignal(vendorReport,
                                      QStringLiteral("fsm_state_nxt"),
                                      QStringLiteral("vendor_ip_edma_ctx")),
           "VENDOR ctx layout metric found");

    QString selectedFrom;
    QString selectedTo;
    QStringList selectedConditions;
    const FsmGraph* phyGraph = nullptr;
    for (const FsmGraph& graph : report.graphs) {
        if (graph.nextStateSignalDisplayName != QStringLiteral("phy_cfg_ns"))
            continue;
        phyGraph = &graph;
        for (const FsmTransitionRow& row : graph.transitionRows) {
            if (row.fromStateDisplayName != row.toStateDisplayName) {
                selectedFrom = row.fromStateDisplayName;
                selectedTo = row.toStateDisplayName;
                break;
            }
        }
    }
    expect(!selectedFrom.isEmpty(), "selected transition candidate found");
    if (phyGraph) {
        for (const FsmTransitionRow& row : phyGraph->transitionRows) {
            if (row.fromStateDisplayName == selectedFrom
                && row.toStateDisplayName == selectedTo) {
                selectedConditions.append(row.conditionDisplayName);
            }
        }
    }

    QString hoverState;
    int hoverStateItemCount = 0;
    int hoverEdgeItemCount = 0;
    if (phyGraph) {
        const FsmGraphLayout hoverLayout =
            layoutFsmGraph(*phyGraph, FsmLayoutOptions{});
        int hoverCanonicalId = -1;
        for (const FsmLayoutNode& node : hoverLayout.nodes) {
            if (node.alias) {
                hoverCanonicalId = node.canonicalNodeId;
                break;
            }
        }
        QHash<int, int> canonicalByNodeId;
        for (const FsmLayoutNode& node : hoverLayout.nodes) {
            canonicalByNodeId.insert(node.nodeId, node.canonicalNodeId);
            if (node.canonicalNodeId != hoverCanonicalId)
                continue;
            ++hoverStateItemCount;
            if (!node.alias)
                hoverState = node.displayName;
        }
        for (const FsmLayoutEdge& edge : hoverLayout.edges) {
            if (canonicalByNodeId.value(edge.fromNodeId, -1)
                    == hoverCanonicalId
                || canonicalByNodeId.value(edge.toNodeId, -1)
                       == hoverCanonicalId) {
                ++hoverEdgeItemCount;
            }
        }
    }
    expect(!hoverState.isEmpty() && hoverStateItemCount >= 2,
           "hover fixture has canonical state and aliases");
    expect(hoverEdgeItemCount > 0,
           "hover fixture canonical state has related transitions");

    const QString outputDir =
        normalizedPath(QDir(sourceRoot).filePath(QStringLiteral("artifacts/ui/fsm")));
    renderSignalSnapshot(index,
                         chlCtrlPath,
                         QStringLiteral("chl_ctrl"),
                         QStringLiteral("elec_cfg_ns"),
                         QDir(outputDir).filePath(QStringLiteral("fsm_elec_cfg_ns.png")));
    renderSignalSnapshot(index,
                         chlCtrlPath,
                         QStringLiteral("chl_ctrl"),
                         QStringLiteral("phy_cfg_ns"),
                         QDir(outputDir).filePath(QStringLiteral("fsm_phy_cfg_ns.png")));
    renderSignalSnapshot(index,
                         chlCtrlPath,
                         QStringLiteral("chl_ctrl"),
                         QStringLiteral("phy_pass_thrg_cfg_ns"),
                         QDir(outputDir).filePath(
                             QStringLiteral("fsm_phy_pass_thrg_cfg_ns.png")));
    renderSignalSnapshot(index,
                         chlCtrlPath,
                         QStringLiteral("chl_ctrl"),
                         QStringLiteral("phy_cfg_ns"),
                         QDir(outputDir).filePath(
                             QStringLiteral("fsm_selected_transition.png")),
                         selectedFrom,
                         selectedTo);
    if (!hoverState.isEmpty()) {
        verifyHoverDoesNotChangeSelection(index,
                                          chlCtrlPath,
                                          QStringLiteral("chl_ctrl"),
                                          QStringLiteral("phy_cfg_ns"),
                                          hoverState,
                                          selectedFrom,
                                          selectedTo);
        renderHoverSnapshot(
            index,
            chlCtrlPath,
            QStringLiteral("chl_ctrl"),
            QStringLiteral("phy_cfg_ns"),
            QDir(outputDir).filePath(QStringLiteral("fsm_hover_state.png")),
            QStringLiteral("state"),
            hoverState,
            QString(),
            hoverStateItemCount,
            hoverEdgeItemCount);
    }
    renderHoverSnapshot(
        index,
        chlCtrlPath,
        QStringLiteral("chl_ctrl"),
        QStringLiteral("phy_cfg_ns"),
        QDir(outputDir).filePath(
            QStringLiteral("fsm_hover_edge_tooltip.png")),
        QStringLiteral("transition"),
        selectedFrom,
        selectedTo,
        0,
        1,
        selectedConditions);
    renderSignalSnapshot(index,
                         vendorCtxFsmPath,
                         QStringLiteral("vendor_ip_edma_ctx_fsm"),
                         QStringLiteral("fsm_state_nxt"),
                         QDir(outputDir).filePath(
                             QStringLiteral("fsm_vendor_ip_edma_ctx.png")));

    FsmGraphService::getInstance()->setSemanticIndex(SemanticIndex::getInstance());
    StateTransitionGraphService::getInstance()->setSemanticIndex(
        SemanticIndex::getInstance());
    return g_failures == 0 ? 0 : 1;
}
