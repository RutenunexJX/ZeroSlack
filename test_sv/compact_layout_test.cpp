#include "applicationthememanager.h"
#include "testuistyle.h"
#include "roundedicons.h"
#include "compactlayout.h"
#include <QLineEdit>
#include <QPushButton>
#include "live_insight_test_utils.h"
#include "semanticindexsnapshot.h"
#include <QScopeGuard>
#include "insightvisualstyle.h"
#include "mainwindow.h"
#include "contextdockhost.h"
#include "liveinsightscontextview.h"
#include "liveinsighttoolpage.h"
#include "liveinsightscontextprovider.h"
#include "rtlinsightworkbench.h"
#include "rtlinsightspanelcoordinator.h"
#include "signalusagehotspotpanel.h"
#include "tabmanager.h"
#include "temporaryeditorcontextprovider.h"
#include <QProxyStyle>
#include <QStyleFactory>
#include <QStyleOptionTab>
#include <QStackedWidget>
#include <QTabBar>
#include <QToolButton>
#include <QRegularExpression>
#include <QPaintEngine>
#include <QVBoxLayout>
#include <QAbstractButton>
#include <QDockWidget>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QTest>
#include <QTemporaryDir>
#include <QSettings>
#include <QLabel>
#include <QScreen>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QSignalSpy>

namespace {
class TextEngine : public QPaintEngine {
public:
    TextEngine():QPaintEngine(QPaintEngine::AllFeatures){}
    QStringList labels;
    bool begin(QPaintDevice*) override { return true; }
    bool end() override { return true; }
    Type type() const override { return User; }
    void updateState(const QPaintEngineState&) override {}
    void drawPixmap(const QRectF&,const QPixmap&,const QRectF&) override {}
    void drawImage(const QRectF&,const QImage&,const QRectF&,Qt::ImageConversionFlags) override {}
    void drawPath(const QPainterPath&) override {}
    void drawPolygon(const QPointF*,int,PolygonDrawMode) override {}
    void drawTextItem(const QPointF&,const QTextItem& item) override { labels.append(item.text()); }
};
class TextDevice : public QPaintDevice {
public:
    mutable TextEngine engine;
    QSize size;
    explicit TextDevice(QSize size):size(size){}
    QPaintEngine* paintEngine() const override { return &engine; }
    int metric(PaintDeviceMetric metric) const override {
        if (metric==PdmWidth) return size.width();
        if (metric==PdmHeight) return size.height();
        if (metric==PdmDepth) return 32;
        if (metric==PdmDevicePixelRatio) return 1;
        if (metric==PdmDevicePixelRatioScaled) return devicePixelRatioFScale();
        return 96;
    }
};
QRect globalRect(QWidget* w) { return QRect(w->mapToGlobal(QPoint()), w->size()); }
struct Counts { int overlap=0, outside=0, clipped=0; QList<QWidget*> outsideWidgets; };
Counts audit(QWidget* root) {
    Counts result;
    const auto all=root->findChildren<QWidget*>();
    for (auto* w : all) {
        if (!w->isVisible() || w->size().isEmpty()) continue;
        const QRect rect=globalRect(w);
        if (auto* parent=w->parentWidget(); parent && !globalRect(parent).contains(rect)) {
            ++result.outside; result.outsideWidgets.append(w); qWarning() << "I4" << w << w->geometry() << "parent" << parent << parent->size();
        }
        if (auto* b=qobject_cast<QAbstractButton*>(w); b && b->width()<b->minimumSizeHint().width()) {
            ++result.clipped; qWarning() << "I5" << b << b->text() << b->size() << b->minimumSizeHint();
        }
        if (auto* b=qobject_cast<QAbstractButton*>(w); b && !b->text().isEmpty()) {
            const QString text=b->text().remove('&');
            if (b->fontMetrics().horizontalAdvance(text)>b->width()
                && !(text.endsWith(QChar(0x2026)) && !b->toolTip().isEmpty())) ++result.clipped;
        }
        for (auto* other : all) {
            if (other<=w || !other->isVisible() || other->parentWidget()!=w->parentWidget()) continue;
            if (!rect.intersected(globalRect(other)).isEmpty()) {
                ++result.overlap; qWarning() << "I3" << w << w->geometry() << other << other->geometry();
            }
        }
    }
    return result;
}
bool recordAudit(QWidget* root, const Counts& counts, const QString& phase={}) {
    const QString directory=qEnvironmentVariable("ZEROSLACK_TEST_ARTIFACT_DIR");
    if (directory.isEmpty()) return true;
    QDir().mkpath(directory);
    const QString name=QString::fromLatin1(QTest::currentTestFunction())+"_"+QTest::currentDataTag()+phase;
    QJsonArray controls;
    for (auto* w:root->findChildren<QWidget*>()) {
        if (!w->isVisible() || !w->objectName().startsWith("signalUsageHotspot")) continue;
        if (!qobject_cast<QAbstractButton*>(w) && !qobject_cast<QLineEdit*>(w)) continue;
        const QPoint at=w->mapTo(root,QPoint());
        controls.append(QJsonObject{{"name",w->objectName()},{"x",at.x()},{"y",at.y()},
            {"width",w->width()},{"height",w->height()},{"minimumWidth",w->minimumSizeHint().width()}});
    }
    const QJsonObject record{{"case",name},{"logicalWidth",root->width()},{"logicalHeight",root->height()},
        {"actualDpr",root->devicePixelRatioF()},{"scaleFactor",qEnvironmentVariable("QT_SCALE_FACTOR")},
        {"actualUiBackend",ApplicationThemeManager::instance().backend()==UiStyleBackend::Ela?"ela":"classic"},
        {"overlap",counts.overlap},{"outside",counts.outside},{"clipped",counts.clipped},{"controls",controls}};
    QFile file(directory+"/compact-measurements.jsonl");
    if (!file.open(QIODevice::Append)) return false;
    file.write(QJsonDocument(record).toJson(QJsonDocument::Compact)+"\n");
    return root->grab().save(directory+"/"+name+".png");
}
}
class CompactLayoutTest : public QObject {
    Q_OBJECT
private slots:
    void commonFlowLayoutHandlesVisibilityStretchAndResize() {
        QWidget host;
        auto* flow = new CompactFlowLayout(6);
        host.setLayout(flow);
        auto* first = new QPushButton(QStringLiteral("First"));
        auto* last = new QPushButton(QStringLiteral("Last"));
        auto* hidden = new QPushButton(QStringLiteral("Hidden control with a very long label"));
        flow->addWidget(first);
        flow->addItem(new QSpacerItem(0,0,QSizePolicy::Expanding,QSizePolicy::Minimum));
        flow->addWidget(last);
        flow->addWidget(hidden);
        hidden->hide();
        host.resize(480,160); host.show();
        flow->setGeometry(QRect(0,0,480,160));
        QCOMPARE(first->y(), last->y());
        QCOMPARE(last->geometry().right(), 479);
        const int visibleHeight = flow->heightForWidth(180);
        hidden->show();
        QVERIFY(flow->heightForWidth(180) > visibleHeight);
        hidden->hide();
        QCOMPARE(flow->heightForWidth(180), visibleHeight);
        const auto before = last->geometry();
        flow->heightForWidth(20);
        QCOMPARE(last->geometry(), before);
        flow->setGeometry(QRect(0,0,90,160));
        QVERIFY(last->y() > first->y());
        QCOMPARE(flow->minimumHeightForWidth(90), flow->heightForWidth(90));
        auto* field = new QLineEdit;
        field->setMinimumWidth(0);
        field->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        flow->addWidget(field);
        field->show();
        flow->setGeometry(QRect(0,0,120,160));
        QVERIFY(field->width() <= 120);
        const auto smallFontHeight = flow->heightForWidth(180);
        auto font = host.font(); font.setPointSizeF(font.pointSizeF() * 1.5);
        host.setFont(font); flow->invalidate();
        QVERIFY(flow->heightForWidth(180) >= smallFontHeight);
        flow->setGeometry(QRect(0,0,480,200));
        QCOMPARE(last->height(), last->sizeHint().height());
        delete flow->takeAt(flow->count()-1);
        delete field;
        flow->setGeometry(QRect(0,0,480,160));
        QCOMPARE(flow->count(), 4);
    }
    void mainWindowGeometry() {
        MainWindow window;
        window.show(); window.resize(1160,900); QTest::qWait(100);
        auto* context=window.findChild<ContextDockHost*>();QVERIFY(context);
        auto resource=LiveInsightsContextProvider::resourceForKind(LiveInsightKind::Hotspot,{});
        QVERIFY(context->addResource(resource,new LiveInsightsContextView(nullptr,LiveInsightKind::Hotspot),true));
        QWidget* ancestor=context;
        while (ancestor && !qobject_cast<QDockWidget*>(ancestor)) ancestor=ancestor->parentWidget();
        auto* contextDock=qobject_cast<QDockWidget*>(ancestor);QVERIFY(contextDock);contextDock->show();
        const auto groups=window.findChildren<QTabWidget*>(QRegularExpression("editorTabGroup.*"));QVERIFY(!groups.isEmpty());
        auto* group=groups.first();
        group->setCurrentIndex(group->addTab(new LiveInsightToolPage(LiveInsightKind::Hotspot),"Signal Hotspot"));
        window.resize(1900,1040); QTest::qWait(100);
        auto* center=window.centralWidget();QVERIFY(center);
        for (auto* dock:window.findChildren<QDockWidget*>(QString(),Qt::FindDirectChildrenOnly)) {
            if (dock->isVisible() && !dock->isFloating())
                QVERIFY2(globalRect(center).intersected(globalRect(dock)).isEmpty(),qPrintable(dock->objectName()));
        }
        qInfo()<<"main window"<<window.size()<<"center"<<center->geometry()<<"layout"<<window.layout()->geometry();
    }
    void editorTabs_data() {
        QTest::addColumn<bool>("dark");
        QTest::addColumn<bool>("largeFont");
        for (bool dark : {false,true}) for (bool large : {false,true})
            QTest::newRow(qPrintable(QString("%1-%2").arg(dark?"dark":"light").arg(large?"large":"normal")))
                << dark << large;
    }
    void editorTabs() {
        QFETCH(bool,dark); QFETCH(bool,largeFont);
        auto& theme=ApplicationThemeManager::instance();
        const auto oldTheme=theme.mode();
        const auto restore=qScopeGuard([&]{theme.setMode(oldTheme);});
        theme.setMode(dark?ThemeMode::Dark:ThemeMode::Light);
        for (bool split : {false,true}) {
            QWidget host;
            auto* tabs=UiControls::editorTabWidget(&host);
            auto* layout=new QVBoxLayout(&host);layout->setContentsMargins(0,0,0,0);layout->addWidget(tabs);
            EditorSplitController controller(tabs); controller.setHost(&host);
            auto fill=[](QTabWidget* group) {
                for (const QString& name:{"uart_receive_controller.sv","uart_transmit_controller.sv","reset_synchronizer.sv","clock_generator.sv","axi_register_bank.sv","stream_fifo.sv","packet_decoder.sv","byte_serializer.sv"})
                    group->addTab(new QWidget,name);
                for (int i=0;i<group->count();i+=2)
                    group->setTabIcon(i,RoundedIcons::icon(RoundedIcons::File));
            };
            fill(tabs);
            if (split) fill(controller.createSplit(tabs,EditorSplitDirection::Right));
            for (auto* group:controller.groups()) group->show();
            // MainWindow reapplies this style after creating groups and changing themes.
            for (auto* group:controller.groups())
                group->tabBar()->setStyleSheet(InsightVisualStyle::tabBarStyleSheet(group->tabBar()->objectName()));
            host.resize(split ? 900 : 615,700);host.show();QTest::qWait(30);
            if (largeFont) {
                for (auto* group:controller.groups()) {
                    auto font=group->tabBar()->font();
                    if (font.pixelSize()>0) font.setPixelSize(qRound(font.pixelSize()*1.5));
                    else font.setPointSizeF(font.pointSizeF()*1.5);
                    group->tabBar()->setFont(font);
                }
            }
            QJsonArray evidence;
            for (int width : (split ? QList<int>{900,760,900} : QList<int>{615,480,615})) {
            host.resize(width,700);QApplication::processEvents();
            QCOMPARE(host.width(),width);
            for (auto* group:controller.groups()) {
                auto* bar=group->tabBar();
                QList<int> widths;
                int shortestPaintedText=1000;
                for (int i=0;i<bar->count();++i) {
                    group->setCurrentIndex(i);QTest::qWait(1);
                    widths.append(bar->tabRect(i).width());
                    if (ApplicationThemeManager::instance().backend()==UiStyleBackend::Classic)
                        QVERIFY(bar->tabRect(i).width()>=108);
                    else
                        QVERIFY(bar->tabRect(i).width()>=bar->fontMetrics().horizontalAdvance("aaa"));
                    TextDevice device(bar->size());
                    // Observe tab labels, excluding child arrow/close controls.
                    bar->render(&device,QPoint(),QRegion(),QWidget::DrawWindowBackground);
                    QVERIFY2(!device.engine.labels.isEmpty(),"Actual tab label painting was not observed");
                    for (QString text:device.engine.labels) {
                        text.remove(QChar(0x2026));text.remove("...");
                        shortestPaintedText=qMin(shortestPaintedText,int(text.trimmed().size()));
                        QVERIFY2(text.trimmed().size()>=3,qPrintable(text));
                    }
                    const QRect visible=bar->tabRect(i).intersected(bar->rect());
                    QVERIFY(!visible.isEmpty());
                    QCOMPARE(bar->tabAt(visible.center()),i);
                    QSignalSpy selected(bar,&QTabBar::tabBarClicked);
                    QTest::mouseClick(bar,Qt::LeftButton,Qt::NoModifier,visible.center());
                    QCOMPARE(selected.count(),1);QCOMPARE(selected.first().first().toInt(),i);
                    QCOMPARE(group->currentIndex(),i);
                }
                bool scrollVisible=false;
                for (auto* button:bar->findChildren<QToolButton*>()) {
                    scrollVisible|=button->isVisible();
                    if (button->isVisible()) QVERIFY2(button->width()>=button->minimumSizeHint().width(),
                        qPrintable(QString("%1 width=%2 minimum=%3 hint=%4 metric=%5 style=%6")
                            .arg(button->objectName()).arg(button->width()).arg(button->minimumSizeHint().width())
                            .arg(button->sizeHint().width()).arg(bar->style()->pixelMetric(QStyle::PM_TabBarScrollButtonWidth,nullptr,bar))
                            .arg(button->style()->metaObject()->className())));
                }
                qInfo() << "I1/I2 split=" << split << "widths=" << widths << "scroll=" << bar->usesScrollButtons() << scrollVisible;
                qInfo()<<"Tab geometry"<<host.size()<<group->size()<<bar->size()<<bar->isVisible()<<group->parentWidget()->isVisible();
                qInfo()<<"I2 shortest painted label excluding ellipsis"<<shortestPaintedText;
                QVERIFY(bar->usesScrollButtons());QVERIFY(scrollVisible);
                evidence.append(QJsonObject{{"split",split},{"hostWidth",width},{"group",group->objectName()},
                    {"barWidth",bar->width()},{"actualDpr",bar->devicePixelRatioF()},
                    {"shortestPaintedText",shortestPaintedText},{"fontPointSize",bar->font().pointSizeF()},
                    {"fontPixelSize",bar->font().pixelSize()}});
                // Exercise native close hit targets for the current and a visible
                // non-current tab; the controller must route the exact index.
                group->setCurrentIndex(0);QApplication::processEvents();
                for (int i : {0,1}) {
                    QWidget* close=bar->tabButton(i,QTabBar::RightSide);
                    if (!close) close=bar->tabButton(i,QTabBar::LeftSide);
                    QVERIFY(close && close->isVisible());
                    QVERIFY(bar->rect().contains(QRect(close->mapTo(bar,QPoint()),close->size())));
                    QSignalSpy requested(&controller,&EditorSplitController::tabCloseRequested);
                    QTest::mouseClick(close,Qt::LeftButton);
                    QCOMPARE(requested.count(),1);
                    QCOMPARE(qvariant_cast<QTabWidget*>(requested.first().at(0)),group);
                    QCOMPARE(requested.first().at(1).toInt(),i);
                }
            }
            }
            const QString artifacts=qEnvironmentVariable("ZEROSLACK_TEST_ARTIFACT_DIR");
            if (!artifacts.isEmpty()) {
                const QString stem=artifacts+QString("/editor-tabs-%1-%2-%3")
                    .arg(dark?"dark":"light").arg(largeFont?"large":"normal").arg(split?"split":"single");
                QFile output(stem+".json");QVERIFY(output.open(QIODevice::WriteOnly));
                output.write(QJsonDocument(evidence).toJson());
                QVERIFY(host.grab().save(stem+".png"));
            }
        }
    }
    void untitledNames() {
        QTabWidget editors;
        TabManager manager(&editors);
        manager.createNewTab();manager.createNewTab();
        TemporaryEditorContextProvider provider(&manager);
        const auto resource=provider.activationResource({});
        ContextDockHost host;
        auto* view=provider.createView(resource,&host);
        provider.observeViewResourceChanges(view,&host,[&](const ContextResource& updated) { host.updateResource(updated); });
        QVERIFY(provider.activateView(view,resource));
        QVERIFY(host.addResource(resource,view));
        host.show();QTest::qWait(30);
        auto* header=host.sectionHeader(resource.stableKey());QVERIFY(header);
        auto* title=header->findChild<QLabel*>("contextSectionTitle");QVERIFY(title);
        const QRegularExpression uuid("[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-");
        for (const auto& key:host.resourceKeys()) QVERIFY(!uuid.match(host.sectionHeader(key)->findChild<QLabel*>("contextSectionTitle")->text()).hasMatch());
        QCOMPARE(title->text(),editors.tabText(editors.currentIndex()));
        QCOMPARE(header->toolTip(),resource.uri.toString());
        qInfo()<<"I6 editor="<<editors.tabText(editors.currentIndex())<<"context="<<title->text();
        QVERIFY(manager.duplicateCurrentView());QTest::qWait(30);
        QCOMPARE(title->text(),editors.tabText(editors.currentIndex()));
        qInfo()<<"I6 duplicated editor="<<editors.tabText(editors.currentIndex())<<"context="<<title->text();
        manager.getCurrentEditor()->insertPlainText("module test; endmodule");QTest::qWait(30);
        QCOMPARE(title->text(),editors.tabText(editors.currentIndex()));
        QCOMPARE(header->toolTip(),resource.uri.toString());
        for (int width : {220,270,340,480}) {
            host.setFixedWidth(width);host.resize(width,900);QTest::qWait(30);
            const auto c=audit(&host);
            qInfo()<<"Temporary editor"<<width<<"I3/I4/I5"<<c.overlap<<c.outside<<c.clipped;
            QVERIFY(c.overlap==0 && c.outside==0 && c.clipped==0);
        }
    }
    void insightSectionHeights() {
        // Three sections share the dock proportionally. Equal saved weights
        // must not change their sizes or leave the canvas below usable size.
        auto* index = SemanticIndex::getInstance();
        const auto previous = index->snapshotToken();
        const auto restore = qScopeGuard([&] { index->setSnapshot(previous.snapshot); });
        index->setSnapshot(std::make_shared<SemanticIndexSnapshot>());
        const QRect available=QApplication::primaryScreen()->availableGeometry();
        const int hostHeight=available.height();
        const int width=qMin(480,available.width());
        const QList<LiveInsightKind> kinds={LiveInsightKind::Module,LiveInsightKind::Kernel,LiveInsightKind::State};
        struct Sample { int content=0; int canvas=0; int sectionHeight=0; };
        auto measure=[&](int suggested)->Sample {
            ContextDockHost host;
            QStringList keys;
            for (LiveInsightKind kind : kinds) {
                auto resource=LiveInsightsContextProvider::resourceForKind(kind,{});
                auto* view=new LiveInsightsContextView(nullptr,kind);
                view->setToolContextSource([]{
                    LiveInsightToolContext context;
                    context.workspaceId="workspace-a";
                    context.documentId="document-a";
                    context.fileName="uart.sv";
                    context.moduleName="uart";
                    context.signalName="byte_data";
                    return context;
                });
                QTest::qVerify(host.addResource(resource,view,true),"addResource","",__FILE__,__LINE__);
                keys.append(resource.stableKey());
            }
            host.setFixedWidth(width); host.resize(width,hostHeight); host.show(); QTest::qWait(30);
            for (const auto& key : keys) {
                auto* view = qobject_cast<LiveInsightsContextView*>(host.viewForResource(key));
                auto* surface = view ? view->surfaceForTest() : nullptr;
                auto* panel = surface ? surface->workbenchForTest()->rtlSurfaceForTest() : nullptr;
                if (panel && !QTest::qVerify(waitForLiveInsightReports(*panel),
                    "graph report ready before geometry measurement", "", __FILE__, __LINE__)) return {};
            }
            if (suggested>0) { for (const QString& key : keys) host.setSectionHeight(key,suggested); }
            QTest::qWait(30);
            const QString artifacts=qEnvironmentVariable("ZEROSLACK_TEST_ARTIFACT_DIR");
            if (!artifacts.isEmpty()) {
                QDir().mkpath(artifacts);
                host.grab().save(artifacts+QString("/insight_sections_%1.png")
                                     .arg(suggested>0 ? "suggested" : "split"));
            }
            const QString focused=keys.last();
            Sample sample;
            sample.sectionHeight=host.sectionWidget(focused)->height();
            QWidget* view=host.viewForResource(focused);
            sample.content=view ? view->height() : 0;
            for (const char* name : {"rtlInsightsGraphView","insightCanvasView"}) {
                if (auto* canvas=view ? view->findChild<QWidget*>(QString::fromLatin1(name)) : nullptr) {
                    if (canvas->isVisible() && canvas->height()>sample.canvas) sample.canvas=canvas->height();
                }
            }
            return sample;
        };
        const Sample split=measure(0);
        const Sample suggested=measure(LiveInsightsContextProvider::suggestedSectionHeight());
        const QString report=QString("host %1x%2 scale %3 | split section/content/canvas %4/%5/%6 | "
                                     "suggested section/content/canvas %7/%8/%9 | canvas share %10%")
            .arg(width).arg(hostHeight)
            .arg(qEnvironmentVariable("QT_SCALE_FACTOR","1"))
            .arg(split.sectionHeight).arg(split.content).arg(split.canvas)
            .arg(suggested.sectionHeight).arg(suggested.content).arg(suggested.canvas)
            .arg(suggested.content>0 ? suggested.canvas*100/suggested.content : 0);
        qInfo().noquote()<<report;
        // This binary produces no console output under the test harness, so the
        // measurement the suggested height is chosen from is written out.
        if (const QString artifacts=qEnvironmentVariable("ZEROSLACK_TEST_ARTIFACT_DIR");!artifacts.isEmpty()) {
            QDir().mkpath(artifacts);
            QFile file(artifacts+"/insight_section_heights.txt");
            if (file.open(QIODevice::Append|QIODevice::Text))
                file.write(report.toUtf8()+"\n");
        }
        QVERIFY(qAbs(suggested.sectionHeight-split.sectionHeight)<=1);
        QVERIFY(qAbs(suggested.canvas-split.canvas)<=1);
        QVERIFY(suggested.canvas>0 && suggested.content>0);
        QVERIFY(suggested.canvas*10>=suggested.content*4);
    }
    void contextWidths_data() {
        QTest::addColumn<int>("width");
        QTest::addColumn<bool>("withSurface");
        QTest::addColumn<bool>("dark");
        for (bool dark:{false,true}) for (bool surface:{false,true}) for (int width:{220,270,340,480})
            QTest::newRow(qPrintable(QString("%1-%2-%3").arg(dark?"dark":"light").arg(surface?"surface":"summary").arg(width)))
                << width << surface << dark;
    }
    void contextWidths() {
        QFETCH(int,width); QFETCH(bool,withSurface); QFETCH(bool,dark);
        auto& theme=ApplicationThemeManager::instance();
        const auto original=theme.mode();
        const auto restore=qScopeGuard([&]{theme.setMode(original);});
        theme.setMode(dark?ThemeMode::Dark:ThemeMode::Light);
        // withSurface repeats the audit for the product path, where the
        // section hosts the real insight surface instead of the summary card.
            ContextDockHost host;
            auto resource=LiveInsightsContextProvider::resourceForKind(LiveInsightKind::Hotspot,{});
            auto* view=new LiveInsightsContextView(nullptr,LiveInsightKind::Hotspot);
            if (withSurface) {
                view->setToolContextSource([]{
                    LiveInsightToolContext context;
                    context.workspaceId="workspace-a";
                    context.documentId="document-a";
                    context.fileName="uart.sv";
                    context.moduleName="uart";
                    context.signalName="byte_data";
                    return context;
                });
            }
            QVERIFY(host.addResource(resource,view,true));
            host.setFixedWidth(width); host.resize(width,900); host.show(); QTest::qWait(30);
            QVERIFY(withSurface ? view->surfaceForTest()!=nullptr : view->surfaceForTest()==nullptr);
            auto c=audit(&host);
            QVERIFY(recordAudit(&host,c));
            const QString artifacts=qEnvironmentVariable("ZEROSLACK_TEST_ARTIFACT_DIR");
            if (withSurface && !artifacts.isEmpty() && (width==340 || width==480)) {
                QDir().mkpath(artifacts);
                QVERIFY(host.grab().save(artifacts+QString("/context_section_surface_%1.png").arg(width)));
            }
            qInfo() << "Context" << width << "surface" << withSurface << "actual" << host.width() << "I3/I4/I5" << c.overlap << c.outside << c.clipped;
            if (!artifacts.isEmpty()) {
                // This binary has no console under the harness; the audit
                // numbers are written out so they can be quoted.
                QDir().mkpath(artifacts);
                QFile file(artifacts+"/context_widths.txt");
                if (file.open(QIODevice::Append|QIODevice::Text)) {
                    file.write(QString("width %1 surface %2 | overlap/outside/clipped %3/%4/%5\n")
                                   .arg(width).arg(withSurface ? "yes" : "no")
                                   .arg(c.overlap).arg(c.outside).arg(c.clipped).toUtf8());
                }
            }
            QVERIFY(host.width()==width);
            QVERIFY(c.overlap==0 && c.outside==0 && c.clipped==0);
    }
    void fullViewWidths_data() {
        QTest::addColumn<int>("width");
        QTest::addColumn<bool>("dark");
        for (bool dark:{false,true}) for (int width:{220,270,340,480})
            QTest::newRow(qPrintable(QString("%1-%2").arg(dark?"dark":"light").arg(width))) << width << dark;
    }
    void fullViewWidths() {
        QFETCH(int,width); QFETCH(bool,dark);
        auto& theme=ApplicationThemeManager::instance();
        const auto original=theme.mode();
        const auto restore=qScopeGuard([&]{theme.setMode(original);});
        theme.setMode(dark?ThemeMode::Dark:ThemeMode::Light);
            ContextDockHost host;
            auto resource=LiveInsightsContextProvider::resourceForKind(LiveInsightKind::Hotspot,{});
            auto* page=new LiveInsightToolPage(LiveInsightKind::Hotspot);
            auto* rtl=page->workbenchForTest()->rtlSurfaceForTest();QVERIFY(rtl);
            auto* hotspot=rtl->signalUsageHotspotPanelForTest();
            SignalUsageHotspotReport report;report.found=true;report.declarationDisplayName="byte_data";
            SignalUsageHotspotItem item;item.role=SignalUsageHotspotRole::Read;item.moduleName="uart";item.fileName="uart.sv";item.line=10;item.snippet="assign out_data = byte_data;";
            report.items.append(item);
            SignalUsageHotspotTrackLane lane;lane.moduleName=item.moduleName;lane.fileName=item.fileName;lane.startLine=10;lane.endLine=20;lane.count=1;
            SignalUsageHotspotTrackPosition position;position.itemIndex=0;position.role=item.role;position.line=10;lane.positions.append(position);report.trackLanes.append(lane);
            hotspot->renderReportForTest(report);
            rtl->stackForTest()->setCurrentWidget(hotspot);
            QVERIFY(host.addResource(resource,page,true));
            host.setFixedWidth(width); host.resize(width,900); host.show(); QTest::qWait(30);
            auto c=audit(&host);
            QVERIFY(recordAudit(&host,c));
            qInfo() << "Full view" << width << "actual" << host.width() << "I3/I4/I5" << c.overlap << c.outside << c.clipped;
            const QString root=qEnvironmentVariable("ZEROSLACK_TEST_ARTIFACT_DIR");
            if (!root.isEmpty() && (width==220 || width==480)) {
                QDir().mkpath(root);QVERIFY(host.grab().save(root+QString("/context_width_%1.png").arg(width)));
            }
            QVERIFY(host.width()==width);
            QVERIFY(c.overlap==0 && c.outside==0 && c.clipped==0);
            auto* matrix=hotspot->findChild<QPushButton*>("signalUsageHotspotMatrixModeButton");
            auto* track=hotspot->findChild<QPushButton*>("signalUsageHotspotTrackModeButton");
            auto* search=hotspot->findChild<QLineEdit*>("signalUsageHotspotSearch");
            auto* more=hotspot->findChild<QToolButton*>("signalUsageHotspotMoreButton");
            QVERIFY(matrix && track && search && more && more->menu());
            QTest::mouseClick(matrix,Qt::LeftButton); QVERIFY(hotspot->matrixModeForTest());
            QTest::mouseClick(track,Qt::LeftButton); QVERIFY(!hotspot->matrixModeForTest());
            search->setText("byte_data");
            for (auto* action:hotspot->graphViewActionsForTest()) QVERIFY(more->menu()->actions().contains(action));
            for (int resized:{480,220,width}) {
                host.setFixedWidth(resized);QTest::qWait(30);
                const auto after=audit(&host);
                QVERIFY(recordAudit(&host,after,QString("-resize-%1").arg(resized)));
                QVERIFY(after.overlap==0 && after.outside==0 && after.clipped==0);
                QCOMPARE(search->text(),QString("byte_data"));
                QVERIFY(!hotspot->matrixModeForTest());
                QVERIFY(matrix->isVisible() && track->isVisible() && more->isVisible());
            }
    }
};
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    if (!initializeUiStyleForTest()) return 3;
    ApplicationThemeManager::instance().applyToApplication();
    CompactLayoutTest test; return QTest::qExec(&test,argc,argv);
}
#include "compact_layout_test.moc"
