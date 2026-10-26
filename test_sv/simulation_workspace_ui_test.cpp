#include "mainwindow.h"
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
        put(workspace + "/tb_manual.sv", "module tb_manual; logic clk=0,rst=0,q; dut d(.*); always #5 clk=~clk; endmodule\n");
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
    QAbstractItemView* sources() const { return child<QAbstractItemView>(window.get(), "sourceList"); }
    QPlainTextEdit* log() const { return child<QPlainTextEdit>(window.get(), "simulationLog"); }
    simdock::Project saved() const {
        for (const auto& p : simdock::loadProjects(workspace)) if (p.id == project.id) return p;
        return {};
    }
    bool open() { return sessions()->openWorkspace(workspace) && panel()->openProject(project.id).isEmpty(); }
    bool simulation() {
        return context()->openResource(SimDockContextProvider().activationResource(workspace),
            {ContextSurface::Docked, ContextPersistence::Kept});
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
    void ready(Host& f) { QTRY_VERIFY_WITH_TIMEOUT(child<QComboBox>(f.panel(), "dutSelector")->count() > 0, 15000); }
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
        NavigationWidget navigation;navigation.resize(500,400);navigation.show();
        auto* tabs=child<QTabWidget>(&navigation,"navigationTabs");auto* bar=tabs->tabBar();
        QCOMPARE(tabs->count(),2);QCOMPARE(tabs->tabText(0),QString("Files"));QVERIFY(tabs->tabText(1).isEmpty());
        navigation.setActiveTab(NavigationWidget::DesignTab);navigation.setSourceFilesWidget(new QWidget);
        QCOMPARE(tabs->count(),3);QCOMPARE(tabs->currentIndex(),1);QCOMPARE(tabs->tabText(1),QString("Design"));
        class Translation final : public QTranslator {
        public:
            bool isEmpty() const override { return false; }
            QString translate(const char* context,const char* text,const char*,int) const override {
                if(QByteArray(context)!="NavigationWidget")return {};
                if(QByteArray(text)=="Files")return QStringLiteral("Workspace source documents");
                if(QByteArray(text)=="Build inputs")return QString::fromUtf8("e\xCC\x81 中文 compilation dependencies");
                return {};
            }
        } translation;
        QVERIFY(QCoreApplication::installTranslator(&translation));QCoreApplication::processEvents();
        navigation.setActiveTab(NavigationWidget::SourceFilesTab);QTest::qWait(30);
        QVERIFY(bar->tabText(2).startsWith(QString::fromUtf8("e\xCC\x81")));
        QCOMPARE(bar->accessibleTabName(0),QString("Workspace source documents"));
        QFont large=bar->font();large.setPointSizeF(22);bar->setFont(large);navigation.resize(200,400);QTest::qWait(40);
        for(int i=0;i<3;++i){QVERIFY(bar->rect().contains(bar->tabRect(i)));QVERIFY(bar->tabRect(i).width()>=44);}
        recordNavigation(bar,"translated-large-font");
        navigation.hide();navigation.resize(600,400);navigation.show();QTest::qWait(40);recordNavigation(bar,"translated-restored");
        QCoreApplication::removeTranslator(&translation);QCoreApplication::processEvents();
        QCOMPARE(bar->tabText(2),QString("Build inputs"));
        QCOMPARE(bar->accessibleTabName(0),QString("Files"));
    }
    void navigationInputTargets() {
        NavigationWidget navigation;navigation.resize(650,350);navigation.show();
        navigation.setSourceFilesWidget(new QWidget);
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
        QTest::keyClick(bar,Qt::Key_Right);QCOMPARE(tabs->currentIndex(),2);
        QTest::keyClick(bar,Qt::Key_Left);QCOMPARE(tabs->currentIndex(),1);
        QTest::mousePress(bar,Qt::LeftButton,Qt::NoModifier,bar->tabRect(0).center());
        QTest::mouseMove(bar,QPoint(-30,-30));QTest::mouseRelease(bar,Qt::LeftButton,Qt::NoModifier,QPoint(-30,-30));
        QCOMPARE(tabs->currentIndex(),0);
        QTest::mouseClick(bar,Qt::LeftButton,Qt::NoModifier,bar->tabRect(2).center());QCOMPARE(tabs->currentIndex(),2);
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
        for(int target=0;target<3;++target) {
            QTest::keyClick(bar,Qt::Key_Right);
            QTest::mouseClick(bar,Qt::LeftButton,Qt::NoModifier,bar->tabRect(target).center());QCOMPARE(tabs->currentIndex(),target);
            QTest::qWait(40);
            for(int i=0;i<3;++i){QCOMPARE(bar->tabText(i),i==target?bar->accessibleTabName(i):QString());QVERIFY(bar->rect().contains(bar->tabRect(i)));}
            recordNavigation(bar,QString("%1-tab%2").arg(QTest::currentDataTag()).arg(target));
            capture(f,QString("navigation-%1-tab%2").arg(QTest::currentDataTag()).arg(target));
        }
        QVERIFY(f.sessions()->saveSession(false));QVERIFY(f.sessions()->openWorkspace(f.other));QVERIFY(f.sessions()->openWorkspace(f.workspace));
        QCOMPARE(tabs->currentIndex(),2);QCOMPARE(bar->tabText(2),QString("Build inputs"));
    }
    void navigationMotionRetainsBuildInputs_data() {
        QTest::addColumn<int>("tab"); QTest::addColumn<int>("width"); QTest::addColumn<bool>("dark");
        QTest::newRow("files-light") << 0 << 1280 << false;
        QTest::newRow("design-light") << 1 << 1280 << false;
        QTest::newRow("inputs-light") << 2 << 1280 << false;
        QTest::newRow("inputs-narrow-dark") << 2 << 780 << true;
    }
    void navigationMotionRetainsBuildInputs() {
        QFETCH(int, tab); QFETCH(int, width); QFETCH(bool, dark);
        auto& theme = ApplicationThemeManager::instance();
        const bool animations = theme.animationsEnabled();
        theme.setAnimationsEnabled(true);
        const auto restore = qScopeGuard([&] { theme.setAnimationsEnabled(animations); });
        Host f; QVERIFY(f.open()); ready(f);
        theme.setMode(dark ? ThemeMode::Dark : ThemeMode::Light);
        auto* pane = f.window->findChild<NavigationPaneCoordinator*>(); QVERIFY(pane);
        f.window->resize(width, 720);
        pane->setActiveTab(tab); pane->setExpanded(true, false); QTest::qWait(80);
        const auto sourceModel = f.sources()->model();
        QSignalSpy scanned(f.panel(), &simdock::Workbench::scanFinished);
        QSignalSpy resets(sourceModel, &QAbstractItemModel::modelReset);
        const auto projectId = f.saved().id;
        const auto compilationOrder = f.saved().sources;
        const int expandedWidth = pane->dock()->width();
        const int contentWidth = f.navigation()->width();
        for (int i = 0; i < 3; ++i) {
            pane->setExpanded(false);
            QTRY_VERIFY_WITH_TIMEOUT(!pane->isAnimating(), 1500);
            QVERIFY(!pane->isExpanded());
            if (i == 0) capture(f, QString::fromLatin1(QTest::currentDataTag()) + "-collapsed");
            pane->setExpanded(true);
            QTRY_VERIFY_WITH_TIMEOUT(!pane->isAnimating(), 1500);
            QVERIFY(pane->isExpanded());
            QCOMPARE(f.navigation()->width(), contentWidth);
            if (width >= 850) QCOMPARE(pane->dock()->width(), expandedWidth);
        }
        pane->setExpanded(false); QTest::qWait(35);
        pane->setExpanded(true); QTest::qWait(35);
        pane->setExpanded(false); QTest::qWait(25);
        pane->setExpanded(true);
        QTRY_VERIFY_WITH_TIMEOUT(!pane->isAnimating(), 1500);
        QVERIFY(pane->isExpanded()); QCOMPARE(pane->activeTab(), tab);
        QCOMPARE(f.sources()->model(), sourceModel);
        QCOMPARE(scanned.size(), 0);
        QCOMPARE(resets.size(), 0);
        QCOMPARE(f.saved().id, projectId); QCOMPARE(f.saved().sources, compilationOrder);
        if (tab == 2) {
            QVERIFY(f.sources()->isVisible());
            f.sources()->setCurrentIndex(sourceModel->index(1, 0));
            QVERIFY(!f.window->findChild<QWidget*>("moveSourceUp"));
            QCOMPARE(f.saved().sources, QStringList({"dut.sv", "extra.sv"}));
        }
        capture(f, QString::fromLatin1(QTest::currentDataTag()) + "-motion-restored");
    }
    void sourceFirstSingleOwnershipAndSessionRestore() {
        Host f; QVERIFY(f.open()); ready(f);
        QVERIFY(!f.context()->viewForResource(key));
        f.navigation()->setActiveTab(NavigationWidget::SourceFilesTab);
        auto* panel = f.panel(); auto* model = f.sources()->model(); auto* log = f.log();
        QVERIFY(f.sources()->isVisible());
        QVERIFY(!f.window->findChild<QWidget*>("openWorkspace"));
        QVERIFY(!panel->findChild<QWidget*>("workbenchSection"));
        f.sources()->setCurrentIndex(model->index(1, 0));
        QVERIFY(!f.window->findChild<QWidget*>("moveSourceUp"));
        QCOMPARE(f.saved().sources, QStringList({"dut.sv", "extra.sv"}));
        const auto firstId = f.project.id;
        QVERIFY(panel->createProject("Another project"));
        const auto secondId = panel->saveState().value("projectId").toString();
        QVERIFY(secondId != firstId);
        QVERIFY(panel->openProject(firstId).isEmpty());
        QCOMPARE(f.sources()->model(), model);
        QVERIFY(f.simulation()); QCOMPARE(f.panel(), panel);
        QVERIFY(!panel->isAncestorOf(f.sources())); QVERIFY(!panel->isAncestorOf(log));
        QCOMPARE(f.window->findChildren<simdock::Workbench*>().size(), 1);
        QVERIFY(f.context()->unpinResource(key)); QCOMPARE(f.panel(), panel);
        f.context()->setFloatingCollapsed(true); f.context()->setFloatingCollapsed(false);
        QVERIFY(f.context()->pinFloatingResource(key));
        QVERIFY(f.context()->setDockVisible(false)); QVERIFY(f.context()->setDockVisible(true));
        QVERIFY(f.context()->closePinnedResource(key));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(f.panel(), panel); QCOMPARE(f.log(), log); QCOMPARE(f.sources()->model(), model);
        QVERIFY(panel->openProject(secondId).isEmpty());
        QVERIFY(f.sessions()->saveSession(false));
        QVERIFY(f.sessions()->openWorkspace(f.other)); QCOMPARE(f.panel()->workspace(), f.other);
        QVERIFY(f.sessions()->openWorkspace(f.workspace));
        QCOMPARE(f.panel()->saveState().value("projectId").toString(), secondId);
        QCOMPARE(f.navigation()->activeTab(), int(NavigationWidget::SourceFilesTab));
        QVERIFY(f.simulation()); QCOMPARE(f.panel(), panel);
        QVERIFY(f.sessions()->closeWorkspace(f.window->workspaceManager->activeWorkspaceIndex()));
        // Closing the last remaining workspace is covered after closing the other workspace as well.
        if (f.window->workspaceManager->isWorkspaceOpen()) QVERIFY(f.sessions()->closeWorkspace(0));
        QVERIFY(f.panel()->workspace().isEmpty());
        QVERIFY(!child<QAbstractButton>(panel, "startSimulation")->isEnabled());
    }
    void navigationSearchAndTransferredModalGuard() {
        Host f; QVERIFY(f.open()); ready(f);
        f.navigation()->setSearchFilter(NavigationWidget::FileTab, "dut");
        f.navigation()->setSearchFilter(NavigationWidget::DesignTab, "dut");
        f.navigation()->setActiveTab(NavigationWidget::SourceFilesTab);
        auto* search = child<QLineEdit>(f.window.get(), "navigationSearchLineEdit");
        QVERIFY(!search->isVisible());
        f.navigation()->setSearchFilter(NavigationWidget::SourceFilesTab, "ignored");
        QCOMPARE(f.navigation()->searchFilter(NavigationWidget::FileTab), QString("dut"));
        f.navigation()->setActiveTab(NavigationWidget::DesignTab);
        QTRY_VERIFY_WITH_TIMEOUT(search->isVisible(),10000);
        QCOMPARE(search->text(), QString());
        QCOMPARE(f.navigation()->searchFilter(NavigationWidget::DesignTab), QString("dut"));
        search->setText("extra");
        QCOMPARE(f.navigation()->searchFilter(NavigationWidget::DesignTab), QString("dut"));
        f.navigation()->setActiveTab(NavigationWidget::FileTab); QCOMPARE(search->text(), QString("dut"));
        f.navigation()->setActiveTab(NavigationWidget::SourceFilesTab);
        bool guarded = false;
        QTimer::singleShot(0, f.panel(), [&] {
            auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget()); QVERIFY(modal);
            QVERIFY(!f.view()->canClose()); QVERIFY(!f.sessions()->openWorkspace(f.other));
            QCloseEvent event; QApplication::sendEvent(f.window.get(), &event); QVERIFY(!event.isAccepted());
            guarded = true; modal->reject();
        });
        click(f.window.get(), "newProject"); QVERIFY(guarded);
        QVERIFY(!f.context()->viewForResource(key)); QVERIFY(f.view()->canClose());
    }
    void existingTestbenchAndRetiredInputs() {
        Host f; QVERIFY(f.open()); ready(f); QVERIFY(f.simulation());
        QVERIFY(!f.panel()->findChild<QWidget*>("simulationInputMode"));
        QVERIFY(!f.panel()->findChild<QWidget*>("editStimulus"));
        QVERIFY(!f.panel()->findChild<QWidget*>("generatedTbPath"));
        QCOMPARE(child<QLineEdit>(f.panel(), "tbFile")->text(), QString("tb_manual.sv"));
        click(f.panel(), "openTb");
        QCOMPARE(QFileInfo(f.window->tabManager->getCurrentDocumentMetadata().fileName).fileName(), QString("tb_manual.sv"));
        const auto manual = read(f.workspace + "/tb_manual.sv");
        child<QSpinBox>(f.panel(), "duration")->setValue(1700);
        QCOMPARE(f.saved().durationNs, 1700LL);
        auto* dut = child<QComboBox>(f.panel(), "dutSelector");
        const auto other = dut->findData(QStringList({"extra.sv", "extra"}));
        QVERIFY(other >= 0); dut->setCurrentIndex(other);
        QCOMPARE(f.saved().tbFile, QString("tb_manual.sv"));
        QVERIFY(f.panel()->setContext(f.workspace).isEmpty()); ready(f);
        QCOMPARE(f.saved().durationNs, 1700LL);
        QCOMPARE(read(f.workspace + "/tb_manual.sv"), manual);
        QVERIFY(child<QWidget>(f.panel(), "duration")->isVisible());
        QVERIFY(child<QWidget>(f.panel(), "tbTop")->isVisible());
    }
    void logSelectionScrollAndHiddenDelivery() {
        auto* originalClipboard = new QMimeData;
        if (const auto* data = QApplication::clipboard()->mimeData())
            for (const auto& format : data->formats()) originalClipboard->setData(format, data->data(format));
        const auto restoreClipboard = qScopeGuard([originalClipboard] { QApplication::clipboard()->setMimeData(originalClipboard); });
        Host f; QVERIFY(f.open()); ready(f); QVERIFY(f.simulation()); f.runLog();
        auto* session = f.panel()->findChild<simdock::QuestaSession*>(); QVERIFY(session);
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
        QFETCH(int, width); QFETCH(bool, dark);
        Host f; QVERIFY(f.open()); ready(f); QVERIFY(f.simulation());
        f.navigation()->setActiveTab(NavigationWidget::SourceFilesTab); f.runLog();
        ApplicationThemeManager::instance().setMode(dark ? ThemeMode::Dark : ThemeMode::Light);
        const auto screen = f.window->screen()->availableGeometry();
        f.window->resize(qMin(width, screen.width() - 24), qMin(840, screen.height() - 60));
        f.window->move(screen.topLeft() + QPoint(12, 12));
        f.window->resizeDocks({f.context()->dockWidget()}, {420}, Qt::Horizontal); QTest::qWait(120);
        auto* run = child<QWidget>(f.panel(), "startSimulation");
        auto* settings = child<QToolButton>(f.panel(), "openSettings");
        QVERIFY(!settings->icon().isNull()); QVERIFY(!settings->accessibleName().isEmpty()); QVERIFY(settings->text().isEmpty());
        QVERIFY(f.panel()->rect().contains(QRect(run->mapTo(f.panel(), QPoint()), run->size())));
        QVERIFY(f.sources()->height() >= 80); QVERIFY(child<QWidget>(f.window.get(), "projectList")->height() <= 140);
        QVERIFY(f.sources()->isVisible()); QVERIFY(f.log()->isVisible());
        auto* tabs = child<QTabWidget>(f.window.get(), "navigationTabs")->tabBar();
        for (int i = 0; i < 3; ++i) QVERIFY(tabs->rect().contains(tabs->tabRect(i)));
        for (int i = 0; i < 3; ++i) {
            QCOMPARE(tabs->tabText(i),i==tabs->currentIndex()?tabs->accessibleTabName(i):QString());
            QVERIFY(!tabs->tabIcon(i).isNull());
            QVERIFY(!tabs->tabToolTip(i).isEmpty());
            QCOMPARE(tabs->accessibleTabName(i), tabs->tabToolTip(i));
            tabs->setCurrentIndex(i);
            QTest::qWait(30);
            QVERIFY(tabs->rect().contains(tabs->tabRect(i)));
        }
        tabs->setCurrentIndex(2);
        QVERIFY(!f.panel()->findChild<QWidget*>("workbenchSection"));
        QVERIFY(!f.panel()->findChild<QWidget*>("waveOptionsToggle"));
        QVERIFY(!f.panel()->findChild<QWidget*>("waveScope"));
        QVERIFY(!f.panel()->findChild<QWidget*>("chooseWaveSignals"));
        QTimer::singleShot(0, f.panel(), [&] { auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()); QVERIFY(dialog); QCOMPARE(dialog->objectName(), QString("settingsDialog")); dialog->reject(); });
        settings->click();
        auto* menu = child<QToolButton>(f.panel(), "simulationActions")->menu(); QVERIFY(menu);
        auto* action = child<QAction>(f.panel(), "createTb"); QVERIFY(menu->actions().contains(action));
        action->trigger();
        auto* popup = f.window->findChild<QWidget*>(QStringLiteral("newTbEditorPopup"));
        QVERIFY(popup && popup->isVisible());
        child<QAbstractButton>(popup, "tbCreateCancel")->click();
        capture(f, QString::fromLatin1(QTest::currentDataTag()) + "-existing");

    }
    void liveQuestaStartStopFailureRecovery() {
        const auto executable = qEnvironmentVariable("SIMDOCK_TEST_QUESTA");
        QVERIFY2(QFileInfo::exists(executable), "Real Questa is required for this test.");
        const auto output = qEnvironmentVariable("ZEROSLACK_SIMULATION_UI_REPORT");
        QString allLog;
        Host f; QVERIFY(f.open()); ready(f); QVERIFY(f.simulation()); f.panel()->setSimulator(executable); f.runLog();
        auto* session = f.panel()->findChild<simdock::QuestaSession*>(); QVERIFY(session);
        QSignalSpy finished(session, &simdock::QuestaSession::finished);
        connect(session, &simdock::QuestaSession::logText, this, [&](const QString& text) {
            allLog += text;
            if (!output.isEmpty()) put(QDir(output).filePath("questa-all-runs.log"), allLog.toUtf8());
        });
        const auto preserveRun = [&](const QString& phase) {
            if (output.isEmpty()) return true;
            const QDir destination(QDir(output).filePath(phase));
            if (!put(destination.filePath("host.log"), f.log()->toPlainText().toUtf8())) return false;
            for (const auto& name : {"run.do", "modelsim.ini", "transcript.log"}) {
                const auto path = QDir(session->lastRunDirectory()).filePath(QLatin1String(name));
                if (!QFileInfo::exists(path) || !put(destination.filePath(QLatin1String(name)), read(path))) return false;
            }
            return put(destination.filePath("identity.json"), QJsonDocument(QJsonObject{
                {"runDirectory", session->lastRunDirectory()}, {"processId", session->processId()},
                {"alive", session->alive()}, {"busy", session->busy()}, {"canClose", f.view()->canClose()}}).toJson());
        };
        click(f.panel(), "startSimulation");
        QTRY_VERIFY_WITH_TIMEOUT(session->alive(), 20000);
        QTRY_VERIFY2_WITH_TIMEOUT(!finished.isEmpty(), qPrintable(allLog), 130000);
        QVERIFY2(finished.takeFirst()[0].toBool(), qPrintable(allLog));
        QVERIFY(f.view()->canClose());
        QVERIFY(preserveRun("first-success"));
        click(f.panel(), "stopSimulation"); QTRY_VERIFY_WITH_TIMEOUT(!session->alive(), 10000);
        QVERIFY(f.view()->canClose());
        child<QLineEdit>(f.panel(), "tbTop")->setText("missing_tb_top");
        QMetaObject::invokeMethod(child<QLineEdit>(f.panel(), "tbTop"), "editingFinished");
        click(f.window.get(), "clearLog"); click(f.panel(), "startSimulation");
        QTRY_VERIFY2_WITH_TIMEOUT(!finished.isEmpty(), qPrintable(allLog), 130000);
        QVERIFY2(!finished.takeFirst()[0].toBool(), qPrintable(allLog));
        QVERIFY2(allLog.contains("missing_tb_top") && allLog.contains("Questa could not load the testbench"), qPrintable(allLog));
        QVERIFY(session->alive()); QVERIFY(f.view()->canClose());
        QVERIFY(preserveRun("invalid-top"));
        click(f.panel(), "stopSimulation"); QTRY_VERIFY_WITH_TIMEOUT(!session->alive(), 10000);
        QVERIFY(f.view()->canClose());
        child<QLineEdit>(f.panel(), "tbTop")->setText("tb_manual"); QMetaObject::invokeMethod(child<QLineEdit>(f.panel(), "tbTop"), "editingFinished");
        click(f.window.get(), "clearLog"); click(f.panel(), "startSimulation");
        QTRY_VERIFY2_WITH_TIMEOUT(!finished.isEmpty(), qPrintable(allLog), 130000);
        QVERIFY2(finished.takeFirst()[0].toBool(), qPrintable(allLog));
        QVERIFY2(!allLog.contains("Could not publish Questa state"), qPrintable(allLog));
        QVERIFY(f.view()->canClose());
        QVERIFY(preserveRun("corrected-top"));
        if (!output.isEmpty()) QVERIFY(put(QDir(output).filePath("questa-host.log"), f.log()->toPlainText().toUtf8()));
        const auto lastRun = session->lastRunDirectory();
        QVERIFY(f.sessions()->openWorkspace(f.other));
        QTRY_VERIFY_WITH_TIMEOUT(!session->alive(), 10000);
        QVERIFY(!QFileInfo::exists(lastRun));
        QVERIFY(f.view()->canClose());
        QVERIFY(f.window->close());
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
