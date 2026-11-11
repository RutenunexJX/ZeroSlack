#include "../src/simulation/simdock/core/simulationbuildplanner.h"
#include "mainwindow.h"
#include "applicationthememanager.h"
#include "welcomecard.h"
#include "navigationmanager.h"
#include "navigationwidget.h"
#include "navigationrowstyle.h"
#include "navigationpanecoordinator.h"
#include "workspacemanager.h"
#include "workspacesessioncoordinator.h"
#include "projectmodel.h"
#include "tabmanager.h"
#include "editorsplitcontroller.h"
#include "crashrecoveryservice.h"
#include "testuistyle.h"
#include "contextworkspacecontroller.h"
#include "../src/integrations/pinloom/pinloomcontextprovider.h"
#include "../src/integrations/pinloom/pinloomcontextview.h"
#include "../src/simulation/simdock/ui/workbench.h"
#include <zeroslack/semantic/workspaceconfigurationservice.h>
#include <QtTest>
#include <QApplication>
#include <QEnterEvent>
#include <QFileDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QStandardPaths>
#include <QSettings>
#include <QTemporaryDir>
#include <QTreeWidgetItemIterator>
#include <QListView>
#include <QAbstractButton>
#include <QAction>
#include <QJsonArray>
#include <QPlainTextEdit>
#include <QClipboard>
#include <QMimeData>
#include <QToolButton>
#include <QElapsedTimer>
#include <QWindowStateChangeEvent>
#include <QCryptographicHash>
#include <QTabBar>
#include <QScreen>
#include <QPdfWriter>
#include <QPainter>
#include <QMouseEvent>

namespace {
void put(const QString& path,const QByteArray& bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath()); QFile file(path);
    if (!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()) qFatal("fixture write");
}
void hoverTab(QTabBar* bar, int index) {
    class MoveCounter final : public QObject {
    public:
        int count = 0;
        bool eventFilter(QObject*, QEvent* event) override {
            if (event->type()==QEvent::MouseMove) ++count;
            return false;
        }
    } counter;
    bar->installEventFilter(&counter);
    QPoint position = bar->tabRect(index).center();
    QTest::mouseMove(bar,position);
    QApplication::processEvents();
    if (position != bar->tabRect(index).center()) {
        position = bar->tabRect(index).center();
        QTest::mouseMove(bar,position);
        QApplication::processEvents();
    }
    qInfo() << "LEFT_TAB_MOUSE_MOVE" << bar->tabText(index) << "nativeMoveEvents" << counter.count
            << "rect" << bar->tabRect(index) << "pointer" << bar->mapFromGlobal(QCursor::pos());
    if (!counter.count) {
        // Native child hosts can consume QTest's cursor move. Deliver the widget
        // event explicitly; physical pointer coverage is recorded separately.
        QMouseEvent move(QEvent::MouseMove,QPointF(position),QPointF(bar->mapToGlobal(position)),
                         Qt::NoButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(bar,&move);
    }
    QVERIFY(counter.count>0);
}
struct Host {
    QTemporaryDir root;
    std::unique_ptr<MainWindow> window;
    QString workspace, other;
    Host() {
        workspace=root.filePath("workspace"); other=root.filePath("other"); QDir().mkpath(other);
        put(workspace+"/rtl/design.sv","module sidebar_parent; sidebar_leaf u_leaf(); endmodule\nmodule sidebar_leaf; endmodule\n");
        put(workspace+"/a_use.sv","module uses_pkg; import build_pkg::*; word_t data; endmodule\n");
        put(workspace+"/z_pkg.sv","package build_pkg; typedef logic [3:0] word_t; endpackage\n");
        qputenv("ZEROSLACK_SESSION_STORAGE_PATH",root.filePath("sessions.ini").toUtf8());
        qputenv("ZEROSLACK_EDITING_TIME_STORAGE_PATH",root.filePath("editing.json").toUtf8());
        window=std::make_unique<MainWindow>();
        window->tabManager->setCrashRecoveryService(std::make_unique<CrashRecoveryService>(root.filePath("recovery")));
        window->workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
        window->resize(1400,900); window->show(); window->activateWindow();
    }
    WorkspaceSessionCoordinator* sessions() const { return window->findChild<WorkspaceSessionCoordinator*>(); }
    simdock::Workbench* workbench() const { return window->findChild<simdock::Workbench*>(); }
    NavigationWidget* navigation() const { return window->findChild<NavigationWidget*>(); }
    NavigationManager* manager() const { return window->findChild<NavigationManager*>(); }
};
}

class SidebarMainWindowTest : public QObject {
    Q_OBJECT
    void capture(Host& host,const QString& name) {
        const auto output=qEnvironmentVariable("ZEROSLACK_SIDEBAR_EVIDENCE"); if(output.isEmpty())return;
        QDir().mkpath(output); QTest::qWait(60);
        QVERIFY(host.window->grab().save(QDir(output).filePath(name+".png")));
        if (name=="left-tabs-files" || name=="left-tabs-design" || name.endsWith("-hover")) {
            auto* bar=host.window->tabManager->editorSplitController()->regionGroup("left")->tabBar();
            QVERIFY(bar->grab().save(QDir(output).filePath(name+"-strip.png")));
        }
    }
    void captureIconSurfaces(Host& h) {
        if (!qEnvironmentVariableIsSet("ZEROSLACK_ICONS_EVIDENCE")) return;
        auto* controller=h.window->findChild<ContextWorkspaceController*>(); QVERIFY(controller);
        auto* target=h.window->findChild<QPushButton*>("startSimulation");QVERIFY(target);
        const auto selected=h.workbench()->controller()->target();
        // Real host view and actions; only the external Pinloom data boundary is a fixture.
        QVERIFY(controller->unregisterProvider("pinloom"));
        const QJsonObject entry{{"identity",QJsonObject{{"entryId","anchor:icon-review"},{"resourceId","review-note"},{"anchorId","icon-review"}}},
            {"uri","pinloom://entry/anchor:icon-review?resource=review-note&anchor=icon-review"},
            {"type","anchor"},{"title","Compilation note"},{"summary","Controlled UI review fixture"}};
        auto* client=new PinloomHostClient([entry](const QJsonObject& request,PinloomHostClient::RawReplyHandler reply) {
            if(request.value("method")=="search") reply({{"entries",QJsonArray{}}},{});
            else if(request.value("method")=="resolve") reply({{"entry",entry},{"content","Package declarations precede their imports."},{"contentType","text/plain"}},{});
            else reply({},"Unexpected fixture operation");
        },h.window.get());
        QVERIFY(controller->registerProvider(std::make_unique<PinloomContextProvider>(client)));
        const auto home=PinloomContextProvider::homeResource(h.workspace);
        const auto linked=PinloomContextProvider::resourceForEntry(PinloomHostEntry::fromJson(entry),h.workspace);
        struct ClipboardRestore {
            QMimeData* saved=new QMimeData;
            ClipboardRestore() { if(const auto* data=QApplication::clipboard()->mimeData())
                for(const auto& format:data->formats()) saved->setData(format,data->data(format)); }
            ~ClipboardRestore() { QApplication::clipboard()->setMimeData(saved); }
        } clipboardRestore;
        for(const auto mode:{ThemeMode::Light,ThemeMode::Dark}) {
            const auto suffix=mode==ThemeMode::Dark ? "dark" : "light";
            ApplicationThemeManager::instance().setMode(mode);
            h.navigation()->setActiveTab(NavigationWidget::FileTab);
            capture(h,QString("icons-normal-%1").arg(suffix));
            h.navigation()->setActiveTab(NavigationWidget::DesignTab);
            QVERIFY(h.workbench()->isVisible());
            QVERIFY(!h.window->findChild<QAction*>("contextRail.simdock"));
            capture(h,QString("icons-simulation-%1").arg(suffix));
            auto* settings=h.workbench()->findChild<QToolButton*>("openSettings");QVERIFY(settings&&!settings->icon().isNull());
            const ContextPlacement kept{ContextSurface::Docked,ContextPersistence::Kept};
            QVERIFY(controller->openResource(home,kept));
            auto* view=qobject_cast<PinloomContextView*>(controller->viewForResource(home.stableKey())); QVERIFY(view);
            QTRY_VERIFY(view->isVisible());
            QVERIFY(!view->copyLinkButton()->isEnabled());
            capture(h,QString("icons-copy-disabled-%1").arg(suffix));
            QVERIFY(controller->openResource(linked,kept));
            QTRY_VERIFY(view->copyLinkButton()->isEnabled());
            QTRY_VERIFY(view->previewEditor()->toPlainText().contains("Package declarations"));
            capture(h,QString("icons-copy-enabled-%1").arg(suffix));
            QTest::mouseClick(view->copyLinkButton(),Qt::LeftButton);
            QCOMPARE(QApplication::clipboard()->text(),entry.value("uri").toString());
            QVERIFY(controller->closePinnedResource(home.stableKey()));
            QVERIFY(h.sessions()->openWorkspace(h.other));
            QVERIFY(h.workbench()->controller()->target().isEmpty());
            ApplicationThemeManager::instance().setMode(mode);
            capture(h,QString("icons-current-editor-%1").arg(suffix));
            QVERIFY(h.sessions()->openWorkspace(h.workspace));
            QCOMPARE(h.workbench()->controller()->target().file,selected.file);
            QCOMPARE(h.workbench()->controller()->target().top,selected.top);
        }
        ApplicationThemeManager::instance().setMode(ThemeMode::Light);
    }
private slots:
    void initTestCase() { QVERIFY(initializeUiStyleForTest()); }
    void leftTabLabelsAcrossDocumentKindsAndRestore() {
        if (QGuiApplication::platformName()!=QStringLiteral("windows"))
            QSKIP("Markdown/Block/PDF workers require native Windows hosts; run this case in the native DPR matrix");
        ApplicationThemeManager::instance().setMode(ThemeMode::Light);
        Host h;
        const QSize available = h.window->screen()->availableGeometry().size();
        h.window->resize(qMin(1400, available.width()-40), qMin(900, available.height()-80));
        const auto expectedDpr = qEnvironmentVariable("ZEROSLACK_EXPECTED_DPR");
        if (!expectedDpr.isEmpty()) QCOMPARE(h.window->devicePixelRatioF(), expectedDpr.toDouble());
        QVERIFY(h.sessions()->openWorkspace(h.workspace));
        auto* tabs = h.window->tabManager.get();
        auto* split = tabs->editorSplitController();
        auto* left = split->regionGroup("left");
        QVERIFY(left);
        auto* bar = left->tabBar();
        h.navigation()->setActiveTab(NavigationWidget::FileTab);
        auto* files = left->currentWidget();
        const int filesIndex = left->currentIndex();
        QCOMPARE(left->tabText(filesIndex), QString("Files"));
        h.navigation()->setActiveTab(NavigationWidget::DesignTab);
        auto* design = left->currentWidget();
        const int designIndex = left->currentIndex();
        QCOMPARE(left->tabText(designIndex), QString("Design"));
        QApplication::processEvents();
        QVERIFY(bar->tabRect(designIndex).width() > bar->tabRect(filesIndex).width());
        QCOMPARE(bar->accessibleTabName(filesIndex), QString("Files"));
        QCOMPARE(bar->accessibleTabName(designIndex), QString("Design"));
        QTest::mouseMove(h.window.get(),QPoint(h.window->width()-80,80));
        capture(h, "left-tabs-design");
        QTest::mouseClick(bar, Qt::LeftButton, {}, bar->tabRect(filesIndex).center());
        QCOMPARE(left->currentWidget(), files);
        QVERIFY(bar->tabRect(filesIndex).width() > bar->tabRect(designIndex).width());
        QTest::mouseMove(h.window.get(),QPoint(h.window->width()-80,80));
        capture(h, "left-tabs-files");

        const auto markdownPath = h.workspace+"/notes.md";
        const auto blocksPath = h.workspace+"/diagram.zblock";
        const auto pdfPath = h.workspace+"/reference.pdf";
        put(markdownPath, "# Sidebar labels\n\nEach document retains its full title.\n");
        {
            QPdfWriter writer(pdfPath); writer.setPageSize(QPageSize(QPageSize::A6));
            QPainter painter(&writer); painter.drawText(100, 200, "Sidebar reference");
        }
        QVERIFY(tabs->openFileInRegion(h.workspace+"/rtl/design.sv", "main"));
        auto* code = tabs->getCurrentEditor();
        QVERIFY(code);
        code->insertPlainText("// unsaved sidebar view\n");
        QVERIFY(split->movePage(code, left));
        const QString codeTitle = left->tabText(left->indexOf(code));
        QVERIFY(codeTitle.contains(QChar(0x25cf)));
        QVERIFY(tabs->openFileInRegion(markdownPath, "left"));
        auto* markdown = tabs->currentWebDocument();
        QVERIFY(markdown); QTRY_VERIFY(markdown->ready());
        QString error;
        auto* blocks = tabs->createWebDocumentFile(WebDocumentPage::Kind::Blocks, blocksPath, "left", &error);
        QVERIFY2(blocks, qPrintable(error));
        QVERIFY(tabs->openFileInRegion(pdfPath, "left"));
        auto* pdf = tabs->currentPdfDocument();
        QVERIFY(pdf); QTRY_VERIFY(pdf->ready());
        const QList<QWidget*> pages{files, design, code, markdown, blocks, pdf};
        for (auto* page : pages) {
            QVERIFY(split->activatePage(page));
            QApplication::processEvents();
            const int current = left->indexOf(page);
            QCOMPARE(left->currentWidget(), page);
            hoverTab(bar,current);
            QApplication::processEvents();
            for (int i=0; i<left->count(); ++i) {
                QVERIFY(!left->tabText(i).isEmpty());
                QVERIFY(!left->tabToolTip(i).isEmpty());
                auto* close = bar->tabButton(i,QTabBar::RightSide);
                if (!close) close = bar->tabButton(i,QTabBar::LeftSide);
                if (close) QTRY_COMPARE(close->isVisible(), i==current);
            }
            capture(h,"left-tabs-page-"+QString::number(current)+"-hover");
            QTest::mouseMove(h.window.get(),QPoint(h.window->width()-80,80));
            capture(h, "left-tabs-page-"+QString::number(current));
        }
        QCOMPARE(left->tabText(left->indexOf(code)), codeTitle);
        bar->moveTab(left->indexOf(markdown),0);
        QCOMPARE(left->widget(0), markdown);
        const auto identity = split->pageId(markdown);
        for (const auto& region : {QString("main"), QString("right"), QString("bottom"), QString("left")}) {
            auto* target = split->regionGroup(region);
            QVERIFY(target);
            QVERIFY(split->movePage(markdown,target));
            QCOMPARE(split->pageId(markdown),identity);
            QCOMPARE(target->tabText(target->indexOf(markdown)),QString("notes.md"));
            QCOMPARE(target->tabBar()->property("currentLabelTabs").toBool(),region=="left");
        }
        auto* lower = split->movePageToSplit(blocks,EditorSplitDirection::Below,left);
        QVERIFY(lower && lower!=left);
        QCOMPARE(split->regionForGroup(lower),QString("left"));
        QVERIFY(lower->tabBar()->property("currentLabelTabs").toBool());
        const auto layout = split->savePageLayout();
        const auto blockIdentity = split->pageId(blocks);
        QVERIFY(split->movePage(blocks,split->regionGroup("main")));
        split->removeEmptyGroups();
        QVERIFY(split->restorePageLayout(layout).isEmpty());
        QCOMPARE(split->pageId(blocks),blockIdentity);
        QCOMPARE(split->regionForGroup(split->groupForPage(blocks)),QString("left"));
        QVERIFY(split->groupForPage(blocks)!=left);
        capture(h,"left-tabs-restored-groups");
        QVERIFY(split->movePage(blocks,left)); split->removeEmptyGroups();
        split->regionHost("left")->setFixedWidth(220);
        // Finish native WebView reparent/focus notifications before the next selection.
        QTest::qWait(60);
        QVERIFY(split->activatePage(code));
        QApplication::processEvents();
        QCOMPARE(left->currentWidget(),code);
        auto* close = bar->tabButton(left->indexOf(code),QTabBar::RightSide);
        if (!close) close = bar->tabButton(left->indexOf(code),QTabBar::LeftSide);
        hoverTab(bar,left->indexOf(code));
        qInfo() << "LEFT_TAB_NARROW_CLOSE" << bar->size() << bar->tabRect(left->indexOf(code))
                << (close?close->geometry():QRect()) << bar->mapFromGlobal(QCursor::pos())
                << "selected" << bar->currentIndex() << "code" << left->indexOf(code);
        QTRY_VERIFY(close && close->isVisible());
        QVERIFY(bar->rect().contains(close->geometry()));
        const int narrowCurrent = bar->tabRect(left->indexOf(code)).width();
        const int narrowInactive = bar->tabRect(left->indexOf(files)).width();
        QVERIFY(narrowCurrent > narrowInactive);
        qInfo() << "LEFT_TAB_GEOMETRY" << "dpr" << h.window->devicePixelRatioF()
                << "window" << h.window->size() << "bar" << bar->size()
                << "selected" << narrowCurrent << "inactive" << narrowInactive;
        QTest::mouseMove(h.window.get(),QPoint(h.window->width()-80,80));
        capture(h,"left-tabs-narrow");
        ApplicationThemeManager::instance().setMode(ThemeMode::Dark);
        capture(h,"left-tabs-narrow-dark");
        ApplicationThemeManager::instance().setMode(ThemeMode::Light);
        code->undo();
        QVERIFY(!tabs->sharedDocumentForEditor(code)->dirty());
        QVERIFY(tabs->saveEditorView(code));
        QVERIFY(!tabs->hasUnsavedChanges());
        QVERIFY(h.sessions()->saveSession(false));
        QVERIFY(h.sessions()->openWorkspace(h.other));
        QVERIFY(h.sessions()->openWorkspace(h.workspace));
        auto* restored = split->pageById(identity);
        QVERIFY(restored);
        QCOMPARE(split->regionForGroup(split->groupForPage(restored)),QString("left"));
        QVERIFY(split->activatePage(restored));
        QCOMPARE(split->groupForPage(restored)->tabText(split->groupForPage(restored)->indexOf(restored)),QString("notes.md"));
        capture(h,"left-tabs-workspace-reopened");
    }
    void legacyExplicitSelection_data() {
        QTest::addColumn<QString>("module");
        QTest::newRow("existing-child") << QString("sidebar_leaf");
        QTest::newRow("missing-kept-unresolved") << QString("gone_module");
    }
    void legacyExplicitSelection() {
        QFETCH(QString, module);
        Host h;
        WorkspaceConfigurationService service;
        auto config=service.defaultConfiguration(h.workspace); config.topModule=module;
        QVERIFY(service.save(config));
        h.navigation()->setActiveTab(NavigationWidget::DesignTab);
        QVERIFY(h.sessions()->openWorkspace(h.workspace));
        QTRY_COMPARE_WITH_TIMEOUT(h.workbench()->designRoots().size(),1,15000);
        const auto root=h.workbench()->designRoots().first();
        QCOMPARE(root.module,module); QCOMPARE(h.workbench()->designService()->activeProjectId(),root.projectId);
        QCOMPARE(root.file.isEmpty(),module=="gone_module");
        auto* tree=h.window->findChild<QTreeWidget*>("navigationDesignTree");
        QTRY_COMPARE_WITH_TIMEOUT(tree->topLevelItemCount(),1,15000);
        QTRY_COMPARE_WITH_TIMEOUT(tree->topLevelItem(0)->data(0, NavigationRowStyle::UnresolvedRole).toBool(),
                                  module=="gone_module",15000);
        const auto* row = tree->topLevelItem(0);
        QVERIFY(!row->text(0).contains("(unresolved)"));
        QCOMPARE(row->toolTip(0).contains("Unresolved:", Qt::CaseInsensitive), module=="gone_module");
        QCOMPARE(row->data(0, Qt::AccessibleDescriptionRole).toString().contains("Unresolved:", Qt::CaseInsensitive),
                 module=="gone_module");
        QVERIFY(h.sessions()->openWorkspace(h.other));
        QVERIFY(h.sessions()->openWorkspace(h.workspace));
        QCOMPARE(h.workbench()->designRoots().size(),1);
        QCOMPARE(h.workbench()->designService()->activeProjectId(),root.projectId);
    }
    void welcomeAndProjectWorkflow() {
        Host h;
        auto* welcome=h.window->findChild<QScrollArea*>("welcomePage"); QVERIFY(welcome);
        QTRY_VERIFY(welcome->isVisible());
        auto* rtl=dynamic_cast<WelcomeCard*>(h.window->findChild<QPushButton*>("welcomeCard0"));
        auto* wave=dynamic_cast<WelcomeCard*>(h.window->findChild<QPushButton*>("welcomeCard1"));
        auto* registers=dynamic_cast<WelcomeCard*>(h.window->findChild<QPushButton*>("welcomeCard2"));
        QVERIFY(rtl && wave && registers); QVERIFY(rtl->isEnabled()); QVERIFY(!wave->isEnabled() && !registers->isEnabled());
        QVERIFY(rtl->findChildren<QVariantAnimation*>().isEmpty()); capture(h,"welcome-light");
        h.window->resize(700,900); QTest::qWait(100);
        QCOMPARE(welcome->horizontalScrollBar()->maximum(),0); capture(h,"welcome-narrow");
        ApplicationThemeManager::instance().setMode(ThemeMode::Dark);
        h.window->resize(1400,900); QTest::qWait(100); capture(h,"welcome-dark");
        ApplicationThemeManager::instance().setMode(ThemeMode::Light);
        bool picker=false;
        QTimer::singleShot(100,[&]{ for(auto* widget:QApplication::topLevelWidgets())if(auto* dialog=qobject_cast<QFileDialog*>(widget)){picker=true; dialog->reject();}});
        rtl->click(); QVERIFY(picker); QVERIFY(!h.window->workspaceManager->isWorkspaceOpen());
        QVERIFY(h.sessions()->openWorkspace(h.workspace));
        QTRY_VERIFY(!welcome->isVisible()); QVERIFY(rtl->findChildren<QVariantAnimation*>().isEmpty());
        auto* workbench=h.workbench(); QVERIFY(workbench);
        QTRY_VERIFY_WITH_TIMEOUT(workbench->workspace()==h.workspace,10000);
        QVERIFY(workbench->designService()->activeProjectId().isEmpty());
        QVERIFY(h.manager());
        QTRY_VERIFY_WITH_TIMEOUT(h.manager()->setDesignTop("sidebar_parent",h.workspace+"/rtl/design.sv").isEmpty(),15000);
        const auto parent=workbench->designService()->activeProjectId();
        QVERIFY(h.manager()->setDesignTop("sidebar_leaf",h.workspace+"/rtl/design.sv").isEmpty());
        const auto leaf=workbench->designService()->activeProjectId(); QVERIFY(parent!=leaf);
        QCOMPARE(workbench->designRoots().size(),2);
        auto* tree=h.window->findChild<QTreeWidget*>("navigationDesignTree"); QVERIFY(tree);
        QTRY_COMPARE_WITH_TIMEOUT(tree->topLevelItemCount(),2,15000);
        QCOMPARE(tree->columnCount(),1); QVERIFY(tree->isHeaderHidden());
        QTRY_VERIFY_WITH_TIMEOUT(tree->topLevelItem(0)->childCount()==1,15000);
        auto* parentRow=tree->topLevelItem(0); auto* leafRow=tree->topLevelItem(1);
        QCOMPARE(parentRow->child(0)->text(0),QString("u_leaf [sidebar_leaf]"));
        const auto parentRect=tree->visualItemRect(parentRow);
        QTest::mouseClick(tree->viewport(),Qt::LeftButton,{},parentRect.center());
        QVERIFY(!parentRow->isExpanded()); QCOMPARE(workbench->designService()->activeProjectId(),leaf);
        QTest::mouseClick(tree->viewport(),Qt::LeftButton,{},QPoint(parentRect.right()-12,parentRect.center().y()));
        QTRY_COMPARE(workbench->designService()->activeProjectId(),parent);
        QCOMPARE(h.window->workspaceManager->getProjectModel()->snapshot().activeDesignProjectId,parent);
        QTRY_VERIFY(tree->topLevelItemCount()==2
                    && tree->topLevelItem(0)->data(0, NavigationRowStyle::ActiveRole).toBool());
        QVERIFY(tree->topLevelItem(0)->toolTip(0).contains("Active project"));
        QVERIFY(tree->topLevelItem(0)->data(0, Qt::AccessibleDescriptionRole).toString().contains("Active project"));
        QVERIFY(!tree->topLevelItem(0)->isExpanded()); capture(h,"design-projects");
        h.navigation()->setActiveTab(NavigationWidget::FileTab);
        auto* files=h.window->findChild<QTreeWidget*>("navigationFileTree"); QVERIFY(files);
        QTRY_VERIFY(files->topLevelItemCount()>0);
        for(QTreeWidgetItemIterator it(files);*it;++it) { QVERIFY(!(*it)->text(0).contains("[TOP]")); QVERIFY(!(*it)->font(0).bold()); }
        capture(h,"files-tree");
        QVERIFY(workbench->selectTarget(h.workspace+"/a_use.sv").isEmpty());
        QCOMPARE(workbench->designService()->activeProjectId(),parent);
        const auto plan=[&] {
            QString error;auto request=simdock::SimulationBuildPlanner::workspaceRequest(h.workspace,workbench->controller()->target(),&error);
            return simdock::SimulationBuildPlanner::plan(request);
        };
        auto selected=plan();QVERIFY2(selected->valid(),qPrintable(selected->error()));
        const auto ordered=selected->compilation.files;
        QVERIFY(ordered.indexOf(SemanticInputCapture::pathKey(h.workspace+"/z_pkg.sv"))<ordered.indexOf(SemanticInputCapture::pathKey(h.workspace+"/a_use.sv")));
        QVERIFY(!h.window->findChild<QWidget*>("sourceList"));QVERIFY(!h.window->findChild<QWidget*>("projectList"));
        put(h.workspace+"/z_pkg.sv","package changed_pkg; endpackage\n");
        QVERIFY(plan()->error().contains("build_pkg"));
        put(h.workspace+"/z_pkg.sv","package build_pkg; typedef logic [3:0] word_t; endpackage\n");
        QVERIFY(plan()->valid());QCOMPARE(workbench->designService()->activeProjectId(),parent);
        capture(h,"compact-simulation");
        captureIconSurfaces(h);
        QVERIFY(!QTest::currentTestFailed());
        QVERIFY(h.sessions()->openWorkspace(h.other));
        QCOMPARE(workbench->designService()->activeProjectId(),QString());
        QVERIFY(h.sessions()->openWorkspace(h.workspace)); QCOMPARE(workbench->designService()->activeProjectId(),parent);
        h.navigation()->setActiveTab(NavigationWidget::DesignTab);
        QTRY_COMPARE_WITH_TIMEOUT(tree->topLevelItemCount(),2,15000);
        for(int i=0;i<tree->topLevelItemCount();++i)
            if(tree->topLevelItem(i)->text(0).contains("sidebar_parent")) QVERIFY(!tree->topLevelItem(i)->isExpanded());
        QVERIFY(h.sessions()->saveSession(false));
    }
};
int main(int argc,char** argv) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc,argv); QTemporaryDir settings;
    QCoreApplication::setOrganizationName("ZeroSlackTests"); QCoreApplication::setApplicationName("SidebarMainWindow");
    QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    QStandardPaths::setTestModeEnabled(true);
    SidebarMainWindowTest test;return QTest::qExec(&test,argc,argv);
}
#include "sidebar_mainwindow_test.moc"
