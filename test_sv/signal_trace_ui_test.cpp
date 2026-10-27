#include "signaltracepanel.h"
#include "signaltracemodels.h"
#include "mainwindow.h"
#include "workspacemanager.h"
#include "workspacesessioncoordinator.h"
#include "tabmanager.h"
#include "mycodeeditor.h"
#include "contextworkspacecontroller.h"
#include "liveinsightscontextprovider.h"
#include "liveinsighttoolpage.h"
#include "rtlinsightworkbench.h"
#include "actionregistry.h"
#include "navigationmanager.h"
#include "testuistyle.h"
#include "applicationthememanager.h"
#include <zeroslack/semantic/incrementalsemanticanalysisworker.h>
#include <zeroslack/semantic/semanticindexsnapshot.h>
#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QLabel>
#include <QTemporaryDir>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QTest>
#include <QTreeWidget>
#include <QTableView>
#include <QScrollBar>
#include <QScopeGuard>
#include <QPushButton>
#include <QMainWindow>
#include <QAction>
#include <QToolButton>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsItem>

class SignalTraceUiTest final:public QObject {
    Q_OBJECT
    QTemporaryDir temporary;
    QString file;
    quint64 revision=1;
    SignalTraceContext build(const QString& source) {
        file=temporary.filePath("trace.sv");QFile out(file);if(!out.open(QIODevice::WriteOnly))return {};out.write(source.toUtf8());out.close();
        SemanticAnalysisRequest request;request.generation=++revision;request.computationRevision=revision;
        request.reason=SemanticAnalysisReason::WorkspaceOpen;request.impactHint=SemanticChangeImpact::WorkspaceConfig;
        request.project.workspaceRoot=temporary.path();request.project.systemVerilogFiles={file};request.project.allFiles={file};request.project.topModule="top";
        request.changedFiles={file};request.sourceOverrides={{file,source}};
        auto result=IncrementalSemanticAnalysisWorker::analyze(request,{}, {},[]{return false;});
        return {temporary.path(),"main",{}, {result.preparedSnapshot,revision}};
    }
    const QString design="module top(input logic s,c, output wire a,b,d);\nassign a=s;\nassign b=s;\nassign d=a|b;\nendmodule\n";
    const QString feedbackDesign=
        "module leaf(input wire d, output wire q);\n  assign q = d;\nendmodule\n\n"
        "module top(input logic clk, enable, source, output wire done);\n  wire a, b;\n  logic registered;\n  wire loop_a, loop_b;\n"
        "  leaf left(.d(source), .q(a));\n  leaf right(.d(source), .q(b));\n  assign done = a | b;\n"
        "  always_ff @(posedge clk)\n    if (enable) registered <= done;\n  assign loop_a = ~loop_b;\n  assign loop_b = loop_a;\nendmodule\n";
private slots:
    void modeRoundTripOwnsEvidenceAndLateResults(){
        auto context=build(feedbackDesign);QList<SignalTraceEvidence> marked;
        SignalTracePanel panel;panel.resize(760,300);panel.setContext(context);panel.show();
        panel.setMarkerHandler([&](const auto& values){marked=values;});
        panel.showForSymbol("done",file,"top");QTRY_VERIFY(!panel.busy());
        auto* tree=panel.traceTree(SignalTraceDirection::Upstream);auto* root=tree->topLevelItem(0);QVERIFY(root && root->childCount()==2);
        root->child(0)->setExpanded(true);tree->setCurrentItem(root);QTRY_VERIFY(!panel.busy());
        auto* branch=root->child(0);QVERIFY(branch->childCount()>0);const auto selected=panel.selectedObject().id();
        panel.setHeatmapMode(true);auto* table=panel.heatTable();auto* heat=panel.heatModel();
        const auto read=heat->index(0,SignalTraceHeatModel::Read);table->setCurrentIndex(read);
        QCOMPARE(marked.size(),1);QCOMPARE(marked.first().role,SignalTraceRole::Read);QCOMPARE(marked.first().range.line,13);QCOMPARE(marked.first().range.column,31);
        const auto tableSelection=table->currentIndex();const int scroll=table->verticalScrollBar()->value();
        // Leave while the real feedback query is still queued: its result must not own Trace.
        panel.analyzeFeedback();panel.setHeatmapMode(false);QTRY_VERIFY(!panel.busy());
        QCOMPARE(panel.selectedObject().id(),selected);QCOMPARE(tree->topLevelItem(0),root);QVERIFY(branch->isExpanded());
        QCOMPARE(marked.size(),2);QVERIFY(std::any_of(marked.begin(),marked.end(),[](const auto& ref){return ref.role==SignalTraceRole::Read && ref.range.line==13 && ref.range.column==31;}));
        QVERIFY(std::none_of(marked.begin(),marked.end(),[](const auto& ref){return ref.role==SignalTraceRole::Write && ref.range.line==13;}));
        auto* mode=panel.findChild<QComboBox*>("traceMode");mode->setCurrentIndex(2);QVERIFY(!panel.busy());
        auto* cycles=panel.findChild<QComboBox*>("traceCycles");int sequential=-1;
        for(int i=0;i<cycles->count();++i)if(cycles->itemText(i).startsWith("Sequential"))sequential=i;
        QVERIFY(sequential>=0);cycles->setCurrentIndex(sequential);
        QVERIFY(!marked.isEmpty());for(const auto& ref:marked){QCOMPARE(ref.role,SignalTraceRole::Write);QCOMPARE(ref.range.line,13);QCOMPARE(ref.range.column,17);}
        panel.setHeatmapMode(true);QCOMPARE(table->currentIndex(),tableSelection);QCOMPARE(table->verticalScrollBar()->value(),scroll);
        QCOMPARE(marked.size(),1);QCOMPARE(marked.first().role,SignalTraceRole::Read);QCOMPARE(marked.first().range.column,31);
        panel.setHeatmapMode(false);QCOMPARE(panel.selectedObject().id(),selected);QCOMPARE(marked.size(),2);
        QVERIFY(panel.findChild<QLabel*>("traceStatus")->text().isEmpty());
        auto* direction=panel.findChild<QComboBox*>("traceDirection");direction->setCurrentIndex(1);QTRY_VERIFY(!panel.busy());
        auto* down=panel.traceTree(SignalTraceDirection::Downstream);QVERIFY(down->topLevelItem(0)->childCount()>0);
        down->setCurrentItem(down->topLevelItem(0)->child(0));QTRY_VERIFY(!panel.busy());
        QCOMPARE(panel.selectedObject().symbol.symbolName,QString("registered"));QCOMPARE(marked.first().range.column,17);
        direction->setCurrentIndex(0);QTRY_VERIFY(!panel.busy());QCOMPARE(panel.selectedObject().id(),selected);
        QCOMPARE(tree->topLevelItem(0),root);QVERIFY(branch->isExpanded());
        QVERIFY(std::any_of(marked.begin(),marked.end(),[](const auto& ref){return ref.role==SignalTraceRole::Read && ref.range.column==31;}));
        panel.analyzeFeedback();panel.cancel();QVERIFY(!panel.busy());
        auto* status=panel.findChild<QLabel*>("traceStatus");
        QVERIFY(status->text().contains("Cancelled"));QVERIFY(!status->text().contains("Analyzing"));
        panel.analyzeFeedback();QTRY_VERIFY(!panel.busy());QVERIFY(status->text().isEmpty());
    }
    void compactViewAndOnDemandReferences(){
        auto context=build(design);SignalTracePanel panel;panel.resize(700,240);panel.setContext(context);panel.show();
        panel.showForSymbol("d",file,"top");QTRY_VERIFY(!panel.busy());QApplication::processEvents();
        auto* tree=panel.traceTree(SignalTraceDirection::Upstream);auto* details=panel.findChild<QTreeWidget*>("traceReferences");
        auto* button=panel.findChild<QToolButton*>("traceDetails");QVERIFY(button);
        QVERIFY(!details->isVisible());QVERIFY(tree->height()>panel.height()*0.70);
        QVERIFY(!panel.findChild<QToolButton*>("traceCancel")->isVisible());QVERIFY(!panel.findChild<QLabel*>("traceStatus")->isVisible());
        QVERIFY(tree->topLevelItem(0)->text(2).isEmpty());QVERIFY(tree->topLevelItem(0)->child(0)->text(2).isEmpty());
        for(const auto& name:{"traceSave","traceOpen","traceCopy","traceRefresh","traceAnalyze","tracePin"})QVERIFY(panel.findChild<QAction*>(name));
        auto* root=tree->topLevelItem(0);button->click();QVERIFY(details->isVisible());QVERIFY(!tree->isVisible());QCOMPARE(button->text(),QString("Back"));
        button->click();QVERIFY(tree->isVisible());QVERIFY(!details->isVisible());QCOMPARE(tree->topLevelItem(0),root);
        panel.setHeatmapMode(true);auto* table=panel.heatTable();QVERIFY(table->height()>panel.height()*0.55);
        panel.focusInspector();QVERIFY(details->isVisible());button->click();QVERIFY(table->isVisible());
        auto changed=context;changed.currentSourceHashes[file]=SemanticSignalRoleEvidence::sourceHash("changed");panel.setContext(changed);
        QVERIFY(panel.findChild<QLabel*>("traceStatus")->isVisible());QVERIFY(panel.findChild<QLabel*>("traceStatus")->text().contains("changed",Qt::CaseInsensitive));
    }
    void feedbackEdgesAvoidLabelsAndFitNeverEnlarges(){
        auto context=build(feedbackDesign);SignalTracePanel panel;panel.resize(1600,900);panel.setContext(context);panel.show();
        panel.showForSymbol("done",file,"top");QTRY_VERIFY(!panel.busy());panel.analyzeFeedback();QTRY_VERIFY(!panel.busy());
        auto* cycles=panel.findChild<QComboBox*>("traceCycles");auto* view=panel.findChild<QGraphicsView*>("traceCycleView");
        QCOMPARE(cycles->count(),2);
        for(int cycle=0;cycle<cycles->count();++cycle){
            cycles->setCurrentIndex(cycle);QApplication::processEvents();
            QList<QRectF> labels;QList<QGraphicsPathItem*> edges;
            for(auto* item:view->scene()->items()){
                if(auto* label=qgraphicsitem_cast<QGraphicsTextItem*>(item))labels<<label->sceneBoundingRect();
                if(auto* edge=qgraphicsitem_cast<QGraphicsPathItem*>(item);edge && !edge->data(1).toString().isEmpty())edges<<edge;
            }
            QVERIFY(!labels.isEmpty());QVERIFY(!edges.isEmpty());
            for(auto* edge:edges)for(int sample=0;sample<=100;++sample)for(const auto& label:labels)
                QVERIFY2(!label.contains(edge->path().pointAtPercent(sample/100.0)),"Feedback edge crosses a node label");
            for(const auto& size:{QSize(1600,900),QSize(700,240),QSize(1071,600)}){
                panel.resize(size);QApplication::processEvents();panel.focusFit();
                QVERIFY(view->transform().m11()<=1.001);
                for(const auto& label:labels)QVERIFY(view->viewport()->rect().adjusted(-2,-2,2,2).contains(view->mapFromScene(label).boundingRect()));
            }
        }
    }
    void mainWindowWorkflow() {
        build(design);
        MainWindow window;window.resize(1200,800);window.show();
        window.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
        auto* sessions=window.findChild<WorkspaceSessionCoordinator*>();QVERIFY(sessions);
        QVERIFY(sessions->openWorkspace(temporary.path()));QVERIFY(window.revealSuiteSource(file));
        QTRY_VERIFY_WITH_TIMEOUT(SemanticIndex::getInstance()->snapshotToken().snapshot
            && !SemanticIndex::getInstance()->snapshotToken().snapshot->getSymbolRecordsByName("d",file).isEmpty(),20000);
        const auto issue=window.navigationManager->setDesignTop("top",file);QVERIFY2(issue.isEmpty(),qPrintable(issue));
        auto* contexts=window.findChild<ContextWorkspaceController*>();QVERIFY(contexts);
        const QVariantMap target{{"fileName",file},{"moduleName","top"},{"signalName","d"}};
        QString error;
        QVERIFY2(contexts->openResource(LiveInsightsContextProvider::resourceForKind(LiveInsightKind::Kernel,temporary.path(),{{"target",target}}),
            {ContextSurface::Docked,ContextPersistence::Kept,ContextBinding::Global},&error),qPrintable(error));
        auto* panel=window.findChild<SignalTracePanel*>();QVERIFY(panel);
        QTRY_VERIFY_WITH_TIMEOUT(panel->currentObject().isConcrete() && !panel->busy(),20000);
        QCOMPARE(panel->currentObject().symbol.symbolName,QString("d"));
        panel->expandCone(2);QTRY_VERIFY_WITH_TIMEOUT(!panel->busy(),10000);panel->setHeatmapMode(true);
        QVERIFY(panel->heatModel()->rowCount()>0);
        auto* references=panel->findChild<QTreeWidget*>("traceReferences");QVERIFY(references && references->topLevelItemCount()>0);
        references->itemActivated(references->topLevelItem(0),0);
        QTRY_VERIFY(window.tabManager->getCurrentEditor());
        auto* editor=window.tabManager->getCurrentEditor();const auto original=editor->toPlainText();
        QTextCursor cursor=editor->textCursor();cursor.movePosition(QTextCursor::End);editor->setTextCursor(cursor);editor->insertPlainText("// pending edit\n");
        QTRY_VERIFY_WITH_TIMEOUT(panel->findChild<QLabel*>("traceStatus")->text().contains("changed",Qt::CaseInsensitive)
            || panel->findChild<QLabel*>("traceStatus")->text().contains("snapshot",Qt::CaseInsensitive),15000);
        editor->undo();QCOMPARE(editor->toPlainText(),original);
        QTRY_VERIFY_WITH_TIMEOUT(SemanticIndex::getInstance()->snapshotToken().snapshot->cachedFileSource(file).text==original,20000);
        panel->refresh();QTRY_VERIFY_WITH_TIMEOUT(!panel->busy(),10000);panel->pinSelection();
        const auto path=temporary.filePath("main-window.ztrace");QVERIFY(panel->saveSession(path));QVERIFY(panel->openSession(path));QTRY_VERIFY_WITH_TIMEOUT(!panel->busy(),10000);
        panel->analyzeFeedback();QTRY_VERIFY_WITH_TIMEOUT(!panel->busy(),10000);
        ActionInvocation exportRequest;exportRequest.parameters={{"outputPath",temporary.filePath("main-heat.csv")}};
        const auto* exportAction=findActionById("signal.trace.exportHeatmap");QVERIFY(exportAction);
        const auto exported=window.executeRegisteredUiAction(exportAction->id,exportRequest.parameters);QVERIFY2(exported.succeeded,qPrintable(exported.failureReason));
        const auto* retired=findActionById(ActionIds::GraphExportUsageHotspotTrack);QVERIFY(retired);QVERIFY(retired->aliases.isEmpty());
        const auto migration=window.executeRegisteredUiAction(retired->id,exportRequest.parameters);QVERIFY(migration.handled && !migration.succeeded);
        QCOMPARE(migration.output.value("schema").toString(),QString("zeroslack-export-migration/v1"));
        const QString evidence=qEnvironmentVariable("ZEROSLACK_QT_TEST_LOG_ROOT");if(!evidence.isEmpty()){QDir().mkpath(evidence);QVERIFY(window.grab().save(evidence+"/mainwindow-trace.png"));}
        QVERIFY(contexts->openResource(LiveInsightsContextProvider::resourceForKind(LiveInsightKind::Module,temporary.path()),
            {ContextSurface::Docked,ContextPersistence::Kept,ContextBinding::Global},&error));
        window.close();
    }
    void lazyBranchesHistoryPinsAndStaleEdits() {
        auto context=build(design);QVERIFY(context.token.snapshot);
        SignalTracePanel panel;panel.resize(1100,650);panel.setContext(context);panel.show();panel.showForSymbol("d",file,"top");
        QTRY_VERIFY_WITH_TIMEOUT(!panel.busy(),10000);
        auto* tree=panel.traceTree(SignalTraceDirection::Upstream);QCOMPARE(tree->topLevelItemCount(),1);
        auto* root=tree->topLevelItem(0);QCOMPARE(root->childCount(),2);
        root->child(0)->setExpanded(true);root->child(1)->setExpanded(true);QTRY_VERIFY_WITH_TIMEOUT(!panel.busy(),10000);
        QCOMPARE(root->child(0)->childCount(),1);QCOMPARE(root->child(1)->childCount(),1);
        tree->setCurrentItem(root->child(0)->child(0));QTRY_VERIFY_WITH_TIMEOUT(!panel.busy(),10000);panel.pinSelection();
        const QString session=temporary.filePath("path.ztrace");QString error;QVERIFY2(panel.saveSession(session,&error),qPrintable(error));
        panel.showForSymbol("a",file,"top");QTRY_VERIFY_WITH_TIMEOUT(!panel.busy(),10000);panel.goBack();QTRY_VERIFY_WITH_TIMEOUT(!panel.busy(),10000);
        QCOMPARE(panel.currentObject().symbol.symbolName,QString("d"));panel.goForward();QTRY_VERIFY_WITH_TIMEOUT(!panel.busy(),10000);QCOMPARE(panel.currentObject().symbol.symbolName,QString("a"));
        QVERIFY(panel.openSession(session,&error));QTRY_VERIFY_WITH_TIMEOUT(!panel.busy(),10000);panel.openPin(0);QTRY_VERIFY_WITH_TIMEOUT(!panel.busy(),10000);
        QCOMPARE(panel.currentObject().symbol.symbolName,QString("d"));QCOMPARE(panel.selectedObject().symbol.symbolName,QString("s"));
        context=build(design+"// edited\n");panel.setContext(context);QVERIFY(!panel.busy());
        QVERIFY(panel.findChild<QLabel*>("traceStatus")->text().contains("Source changed"));
        panel.openPin(0);QTRY_VERIFY_WITH_TIMEOUT(!panel.busy(),10000);QVERIFY(panel.findChild<QLabel*>("traceStatus")->text().contains("Source changed"));
        panel.refresh();QTRY_VERIFY_WITH_TIMEOUT(!panel.busy(),10000);QVERIFY(panel.traceTree(SignalTraceDirection::Upstream)->topLevelItemCount()==1);
    }
    void heatmapUsesOneFilteredModel() {
        auto context=build(design);SignalTracePanel panel;panel.setContext(context);panel.show();panel.showForSymbol("s",file,"top");
        QTRY_VERIFY_WITH_TIMEOUT(!panel.busy(),10000);panel.setHeatmapMode(true);
        auto* model=panel.heatModel();QCOMPARE(model->rowCount(),1);QCOMPARE(model->data(model->index(0,SignalTraceHeatModel::Read)).toInt(),2);
        QCOMPARE(model->data(model->index(0,SignalTraceHeatModel::Write)).toInt(),0);QCOMPARE(model->maximumCount(),2);
        const auto before=model->csv();panel.setFocusSearchText("assign a");QCOMPARE(model->references().size(),1);QCOMPARE(model->maximumCount(),1);
        QVERIFY(model->csv()!=before);QCOMPARE(model->data(model->index(0,SignalTraceHeatModel::Total)).toInt(),1);
        const QString path=temporary.filePath("heat.csv");QVERIFY(panel.exportHeatmap(path));QFile saved(path);QVERIFY(saved.open(QIODevice::ReadOnly));QCOMPARE(saved.readAll(),model->csv());
        panel.setFocusSearchText("absent");QCOMPARE(model->rowCount(),0);
    }
    void contextSwitchPreservesReadingAndSyncDoesNotRebase(){
        auto context=build(design);SignalTraceService service(context);
        SignalTracePanel left,right;left.resize(1000,600);right.resize(1000,600);
        for(auto* panel:{&left,&right}){panel->setContext(context);panel->show();panel->showForSymbol("d",file,"top");panel->expandCone(2);}
        QTRY_VERIFY_WITH_TIMEOUT(!left.busy() && !right.busy(),10000);
        int notifications=0;left.setSelectionHandler([&](const SignalTraceObject& object){++notifications;right.syncSelection(object);});
        right.setSelectionHandler([&](const SignalTraceObject& object){++notifications;left.syncSelection(object);});
        auto* tree=left.traceTree(SignalTraceDirection::Upstream);auto* root=tree->topLevelItem(0);
        root->child(0)->setExpanded(true);QTRY_VERIFY(!left.busy());tree->setCurrentItem(root->child(0));QTRY_VERIFY(!left.busy() && !right.busy());
        QCOMPARE(notifications,1);QCOMPARE(left.selectedObject().objectPath,right.selectedObject().objectPath);
        QCOMPARE(right.currentObject().symbol.symbolName,QString("d"));
        LiveInsightRequestKey another;another.documentId="other-editor";another.documentRevision=99;
        left.setContext(context,another);QCOMPARE(tree->topLevelItem(0),root);QVERIFY(root->child(0)->isExpanded());
        const auto selected=left.selectedObject().id();left.showForSymbol("s",file,"top");QTRY_VERIFY(!left.busy());left.goBack();QTRY_VERIFY(!left.busy());
        QCOMPARE(left.currentObject().symbol.symbolName,QString("d"));QCOMPARE(left.selectedObject().id(),selected);
    }
    void linkedDetachedMigrationAndDistributionNavigation(){
        auto context=build(design);auto* index=SemanticIndex::getInstance();const auto old=index->snapshot();
        const auto restore=qScopeGuard([&]{index->setSnapshot(old);});index->setSnapshot(context.token.snapshot);
        LiveInsightToolPage page(LiveInsightKind::Hotspot);page.resize(1100,650);page.show();
        LiveInsightToolContext target;target.workspaceId=context.workspaceId;target.projectId=context.projectId;
        target.fileName=target.documentId=file;target.documentText=design;target.moduleName="top";target.signalName="s";target.documentRevision=5;
        page.setContext(target);auto* panel=page.traceSurface();QVERIFY(panel);QTRY_VERIFY(!panel->busy());QCOMPARE(panel->heatModel()->rowCount(),1);
        panel->restoreViewState({{"signal","s"},{"file",file},{"module","top"},{"matrix",true},{"search","assign"}});QTRY_VERIFY(!panel->busy());
        QCOMPARE(panel->focusSearchText(),QString("assign"));QCOMPARE(panel->heatModel()->references().size(),2);
        int line=0;bool sameFile=false;page.setNavigationHandler([&](const QString& path,int row,int){sameFile=path==file;line=row;return true;});
        auto* table=panel->heatTable();const auto cell=table->model()->index(0,SignalTraceHeatModel::Distribution);
        const auto rect=table->visualRect(cell);QVERIFY(rect.isValid());QTest::mouseDClick(table->viewport(),Qt::LeftButton,Qt::NoModifier,QPoint(rect.right()-10,rect.center().y()));
        QCOMPARE(line,3);QVERIFY(sameFile);
        panel->pinSelection();auto* detached=page.detachToWindow();QVERIFY(detached);auto* detachedPanel=detached->findChild<SignalTracePanel*>();QVERIFY(detachedPanel);
        QTRY_VERIFY(!detachedPanel->busy());QCOMPARE(detachedPanel->currentObject().id(),panel->currentObject().id());
        QCOMPARE(detachedPanel->saveViewState().value("pins"),panel->saveViewState().value("pins"));
        auto older=target;older.documentRevision=4;page.setContext(older);QCOMPARE(page.contextForTest().documentRevision,quint64(5));
        target.projectId="other-project";page.setContext(target);QTRY_VERIFY(!panel->busy() && !detachedPanel->busy());
        QCOMPARE(detachedPanel->currentObject().projectId,QString("other-project"));
        detached->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        LiveInsightToolPage module(LiveInsightKind::Module);module.setContext(target);int openings=0;
        module.setDesignHandler([&]{++openings;});auto* button=module.findChild<QPushButton*>("openExistingDesign");QVERIFY(button && button->isEnabled());button->click();QCOMPARE(openings,1);
    }
    void cancellationSwitchAndDestruction() {
        QString source="module top(input wire s);\n";for(int i=0;i<500;++i)source+=QString("wire n%1;assign n%1=s;\n").arg(i);source+="endmodule\n";
        auto context=build(source);auto* panel=new SignalTracePanel;panel->setContext(context);panel->show();panel->showForSymbol("s",file,"top");panel->cancel();
        context.projectId="other";panel->setContext(context);QCoreApplication::processEvents();QVERIFY(!panel->currentObject().isValid());
        panel->showForSymbol("s",file,"top");panel->expandCone(8);delete panel;QCoreApplication::processEvents();
    }
    void boundedConeAndFeedback() {
        auto context=build(design);SignalTracePanel panel;panel.setContext(context);panel.show();panel.showForSymbol("d",file,"top");QTRY_VERIFY_WITH_TIMEOUT(!panel.busy(),10000);
        panel.expandCone(2);QTRY_VERIFY_WITH_TIMEOUT(!panel.busy(),10000);
        auto* root=panel.traceTree(SignalTraceDirection::Upstream)->topLevelItem(0);QVERIFY(root->child(0)->isExpanded());QVERIFY(root->child(1)->isExpanded());
        context=build("module top(output wire a,b); assign a=~b;assign b=~a;endmodule\n");panel.setContext(context);panel.showForSymbol("a",file,"top");QTRY_VERIFY_WITH_TIMEOUT(!panel.busy(),10000);
        panel.analyzeFeedback();QTRY_VERIFY_WITH_TIMEOUT(!panel.busy(),10000);QCOMPARE(panel.findChild<QComboBox*>("traceCycles")->count(),1);
    }
};
int main(int argc,char** argv) {
    QStandardPaths::setTestModeEnabled(true);QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
    QTemporaryDir profile;if(!profile.isValid())return 2;
    QCoreApplication::setApplicationName("ZeroSlack");QCoreApplication::setOrganizationName("ZeroSlack");
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,profile.path());
    QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,profile.path());
    qputenv("ZEROSLACK_SESSION_STORAGE_PATH",profile.filePath("session.ini").toUtf8());
    qputenv("ZEROSLACK_EDITING_TIME_STORAGE_PATH",profile.filePath("time.json").toUtf8());
    qputenv("ZEROSLACK_SIMDOCK_LEGACY_SETTINGS_PATH",profile.filePath("legacy.ini").toUtf8());
    if(!initializeUiStyleForTest())return 3;ApplicationThemeManager::instance().applyToApplication();
    SignalTraceUiTest test;return QTest::qExec(&test,argc,argv);
}
#include "signal_trace_ui_test.moc"
