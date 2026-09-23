#include "contextworkspacecontroller.h"
#include "contextfloatingwindow.h"
#include "contextdockhost.h"
#include "testuistyle.h"
#include "contextrail.h"
#include "insightcanvas.h"
#include "insightgraphcore.h"
#include "rtlinsightsgraphconstants.h"
#include "actionregistry.h"
#include "liveinsightscontextprovider.h"
#include "liveinsightscontextview.h"
#include "liveinsightsession.h"
#include "liveinsighttoolpage.h"
#include "rtlinsightspanelcoordinator.h"
#include "rtlinsightworkbench.h"
#include "signalkernelgraphpanelcoordinator.h"
#include "signalusagehotspotpanel.h"
#include "applicationthememanager.h"
#include "insightvisualstyle.h"
#include "semanticindex.h"
#include "semanticindexsnapshot.h"
#include "slangmanager.h"
#include "moduleblockdiagramservice.h"
#include "symbolrelationshipengine.h"
#include "semantic_fixture_records.h"
#include "workspacechrome.h"
#include "mainwindow.h"
#include <QSettings>
#include <QSignalSpy>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QFileInfo>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsRectItem>
#include <QDockWidget>
#include <QLineEdit>
#include <QMenu>
#include <QAbstractItemView>
#include <QIcon>
#include <QImage>
#include <QMainWindow>
#include <QPushButton>
#include <QSet>
#include <QScopeGuard>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

namespace {
InsightGraphNode makeNode(
    const QString& id,
    const QString& name,
    InsightGraphDomain domain = InsightGraphDomain::Symbol)
{
    InsightGraphNode result;
    result.nodeId = id;
    result.displayName = name;
    result.domain = domain;
    return result;
}

InsightGraphDraft draftFor(const QString& identity,
                           quint64 revision = 1)
{
    InsightGraphDraft draft;
    draft.workspaceId = QStringLiteral("workspace");
    draft.documentId = QStringLiteral("rtl/top.sv");
    draft.documentRevision = revision;
    draft.semanticRevision = revision;
    draft.contextKey = identity;
    draft.layoutHint = QStringLiteral("block");
    InsightGraphNode root = makeNode(
        QStringLiteral("sym:%1-root").arg(identity), identity);
    root.attributes.insert(QStringLiteral("depth"), 0);
    draft.nodes.append(root);
    return draft;
}

SignalUsageHotspotReport hotspotGraphViewFixture()
{
    SignalUsageHotspotItem item;
    item.role = SignalUsageHotspotRole::Read;
    item.moduleName = QStringLiteral("graph_view_module");
    item.fileName = QStringLiteral("graph_view.sv");
    item.line = 12;
    item.column = 5;
    item.endLine = 12;
    item.endColumn = 16;
    item.snippet = QStringLiteral("sample <= tracked_signal;");
    item.roleReasonDisplayName = QStringLiteral("read");

    SignalUsageHotspotTrackPosition position;
    position.itemIndex = 0;
    position.role = item.role;
    position.roleDisplayName = item.roleReasonDisplayName;
    position.line = item.line;
    position.column = item.column;
    position.endLine = item.endLine;
    position.endColumn = item.endColumn;

    SignalUsageHotspotTrackLane lane;
    lane.moduleName = item.moduleName;
    lane.fileName = item.fileName;
    lane.startLine = 1;
    lane.endLine = 40;
    lane.count = 1;
    lane.positions = {position};

    SignalUsageHotspotReport report;
    report.found = true;
    report.declarationDisplayName =
        QStringLiteral("tracked_signal");
    report.items = {item};
    report.trackLanes = {lane};
    return report;
}

SignalKernelGraphReport signalKernelThemeFixture()
{
    SignalKernelGraphReport report;
    report.found = true;
    report.kernel.id = 0;
    report.kernel.role = SignalKernelGraphNodeRole::Kernel;
    report.kernel.displayName = QStringLiteral("tracked_signal");
    report.kernel.moduleDisplayName = QStringLiteral("graph_view_module");

    SignalKernelGraphNode input;
    input.id = 1;
    input.role = SignalKernelGraphNodeRole::Input;
    input.inputLane = SignalKernelGraphInputLane::Data;
    input.displayName = QStringLiteral("source_a");
    input.moduleDisplayName = QStringLiteral("source_module");
    report.inputs = {input};

    SignalKernelGraphNode outputA;
    outputA.id = 2;
    outputA.role = SignalKernelGraphNodeRole::Output;
    outputA.displayName = QStringLiteral("sink_a");
    outputA.moduleDisplayName = QStringLiteral("sink_module");
    SignalKernelGraphNode outputB = outputA;
    outputB.id = 3;
    outputB.displayName = QStringLiteral("sink_b");
    report.outputs = {outputA, outputB};
    report.edges = {{1, 0, QStringLiteral("read")},
                    {0, 2, QStringLiteral("write")},
                    {0, 3, QStringLiteral("write")}};

    SignalKernelGraphFanoutGroup group;
    group.id = 7;
    group.role = SignalKernelGraphNodeRole::Output;
    group.groupKey = QStringLiteral("output:sink_module");
    group.displayName = QStringLiteral("sink_module outputs");
    group.moduleName = QStringLiteral("sink_module");
    group.nodeIds = {2, 3};
    group.nodeCount = 2;
    group.totalRoleNodeCount = 2;
    group.highFanout = true;
    report.outputFanoutGroups = {group};
    return report;
}


QImage iconImage(const QIcon& icon)
{
    return icon.pixmap(QSize(32, 32), QIcon::Normal, QIcon::Off).toImage();
}
}

class RtlInsightWorkbenchTest final : public QObject
{
    Q_OBJECT

private slots:
    void stableSymbolIdIgnoresSourceOffsets();
    void graphCorePublishesIncrementalDiffs();
    void canvasPreservesStableNodePositions();
    void workbenchRoutesFourPluginsIndependently();
    void workbenchPreservesPerViewState();
    void toolPagesRouteAllFourWorkbenchViews();
    void specializedModesSurviveRouting();
    void sidebarSectionTeardownReleasesFocusedControls();
    void moduleDiagramAdaptsAndNavigatesInstances();
    void moduleDiagramIncludesDeepDescendantsAndTerminatesCycles();
    void catppuccinPalettes();
    void windowChromeSupportsNativeSnap();
    void realWindowChromeButtons();
    void queuedNativeMouseAllowsNullResult();
    void sidebarRegistersFourDistinctIconsAndResources();
    void toolPageDetachesIntoWindow();
    void sharedCanvasExportsSvgPngAndPdf();
    void legacyBottomDockDoesNotCarryStateWhenDisabled();
};

void RtlInsightWorkbenchTest::stableSymbolIdIgnoresSourceOffsets()
{
    SymbolStableKey before;
    before.fileName = QStringLiteral("RTL\\Control.sv");
    before.ownerScope = QStringLiteral("top.controller");
    before.declarationKind = SymbolTaxonomy::DeclarationKind::Signal;
    before.symbolName = QStringLiteral("state_q");
    before.sourcePosition = 128;
    before.sourceLength = 7;
    SymbolStableKey shifted = before;
    shifted.sourcePosition = 912;
    shifted.sourceLength = 19;

    const QString beforeId = InsightGraphCore::stableSymbolId(before);
    const QString shiftedId = InsightGraphCore::stableSymbolId(shifted);
    QVERIFY(beforeId.startsWith(QStringLiteral("sym:")));
    QCOMPARE(beforeId, shiftedId);
    shifted.symbolName = QStringLiteral("state_d");
    QVERIFY(InsightGraphCore::stableSymbolId(shifted) != beforeId);
}

void RtlInsightWorkbenchTest::graphCorePublishesIncrementalDiffs()
{
    InsightGraphCore core;
    InsightGraphDraft first = draftFor(QStringLiteral("block"), 1);
    first.nodes.append(makeNode(QStringLiteral("sym:child"),
                                QStringLiteral("child"),
                                InsightGraphDomain::Hierarchy));
    InsightGraphFact astFact;
    astFact.domain = InsightGraphDomain::Ast;
    astFact.symbolName = QStringLiteral("always_ff");
    astFact.sourceFile = QStringLiteral("rtl/top.sv");
    astFact.ownerScope = QStringLiteral("top");
    first.facts.append(astFact);
    InsightGraphEdge edge;
    edge.fromNodeId = first.nodes.at(0).nodeId;
    edge.toNodeId = QStringLiteral("sym:child");
    edge.displayName = QStringLiteral("instantiates");
    edge.edgeId = InsightGraphCore::stableEdgeId(
        edge.fromNodeId, edge.toNodeId, edge.displayName);
    first.edges.append(edge);

    const InsightGraphUpdate initial =
        core.update(QStringLiteral("block"), first);
    QCOMPARE(initial.diff.addedNodeIds.size(), 3);
    QCOMPARE(initial.diff.addedEdgeIds.size(), 1);
    QCOMPARE(initial.snapshot.statistics.astCount, 1);
    QCOMPARE(initial.snapshot.statistics.symbolCount, 1);
    QCOMPARE(initial.snapshot.statistics.hierarchyCount, 1);
    QCOMPARE(initial.snapshot.statistics.connectionCount, 1);
    QVERIFY(initial.diff.topologyChanged());

    InsightGraphDraft second = first;
    second.documentRevision = 2;
    second.semanticRevision = 2;
    second.nodes[1].detail = QStringLiteral("updated type");
    second.nodes.append(makeNode(QStringLiteral("sym:new"),
                                 QStringLiteral("new child"),
                                 InsightGraphDomain::Hierarchy));
    const InsightGraphUpdate incremental =
        core.update(QStringLiteral("block"), second);
    QCOMPARE(incremental.snapshot.generation, quint64(2));
    QCOMPARE(incremental.diff.addedNodeIds,
             QStringList{QStringLiteral("sym:new")});
    QCOMPARE(incremental.diff.updatedNodeIds,
             QStringList{QStringLiteral("sym:child")});
    QVERIFY(incremental.diff.removedNodeIds.isEmpty());
}

void RtlInsightWorkbenchTest::canvasPreservesStableNodePositions()
{
    InsightGraphCore core;
    InsightCanvas canvas;
    InsightGraphDraft first = draftFor(QStringLiteral("layout"), 1);
    first.nodes.append(makeNode(QStringLiteral("sym:child"),
                                QStringLiteral("child"),
                                InsightGraphDomain::Hierarchy));
    first.nodes[1].attributes.insert(QStringLiteral("depth"), 1);
    canvas.applyUpdate(core.update(QStringLiteral("block"), first));
    const QPointF stablePosition =
        canvas.nodePositions().value(QStringLiteral("sym:child"));

    InsightGraphDraft second = first;
    second.documentRevision = 2;
    second.nodes.append(makeNode(QStringLiteral("sym:added"),
                                 QStringLiteral("added"),
                                 InsightGraphDomain::Hierarchy));
    second.nodes[2].attributes.insert(QStringLiteral("depth"), 2);
    canvas.applyUpdate(core.update(QStringLiteral("block"), second));
    QCOMPARE(canvas.nodePositions().value(QStringLiteral("sym:child")),
             stablePosition);
    QCOMPARE(canvas.nodeItemCountForTest(), 3);
}

void RtlInsightWorkbenchTest::workbenchRoutesFourPluginsIndependently()
{
    RtlInsightWorkbench workbench;
    QCOMPARE(workbench.pluginCount(), 4);
    const QStringList pluginIds = workbench.pluginIds();
    const QSet<QString> ids(pluginIds.cbegin(), pluginIds.cend());
    QCOMPARE(ids,
             QSet<QString>({QStringLiteral("kernel"),
                            QStringLiteral("block"),
                            QStringLiteral("hotspot"),
                            QStringLiteral("state")}));

    const QList<InsightWorkbenchViewKind> kinds = {
        InsightWorkbenchViewKind::Kernel,
        InsightWorkbenchViewKind::Block,
        InsightWorkbenchViewKind::Hotspot,
        InsightWorkbenchViewKind::StateTransition
    };
    for (InsightWorkbenchViewKind kind : kinds) {
        workbench.setBuildOverrideForTest(
            kind,
            [kind](const InsightViewContext&) {
                InsightViewBuildResult result;
                const QString id = insightWorkbenchViewKindId(kind);
                result.draft = draftFor(id);
                result.draft.layoutHint = id;
                result.summary = id;
                result.available = true;
                return result;
            });
    }
    InsightViewContext context;
    context.workspaceId = QStringLiteral("workspace");
    context.documentId = QStringLiteral("rtl/top.sv");
    context.fileName = context.documentId;
    context.moduleName = QStringLiteral("top");
    context.signalName = QStringLiteral("state_q");
    workbench.setContext(context);

    for (InsightWorkbenchViewKind kind : kinds) {
        QVERIFY(workbench.setViewKind(kind));
        QCOMPARE(workbench.currentPluginId(), insightWorkbenchViewKindId(kind));
        QCOMPARE(workbench.canvas()->snapshot().channelId,
                 insightWorkbenchViewKindId(kind));
        QCOMPARE(workbench.canvas()->nodeItemCountForTest(), 1);
    }
}

void RtlInsightWorkbenchTest::workbenchPreservesPerViewState()
{
    RtlInsightWorkbench workbench;
    for (InsightWorkbenchViewKind kind : {
             InsightWorkbenchViewKind::Kernel,
             InsightWorkbenchViewKind::Block}) {
        workbench.setBuildOverrideForTest(
            kind,
            [kind](const InsightViewContext&) {
                InsightViewBuildResult result;
                const QString id = insightWorkbenchViewKindId(kind);
                result.draft = draftFor(id);
                result.available = true;
                return result;
            });
    }
    InsightViewContext context;
    context.workspaceId = QStringLiteral("workspace");
    context.documentId = QStringLiteral("rtl/top.sv");
    context.fileName = context.documentId;
    workbench.setContext(context);

    workbench.canvas()->setMinimapVisible(false);
    workbench.canvas()->selectNodeIds(
        {QStringLiteral("sym:kernel-root")});
    workbench.canvas()->zoomIn();
    const qreal kernelZoom = workbench.canvas()->zoomFactor();

    QVERIFY(workbench.setViewKind(InsightWorkbenchViewKind::Block));
    QVERIFY(workbench.canvas()->isMinimapVisible());
    QVERIFY(workbench.canvas()->selectedNodeIds().isEmpty());
    QVERIFY(workbench.setViewKind(InsightWorkbenchViewKind::Kernel));
    QVERIFY(!workbench.canvas()->isMinimapVisible());
    QCOMPARE(workbench.canvas()->selectedNodeIds(),
             QStringList{QStringLiteral("sym:kernel-root")});
    QVERIFY(qAbs(workbench.canvas()->zoomFactor() - kernelZoom) < 0.0001);
}

void RtlInsightWorkbenchTest::toolPagesRouteAllFourWorkbenchViews()
{
    const QList<QPair<LiveInsightKind, InsightWorkbenchViewKind>> routes = {
        {LiveInsightKind::Kernel, InsightWorkbenchViewKind::Kernel},
        {LiveInsightKind::Module, InsightWorkbenchViewKind::Block},
        {LiveInsightKind::Hotspot, InsightWorkbenchViewKind::Hotspot},
        {LiveInsightKind::State, InsightWorkbenchViewKind::StateTransition}
    };
    for (const auto& route : routes) {
        LiveInsightToolPage page(route.first);
        QVERIFY(page.workbenchForTest());
        QCOMPARE(page.workbenchForTest()->viewKind(), route.second);
        QVERIFY(!page.workbenchForTest()->canvas());
        if (route.first == LiveInsightKind::Kernel) QVERIFY(page.workbenchForTest()->kernelSurfaceForTest());
        else QVERIFY(page.workbenchForTest()->rtlSurfaceForTest());
    }
}

void RtlInsightWorkbenchTest::specializedModesSurviveRouting()
{
    QTemporaryDir output;
    QVERIFY(output.isValid());
    LiveInsightToolPage hotspotPage(LiveInsightKind::Hotspot);
    hotspotPage.resize(1120, 760);
    hotspotPage.show();
    auto* panel = hotspotPage.workbenchForTest()->rtlSurfaceForTest()->signalUsageHotspotPanelForTest();
    QVERIFY(panel);
    auto report = hotspotGraphViewFixture();
    report.matrixCells = {{QStringLiteral("graph_view_module"), QStringLiteral("graph_view.sv"),
        SignalUsageHotspotRole::Read, QStringLiteral("Read"), 1}};
    panel->setReportBuilderForTest([report](const SignalUsageHotspotQuery&, std::shared_ptr<const SemanticIndexSnapshot>) { return report; });
    LiveInsightToolContext context;
    context.fileName = QStringLiteral("graph_view.sv");
    context.documentId = context.fileName;
    context.moduleName = QStringLiteral("graph_view_module");
    context.signalName = QStringLiteral("tracked_signal");
    context.documentRevision = 5;
    hotspotPage.setContext(context);
    QTRY_VERIFY(!panel->reportBuildInFlightForTest());
    QVERIFY(panel->trackBlockCountForTest() > 0);
    QVERIFY(panel->matrixNonEmptyCellCountForTest() > 0);
    int navigationCount = 0;
    hotspotPage.setNavigationHandler([&](const QString&, int, int) { ++navigationCount; return true; });
    panel->setMatrixModeForTest(true);
    QVERIFY(panel->matrixModeForTest());
    const QString reviewDir = qEnvironmentVariable("ZEROSLACK_UI_REVIEW_DIR");
    if (!reviewDir.isEmpty()) {
        QDir().mkpath(reviewDir);
        QCoreApplication::processEvents();
        hotspotPage.grab().save(reviewDir + "/hotspot-matrix.png");
    }
    QVERIFY(panel->selectMatrixCellForTest(SignalUsageHotspotRole::Read, context.moduleName, context.fileName));
    panel->triggerFirstUsageNavigationForTest();
    QVERIFY(navigationCount > 0);
    QVERIFY(panel->exportGraph(SignalUsageHotspotExportSurface::Matrix, output.filePath("matrix.svg")).success);
    panel->setMatrixModeForTest(false);
    if (!reviewDir.isEmpty()) {
        QCoreApplication::processEvents();
        hotspotPage.grab().save(reviewDir + "/hotspot-track.png");
    }
    QVERIFY(panel->exportGraph(SignalUsageHotspotExportSurface::Track, output.filePath("track.svg")).success);
    const auto builds = panel->reportBuildRequestCountForTest();
    hotspotPage.setContext(context);
    QCOMPARE(panel->reportBuildRequestCountForTest(), builds);
    context.documentRevision = 4;
    hotspotPage.setContext(context);
    QCOMPARE(hotspotPage.workbenchForTest()->context().documentRevision, quint64(5));

    LiveInsightToolPage kernelPage(LiveInsightKind::Kernel);
    auto* kernel = kernelPage.workbenchForTest()->kernelSurfaceForTest();
    QVERIFY(kernel);
    kernel->renderReportForTest(signalKernelThemeFixture());
    const int nodes = kernel->visibleGraphNodeCountForTest();
    kernel->setGraphFilterForTest(false, true, false);
    QVERIFY(kernel->visibleGraphNodeCountForTest() < nodes);
    kernel->setGraphFilterForTest(true, true, false);
    QVERIFY(kernel->toggleFanoutGroupForTest(QStringLiteral("output:sink_module")));
    QVERIFY(kernelPage.workbenchForTest()->exportCurrentGraph(output.filePath("kernel.svg")).success);
    if (!reviewDir.isEmpty()) {
        kernelPage.resize(1120, 760); kernelPage.show();
        QCoreApplication::processEvents(); kernel->focusFit();
        kernelPage.grab().save(reviewDir + "/kernel.png");
    }
}

void RtlInsightWorkbenchTest::sidebarSectionTeardownReleasesFocusedControls()
{
    auto* index = SemanticIndex::getInstance();
    const auto previous = index->snapshot();
    const auto restoreSnapshot = qScopeGuard([&] { index->setSnapshot(previous); });
    const QString fileName = QDir::tempPath() + QStringLiteral("/ela_teardown.sv");
    const QHash<QString, QString> contents{{fileName,
        QStringLiteral("module child(input logic clk); endmodule\n"
                       "module top(input logic clk); child u_child(.clk(clk)); endmodule\n")}};
    SlangManager slang;
    const auto records = slang.extractOverlayWorkspaceSymbolRecords(
        contents, {}, {}, nullptr, nullptr, {fileName});
    index->setSnapshot(std::make_shared<const SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolRecords(records, {}, {}, contents)));
    QMainWindow window;
    auto* editor = new QWidget(&window);
    window.setCentralWidget(editor);
    ContextWorkspaceController controller(&window, editor);
    LiveInsightSession session;
    auto provider = std::make_unique<LiveInsightsContextProvider>(LiveInsightKind::Module, &session);
    provider->setToolContextSource([fileName] {
        LiveInsightToolContext context;
        context.workspaceId = QStringLiteral("workspace");
        context.documentId = fileName;
        context.fileName = fileName;
        context.moduleName = QStringLiteral("top");
        return context;
    });
    QVERIFY(controller.registerProvider(std::move(provider)));
    window.resize(1280, 800);
    window.show();
    const auto resource = LiveInsightsContextProvider::resourceForKind(
        LiveInsightKind::Module, QStringLiteral("workspace"));
    for (int repeat = 0; repeat < 3; ++repeat) {
        QVERIFY(controller.openResource(resource));
        QPointer<QWidget> view = controller.viewForResource(resource.stableKey());
        QVERIFY(view);
        QCoreApplication::processEvents();
        auto* depth = view->findChild<QSpinBox*>(QStringLiteral("rtlModuleBlockDepthSpin"));
        auto* breadcrumbs = view->findChild<QWidget*>(QStringLiteral("rtlModuleBreadcrumbs"));
        auto* more = view->findChild<QToolButton*>(QStringLiteral("rtlModuleMoreButton"));
        QVERIFY(!depth && !breadcrumbs && !more);
        auto* navigationRow = view->findChild<QWidget*>(QStringLiteral("rtlModuleBlockToolbar"));
        QVERIFY(navigationRow && navigationRow->isHidden());
        auto* inspector = view->findChild<QWidget*>(QStringLiteral("rtlGraphInspectorPanel"));
        auto* table = view->findChild<QTableWidget*>(QStringLiteral("rtlGraphDetailTable"));
        auto* body = view->findChild<QSplitter*>(QStringLiteral("rtlGraphBodySplitter"));
        QVERIFY(inspector && table && body);
        QVERIFY(inspector->isHidden());
        QVERIFY(table->isHidden());
        QCOMPARE(table->rowCount(), 0);
        auto* graph = qobject_cast<QGraphicsView*>(body->widget(0));
        QVERIFY(graph && graph->isVisible());
        QCOMPARE(graph->frameWidth(), 0);
        if (repeat == 0) {
            auto& theme = ApplicationThemeManager::instance();
            const auto previousMode = theme.mode();
            theme.setMode(ThemeMode::Dark);
            QCOMPARE(graph->frameWidth(), 0);
            theme.setMode(previousMode);
            QCOMPARE(graph->frameWidth(), 0);
            QVERIFY(navigationRow->isHidden());
        }
        QTRY_COMPARE(graph->width(), body->contentsRect().width());
        QVERIFY2(graph->height() >= view->height() * 0.75, qPrintable(QStringLiteral("canvas %1 / body %2")
            .arg(graph->height()).arg(view->height())));
        QTRY_VERIFY(graph->mapFromScene(graph->sceneRect()).boundingRect().width() > graph->viewport()->width() * 0.65
                    || graph->mapFromScene(graph->sceneRect()).boundingRect().height() > graph->viewport()->height() * 0.65);
        const QString review = qEnvironmentVariable("ZEROSLACK_UI_REVIEW_DIR");
        if (repeat == 0 && !review.isEmpty()) {
            QDir().mkpath(review);
            view->grab().save(review + QStringLiteral("/module-docked.png"));
        }
        auto* fit = controller.dockHost()->findChild<QToolButton*>(QStringLiteral("contextSectionFit"));
        QVERIFY(fit && fit->isVisible());
        auto* toggle = controller.dockHost()->findChild<QToolButton*>(QStringLiteral("contextSectionToggle"));
        auto* fullView = controller.dockHost()->findChild<QToolButton*>(QStringLiteral("contextDockFullView"));
        QVERIFY(toggle && toggle->isHidden());
        QVERIFY(fullView && fullView->isHidden());
        QVERIFY(controller.dockHost()->setSectionCollapsed(resource.stableKey(), true, false));
        QVERIFY(controller.dockHost()->isSectionCollapsed(resource.stableKey()));
        QVERIFY(controller.dockHost()->setSectionCollapsed(resource.stableKey(), false, false));
        QVERIFY(!controller.dockHost()->isSectionCollapsed(resource.stableKey()));
        QVERIFY(view->isVisible());
        fit->setFocus();
        fit->click();
        if (repeat == 0) {
            const auto key = resource.stableKey();
            QVERIFY(controller.unpinResource(key));
            auto* floating = controller.floatingWindow();
            QVERIFY(floating && floating->view() == view);
            QVERIFY(floating->windowFlags().testFlag(Qt::FramelessWindowHint));
            auto* floatingFit = floating->findChild<QToolButton*>(QStringLiteral("contextFloatingFit"));
            auto* floatingFullView = floating->findChild<QToolButton*>(QStringLiteral("contextFloatingFullView"));
            QVERIFY(floatingFit && floatingFit->isVisible());
            QVERIFY(floatingFullView && floatingFullView->isHidden());
            QCOMPARE(graph->frameWidth(), 0);
            floatingFit->click();
            if (!review.isEmpty()) floating->grab().save(review + QStringLiteral("/module-floating.png"));
            QVERIFY(controller.pinFloatingResource(key, true));
            QCOMPARE(controller.viewForResource(key), view.data());
            QVERIFY(controller.bottomDockWidget()->isVisible());
            QTRY_VERIFY(view->height() > 120);
            QTRY_VERIFY(controller.bottomDockWidget()->rect().contains(
                QRect(view->mapTo(controller.bottomDockWidget(), QPoint()), view->size())));
            QTRY_VERIFY(graph->isVisibleTo(controller.bottomDockWidget()));
            QVERIFY(navigationRow->isHidden());
            QTRY_VERIFY2(graph->height() >= view->height() - 8,
                qPrintable(QStringLiteral("canvas %1 / body %2").arg(graph->height()).arg(view->height())));
            QTRY_VERIFY(graph->viewport()->rect().adjusted(-2, -2, 2, 2).contains(
                graph->mapFromScene(graph->sceneRect()).boundingRect()));
            QTRY_VERIFY(graph->mapFromScene(graph->sceneRect()).boundingRect().width() > graph->viewport()->width() * 0.65
                        || graph->mapFromScene(graph->sceneRect()).boundingRect().height() > graph->viewport()->height() * 0.65);
            if (!review.isEmpty()) window.grab().save(review + QStringLiteral("/module-bottom.png"));
            QVERIFY(controller.unpinResource(key));
            QVERIFY(controller.pinFloatingResource(key));
            QCOMPARE(controller.viewForResource(key), view.data());
            QVERIFY(controller.dockWidget()->isVisible() && !controller.bottomDockWidget()->isVisible());
        }
        QToolButton* close = nullptr;
        for (auto* button : controller.dockHost()->findChildren<QToolButton*>())
            if (button->toolTip() == QStringLiteral("Close section")) close = button;
        QVERIFY(close);
        QTest::mouseClick(close, Qt::LeftButton);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QTest::qWait(500);
        QVERIFY(view.isNull());
        QCOMPARE(controller.dockHost()->resourceCount(), 0);
    }
    controller.setWorkspaceRoot(resource.workspaceId);
    QVERIFY(controller.openResource(resource,
        {ContextSurface::Docked, ContextPersistence::Kept, ContextBinding::Global}));
    auto saved = controller.captureState();
    QVERIFY(!saved.dockSections.isEmpty());
    saved.dockSections.first().collapsed = true;
    const auto restored = controller.restoreState(saved);
    QCOMPARE(restored.restoredResources, 1);
    QVERIFY(!controller.dockHost()->isSectionCollapsed(resource.stableKey()));
    QVERIFY(controller.viewForResource(resource.stableKey()));
    QVERIFY(controller.viewForResource(resource.stableKey())->isVisible());
}

void RtlInsightWorkbenchTest::moduleDiagramAdaptsAndNavigatesInstances()
{
    auto* index = SemanticIndex::getInstance();
    const auto previous = index->snapshot();
    const auto restore = qScopeGuard([&] { index->setSnapshot(previous); });
    const QString file = QDir::tempPath() + QStringLiteral("/module_compact_layout.sv");
    const QHash<QString, QString> contents{{file, QStringLiteral(
        "module leaf; endmodule\n"
        "module branch; leaf u_leaf(); leaf u_long_instance_name_for_width(); endmodule\n"
        "module top; branch u_left(); branch u_right(); endmodule\n")}};
    SlangManager slang;
    const auto records = slang.extractOverlayWorkspaceSymbolRecords(contents, {}, {}, nullptr, nullptr, {file});
    index->setSnapshot(std::make_shared<const SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolRecords(records, {}, {}, contents)));
    QMainWindow window;
    RtlInsightsPanelCoordinator panel(&window);
    window.addDockWidget(Qt::LeftDockWidgetArea, panel.dock());
    window.resize(1120, 700);
    window.show();
    panel.showModuleBlockDiagramForModule(file, QStringLiteral("top"));
    auto* graph = panel.graphView();
    auto* breadcrumb = window.findChild<QWidget*>(QStringLiteral("rtlModuleBreadcrumbs"));
    auto* toolbar = window.findChild<QWidget*>(QStringLiteral("rtlModuleBlockToolbar"));
    auto* fold = window.findChild<QAction*>(QStringLiteral("rtlModuleFoldAction"));
    auto* back = window.findChild<QAction*>(QStringLiteral("rtlModuleBackAction"));
    auto* forward = window.findChild<QAction*>(QStringLiteral("rtlModuleForwardAction"));
    QVERIFY(graph && !breadcrumb && toolbar && !fold && !back && !forward);
    QCOMPARE(graph->frameWidth(), 0);
    int navigations = 0;
    int definitionLine = 0;
    bool allowNavigation = true;
    panel.setNavigationHandler([&](const QString& targetFile, int line, int column) {
        if (!allowNavigation || targetFile != file || line <= 0 || column <= 0) return false;
        ++navigations;
        definitionLine = line;
        return true;
    });
    if (ApplicationThemeManager::instance().backend() == UiStyleBackend::Ela) {
        QVERIFY(window.findChild<QAction*>(QStringLiteral("rtlModuleFitAction")));
    }
    const auto node = [&graph](const QString& path) -> QGraphicsRectItem* {
        for (auto* item : graph->scene()->items()) {
            if (item->data(kGraphKindRole).toString() == QStringLiteral("module")
                && item->data(kGraphInstancePathRole).toString() == path)
                return dynamic_cast<QGraphicsRectItem*>(item);
        }
        return nullptr;
    };
    const auto enter = [&](const QString& path) {
        const QPoint point = graph->mapFromScene(node(path)->rect().topLeft() + QPointF(26, 16));
        QTest::mouseClick(graph->viewport(), Qt::LeftButton, Qt::NoModifier, point);
        QTest::mouseDClick(graph->viewport(), Qt::LeftButton, Qt::NoModifier, point);
        QTest::mouseRelease(graph->viewport(), Qt::LeftButton, Qt::NoModifier, point);
    };
    const auto selectedPath = [&graph]() {
        for (auto* item : graph->scene()->items())
            if (item->isSelected() && item->data(kGraphKindRole).toString() == QStringLiteral("module"))
                return item->data(kGraphInstancePathRole).toString();
        return QString();
    };
    QTRY_COMPARE(panel.graphNodeItemCountForTest(), 7);
    QTest::qWait(120);
    QVERIFY(graph->transform().m11() > 0.0);
    QVERIFY(node("top.u_left.u_leaf") && node("top.u_left.u_long_instance_name_for_width"));
    QVERIFY(node("top.u_left.u_leaf")->rect().width() < node("top.u_left.u_long_instance_name_for_width")->rect().width());
    QVERIFY(node("top.u_left.u_leaf")->rect().height() < 80);
    const QRectF initialRoot = node("top")->rect();
    QVERIFY(node("top")->rect().contains(node("top.u_left")->rect()));
    QVERIFY(node("top.u_left")->rect().contains(node("top.u_left.u_leaf")->rect()));
    QVERIFY(!node("top.u_left")->rect().intersects(node("top.u_right")->rect()));
    const auto capture = [&](const QString& name) {
        const QString dir = qEnvironmentVariable("ZEROSLACK_UI_REVIEW_DIR");
        if (!dir.isEmpty()) {
            QDir().mkpath(dir);
            window.grab().save(dir + QLatin1Char('/') + name + QStringLiteral(".png"));
        }
    };
    capture(QStringLiteral("module-wide"));

    QCOMPARE(node("top.u_left")->brush().color(), node("top.u_right")->brush().color());
    QVERIFY(node("top")->brush().color() != node("top.u_left")->brush().color());
    QCOMPARE(node("top.u_left")->cursor().shape(), Qt::ArrowCursor);
    panel.focusZoomIn();
    const qreal zoom = graph->transform().m11();
    window.resize(1100, 680);
    QTest::qWait(150);
    QCOMPARE(graph->transform().m11(), zoom);
    panel.focusFit();
    enter("top.u_right");
    QTRY_COMPARE(navigations, 1);
    QCOMPARE(definitionLine, 2);
    QCOMPARE(panel.graphNodeItemCountForTest(), 7);
    QCOMPARE(node("top.u_right")->opacity(), 1.0);
    QVERIFY(node("top")->opacity() < 0.4);
    QVERIFY(node("top.u_left")->opacity() < 0.4);
    QVERIFY(node("top.u_right.u_leaf"));
    QCOMPARE(selectedPath(), QStringLiteral("top.u_right"));
    enter("top.u_right.u_leaf");
    QTRY_COMPARE(navigations, 2);
    QCOMPARE(definitionLine, 1);
    QCOMPARE(panel.graphNodeItemCountForTest(), 7);
    QVERIFY(node("top.u_right.u_leaf"));
    QCOMPARE(selectedPath(), QStringLiteral("top.u_right.u_leaf"));
    QTest::mouseClick(graph->viewport(), Qt::BackButton);
    QTRY_COMPARE(selectedPath(), QStringLiteral("top.u_right"));
    QTest::mouseClick(graph->viewport(), Qt::ForwardButton);
    QTRY_COMPARE(selectedPath(), QStringLiteral("top.u_right.u_leaf"));
    QTest::mouseClick(graph->viewport(), Qt::BackButton);
    QTest::mouseClick(graph->viewport(), Qt::BackButton);
    QTRY_COMPARE(selectedPath(), QStringLiteral("top"));
    QCOMPARE(panel.graphNodeItemCountForTest(), 7);
    const int rootNavigations = navigations;
    QTest::mouseClick(graph->viewport(), Qt::BackButton);
    QCOMPARE(navigations, rootNavigations);
    allowNavigation = false;
    enter("top.u_left");
    QTest::qWait(20);
    QCOMPARE(navigations, rootNavigations);
    QCOMPARE(node("top")->opacity(), 1.0);
    allowNavigation = true;
    enter("top.u_left");
    QTRY_COMPARE(navigations, rootNavigations + 1);
    QTRY_COMPARE(selectedPath(), QStringLiteral("top.u_left"));
    const int branchNavigations = navigations;
    QTest::mouseClick(graph->viewport(), Qt::ForwardButton);
    QCOMPARE(navigations, branchNavigations);
    QCOMPARE(selectedPath(), QStringLiteral("top.u_left"));
    QTest::mouseClick(graph->viewport(), Qt::BackButton);
    QTRY_COMPARE(selectedPath(), QStringLiteral("top"));
    QCOMPARE(panel.graphNodeItemCountForTest(), 7);

    window.resize(390, 700);
    QTest::qWait(150);
    QVERIFY(node("top")->rect().width() < initialRoot.width());
    QVERIFY(node("top")->rect().height() > initialRoot.height());
    QVERIFY(node("top.u_left")->rect().bottom() < node("top.u_right")->rect().top());
    QVERIFY(toolbar->isVisible());
    capture(QStringLiteral("module-narrow"));
    panel.focusFit();
    QVERIFY(graph->transform().m11() > 0.0);
    const int narrowNavigations = navigations;
    enter("top.u_right");
    QTRY_COMPARE(navigations, narrowNavigations + 1);
    QTRY_COMPARE(selectedPath(), QStringLiteral("top.u_right"));
}

void RtlInsightWorkbenchTest::moduleDiagramIncludesDeepDescendantsAndTerminatesCycles()
{
    const QString file = QDir::tempPath() + QStringLiteral("/deep_modules.sv");
    QString source;
    for (int depth = 0; depth <= 12; ++depth) {
        source += QStringLiteral("module m%1; ").arg(depth);
        source += depth < 12 ? QStringLiteral("m%1 u_next(); ").arg(depth + 1)
                            : QStringLiteral("missing_type u_missing(); ");
        source += QStringLiteral("endmodule\n");
    }
    source += QStringLiteral("module independent; endmodule\n");
    const QHash<QString, QString> contents{{file, source}};
    SlangManager slang;
    const auto records = slang.extractOverlayWorkspaceSymbolRecords(contents, {}, {}, nullptr, nullptr, {file});
    SemanticIndex index;
    index.setSnapshot(std::make_shared<const SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolRecords(records, {}, {}, contents)));
    ModuleBlockDiagramService service(&index);
    ModuleBlockDiagramQuery query;
    query.fileName = file;
    query.moduleName = QStringLiteral("m0");
    const auto report = service.buildModuleBlockDiagram(query);
    QVERIFY(report.found);
    QCOMPARE(report.nodes.size(), 14);
    QCOMPARE(report.nodes.last().depth, 13);
    QVERIFY(report.nodes.last().unresolved);
    for (const auto& node : report.nodes) QVERIFY(node.moduleDisplayName != QStringLiteral("independent"));
    query.maxDepth = 2;
    QCOMPARE(service.buildModuleBlockDiagram(query).nodes.size(), 3);

    const QString inactiveSource = QStringLiteral(
        "module leaf; endmodule\n"
        "module branch; leaf u_leaf(); endmodule\n"
        "module top; if (0) begin : disabled branch u_branch(); end endmodule\n");
    const QHash<QString, QString> inactiveContents{{file, inactiveSource}};
    const auto inactiveRecords = slang.extractOverlayWorkspaceSymbolRecords(
        inactiveContents, {}, {}, nullptr, nullptr, {file});
    index.setSnapshot(std::make_shared<const SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolRecords(inactiveRecords, {}, {}, inactiveContents)));
    query.moduleName = QStringLiteral("top");
    query.maxDepth = -1;
    const auto inactive = service.buildModuleBlockDiagram(query);
    QCOMPARE(inactive.nodes.size(), 3);
    QVERIFY(inactive.nodes.at(1).unresolved);
    QVERIFY(inactive.nodes.at(2).unresolved);
    QCOMPARE(inactive.nodes.at(2).depth, 2);
    QVERIFY(!inactive.nodes.at(2).definitionCodeLink.fileName.isEmpty());

    SemanticIndex cycleIndex;
    const auto a = SemanticFixtureRecordBuilder("cycle_a", SymbolTaxonomy::DeclarationKind::Module)
        .withFile(file).withLine(1).withLocalHandle(90001).record();
    const auto b = SemanticFixtureRecordBuilder("cycle_b", SymbolTaxonomy::DeclarationKind::Module)
        .withFile(file).withLine(2).withLocalHandle(90002).record();
    cycleIndex.updateSymbolRecordsForFile(file, {a, b}, "module cycle_a; endmodule\nmodule cycle_b; endmodule\n");
    SymbolRelationshipEngine relationships;
    cycleIndex.attachRelationshipEngine(&relationships);
    relationships.addRelationship(a.localHandle, b.localHandle, SymbolRelationshipEngine::INSTANTIATES, "a to b");
    relationships.addRelationship(b.localHandle, a.localHandle, SymbolRelationshipEngine::INSTANTIATES, "b to a");
    ModuleBlockDiagramService cycleService(&cycleIndex);
    query.moduleName = QStringLiteral("cycle_a");
    query.maxDepth = -1;
    const auto cycle = cycleService.buildModuleBlockDiagram(query);
    QCOMPARE(cycle.nodes.size(), 3);
    QVERIFY(cycle.nodes.last().unresolved);
    QVERIFY(cycle.nodes.last().unresolvedReason.contains(QStringLiteral("cyclic")));
}

void RtlInsightWorkbenchTest::windowChromeSupportsNativeSnap()
{
#ifdef Q_OS_WIN
    if (QGuiApplication::platformName() != QStringLiteral("windows")) QSKIP("Native Windows platform required");
    QMainWindow window;
    window.setCentralWidget(new QWidget(&window));
    auto* navigationPane = new NavigationPaneCoordinator(&window);
    window.addDockWidget(Qt::LeftDockWidgetArea, navigationPane->dock());
    new WorkspaceChrome(&window, navigationPane, [] {});
    window.setGeometry(180, 180, 900, 600);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    const HWND handle = reinterpret_cast<HWND>(window.winId());
    const LONG_PTR style = GetWindowLongPtr(handle, GWL_STYLE);
    QVERIFY((style & WS_OVERLAPPEDWINDOW) == WS_OVERLAPPEDWINDOW);
    QVERIFY(!(style & WS_POPUP));
    auto* maximize = window.findChild<QToolButton*>(QStringLiteral("windowMaximizeButton"));
    QVERIFY(maximize);
    const QPoint maxPoint = maximize->mapToGlobal(maximize->rect().center());
    const qreal scale = window.devicePixelRatioF();
    const LRESULT hit = SendMessage(handle, WM_NCHITTEST, 0,
        MAKELPARAM(qRound(maxPoint.x() * scale), qRound(maxPoint.y() * scale)));
    QCOMPARE(hit, LRESULT(HTMAXBUTTON));
    SendMessage(handle, WM_SYSCOMMAND, SC_MAXIMIZE, 0);
    QTRY_VERIFY(window.isMaximized());
    SendMessage(handle, WM_SYSCOMMAND, SC_RESTORE, 0);
    QTRY_VERIFY(!window.isMaximized());
    window.activateWindow();
    SetForegroundWindow(handle);
    QTest::qWait(100);
    if (GetForegroundWindow() != handle) QSKIP("Foreground activation unavailable; native style and hit testing passed");
    INPUT keys[4]{};
    for (auto& key : keys) key.type = INPUT_KEYBOARD;
    keys[0].ki.wVk = VK_LWIN;
    keys[1].ki.wVk = VK_LEFT;
    keys[2].ki.wVk = VK_LEFT; keys[2].ki.dwFlags = KEYEVENTF_KEYUP;
    keys[3].ki.wVk = VK_LWIN; keys[3].ki.dwFlags = KEYEVENTF_KEYUP;
    const QRect before = window.geometry();
    QCOMPARE(SendInput(4, keys, sizeof(INPUT)), UINT(4));
    QTRY_VERIFY_WITH_TIMEOUT(window.geometry() != before, 2000);
    window.close();
#else
    QSKIP("Windows only");
#endif
}

void RtlInsightWorkbenchTest::queuedNativeMouseAllowsNullResult()
{
#ifdef Q_OS_WIN
    QMainWindow window;
    auto* maximize = new QToolButton(&window);
    WindowSnapChrome filter(&window, maximize);
    QSignalSpy clicks(maximize, &QToolButton::clicked);
    MSG message{};
    message.hwnd = reinterpret_cast<HWND>(window.winId());
    message.wParam = HTMAXBUTTON;
    message.message = WM_NCMOUSEMOVE;
    QVERIFY(!filter.nativeEventFilter("windows_generic_MSG", &message, nullptr));
    QVERIFY(maximize->property("nativeHovered").toBool());
    message.message = WM_NCMOUSELEAVE;
    QVERIFY(!filter.nativeEventFilter("windows_generic_MSG", &message, nullptr));
    QVERIFY(!maximize->property("nativeHovered").toBool());
    message.message = WM_NCLBUTTONDOWN;
    QVERIFY(filter.nativeEventFilter("windows_generic_MSG", &message, nullptr));
    message.message = WM_NCLBUTTONUP;
    QVERIFY(filter.nativeEventFilter("windows_generic_MSG", &message, nullptr));
    QCOMPARE(clicks.count(), 0);
    QTRY_COMPARE(clicks.count(), 1);
    message.message = WM_NCHITTEST;
    QVERIFY(!filter.nativeEventFilter("windows_generic_MSG", &message, nullptr));
    message.message = WM_NCCALCSIZE;
    QVERIFY(!filter.nativeEventFilter("windows_generic_MSG", &message, nullptr));
#endif
}

void RtlInsightWorkbenchTest::realWindowChromeButtons()
{
#ifdef Q_OS_WIN
    if (QGuiApplication::platformName() != QStringLiteral("windows")) QSKIP("Native Windows platform required");
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    ApplicationThemeManager::instance().applyToApplication();
    MainWindow window;
    window.resize(1120, 760);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto* title = window.findChild<QWidget*>(QStringLiteral("workspaceTitleBar"));
    QVERIFY(title);
    const QList<QAbstractButton*> buttons {
        title->findChild<QAbstractButton*>(QStringLiteral("windowMinimizeButton")),
        title->findChild<QAbstractButton*>(QStringLiteral("windowMaximizeButton")),
        title->findChild<QAbstractButton*>(QStringLiteral("windowCloseButton"))
    };
    for (auto* button : buttons) QVERIFY(button);
    window.activateWindow();
    SetForegroundWindow(reinterpret_cast<HWND>(window.internalWinId()));
    const QString reviewDir = qEnvironmentVariable("ZEROSLACK_UI_REVIEW_DIR");
    if (!reviewDir.isEmpty()) QDir().mkpath(reviewDir);
    for (auto* button : QList<QAbstractButton*>{window.findChild<QToolButton*>(QStringLiteral("projectRailButton")), buttons[0], buttons[1]}) {
        QVERIFY(button);
        QTest::mouseMove(title, QPoint(180, 18));
        QTest::qWait(50);
        const QColor rest = button->grab().toImage().pixelColor(6, 6);
        QTest::mouseMove(button, button->rect().center());
        QTest::qWait(100);
        QVERIFY2(button->grab().toImage().pixelColor(6, 6) != rest, qPrintable(button->objectName()));
        if (!reviewDir.isEmpty()) window.grab().save(reviewDir + "/" + button->objectName() + "-hover.png");
    }
    for (int i = 0; i < 3; ++i) {
        qInfo("minimize button");
        buttons[0]->click();
        QTRY_VERIFY(window.isMinimized());
        window.showNormal();
        QTRY_VERIFY(!window.isMinimized());
        qInfo("maximize button");
        buttons[1]->click();
        QTRY_VERIFY(window.isMaximized());
        qInfo("restore button");
        buttons[1]->click();
        QTRY_VERIFY(!window.isMaximized());
    }
    auto physicalClick = [&](QAbstractButton* button) {
        window.activateWindow();
        SetForegroundWindow(reinterpret_cast<HWND>(window.internalWinId()));
        QTest::qWait(100);
        if (GetForegroundWindow() != reinterpret_cast<HWND>(window.internalWinId())) return false;
        const QPoint local = button->mapTo(&window, button->rect().center());
        const qreal scale = window.devicePixelRatioF();
        POINT point{qRound(local.x() * scale), qRound(local.y() * scale)};
        ClientToScreen(reinterpret_cast<HWND>(window.internalWinId()), &point);
        SetCursorPos(point.x, point.y);
        INPUT input[2]{};
        input[0].type = input[1].type = INPUT_MOUSE;
        input[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        input[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
        return SendInput(2, input, sizeof(INPUT)) == 2;
    };
    for (int i = 0; i < 3; ++i) {
        qInfo("physical maximize");
        QVERIFY(physicalClick(buttons[1]));
        QTRY_VERIFY(window.isMaximized());
        qInfo("physical restore");
        QVERIFY(physicalClick(buttons[1]));
        QTRY_VERIFY(!window.isMaximized());
        qInfo("physical minimize");
        QVERIFY(physicalClick(buttons[0]));
        QTRY_VERIFY(window.isMinimized());
        window.showNormal();
        QTRY_VERIFY(!window.isMinimized());
    }
    window.close();
#else
    QSKIP("Windows only");
#endif
}

void RtlInsightWorkbenchTest::catppuccinPalettes()
{
    const QList<ThemeMode> modes = {ThemeMode::CatppuccinLatte, ThemeMode::CatppuccinFrappe,
        ThemeMode::CatppuccinMacchiato, ThemeMode::CatppuccinMocha};
    const QStringList bases = {"#eff1f5", "#303446", "#24273a", "#1e1e2e"};
    for (int i = 0; i < modes.size(); ++i) {
        ApplicationThemeManager::instance().setMode(modes[i]);
        const auto& theme = ApplicationThemeManager::instance().theme();
        QCOMPARE(theme.canvasBackground.name(), bases[i]);
        QCOMPARE(theme.input.text, theme.textPrimary);
        QVERIFY(theme.syntax.keyword != theme.syntax.comment);
        QCOMPARE(isDarkTheme(modes[i]), i != 0);
    }
    ApplicationThemeManager::instance().setMode(ThemeMode::Light);
}

void RtlInsightWorkbenchTest::sidebarRegistersFourDistinctIconsAndResources()
{
    QMainWindow window;
    auto* editorRegion = new QWidget(&window);
    window.setCentralWidget(editorRegion);
    ContextWorkspaceController controller(&window, editorRegion);
    LiveInsightSession session;
    const QList<LiveInsightKind> kinds = {
        LiveInsightKind::Kernel,
        LiveInsightKind::Module,
        LiveInsightKind::Hotspot,
        LiveInsightKind::State,
    };
    QSet<QString> stableKeys;
    for (LiveInsightKind kind : kinds) {
        const ContextResource resource =
            LiveInsightsContextProvider::resourceForKind(
                kind, QStringLiteral("workspace"));
        stableKeys.insert(resource.stableKey());
        QVERIFY(controller.registerProvider(
            std::make_unique<LiveInsightsContextProvider>(kind, &session)));
    }
    QCOMPARE(stableKeys.size(), 4);
    QCOMPARE(controller.rail()->entryIds().size(), 4);

    QList<QImage> images;
    for (LiveInsightKind kind : kinds) {
        const QString id =
            LiveInsightsContextProvider::providerIdForKind(kind);
        QAction* action = window.findChild<QAction*>(
            QStringLiteral("contextRail.%1").arg(id));
        QVERIFY(action);
        const QImage image = iconImage(action->icon());
        QVERIFY(!image.isNull());
        for (const QImage& previous : images)
            QVERIFY(image != previous);
        images.append(image);
    }

    LiveInsightsContextProvider kernelProvider(
        LiveInsightKind::Kernel, &session);
    const ContextResource kernelResource =
        kernelProvider.activationResource(QStringLiteral("workspace"));
    std::unique_ptr<QWidget> view(
        kernelProvider.createView(kernelResource, nullptr));
    auto* contextView = qobject_cast<LiveInsightsContextView*>(view.get());
    QVERIFY(contextView);
    QVERIFY(contextView->hasFixedKind());
    QCOMPARE(contextView->selectedKind(), LiveInsightKind::Kernel);
}

void RtlInsightWorkbenchTest::toolPageDetachesIntoWindow()
{
    LiveInsightToolPage page(LiveInsightKind::Kernel);
    QVERIFY(page.workbenchForTest());
    QMainWindow* detached = page.detachToWindow();
    QVERIFY(detached);
    QCOMPARE(page.detachedWindowForTest(), detached);
    auto* detachedPage =
        dynamic_cast<LiveInsightToolPage*>(detached->centralWidget());
    QVERIFY(detachedPage);
    QCOMPARE(detachedPage->kind(), LiveInsightKind::Kernel);
    QVERIFY(detachedPage->workbenchForTest());
    LiveInsightToolContext updatedContext;
    updatedContext.workspaceId = QStringLiteral("workspace");
    updatedContext.documentId = QStringLiteral("rtl/updated.sv");
    updatedContext.documentRevision = 7;
    updatedContext.fileName = updatedContext.documentId;
    updatedContext.moduleName = QStringLiteral("updated_top");
    updatedContext.signalName = QStringLiteral("updated_q");
    page.setContext(updatedContext);
    QCOMPARE(detachedPage->workbenchForTest()->context().documentRevision,
             quint64(7));
    QCOMPARE(detachedPage->workbenchForTest()->context().signalName,
             QStringLiteral("updated_q"));
    detached->close();
    QCoreApplication::processEvents();

}

void RtlInsightWorkbenchTest::sharedCanvasExportsSvgPngAndPdf()
{
    InsightGraphCore core;
    InsightGraphDraft draft = draftFor(QStringLiteral("export"));
    draft.nodes.append(makeNode(QStringLiteral("sym:target"),
                                QStringLiteral("target"),
                                InsightGraphDomain::DataFlow));
    InsightGraphEdge edge;
    edge.fromNodeId = draft.nodes.at(0).nodeId;
    edge.toNodeId = draft.nodes.at(1).nodeId;
    edge.displayName = QStringLiteral("drives");
    edge.domain = InsightGraphDomain::DataFlow;
    edge.edgeId = InsightGraphCore::stableEdgeId(
        edge.fromNodeId, edge.toNodeId, edge.displayName);
    draft.edges.append(edge);
    InsightCanvas canvas;
    canvas.applyUpdate(core.update(QStringLiteral("export"), draft));

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    for (const QString& suffix : {
             QStringLiteral("svg"),
             QStringLiteral("png"),
             QStringLiteral("pdf")}) {
        const QString path = directory.filePath(
            QStringLiteral("graph.%1").arg(suffix));
        const GraphExportResult result = canvas.exportGraph(path);
        QVERIFY2(result.success, qPrintable(result.failureReason));
        QVERIFY(QFileInfo(path).size() > 0);
    }
}

void RtlInsightWorkbenchTest::legacyBottomDockDoesNotCarryStateWhenDisabled()
{
    QWidget host;
    RtlInsightsPanelCoordinator panel(&host);
    QVERIFY(panel.stateViewEnabledForTest());
    panel.setStateViewEnabled(false);
    QVERIFY(!panel.stateViewEnabledForTest());
    QVERIFY(panel.dock());
    QCOMPARE(panel.dock()->property("carriesStateInsight").toBool(), false);
    auto* stateButton = panel.dock()->findChild<QWidget*>(
        QStringLiteral("rtlFsmGraphButton"));
    QVERIFY(stateButton);
    QVERIFY(stateButton->isHidden());
    const QString mode = panel.graphModeForTest();
    panel.showFsmGraph();
    QCOMPARE(panel.graphModeForTest(), mode);
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    if (!initializeUiStyleForTest()) return 3;
    RtlInsightWorkbenchTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "rtl_insight_workbench_test.moc"
