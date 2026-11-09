#include "mainwindow.h"
#include "../src/simulation/simdock/ui/simulationruncoordinator.h"
#include "automationservice.h"
#include "shareddocument.h"
#include "mycodeeditor.h"
#include "editorsemanticcontextservice.h"
#include "applicationthememanager.h"
#include "contextworkspacecontroller.h"
#include "contextfloatingwindow.h"
#include "navigationwidget.h"
#include "navigationpanecoordinator.h"
#include "panellayoutcontroller.h"
#include "tabmanager.h"
#include "workspacemanager.h"
#include "workspacesessioncoordinator.h"
#include "usertemplateservice.h"
#include "crashrecoveryservice.h"
#include "testuistyle.h"
#include "uicontrols.h"
#include "roundedicons.h"
#include "../src/integrations/simdock/simdockcontextprovider.h"
#include "../src/integrations/simdock/simdockcontextview.h"
#include "../src/simulation/simdock/ui/workbench.h"
#include "../src/simulation/simdock/core/workspace.h"
#include <QtTest>
#include <QAction>
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QCloseEvent>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDockWidget>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QScopeGuard>
#include <QPlainTextEdit>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTabBar>
#include <QTabWidget>
#include <QTranslator>
#include <QStyleHints>
#include <cmath>

namespace {
bool put(const QString& path, const QByteArray& bytes) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile f(path); return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}
QByteArray read(const QString& path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }
template<class T> T* child(QObject* owner, const char* name) {
    auto* value = owner->findChild<T*>(QLatin1String(name));
    if (!value) qFatal("Missing control: %s", name);
    return value;
}
void click(QObject* owner, const char* name) { child<QAbstractButton>(owner, name)->click(); }
const auto key = QStringLiteral("simdock:workbench");
struct Host {
    QTemporaryDir data;
    std::unique_ptr<MainWindow> window;
    simdock::Project project;
    QString workspace, other;
    Host() {
        workspace = data.filePath("workspace"); other = data.filePath("other");
        QDir().mkpath(other);
        put(workspace + "/dut.sv", "module dut(input logic clk, input logic rst, output logic q); assign q = clk & ~rst; endmodule\n");
        put(workspace + "/extra.sv", "module extra; endmodule\n");
        put(workspace + "/tb_manual.sv", "module tb_manual; logic clk=0,rst=0,q; dut d(.*); always #5 clk=~clk; initial #20 $finish; endmodule\n");
        project = simdock::newProject("Simulation project");
        project.sources = {"dut.sv", "extra.sv"}; project.dutName = "dut"; project.dutFile = "dut.sv";
        project.inputMode = simdock::InputMode::ExistingTb;
        project.tbFile = "tb_manual.sv"; project.tbName = "tb_manual"; project.durationNs = 1600;
        QString error;
        if (!simdock::saveProject(workspace, project, &error)) qFatal("%s", qPrintable(error));
        qputenv("ZEROSLACK_SESSION_STORAGE_PATH", data.filePath("sessions.ini").toUtf8());
        qputenv("ZEROSLACK_EDITING_TIME_STORAGE_PATH", data.filePath("editing.json").toUtf8());
        window = std::make_unique<MainWindow>();
        window->tabManager->setCrashRecoveryService(std::make_unique<CrashRecoveryService>(data.filePath("recovery")));
        window->workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
        window->resize(1400, 850); window->show();
    }
    SimDockContextView* view() const { return window->findChild<SimDockContextView*>(); }
    simdock::Workbench* panel() const { return view()->workbench(); }
    WorkspaceSessionCoordinator* sessions() const { return window->findChild<WorkspaceSessionCoordinator*>(); }
    ContextWorkspaceController* context() const { return window->findChild<ContextWorkspaceController*>(); }
    NavigationWidget* navigation() const { return window->findChild<NavigationWidget*>(); }
    QPlainTextEdit* log() const { return child<QPlainTextEdit>(window.get(), "simulationLog"); }
    simdock::Project saved() const {
        for (const auto& p : simdock::loadProjects(workspace)) if (p.id == project.id) return p;
        return {};
    }
    bool open() { return sessions()->openWorkspace(workspace) && panel()->openProject(project.id).isEmpty(); }
    bool simulation() {
        return panel()->isVisible();
    }
    void runLog() { click(window.get(), "bottomPanelButton_simulationLog"); }
};
}

class SimulationWorkspaceUiTest : public QObject {
    Q_OBJECT
    void recordNavigation(QTabBar* bar,const QString& name) {
        const auto output=qEnvironmentVariable("ZEROSLACK_NAVIGATION_UI_REPORT");if(output.isEmpty())return;
        QDir().mkpath(output);QTest::qWait(20);
        const auto pixels=bar->grab();const auto image=pixels.toImage();const qreal dpr=pixels.devicePixelRatio();
        QVERIFY(pixels.save(QDir(output).filePath(name+".png")));
        QJsonArray items;
        for(int i=0;i<bar->count();++i){const auto r=bar->tabRect(i);items.append(QJsonObject{{"index",i},{"rect",QJsonArray{r.x(),r.y(),r.width(),r.height()}},{"text",bar->tabText(i)},{"accessible",bar->accessibleTabName(i)},{"tooltip",bar->tabToolTip(i)}});}
        QJsonObject record{{"width",bar->width()},{"minimum",bar->minimumSizeHint().width()},{"dpr",dpr},{"fontPointSize",bar->font().pointSizeF()},{"fontHeight",bar->fontMetrics().height()},{"iconWidth",bar->iconSize().width()},{"current",bar->currentIndex()},{"focus",bar->hasFocus()},{"tabs",items}};
        if(bar->currentIndex()>=0){
            const auto r=bar->tabRect(bar->currentIndex());const auto text=bar->tabText(bar->currentIndex());
            record["labelWidth"]=bar->fontMetrics().horizontalAdvance(text);record["textSpace"]=r.width()-qMax(36,bar->iconSize().width()+24)-6;
            const auto sample=[&](int x,int y){return image.pixelColor(qBound(0,qRound(x*dpr),image.width()-1),qBound(0,qRound(y*dpr),image.height()-1));};
            const QColor fill=sample(r.left()+10,r.top()+5);record["selectedFill"]=fill.name();
            QCOMPARE(fill.lightness()<128,isDarkTheme(ApplicationThemeManager::instance().mode()));
            const bool dark=fill.lightness()<128;QColor ink=fill;
            for(int x=r.left()+38;x<r.right()-12;++x)for(int y=r.top()+7;y<r.bottom()-7;++y){const auto c=sample(x,y);if(dark?c.lightness()>ink.lightness():c.lightness()<ink.lightness())ink=c;}
            const auto luminance=[](QColor c){const auto linear=[](double v){return v<=.04045?v/12.92:std::pow((v+.055)/1.055,2.4);};return .2126*linear(c.redF())+.7152*linear(c.greenF())+.0722*linear(c.blueF());};
            const double a=luminance(ink),b=luminance(fill);record["sampledInk"]=ink.name();record["sampledContrast"]=(qMax(a,b)+.05)/(qMin(a,b)+.05);
            if(!text.isEmpty()&&record["textSpace"].toInt()>=record["labelWidth"].toInt())QVERIFY(record["sampledContrast"].toDouble()>=4.5);
        }
        QVERIFY(put(QDir(output).filePath(name+".json"),QJsonDocument(record).toJson()));
    }
    void ready(Host& f) { QTRY_VERIFY_WITH_TIMEOUT(!f.window->workspaceManager->isWorkspaceScanActive() && f.window->workspaceManager->projectSnapshot().sourceDiscoveryComplete,15000); }
    void capture(Host& f, const QString& name) {
        const auto output = qEnvironmentVariable("ZEROSLACK_SIMULATION_UI_REPORT");
        if (output.isEmpty()) return;
        QDir().mkpath(output); QTest::qWait(80);
        const auto pixels = f.window->grab();
        QVERIFY(pixels.save(QDir(output).filePath(name + ".png")));
        const auto bounds = f.window->screen()->availableGeometry();
        QVERIFY(put(QDir(output).filePath(name + ".json"), QJsonDocument(QJsonObject{
            {"platform", QGuiApplication::platformName()}, {"dpr", f.window->devicePixelRatioF()},
            {"width", f.window->width()}, {"height", f.window->height()}, {"pixelWidth", pixels.width()}, {"pixelHeight", pixels.height()},
            {"screenWidth", bounds.width()}, {"screenHeight", bounds.height()}}).toJson()));
    }
private slots:
    void navigationCompactGeometry() {
        const QStringList names{QStringLiteral("Files"),QStringLiteral("Design"),QStringLiteral("Build inputs")};
        for (int count=0;count<=3;++count) {
            std::unique_ptr<QTabWidget> tabs(UiControls::navigationTabWidget());
            tabs->setIconSize(QSize(20,20));auto* bar=tabs->tabBar();
            for(int i=0;i<count;++i){tabs->addTab(new QWidget,RoundedIcons::icon(RoundedIcons::File),QString());bar->setAccessibleTabName(i,names[i]);bar->setTabToolTip(i,names[i]);}
            const auto labels=[&]{for(int i=0;i<count;++i)tabs->setTabText(i,i==tabs->currentIndex()?names[i]:QString());};
            connect(tabs.get(),&QTabWidget::currentChanged,tabs.get(),labels);labels();
            if(count)tabs->setCurrentIndex(count-1);
            const int minimum=count*44;
            QCOMPARE(bar->minimumSizeHint().width(),minimum);
            for(int extra:{0,28,70,300}) {
                tabs->setFixedSize(qMax(50,minimum+extra+4),150);tabs->show();QTest::qWait(40);
                QVERIFY(!bar->usesScrollButtons());
                for(int i=0;i<count;++i) {
                    QVERIFY2(bar->rect().contains(bar->tabRect(i)),qPrintable(QString("count %1, extra %2, bar %3, tab %4: %5..%6").arg(count).arg(extra).arg(bar->width()).arg(i).arg(bar->tabRect(i).left()).arg(bar->tabRect(i).right())));
                    QVERIFY(bar->tabRect(i).width()>=44);
                    QCOMPARE(bar->tabText(i),i==bar->currentIndex()?names[i]:QString());
                    QCOMPARE(bar->accessibleTabName(i),names[i]);QCOMPARE(bar->tabToolTip(i),names[i]);
                }
                for(auto* button:bar->findChildren<QToolButton*>())QVERIFY(!button->isVisible());
                if(count==3)recordNavigation(bar,QString("geometry-%1").arg(extra));
            }
            while(tabs->count())tabs->removeTab(tabs->count()-1);
            QCOMPARE(bar->currentIndex(),-1);QCOMPARE(bar->minimumSizeHint().width(),0);
        }
    }
    void navigationInitializationAndMetrics() {
        NavigationWidget navigation; navigation.resize(500,400); navigation.show();
        auto* tabs=child<QTabWidget>(&navigation,"navigationTabs"); auto* bar=tabs->tabBar();
        QCOMPARE(tabs->count(),2); QCOMPARE(tabs->tabText(0),QString("Files")); QCOMPARE(tabs->tabText(1),QString("Design"));
        navigation.setActiveTab(NavigationWidget::DesignTab);
        QCOMPARE(tabs->currentIndex(),1); QCOMPARE(tabs->tabText(1),QString("Design"));
        class Translation final : public QTranslator {
        public:
            bool isEmpty() const override { return false; }
            QString translate(const char* context,const char* text,const char*,int) const override {
                if(QByteArray(context)!="NavigationWidget")return {};
                if(QByteArray(text)=="Files")return QStringLiteral("Workspace source documents");
                if(QByteArray(text)=="Design")return QString::fromUtf8("e\xCC\x81 中文 design hierarchy");
                return {};
            }
        } translation;
        QVERIFY(QCoreApplication::installTranslator(&translation)); QCoreApplication::processEvents();
        QVERIFY(bar->tabText(1).startsWith(QString::fromUtf8("e\xCC\x81")));
        QCOMPARE(bar->accessibleTabName(0),QString("Workspace source documents"));
        QFont large=bar->font();large.setPointSizeF(22);bar->setFont(large);navigation.resize(200,400);QTest::qWait(40);
        for(int i=0;i<2;++i){QVERIFY(bar->rect().contains(bar->tabRect(i)));QVERIFY(bar->tabRect(i).width()>=44);}
        recordNavigation(bar,"translated-large-font");
        navigation.hide();navigation.resize(600,400);navigation.show();QTest::qWait(40);recordNavigation(bar,"translated-restored");
        QCoreApplication::removeTranslator(&translation);QCoreApplication::processEvents();
        QCOMPARE(bar->tabText(1),QString("Design")); QCOMPARE(bar->accessibleTabName(0),QString("Files"));
        navigation.setActiveTab(static_cast<NavigationWidget::NavigationTab>(2)); QCOMPARE(navigation.activeTab(),0);
    }
    void navigationInputTargets() {
        NavigationWidget navigation;navigation.resize(650,350);navigation.show();
        auto* tabs=child<QTabWidget>(&navigation,"navigationTabs");auto* bar=tabs->tabBar();
        // A longer translated first label makes a stationary second click hit the following tab after reflow.
        tabs->setTabText(0,QStringLiteral("Workspace source documents"));QTest::qWait(40);
        const auto original=bar->tabRect(1);const QPoint position=original.center();
        QSignalSpy changed(tabs,&QTabWidget::currentChanged);QSignalSpy clicked(bar,&QTabBar::tabBarClicked);
        QTest::mousePress(bar,Qt::LeftButton,Qt::NoModifier,position);
        QCOMPARE(tabs->currentIndex(),1);QVERIFY(bar->tabRect(1).left()!=original.left());
        QTest::mouseRelease(bar,Qt::LeftButton,Qt::NoModifier,position);
        QCOMPARE(tabs->currentIndex(),1);
        QTest::mouseClick(bar,Qt::LeftButton,Qt::NoModifier,position);
        QTest::mouseDClick(bar,Qt::LeftButton,Qt::NoModifier,position);
        QTest::mouseRelease(bar,Qt::LeftButton,Qt::NoModifier,position);
        QCOMPARE(tabs->currentIndex(),1);QCOMPARE(changed.size(),1);
        for(const auto& args:clicked)QCOMPARE(args[0].toInt(),1);
        recordNavigation(bar,"stationary-double-click");
        QTest::keyClick(bar,Qt::Key_Left);QCOMPARE(tabs->currentIndex(),0);
        QTest::keyClick(bar,Qt::Key_Right);QCOMPARE(tabs->currentIndex(),1);
        QTest::mousePress(bar,Qt::LeftButton,Qt::NoModifier,bar->tabRect(0).center());
        QTest::mouseMove(bar,QPoint(-30,-30));QTest::mouseRelease(bar,Qt::LeftButton,Qt::NoModifier,QPoint(-30,-30));
        QCOMPARE(tabs->currentIndex(),0);
        QTest::mouseClick(bar,Qt::LeftButton,Qt::NoModifier,bar->tabRect(1).center());QCOMPARE(tabs->currentIndex(),1);
        recordNavigation(bar,"keyboard-and-cancel");
    }
    void navigationCompactMainWindow_data() {
        QTest::addColumn<int>("width");QTest::addColumn<bool>("dark");
        QTest::newRow("wide-light")<<1280<<false;QTest::newRow("wide-dark")<<1280<<true;
        QTest::newRow("narrow-light")<<780<<false;QTest::newRow("narrow-dark")<<780<<true;
    }
    void navigationCompactMainWindow() {
        QFETCH(int,width);QFETCH(bool,dark);Host f;QVERIFY(f.open());ready(f);
        f.window->resize(width,720);ApplicationThemeManager::instance().setMode(dark?ThemeMode::Dark:ThemeMode::Light);
        auto* tabs=child<QTabWidget>(f.window.get(),"navigationTabs");auto* bar=tabs->tabBar();
        for(int target=0;target<2;++target) {
            QTest::keyClick(bar,Qt::Key_Right);
            QTest::mouseClick(bar,Qt::LeftButton,Qt::NoModifier,bar->tabRect(target).center());QCOMPARE(tabs->currentIndex(),target);
            QTest::qWait(40);
            for(int i=0;i<2;++i){QCOMPARE(bar->tabText(i),bar->accessibleTabName(i));QVERIFY(bar->rect().contains(bar->tabRect(i)));}
            recordNavigation(bar,QString("%1-tab%2").arg(QTest::currentDataTag()).arg(target));
            capture(f,QString("navigation-%1-tab%2").arg(QTest::currentDataTag()).arg(target));
        }
        QVERIFY(f.sessions()->saveSession(false));QVERIFY(f.sessions()->openWorkspace(f.other));QVERIFY(f.sessions()->openWorkspace(f.workspace));
        QCOMPARE(tabs->currentIndex(),1);QCOMPARE(bar->tabText(1),QString("Design"));
    }
    void navigationMotionRetainsBuildInputs_data() {
        QTest::addColumn<int>("tab"); QTest::addColumn<int>("width"); QTest::addColumn<bool>("dark");
        QTest::newRow("files-light") << 0 << 1280 << false;
        QTest::newRow("design-light") << 1 << 1280 << false;
        QTest::newRow("files-narrow-dark") << 0 << 780 << true;
        QTest::newRow("design-narrow-dark") << 1 << 780 << true;
    }
    void navigationMotionRetainsBuildInputs() {
        QFETCH(int, tab); QFETCH(int, width); QFETCH(bool, dark);
        auto& theme = ApplicationThemeManager::instance(); const bool animations = theme.animationsEnabled();
        theme.setAnimationsEnabled(true); const auto restore = qScopeGuard([&] { theme.setAnimationsEnabled(animations); });
        Host f; QVERIFY(f.open()); ready(f); theme.setMode(dark ? ThemeMode::Dark : ThemeMode::Light);
        auto* pane = f.window->findChild<NavigationPaneCoordinator*>(); QVERIFY(pane);
        f.window->resize(width, 720); pane->setActiveTab(tab); pane->setExpanded(true,false); QTest::qWait(80);
        const auto target = f.panel()->controller()->target(); auto* session = f.panel()->controller()->session();
        QSignalSpy scanned(f.panel(), &simdock::Workbench::scanFinished);
        const int expandedWidth=pane->dock()->width(), contentWidth=f.navigation()->width();
        for(int i=0;i<3;++i) {
            pane->setExpanded(false); QTRY_VERIFY_WITH_TIMEOUT(!pane->isAnimating(),1500); QVERIFY(!pane->isExpanded());
            pane->setExpanded(true); QTRY_VERIFY_WITH_TIMEOUT(!pane->isAnimating(),1500); QVERIFY(pane->isExpanded());
            QCOMPARE(f.navigation()->width(),contentWidth); if(width>=850)QCOMPARE(pane->dock()->width(),expandedWidth);
        }
        pane->setExpanded(false);QTest::qWait(35);pane->setExpanded(true);QTest::qWait(35);
        pane->setExpanded(false);QTest::qWait(25);pane->setExpanded(true);
        QTRY_VERIFY_WITH_TIMEOUT(!pane->isAnimating(),1500); QVERIFY(pane->isExpanded()); QCOMPARE(pane->activeTab(),tab);
        QCOMPARE(scanned.size(),0); QVERIFY(f.panel()->controller()->target()==target); QCOMPARE(f.panel()->controller()->session(),session);
        QCOMPARE(f.saved().sources,QStringList({"dut.sv","extra.sv"}));
        capture(f,QString::fromLatin1(QTest::currentDataTag())+"-motion-restored");
    }
    void sourceFirstSingleOwnershipAndSessionRestore() {
        Host f; QVERIFY(f.open()); ready(f); auto* panel=f.panel(); auto* log=f.log();
        auto* controller=panel->controller(); const auto target=controller->target();
        QVERIFY(panel->isVisible()); QVERIFY(!f.context()->viewForResource(key));
        QVERIFY(!f.window->findChild<QAction*>("contextRail.simdock"));
        QVERIFY(!f.window->findChild<QWidget*>("sourceList")); QVERIFY(!f.window->findChild<QWidget*>("projectList"));
        QCOMPARE(f.window->findChildren<simdock::Workbench*>().size(),1);
        controller->session()->logText("retained background log\n"); QTRY_VERIFY(log->toPlainText().contains("retained background log"));
        panel->hide(); controller->session()->logText("while controls hidden\n"); panel->show();
        QTRY_VERIFY(log->toPlainText().contains("while controls hidden")); QCOMPARE(panel->controller(),controller);
        f.navigation()->setActiveTab(NavigationWidget::DesignTab);
        QVERIFY(f.sessions()->saveSession(false)); QVERIFY(f.sessions()->openWorkspace(f.other));
        QVERIFY(f.sessions()->openWorkspace(f.workspace)); ready(f);
        QCOMPARE(f.panel(),panel); QCOMPARE(f.log(),log); QVERIFY(controller->target()==target);
        QCOMPARE(f.navigation()->activeTab(),int(NavigationWidget::DesignTab));
        QCOMPARE(f.saved().sources,QStringList({"dut.sv","extra.sv"}));
        QVERIFY(f.sessions()->closeWorkspace(f.window->workspaceManager->activeWorkspaceIndex()));
        if(f.window->workspaceManager->isWorkspaceOpen())QVERIFY(f.sessions()->closeWorkspace(0));
        QVERIFY(panel->workspace().isEmpty()); QVERIFY(!child<QAbstractButton>(panel,"startSimulation")->isEnabled());
    }
    void navigationSearchAndTransferredModalGuard() {
        Host f; QVERIFY(f.open()); ready(f);
        f.navigation()->setSearchFilter(NavigationWidget::FileTab,"dut");
        f.navigation()->setSearchFilter(NavigationWidget::DesignTab,"dut");
        auto* search=child<QLineEdit>(f.window.get(),"navigationDesignSearchLineEdit");
        f.navigation()->setActiveTab(NavigationWidget::DesignTab); QTRY_VERIFY(search->isVisible());
        QCOMPARE(search->text(),QString()); QCOMPARE(f.navigation()->searchFilter(NavigationWidget::DesignTab),QString("dut"));
        search->setText("extra"); QCOMPARE(f.navigation()->searchFilter(NavigationWidget::DesignTab),QString("dut"));
        f.navigation()->setActiveTab(NavigationWidget::FileTab);
        QCOMPARE(child<QLineEdit>(f.window.get(),"navigationSearchLineEdit")->text(),QString("dut"));
        QCOMPARE(search->text(),QString("extra"));
        bool guarded=false;
        QTimer::singleShot(0,f.panel(),[&] {
            auto* modal=qobject_cast<QDialog*>(QApplication::activeModalWidget()); QVERIFY(modal);
            QVERIFY(!f.view()->canClose()); QVERIFY(!f.sessions()->openWorkspace(f.other));
            QCloseEvent event; QApplication::sendEvent(f.window.get(),&event); QVERIFY(!event.isAccepted());
            guarded=true;modal->reject();
        });
        click(f.window.get(),"openSettings"); QVERIFY(guarded); QVERIFY(f.view()->canClose());
    }
    void existingTestbenchAndRetiredInputs() {
        Host f; QVERIFY(f.open()); ready(f); QVERIFY(f.simulation());
        const auto manual=read(f.workspace+"/tb_manual.sv"); const auto legacy=simdock::projectJson(f.saved());
        for(const auto* name:{"simulationInputMode","editStimulus","generatedTbPath","duration","tbTop","sourceList","projectList","newProject"})
            QVERIFY(!f.window->findChild<QWidget*>(name));
        QVERIFY(f.panel()->controller()->target()==(simdock::SimulationTarget{"tb_manual.sv","tb_manual"}));
        child<QAction>(f.panel(),"openTb")->trigger();
        QCOMPARE(QFileInfo(f.window->tabManager->getCurrentDocumentMetadata().fileName).fileName(),QString("tb_manual.sv"));
        QVERIFY(f.window->tabManager->openFileInTab(f.workspace+"/dut.sv"));
        QCOMPARE(f.panel()->controller()->target().top,QString("tb_manual"));
        QVERIFY(f.panel()->setContext(f.workspace).isEmpty()); QCOMPARE(read(f.workspace+"/tb_manual.sv"),manual);
        QCOMPARE(simdock::projectJson(f.saved()),legacy);
        QVERIFY(child<QWidget>(f.panel(),"simulationTarget")->isVisible());
    }
    void logSelectionScrollAndHiddenDelivery() {
        auto* originalClipboard = new QMimeData;
        if (const auto* data = QApplication::clipboard()->mimeData())
            for (const auto& format : data->formats()) originalClipboard->setData(format, data->data(format));
        const auto restoreClipboard = qScopeGuard([originalClipboard] { QApplication::clipboard()->setMimeData(originalClipboard); });
        Host f; QVERIFY(f.open()); ready(f); QVERIFY(f.simulation()); f.runLog();
        auto* session = f.panel()->controller()->session(); QVERIFY(session);
        QString text; for (int i = 0; i < 500; ++i) text += QString("line %1\n").arg(i);
        session->logText(text); QTRY_VERIFY(f.log()->toPlainText().contains("line 499"));
        f.log()->verticalScrollBar()->setValue(0);
        auto cursor = f.log()->textCursor(); cursor.setPosition(5); cursor.setPosition(15, QTextCursor::KeepAnchor); f.log()->setTextCursor(cursor);
        const auto selection = cursor.selectedText(); const auto position = f.log()->verticalScrollBar()->value();
        click(f.window.get(), "bottomPanelButton_activity");
        session->logText("hidden output\n"); QTRY_VERIFY(f.log()->toPlainText().contains("hidden output"));
        f.runLog(); QCOMPARE(f.log()->textCursor().selectedText(), selection); QCOMPARE(f.log()->verticalScrollBar()->value(), position);
        f.log()->copy(); QCOMPARE(QApplication::clipboard()->text(), QString(selection).replace(QChar::ParagraphSeparator, QLatin1Char('\n')));
        click(f.window.get(), "clearLog"); QCOMPARE(f.log()->toPlainText(), QString());
        session->logText("pending"); click(f.window.get(), "clearLog"); QTest::qWait(40); QVERIFY(f.log()->toPlainText().isEmpty());
    }
    void hostVisualsAndMenus_data() {
        QTest::addColumn<int>("width"); QTest::addColumn<bool>("dark");
        QTest::newRow("wide-light") << 1480 << false; QTest::newRow("wide-dark") << 1480 << true;
        QTest::newRow("narrow-light") << 1050 << false; QTest::newRow("narrow-dark") << 1050 << true;
    }
    void hostVisualsAndMenus() {
        QFETCH(int,width); QFETCH(bool,dark); Host f; QVERIFY(f.open());ready(f);f.runLog();
        ApplicationThemeManager::instance().setMode(dark?ThemeMode::Dark:ThemeMode::Light);
        const auto screen=f.window->screen()->availableGeometry();
        f.window->resize(qMin(width,screen.width()-24),qMin(840,screen.height()-60));f.window->move(screen.topLeft()+QPoint(12,12));QTest::qWait(120);
        for(const auto* name:{"simulationTarget","startSimulation","openSettings"}) {
            auto* control=child<QWidget>(f.panel(),name); QVERIFY(control->isVisible());
            QVERIFY(f.panel()->rect().contains(QRect(control->mapTo(f.panel(),QPoint()),control->size())));
        }
        auto* settings=child<QToolButton>(f.panel(),"openSettings"); QVERIFY(!settings->icon().isNull());
        QVERIFY(!settings->accessibleName().isEmpty());QVERIFY(settings->text().isEmpty());QVERIFY(f.log()->isVisible());
        auto* tabs=child<QTabWidget>(f.window.get(),"navigationTabs")->tabBar(); QCOMPARE(tabs->count(),2);
        for(int i=0;i<tabs->count();++i) {
            QVERIFY(!tabs->tabIcon(i).isNull());QVERIFY(!tabs->tabToolTip(i).isEmpty());QCOMPARE(tabs->accessibleTabName(i),tabs->tabToolTip(i));
            tabs->setCurrentIndex(i);QTest::qWait(30);QVERIFY(tabs->rect().contains(tabs->tabRect(i)));
        }
        for(const auto* name:{"workbenchSection","waveOptionsToggle","waveScope","chooseWaveSignals","duration"})
            QVERIFY(!f.panel()->findChild<QWidget*>(name));
        QTimer::singleShot(0,f.panel(),[&] {
            auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());QVERIFY(dialog);
            QCOMPARE(dialog->objectName(),QString("settingsDialog"));QVERIFY(dialog->findChild<QPlainTextEdit*>("simulationIncludes"));
            QVERIFY(dialog->findChild<QPlainTextEdit*>("simulationDefinitionSources"));dialog->reject();
        });
        settings->click();
        auto* menu=child<QToolButton>(f.panel(),"simulationTarget")->menu(); QVERIFY(menu); QVERIFY(menu->actions().size()>=3);
        capture(f,QString("compact-%1").arg(QTest::currentDataTag()));
    }
    void retiredCreationEntrypoints() {
        Host f; QVERIFY(f.open()); ready(f);
        auto* tabs = f.window->tabManager.get();
        const auto file = f.workspace + "/tb_manual.sv";
        const auto original = read(file);
        const auto files = QDir(f.workspace).entryList({"*.sv", "*.v"}, QDir::Files);
        QVERIFY(tabs->openFileInTab(file));
        const auto noCreationControls = [&] {
            QVERIFY(!f.window->findChild<QAbstractButton*>("editorNewTb"));
            for (auto* group : tabs->editorSplitController()->groups(QStringLiteral("main")))
                QVERIFY(!group->cornerWidget(Qt::TopRightCorner));
            QVERIFY(f.window->findChild<QToolButton*>("leftAddDocument"));
        };
        noCreationControls(); QCOMPARE(tabs->splitCount(), 1);
        QVERIFY(tabs->splitCurrentView(EditorSplitDirection::Right));
        QCOMPARE(tabs->splitCount(), 2); noCreationControls();
        auto* group = tabs->editorSplitController()->activeGroup();
        const auto closedGroupId = tabs->editorSplitController()->groupId(group);
        QVERIFY(tabs->openFileInTab(f.workspace + "/dut.sv"));
        QCOMPARE(tabs->editorSplitController()->activeGroup(), group);
        QCOMPARE(group->count(), 2);
        tabs->closeTab(group->currentIndex());
        QCOMPARE(group->count(), 1);
        tabs->closeTab(group->currentIndex());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(tabs->splitCount(), 1); noCreationControls();
        QVERIFY(tabs->reopenClosedTab()); QCOMPARE(tabs->splitCount(), 2); noCreationControls();
        QCOMPARE(tabs->editorSplitController()->groupId(tabs->editorSplitController()->activeGroup()), closedGroupId);
        QVERIFY(tabs->reopenClosedTab()); QCOMPARE(tabs->splitCount(), 2);
        QCOMPARE(tabs->editorSplitController()->groupId(tabs->editorSplitController()->activeGroup()), closedGroupId);
        QCOMPARE(tabs->editorSplitController()->activeGroup()->count(), 2);
        QVERIFY(tabs->activateOpenFile(file));
        auto* editor = tabs->getCurrentEditor(); QVERIFY(editor);
        QMenu menu;
        editor->sourceSymbolContextMenuRequested(&menu, editor->editorSemanticContextForPosition(0, true));
        QVERIFY(!menu.findChild<QAction*>("editorNewTestbench"));
        for (auto* action : menu.actions()) QVERIFY(!action->text().contains("New testbench"));
        QVERIFY(f.simulation()); QVERIFY(!f.panel()->findChild<QAction*>("createTb"));
        const auto formatted = editor->formatDocument(); QVERIFY2(formatted.accepted(), qPrintable(formatted.diagnostic));
        QVERIFY(tabs->saveCurrentTab()); const auto saved = editor->toPlainText();
        QVERIFY(tabs->closeAllTabs()); QVERIFY(tabs->openFileInTab(file));
        QCOMPARE(tabs->getCurrentEditor()->toPlainText(), saved); QCOMPARE(read(file), saved.toUtf8());
        auto empty = f.saved(); empty.tbFile.clear(); empty.tbName.clear();
        QCOMPARE(simdock::validateInputs(f.workspace, empty), QString("Select an existing TB first."));
        QVERIFY(f.panel()->controller()->clearTarget().isEmpty());
        QVERIFY(child<QAbstractButton>(f.panel(),"startSimulation")->isEnabled());
        bool chose=false;
        QTimer::singleShot(0,f.panel(),[&] {
            auto* dialog=f.panel()->findChild<QFileDialog*>();QVERIFY(dialog);
            QTimer::singleShot(5000,dialog,&QDialog::reject);dialog->selectFile(file);chose=QMetaObject::invokeMethod(dialog,"accept");
        });
        child<QAction>(f.panel(),"chooseTb")->trigger();QVERIFY(chose);
        QCOMPARE(f.panel()->controller()->target().file,QString("tb_manual.sv"));
        QCOMPARE(f.panel()->controller()->target().top,QString("tb_manual"));
        child<QAction>(f.panel(),"openTb")->trigger();QCOMPARE(tabs->getCurrentEditor()->toPlainText(),saved);
        QVERIFY(f.sessions()->saveSession(false));QVERIFY(f.sessions()->openWorkspace(f.other));QVERIFY(f.sessions()->openWorkspace(f.workspace));
        QCOMPARE(f.panel()->controller()->target().file,QString("tb_manual.sv"));
        QCOMPARE(read(file), saved.toUtf8()); QVERIFY(!original.isEmpty());
        QCOMPARE(QDir(f.workspace).entryList({"*.sv", "*.v"}, QDir::Files), files);
        noCreationControls();
    }
    void logLocationsNavigateWithoutOpeningOutsideWorkspace() {
        Host f; QVERIFY(f.open()); ready(f); f.runLog();
        const auto source = f.workspace + "/rtl with spaces/failure.sv";
        QVERIFY(put(source, "module failure;\nlogic flag;\nendmodule\n"));
        const auto outside = f.data.filePath("outside.sv");
        QVERIFY(put(outside, "module outside; endmodule\n"));
        QSignalSpy located(f.panel(), &simdock::Workbench::openSourceRequested);
        const auto probe = [&](const QString& text) {
            f.panel()->controller()->clearLog(); f.panel()->controller()->appendLog(text + '\n');
            QTRY_VERIFY(f.log()->toPlainText().contains(text));
            f.log()->moveCursor(QTextCursor::Start);
            QTest::mouseDClick(f.log()->viewport(), Qt::LeftButton, {}, f.log()->cursorRect().center());
        };
        probe("# ** Error: " + source + "(2): expected declaration");
        QTRY_COMPARE(located.size(), 1); QCOMPARE(located.last()[1].toInt(), 2);
        QCOMPARE(f.window->tabManager->getCurrentEditor()->documentFileName(), source);
        QCOMPARE(f.window->tabManager->getCurrentEditor()->textCursor().blockNumber(), 1);
        probe("rtl with spaces/failure.sv:3: expected endmodule");
        QTRY_COMPARE(located.size(), 2); QCOMPARE(located.last()[1].toInt(), 3);
        probe("# ** Error: " + outside + "(1): external log text");
        QCOMPARE(located.size(), 2);
    }
    void runWaitsForDiscoveryAndSupportsCancellation() {
        Host f; QVERIFY(f.open()); ready(f);
        auto* controller = f.panel()->controller();
        auto* runner = f.window->simulationRunCoordinator();
        QVERIFY(f.window->tabManager->openFileInTab(f.workspace + "/tb_manual.sv"));
        QVERIFY(controller->clearTarget().isEmpty());
        f.panel()->setSimulator(f.data.filePath("missing-vsim.exe"));
        f.window->workspaceManager->refreshWorkspaceFiles();
        QVERIFY(f.window->workspaceManager->isWorkspaceScanActive());
        QSignalSpy finished(controller, &simdock::WorkspaceSimulationController::finished);
        QVERIFY(runner->run().isEmpty());
        QVERIFY(controller->preparing());
        QCOMPARE(controller->target().top, QString("tb_manual"));
        QVERIFY(!f.sessions()->openWorkspace(f.other));
        controller->stop();
        QCOMPARE(finished.size(), 1);
        QCOMPARE(finished.takeFirst()[1].toString(), QString("Preparation cancelled"));
        ready(f); QTest::qWait(80);
        QVERIFY(finished.isEmpty()); QVERIFY(!controller->session()->alive());
        f.window->workspaceManager->refreshWorkspaceFiles();
        QVERIFY(runner->run().isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 15000);
        QVERIFY(!finished.first()[0].toBool());
        QVERIFY2(finished.first()[1].toString().contains("vsim", Qt::CaseInsensitive), qPrintable(finished.first()[1].toString()));
        QVERIFY(!finished.first()[1].toString().contains("discovery"));
    }
    void participatingDocumentSavesAndRevisionChecks() {
        Host f;
        QVERIFY(put(f.workspace+"/defs.svh","localparam bit MASK=1;\n"));
        QVERIFY(put(f.workspace+"/dut.sv","module dut(input logic clk,rst,output logic q);\n`include \"defs.svh\"\nassign q=clk & ~rst & MASK;endmodule\n"));
        QVERIFY(f.open());ready(f);auto* tabs=f.window->tabManager.get();auto* runner=f.window->simulationRunCoordinator();QVERIFY(runner);
        QHash<QString,MyCodeEditor*> editors;
        for(const auto* name:{"dut.sv","defs.svh","tb_manual.sv","extra.sv"}){
            QVERIFY(tabs->openFileInTab(f.workspace+"/"+name));auto* editor=tabs->getCurrentEditor();editors.insert(name,editor);
            auto cursor=editor->textCursor();cursor.movePosition(QTextCursor::End);cursor.insertText("// unsaved "+QString(name)+"\n");
        }
        const auto unrelatedDisk=read(f.workspace+"/extra.sv");
        QString error;auto request=runner->capture(f.panel()->controller()->target(),&error);
        QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(request.overrides.size(),4);
        const auto plan=simdock::SimulationBuildPlanner::plan(request);QVERIFY2(plan->valid(),qPrintable(plan->error()));
        QVERIFY(plan->participatingFiles.contains(SemanticInputCapture::pathKey(f.workspace+"/defs.svh")));
        QVERIFY(!plan->participatingFiles.contains(SemanticInputCapture::pathKey(f.workspace+"/extra.sv")));
        const auto saved=runner->saveInputs()(*plan,request);QVERIFY2(saved.isEmpty(),qPrintable(saved));
        for(const auto* name:{"dut.sv","defs.svh","tb_manual.sv"}){
            QVERIFY(!editors[name]->document()->isModified());QCOMPARE(read(f.workspace+"/"+name),editors[name]->toPlainText().toUtf8());
        }
        QVERIFY(editors["extra.sv"]->document()->isModified());QCOMPARE(read(f.workspace+"/extra.sv"),unrelatedDisk);
        QVERIFY(request.refreshCatalog);QVERIFY(request.overrides.contains(SemanticInputCapture::pathKey(f.workspace+"/extra.sv")));
        ready(f);request=runner->capture(f.panel()->controller()->target(),&error);QVERIFY(error.isEmpty());
        QVERIFY(runner->checkCurrent()(request).isEmpty());
        QVERIFY(tabs->openFileInTab(f.workspace+"/dut.sv"));QVERIFY(tabs->splitCurrentView(EditorSplitDirection::Right));
        QVERIFY(runner->checkCurrent()(request).isEmpty()); // another view preserves document identity
        auto* duplicate=tabs->getCurrentEditor();auto cursor=duplicate->textCursor();cursor.movePosition(QTextCursor::End);cursor.insertText("// newer\n");
        QVERIFY(!runner->checkCurrent()(request).isEmpty());
        auto newer=runner->capture(f.panel()->controller()->target(),&error);QVERIFY(error.isEmpty());
        const auto newerPlan=simdock::SimulationBuildPlanner::plan(newer);QVERIFY(newerPlan->valid());
        tabs->sharedDocumentForEditor(duplicate)->setReadOnly(true);
        const auto denied=runner->saveInputs()(*newerPlan,newer);QVERIFY2(denied.contains("read-only"),qPrintable(denied));
        QVERIFY(!f.panel()->controller()->session()->alive());QVERIFY(duplicate->document()->isModified());
        tabs->sharedDocumentForEditor(duplicate)->setReadOnly(false);
    }
    void participatingSaveConflictBlocksRun() {
        Host f;QVERIFY(f.open());ready(f);auto* tabs=f.window->tabManager.get();auto* runner=f.window->simulationRunCoordinator();
        const auto file=f.workspace+"/tb_manual.sv";QVERIFY(tabs->openFileInTab(file));auto* editor=tabs->getCurrentEditor();
        auto cursor=editor->textCursor();cursor.movePosition(QTextCursor::End);cursor.insertText("// local modification\n");
        QString error;auto request=runner->capture(f.panel()->controller()->target(),&error);QVERIFY(error.isEmpty());
        const auto plan=simdock::SimulationBuildPlanner::plan(request);QVERIFY(plan->valid());
        const auto external=read(file)+"// external modification\n";QVERIFY(put(file,external));
        QSignalSpy failure(tabs,&TabManager::fileSaveFailed);
        const auto issue=runner->saveInputs()(*plan,request);QVERIFY2(!issue.isEmpty(),qPrintable(issue));
        QVERIFY(!failure.isEmpty());QCOMPARE(read(file),external);QVERIFY(editor->document()->isModified());
        QVERIFY(!f.panel()->controller()->session()->alive());
    }
    void automationSharesTbPlanningContract() {
        Host f;QVERIFY(f.open());ready(f);
        ZeroSlackAutomationService gui(f.window.get()),headless;
        QJsonObject request{{"command","simulation.validate"},{"workspace",f.workspace},{"tb","tb_manual.sv"},{"top","tb_manual"}};
        const auto a=gui.execute(request),b=headless.execute(request);
        QVERIFY2(a.value("ok").toBool(),QJsonDocument(a).toJson().constData());QVERIFY2(b.value("ok").toBool(),QJsonDocument(b).toJson().constData());
        QCOMPARE(a.value("data"),b.value("data"));QCOMPARE(a.value("data").toObject().value("compilationOrder").toArray().size(),2);
        const auto before=simdock::projectJson(f.saved());
        auto legacy=request;legacy.remove("tb");legacy.remove("top");legacy["projectId"]=f.project.id;
        const auto mapped=headless.execute(legacy);QVERIFY(mapped.value("ok").toBool());QCOMPARE(mapped.value("data"),b.value("data"));
        auto mixed=request;mixed["projectId"]=f.project.id;
        QCOMPARE(headless.execute(mixed).value("error").toObject().value("code").toString(),QString("invalid_target"));
        auto missing=request;missing.remove("top");
        QCOMPARE(headless.execute(missing).value("error").toObject().value("code").toString(),QString("invalid_target"));
        for(const auto* key:{"durationNs","timeoutMs","sources"}){
            auto retired=request;retired[key]=key==QString("sources")?QJsonValue(QJsonArray{"dut.sv"}):QJsonValue(1);
            QCOMPARE(gui.execute(retired).value("error").toObject().value("code").toString(),QString("retired_parameter"));
        }
        auto retired=request;retired["command"]="simulation.put";
        QCOMPARE(headless.execute(retired).value("error").toObject().value("code").toString(),QString("retired_operation"));
        QCOMPARE(simdock::projectJson(f.saved()),before);
        QCOMPARE(ZeroSlackAutomationService::capabilities().value("data").toObject().value("simulationContract").toObject().value("version").toInt(),2);
        auto* tabs=f.window->tabManager.get();QVERIFY(tabs->openFileInTab(f.workspace+"/tb_manual.sv"));
        auto* editor=tabs->getCurrentEditor();editor->setPlainText("module tb_manual; missing_impl d(); endmodule\n");editor->document()->setModified(true);
        const auto dirty=gui.execute(request);QVERIFY(!dirty.value("ok").toBool());QVERIFY(dirty.value("error").toObject().value("message").toString().contains("missing_impl"));
        QVERIFY(headless.execute(request).value("ok").toBool()); // validation never saves or silently substitutes disk for GUI overlays
        QCOMPARE(simdock::projectJson(f.saved()),before);
    }
    void liveQuestaStartStopFailureRecovery() {
        const auto executable=qEnvironmentVariable("SIMDOCK_TEST_QUESTA"); QVERIFY2(QFileInfo::exists(executable),"Real Questa is required.");
        Host f; QVERIFY(f.open());ready(f);f.panel()->setSimulator(executable);f.runLog();
        auto* controller=f.panel()->controller();auto* session=controller->session();session->setInteractive(false);
        const auto retainRunLog=qScopeGuard([&] {
            const auto output=qEnvironmentVariable("ZEROSLACK_SIMULATION_UI_REPORT");
            if(!output.isEmpty()) put(QDir(output).filePath("questa-mainwindow.log"),f.log()->toPlainText().toUtf8());
        });
        QSignalSpy finished(controller,&simdock::WorkspaceSimulationController::finished);
        QVERIFY(f.window->tabManager->openFileInTab(f.workspace+"/tb_manual.sv"));
        QVERIFY(controller->clearTarget().isEmpty());
        f.window->workspaceManager->refreshWorkspaceFiles();
        click(f.panel(),"startSimulation");QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(),30000);
        QVERIFY2(finished.takeFirst()[0].toBool(),qPrintable(f.log()->toPlainText()));QCOMPARE(session->outcome(),QString("finished"));
        const auto pid=session->processId(); const auto first=session->lastRunDirectory();
        click(f.panel(),"startSimulation");QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(),30000);
        QVERIFY(finished.takeFirst()[0].toBool());QCOMPARE(session->processId(),pid);QVERIFY(!QFileInfo::exists(first));
        QVERIFY(put(f.workspace+"/dut.sv","module dut; syntax broken @@@; endmodule\n"));
        click(f.panel(),"startSimulation");QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(),30000);
        QVERIFY(!finished.takeFirst()[0].toBool()); QVERIFY(f.log()->toPlainText().contains("dut"));
        QVERIFY(put(f.workspace+"/dut.sv","module dut(input logic clk,rst,output logic q);assign q=clk & ~rst;endmodule\n"));
        auto* editor=f.window->tabManager->getCurrentEditor();QVERIFY(editor);
        editor->setPlainText("module tb_manual;logic clk=0,rst=0,q;dut d(.*);always #5 clk=~clk;endmodule\n");
        editor->document()->setModified(true);
        click(f.panel(),"startSimulation");QTRY_VERIFY2_WITH_TIMEOUT(session->busy()&&controller->state()=="Running",qPrintable(controller->state()+"\n"+f.log()->toPlainText()),30000);
        QVERIFY(!editor->document()->isModified());QCOMPARE(read(f.workspace+"/tb_manual.sv"),editor->toPlainText().toUtf8());
        f.panel()->hide();QTest::qWait(100);QVERIFY(session->busy());f.panel()->show();
        QVERIFY(!f.sessions()->openWorkspace(f.other)); const auto owned=QFileInfo(session->lastRunDirectory()).absolutePath();
        click(f.panel(),"stopSimulation");QTRY_VERIFY_WITH_TIMEOUT(!session->alive(),10000);
        QCOMPARE(session->outcome(),QString("cancelled"));QVERIFY(!QFileInfo::exists(owned));QVERIFY(f.sessions()->openWorkspace(f.other));
    }
};

int main(int argc, char** argv) {
    QStandardPaths::setTestModeEnabled(true); QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
    QCoreApplication::setOrganizationName("ZeroSlackSimulationUITest"); QCoreApplication::setApplicationName("ZeroSlackSimulationUITest");
    QTemporaryDir profile;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, profile.path());
    qputenv("ZEROSLACK_SIMDOCK_LEGACY_SETTINGS_PATH", profile.filePath("legacy.ini").toUtf8());
    UserTemplateService::getInstance()->setGlobalTemplateFilePath(profile.filePath("templates.json"));
    if (!initializeUiStyleForTest()) return 2;
    ApplicationThemeManager::instance().setAnimationsEnabled(false);
    ApplicationThemeManager::instance().applyToApplication();
    SimulationWorkspaceUiTest test; return QTest::qExec(&test, argc, argv);
}
#include "simulation_workspace_ui_test.moc"
