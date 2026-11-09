#include "narrow_layout_fixture.h"
#include "compactlayout.h"
#include "insightvisualstyle.h"
#include "applicationthememanager.h"
#include "testuistyle.h"
#include "signaltracemodels.h"
#include "liveinsighttoolpage.h"
#include "liveinsightscontextview.h"
#include "liveinsightscontextprovider.h"
#include "liveinsightgraphreport.h"
#include "live_insight_test_utils.h"
#include "rtlinsightworkbench.h"
#include "contextdockhost.h"
#include "contextworkspacecontroller.h"
#include "mainwindow.h"
#include "analysisscheduler.h"
#include "crashrecoveryservice.h"
#include "navigationmanager.h"
#include "navigationpanecoordinator.h"
#include "navigationwidget.h"
#include "panellayoutcontroller.h"
#include "semanticdockcoordinator.h"
#include "problemspanelcoordinator.h"
#include "scopedsearchpanel.h"
#include "rtlhighriskeditpanel.h"
#include "instancepairconnectionpanel.h"
#include "activitylogservice.h"
#include "tabmanager.h"
#include "editorsplitcontroller.h"
#include "projectmodel.h"
#include "usertemplateservice.h"
#include "workspacemanager.h"
#include "workspacesessioncoordinator.h"
#ifdef ZEROSLACK_ENABLE_ELA
#include "ElaNavigationBar.h"
#endif
#include "../src/simulation/simdock/ui/simulationeventspanel.h"
#include "../src/simulation/simdock/ui/workbench.h"
#include <QtTest>
#include <QAbstractItemView>
#include <QComboBox>
#include <QElapsedTimer>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTableView>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <QScopeGuard>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>

namespace {
template<class T>T* owner(QObject* root){
    for(auto* object:root->findChildren<QObject*>())if(auto* value=dynamic_cast<T*>(object))return value;
    return nullptr;
}
QRect inRoot(QWidget* widget,QWidget* root){return {widget->mapTo(root,QPoint()),widget->size()};}
QString label(QWidget* widget){return widget->objectName().isEmpty()?QString::fromLatin1(widget->metaObject()->className()):widget->objectName();}
QJsonArray strings(const QStringList& values){QJsonArray result;for(const auto& value:values)result.append(value);return result;}

struct MeasurementCosts {
    qint64 measureNs = 0, recordNs = 0, grabNs = 0, saveNs = 0;
    int measurements = 0, images = 0, waits = 0;
    qint64 waitNs = 0, requestedWaitNs = 0;
    QJsonObject json() const {
        return {{"measureNs", measureNs}, {"recordNs", recordNs}, {"grabNs", grabNs},
            {"saveNs", saveNs}, {"measurements", measurements}, {"images", images},
            {"waitNs", waitNs}, {"requestedWaitNs", requestedWaitNs}, {"waits", waits}};
    }
} measurementCosts;

class ImageEvidenceWriter {
    struct Result { bool saved; qint64 nanoseconds; };
    QThreadPool pool;
    QFuture<Result> pending;
    QString pendingPath;
    QString failure;
public:
    qint64 saveNs = 0;
    ImageEvidenceWriter() { pool.setMaxThreadCount(1); }
    ~ImageEvidenceWriter() { finish(); }
    bool finish() {
        if (!pendingPath.isEmpty()) {
            const auto result = pending.result();
            saveNs += result.nanoseconds;
            if (!result.saved) failure = "Cannot save screenshot: " + pendingPath;
            pendingPath.clear();
        }
        return failure.isEmpty();
    }
    QString error() const { return failure; }
    bool enqueue(QImage image, const QString& path) {
        if (!finish()) return false;
        pendingPath = path;
        pending = QtConcurrent::run(&pool, [image = std::move(image), path] {
            QElapsedTimer timer; timer.start();
            const bool saved = image.save(path);
            return Result{saved, timer.nsecsElapsed()};
        });
        return true;
    }
};
std::unique_ptr<ImageEvidenceWriter> imageEvidence;


struct WaitCost {
    QElapsedTimer timer;
    explicit WaitCost(int milliseconds) {
        timer.start();
        measurementCosts.requestedWaitNs += qint64(milliseconds) * 1000000;
        ++measurementCosts.waits;
    }
    ~WaitCost() { measurementCosts.waitNs += timer.nsecsElapsed(); }
};

struct PhaseCost {
    QString phase;
    QElapsedTimer timer;
    qint64 checkpointNs = 0;
    void checkpoint(const QString& part) {
        const qint64 now = timer.nsecsElapsed();
        const QJsonObject row{{"phase", phase + "/" + part}, {"data", QTest::currentDataTag()},
            {"elapsedNs", now - checkpointNs}};
        checkpointNs = now;
        QFile output(qEnvironmentVariable("ZEROSLACK_TEST_ARTIFACT_DIR") + "/narrow-phase-costs.jsonl");
        if (output.open(QIODevice::Append))
            output.write(QJsonDocument(row).toJson(QJsonDocument::Compact) + '\n');
    }
    qint64 waitStart = measurementCosts.waitNs;
    qint64 recordStart = measurementCosts.recordNs;
    explicit PhaseCost(QString value) : phase(std::move(value)) { timer.start(); }
    ~PhaseCost() {
        const QJsonObject row{{"phase", phase}, {"data", QTest::currentDataTag()},
            {"elapsedNs", timer.nsecsElapsed()},
            {"waitNs", measurementCosts.waitNs - waitStart},
            {"recordNs", measurementCosts.recordNs - recordStart}};
        QFile output(qEnvironmentVariable("ZEROSLACK_TEST_ARTIFACT_DIR") + "/narrow-phase-costs.jsonl");
        if (output.open(QIODevice::Append))
            output.write(QJsonDocument(row).toJson(QJsonDocument::Compact) + '\n');
    }
};

struct Measurement {
    QJsonArray controls,views;
    QStringList failures;
};
QRect reachableViewport(QWidget* widget,QWidget* root,QJsonArray* ancestors=nullptr)
{
    QList<QPair<QScrollBar*,int>> positions;
    const auto restore=qScopeGuard([&]{for(auto it=positions.crbegin();it!=positions.crend();++it)it->first->setValue(it->second);});
    for(auto* parent=widget->parentWidget();parent && parent!=root;parent=parent->parentWidget())
        if(auto* area=qobject_cast<QScrollArea*>(parent);area && area->horizontalScrollBar()->maximum()==0){
            positions.append({area->verticalScrollBar(),area->verticalScrollBar()->value()});
            area->ensureWidgetVisible(widget,0,0);
            // Reach the whole control, including the frame around an input cursor.
            const QRect bounds(widget->mapTo(area->viewport(),QPoint()),widget->size());
            if(bounds.height()<=area->viewport()->height()){
                const int delta=bounds.top()<0?bounds.top():qMax(0,bounds.bottom()-area->viewport()->height()+1);
                area->verticalScrollBar()->setValue(area->verticalScrollBar()->value()+delta);
            }
        }
    QRect visible=inRoot(widget,root);
    for(auto* parent=widget->parentWidget();parent;parent=parent->parentWidget()){
        if(ancestors)ancestors->append(QJsonObject{{"name",label(parent)},{"x",inRoot(parent,root).x()},
            {"y",inRoot(parent,root).y()},{"width",parent->width()},{"height",parent->height()},
            {"minimumHeight",parent->minimumHeight()},{"minimumHintHeight",parent->minimumSizeHint().height()}});
        visible=visible.intersected(parent==root?root->rect():inRoot(parent,root));
        if(parent==root)break;
    }
    return visible.translated(-widget->mapTo(root,QPoint()));
}

// QScrollArea can resize its content before a posted LayoutRequest removes the
// old scrollbar. Measure only after that cascade has completed, including the
// lazy tool page's changes to the MainWindow minimum size.
bool connectionsLayoutSettled(QWidget* window,QScrollArea* scroll)
{
    PhaseCost cost(QStringLiteral("connections-layout-settled"));
    class LayoutEvents : public QObject {
    public:
        QWidget* root;
        unsigned revision=0;
        explicit LayoutEvents(QWidget* root):root(root){}
        bool eventFilter(QObject* object,QEvent* event) override {
            const auto type=event->type();
            if(type==QEvent::LayoutRequest || type==QEvent::Resize || type==QEvent::Show
                || type==QEvent::Hide || type==QEvent::PolishRequest)
                if(auto* widget=qobject_cast<QWidget*>(object);widget && (widget==root || root->isAncestorOf(widget)))++revision;
            return false;
        }
    } events(window);
    qApp->installEventFilter(&events);
    const auto remove=qScopeGuard([&]{qApp->removeEventFilter(&events);});
    int quietTurns=0;
    unsigned observedRevision=events.revision;
    return QTest::qWaitFor([&]{
        QCoreApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);
        QCoreApplication::processEvents();
        quietTurns=observedRevision==events.revision?quietTurns+1:0;
        observedRevision=events.revision;
        const auto* vertical=scroll->verticalScrollBar();
        const auto* horizontal=scroll->horizontalScrollBar();
        const bool barsReady=vertical->isVisible()==(vertical->maximum()>0)
            && horizontal->isVisible()==(horizontal->maximum()>0);
        const bool widthReady=horizontal->maximum()>0 || scroll->widget()->width()==scroll->viewport()->width();
        return quietTurns>=2 && barsReady && widthReady;
    },2000);
}

Measurement measure(QWidget* root,int minimumRows=0)
{
    QElapsedTimer cost; cost.start();
    const auto costScope = qScopeGuard([&] { measurementCosts.measureNs += cost.nsecsElapsed(); ++measurementCosts.measurements; });
    Measurement result;
    QList<QWidget*> controls;
    for(auto* widget:root->findChildren<QWidget*>()){
        if(!widget->isVisibleTo(root) || widget->isWindow())continue;
        auto* button=qobject_cast<QAbstractButton*>(widget);
        auto* line=qobject_cast<QLineEdit*>(widget);
        auto* combo=qobject_cast<QComboBox*>(widget);
        if(!button && !line && !combo)continue;
        // An embedded clear affordance is sized by QLineEdit's style contract.
        if(button && qobject_cast<QLineEdit*>(widget->parentWidget()))continue;
        if(line && qobject_cast<QComboBox*>(line->parentWidget()))continue;
        const QRect rect=inRoot(widget,root);
        const QString name=label(widget);
        QJsonArray ancestors;
        const QRect reachable=reachableViewport(widget,root,&ancestors);
        // Qt retains an empty table-corner widget when its row header is hidden.
        const bool emptyDecoration=rect.isEmpty() && widget->minimumSizeHint().isEmpty()
            && button && button->text().isEmpty() && button->accessibleName().isEmpty();
        if(!emptyDecoration && reachable!=widget->rect())result.failures<<name+": not fully reachable within host";
        if(button && (widget->width()<widget->minimumSizeHint().width() || widget->height()<widget->minimumSizeHint().height()))
            result.failures<<name+": below intrinsic minimum";
        if(line && widget->width()<line->minimumSizeHint().width())result.failures<<name+": search field below minimum";
        const auto* tool=qobject_cast<QToolButton*>(button);
        const bool icon=tool && tool->toolButtonStyle()==Qt::ToolButtonIconOnly && !tool->icon().isNull();
        if(button && !button->text().isEmpty() && !icon && button->fontMetrics().horizontalAdvance(button->text().remove('&'))>button->width())
            result.failures<<name+": operation text clipped";
        if(icon && button->text()=="References" && (button->accessibleName()!="References" || !button->toolTip().contains("References")))
            result.failures<<name+": missing full References name";
        for(auto* other:controls)if(other->parentWidget()==widget->parentWidget() && !rect.intersected(inRoot(other,root)).isEmpty())
            result.failures<<name+": overlaps "+label(other);
        controls<<widget;
        QJsonObject control{{"name",name},{"x",rect.x()},{"y",rect.y()},{"width",rect.width()},{"height",rect.height()},
            {"minimumWidth",widget->minimumSizeHint().width()},{"minimumHeight",widget->minimumSizeHint().height()},
            {"iconOnly",icon},{"accessibleName",widget->accessibleName()},{"tooltip",widget->toolTip()},
            {"reachableWidth",reachable.width()},{"reachableHeight",reachable.height()},
            {"clippingAncestors",reachable==widget->rect()?QJsonArray():ancestors}};
        if (button && widget->width() < widget->minimumSizeHint().width()) {
            auto* parent = widget->parentWidget();
            auto* layout = parent ? parent->layout() : nullptr;
            auto* item = layout ? layout->itemAt(layout->indexOf(widget)) : nullptr;
            control.insert("intrinsicWidthDiagnostic", QJsonObject{
                {"parent", parent ? parent->objectName() : QString()},
                {"parentWidth", parent ? parent->width() : -1},
                {"sizeHintWidth", widget->sizeHint().width()},
                {"explicitMinimumWidth", widget->minimumWidth()},
                {"explicitMaximumWidth", widget->maximumWidth()},
                {"layoutWidth", layout ? layout->geometry().width() : -1},
                {"layoutMinimumWidth", layout ? layout->minimumSize().width() : -1},
                {"itemMinimumWidth", item ? item->minimumSize().width() : -1},
                {"itemSizeHintWidth", item ? item->sizeHint().width() : -1},
                {"itemMaximumWidth", item ? item->maximumSize().width() : -1}});
        }
        result.controls.append(control);
    }
    static const QStringList readableLabels={"instancePairConnectionHeading","instancePairConnectionState",
        "instancePairLeftInstancePath","instancePairRightInstancePath","instancePairLeftModule","instancePairRightModule",
        "rtlHighRiskEditHeading","rtlHighRiskEditState","rtlHighRiskRenameSubject","rtlHighRiskRenameOldName",
        "rtlHighRiskConnectionInstance","rtlHighRiskTransactionSummary","rtlHighRiskConfirmationFingerprint"};
    for(auto* info:root->findChildren<QLabel*>()){
        if(!readableLabels.contains(info->objectName()) || !info->isVisibleTo(root) || info->text().isEmpty())continue;
        const QRect reachable=reachableViewport(info,root);
        const int readable=qMin(info->fontMetrics().horizontalAdvance(info->text()),
            info->fontMetrics().horizontalAdvance("...")+3*info->fontMetrics().averageCharWidth());
        if(info->width()<readable || reachable!=info->rect())result.failures<<label(info)+": information label is not readable";
        if(!info->toolTip().contains(info->text()))result.failures<<label(info)+": missing complete information tooltip";
        result.controls.append(QJsonObject{{"name",label(info)},{"informationLabel",true},{"width",info->width()},
            {"height",info->height()},{"minimumWidth",info->minimumSizeHint().width()},
            {"minimumHeight",info->minimumSizeHint().height()},{"text",info->text()},{"tooltip",info->toolTip()},
            {"reachableWidth",reachable.width()},{"reachableHeight",reachable.height()}});
    }
    for(auto* area:root->findChildren<QScrollArea*>())if(area->isVisibleTo(root) && area->horizontalScrollBar()->maximum()>0)
        result.failures<<label(area)+": whole-panel horizontal scrolling";
    auto itemViews=root->findChildren<QAbstractItemView*>();
    if(auto* view=qobject_cast<QAbstractItemView*>(root))itemViews.prepend(view);
    for(auto* view:itemViews){
        if(!view->isVisibleTo(root) || qobject_cast<QHeaderView*>(view))continue;
        const QRect reachable=reachableViewport(view->viewport(),root);
        int visible=0,total=0,rowHeight=view->fontMetrics().height();
        const auto visit=[&](const auto& self,const QModelIndex& parent)->void{
            if(!view->model())return;
            for(int row=0;row<view->model()->rowCount(parent);++row){
                const auto index=view->model()->index(row,0,parent);++total;
                const QRect rect=view->visualRect(index);
                if(rect.height()>0)rowHeight=qMax(rowHeight,rect.height());
                if(!rect.isEmpty() && rect.top()>=reachable.top() && rect.bottom()<=reachable.bottom())++visible;
                if(auto* tree=qobject_cast<QTreeView*>(view);tree && tree->isExpanded(index))self(self,index);
            }
        };
        visit(visit,{});
        const int required=qMin(minimumRows,total);
        if(required>0 && visible<required)result.failures<<label(view)+QString(": %1 visible rows; requires %2").arg(visible).arg(required);
        result.views.append(QJsonObject{{"name",label(view)},{"viewportWidth",view->viewport()->width()},
            {"viewportHeight",view->viewport()->height()},{"rowHeight",rowHeight},{"rows",total},{"visibleRows",visible},
            {"requiredRows",required},{"horizontalRange",view->horizontalScrollBar()->maximum()},
            {"reachableWidth",reachable.width()},{"reachableHeight",reachable.height()}});
    }
    for(auto* view:root->findChildren<QGraphicsView*>())if(view->isVisibleTo(root)){
        const int line=view->fontMetrics().height();
        const QRect reachable=reachableViewport(view->viewport(),root);
        if(reachable.height()<4*line || reachable.width()<6*line)
            result.failures<<label(view)+": graph viewport below four text rows by six text heights";
        result.views.append(QJsonObject{{"name",label(view)},{"viewportWidth",view->viewport()->width()},
            {"viewportHeight",view->viewport()->height()},{"fontHeight",line},{"items",view->scene()?view->scene()->items().size():0},
            {"reachableWidth",reachable.width()},{"reachableHeight",reachable.height()}});
    }
    auto textViews=root->findChildren<QPlainTextEdit*>();
    if(auto* view=qobject_cast<QPlainTextEdit*>(root))textViews.prepend(view);
    for(auto* view:textViews)if(view->isVisibleTo(root)){
        const QRect reachable=reachableViewport(view->viewport(),root);
        const int lines=reachable.height()/view->fontMetrics().lineSpacing();
        if(lines<minimumRows)result.failures<<label(view)+": text viewport too short";
        result.views.append(QJsonObject{{"name",label(view)},{"viewportHeight",view->viewport()->height()},
            {"rowHeight",view->fontMetrics().lineSpacing()},{"rows",view->blockCount()},{"visibleRows",lines},
            {"reachableWidth",reachable.width()},{"reachableHeight",reachable.height()}});
    }
    return result;
}

bool record(QWidget* root,const QString& suffix,const Measurement& measurement,QJsonObject extra={})
{
    QElapsedTimer cost; cost.start();
    const auto costScope = qScopeGuard([&] { measurementCosts.recordNs += cost.nsecsElapsed(); });
    const QString dir=qEnvironmentVariable("ZEROSLACK_TEST_ARTIFACT_DIR");
    if(dir.isEmpty())return false;
    QDir().mkpath(dir);
    const QString name=QString::fromLatin1(QTest::currentTestFunction())+"_"+QTest::currentDataTag()+"_"+suffix;
    auto data=extra;
    data.insert("case",name);data.insert("logicalWidth",root->width());data.insert("logicalHeight",root->height());
    data.insert("actualPanelObjectName",root->objectName());
    data.insert("actualPanelClass",QString::fromLatin1(root->metaObject()->className()));
    data.insert("actualDpr",root->devicePixelRatioF());data.insert("fontHeight",root->fontMetrics().height());
    data.insert("theme",ApplicationThemeManager::instance().mode()==ThemeMode::Dark?"dark":"light");
    data.insert("platform",QGuiApplication::platformName());
    data.insert("actualUiBackend","ela");
    data.insert("availableWidth",root->screen()->availableGeometry().width());data.insert("availableHeight",root->screen()->availableGeometry().height());
    data.insert("cumulativeCostsBeforeCapture", measurementCosts.json());
    data.insert("controls",measurement.controls);data.insert("views",measurement.views);data.insert("failures",strings(measurement.failures));
    QFile output(dir+"/narrow-measurements.jsonl");
    if(!output.open(QIODevice::Append))return false;
    output.write(QJsonDocument(data).toJson(QJsonDocument::Compact)+"\n");
    QElapsedTimer captureCost; captureCost.start();
    const QPixmap captured = root->grab();
    measurementCosts.grabNs += captureCost.nsecsElapsed();
    // Capture stays on the GUI thread; one bounded write overlaps existing waits.
    const bool queued = imageEvidence->enqueue(captured.toImage(), dir+"/"+name+".png");
    measurementCosts.saveNs = imageEvidence->saveNs;
    ++measurementCosts.images;
    return queued;
}

QJsonObject controlState(QWidget* root)
{
    QJsonObject values;
    for(auto* widget:root->findChildren<QWidget*>()){
        if(widget->objectName().isEmpty())continue;
        if(auto* line=qobject_cast<QLineEdit*>(widget))values[widget->objectName()]=line->text();
        else if(auto* combo=qobject_cast<QComboBox*>(widget))values[widget->objectName()]=combo->currentIndex();
        else if(auto* button=qobject_cast<QAbstractButton*>(widget))
            values[widget->objectName()]=QJsonObject{{"checked",button->isChecked()},{"enabled",button->isEnabled()}};
    }
    return values;
}

bool recordOverflow(QWidget* root,const QString& suffix,QStringList& failures)
{
    for(auto* button:root->findChildren<QToolButton*>()){
        auto* menu=button->menu();
        if(!menu || !button->isVisibleTo(root))continue;
        bool overflow=false;
        for(auto* action:menu->actions())if(qobject_cast<QWidgetAction*>(action)
            && action->objectName().endsWith("Overflow") && action->isVisible())overflow=true;
        if(!overflow)continue;
        menu->popup(button->mapToGlobal(QPoint(0,button->height())));{ WaitCost waitCost(15); QTest::qWait(15); }
        auto sample=measure(menu);
        if(!menu->screen()->availableGeometry().contains(menu->geometry()))sample.failures<<label(button)+": overflow menu outside available screen";
        failures+=sample.failures;
        const bool saved=record(menu,suffix+'-'+button->objectName(),sample,{{"host","actual toolbar overflow"}});
        menu->close();{ WaitCost waitCost(5); QTest::qWait(5); }
        if(!saved)return false;
    }
    return true;
}

void dimensions(bool heights=false,bool modes=false)
{
    QTest::addColumn<int>("width");QTest::addColumn<bool>("dark");QTest::addColumn<bool>("populated");
    if(heights)QTest::addColumn<int>("height");
    if(modes)QTest::addColumn<int>("modeIndex");
    for(bool dark:{false,true})for(bool populated:{false,true})for(int width:{220,270,340,480})
        for(int height:(heights?QList<int>{160,280}:QList<int>{640}))for(int mode:(modes?QList<int>{0,1,2}:QList<int>{0})){
            auto& row=QTest::newRow(qPrintable(QString("%1-%2-w%3-h%4-m%5").arg(dark?"dark":"light").arg(populated?"data":"empty").arg(width).arg(height).arg(mode)));
            row<<width<<dark<<populated;if(heights)row<<height;if(modes)row<<mode;
        }
}
}

class NarrowLayoutMatrix final:public QObject {
    Q_OBJECT
    NarrowLayoutTest::Fixture fixture;
    void configure(SignalTracePanel* panel,bool populated,int mode=0){
        panel->setContext(fixture.context);
        if(populated)panel->showForSymbol(mode==2?"loop_a":"core",fixture.file,"top");
        panel->findChild<QComboBox*>("traceMode")->setCurrentIndex(mode);
        if(mode==0)panel->findChild<QComboBox*>("traceDirection")->setCurrentIndex(1);
    }
private slots:
    void cleanup() {
        QVERIFY2(imageEvidence->finish(), qPrintable(imageEvidence->error()));
        measurementCosts.saveNs = imageEvidence->saveNs;
    }
    void cleanupTestCase() {
        QVERIFY2(imageEvidence->finish(), qPrintable(imageEvidence->error()));
        measurementCosts.saveNs = imageEvidence->saveNs;
        QFile costs(qEnvironmentVariable("ZEROSLACK_TEST_ARTIFACT_DIR") + "/narrow-costs.json");
        QVERIFY(costs.open(QIODevice::WriteOnly));
        const QByteArray report = QJsonDocument(measurementCosts.json()).toJson();
        QCOMPARE(costs.write(report), report.size());
        imageEvidence.reset();
    }
    void initTestCase(){
        imageEvidence = std::make_unique<ImageEvidenceWriter>();
        QTemporaryDir encoding;
        QVERIFY(encoding.isValid());
        QImage pixels(7, 5, QImage::Format_ARGB32_Premultiplied);
        pixels.fill(QColor(33, 127, 211, 179));
        pixels.setDevicePixelRatio(1.5);
        const auto pixmap = QPixmap::fromImage(pixels);
        const auto original = encoding.filePath("original.png");
        const auto queued = encoding.filePath("queued.png");
        QVERIFY(pixmap.save(original));
        ImageEvidenceWriter checked;
        QVERIFY(checked.enqueue(pixmap.toImage(), queued));
        QVERIFY2(checked.finish(), qPrintable(checked.error()));
        QFile before(original), after(queued);
        QVERIFY(before.open(QIODevice::ReadOnly));
        QVERIFY(after.open(QIODevice::ReadOnly));
        QCOMPARE(after.readAll(), before.readAll());
        QCOMPARE(QImage(queued), QImage(original));
        const auto missing = encoding.filePath("missing/screenshot.png");
        QVERIFY(checked.enqueue(pixmap.toImage(), missing));
        QVERIFY(!checked.finish());
        QVERIFY(checked.error().contains(missing));
        QVERIFY(fixture.build());
        const qreal requested=qEnvironmentVariable("QT_SCALE_FACTOR","1").toDouble();
        QCOMPARE(QGuiApplication::primaryScreen()->devicePixelRatio(),requested);
    }
private:
    void overflowRestorationCommitsIntrinsicWidths() {
        QWidget host;
        auto* row = new QHBoxLayout;
        QList<QPushButton*> buttons;
        int fullWidth = 8;
        for (const auto* text : {"Jump", "Focus", "Reveal"}) {
            auto* button = UiControls::pushButton(QString::fromLatin1(text), &host);
            InsightVisualStyle::applyToolbarButton(button);
            row->addWidget(button);
            buttons.append(button);
            fullWidth += button->minimumSizeHint().width();
        }
        auto* toolbar = new CompactOverflowToolbar(row, QStringLiteral("restoration"), &host);
        host.resize(fullWidth, 100);
        toolbar->setGeometry(0, 0, fullWidth, 40);
        host.show();
        QCoreApplication::processEvents();
        toolbar->setFixedWidth(fullWidth - 20);
        QCoreApplication::processEvents();
        auto* more = toolbar->findChild<QToolButton*>(QStringLiteral("restorationMore"));
        QVERIFY(more && more->isVisible());
        QVERIFY(buttons.last()->parentWidget() != toolbar);
        toolbar->setFixedWidth(fullWidth);
        // Dispatch the queued owner arrangement without relying on a later
        // LayoutRequest to repair geometry after its visibility changes.
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QVERIFY(more->isHidden());
        for (auto* button : buttons) {
            QCOMPARE(button->parentWidget(), toolbar);
            qInfo() << "overflow-restoration" << button->text() << "actualWidth" << button->width()
                    << "intrinsicWidth" << button->minimumSizeHint().width();
            QVERIFY(button->width() >= button->minimumSizeHint().width());
        }
    }

private slots:
    void stableGeometryRejectsClipping_data(){
        QTest::addColumn<bool>("vertical");
        QTest::newRow("right-one-logical-pixel")<<false;
        QTest::newRow("bottom-one-logical-pixel")<<true;
    }
    void stableGeometryRejectsClipping(){
        QFETCH(bool,vertical);
        QWidget window;
        QVBoxLayout layout(&window);layout.setContentsMargins(0,0,0,0);
        auto* scroll=new QScrollArea;scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);layout.addWidget(scroll);
        auto* content=new QWidget;scroll->setWidget(content);
        auto* info=new QLabel("Connection state",content);
        info->setObjectName("instancePairConnectionState");info->setToolTip(info->text());
        info->setGeometry(8,8,160,info->fontMetrics().height()+8);
        window.resize(320,200);window.show();
        QVERIFY(connectionsLayoutSettled(&window,scroll));
        content->setFixedSize(scroll->viewport()->size());
        QVERIFY(connectionsLayoutSettled(&window,scroll));
        QVERIFY(measure(&window).failures.isEmpty());
        if(vertical)info->move(8,content->height()-info->height()+1);
        else info->move(content->width()-info->width()+1,8);
        // Stable geometry is not an acceptance result: measurement must reject clipping.
        QVERIFY(connectionsLayoutSettled(&window,scroll));
        QCOMPARE(scroll->horizontalScrollBar()->maximum(),0);
        QCOMPARE(scroll->verticalScrollBar()->maximum(),0);
        const QRect reachable=reachableViewport(info,&window);
        QCOMPARE(info->width()-reachable.width(),vertical?0:1);
        QCOMPARE(info->height()-reachable.height(),vertical?1:0);
        const auto clipped=measure(&window);
        QVERIFY(clipped.failures.contains("instancePairConnectionState: information label is not readable"));
        const QJsonObject evidence{{"case",QString::fromLatin1(QTest::currentDataTag())},
            {"actualDpr",window.devicePixelRatioF()},{"layoutSettled",true},{"clippingRejected",true},
            {"width",info->width()},{"height",info->height()},
            {"reachableWidth",reachable.width()},{"reachableHeight",reachable.height()},
            {"failures",strings(clipped.failures)}};
        const QString directory=qEnvironmentVariable("ZEROSLACK_TEST_ARTIFACT_DIR");
        QFile output(directory+"/narrow-negative-controls.jsonl");
        QVERIFY(output.open(QIODevice::Append));
        const auto bytes=QJsonDocument(evidence).toJson(QJsonDocument::Compact)+'\n';
        QCOMPARE(output.write(bytes),qint64(bytes.size()));
        QVERIFY(window.grab().save(directory+"/negative-"+QTest::currentDataTag()+".png"));
        info->move(8,8);QVERIFY(connectionsLayoutSettled(&window,scroll));
        QVERIFY(measure(&window).failures.isEmpty());
    }
    void riskStates(){
        LiveInsightToolPage host(LiveInsightKind::Kernel);host.resize(480,280);host.show();
        auto* panel=host.traceSurface();QVERIFY(panel);configure(panel,true);QVERIFY(SignalTraceTest::settled(*panel));
        auto* details=panel->findChild<QToolButton*>("traceDetails");
        auto* role=panel->findChild<QComboBox*>("traceRole");auto* mode=panel->findChild<QComboBox*>("traceMode");
        auto* search=panel->findChild<QLineEdit*>("traceFilter");
        auto* status=panel->findChild<QLabel*>("traceStatus");QVERIFY(details && role && mode && search && status);
        QStringList failures;
        panel->setHeatmapMode(true);
        auto multipleRoles=panel->saveViewState();multipleRoles["roleMask"]=7;panel->restoreViewState(multipleRoles);QVERIFY(SignalTraceTest::settled(*panel));
        search->setText("references");
        role->setFocus();QVERIFY(role->hasFocus());
        const auto reading=panel->saveViewState();const auto selected=panel->selectedObject().id();
        auto* root=panel->traceTree(SignalTraceDirection::Upstream)->topLevelItem(0);
        host.setFixedWidth(220);{ WaitCost waitCost(20); QTest::qWait(20); }
        auto* compactRole=panel->findChild<QToolButton*>("traceCompactRole");
        qInfo()<<"role focus"<<role->currentIndex()<<role->currentText()<<role->size()<<role->minimumSizeHint()
            <<"compact visible"<<(compactRole && compactRole->isVisible())<<"focused"<<QApplication::focusWidget();
        QVERIFY(record(panel,"role-focus",measure(panel),{{"roleIndex",role->currentIndex()},{"focus",QApplication::focusWidget()?label(QApplication::focusWidget()):QString()}}));
        QVERIFY(compactRole && compactRole->isVisible() && compactRole->hasFocus());
        QCOMPARE(search->text(),QString("references"));QCOMPARE(role->currentIndex(),7);
        QCOMPARE(panel->saveViewState(),reading);QCOMPARE(panel->selectedObject().id(),selected);QVERIFY(!panel->busy());
        QCOMPARE(panel->traceTree(SignalTraceDirection::Upstream)->topLevelItem(0),root);
        QVERIFY(compactRole->menu()->actions().last()->isChecked());
        compactRole->menu()->actions().at(0)->trigger();QCOMPARE(role->currentIndex(),0);
        compactRole->setFocus();host.setFixedWidth(480);{ WaitCost waitCost(20); QTest::qWait(20); }QVERIFY(role->hasFocus());
        host.setFixedWidth(220);{ WaitCost waitCost(20); QTest::qWait(20); }QCOMPARE(details->toolButtonStyle(),Qt::ToolButtonIconOnly);
        QCOMPARE(details->accessibleName(),QString("References"));QVERIFY(details->toolTip().contains("References"));
        details->setFocus();QTest::keyClick(details,Qt::Key_Space);QVERIFY(details->isChecked());
        { WaitCost waitCost(10); QTest::qWait(10); }
        QVERIFY(panel->findChild<QTreeWidget*>("traceReferences")->isVisible());
        auto sample=measure(panel,4);failures+=sample.failures;QVERIFY(record(panel,"filtered-references",sample));
        QTest::keyClick(details,Qt::Key_Space);QVERIFY(!details->isChecked());
        bool expanded=false;panel->setMainAreaHandler([&](bool value){expanded=value;});
        panel->setHeatmapMode(false);{ WaitCost waitCost(20); QTest::qWait(20); }
        auto* direction=panel->findChild<QComboBox*>("traceDirection");
        auto* compactDirection=panel->findChild<QToolButton*>("traceCompactDirection");QVERIFY(compactDirection && compactDirection->isVisible());
        compactDirection->menu()->actions().at(1)->trigger();QCOMPARE(direction->currentIndex(),1);QVERIFY(SignalTraceTest::settled(*panel));
        auto* overflow=panel->findChild<QAction*>("traceMainAreaOverflow");QVERIFY(overflow && overflow->isVisible());overflow->trigger();QVERIFY(expanded);
        auto* moreButton=panel->findChild<QToolButton*>("traceMore");QVERIFY(moreButton);
        moreButton->menu()->popup(moreButton->mapToGlobal(QPoint(0,moreButton->height())));{ WaitCost waitCost(10); QTest::qWait(10); }
        QVERIFY(panel->screen()->availableGeometry().contains(moreButton->menu()->geometry()));moreButton->menu()->close();
        auto changed=fixture.context;changed.currentSourceHashes[fixture.file]=SemanticSignalRoleEvidence::sourceHash("changed");panel->setContext(changed);
        QVERIFY(status->isVisible() && status->text().contains("changed",Qt::CaseInsensitive));
        sample=measure(panel,4);failures+=sample.failures;QVERIFY(record(panel,"stale",sample));
        panel->setContext(fixture.context);panel->showForSymbol("d",fixture.leafFile,"leaf");QVERIFY(SignalTraceTest::settled(*panel));
        auto* instances=panel->findChild<QComboBox*>("traceInstance");QVERIFY(instances && instances->isVisible() && instances->count()>=2);
        panel->analyzeFeedback();QVERIFY(SignalTraceTest::settled(*panel));QVERIFY(status->text().contains("instance",Qt::CaseInsensitive));
        sample=measure(panel,4);failures+=sample.failures;QVERIFY(record(panel,"ambiguous-instance",sample));
        configure(panel,true,2);panel->analyzeFeedback();panel->cancel();QVERIFY(!panel->busy());QVERIFY(status->text().contains("Cancelled"));
        sample=measure(panel,4);failures+=sample.failures;QVERIFY(record(panel,"cancelled",sample));
        panel->setContext({fixture.directory.path(),"unavailable",{}, {}});panel->showForSymbol("core",fixture.file,"top");QVERIFY(SignalTraceTest::settled(*panel));
        QVERIFY(!status->text().isEmpty());sample=measure(panel,4);failures+=sample.failures;QVERIFY(record(panel,"unavailable",sample));
        QVERIFY2(failures.isEmpty(),qPrintable(failures.join('\n')));
    }
    void paginationAndUncertainBoundary(){
        SignalTraceTest::Fixture paged;QVERIFY(paged.build(SignalTraceTest::branchingSource(500)));
        SignalTracePanel panel;panel.setFixedSize(220,280);panel.show();panel.setContext(paged.context);
        panel.showForSymbol("core",paged.file,"top");QVERIFY(SignalTraceTest::settled(panel));panel.setHeatmapMode(true);
        auto* status=panel.findChild<QLabel*>("traceStatus");QVERIFY(status && status->text().contains("partial",Qt::CaseInsensitive));
        QVERIFY(!panel.heatModel()->isComplete());
        panel.focusInspector();{ WaitCost waitCost(10); QTest::qWait(10); }
        auto sample=measure(&panel,4);QStringList failures=sample.failures;QVERIFY(record(&panel,"partial",sample));
        QPushButton* more=nullptr;for(auto* button:panel.findChildren<QPushButton*>())if(button->text()=="Load more references")more=button;
        QVERIFY(more);const int initial=panel.heatModel()->references().size();
        for(int page=0;page<4 && !panel.heatModel()->isComplete();++page){
            if(more->isVisible())more->click();
            else {auto* action=panel.findChild<QAction*>("traceReferencesOverflow");QVERIFY(action && action->isVisible());action->trigger();}
            QVERIFY(SignalTraceTest::settled(panel));
        }
        QVERIFY(panel.heatModel()->isComplete());QVERIFY(panel.heatModel()->references().size()>initial);
        const int complete=panel.heatModel()->references().size();
        panel.setFixedWidth(480);{ WaitCost waitCost(10); QTest::qWait(10); }panel.setFixedWidth(220);{ WaitCost waitCost(10); QTest::qWait(10); }
        QCOMPARE(panel.heatModel()->references().size(),complete);QVERIFY(panel.heatModel()->isComplete());
        SignalTraceTest::Fixture uncertain;
        QVERIFY(uncertain.build("module child(inout wire p); endmodule\nmodule top(inout wire a,b); alias a=b; child u(.p(a)); endmodule\n"));
        panel.setContext(uncertain.context);panel.showForSymbol("a",uncertain.file,"top");QVERIFY(SignalTraceTest::settled(panel));
        panel.setHeatmapMode(false);panel.analyzeFeedback();QVERIFY(SignalTraceTest::settled(panel));
        QVERIFY(status->text().contains("Partial"));QCOMPARE(status->toolTip(),status->text());QCOMPARE(status->accessibleName(),status->text());
        sample=measure(&panel,4);failures+=sample.failures;QVERIFY(record(&panel,"uncertain-boundary",sample,{{"status",status->text()}}));
        QVERIFY2(failures.isEmpty(),qPrintable(failures.join('\n')));
    }
    void shortFilterPopup(){
        SignalTracePanel panel;panel.setFixedSize(220,280);panel.show();configure(&panel,true,1);QVERIFY(SignalTraceTest::settled(panel));
        auto* search=panel.findChild<QLineEdit*>("traceFilter");QVERIFY(search && search->isVisible());search->setText("references");search->setFocus();
        panel.setFixedHeight(160);{ WaitCost waitCost(20); QTest::qWait(20); }
        auto* more=panel.findChild<QToolButton*>("traceMore");auto* filter=panel.findChild<QMenu*>("traceFilterMenu");
        QVERIFY(more && filter && filter->menuAction()->isVisible());QVERIFY(more->hasFocus());QVERIFY(!search->isVisible());
        filter->popup(more->mapToGlobal(QPoint(0,more->height())));{ WaitCost waitCost(20); QTest::qWait(20); }
        QVERIFY(search->isVisible() && search->hasFocus());QVERIFY(search->width()>=search->minimumSizeHint().width());
        QVERIFY(panel.screen()->availableGeometry().contains(filter->geometry()));
        QTest::keyClick(search,Qt::Key_A,Qt::ControlModifier);QTest::keyClicks(search,"original");QCOMPARE(search->text(),QString("original"));
        panel.setFixedHeight(280);{ WaitCost waitCost(20); QTest::qWait(20); }QVERIFY(search->isVisible() && search->hasFocus());
        QCOMPARE(search->text(),QString("original"));QVERIFY(!filter->menuAction()->isVisible());filter->close();
        const auto sample=measure(&panel);QVERIFY(record(&panel,"restored-from-menu",sample));QVERIFY2(sample.failures.isEmpty(),qPrintable(sample.failures.join('\n')));
    }
    void relocatedControlsKeepTheirState(){
        ScopedSearchPanel search;
        search.setFixedSize(900,280);search.show();{ WaitCost waitCost(20); QTest::qWait(20); }
        auto* query=search.findChild<QLineEdit*>("scopedSearchQueryEdit");QVERIFY(query);
        query->setText("complete query with spaces");
        auto* scope=search.findChild<QComboBox*>("scopedSearchScopeCombo");QVERIFY(scope);
        scope->setCurrentIndex(2);scope->setFocus();QVERIFY(scope->hasFocus());
        search.setFixedWidth(220);{ WaitCost waitCost(20); QTest::qWait(20); }
        auto* more=search.findChild<QToolButton*>("scopedSearchMore");QVERIFY(more && more->isVisible());
        QVERIFY(more->hasFocus());QCOMPARE(scope->currentIndex(),2);QCOMPARE(query->text(),QString("complete query with spaces"));
        more->menu()->popup(more->mapToGlobal(QPoint(0,more->height())));{ WaitCost waitCost(20); QTest::qWait(20); }
        QVERIFY(scope->isVisible());QVERIFY(scope->width()>=scope->minimumSizeHint().width());
        scope->setCurrentIndex(1);scope->setFocus();
        more->menu()->close();more->setFocus();search.setFixedWidth(900);{ WaitCost waitCost(20); QTest::qWait(20); }
        QCOMPARE(scope->currentIndex(),1);QVERIFY(scope->isVisibleTo(&search));QVERIFY(scope->hasFocus());
        QCOMPARE(query->text(),QString("complete query with spaces"));

        RtlHighRiskEditPanel edit;edit.setFixedSize(220,280);edit.show();
        RtlConnectionTransformRequest defaults;
        edit.beginConnectionTransform(42,"top.instance_with_a_complete_long_name",defaults,true);
        RtlHighRiskEditPanelOutcome outcome;outcome.panelState=RtlHighRiskEditPanelState::Editing;
        outcome.canPreview=true;outcome.canCancel=true;edit.presentOutcome(outcome);{ WaitCost waitCost(20); QTest::qWait(20); }
        edit.setConnectionOptions(true,true,RtlMissingPortConnectionPolicy::ConnectSameNamedSignal,
            RtlExplicitCastPolicy::InsertWhenRequired,true,true);
        QSignalSpy changed(&edit,&RtlHighRiskEditPanel::draftChanged);
        QStringList failures;
        for(const QSize size:{QSize(220,160),QSize(480,280),QSize(220,280)}){
            edit.setFixedSize(size);{ WaitCost waitCost(20); QTest::qWait(20); }
            QCOMPARE(edit.sessionId(),std::uint64_t(42));QCOMPARE(edit.state(),RtlHighRiskEditPanelState::Editing);
            const auto draft=edit.draft();QVERIFY(draft.convertOrderedToNamed && draft.addMissingPorts && draft.removeUnknownPorts && draft.synchronizeAllInstances);
            QCOMPARE(draft.missingPortPolicy,RtlMissingPortConnectionPolicy::ConnectSameNamedSignal);
            QCOMPARE(draft.castPolicy,RtlExplicitCastPolicy::InsertWhenRequired);
            const auto sample=measure(&edit,size.height()==160?2:4);failures+=sample.failures;
            QVERIFY(record(&edit,QString("connection-input-%1x%2").arg(size.width()).arg(size.height()),sample));
        }
        QCOMPARE(changed.count(),0);
        QVERIFY(recordOverflow(&edit,"connection-input-options",failures));
        auto* preview=edit.findChild<QPushButton*>("rtlHighRiskPreviewButton");QVERIFY(preview);
        QSignalSpy requested(&edit,&RtlHighRiskEditPanel::previewRequested);
        auto* actions=edit.findChild<QToolButton*>("rtlHighRiskActionsMore");QVERIFY(actions);
        actions->menu()->popup(actions->mapToGlobal(QPoint(0,actions->height())));{ WaitCost waitCost(20); QTest::qWait(20); }
        QPushButton* previous=nullptr;
        for(const auto* name:{"rtlHighRiskConfirmButton","rtlHighRiskCancelButton","rtlHighRiskUndoButton","rtlHighRiskRetireButton"}){
            auto* button=edit.findChild<QPushButton*>(name);QVERIFY(button && button->isVisible());
            if(previous){QCOMPARE(button->parentWidget(),previous->parentWidget());QVERIFY(button->y()>previous->y());}
            previous=button;
        }
        QVERIFY(preview->isVisible());QTest::mouseClick(preview,Qt::LeftButton);
        QCOMPARE(requested.count(),1);QCOMPARE(requested.first().at(0).toULongLong(),qulonglong(42));
        actions->menu()->close();
        QVERIFY2(failures.isEmpty(),qPrintable(failures.join('\n')));
        overflowRestorationCommitsIntrinsicWidths();
        realMainWindowInspectorRestoration();
    }
    void signalModes_data(){dimensions(true,true);}
    void signalModes(){
        QFETCH(int,width);QFETCH(bool,dark);QFETCH(bool,populated);QFETCH(int,height);QFETCH(int,modeIndex);
        ApplicationThemeManager::instance().setMode(dark?ThemeMode::Dark:ThemeMode::Light);
        LiveInsightToolPage host(LiveInsightKind::Kernel);host.setFixedSize(width,height);host.show();
        auto* panel=host.traceSurface();QVERIFY(panel);configure(panel,populated,modeIndex);
        QVERIFY(SignalTraceTest::settled(*panel));{ WaitCost waitCost(10); QTest::qWait(10); }
        QCOMPARE(panel->width(),width);
        if(populated){
            QVERIFY(panel->currentObject().isValid());
            if(modeIndex==1)QVERIFY(panel->heatModel()->rowCount()>=3);
            if(modeIndex==2)QVERIFY(panel->findChild<QGraphicsView*>("traceCycleView")->scene()->items().size()>0);
        }
        QStringList failures;
        auto sample=measure(panel,height==160?2:4);failures+=sample.failures;
        QVERIFY(record(panel,"view",sample,{{"mode",modeIndex},{"populated",populated},{"requestedHeight",height}}));
        if(width==220 || width==480){
            const auto rootBefore=panel->currentObject().id();
            panel->focusZoomIn();{ WaitCost waitCost(10); QTest::qWait(10); }sample=measure(panel,height==160?2:4);failures+=sample.failures;
            QVERIFY(record(panel,"zoom",sample,{{"mode",modeIndex},{"populated",populated}}));
            panel->focusZoomOut();if(modeIndex==2)panel->focusFit();{ WaitCost waitCost(10); QTest::qWait(10); }
            QCOMPARE(panel->currentObject().id(),rootBefore);QVERIFY(!panel->busy());
        }
        auto* details=panel->findChild<QToolButton*>("traceDetails");QVERIFY(details);
        details->click();{ WaitCost waitCost(10); QTest::qWait(10); }QVERIFY(details->isChecked());
        sample=measure(panel,height==160?2:4);failures+=sample.failures;
        QVERIFY(record(panel,"references",sample,{{"mode",modeIndex},{"populated",populated},{"requestedHeight",height}}));
        details->click();QVERIFY(!details->isChecked());
        const auto object=panel->selectedObject().id();const auto reading=panel->saveViewState();
        auto* tree=panel->traceTree(SignalTraceDirection::Upstream);auto* root=tree->topLevelItem(0);
        int resizeStep=0;
        for(int changed:{480,220,width}){
            host.setFixedWidth(changed);{ WaitCost waitCost(10); QTest::qWait(10); }
            QCOMPARE(panel->selectedObject().id(),object);QCOMPARE(tree->topLevelItem(0),root);
            QCOMPARE(panel->saveViewState(),reading);QVERIFY(!panel->busy());
            sample=measure(panel,height==160?2:4);failures+=sample.failures;
            QVERIFY(record(panel,QString("resize-%1-w%2").arg(++resizeStep).arg(changed),sample,{{"mode",modeIndex},{"populated",populated}}));
        }
        QVERIFY2(failures.isEmpty(),qPrintable(failures.join('\n')));
    }
    void contextSections_data(){dimensions();}
    void contextSections(){
        QFETCH(int,width);QFETCH(bool,dark);QFETCH(bool,populated);
        ApplicationThemeManager::instance().setMode(dark?ThemeMode::Dark:ThemeMode::Light);
        auto* index=SemanticIndex::getInstance();const auto previous=index->snapshotToken();
        const auto restore=qScopeGuard([&]{index->setSnapshot(previous.snapshot);});
        index->setSnapshot(populated?fixture.context.token.snapshot:std::make_shared<SemanticIndexSnapshot>());
        ContextDockHost host;
        QStringList keys;
        for(auto kind:{LiveInsightKind::Kernel,LiveInsightKind::State,LiveInsightKind::Module}){
            const auto resource=LiveInsightsContextProvider::resourceForKind(kind,fixture.directory.path());
            auto* view=new LiveInsightsContextView(nullptr,kind);
            view->setToolContextSource([&,populated,kind]{LiveInsightToolContext context;
                context.workspaceId=fixture.directory.path();context.projectId="narrow-layout";
                context.documentId=fixture.file;context.documentRevision=1;context.semanticRevision=fixture.context.token.revision;
                if(populated){context.fileName=fixture.file;context.moduleName="top";context.signalName=kind==LiveInsightKind::State?"next_state":"core";context.documentText=fixture.sources.value(fixture.file);}
                context.configureTrace=[&,populated](SignalTracePanel* panel){configure(panel,populated);};return context;});
            QVERIFY(host.addResource(resource,view,true));keys<<resource.stableKey();
        }
        host.setFixedWidth(width);host.resize(width,640);host.show();{ WaitCost waitCost(50); QTest::qWait(50); }
        for(auto* panel:host.findChildren<SignalTracePanel*>())QVERIFY(SignalTraceTest::settled(*panel));
        for(auto* view:host.findChildren<LiveInsightsContextView*>())if(auto* surface=view->surfaceForTest())
            if(auto* workbench=surface->workbenchForTest())if(auto* panel=workbench->rtlSurfaceForTest()){
                QVERIFY(waitForLiveInsightReports(*panel));
                if(populated){
                    const auto snapshot=panel->graphSession()->snapshot(LiveInsightKind::State);
                    const auto report=snapshot.payload.value("graphReport").value<LiveInsightGraphReportPtr>();QVERIFY(report);
                    const auto* state=std::get_if<StateTransitionGraphReport>(&report->value);QVERIFY(state);
                    QVERIFY2(state->found,qPrintable(state->notFoundReasonDisplayName));QVERIFY(state->stateCount>=3);
                }
            }
        QStringList failures;
        auto sample=measure(&host);failures+=sample.failures;QVERIFY(record(&host,"expanded",sample,{{"populated",populated}}));
        for(const auto& key:keys){
            const auto* view=host.viewForResource(key);QVERIFY(view);
            if(view->height()<4*view->fontMetrics().height())failures<<key+": section content below four text rows";
            QVERIFY(host.setSectionCollapsed(key,true,false));QVERIFY(host.isSectionCollapsed(key));
            QVERIFY(host.setSectionCollapsed(key,false,false));QVERIFY(!host.isSectionCollapsed(key));
        }
        { WaitCost waitCost(20); QTest::qWait(20); }sample=measure(&host);failures+=sample.failures;QVERIFY(record(&host,"restored",sample,{{"populated",populated}}));
        const auto controls=controlState(&host);
        for(int changed:{480,220,width}){
            host.setFixedWidth(changed);{ WaitCost waitCost(20); QTest::qWait(20); }
            QCOMPARE(controlState(&host),controls);
            sample=measure(&host);failures+=sample.failures;
            QVERIFY(record(&host,QString("resize-w%1").arg(changed),sample,{{"populated",populated}}));
        }
        QVERIFY(recordOverflow(&host,"context-options",failures));
        QVERIFY2(failures.isEmpty(),qPrintable(failures.join('\n')));
    }
private:
    void realMainWindowInspectorRestoration(){
        QTemporaryDir temporary;
        qputenv("ZEROSLACK_SESSION_STORAGE_PATH",temporary.filePath("sessions.ini").toUtf8());
        qputenv("ZEROSLACK_EDITING_TIME_STORAGE_PATH",temporary.filePath("editing.json").toUtf8());
        ApplicationThemeManager::instance().setMode(ThemeMode::Light);
        MainWindow window;
        window.tabManager->setCrashRecoveryService(std::make_unique<CrashRecoveryService>(temporary.filePath("recovery")));
        window.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
        window.analysisScheduler->setSemanticAnalysisRuntimePolicy(SemanticAnalysisRuntimePolicy{});
        window.resize(1280,760);window.show();
        auto* sessions=window.findChild<WorkspaceSessionCoordinator*>();QVERIFY(sessions);
        QVERIFY(sessions->openWorkspace(fixture.directory.path()));
        QVERIFY(window.tabManager->openFileInTab(fixture.file));
        window.analysisScheduler->requestDocumentSemanticRefresh({fixture.file});
        QTRY_VERIFY_WITH_TIMEOUT(SemanticIndex::getInstance()->snapshotToken().snapshot
            && !SemanticIndex::getInstance()->snapshotToken().snapshot->getSymbolRecordsByName("next_state",fixture.file).isEmpty(),20000);
        QTRY_VERIFY_WITH_TIMEOUT(!window.analysisScheduler->isSemanticAnalysisActive(),20000);
        auto* controller=window.findChild<ContextWorkspaceController*>();QVERIFY(controller);
        const auto resource=LiveInsightsContextProvider::resourceForKind(LiveInsightKind::State,fixture.directory.path());
        QString error;
        QVERIFY2(controller->openResource(resource,{ContextSurface::Docked,ContextPersistence::Kept},&error),qPrintable(error));
        auto* view=qobject_cast<LiveInsightsContextView*>(controller->viewForResource(resource.stableKey()));QVERIFY(view);
        view->setToolContextSource([&]{
            LiveInsightToolContext context;
            context.workspaceId=fixture.directory.path();context.projectId="narrow-layout";
            context.documentId=fixture.file;context.documentRevision=1;
            context.semanticRevision=SemanticIndex::getInstance()->snapshotToken().revision;
            context.fileName=fixture.file;context.moduleName="top";context.signalName="next_state";
            context.documentText=fixture.sources.value(fixture.file);
            return context;
        });
        qInfo()<<"initial-state-target"<<view->saveState().value("target");
        LiveInsightsContextView::TargetCandidate target;
        target.label="next_state";target.fileName=fixture.file;target.moduleName="top";target.signalName="next_state";
        QVERIFY(view->applyTargetCandidate(target));
        QCOMPARE(view->saveState().value("target").toMap().value("signalName").toString(),QString("next_state"));
        auto* surface=view->surfaceForTest();QVERIFY(surface);
        auto* workbench=surface->workbenchForTest();QVERIFY(workbench);
        auto* panel=workbench->rtlSurfaceForTest();QVERIFY(panel);
        QVERIFY(waitForLiveInsightReports(*panel));
        const auto snapshot=panel->graphSession()->snapshot(LiveInsightKind::State);
        const auto report=snapshot.payload.value("graphReport").value<LiveInsightGraphReportPtr>();QVERIFY(report);
        const auto* state=std::get_if<StateTransitionGraphReport>(&report->value);QVERIFY(state);
        QVERIFY2(state->found,qPrintable(state->notFoundReasonDisplayName));QVERIFY(state->stateCount>=3);
        auto* host=controller->dockHost();QVERIFY(host);
        auto* dock=controller->dockWidget();QVERIFY(dock);
        QVERIFY(controller->dockVisible());QVERIFY(window.isAncestorOf(view));
        auto* scroll=view->findChild<QScrollArea*>("rtlInsightsGraphPanelScroll");QVERIFY(scroll);
        const int narrowWidth=qMax(ContextWorkspaceState::kMinimumDockWidth,dock->minimumSizeHint().width());
        const int wideWidth=qMax(480,narrowWidth+200);
        QStringList failures;int step=0;
        for(int width:{narrowWidth,wideWidth,narrowWidth,wideWidth}){
            dock->setFixedWidth(width);
            window.resizeDocks({dock},{width},Qt::Horizontal);
            QCoreApplication::processEvents();
            QCOMPARE(dock->width(),width);
            QVERIFY(view->isVisibleTo(&window));
            if(++step==3){
                QVERIFY(host->setSectionCollapsed(resource.stableKey(),true,false));
                QVERIFY(host->isSectionCollapsed(resource.stableKey()));
                QVERIFY(host->setSectionCollapsed(resource.stableKey(),false,false));
                QVERIFY(!host->isSectionCollapsed(resource.stableKey()));
                QCoreApplication::processEvents();
            }
            QVERIFY(connectionsLayoutSettled(&window,scroll));
            QVERIFY(dock->width()>=dock->minimumSizeHint().width());
            const auto sample=measure(host);failures+=sample.failures;
            QVERIFY(record(host,QString("state-view-%1").arg(step),sample,
                {{"host","actual MainWindow / ContextWorkspaceController / State"},
                 {"requestedDockWidth",width},{"minimumDockWidth",narrowWidth},{"stateCount",state->stateCount}}));
            const QString screenshot=qEnvironmentVariable("ZEROSLACK_TEST_ARTIFACT_DIR")
                +QString("/mainwindow-state-%1.png").arg(step);
            QVERIFY(window.grab().save(screenshot));
            qInfo()<<"actual-mainwindow"<<QGuiApplication::platformName()<<"DPR"<<window.devicePixelRatioF()
                   <<"dockWidth"<<dock->width()<<"stateCount"<<state->stateCount<<"screenshot"<<screenshot;
        }
        QVERIFY2(failures.isEmpty(),qPrintable(failures.join('\n')));
        QVERIFY(window.close());
    }
private slots:
    void realNavigationAndBottom_data(){dimensions(true);}
    void realNavigationAndBottom(){
        PhaseCost cost(QStringLiteral("realNavigationAndBottom"));
        QFETCH(int,width);QFETCH(bool,dark);QFETCH(bool,populated);QFETCH(int,height);
        QTemporaryDir temporary;const QString workspace=temporary.filePath("workspace");QVERIFY(QDir().mkpath(workspace));
        const QString top=workspace+'/'+QFileInfo(fixture.file).fileName();
        if(populated)for(auto it=fixture.sources.cbegin();it!=fixture.sources.cend();++it){
            QFile out(workspace+'/'+QFileInfo(it.key()).fileName());QVERIFY(out.open(QIODevice::WriteOnly));QCOMPARE(out.write(it.value().toUtf8()),it.value().toUtf8().size());
        }
        qputenv("ZEROSLACK_SESSION_STORAGE_PATH",temporary.filePath("sessions.ini").toUtf8());
        qputenv("ZEROSLACK_EDITING_TIME_STORAGE_PATH",temporary.filePath("editing.json").toUtf8());
        ApplicationThemeManager::instance().setMode(dark?ThemeMode::Dark:ThemeMode::Light);
        cost.checkpoint("fixture");
        MainWindow window;
        cost.checkpoint("create-window");
        window.tabManager->setCrashRecoveryService(std::make_unique<CrashRecoveryService>(temporary.filePath("recovery")));
        window.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
        window.analysisScheduler->setSemanticAnalysisRuntimePolicy(SemanticAnalysisRuntimePolicy{});
        window.resize(1280,760);window.show();
        cost.checkpoint("show-window");
        auto* sessions=window.findChild<WorkspaceSessionCoordinator*>();QVERIFY(sessions);QVERIFY(sessions->openWorkspace(workspace));
        cost.checkpoint("open-workspace");
        if(populated){
            QVERIFY(window.tabManager->openFileInTab(top));window.analysisScheduler->requestDocumentSemanticRefresh({top});
            QTRY_VERIFY_WITH_TIMEOUT(SemanticIndex::getInstance()->snapshotToken().snapshot
                && !SemanticIndex::getInstance()->snapshotToken().snapshot->getSymbolRecordsByName("core",top).isEmpty(),20000);
            QVERIFY(window.navigationManager->setDesignTop("top",top).isEmpty());
        }
        QTRY_VERIFY_WITH_TIMEOUT(!window.analysisScheduler->isSemanticAnalysisActive(),20000);
        ApplicationThemeManager::instance().setMode(dark?ThemeMode::Dark:ThemeMode::Light);
        cost.checkpoint("analysis");
        QStringList failures;
        auto* navigation=window.findChild<NavigationWidget*>();QVERIFY(navigation);
        auto* navigationPane=owner<NavigationPaneCoordinator>(&window);QVERIFY(navigationPane);
        const int designWidth=width==480?400:width;
        navigationPane->showDesign();navigationPane->dock()->setFixedWidth(designWidth);
        auto* design=navigation->findChild<QTreeWidget*>("navigationDesignTree");QVERIFY(design);
        if(populated){
            QTRY_VERIFY_WITH_TIMEOUT(design->topLevelItemCount()>0,10000);
            QTRY_VERIFY_WITH_TIMEOUT(navigation->selectDesignInstance("top.instance_with_a_long_name_left",true),10000);
            QVERIFY(!design->currentItem()->toolTip(0).isEmpty());
            auto* simulation=window.findChild<simdock::Workbench*>();QVERIFY(simulation);
            const auto project=simulation->designService()->activeProjectId();QVERIFY(!project.isEmpty());
            QVERIFY(window.navigationManager->setDesignTop("leaf",workspace+"/leaf.sv").isEmpty());
            QTRY_VERIFY(simulation->designService()->activeProjectId()!=project);
            QVERIFY(window.navigationManager->setDesignTop("top",top).isEmpty());
            QTRY_COMPARE(simulation->designService()->activeProjectId(),project);
            QTRY_VERIFY(navigation->selectDesignInstance("top.instance_with_a_long_name_left",true));
            design->collapseAll();design->expandAll();
        }
#ifdef ZEROSLACK_ENABLE_ELA
        if(auto* bar=navigationPane->dock()->findChild<ElaNavigationBar*>("navigationElaBar"))bar->setNavigationBarWidth(designWidth);
#endif
        window.resizeDocks({navigationPane->dock()},{designWidth},Qt::Horizontal);
        { WaitCost waitCost(20); QTest::qWait(20); }
        auto sample=measure(navigation,populated?4:0);
        if(navigation->width()!=designWidth)sample.failures<<QString("requested Design content width %1; actual %2").arg(designWidth).arg(navigation->width());
        failures+=sample.failures;
        QVERIFY(record(navigation,"design",sample,{{"populated",populated},{"zoom","not applicable"},{"requestedWidth",designWidth},{"design480","void_not_passed"}}));
        for(int changed:{400,220,designWidth}){
            navigationPane->dock()->setFixedWidth(changed);
#ifdef ZEROSLACK_ENABLE_ELA
            if(auto* bar=navigationPane->dock()->findChild<ElaNavigationBar*>("navigationElaBar"))bar->setNavigationBarWidth(changed);
#endif
            window.resizeDocks({navigationPane->dock()},{changed},Qt::Horizontal);{ WaitCost waitCost(15); QTest::qWait(15); }
            sample=measure(navigation,populated?4:0);failures+=sample.failures;
            QCOMPARE(navigation->width(),changed);
            QVERIFY(record(navigation,QString("design-resize-%1").arg(changed),sample,{{"requestedWidth",changed}}));
        }
        navigationPane->setExpanded(false,false);QVERIFY(!navigationPane->isExpanded());
        navigationPane->setExpanded(true,false);QVERIFY(navigationPane->isExpanded());

        cost.checkpoint("navigation");
        // Use the shared Bottom groups, with a Context resource alongside tools.
        auto* host=window.findChild<ContextDockHost*>();QVERIFY(host);
        const auto resource=LiveInsightsContextProvider::resourceForKind(LiveInsightKind::Module,workspace);
        QVERIFY(host->addResource(resource,new LiveInsightsContextView(nullptr,LiveInsightKind::Module),true));
        QVERIFY(host->moveResourceToArea(resource.stableKey(),true));
        auto* layout=owner<PanelLayoutController>(&window);QVERIFY(layout);layout->setAnimationsEnabled(false);
        auto* registry=window.tabManager->editorSplitController();QVERIFY(registry);
        auto* splitter=qobject_cast<QSplitter*>(registry->regionRoot("bottom"));QVERIFY(splitter);
        auto* bottomHost=layout->drawerContent();QVERIFY(bottomHost);
        const QStringList expected={"problems","scopedSearch","activity","simulationLog","simulationEvents","hotspot","rtlHighRiskEdit","connections"};
        for (const auto& id:expected) {
            QVERIFY(registry->pageById("tool:"+id));
            QVERIFY(layout->restorePanel(id));
        }
        const auto bottomIds=layout->bottomPanelIds();
        auto expectedBottom=expected;expectedBottom.removeAll("connections");
        QCOMPARE(QSet<QString>(bottomIds.begin(),bottomIds.end()),QSet<QString>(expectedBottom.begin(),expectedBottom.end()));
        // Measure bottom tools before opening the independently sized main-area page.
        QVERIFY(layout->closePanel("connections"));
        const auto ids=expected;
        cost.checkpoint("bottom-setup");
        for(const auto& id:ids){
            PhaseCost panelCost(id);
            QVERIFY(layout->restorePanel(id));QVERIFY(layout->setPanelHeight(id,height));
            { WaitCost waitCost(10); QTest::qWait(10); }
            QWidget* panel=registry->pageById("tool:"+id);QVERIFY(panel);
            if(id!="connections"){QCOMPARE(layout->activeBottomPanelId(),id);QVERIFY(panel->isVisibleTo(&window));}
            if(id=="hotspot"){
                auto* trace=qobject_cast<SignalTracePanel*>(panel);QVERIFY(trace);configure(trace,populated,1);QVERIFY(SignalTraceTest::settled(*trace));
            }else if(id=="activity"){
                ActivityLogService::getInstance()->clear();
                if(populated)for(int i=0;i<30;++i)ActivityLogService::getInstance()->append("layout fixture",ActivityLogLevel::Info,QString("Activity row %1 with a complete diagnostic message").arg(i));
            }else if(id=="simulationLog"){
                auto* text=panel->findChild<QPlainTextEdit*>();if(!text)text=qobject_cast<QPlainTextEdit*>(panel);QVERIFY(text);
                text->clear();if(populated)for(int i=0;i<30;++i)text->appendPlainText(QString("simulation output %1").arg(i));
            }else if(id=="simulationEvents"){
                auto* events=qobject_cast<simdock::SimulationEventsPanel*>(panel);if(!events)events=panel->findChild<simdock::SimulationEventsPanel*>();QVERIFY(events);
                events->startRun("narrow-layout");QList<simdock::SimulationEvent> values;
                if(populated)for(int i=0;i<30;++i){simdock::SimulationEvent event;event.runId="narrow-layout";event.timePs=QString::number(i*10);event.level="info";event.tag="layout";event.instance="top.instance_with_a_long_name_left";event.sourceFile=top;event.sourceLine=i+1;event.message=QString("Event %1").arg(i);values<<event;}
                events->append(values);QCOMPARE(events->eventModel()->rowCount(),populated?30:0);
            }else if(id=="scopedSearch"){
                auto* searchOwner=owner<ScopedSearchPanelCoordinator>(&window);QVERIFY(searchOwner);
                auto* search=searchOwner->panel();QVERIFY(search);
                ScopedSearchPanelContext context;context.activeFileName=top;
                if(populated){context.documents={{top,fixture.sources.value(fixture.file),nullptr,1}};context.workspaceDocuments=context.documents;}
                context.workspaceDocumentsSpecified=true;search->setContextProvider({});search->setSearchContext(context);
                search->setScope(ScopedSearchScope::File);search->setQueryText(populated?"core":QString());search->refresh();
                if(populated)QVERIFY(search->displayedResultCount()>=4);
            }else if(id=="problems"){
                auto* problems=owner<ProblemsPanelCoordinator>(&window);QVERIFY(problems);
                auto* index=SemanticIndex::getInstance();auto snapshot=index->snapshotToken().snapshot;
                if(!snapshot)snapshot=std::make_shared<SemanticIndexSnapshot>();
                QList<SemanticDiagnostic> values;
                if(populated)for(int i=0;i<30;++i){SemanticDiagnostic value;value.fileName=top;value.line=i+1;value.column=1;value.message=QString("Original layout diagnostic %1").arg(i);value.severity=SemanticDiagnostic::Warning;values<<value;}
                index->setSnapshot(std::make_shared<SemanticIndexSnapshot>(snapshot->withReplacedDiagnostics({top},values)));
                problems->setCurrentFileProvider([top]{return top;});problems->update();problems->tree()->expandAll();
            }else if(id=="rtlHighRiskEdit"){
                auto* editsOwner=owner<RtlHighRiskEditPanelCoordinator>(&window);QVERIFY(editsOwner);
                auto* edits=editsOwner->panel();QVERIFY(edits);
                if(populated){edits->beginRename(1,"top.instance_with_a_long_name_left.core","core","renamed_core",true);
                    RtlHighRiskEditPanelOutcome outcome;outcome.panelState=RtlHighRiskEditPanelState::Editing;outcome.message="Review the current rename parameters";outcome.canPreview=true;outcome.canCancel=true;edits->presentOutcome(outcome);
                }else edits->clearSession();
            }else if(id=="connections"){
                QVERIFY(window.semanticDocks->showConnectionPage(InstancePairConnectionCoordinator::panelId()));
                auto* connections=window.semanticDocks->instancePairConnectionCoordinator();QVERIFY(connections);
                InstancePairConnectionAnalysis analysis;
                if(populated){analysis.status=InstancePairConnectionStatus::Ready;analysis.failure=InstancePairConnectionFailure::None;analysis.message="Original connection layout data";
                    analysis.query.leftInstancePath="top.instance_with_a_long_name_left";analysis.query.rightInstancePath="top.instance_with_a_long_name_right";
                    analysis.blockView.left.instancePath=analysis.query.leftInstancePath;analysis.blockView.right.instancePath=analysis.query.rightInstancePath;
                    analysis.blockView.left.moduleName=analysis.blockView.right.moduleName="leaf";
                    analysis.blockView.left.instanceFile=analysis.blockView.right.instanceFile=top;
                    analysis.blockView.left.definitionFile=analysis.blockView.right.definitionFile=top;
                    for(int i=0;i<20;++i){InstancePairBlockPortView port;port.name=QString("original_port_%1").arg(i);analysis.blockView.left.ports<<port;analysis.blockView.right.ports<<port;}
                }
                connections->presentAnalysis(analysis);
                auto* connectionPanel=connections->panel();QVERIFY(connectionPanel);
                auto* page=window.tabManager->toolPage(id);QVERIFY(page && page->isAncestorOf(connectionPanel));
                QTRY_VERIFY(connectionPanel->isVisibleTo(&window));
                QVERIFY(!bottomHost->isAncestorOf(connectionPanel));QVERIFY(layout->activeBottomPanelId()!=id);
                auto* connectionScroll=connectionPanel->findChild<QScrollArea*>("instancePairConnectionPanelScroll");QVERIFY(connectionScroll);
                // Respect the native tab host's minimum after lazy pages are polished.
                QVERIFY(connectionsLayoutSettled(&window,connectionScroll));
                window.layout()->activate();
                window.resize(window.size().expandedTo(window.minimumSizeHint()));
                QVERIFY(connectionsLayoutSettled(&window,connectionScroll));
                if(populated)for(const auto* name:{"instancePairLeftItems","instancePairRightItems"}){
                    auto* ports=connectionPanel->findChild<QTreeWidget*>(name);QVERIFY(ports);int count=0;
                    for(QTreeWidgetItemIterator it(ports);*it;++it)count+=(*it)->text(0).startsWith("original_port_");
                    QCOMPARE(count,20);
                }
                sample=measure(connectionPanel,4);failures+=sample.failures;
                if(populated)for(const auto* name:{"instancePairLeftInstancePath","instancePairRightInstancePath","instancePairLeftModule","instancePairRightModule"}){
                    auto* info=connectionPanel->findChild<QLabel*>(name);QVERIFY(info && info->toolTip().contains(top));
                }
                QVERIFY(record(connectionPanel,"connections-main-area",sample,{{"populated",populated},
                    {"host","MainWindow / actual Connections tool page"},{"bottomWidthHeightApplicable",false},
                    {"routeReason","PanelLayoutController::activatePanel always routes Connections to the main area"}}));
                const auto controls=controlState(connectionPanel);
                const QSize windowSize=window.size(),panelSize=connectionPanel->size();
                int hostStep=0;
                for(const QSize requested:{windowSize+QSize(160,120),windowSize}){
                    window.resize(requested);QVERIFY(connectionsLayoutSettled(&window,connectionScroll));
                    QCOMPARE(controlState(connectionPanel),controls);
                    sample=measure(connectionPanel,4);failures+=sample.failures;
                    QVERIFY(record(connectionPanel,QString("connections-main-area-resize-%1").arg(++hostStep),sample,
                        {{"populated",populated},{"bottomWidthHeightApplicable",false},
                         {"windowHeight",window.height()},{"windowMinimumHeight",window.minimumHeight()}}));
                }
                QCOMPARE(connectionPanel->size(),panelSize);
                auto* toggle=connectionPanel->findChild<QToolButton*>("instancePairTransactionPreviewToggle");QVERIFY(toggle);
                toggle->click();QVERIFY(connectionsLayoutSettled(&window,connectionScroll));QVERIFY(toggle->isChecked());
                sample=measure(connectionPanel,4);failures+=sample.failures;
                QVERIFY(record(connectionPanel,"connections-preview",sample));
                QVERIFY(recordOverflow(connectionPanel,"connections-actions",failures));
                toggle->click();QVERIFY(connectionsLayoutSettled(&window,connectionScroll));QCOMPARE(controlState(connectionPanel),controls);
                continue;
            }
            auto* group=registry->groupForPage(panel);QVERIFY(group);
            const auto sizeContent=[&](const QSize& desired) {
                // Requested dimensions refer to the content, excluding shared tab chrome.
                const int chromeHeight=group->height()-panel->height();
                if (!layout->setPanelHeight(id,desired.height()+chromeHeight)) return false;
                for(int settle=0;settle<3;++settle) {
                    const int groupWidth=desired.width()+group->width()-panel->width();
                    splitter->setSizes({groupWidth,splitter->width()-groupWidth-splitter->handleWidth()});
                    WaitCost waitCost(10); QTest::qWait(10);
                }
                if (panel->size()!=desired) qWarning()<<"content-sizing"<<id<<"active"<<layout->activeBottomPanelId()
                    <<"requested"<<desired<<"actual"<<panel->size()<<"group"<<group->size()
                    <<"chrome"<<chromeHeight<<"stored"<<layout->panelHeight(id)<<"maximum"<<layout->maximumContentHeight()
                    <<"host"<<bottomHost->size()<<"window"<<window.size()<<"minimum"<<window.minimumSizeHint();
                if (panel->size()!=desired) {
                    auto* central=window.centralWidget();
                    qWarning()<<"central minimum"<<central->size()<<central->minimumSize()<<central->minimumSizeHint();
                    for(auto* child:central->findChildren<QWidget*>())
                        if(child->minimumSizeHint().height()>300 || child->minimumHeight()>300)
                            qWarning()<<"minimum contributor"<<child->metaObject()->className()<<child->objectName()<<child->size()
                                <<child->minimumSize()<<child->minimumSizeHint()<<"visible"<<child->isVisible();
                }
                return true;
            };
            for(int mode=0;mode<(id=="hotspot"?3:1);++mode){
                if(id=="hotspot"){auto* trace=qobject_cast<SignalTracePanel*>(panel);configure(trace,populated,mode);QVERIFY(SignalTraceTest::settled(*trace));}
                QVERIFY(sizeContent(QSize(width,height)));
                sample=measure(panel,height==160?2:4);
                if(panel->width()!=width)sample.failures<<QString("requested width %1; actual %2").arg(width).arg(panel->width());
                if(panel->height()!=height)sample.failures<<QString("requested content height %1; actual %2").arg(height).arg(panel->height());
                failures+=sample.failures;
                QVERIFY(record(panel,id+QString("-m%1").arg(mode),sample,{{"populated",populated},{"requestedWidth",width},{"requestedHeight",height},
                    {"bottomPanelIds",strings(ids)},{"host","MainWindow / PanelLayoutController / Context split"}}));
                if(id=="problems" || id=="scopedSearch" || id=="simulationEvents" || id=="rtlHighRiskEdit"){
                    const auto controls=controlState(panel);
                    int step=0;
                    for(const QSize changed:{QSize(480,280),QSize(220,160),QSize(width,height)}){
                        QVERIFY(sizeContent(changed));
                        QCOMPARE(panel->size(),changed);QCOMPARE(controlState(panel),controls);
                        sample=measure(panel,changed.height()==160?2:4);failures+=sample.failures;
                        QVERIFY(record(panel,id+QString("-resize-%1").arg(++step),sample,{{"populated",populated}}));
                    }
                    QVERIFY(recordOverflow(panel,id+"-options",failures));
                    if(id=="scopedSearch"){
                        auto* toggle=panel->findChild<QToolButton*>("scopedSearchReplaceGroupToggle");QVERIFY(toggle);
                        toggle->click();{ WaitCost waitCost(20); QTest::qWait(20); }QVERIFY(toggle->isChecked());
                        sample=measure(panel,height==160?2:4);failures+=sample.failures;
                        QVERIFY(record(panel,"scopedSearch-replace-expanded",sample,{{"populated",populated}}));
                        QVERIFY(recordOverflow(panel,"scopedSearch-replace-options",failures));
                        toggle->click();{ WaitCost waitCost(20); QTest::qWait(20); }QCOMPARE(controlState(panel),controls);
                    }
                }
            }
        }
        cost.checkpoint("bottom-panels");
        QVERIFY(window.close());
        cost.checkpoint("close-window");
        QVERIFY2(failures.isEmpty(),qPrintable(failures.join('\n')));
    }
};

int main(int argc,char** argv)
{
    QStandardPaths::setTestModeEnabled(true);
    QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
    QCoreApplication::setOrganizationName("ZeroSlackNarrowLayoutTest");QCoreApplication::setApplicationName("ZeroSlackNarrowLayoutTest");
    QTemporaryDir profile;QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,profile.path());QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,profile.path());
    qputenv("ZEROSLACK_SIMDOCK_LEGACY_SETTINGS_PATH",profile.filePath("legacy.ini").toUtf8());
    UserTemplateService::getInstance()->setGlobalTemplateFilePath(profile.filePath("templates.json"));
    if(!initializeUiStyleForTest())return 2;
    ApplicationThemeManager::instance().setAnimationsEnabled(false);
    QTemporaryDir artifacts;
    if(qEnvironmentVariableIsEmpty("ZEROSLACK_TEST_ARTIFACT_DIR"))qputenv("ZEROSLACK_TEST_ARTIFACT_DIR",artifacts.path().toUtf8());
    NarrowLayoutMatrix test;return QTest::qExec(&test,argc,argv);
}
#include "narrow_layout_test.moc"
