#include "contextworkspacecontroller.h"
#include "contextfloatingwindow.h"
#include "contextdockhost.h"
#include "testuistyle.h"
#include "live_insight_test_utils.h"
#include "contextrail.h"
#include "insightcanvas.h"
#include "insightgraphcore.h"
#include "rtlinsightsgraphconstants.h"
#include "actionregistry.h"
#include "liveinsightscontextprovider.h"
#include "liveinsightscontextview.h"
#include "liveinsightsession.h"
#include "liveinsightgraphreport.h"
#include "tabmanager.h"
#include "workspacemanager.h"
#include "mycodeeditor.h"
#include "liveinsighttoolpage.h"
#include "rtlinsightspanelcoordinator.h"
#include "rtlinsightworkbench.h"
#include "signal_trace_test_fixture.h"
#include "navigationwidget.h"
#include <QTreeWidgetItemIterator>

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
    void ordinaryAndSpecializedModuleDepthsDiffer();
    void stateTargetPickerUsesRenderSession();
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

    for(auto kind:{LiveInsightKind::Kernel,LiveInsightKind::Hotspot,LiveInsightKind::Module,LiveInsightKind::State}){
        LiveInsightToolPage page(kind);
        if(kind==LiveInsightKind::Kernel || kind==LiveInsightKind::Hotspot){QVERIFY(page.traceSurface());QVERIFY(!page.workbenchForTest());}
        else if(kind==LiveInsightKind::Module){QVERIFY(!page.workbenchForTest());QVERIFY(page.findChild<QPushButton*>("openExistingDesign"));}
        else{QVERIFY(page.workbenchForTest());QVERIFY(page.workbenchForTest()->rtlSurfaceForTest());QCOMPARE(page.workbenchForTest()->viewKind(),InsightWorkbenchViewKind::StateTransition);}
    }

}

void RtlInsightWorkbenchTest::specializedModesSurviveRouting()
{

    SignalTraceTestFixture fixture;QVERIFY(fixture.build("module top(input wire s,output wire a);assign a=s;endmodule\n"));
    auto* index=SemanticIndex::getInstance();const auto previous=index->snapshot();const auto restore=qScopeGuard([&]{index->setSnapshot(previous);});index->setSnapshot(fixture.context.token.snapshot);
    LiveInsightToolContext context;context.workspaceId=fixture.directory.path();context.projectId="main";context.fileName=context.documentId=fixture.file;
    context.documentText=fixture.source;context.documentRevision=5;context.moduleName="top";context.signalName="s";
    for(auto kind:{LiveInsightKind::Kernel,LiveInsightKind::Hotspot}){
        LiveInsightToolPage page(kind);page.resize(1100,700);page.show();page.setContext(context);auto* panel=page.traceSurface();QVERIFY(panel);QTRY_VERIFY(!panel->busy());
        QVERIFY(panel->currentObject().isConcrete());QCOMPARE(panel->heatModel()->references().size(),1);
        panel->setHeatmapMode(true);QVERIFY(panel->exportGraph(fixture.directory.filePath(liveInsightKindId(kind)+".svg")).success);
        auto* root=panel->traceTree(SignalTraceDirection::Upstream)->topLevelItem(0);page.setContext(context);QCOMPARE(panel->traceTree(SignalTraceDirection::Upstream)->topLevelItem(0),root);
        auto older=context;older.documentRevision=4;page.setContext(older);QCOMPARE(page.contextForTest().documentRevision,quint64(5));
    }

}

void RtlInsightWorkbenchTest::sidebarSectionTeardownReleasesFocusedControls()
{

    SignalTraceTestFixture fixture;QVERIFY(fixture.build("module top(input wire s,output wire a);assign a=s;endmodule\n"));
    auto* index=SemanticIndex::getInstance();const auto previous=index->snapshot();const auto restore=qScopeGuard([&]{index->setSnapshot(previous);});index->setSnapshot(fixture.context.token.snapshot);
    QMainWindow window;auto* editor=new QWidget(&window);window.setCentralWidget(editor);ContextWorkspaceController controller(&window,editor);LiveInsightSession session;
    auto provider=std::make_unique<LiveInsightsContextProvider>(LiveInsightKind::Kernel,&session);
    provider->setToolContextSource([&]{LiveInsightToolContext c;c.workspaceId=fixture.directory.path();c.projectId="main";c.fileName=c.documentId=fixture.file;c.moduleName="top";c.signalName="s";c.documentText=fixture.source;return c;});
    QVERIFY(controller.registerProvider(std::move(provider)));controller.setWorkspaceRoot(fixture.directory.path());window.resize(1280,800);window.show();
    const auto resource=LiveInsightsContextProvider::resourceForKind(LiveInsightKind::Kernel,fixture.directory.path());
    for(int repeat=0;repeat<3;++repeat){
        QVERIFY(controller.openResource(resource));QPointer<QWidget> view=controller.viewForResource(resource.stableKey());QVERIFY(view);
        auto* panel=view->findChild<SignalTracePanel*>();QVERIFY(panel);QTRY_VERIFY(!panel->busy());panel->setFocusSearchText("assign");
        auto* filter=panel->findChild<QLineEdit*>("traceFilter");QVERIFY(filter);filter->setFocus();
        QVERIFY(controller.unpinResource(resource.stableKey()));QVERIFY(controller.floatingWindow());QCOMPARE(controller.floatingWindow()->view(),view.data());
        QVERIFY(controller.pinFloatingResource(resource.stableKey(),true));QCOMPARE(controller.viewForResource(resource.stableKey()),view.data());QCOMPARE(panel->focusSearchText(),QString("assign"));
        QVERIFY(controller.unpinResource(resource.stableKey()));QVERIFY(controller.pinFloatingResource(resource.stableKey()));
        QToolButton* close=nullptr;for(auto* button:controller.dockHost()->findChildren<QToolButton*>())if(button->toolTip()=="Close section")close=button;
        QVERIFY(close);close->click();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QTRY_VERIFY(view.isNull());QCOMPARE(controller.dockHost()->resourceCount(),0);
    }

}

void RtlInsightWorkbenchTest::moduleDiagramAdaptsAndNavigatesInstances()
{

    SignalTraceTestFixture fixture;QVERIFY(fixture.build("module leaf;endmodule\nmodule branch;leaf u();endmodule\nmodule other;leaf inactive();endmodule\nmodule top;branch left();branch right();endmodule\n"));
    const QList<DesignHierarchyRoot> roots{{"main","Main","top",fixture.file,true},{"other","Other","other",fixture.file,true}};
    const auto report=HierarchyService(fixture.context.token).explicitDesignHierarchy(roots,"main",{fixture.file});
    QVERIFY(report.nodes.size()>=7);NavigationWidget view;view.resize(600,720);view.updateDesignHierarchy(report);view.setActiveTab(NavigationWidget::DesignTab);view.show();
    QVERIFY(view.selectDesignInstance("top.left.u",true));auto* tree=view.findChild<QTreeWidget*>("navigationDesignTree");QVERIFY(tree && tree->currentItem());
    QVERIFY(tree->currentItem()->text(0).contains("u [leaf]"));QVERIFY(view.selectDesignInstance("top.right.u",true));
    QVERIFY(!view.selectDesignInstance("other.inactive",true));QVERIFY(view.selectDesignModule("leaf",fixture.file));
    DesignHierarchyNode navigated;connect(&view,&NavigationWidget::designNodeDoubleClicked,&view,[&](const DesignHierarchyNode& n){navigated=n;});
    tree->itemDoubleClicked(tree->currentItem(),0);QCOMPARE(navigated.moduleType,QString("leaf"));QVERIFY(navigated.instanceLine>0 && navigated.definitionLine>0);
    QVERIFY(navigated.instanceLine!=navigated.definitionLine);
    int selectionEvents=0;connect(&view,&NavigationWidget::designInstanceSelected,&view,[&](const QString&){++selectionEvents;});
    QVERIFY(view.selectDesignInstance("top.left.u"));QCOMPARE(selectionEvents,0);
    for(QTreeWidgetItemIterator it(tree);*it;++it)QVERIFY((*it)->sizeHint(0).height()<=view.height());

    QVERIFY(fixture.build("module leaf;endmodule\nmodule branch #(parameter N=1);for(genvar i=0;i<N;i++)begin:g leaf u();end endmodule\nmodule top;branch #(.N(1)) left();branch #(.N(2)) right();endmodule\n"));
    const auto specialized=HierarchyService(fixture.context.token).explicitDesignHierarchy({{"main","Main","top",fixture.file,true}},"main",{fixture.file});
    QSet<QString> paths;for(const auto& node:specialized.nodes)paths.insert(node.instancePath);
    QVERIFY(paths.contains("top.left.g[0].u"));QVERIFY(paths.contains("top.right.g[0].u"));
    QVERIFY(paths.contains("top.right.g[1].u"));QVERIFY(!paths.contains("top.left.g[1].u"));

}

void RtlInsightWorkbenchTest::moduleDiagramIncludesDeepDescendantsAndTerminatesCycles()
{

    SignalTraceTestFixture fixture;QString source="module top;m0 u();endmodule\n";
    for(int i=0;i<13;++i)source+=QString("module m%1;m%2 u();endmodule\n").arg(i).arg(i+1);
    source+="module cycle;cycle again();endmodule\n";QVERIFY(fixture.build(source));
    const QList<DesignHierarchyRoot> roots{{"main","Main","top",fixture.file,true},{"cycle","Cycle","cycle",fixture.file,true}};
    const auto report=HierarchyService(fixture.context.token).explicitDesignHierarchy(roots,"main",{fixture.file});
    QVERIFY(report.nodes.size()>=15);QVERIFY(report.nodes.size()<40);bool missing=false,recursive=false;
    for(const auto& node:report.nodes){if(node.moduleType=="m13")missing|=node.unresolved;if(node.moduleType=="cycle" && !node.isTop)recursive|=node.unresolved;}
    QVERIFY(missing);QVERIFY(recursive);

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
    QCOMPARE(controller.rail()->entryIds().size(), 5);
    QVERIFY(controller.rail()->entryIds().contains(QStringLiteral("toolbox")));

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
    QVERIFY(page.traceSurface());
    QMainWindow* detached = page.detachToWindow();
    QVERIFY(detached);
    QCOMPARE(page.detachedWindowForTest(), detached);
    auto* detachedPage =
        dynamic_cast<LiveInsightToolPage*>(detached->centralWidget());
    QVERIFY(detachedPage);
    QCOMPARE(detachedPage->kind(), LiveInsightKind::Kernel);
    QVERIFY(detachedPage->traceSurface());
    LiveInsightToolContext updatedContext;
    updatedContext.workspaceId = QStringLiteral("workspace");
    updatedContext.documentId = QStringLiteral("rtl/updated.sv");
    updatedContext.documentRevision = 7;
    updatedContext.fileName = updatedContext.documentId;
    updatedContext.moduleName = QStringLiteral("updated_top");
    updatedContext.signalName = QStringLiteral("updated_q");
    page.setContext(updatedContext);
    QCOMPARE(detachedPage->contextForTest().documentRevision,
             quint64(7));
    QCOMPARE(detachedPage->contextForTest().signalName,
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

void RtlInsightWorkbenchTest::ordinaryAndSpecializedModuleDepthsDiffer()
{

    InsightViewContext context;context.workspaceId="workspace";context.projectId="project";context.fileName="top.sv";context.moduleName="top";
    for(bool specialized:{false,true}){
        RtlInsightWorkbench workbench(nullptr,specialized);QVERIFY(workbench.setViewKind(InsightWorkbenchViewKind::Block));
        int opened=0;workbench.setDesignHandler([&](const QString& file,const QString& module){if(file==context.fileName && module==context.moduleName)++opened;});
        workbench.setContext(context);auto* button=workbench.findChild<QPushButton*>();
        QPushButton* design=nullptr;for(auto* candidate:workbench.findChildren<QPushButton*>())if(candidate->text()=="Open module in Design")design=candidate;
        QVERIFY(design && design->isEnabled());design->click();QCOMPARE(opened,1);QVERIFY(!workbench.exportCurrentGraph("unused.svg").success);
    }
    const auto legacy=ModuleBlockInsightViewPlugin().build(context);QVERIFY(!legacy.available);QVERIFY(legacy.errorText.contains("Design"));

}

void RtlInsightWorkbenchTest::stateTargetPickerUsesRenderSession()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString file = directory.filePath("picker.sv");
    const QString source = "module top(input logic clk, rst_n, go);\n"
        "typedef enum logic [1:0] {S0=0,S1=1} state_t;\nstate_t state_q;\nstate_t state_d;\nstate_t other_q;\nstate_t other_d;\nlogic unused;\n"
        "always_ff @(posedge clk or negedge rst_n) begin\n"
        "if (!rst_n) state_q <= S0;\nelse state_q <= state_d;\nend\n"
        "always_comb begin\nstate_d = state_q;\ncase(state_q)\n"
        "S0: begin\nif (go) state_d = S1;\nend\nS1: begin\nstate_d = S0;\nend\n"
        "default: state_d = S0;\nendcase\nend\n"
        "always_ff @(posedge clk or negedge rst_n) begin\nif (!rst_n) other_q <= S0;\nelse other_q <= other_d;\nend\n"
        "always_comb begin\nother_d = other_q;\ncase(other_q)\n"
        "S0: begin\nother_d = S1;\nend\nS1: begin\nother_d = S0;\nend\n"
        "default: other_d = S0;\nendcase\nend\nendmodule\n";
    QFile output(file); QVERIFY(output.open(QIODevice::WriteOnly)); output.write(source.toUtf8()); output.close();
    auto* index = SemanticIndex::getInstance();
    const auto previous = index->snapshot();
    const auto restore = qScopeGuard([&] { index->setSnapshot(previous); });
    MainWindow window;
    window.resize(1200, 800); window.show();
    auto* tabs = window.findChild<TabManager*>(); QVERIFY(tabs);
    QVERIFY(tabs->openFileInTab(file));
    auto* editor = tabs->getCurrentEditor(); QVERIFY(editor);
    QTest::qWait(100);
    SlangManager slang;
    index->setSnapshot(std::make_shared<const SemanticIndexSnapshot>(SemanticIndexSnapshot::fromSymbolRecords(
        slang.extractSymbolRecords(file, source), {}, {}, {{file, source}})));
    QTextCursor cursor(editor->document()); cursor.setPosition(source.indexOf("state_t state_q"));
    editor->setTextCursor(cursor);
    auto* workspace = window.findChild<ContextWorkspaceController*>(); QVERIFY(workspace);
    auto resource = LiveInsightsContextProvider::resourceForKind(LiveInsightKind::State, "standalone");
    QVERIFY(workspace->openResource(resource, {ContextSurface::Docked, ContextPersistence::Kept, ContextBinding::Global}));
    QPointer<LiveInsightsContextView> view = qobject_cast<LiveInsightsContextView*>(workspace->viewForResource(resource.stableKey())); QVERIFY(view);
    QTRY_VERIFY(view->surfaceForTest());
    auto* session = view->surfaceForTest()->graphSession(); QVERIFY(session);
    session->flushPending(LiveInsightKind::State);
    QTRY_COMPARE(session->snapshot(LiveInsightKind::State).phase, LiveInsightPhase::Ready);
    QList<LiveInsightSession::Task> tasks;
    session->setTaskExecutor([&](auto task) { tasks.append(std::move(task)); });
    int builds = 0;
    bool allOffUi = true;
    session->setBuilder(LiveInsightKind::State, [&](const LiveInsightBuildRequest& request, const LiveInsightCancellationToken& token) {
        ++builds;
        allOffUi = allOffUi && QThread::currentThread() != qApp->thread();
        const auto graph = buildLiveInsightGraphReport(request.input.value("graphInput").value<LiveInsightGraphInput>(), token);
        return LiveInsightBuildResult::success(request, {{"graphReport", QVariant::fromValue(graph)}});
    });
    const auto choose = [&](const QString& name) {
        editor->publishInsightTargetAnnotationsForTest(0, editor->blockCount() - 1);
        for (int guard = 0; guard < 100; ++guard) {
            const auto candidates = editor->insightTargetCandidatesForTest();
            const int active = editor->insightTargetActiveIndexForTest();
            if (active >= 0 && candidates.at(active).name == name) return true;
            QKeyEvent event(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
            QApplication::sendEvent(editor, &event);
        }
        return false;
    };
    const auto enter = [&] { QKeyEvent event(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier); QApplication::sendEvent(editor, &event); };
    const auto work = [&] {
        // The production executor uses Qt threads. Keep their TLS lifetime
        // here while holding only the queued UI delivery for race coverage.
        std::unique_ptr<QThread> worker(QThread::create([task = tasks.takeFirst()]() mutable { task(); }));
        worker->start(); worker->wait();
    };
    QVERIFY(view->requestScopePick()); QVERIFY(choose("state_d")); enter();
    QVERIFY(editor->insightTargetPickModeActive());
    QCOMPARE(builds, 0); QCOMPARE(tasks.size(), 1);
    work();
    QTRY_VERIFY(!editor->insightTargetPickModeActive());
    QCOMPARE(view->saveState().value("target").toMap().value("signalName").toString(), QString("state_d"));
    session->flushPending(LiveInsightKind::State);
    QVERIFY(tasks.isEmpty()); QCOMPARE(builds, 1); QVERIFY(allOffUi);
    const auto accepted = session->snapshot(LiveInsightKind::State).payload.value("graphReport").value<LiveInsightGraphReportPtr>();
    QVERIFY(accepted);
    // Selecting the same valid target reuses this exact session publication.
    QVERIFY(view->requestScopePick()); QVERIFY(choose("state_d")); enter();
    QVERIFY(!editor->insightTargetPickModeActive()); QVERIFY(tasks.isEmpty()); QCOMPARE(builds, 1);
    QCOMPARE(session->snapshot(LiveInsightKind::State).payload.value("graphReport").value<LiveInsightGraphReportPtr>(), accepted);

    QSignalSpy statuses(editor, &MyCodeEditor::editorStatusMessageRequested);
    QVERIFY(view->requestScopePick()); QVERIFY(choose("clk")); enter(); QCOMPARE(tasks.size(), 1);
    work(); QTRY_COMPARE(session->snapshot(LiveInsightKind::State).phase, LiveInsightPhase::Ready);
    QVERIFY(!statuses.isEmpty());
    const QString reason = statuses.last().first().toString();
    QVERIFY(reason.startsWith("clk:") || reason.startsWith("clk has"));
    QVERIFY(editor->insightTargetPickModeActive());
    QCOMPARE(view->saveState().value("target").toMap().value("signalName").toString(), QString("state_d"));
    editor->cancelInsightTargetPickMode();

    // Finish the computation off-thread but hold queued delivery until each
    // lifecycle change. A completed old worker must never select its target.
    for (int action = 0; action < 4; ++action) {
        QVERIFY(view->requestScopePick()); QVERIFY(choose("other_d")); enter();
        QCOMPARE(tasks.size(), 1); work();
        if (action == 0) { QKeyEvent event(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier); QApplication::sendEvent(editor, &event); }
        if (action == 1) editor->cancelInsightTargetPickMode();
        if (action == 2) { QTextCursor edit(editor->document()); edit.movePosition(QTextCursor::End); edit.insertText("\n"); }
        if (action == 3) QMetaObject::invokeMethod(tabs, "activeTabChanged", Qt::DirectConnection, Q_ARG(MyCodeEditor*, nullptr));
        QCoreApplication::processEvents();
        QCOMPARE(view->saveState().value("target").toMap().value("signalName").toString(), QString("state_d"));
        editor->cancelInsightTargetPickMode();
    }
    QVERIFY(view->requestScopePick()); QVERIFY(choose("other_d")); enter();
    QCOMPARE(tasks.size(), 1); work();
    workspace->clearResources();
    QVERIFY(!editor->insightTargetPickModeActive());
    QCoreApplication::processEvents();
    if (view) QVERIFY(view->saveState().value("target").toMap().value("signalName").toString() != "other_d");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(!editor->insightTargetPickModeActive());
    QVERIFY(!view);
    // Reopen the actual consumer, then deliver the workspace transition while
    // its next validation result is queued for the UI thread.
    QVERIFY(workspace->openResource(resource, {ContextSurface::Docked, ContextPersistence::Kept, ContextBinding::Global}));
    view = qobject_cast<LiveInsightsContextView*>(workspace->viewForResource(resource.stableKey())); QVERIFY(view);
    QTRY_VERIFY(view->surfaceForTest());
    session = view->surfaceForTest()->graphSession(); QVERIFY(session);
    session->flushPending(LiveInsightKind::State);
    QTRY_COMPARE(session->snapshot(LiveInsightKind::State).phase, LiveInsightPhase::Ready);
    session->setTaskExecutor([&](auto task) { tasks.append(std::move(task)); });
    QVERIFY(view->requestScopePick()); QVERIFY(choose("other_d")); enter();
    QCOMPARE(tasks.size(), 1); work();
    auto* manager = window.findChild<WorkspaceManager*>(); QVERIFY(manager);
    QMetaObject::invokeMethod(manager, "workspaceClosed", Qt::DirectConnection);
    QCoreApplication::processEvents();
    QVERIFY(!editor->insightTargetPickModeActive());
    if (view) QVERIFY(view->saveState().value("target").toMap().value("signalName").toString() != "other_d");
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settings.path());
    if (!initializeUiStyleForTest()) return 3;
    RtlInsightWorkbenchTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "rtl_insight_workbench_test.moc"
