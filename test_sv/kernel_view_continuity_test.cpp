#include "signal_trace_test_fixture.h"
#include <QComboBox>
#include <QScrollBar>
#include <QTreeWidget>

class KernelViewContinuityTest final:public QObject {
    Q_OBJECT
private slots:
    void nativeBranchesKeepReadingPosition(){
        SignalTraceTestFixture fixture;
        QString source="module top(input wire seed);\n";
        for(int i=0;i<120;++i)source+=QString("wire n%1;assign n%1=seed;\n").arg(i);
        source+="endmodule\n";QVERIFY(fixture.build(source));
        SignalTracePanel panel;QVERIFY(fixture.show(panel,"seed"));
        panel.findChild<QComboBox*>("traceDirection")->setCurrentIndex(1);
        auto* tree=panel.traceTree(SignalTraceDirection::Downstream);auto* root=tree->topLevelItem(0);
        QVERIFY(root && root->childCount()>60);auto* selected=root->child(60);tree->setCurrentItem(selected);tree->scrollToItem(selected);
        QTRY_VERIFY(!panel.busy());const auto object=panel.selectedObject();const int scroll=tree->verticalScrollBar()->value();QVERIFY(scroll>0);
        const auto rowHeight=tree->visualItemRect(selected).height();QVERIFY(rowHeight>=tree->fontMetrics().height());
        LiveInsightRequestKey anotherEditor;anotherEditor.documentId="another.sv";anotherEditor.documentRevision=500;
        panel.setContext(fixture.context,anotherEditor);QCOMPARE(tree->topLevelItem(0),root);QCOMPARE(tree->currentItem(),selected);QCOMPARE(tree->verticalScrollBar()->value(),scroll);
        for(auto mode:{ThemeMode::Dark,ThemeMode::Light}){
            ApplicationThemeManager::instance().setMode(mode);panel.refreshThemePresentation();QCoreApplication::processEvents();
            QCOMPARE(tree->topLevelItem(0),root);QCOMPARE(panel.selectedObject().id(),object.id());QVERIFY(!panel.busy());
        }
        panel.showForSymbol("n0",fixture.file,"top");QTRY_VERIFY(!panel.busy());panel.goBack();QTRY_VERIFY(!panel.busy());
        QCOMPARE(panel.currentObject().symbol.symbolName,QString("seed"));QCOMPARE(panel.selectedObject().id(),object.id());
        QTRY_COMPARE(tree->verticalScrollBar()->value(),scroll);
        const auto state=panel.saveViewState();panel.clearTarget();panel.restoreViewState(state);QTRY_VERIFY(!panel.busy());
        QCOMPARE(panel.selectedObject().id(),object.id());QTRY_COMPARE(tree->verticalScrollBar()->value(),scroll);
    }
    void independentSiblingBranchesAndReconciliation(){
        SignalTraceTestFixture fixture;QVERIFY(fixture.build("module top(input wire s,output wire a,b,d);assign a=s;assign b=s;assign d=a|b;endmodule"));
        SignalTracePanel panel;QVERIFY(fixture.show(panel,"d"));auto* root=panel.traceTree(SignalTraceDirection::Upstream)->topLevelItem(0);
        QCOMPARE(root->childCount(),2);root->child(0)->setExpanded(true);root->child(1)->setExpanded(true);QTRY_VERIFY(!panel.busy());
        root->child(0)->setExpanded(false);QVERIFY(root->child(1)->isExpanded());QCOMPARE(root->child(1)->childCount(),1);
        panel.cancel();panel.refresh();QTRY_VERIFY(!panel.busy());QCOMPARE(panel.currentObject().symbol.symbolName,QString("d"));
        auto updated=fixture.context;updated.projectId="other";panel.setContext(updated);QVERIFY(!panel.currentObject().isValid());
    }
};
int main(int argc,char** argv){return runSignalTraceWidgetTest<KernelViewContinuityTest>(argc,argv);}
#include "kernel_view_continuity_test.moc"
