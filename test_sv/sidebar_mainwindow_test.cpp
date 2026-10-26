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

namespace {
void put(const QString& path,const QByteArray& bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath()); QFile file(path);
    if (!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()) qFatal("fixture write");
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
    }
    void captureIconSurfaces(Host& h) {
        if (!qEnvironmentVariableIsSet("ZEROSLACK_ICONS_EVIDENCE")) return;
        auto* controller=h.window->findChild<ContextWorkspaceController*>(); QVERIFY(controller);
        auto* rename=h.window->findChild<QPushButton*>("renameProject"); QVERIFY(rename);
        const auto projectId=h.workbench()->activeProjectId();
        const auto originalName=[&] {
            for(const auto& p:simdock::loadProjects(h.workspace)) if(p.id==projectId) return p.name;
            return QString();
        }();
        QVERIFY(!originalName.isEmpty());
        for(const auto& name:{QString("Icon review"),originalName}) {
            bool accepted=false;
            QTimer::singleShot(100,[&] {
                auto* dialog=h.window->findChild<QDialog*>("newProjectDialog");
                if(!dialog) return;
                auto* field=dialog->findChild<QLineEdit*>("projectNameInput");
                auto* confirm=dialog->findChild<QPushButton*>("confirmCreateProject");
                if(!field || !confirm) return;
                field->setText(name); confirm->click(); accepted=true;
            });
            rename->click(); QVERIFY(accepted);
            QCOMPARE(h.workbench()->activeProjectId(),projectId);
            bool saved=false;
            for(const auto& p:simdock::loadProjects(h.workspace)) if(p.id==projectId) saved=p.name==name;
            QVERIFY(saved);
        }
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
            h.navigation()->setActiveTab(NavigationWidget::SourceFilesTab);
            auto* simulation=h.window->findChild<QAction*>("contextRail.simdock"); QVERIFY(simulation);
            simulation->trigger(); QTest::qWait(240);
            capture(h,QString("icons-simulation-%1").arg(suffix));
            QVERIFY(controller->openTool("toolbox")); QTest::qWait(240);
            auto* run=h.window->findChild<QToolButton*>("contextToolRun.simdock"); QVERIFY(run && run->isVisible());
            capture(h,QString("icons-toolbox-%1").arg(suffix));
            run->click(); QTest::qWait(240); QVERIFY(!controller->toolboxVisible());
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
            h.navigation()->setActiveTab(NavigationWidget::SourceFilesTab);
            QTRY_VERIFY(!rename->isEnabled());
            ApplicationThemeManager::instance().setMode(mode);
            capture(h,QString("icons-rename-disabled-%1").arg(suffix));
            QVERIFY(h.sessions()->openWorkspace(h.workspace));
            QTRY_COMPARE(h.workbench()->activeProjectId(),projectId);
        }
        ApplicationThemeManager::instance().setMode(ThemeMode::Light);
    }
private slots:
    void initTestCase() { QVERIFY(initializeUiStyleForTest()); }
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
        QCOMPARE(root.module,module); QCOMPARE(h.workbench()->activeProjectId(),root.projectId);
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
        QCOMPARE(h.workbench()->activeProjectId(),root.projectId);
    }
    void welcomeAndProjectWorkflow() {
        Host h;
        auto* welcome=h.window->findChild<QScrollArea*>("welcomePage"); QVERIFY(welcome);
        QTRY_VERIFY(welcome->isVisible());
        auto* rtl=dynamic_cast<WelcomeCard*>(h.window->findChild<QPushButton*>("welcomeCard0"));
        auto* wave=dynamic_cast<WelcomeCard*>(h.window->findChild<QPushButton*>("welcomeCard1"));
        auto* registers=dynamic_cast<WelcomeCard*>(h.window->findChild<QPushButton*>("welcomeCard2"));
        QVERIFY(rtl && wave && registers); QVERIFY(rtl->isEnabled()); QVERIFY(!wave->isEnabled() && !registers->isEnabled());
        QVERIFY(!rtl->motionRunning()); capture(h,"welcome-light");
        if (QGuiApplication::platformName()=="windows") {
            QTRY_VERIFY(h.window->isActiveWindow());
            QEnterEvent enter({},{},{}); QCoreApplication::sendEvent(rtl,&enter);
            QTRY_VERIFY(rtl->motionRunning());
            QEvent leave(QEvent::Leave); QCoreApplication::sendEvent(rtl,&leave); QVERIFY(!rtl->motionRunning());
            for (auto* card : {rtl,wave,registers}) {
                QCoreApplication::sendEvent(card,&enter); QVERIFY(card->motionRunning());
                QCoreApplication::sendEvent(card,&leave); QVERIFY(!card->motionRunning());
                QCoreApplication::sendEvent(card,&enter); QVERIFY(card->motionRunning());
                QEvent deactivate(QEvent::WindowDeactivate); QCoreApplication::sendEvent(h.window.get(),&deactivate);
                QVERIFY(!card->motionRunning());
            }
            QCoreApplication::sendEvent(rtl,&enter); QVERIFY(rtl->motionRunning());
            h.window->showMinimized(); QTRY_VERIFY(!rtl->motionRunning());
            h.window->showNormal(); h.window->activateWindow(); QTest::qWait(80); QVERIFY(!rtl->motionRunning());
            QCoreApplication::sendEvent(rtl,&leave); QCoreApplication::sendEvent(rtl,&enter);
            QTRY_VERIFY(rtl->motionRunning());
            QTRY_VERIFY_WITH_TIMEOUT(!rtl->motionRunning(),3500);
            QCoreApplication::sendEvent(rtl,&leave);
            ApplicationThemeManager::instance().setAnimationsEnabled(false);
            QCoreApplication::sendEvent(rtl,&enter); QVERIFY(!rtl->motionRunning());
            ApplicationThemeManager::instance().setAnimationsEnabled(true);
        }
        h.window->resize(700,900); QTest::qWait(100);
        QCOMPARE(welcome->horizontalScrollBar()->maximum(),0); capture(h,"welcome-narrow");
        ApplicationThemeManager::instance().setMode(ThemeMode::Dark);
        h.window->resize(1400,900); QTest::qWait(100); capture(h,"welcome-dark");
        ApplicationThemeManager::instance().setMode(ThemeMode::Light);
        bool picker=false;
        QTimer::singleShot(100,[&]{ for(auto* widget:QApplication::topLevelWidgets())if(auto* dialog=qobject_cast<QFileDialog*>(widget)){picker=true; dialog->reject();}});
        rtl->click(); QVERIFY(picker); QVERIFY(!h.window->workspaceManager->isWorkspaceOpen());
        QVERIFY(h.sessions()->openWorkspace(h.workspace));
        QTRY_VERIFY(!welcome->isVisible()); QVERIFY(!rtl->motionRunning());
        auto* workbench=h.workbench(); QVERIFY(workbench);
        QTRY_VERIFY_WITH_TIMEOUT(workbench->workspace()==h.workspace,10000);
        QVERIFY(workbench->activeProjectId().isEmpty());
        QVERIFY(h.manager());
        QTRY_VERIFY_WITH_TIMEOUT(h.manager()->setDesignTop("sidebar_parent",h.workspace+"/rtl/design.sv").isEmpty(),15000);
        const auto parent=workbench->activeProjectId();
        QVERIFY(h.manager()->setDesignTop("sidebar_leaf",h.workspace+"/rtl/design.sv").isEmpty());
        const auto leaf=workbench->activeProjectId(); QVERIFY(parent!=leaf);
        QCOMPARE(workbench->designRoots().size(),2);
        auto* tree=h.window->findChild<QTreeWidget*>("navigationDesignTree"); QVERIFY(tree);
        QTRY_COMPARE_WITH_TIMEOUT(tree->topLevelItemCount(),2,15000);
        QCOMPARE(tree->columnCount(),1); QVERIFY(tree->isHeaderHidden());
        QTRY_VERIFY_WITH_TIMEOUT(tree->topLevelItem(0)->childCount()==1,15000);
        auto* parentRow=tree->topLevelItem(0); auto* leafRow=tree->topLevelItem(1);
        QCOMPARE(parentRow->child(0)->text(0),QString("u_leaf [sidebar_leaf]"));
        const auto parentRect=tree->visualItemRect(parentRow);
        QTest::mouseClick(tree->viewport(),Qt::LeftButton,{},parentRect.center());
        QVERIFY(!parentRow->isExpanded()); QCOMPARE(workbench->activeProjectId(),leaf);
        QTest::mouseClick(tree->viewport(),Qt::LeftButton,{},QPoint(parentRect.right()-12,parentRect.center().y()));
        QTRY_COMPARE(workbench->activeProjectId(),parent);
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
        h.navigation()->setActiveTab(NavigationWidget::SourceFilesTab);
        auto* inputs=h.window->findChild<QListView*>("sourceList"); QVERIFY(inputs);
        QTRY_VERIFY(inputs->model()->rowCount()>=3);
        for(const auto& relative:{QString("a_use.sv"),QString("z_pkg.sv")}) {
            for(int row=0;row<inputs->model()->rowCount();++row)if(inputs->model()->index(row,0).data(Qt::UserRole).toString()==relative)
                QVERIFY(inputs->model()->setData(inputs->model()->index(row,0),Qt::Checked,Qt::CheckStateRole));
        }
        auto saved=[&] { for(const auto& p:simdock::loadProjects(h.workspace))if(p.id==parent)return p; return simdock::Project{}; };
        QTRY_VERIFY_WITH_TIMEOUT(saved().sources.indexOf("z_pkg.sv")<saved().sources.indexOf("a_use.sv"),10000);
        auto* status=h.window->findChild<QLabel*>("buildInputsStatus"); QVERIFY(status);
        QTRY_VERIFY(status->text().isEmpty() && status->isHidden());
        put(h.workspace+"/z_pkg.sv","package changed_pkg; endpackage\n");
        QTRY_VERIFY_WITH_TIMEOUT(status->text().contains("build_pkg"),10000);
        QVERIFY(!status->isHidden());
        put(h.workspace+"/z_pkg.sv","package build_pkg; typedef logic [3:0] word_t; endpackage\n");
        QTRY_VERIFY_WITH_TIMEOUT(status->text().isEmpty() && status->isHidden(),10000);
        auto* projectList=h.window->findChild<QListView*>("projectList"); QVERIFY(projectList);
        QTRY_VERIFY(projectList->visualRect(projectList->model()->index(1,0)).bottom()<projectList->viewport()->height());
        capture(h,"build-inputs");
        auto* projects=h.window->findChild<QPushButton*>("projectsDisclosure");
        auto* inputSection=h.window->findChild<QPushButton*>("inputsDisclosure"); QVERIFY(projects&&inputSection);
        const auto initialHeight=inputs->height(); projects->click(); QTest::qWait(80); QVERIFY(inputs->height()>initialHeight);
        projects->click(); inputSection->click(); QTest::qWait(80);
        QVERIFY(h.window->findChild<QListView*>("projectList")->height()>140); capture(h,"projects-expanded");
        inputSection->click();
        captureIconSurfaces(h);
        QVERIFY(!QTest::currentTestFailed());
        QVERIFY(h.sessions()->openWorkspace(h.other));
        QCOMPARE(workbench->activeProjectId(),QString());
        QVERIFY(h.sessions()->openWorkspace(h.workspace)); QCOMPARE(workbench->activeProjectId(),parent);
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
