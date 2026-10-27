#include "signal_trace_test_fixture.h"
#include <QClipboard>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QAction>
#include <QTableView>
#include <QTreeWidget>

class SignalUsageHotspotPanelTest final:public QObject {
    Q_OBJECT
private slots:
    void heatmapSelectionCopyExportAndWholeFileDistribution(){
        SignalTraceTestFixture fixture;
        const auto source=QString("module top(input wire s,c,output logic a);\nalways_comb begin a=s; if(c) a=s; end\nendmodule\n")+QString(400,'\n');
        QVERIFY(fixture.build(source));SignalTracePanel panel;QVERIFY(fixture.show(panel,"s"));panel.setHeatmapMode(true);
        auto* model=panel.heatModel();QCOMPARE(model->rowCount(),1);QCOMPARE(model->data(model->index(0,SignalTraceHeatModel::Read)).toInt(),2);
        const auto references=model->references();QCOMPARE(references.size(),2);QCOMPARE(references.first().fileLineCount,404);
        QVERIFY(references[0].range.column!=references[1].range.column);
        model->appendReferences(references);QCOMPARE(model->data(model->index(0,SignalTraceHeatModel::Total)).toInt(),2);
        panel.heatTable()->setCurrentIndex(model->index(0,SignalTraceHeatModel::Read));
        QCOMPARE(panel.findChild<QTreeWidget*>("traceReferences")->topLevelItemCount(),2);
        panel.findChild<QAction*>("traceCopy")->trigger();QCOMPARE(QApplication::clipboard()->text().toUtf8(),model->csv());
        const auto path=fixture.directory.filePath("heat.csv");QVERIFY(panel.exportHeatmap(path));QFile csv(path);QVERIFY(csv.open(QIODevice::ReadOnly));QCOMPARE(csv.readAll(),model->csv());
        panel.setFocusSearchText("missing");QCOMPARE(model->rowCount(),0);panel.setFocusSearchText("a=s");QCOMPARE(model->rowCount(),1);
        model->setComplete(false);QCOMPARE(model->headerData(SignalTraceHeatModel::Total,Qt::Horizontal).toString(),QString("Loaded total"));
        QVERIFY(model->csv().contains("Partial: loaded reference pages only"));
    }
    void markersClearAndStaleResultsCannotExport(){
        SignalTraceTestFixture fixture;QVERIFY(fixture.build("module top(input wire s,output wire a);assign a=s;endmodule\n"));
        SignalTracePanel panel;int marked=0;panel.setMarkerHandler([&](const QList<SignalTraceEvidence>& values){marked=values.size();});
        QVERIFY(fixture.show(panel,"s"));QVERIFY(marked>0);panel.hide();QCOMPARE(marked,0);panel.show();QTRY_VERIFY(!panel.busy());QVERIFY(marked>0);
        auto changed=fixture.context;changed.currentSourceHashes[fixture.file]=SemanticSignalRoleEvidence::sourceHash("different");panel.setContext(changed);
        QCOMPARE(marked,0);QString error;QVERIFY(!panel.exportHeatmap(fixture.directory.filePath("stale.csv"),&error));QVERIFY(!error.isEmpty());
        QVERIFY(!panel.exportGraph(fixture.directory.filePath("stale.svg")).success);
    }
    void legacyTrackMigratesToFileDistribution(){
        SignalTraceTestFixture fixture;QVERIFY(fixture.build("module top(input wire s,output wire a);assign a=s;endmodule\n"));
        SignalTracePanel panel;panel.setContext(fixture.context);panel.show();
        panel.restoreViewState({{"hotspotStateVersion",1},{"signal","s"},{"file",fixture.file},{"module","top"},{"matrix",false},{"search","assign"}});
        QTRY_VERIFY(!panel.busy());QCOMPARE(panel.currentObject().symbol.symbolName,QString("s"));
        QCOMPARE(panel.findChild<QComboBox*>("traceMode")->currentText(),QString("Heatmap"));QCOMPARE(panel.focusSearchText(),QString("assign"));
        QVERIFY(!panel.findChild<QWidget*>("signalUsageHotspotTrackView"));QVERIFY(panel.heatModel()->rowCount()>0);
    }
};
int main(int argc,char** argv){return runSignalTraceWidgetTest<SignalUsageHotspotPanelTest>(argc,argv);}
#include "signal_usage_hotspot_panel_test.moc"
