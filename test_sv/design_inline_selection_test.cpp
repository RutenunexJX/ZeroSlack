#include "mainwindow.h"
#include "navigationwidget.h"
#include "navigationmanager.h"
#include "analysisscheduler.h"
#include "navigationservice.h"
#include "navigationrowstyle.h"
#include "designhierarchymodel.h"
#include "workspacemanager.h"
#include "tabmanager.h"
#include "editorsplitcontroller.h"
#include "contextworkspacecontroller.h"
#include "liveinsightscontextprovider.h"
#include "liveinsightscontextview.h"
#include "liveinsighttoolpage.h"
#include "rtlinsightworkbench.h"
#include "moduleblockdiagramview.h"
#include "moduleblockdiagramservice.h"
#include "editorcoordinator.h"
#include "editorsemanticcontextservice.h"
#include "mycodeeditor.h"
#include "tabfileio.h"
#include "applicationthememanager.h"
#include "workspaceconfigurationservice.h"
#include "semanticindexsnapshot.h"
#include "semantic_fixture_records.h"
#include "usertemplateservice.h"
#include "crashrecoveryservice.h"
#include "testuistyle.h"
#include "semanticdependencygraph.h"
#include "../src/workspace/designprojectservice.h"
#include <QtTest>
#include <QScreen>
#include <QScrollBar>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QScopeGuard>
#include <QMenu>
#include <QSemaphore>
#include <future>

namespace {
class MeasuredApplication : public QApplication {
public:
    using QApplication::QApplication;
    std::function<void(QObject*, QEvent*)> afterEvent;
    bool notify(QObject* receiver, QEvent* event) override {
        const auto result = QApplication::notify(receiver, event);
        if (afterEvent) afterEvent(receiver, event);
        return result;
    }
};
bool put(const QString& path, const QByteArray& bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath()); QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
SemanticSymbolRecord module(const QString& name, const QString& file) {
    return SemanticFixtureRecordBuilder(name, SymbolTaxonomy::DeclarationKind::Module).withFile(file).record();
}
SemanticSymbolRecord instance(const QString& name, const SemanticSymbolRecord& owner, const QString& type,
                              const SymbolStableKey& target = {}) {
    auto value = SemanticFixtureRecordBuilder(name, SymbolTaxonomy::DeclarationKind::Instance).withFile(owner.location.fileName).record();
    value.owner.name = owner.name; value.owner.stableKey = owner.stableKey;
    value.stableKey.ownerScope = owner.name; value.type.resolvedTypeName = type; value.type.stableKey = target;
    return value;
}
QTreeView* tree(NavigationWidget& widget) { return widget.findChild<QTreeView*>("navigationDesignTree"); }
QLineEdit* search(NavigationWidget& widget) { return widget.findChild<QLineEdit*>("navigationDesignSearchLineEdit"); }
void clickName(QTreeView* view, const QModelIndex& index) {
    view->scrollTo(index); QTest::qWait(10); const auto rect = view->visualRect(index);
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, {rect.left()+65, rect.center().y()});
}
void activateName(QTreeView* view, const QModelIndex& index) {
    clickName(view,index); const auto rect=view->visualRect(index);
    QTest::mouseDClick(view->viewport(),Qt::LeftButton,Qt::NoModifier,{rect.left()+65,rect.center().y()});
}
DesignHierarchyReport wideReport(int count) {
    DesignHierarchyReport report;
    DesignHierarchyNode root; root.id = root.rootId = "root"; root.instanceName = root.instancePath = root.moduleType = "top";
    root.isTop = true; root.definitionFile = "fixture/top.sv"; root.definitionLine = 1; report.nodes.append(root);
    for (int i=0; i<count; ++i) {
        auto child = root; child.id = QString("child-%1").arg(i); child.parentId = "root"; child.isTop = false;
        child.instanceName = QString("u_%1").arg(i); child.instancePath = "top." + child.instanceName;
        child.moduleType = "leaf"; child.definitionLine = 5; report.nodes.append(child);
    }
    return report;
}
void capture(QWidget& widget, const QString& name) {
    const auto output = qEnvironmentVariable("ZEROSLACK_DESIGN_REPORT"); if (output.isEmpty()) return;
    QDir().mkpath(output); QTest::qWait(35); const auto image = widget.grab(); image.save(output+'/'+name+".png");
    put(output+'/'+name+".json", QJsonDocument(QJsonObject{{"platform", QGuiApplication::platformName()},
        {"dpr", widget.devicePixelRatioF()}, {"width", widget.width()}, {"height", widget.height()},
        {"pixelWidth", image.width()}, {"pixelHeight", image.height()}}).toJson());
}
}
class DesignInlineSelectionTest : public QObject {
    Q_OBJECT
private slots:
    void sourceTopPersistenceAndProjection() {
        QTemporaryDir dir;QVERIFY(dir.isValid());
        const auto top=dir.filePath("top.sv"), tb=dir.filePath("tb.sv"), leaf=dir.filePath("leaf.sv"),
            pkg=dir.filePath("types.sv"), header=dir.filePath("defs.svh"), unused=dir.filePath("unused.sv"),
            dup1=dir.filePath("a.sv"), dup2=dir.filePath("b.sv");
        QHash<QString,QString> contents{{top,"module top; import types::*; leaf u(); endmodule"},
            {tb,"`include \"defs.svh\"\nmodule tb; top dut(); endmodule"},{leaf,"module leaf; endmodule"},
            {pkg,"package types; parameter N=1; endpackage"},{header,"`define WIDTH 1\n"},
            {unused,"module unused; endmodule"},{dup1,"module duplicate; endmodule"},{dup2,"module duplicate; endmodule"}};
        ProjectSnapshot project;project.workspaceRoot=dir.path();project.includeDirs={dir.path()};
        for(auto it=contents.cbegin();it!=contents.cend();++it){QVERIFY(put(it.key(),it.value().toUtf8()));project.allFiles<<it.key();
            project.systemVerilogFiles<<it.key();project.sourceRoles.insert(it.key(),SymbolTaxonomy::sourceRoleForFileName(it.key()));}
        const auto topSymbol=module("top",top),tbSymbol=module("tb",tb),leafSymbol=module("leaf",leaf);
        QList<SemanticSymbolRecord> symbols{topSymbol,tbSymbol,leafSymbol,module("unused",unused),module("duplicate",dup1),module("duplicate",dup2),
            instance("u",topSymbol,"leaf",leafSymbol.stableKey),instance("dut",tbSymbol,"top",topSymbol.stableKey)};
        SemanticIndex index;index.setSnapshot(std::make_shared<SemanticIndexSnapshot>(SemanticIndexSnapshot::fromSymbolRecords(symbols,{}, {},contents)));
        const auto token=index.snapshotToken();const auto hierarchy=HierarchyService(token).automaticDesignHierarchy();
        const auto projection=DesignTreeProjection::build(hierarchy,token,project,{top,"top"},{tb,"tb"},{});
        DesignHierarchyModel model;model.setReport(hierarchy,projection);QTRY_VERIFY(!model.applying());QCOMPARE(model.rowCount(),4);
        QCOMPARE(model.report()->nodes.size(),hierarchy.nodes.size());
        const auto hasFile=[&](const QString& section,const QString& file){for(const auto& row:projection->nodes)
            if(row.parentId=="section/"+section && row.file==file && row.semantic.id.isEmpty())return true;return false;};
        QVERIFY(hasFile("Headers",header));QVERIFY(hasFile("Sources",pkg));QVERIFY(hasFile("Simulation",pkg));
        if(!hasFile("Unreferenced",unused)) {
            qWarning()<<"Unresolved hierarchy"<<hierarchy.unresolvedModules;
            const auto facts=SemanticDependencyGraph::build(project,contents,{}).fileFacts();
            for(auto it=facts.cbegin();it!=facts.cend();++it)if(it->parseError)qWarning()<<"Incomplete parse"<<it.key();
        }
        QVERIFY(hasFile("Unreferenced",unused));QVERIFY(hasFile("Unreferenced",dup1));QVERIFY(hasFile("Unreferenced",dup2));
        for(int i=0;i<4;++i)QVERIFY(model.node(model.index(i,0)).id.isEmpty());
        DesignProjectService service;QVERIFY(service.setWorkspace(dir.path()).isEmpty());
        QVERIFY(service.setSourceTop(top,"top").isEmpty());QCOMPARE(service.sourceTop(),qMakePair(top,QString("top")));
        DesignProjectService reopened;QVERIFY(reopened.setWorkspace(dir.path()).isEmpty());QCOMPARE(reopened.sourceTop(),service.sourceTop());
        QVERIFY(!service.setSourceTop(dir.path()+"/../outside.sv","outside").isEmpty());QCOMPARE(service.sourceTop(),reopened.sourceTop());
        QVERIFY(service.setSourceTop(dup2,"duplicate").isEmpty());QCOMPARE(service.sourceTop().first,dup2);
        QVERIFY(QFile::remove(dup2));DesignProjectService missing;QVERIFY(missing.setWorkspace(dir.path()).isEmpty());
        QCOMPARE(missing.sourceTop(),service.sourceTop());
        const auto missingView=DesignTreeProjection::build(hierarchy,token,project,{dir.filePath("missing.sv"),"missing"},{tb,"tb"},{});
        QVERIFY(missingView->nodeById.contains("section/Sources/missing"));
        QVERIFY(missingView->childrenById.value("section/Unreferenced").isEmpty());
    }
    void measuredSemanticAndModuleWorkflow() {
        QTemporaryDir data; QVERIFY(data.isValid());
        const auto configured = qEnvironmentVariable("ZEROSLACK_WORKFLOW_FIXTURE");
        const auto workspace = configured.isEmpty() ? data.filePath("workspace") : configured;
        const auto source = workspace + "/hierarchy.sv";
        QByteArray ports, connections;
        for (int i = 0; i < 24; ++i) {
            if (i) { ports += ','; connections += ','; }
            ports += "input logic p" + QByteArray::number(i);
            connections += ".p" + QByteArray::number(i) + "(1'b0)";
        }
        const auto text = "module leaf(" + ports + "); endmodule\nmodule branch(" + ports
            + "); leaf a(" + connections + "); leaf b(" + connections
            + "); endmodule\nmodule top; branch left(" + connections
            + "); branch right(" + connections + "); endmodule\n";
        if (!QFile::exists(source)) QVERIFY(put(source, text));
        QFile fixture(source); QVERIFY(fixture.open(QIODevice::ReadOnly)); QCOMPARE(fixture.readAll(), text);
        qputenv("ZEROSLACK_SESSION_STORAGE_PATH", data.filePath("session.ini").toUtf8());
        qputenv("ZEROSLACK_EDITING_TIME_STORAGE_PATH", data.filePath("editing.json").toUtf8());
        MainWindow window;
        int slangCalls=0, cacheHits=0;
        QJsonArray semanticTimings;
        connect(window.analysisScheduler.get(),&AnalysisScheduler::semanticAnalysisTelemetry,&window,[&](const auto& telemetry) {
            if(telemetry.stage==SemanticAnalysisStage::Worker && telemetry.slangInvoked) ++slangCalls;
            if(telemetry.stage==SemanticAnalysisStage::Publication) {
                if(telemetry.detail.contains("disk-cache=hit")) ++cacheHits;
                semanticTimings.append(QJsonObject{{"workerMs",telemetry.workerMs},{"publicationMs",telemetry.publicationMs},
                    {"detail",telemetry.detail}});
            }
        });
        window.tabManager->setCrashRecoveryService(std::make_unique<CrashRecoveryService>(data.filePath("recovery")));
        window.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
        window.resize(1400,900); window.show();
        QElapsedTimer elapsed; elapsed.start();
        QVERIFY(window.workspaceManager->openWorkspace(workspace));
        auto* nav = window.findChild<NavigationWidget*>(); QVERIFY(nav);
        nav->setActiveTab(NavigationWidget::DesignTab);
        auto* model = qobject_cast<DesignHierarchyModel*>(tree(*nav)->model()); QVERIFY(model);
        QTRY_VERIFY_WITH_TIMEOUT(model->report() && model->report()->nodesByPath.contains("top.right.b"),30000);
        QCOMPARE(model->report()->nodes.size(),7);
        QJsonObject measurements{{"openToDesignMs", elapsed.nsecsElapsed()/1e6},
            {"designNodes", model->report()->nodes.size()}, {"workspace", workspace},
            {"sourceSha256", QString::fromLatin1(QCryptographicHash::hash(text,QCryptographicHash::Sha256).toHex())}};
        QVERIFY(window.tabManager->openFileInTab(source));
        QTRY_VERIFY_WITH_TIMEOUT(!window.analysisScheduler->isSemanticAnalysisActive(),10000);
        auto* editor = window.tabManager->getCurrentEditor(); QVERIFY(editor);
        const int position = editor->toPlainText().indexOf("top;");
        auto cursor = editor->textCursor(); cursor.setPosition(position); cursor.select(QTextCursor::WordUnderCursor); editor->setTextCursor(cursor);
        const auto context = editor->editorSemanticContextForPosition(position,true);
        QMenu menu;
        auto* app = static_cast<MeasuredApplication*>(qApp);
        double menuPaintMs = -1, graphPaintMs = -1, layoutDeliveredMs = -1;
        app->afterEvent = [&](QObject* receiver,QEvent* event) {
            if (receiver == &menu && event->type() == QEvent::Paint && menuPaintMs < 0)
                menuPaintMs = elapsed.nsecsElapsed()/1e6;
        };
        const auto clearProbe = qScopeGuard([&] { app->afterEvent = {}; });
        elapsed.restart();
        emit editor->sourceSymbolContextMenuRequested(&menu,context);
        measurements["menuPreparationMs"] = elapsed.nsecsElapsed()/1e6;
        menu.popup(editor->mapToGlobal(QPoint(100,100)));
        QTRY_VERIFY_WITH_TIMEOUT(menuPaintMs >= 0,5000);
        measurements["menuFirstPaintMs"] = menuPaintMs;
        menu.close(); QCOMPARE(editor->textCursor().selectedText(),QString("top"));
        ModuleBlockDiagramQuery query; query.moduleName = "top"; query.fileName = source;
        elapsed.restart();
        const auto hierarchy = HierarchyService(SemanticIndex::getInstance()->snapshotToken()).automaticDesignHierarchy();
        measurements["hierarchyMs"] = elapsed.nsecsElapsed()/1e6;
        elapsed.restart();
        const auto report = ModuleBlockDiagramService(SemanticIndex::getInstance()->snapshotToken()).buildModuleBlockDiagram(query);
        measurements["reportMs"] = elapsed.nsecsElapsed()/1e6;
        measurements["reportNodes"] = report.nodes.size(); QVERIFY(report.found);
        QCOMPARE(report.nodes.size(),7);
        QCOMPARE(HierarchyService::automaticBuildCountForTesting(SemanticIndex::getInstance()->snapshotToken()),std::uint64_t(1));
        // Hold the actual global publication's hierarchy builder. Menu opening
        // must not join it, and a concurrent hierarchy query must share its work.
        const auto original = SemanticIndex::getInstance()->snapshotToken();
        const auto fresh = std::make_shared<SemanticIndexSnapshot>(original.snapshot->withAdditionalRelationships({}));
        SemanticIndex::getInstance()->setSnapshot(fresh);
        const auto blockedToken = SemanticIndex::getInstance()->snapshotToken();
        QSemaphore entered, release;
        std::atomic_int checks{0};
        auto first = std::async(std::launch::async,[&] {
            return HierarchyService(blockedToken).sharedDesignHierarchy({},[&] {
                if (++checks == 3) { entered.release(); release.tryAcquire(1,3000); }
                return false;
            });
        });
        const auto releaseBuilder = qScopeGuard([&] { release.release(); if(first.valid()) first.wait();
            SemanticIndex::getInstance()->setSnapshot(original.snapshot); });
        QTRY_VERIFY_WITH_TIMEOUT(entered.available()>0,2000);
        auto second = std::async(std::launch::async,[blockedToken] { return HierarchyService(blockedToken).sharedDesignHierarchy(); });
        menuPaintMs = -1;
        app->afterEvent = [&](QObject* receiver,QEvent* event) {
            if (receiver==&menu && event->type()==QEvent::Paint && menuPaintMs<0) menuPaintMs=elapsed.nsecsElapsed()/1e6;
        };
        elapsed.restart(); emit editor->sourceSymbolContextMenuRequested(&menu,context); menu.popup(editor->mapToGlobal(QPoint(100,100)));
        QTRY_VERIFY_WITH_TIMEOUT(menuPaintMs>=0,1000); menu.close();
        QVERIFY2(menuPaintMs<1000,"Menu waited for the deliberately blocked hierarchy builder");
        measurements["menuWhileHierarchyBlockedMs"]=menuPaintMs;
        const int instanceTypePosition = editor->toPlainText().indexOf("leaf a(");
        QVERIFY(instanceTypePosition >= 0);
        cursor.setPosition(instanceTypePosition); cursor.select(QTextCursor::WordUnderCursor); editor->setTextCursor(cursor);
        const auto instanceContext = editor->editorSemanticContextForPosition(instanceTypePosition,true);
        QVERIFY(editor->instanceSlotsAvailableAt(instanceTypePosition));
        QMenu instanceMenu;
        menuPaintMs = -1;
        app->afterEvent = [&](QObject* receiver,QEvent* event) {
            if (receiver==&instanceMenu && event->type()==QEvent::Paint && menuPaintMs<0) menuPaintMs=elapsed.nsecsElapsed()/1e6;
        };
        elapsed.restart(); emit editor->sourceSymbolContextMenuRequested(&instanceMenu,instanceContext);
        instanceMenu.popup(editor->mapToGlobal(QPoint(100,100)));
        QTRY_VERIFY_WITH_TIMEOUT(menuPaintMs>=0,1000); instanceMenu.close();
        QVERIFY2(menuPaintMs<1000,"Instance menu waited for the deliberately blocked hierarchy builder");
        auto* instanceAction = instanceMenu.findChild<QAction*>("refactor.editInstanceSlots");
        QVERIFY(instanceAction && instanceAction->isEnabled() && instanceAction->property("executable").toBool());
        QCOMPARE(editor->textCursor().selectedText(),QString("leaf"));
        measurements["instanceMenuWhileHierarchyBlockedMs"]=menuPaintMs;
        release.release(); first.wait(); second.wait();
        QVERIFY(first.get()==second.get()); QCOMPARE(HierarchyService::automaticBuildCountForTesting(blockedToken),std::uint64_t(1));
        SemanticIndex::getInstance()->setSnapshot(original.snapshot);
        ModuleBlockDiagramView graph; graph.resize(1000,700);
        auto* canvas = graph.findChild<QWidget*>("moduleBlockCanvas"); QVERIFY(canvas);
        app->afterEvent = [&](QObject* receiver,QEvent* event) {
            if (graph.layoutReady() && layoutDeliveredMs < 0)
                layoutDeliveredMs = elapsed.nsecsElapsed()/1e6;
            if (receiver == canvas && event->type() == QEvent::Paint && graph.layoutReady() && graphPaintMs < 0)
                graphPaintMs = elapsed.nsecsElapsed()/1e6;
        };
        QTimer heartbeat;
        QElapsedTimer eventGap; eventGap.start();
        double maximumEventGapMs=0; int heartbeatCount=0;
        connect(&heartbeat,&QTimer::timeout,&graph,[&] {
            maximumEventGapMs=qMax(maximumEventGapMs,eventGap.nsecsElapsed()/1e6);
            eventGap.restart(); ++heartbeatCount;
        });
        heartbeat.start(1);
        elapsed.restart(); graph.setReport(report,workspace); graph.show();
        QTRY_VERIFY_WITH_TIMEOUT(graph.layoutReady(),10000);
        measurements["layoutObservedMs"] = elapsed.nsecsElapsed()/1e6;
        QTRY_VERIFY_WITH_TIMEOUT(graphPaintMs >= 0,10000);
        heartbeat.stop();
        measurements["layoutDeliveredMs"] = layoutDeliveredMs;
        measurements["maximumUiHeartbeatGapMs"] = maximumEventGapMs;
        measurements["uiHeartbeatCount"] = heartbeatCount;
        measurements["graphFirstPaintMs"] = graphPaintMs;
        measurements["reportMeasurementPath"] = "direct immutable-snapshot service, followed by native view";
        measurements["dpr"] = graph.devicePixelRatioF();
        measurements["slangCalls"] = slangCalls;
        measurements["persistentCacheHits"] = cacheHits;
        measurements["semanticTimings"] = semanticTimings;
        const auto output = qEnvironmentVariable("ZEROSLACK_DESIGN_REPORT");
        if (!output.isEmpty()) QVERIFY(put(output+"/workflow.json",QJsonDocument(measurements).toJson()));
        qInfo().noquote() << QJsonDocument(measurements).toJson(QJsonDocument::Compact);
        capture(graph,"measured-module"); app->afterEvent = {};
    }
    void realInstanceSlotMenuPreservesEntrypointsAndGuards() {
        QTemporaryDir data; QVERIFY(data.isValid());
        const auto workspace = data.filePath("workspace");
        const auto source = workspace + "/slots.sv";
        const QByteArray bytes =
            "module leaf #(parameter WIDTH=2)(input logic clk); endmodule\n"
            "module top; logic clk;\n"
            "  leaf #(.WIDTH(2)) u_leaf(.clk(clk));\n"
            "  leaf empty();\n"
            "endmodule\n";
        const QString text = QString::fromUtf8(bytes);
        QVERIFY(put(source,bytes));
        qputenv("ZEROSLACK_SESSION_STORAGE_PATH",data.filePath("session.ini").toUtf8());
        qputenv("ZEROSLACK_EDITING_TIME_STORAGE_PATH",data.filePath("editing.json").toUtf8());
        MainWindow window;
        window.tabManager->setCrashRecoveryService(std::make_unique<CrashRecoveryService>(data.filePath("recovery")));
        window.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
        window.resize(1200,800); window.show(); window.activateWindow();
        QVERIFY(window.workspaceManager->openWorkspace(workspace));
        QTRY_VERIFY_WITH_TIMEOUT(!window.analysisScheduler->isSemanticAnalysisActive()
            && SemanticIndex::getInstance()->getCachedFileContent(source)==text,30000);
        QVERIFY(window.tabManager->openFileInTab(source));
        auto* editor = window.tabManager->getCurrentEditor(); QVERIFY(editor);
        const auto populate = [&](QMenu& menu,int position) {
            auto cursor=editor->textCursor(); cursor.setPosition(position); cursor.select(QTextCursor::WordUnderCursor);
            editor->setTextCursor(cursor); editor->setFocus();
            emit editor->sourceSymbolContextMenuRequested(&menu,editor->editorSemanticContextForPosition(position,true));
            return menu.findChild<QAction*>("refactor.editInstanceSlots");
        };
        const QList<QPair<QString,int>> entries{
            {"module-type",text.indexOf("leaf #(.WIDTH")},
            {"instance-name",text.indexOf("u_leaf")},
            {"port-connection",text.indexOf(".clk(clk)")+1}
        };
        for(const auto& entry:entries) {
            QVERIFY(entry.second>=0); QMenu menu;
            auto* action=populate(menu,entry.second); QVERIFY(action);
            QVERIFY2(action->isEnabled()&&action->property("executable").toBool(),qPrintable(entry.first));
            const auto selection=editor->textCursor().selectedText(); QVERIFY(!selection.isEmpty());
            if(entry.first=="module-type") {
                auto* diagram=menu.findChild<QAction*>("insight.moduleBlockDiagram");
                QVERIFY(diagram&&diagram->isEnabled());
            }
            action->trigger();
            QVERIFY(editor->templateSlotModeActive());
            QCOMPARE(editor->textCursor().selectedText(),QString("2"));
            QTest::keyClick(editor,Qt::Key_Tab);
            QCOMPARE(editor->textCursor().selectedText(),QString("clk"));
            QTest::keyClick(editor,Qt::Key_Escape);
            QVERIFY(!editor->templateSlotModeActive());
            QCOMPARE(editor->toPlainText(),text);
            QTRY_VERIFY(editor->hasFocus());
            qInfo().noquote()<<"instance-menu-entry"<<entry.first<<"enabled and executed; parameter/port slots preserved";
        }
        for(int position:{text.indexOf("module leaf")+7,text.indexOf("logic clk")+6,text.indexOf("empty()")}) {
            QVERIFY(position>=0); QMenu menu; auto* action=populate(menu,position); QVERIFY(action);
            QVERIFY(!action->isEnabled()); QVERIFY(!action->property("executable").toBool());
        }
        const int position=entries.first().second;
        editor->setReadOnly(true);
        { QMenu menu; auto* action=populate(menu,position); QVERIFY(action);
          QVERIFY(!action->isEnabled()); QVERIFY(action->property("visibleReason").toString().contains("read-only")); }
        editor->setReadOnly(false);
        { QMenu menu; auto* action=populate(menu,position); QVERIFY(action&&action->isEnabled());
          editor->setReadOnly(true); action->trigger();
          QVERIFY(!editor->templateSlotModeActive()); QCOMPARE(editor->toPlainText(),text); }
        editor->setReadOnly(false);
        { QMenu menu; auto* action=populate(menu,position); QVERIFY(action&&action->isEnabled());
          auto changed=editor->textCursor(); changed.setPosition(0); changed.insertText("// newer source\n");
          const auto updated=editor->toPlainText(); action->trigger();
          QVERIFY(!editor->templateSlotModeActive()); QCOMPARE(editor->toPlainText(),updated);
          editor->undo(); QCOMPARE(editor->toPlainText(),text); }
        { QMenu menu; auto* action=populate(menu,position); QVERIFY(action&&action->isEnabled()); action->trigger();
          QVERIFY(editor->templateSlotModeActive()); QTest::keyClicks(editor,"4");
          QVERIFY(editor->toPlainText().contains("WIDTH(4)")); QTest::keyClick(editor,Qt::Key_Escape);
          editor->undo(); QCOMPARE(editor->toPlainText(),text);
          editor->redo(); QVERIFY(editor->toPlainText().contains("WIDTH(4)"));
          editor->undo(); QCOMPARE(editor->toPlainText(),text); }
        qInfo()<<"instance-menu-guards invalid/read-only/stale source and edit undo/redo passed";
        window.close();
    }
    void portReferencesCannotBecomeHierarchyEdges() {
        QTemporaryDir files; QVERIFY(files.isValid());
        const auto source=files.filePath("hierarchy.sv");
        const auto top=module("top",source), leaf=module("leaf",source);
        auto real=instance("u",top,"leaf",leaf.stableKey);
        QList<SemanticSymbolRecord> records{top,leaf,real};
        QList<SemanticRelationship> relationships;
        for(int i=0;i<128;++i) {
            auto port=instance(QString("arbitrary_%1").arg(i),top,"leaf",leaf.stableKey);
            port.usageRole=SymbolTaxonomy::SymbolUsageRole::Reference;
            port.collectorKind=SymbolTaxonomy::CollectorKind::InstPin;
            records.append(port);
            SemanticRelationship edge; edge.fromStableKey=top.stableKey; edge.toStableKey=port.stableKey;
            edge.type=SymbolRelationshipEngine::INSTANTIATES; edge.toAccessPath=port.name; relationships.append(edge);
        }
        const auto snapshot=std::make_shared<SemanticIndexSnapshot>(SemanticIndexSnapshot::fromSymbolRecords(records,relationships));
        const SemanticSnapshotToken token{snapshot,1};
        const auto hierarchy=HierarchyService(token).automaticDesignHierarchy();
        QCOMPARE(hierarchy.nodes.size(),2); QVERIFY(hierarchy.nodesByPath.contains("top.u"));
        QCOMPARE(snapshot->getSymbolRecordsByDeclarationKind(SymbolTaxonomy::DeclarationKind::Instance).size(),129);
        QCOMPARE(snapshot->relationships().size(),128);
        const auto graph=ModuleBlockDiagramService(token).buildModuleBlockDiagram({{},"top",source});
        QCOMPARE(graph.nodes.size(),2); QCOMPARE(graph.nodes[1].stableId,hierarchy.nodes[hierarchy.nodesByPath["top.u"].first()].id);
    }
    void removedSelectionAndWorkspaceSwitchDiscardStaleSearch() {
        QTemporaryDir files; QVERIFY(files.isValid());
        NavigationWidget widget; widget.resize(360,620); widget.setWorkspaceRoot(files.filePath("a"));
        widget.updateDesignHierarchy(wideReport(10000)); widget.setActiveTab(NavigationWidget::DesignTab); widget.show();
        auto* view=tree(widget);auto* model=qobject_cast<DesignHierarchyModel*>(view->model());
        QTRY_COMPARE(model->rowCount(),1);
        QVERIFY(widget.selectDesignInstance("top.u_4",true));
        const auto previous=widget.designViewState();
        QVERIFY(previous["selectionAncestors"].toArray().contains("root"));
        QSignalSpy resets(model,&QAbstractItemModel::modelReset);
        widget.updateDesignHierarchy(wideReport(3));
        QTRY_VERIFY(!model->applying());
        QTRY_COMPARE(view->currentIndex().data(Qt::UserRole).toString(),QString("root"));
        QCOMPARE(resets.size(),0);
        widget.updateDesignHierarchy(wideReport(10000));QTRY_VERIFY(!model->applying());
        search(widget)->setText("u_9999");
        widget.setWorkspaceRoot(files.filePath("b"));
        auto other=wideReport(0);other.nodes[0].id=other.nodes[0].rootId="other-root";
        other.nodes[0].instanceName=other.nodes[0].instancePath=other.nodes[0].moduleType="other";
        widget.updateDesignHierarchy(other);
        QTRY_COMPARE(model->rowCount(),1);QTest::qWait(180);
        QCOMPARE(view->model(),model);QVERIFY(search(widget)->text().isEmpty());
        QCOMPARE(model->node(model->index(0,0)).id,QString("other-root"));
        QVERIFY(!model->report()->nodesByPath.contains("top.u_9999"));
    }
    void damagedCompatibilityRecordNeverLeaksAnotherWorkspace() {
        QTemporaryDir files;QVERIFY(files.isValid());
        const auto first=files.filePath("first"),second=files.filePath("second");
        QVERIFY(QDir().mkpath(first));QVERIFY(QDir().mkpath(second));
        DesignProjectService service;QVERIFY(service.setWorkspace(first).isEmpty());
        QVERIFY(service.saveViewState(first,{{"version",1},{"selected","first-node"}}).isEmpty());
        const auto bad=second+"/.simdock/design.json";const QByteArray bytes="{ damaged record";
        QVERIFY(put(bad,bytes));QVERIFY(!service.setWorkspace(second).isEmpty());
        QCOMPARE(service.workspace(),QFileInfo(second).canonicalFilePath());
        QVERIFY(service.viewState().isEmpty());QVERIFY(service.designRoots().isEmpty());
        QFile file(bad);QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),bytes);
        QVERIFY(service.setWorkspace(first).isEmpty());QCOMPARE(service.viewState()["selected"].toString(),QString("first-node"));
    }
    void automaticRootsIdentityAndAbnormalComponents() {
        const auto top = module("top", "fixture/top.sv"), other = module("other", "fixture/other.sv"),
            leaf = module("leaf", "fixture/leaf.sv"), duplicate = module("leaf", "fixture/other-leaf.sv"),
            orphan = module("orphan", "fixture/orphan.sv"), a = module("cycle_a", "fixture/a.sv"), b = module("cycle_b", "fixture/b.sv");
        auto interface = SemanticFixtureRecordBuilder("bus_if", SymbolTaxonomy::DeclarationKind::Interface).withFile("fixture/bus.sv").record();
        auto package = SemanticFixtureRecordBuilder("types", SymbolTaxonomy::DeclarationKind::Package).withFile("fixture/types.sv").record();
        QList<SemanticSymbolRecord> records{top, other, leaf, duplicate, orphan, a, b, interface, package,
            instance("u_0", top, "leaf", leaf.stableKey), instance("u_1", top, "leaf", leaf.stableKey),
            instance("ambiguous", top, "leaf"), instance("missing", other, "unknown"),
            instance("u_b", a, "cycle_b", b.stableKey), instance("u_a", b, "cycle_a", a.stableKey)};
        SemanticIndex index; index.setSnapshot(std::make_shared<SemanticIndexSnapshot>(SemanticIndexSnapshot::fromSymbolRecords(records)));
        HierarchyService service(&index); const auto report = service.automaticDesignHierarchy();
        QVERIFY(report.rootModules.contains("top")); QVERIFY(report.rootModules.contains("other"));
        QVERIFY(report.rootModules.contains("orphan")); QVERIFY(report.rootModules.contains("bus_if"));
        QVERIFY(!report.rootModules.contains("types")); QVERIFY(report.rootModules.contains("cycle_a"));
        QCOMPARE(report.nodesByPath.value("top.u_0").size(), 1); QCOMPARE(report.nodesByPath.value("top.u_1").size(), 1);
        const auto first = report.nodes[report.nodesByPath["top.u_0"].first()];
        const auto second = report.nodes[report.nodesByPath["top.u_1"].first()];
        QCOMPARE(first.definitionId, second.definitionId); QVERIFY(first.id != second.id);
        QCOMPARE(first.definitionFile, leaf.location.fileName);
        QVERIFY(HierarchyService::definitionIdentity(leaf.stableKey) != HierarchyService::definitionIdentity(duplicate.stableKey));
        QVERIFY(report.nodes[report.nodesByPath["top.ambiguous"].first()].unresolvedReason.contains("Ambiguous"));
        QVERIFY(report.nodes[report.nodesByPath["other.missing"].first()].unresolved);
        QVERIFY(std::any_of(report.nodes.cbegin(), report.nodes.cend(), [](const auto& value) { return value.unresolvedReason.contains("Cyclic"); }));
        for (auto& record : records) { record.location.startLine += 10; record.stableKey.sourcePosition += 100; record.owner.stableKey.sourcePosition += 100; record.type.stableKey.sourcePosition += 100; }
        index.setSnapshot(std::make_shared<SemanticIndexSnapshot>(SemanticIndexSnapshot::fromSymbolRecords(records)));
        const auto shifted = service.automaticDesignHierarchy();
        QCOMPARE(shifted.structureFingerprint, report.structureFingerprint);
        QCOMPARE(shifted.nodes[shifted.nodeById[first.id]].definitionLine, first.definitionLine+10);
        QVERIFY(service.automaticDesignHierarchy({}, [] { return true; }).nodes.isEmpty());
    }
    void lazyRowsAndAnchorRefreshKeepIdentity() {
        DesignHierarchyModel model; auto report = wideReport(10000); model.setReport(report);
        QSignalSpy applied(&model, &DesignHierarchyModel::reportApplied);
        QTRY_VERIFY(!applied.isEmpty()); QCOMPARE(model.rowCount(), 1); QCOMPARE(model.materializedCount(), 1);
        const QPersistentModelIndex root = model.index(0, 0);
        QVERIFY(model.hasChildren(root)); QCOMPARE(model.rowCount(root), 0); QVERIFY(model.canFetchMore(root));
        model.fetchMore(root); QCOMPARE(model.rowCount(root), 256); QCOMPARE(model.materializedCount(), 257);
        const QPersistentModelIndex selected = model.index(10, 0, root);
        QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
        for (auto& node : report.nodes) node.definitionLine += 20;
        model.setReport(report); QTRY_VERIFY(!model.applying());
        QVERIFY(root.isValid()); QVERIFY(selected.isValid()); QCOMPARE(model.node(selected).definitionLine, 25);
        QCOMPARE(model.materializedCount(), 257); QCOMPARE(resets.size(), 0);
        report.nodes.removeAt(2); model.setReport(report); QTRY_VERIFY(!model.applying());
        QVERIFY(selected.isValid()); QCOMPARE(model.node(selected).id, "child-10"); QCOMPARE(resets.size(), 0);
        const auto last = model.indexForId("child-9999"); QVERIFY(last.isValid()); QCOMPARE(model.node(last).instancePath, "top.u_9999");
    }
    void disclosureNavigationSearchAndRecovery_data() {
        QTest::addColumn<bool>("dark"); QTest::addColumn<int>("width");
        QTest::newRow("light-wide") << false << 420; QTest::newRow("light-narrow") << false << 200;
        QTest::newRow("dark-wide") << true << 420; QTest::newRow("dark-narrow") << true << 200;
    }
    void disclosureNavigationSearchAndRecovery() {
        QFETCH(bool,dark); QFETCH(int,width);
        ApplicationThemeManager::instance().setMode(dark ? ThemeMode::Dark : ThemeMode::Light);
        NavigationWidget widget; widget.resize(width, 620); widget.setWorkspaceRoot("fixture");
        widget.updateDesignHierarchy(wideReport(50)); widget.setActiveTab(NavigationWidget::DesignTab); widget.show();
        auto* view = tree(widget); auto* model = qobject_cast<DesignHierarchyModel*>(view->model());
        QTRY_COMPARE(model->rowCount(), 1); QVERIFY(!widget.findChild<QWidget*>("addDesignProject"));
        QVERIFY(!widget.findChild<QWidget*>("cancelDesignSelection")); QVERIFY(!widget.findChild<QWidget*>("designCandidateTree"));
        const auto root = model->index(0, 0); QSignalSpy navigate(&widget, &NavigationWidget::designNodeDoubleClicked);
        const auto rect = view->visualRect(root);
        QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, {rect.left()+10, rect.center().y()});
        QVERIFY(view->isExpanded(root)); QCOMPARE(navigate.size(), 0);
        clickName(view, root); QCOMPARE(navigate.size(), 0); QVERIFY(view->isExpanded(root));
        activateName(view, root); QCOMPARE(navigate.size(), 1); QVERIFY(view->isExpanded(root));
        QTest::keyClick(view,Qt::Key_Return); QCOMPARE(navigate.size(),2);
        QTest::mouseClick(view->viewport(),Qt::RightButton,Qt::NoModifier,{rect.left()+65,rect.center().y()});
        QCOMPARE(navigate.size(),2);
        widget.setDesignHierarchyPending(true); QVERIFY(view->isEnabled()); widget.setDesignHierarchyPending(false);
        widget.setSearchFilter(NavigationWidget::FileTab, "unrelated-files-query");
        const auto selected = model->index(5, 0, root); view->setCurrentIndex(selected); view->scrollTo(selected);
        const auto identity = selected.data(Qt::UserRole).toString(); const auto scroll = view->verticalScrollBar()->value();
        QSignalSpy resets(model, &QAbstractItemModel::modelReset);
        search(widget)->setText("u_49");
        QTRY_VERIFY(!view->property("designSearchPending").toBool()); QVERIFY(view->model() != model);
        QCOMPARE(view->model()->rowCount(), 1); QVERIFY(view->model()->index(0,0).data().toString().contains("top.u_49"));
        search(widget)->clear(); QCOMPARE(view->model(), model); QCOMPARE(view->currentIndex().data(Qt::UserRole).toString(), identity);
        QCOMPARE(view->verticalScrollBar()->value(), scroll); QVERIFY(view->isExpanded(root)); QCOMPARE(resets.size(), 0);
        search(widget)->setText("u_48"); search(widget)->setText("no-such-instance"); search(widget)->clear(); QTest::qWait(180);
        QCOMPARE(view->model(), model); QCOMPARE(widget.searchFilter(NavigationWidget::FileTab), "unrelated-files-query");
        search(widget)->setText("u_49"); QTRY_VERIFY(!view->property("designSearchPending").toBool());
        clickName(view, view->model()->index(0,0)); QVERIFY(view->model()!=model); QCOMPARE(navigate.size(),2);
        activateName(view, view->model()->index(0,0)); QCOMPARE(view->model(), model); QCOMPARE(navigate.size(),3);
        QCOMPARE(model->node(view->currentIndex()).instancePath, "top.u_49"); QVERIFY(view->isExpanded(root));
        capture(widget, QString("design-%1-%2").arg(dark ? "dark" : "light").arg(width));
    }
    void deepRowsAndLargeFont() {
        NavigationWidget widget; widget.resize(200,680); DesignHierarchyReport report;
        for (int i=0; i<14; ++i) {
            DesignHierarchyNode node; node.id=QString::number(i); if(i) node.parentId=QString::number(i-1);
            node.instanceName="long_instance_name_at_depth_"+node.id; node.instancePath=node.instanceName;
            node.moduleType="long_module_type"; node.isTop=i==0; node.unresolved=i==13; node.unresolvedReason="Missing module definition";
            report.nodes.append(node);
        }
        widget.updateDesignHierarchy(report); widget.setActiveTab(NavigationWidget::DesignTab); widget.show();
        auto* view=tree(widget); auto* model=qobject_cast<DesignHierarchyModel*>(view->model());
        QTRY_COMPARE(model->rowCount(),1); auto font=view->font();
        if(font.pixelSize()>0)font.setPixelSize(qCeil(font.pixelSize()*1.5)); else font.setPointSizeF(font.pointSizeF()*1.5);
        view->setFont(font); const auto leaf=model->indexForId("13");
        for(auto parent=leaf.parent();parent.isValid();parent=parent.parent())view->expand(parent);
        view->setCurrentIndex(leaf);view->scrollTo(leaf);QTest::qWait(30);
        const auto rect=view->visualRect(leaf);QVERIFY(view->viewport()->rect().contains(rect.center()));
        QVERIFY(rect.height()>=QFontMetrics(view->font()).height());QVERIFY(leaf.data(Qt::ToolTipRole).toString().contains("Missing module definition"));
        for(bool dark:{false,true}){ApplicationThemeManager::instance().setMode(dark?ThemeMode::Dark:ThemeMode::Light);capture(widget,dark?"deep-large-dark":"deep-large-light");}
    }
    void viewStatePreservesLegacyAndUnknownFields() {
        QTemporaryDir data; QVERIFY(data.isValid()); simdock::WorkspaceStateFile store; QString error;
        QVERIFY(store.open(data.path(), "design", "zeroslack.design/v1", &error));
        const QJsonArray roots{QJsonObject{{"id","legacy"},{"module","old_top"},{"file","old.sv"},{"expanded",false}}};
        QVERIFY(store.save({{"roots",roots},{"activeProject","legacy"},{"futureField",QJsonObject{{"value",42}}}}, &error));
        DesignProjectService service; QVERIFY(service.setWorkspace(data.path()).isEmpty());
        const QJsonObject state{{"version",1},{"selected","stable-instance"},{"expanded",QJsonArray{"root"}}};
        QVERIFY(service.saveViewState(data.path(),state).isEmpty());
        simdock::WorkspaceStateFile read; QVERIFY(read.open(data.path(),"design","zeroslack.design/v1",&error));
        QCOMPARE(read.value()["roots"].toArray(),roots); QCOMPARE(read.value()["activeProject"].toString(),"legacy");
        QCOMPARE(read.value()["futureField"].toObject()["value"].toInt(),42); QCOMPARE(read.value()["viewState"].toObject(),state);
    }
    void defaultsAndExplicitConfiguration() {
        const QStringList expected{".sv",".svh",".v",".vh",".vp",".svp",".svi"};
        QCOMPARE(WorkspaceConfigurationService::defaultFileExtensions(),expected);
        TabFileIo fileIo;
        for(const auto& suffix:expected)QVERIFY(fileIo.isSystemVerilogFile("source"+suffix));
        QVERIFY(!fileIo.isSystemVerilogFile("source.vhd"));
        QTemporaryDir data;ProjectModel model;QStringList files;
        for(const auto& suffix:expected)files<<data.filePath("source"+suffix);
        files<<data.filePath("unsupported.vhd");model.setWorkspaceState(data.path(),files);
        QCOMPARE(model.fileExtensions(),expected);QCOMPARE(model.snapshot().systemVerilogFiles.size(),7);
        WorkspaceConfigurationService service;
        auto config=service.defaultConfiguration(data.path());config.fileExtensions={".v",".custom"};
        QVERIFY(service.save(config));QCOMPARE(service.load(data.path()).fileExtensions,config.fileExtensions);
        config=service.load(data.path());
        config.fileExtensions={".sv",".svh",".v",".vh"};
        QVERIFY(service.save(config));QCOMPARE(service.load(data.path()).fileExtensions,config.fileExtensions);
    }


    void realModuleSourceNavigationKeepsCanvasVisible() {
        QTemporaryDir data; QVERIFY(data.isValid());
        const auto workspace=data.filePath("workspace");
        const auto branch=workspace+"/branch.sv", leaf=workspace+"/leaf.sv";
        QVERIFY(put(workspace+"/top.sv","module top; branch left(); branch right(); endmodule\n"));
        QVERIFY(put(branch,"module branch; leaf u(); endmodule\n"));
        QVERIFY(put(leaf,"module leaf; endmodule\n"));
        qputenv("ZEROSLACK_SESSION_STORAGE_PATH",data.filePath("session.ini").toUtf8());
        qputenv("ZEROSLACK_EDITING_TIME_STORAGE_PATH",data.filePath("editing.json").toUtf8());
        MainWindow window;
        window.tabManager->setCrashRecoveryService(std::make_unique<CrashRecoveryService>(data.filePath("recovery")));
        window.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
        window.resize(1400,900);window.show();window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QVERIFY(window.workspaceManager->openWorkspace(workspace));
        auto* nav=window.findChild<NavigationWidget*>();QVERIFY(nav);
        nav->setActiveTab(NavigationWidget::DesignTab);
        auto* treeView=tree(*nav);
        auto* model=qobject_cast<DesignHierarchyModel*>(treeView->model());
        QTRY_VERIFY_WITH_TIMEOUT(model->report() && model->report()->nodesByPath.contains("top.right.u"),30000);
        auto* split=window.tabManager->editorSplitController();
        split->setActiveGroup(split->regionActiveGroup("left"));
        QVERIFY(nav->selectDesignInstance("top.right",true));activateName(treeView,treeView->currentIndex());
        QTRY_COMPARE(window.tabManager->getCurrentDocument().fileName,branch);
        QCOMPARE(split->regionForGroup(split->groupForPage(window.tabManager->getCurrentEditor())),QString("main"));

        auto* context=window.findChild<ContextWorkspaceController*>();QVERIFY(context);
        const auto resource=LiveInsightsContextProvider::resourceForKind(LiveInsightKind::Module,workspace);
        QVERIFY(context->openResource(resource,{ContextSurface::Docked,ContextPersistence::Kept,ContextBinding::Global}));
        auto* contextView=qobject_cast<LiveInsightsContextView*>(context->viewForResource(resource.stableKey()));QVERIFY(contextView);
        QTRY_VERIFY(contextView->surfaceForTest());
        auto* view=contextView->surfaceForTest()->workbenchForTest()->moduleView();QVERIFY(view);
        QTRY_VERIFY_WITH_TIMEOUT(view->layoutReady(),10000);
        const auto hierarchy=model->report();
        const auto leafId=hierarchy->nodes[hierarchy->nodesByPath.value("top.right.u").first()].id;
        auto* canvas=view->findChild<QWidget*>("moduleBlockCanvas");QVERIFY(canvas);
        QTRY_VERIFY(!view->nodeRect(leafId).isEmpty());
        const auto point=view->nodeRect(leafId).center().toPoint()-canvas->pos();
        split->setActiveGroup(split->regionActiveGroup("right"));
        QTest::mouseClick(canvas,Qt::LeftButton,Qt::NoModifier,point);
        QCOMPARE(window.tabManager->contextDocumentMetadata().fileName,branch);
        QTRY_COMPARE(window.tabManager->getCurrentDocument().fileName,leaf);
        QCOMPARE(split->regionForGroup(split->groupForPage(window.tabManager->getCurrentEditor())),QString("main"));
        QVERIFY(canvas->isVisible());
        QTest::mouseDClick(canvas,Qt::LeftButton,Qt::NoModifier,point);
        QTRY_COMPARE(view->focusId(),leafId);
        QVERIFY(canvas->isVisible());
        window.close();
    }

    void realMainWindowAutomaticHierarchyAndSuffixes() {
        QTemporaryDir data; QVERIFY(data.isValid()); const auto workspace=data.filePath("workspace");
        QStringList files,names; const auto suffixes=WorkspaceConfigurationService::defaultFileExtensions();
        for(int i=0;i<suffixes.size();++i) {
            const auto name=QString("suffix_module_%1").arg(i); names<<name;
            const auto file=workspace+"/source"+suffixes[i];files<<file;QVERIFY(put(file,("module "+name+"; endmodule").toUtf8()));
        }
        const auto hierarchy=workspace+"/hierarchy.sv";
        QVERIFY(put(hierarchy, "module leaf; endmodule\nmodule branch #(parameter N=2); for(genvar i=0;i<N;i++) begin:g leaf u(); end endmodule\nmodule top; branch #(.N(1)) a(); branch #(.N(3)) b(); leaf arr[1:0](); endmodule\n"));
        qputenv("ZEROSLACK_SESSION_STORAGE_PATH",data.filePath("session.ini").toUtf8());
        qputenv("ZEROSLACK_EDITING_TIME_STORAGE_PATH",data.filePath("editing.json").toUtf8());
        MainWindow window; window.tabManager->setCrashRecoveryService(std::make_unique<CrashRecoveryService>(data.filePath("recovery")));
        window.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);window.resize(1280,800);window.show();
        QVERIFY(window.workspaceManager->openWorkspace(workspace)); auto* nav=window.findChild<NavigationWidget*>();QVERIFY(nav);
        nav->setActiveTab(NavigationWidget::DesignTab);auto* view=tree(*nav);auto* model=qobject_cast<DesignHierarchyModel*>(view->model());
        QTRY_COMPARE_WITH_TIMEOUT(window.workspaceManager->getSystemVerilogFiles().size(),8,20000);
        QTRY_COMPARE_WITH_TIMEOUT(model->rowCount(),4,30000);
        QTRY_VERIFY_WITH_TIMEOUT(model->report()->rootModules.size()>=8,30000);
        for(const auto& name:names)QVERIFY(model->report()->rootModules.contains(name));
        QVERIFY(model->report()->rootModules.contains("top"));QVERIFY(!model->report()->rootModules.contains("branch"));
        const auto report=model->report();
        for(const auto& path:{"top.a.g[0].u","top.b.g[0].u","top.b.g[1].u","top.b.g[2].u","top.arr[0]","top.arr[1]"})
            QVERIFY2(report->nodesByPath.contains(path),path);
        QVERIFY(!report->nodesByPath.contains("top.a.g[1].u"));
        for(const auto& file:files)QVERIFY(window.tabManager->openFileInTab(file));
        QCOMPARE(window.tabManager->getOpenSystemVerilogFiles().size(),7);QCOMPARE(window.tabManager->workspaceSessionTabs(workspace).size(),7);
        // Opening documents schedules semantic publication independently of search.
        QTest::qWait(600);
        QTRY_VERIFY_WITH_TIMEOUT(!window.analysisScheduler->isSemanticAnalysisActive() && !model->applying()
            && model->report()->snapshotGeneration==SemanticIndex::getInstance()->snapshotRevision(),10000);
        QSignalSpy scans(window.workspaceManager.get(),&WorkspaceManager::filesScanned);
        QSignalSpy refresh(window.navigationManager.get(),&NavigationManager::dataRefreshed);
        search(*nav)->setText("suffix_module_6");QTRY_VERIFY(!view->property("designSearchPending").toBool());search(*nav)->clear();
        QCOMPARE(scans.size(),0);QCOMPARE(refresh.size(),0);
        QPair<QString,QString> simulation{hierarchy,"top"};
        window.navigationManager->setSimulationHandlers([&]{return simulation;},[&](const QString& file,const QString& name){simulation={file,name};return QString();});
        QVERIFY(nav->selectDesignInstance("top.a",true));clickName(view,view->currentIndex());
        QCOMPARE(simulation.second,"top");QVERIFY(nav->selectDesignModule(names.first(),files.first()));clickName(view,view->currentIndex());
        QCOMPARE(simulation.second,"top");QVERIFY(window.navigationManager->setDesignTop(names.first(),files.first()).isEmpty());
        QCOMPARE(simulation.second,"top");QVERIFY(!window.findChild<QAbstractItemView*>("sourceList"));
        capture(window,"mainwindow-automatic-design");
        const auto state=nav->designViewState(); nav->restoreDesignViewState(state);QCOMPARE(nav->designViewState()["selected"],state["selected"]);
        // Exercise the persisted view with the real workspace watcher. Search
        // must complete across repeated publications, then become quiescent.
        for (int iteration=0;iteration<8;++iteration) {
            QVERIFY(nav->selectDesignInstance("top.a",true));
            search(*nav)->setText("top.a");
            QTRY_VERIFY_WITH_TIMEOUT(!view->property("designSearchPending").toBool(),5000);
            QVERIFY(view->model()->rowCount()>0);
            search(*nav)->clear();
            QTest::qWait(300);
        }
        QTRY_VERIFY_WITH_TIMEOUT(!window.analysisScheduler->isSemanticAnalysisActive() && !model->applying(),10000);
        QTest::qWait(600);
        const auto stableRevision=SemanticIndex::getInstance()->snapshotRevision();
        const auto stableState=nav->designViewState();
        nav->restoreDesignViewState(stableState);
        QTest::qWait(600);
        QCOMPARE(SemanticIndex::getInstance()->snapshotRevision(),stableRevision);
        window.close();
    }
};

int main(int argc,char** argv) {
    QStandardPaths::setTestModeEnabled(true);MeasuredApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
    QCoreApplication::setOrganizationName("ZeroSlackDesignInlineTest");QCoreApplication::setApplicationName("ZeroSlackDesignInlineTest");
    QTemporaryDir profile;
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,profile.path());
    QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,profile.path());
    qputenv("ZEROSLACK_SIMDOCK_LEGACY_SETTINGS_PATH",profile.filePath("legacy.ini").toUtf8());
    UserTemplateService::getInstance()->setGlobalTemplateFilePath(profile.filePath("templates.json"));
    if(!initializeUiStyleForTest())return 2;
    ApplicationThemeManager::instance().setAnimationsEnabled(false);ApplicationThemeManager::instance().applyToApplication();
    DesignInlineSelectionTest test;return QTest::qExec(&test,argc,argv);
}
#include "design_inline_selection_test.moc"
