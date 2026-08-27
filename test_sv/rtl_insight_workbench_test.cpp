#include "contextworkspacecontroller.h"
#include "contextrail.h"
#include "insightcanvas.h"
#include "insightgraphcore.h"
#include "liveinsightscontextprovider.h"
#include "liveinsightscontextview.h"
#include "liveinsightsession.h"
#include "liveinsighttoolpage.h"
#include "rtlinsightspanelcoordinator.h"
#include "rtlinsightworkbench.h"
#include "semanticindex.h"

#include <QAction>
#include <QApplication>
#include <QFileInfo>
#include <QIcon>
#include <QImage>
#include <QMainWindow>
#include <QSet>
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
    result.symbolId = id;
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
    edge.fromSymbolId = first.nodes.at(0).symbolId;
    edge.toSymbolId = QStringLiteral("sym:child");
    edge.displayName = QStringLiteral("instantiates");
    edge.edgeId = InsightGraphCore::stableEdgeId(
        edge.fromSymbolId, edge.toSymbolId, edge.displayName);
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
    workbench.canvas()->selectSymbolIds(
        {QStringLiteral("sym:kernel-root")});
    workbench.canvas()->zoomIn();
    const qreal kernelZoom = workbench.canvas()->zoomFactor();

    QVERIFY(workbench.setViewKind(InsightWorkbenchViewKind::Block));
    QVERIFY(workbench.canvas()->isMinimapVisible());
    QVERIFY(workbench.canvas()->selectedSymbolIds().isEmpty());
    QVERIFY(workbench.setViewKind(InsightWorkbenchViewKind::Kernel));
    QVERIFY(!workbench.canvas()->isMinimapVisible());
    QCOMPARE(workbench.canvas()->selectedSymbolIds(),
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
    }
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
        LiveInsightKind::State
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
    edge.fromSymbolId = draft.nodes.at(0).symbolId;
    edge.toSymbolId = draft.nodes.at(1).symbolId;
    edge.displayName = QStringLiteral("drives");
    edge.domain = InsightGraphDomain::DataFlow;
    edge.edgeId = InsightGraphCore::stableEdgeId(
        edge.fromSymbolId, edge.toSymbolId, edge.displayName);
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

QTEST_MAIN(RtlInsightWorkbenchTest)

#include "rtl_insight_workbench_test.moc"
