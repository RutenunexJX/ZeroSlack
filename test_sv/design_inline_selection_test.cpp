#include "mainwindow.h"
#include "navigationwidget.h"
#include "navigationmanager.h"
#include "navigationservice.h"
#include "navigationrowstyle.h"
#include "workspacemanager.h"
#include "tabmanager.h"
#include "tabfileio.h"
#include "applicationthememanager.h"
#include "workspaceconfigurationservice.h"
#include "semanticindexsnapshot.h"
#include "semantic_fixture_records.h"
#include "usertemplateservice.h"
#include "crashrecoveryservice.h"
#include "testuistyle.h"
#include "../src/simulation/simdock/ui/workbench.h"
#include <QtTest>
#include <QMenu>
#include <QScreen>
#include <QScrollBar>
#include <QStandardPaths>
#include <QTreeWidgetItemIterator>

namespace {
constexpr int FileRole = Qt::UserRole + 60, ModuleRole = Qt::UserRole + 61;
bool put(const QString& path, const QByteArray& data) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);return f.open(QIODevice::WriteOnly) && f.write(data)==data.size();
}
QTreeWidget* candidates(NavigationWidget& widget) {return widget.findChild<QTreeWidget*>("designCandidateTree");}
QTreeWidget* tree(NavigationWidget& widget) {return widget.findChild<QTreeWidget*>("navigationDesignTree");}
QLineEdit* search(NavigationWidget& widget) {return widget.findChild<QLineEdit*>("navigationSearchLineEdit");}
QTreeWidgetItem* allFiles(NavigationWidget& widget) {
    auto* view=candidates(widget);
    for(int i=0;i<view->topLevelItemCount();++i)
        if(view->topLevelItem(i)->data(0,Qt::UserRole).toString()=="allFiles")return view->topLevelItem(i);
    return nullptr;
}
QTreeWidgetItem* fileRow(NavigationWidget& widget,const QString& file) {
    auto* all=allFiles(widget);if(!all)return nullptr;
    for(int i=0;i<all->childCount();++i)if(all->child(i)->data(0,FileRole).toString()==file)return all->child(i);
    return nullptr;
}
void clickRow(QTreeWidget* view,QTreeWidgetItem* item) {
    view->scrollToItem(item);
    const auto rect=view->visualItemRect(item);
    QTest::mouseClick(view->viewport(),Qt::LeftButton,Qt::NoModifier,QPoint(qMin(rect.right()-5,rect.left()+80),rect.center().y()));
}
bool chooseFileMenu(NavigationWidget& widget,const QString& file,const QString& moduleName,bool submenu) {
    bool selected=false;
    QTimer::singleShot(30,&widget,[&] {
        auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if(!menu)return;
        for(auto* action:menu->actions())if(action->property("actionId").toString()=="navigation.design.setTop") {
            if(submenu && action->menu()) {
                for(auto* child:action->menu()->actions())if(child->property("moduleType").toString()==moduleName) {
                    selected=true;child->trigger();break;
                }
            } else if(!submenu && !action->menu()) {selected=true;action->trigger();}
            break;
        }
        menu->close();
    });
    widget.fileTreeNodeContextMenuRequested(file,false,widget.mapToGlobal(QPoint(25,140)));
    return selected;
}
void capture(QWidget& widget,const QString& name) {
    const QString output=qEnvironmentVariable("ZEROSLACK_DESIGN_REPORT");
    if(output.isEmpty())return;
    QDir().mkpath(output);QTest::qWait(40);
    auto pix=widget.grab();pix.save(output+'/'+name+".png");
    const auto available=widget.screen()->availableGeometry();
    QJsonObject record{{"platform",QGuiApplication::platformName()},{"width",widget.width()},{"height",widget.height()},
        {"pixelWidth",pix.width()},{"pixelHeight",pix.height()},{"dpr",pix.devicePixelRatio()},
        {"availableGeometry",QJsonArray{available.x(),available.y(),available.width(),available.height()}}};
    put(output+'/'+name+".json",QJsonDocument(record).toJson());
}
SemanticSymbolRecord module(const QString& name,const QString& file,int position=0) {
    auto r=SemanticFixtureRecordBuilder(name,SymbolTaxonomy::DeclarationKind::Module).withFile(file).record();
    r.location.position=position;r.stableKey.sourcePosition=position;
    return r;
}
}

class DesignInlineSelectionTest : public QObject {
    Q_OBJECT
private slots:
    void emptySearchAndCandidateIdentity() {
        QTemporaryDir data;QVERIFY(data.isValid());
        NavigationWidget widget;widget.resize(300,650);widget.show();widget.setActiveTab(NavigationWidget::DesignTab);
        QVERIFY(widget.findChild<QLabel*>("designEmptyState")->text().contains("Open a workspace"));
        QCOMPARE(tree(widget)->topLevelItemCount(),0);
        widget.setWorkspaceRoot(data.path());
        widget.setDesignCandidateContext({},false,true);
        QVERIFY(widget.findChild<QLabel*>("designEmptyState")->text().contains("Scanning"));
        widget.setDesignCandidateContext({},false,false);
        QVERIFY(widget.findChild<QLabel*>("designEmptyState")->text().contains("No files"));
        const QString one=data.filePath("one.sv"),multi=data.filePath("multi.sv"),header=data.filePath("header.svh"),
            unknown=data.filePath("unknown.svi"),other=data.filePath("other.sv"),ambiguous=data.filePath("ambiguous.sv");
        const QStringList files{one,multi,header,unknown,other,ambiguous};
        widget.setDesignCandidateContext(files,false,false);
        widget.updateDesignCandidates(data.path(),{
            {one,{{"same",false,false}},true,false},{multi,{{"first",false,false},{"second",false,false}},true,false},
            {header,{},true,false},{unknown,{},false,false},{other,{{"same",false,true}},true,false},
            {ambiguous,{{"same",true,false}},true,false}},{"same","second"});
        QVERIFY(search(widget)->isVisible());QVERIFY(allFiles(widget));QCOMPARE(allFiles(widget)->childCount(),6);
        QVERIFY(allFiles(widget)->isExpanded());
        QSignalSpy chosen(&widget,&NavigationWidget::designModuleChosen);
        clickRow(candidates(widget),fileRow(widget,multi));
        QCOMPARE(chosen.size(),0);QVERIFY(fileRow(widget,multi)->isExpanded());
        clickRow(candidates(widget),fileRow(widget,multi)->child(1));
        QCOMPARE(chosen.size(),1);QCOMPARE(chosen.first().at(0).toString(),multi);QCOMPARE(chosen.first().at(1).toString(),"second");
        QVERIFY(!(fileRow(widget,other)->flags()&Qt::ItemIsEnabled));
        QVERIFY(!(fileRow(widget,ambiguous)->flags()&Qt::ItemIsEnabled));
        QVERIFY(fileRow(widget,header)->toolTip(0).contains("No module definitions"));
        QVERIFY(fileRow(widget,unknown)->toolTip(0).contains("Analysis not available"));
        widget.setSearchFilter(NavigationWidget::FileTab,"kept-file-query");
        search(widget)->setText("SECOND");QVERIFY(allFiles(widget));QCOMPARE(allFiles(widget)->childCount(),1);
        QCOMPARE(allFiles(widget)->child(0)->data(0,FileRole).toString(),multi);
        widget.finishDesignProjectSelection("Definition changed",multi,"second");
        QCOMPARE(search(widget)->text(),"SECOND");
        QVERIFY(widget.findChild<QLabel*>("designSelectionError")->isVisible());
        search(widget)->setText("does-not-exist");
        QVERIFY(search(widget)->isVisible());QVERIFY(widget.findChild<QLabel*>("designEmptyState")->text()=="No matches");
        widget.findChild<QAbstractButton*>("clearDesignSearch")->click();
        QCOMPARE(allFiles(widget)->childCount(),6);
        QCOMPARE(widget.searchFilter(NavigationWidget::FileTab),"kept-file-query");
        // A result from a different workspace must not replace this list.
        widget.updateDesignCandidates(data.filePath("other-workspace"),{},{});
        QCOMPARE(allFiles(widget)->childCount(),6);
        capture(widget,"selector-all-files");
    }

    void designRowsAndCancel_data() {
        QTest::addColumn<bool>("dark");QTest::addColumn<int>("width");
        QTest::newRow("light-wide")<<false<<420;QTest::newRow("light-narrow")<<false<<200;
        QTest::newRow("dark-wide")<<true<<420;QTest::newRow("dark-narrow")<<true<<200;
    }
    void designRowsAndCancel() {
        QFETCH(bool,dark);QFETCH(int,width);
        ApplicationThemeManager::instance().setMode(dark?ThemeMode::Dark:ThemeMode::Light);
        NavigationWidget widget;widget.resize(width,640);widget.show();widget.setWorkspaceRoot("C:/isolated-design-fixture");
        DesignHierarchyReport report;
        DesignHierarchyNode root;root.id="root";root.projectId="project";root.projectName="Active";
        root.instanceName="top";root.moduleType="very_long_module_type_that_should_elide_before_Active";
        root.isTop=true;root.inSelectedTop=true;root.definitionFile="C:/isolated-design-fixture/top.sv";
        report.nodes<<root;
        DesignHierarchyNode child;child.id="child";child.parentId="root";child.instanceName="u_child";
        child.moduleType="child_module";child.definitionFile=root.definitionFile;report.nodes<<child;
        DesignHierarchyNode unresolved=root;unresolved.id="unresolved";unresolved.projectId="unknown-project";
        unresolved.projectName="Unresolved project";unresolved.inSelectedTop=false;unresolved.unresolved=true;
        unresolved.unresolvedReason="The selected module definition is absent";report.nodes<<unresolved;
        widget.updateDesignHierarchy(report);widget.setActiveTab(NavigationWidget::DesignTab);QTest::qWait(30);
        auto* view=tree(widget);auto* first=view->topLevelItem(0);
        QCOMPARE(first->data(0,NavigationRowStyle::NameRole).toString(),"Active");
        QVERIFY(!first->text(0).contains(" · Active"));QVERIFY(!first->font(0).bold());
        QVERIFY(first->toolTip(0).contains("Active project"));
        QCOMPARE(first->data(0,Qt::AccessibleTextRole).toString(),first->text(0));
        QVERIFY(view->topLevelItem(1)->data(0,NavigationRowStyle::UnresolvedRole).toBool());
        QVERIFY(!view->topLevelItem(1)->text(0).contains("(unresolved)"));
        QSignalSpy activate(&widget,&NavigationWidget::designProjectActivated);
        QSignalSpy expand(&widget,&NavigationWidget::designExpansionChanged);
        const auto rect=view->visualItemRect(first);
        QTest::mouseClick(view->viewport(),Qt::LeftButton,Qt::NoModifier,{rect.left()+60,rect.center().y()});
        QVERIFY(!first->isExpanded());QCOMPARE(expand.size(),1);
        QTest::mouseDClick(view->viewport(),Qt::LeftButton,Qt::NoModifier,{rect.left()+60,rect.center().y()});
        QTest::mouseRelease(view->viewport(),Qt::LeftButton,Qt::NoModifier,{rect.left()+60,rect.center().y()});
        QCOMPARE(expand.size(),1);
        const QPoint action(rect.right()-10,rect.center().y()),body(rect.left()+60,rect.center().y());
        QTest::mousePress(view->viewport(),Qt::LeftButton,Qt::NoModifier,action);
        QTest::mouseRelease(view->viewport(),Qt::LeftButton,Qt::NoModifier,body);
        QCOMPARE(activate.size(),0);QCOMPARE(expand.size(),1);
        QTest::mouseClick(view->viewport(),Qt::LeftButton,Qt::NoModifier,action);
        QCOMPARE(activate.size(),1);
        const auto bad=view->visualItemRect(view->topLevelItem(1));
        QTest::mouseClick(view->viewport(),Qt::LeftButton,Qt::NoModifier,{bad.right()-10,bad.center().y()});
        QCOMPARE(activate.size(),1);
        widget.setSearchFilter(NavigationWidget::DesignTab,"Active");QTest::qWait(10);
        view->setCurrentItem(view->topLevelItem(0));const auto selected=view->currentItem()->data(0,Qt::UserRole);
        const int scroll=view->verticalScrollBar()->value();
        widget.beginDesignProjectSelection();search(widget)->setText("new-query");
        widget.findChild<QAbstractButton*>("cancelDesignSelection")->click();
        QCOMPARE(search(widget)->text(),"Active");QCOMPARE(view->currentItem()->data(0,Qt::UserRole),selected);
        QCOMPARE(view->verticalScrollBar()->value(),scroll);
        widget.beginDesignProjectSelection();QCOMPARE(search(widget)->text(),"new-query");
        widget.findChild<QAbstractButton*>("cancelDesignSelection")->click();
        widget.setSearchFilter(NavigationWidget::DesignTab,"");
        capture(widget,QString("rows-%1-%2").arg(dark?"dark":"light").arg(width));
    }

    void deepRowsAndLargeFont() {
        NavigationWidget widget;widget.resize(200,680);
        widget.setWorkspaceRoot("C:/isolated-design-fixture");
        DesignHierarchyReport report;
        for(int i=0;i<14;++i) {
            DesignHierarchyNode node;node.id=QString::number(i);
            if(i)node.parentId=QString::number(i-1);
            node.instanceName="long_instance_name_at_depth_"+QString::number(i);
            node.moduleType="long_module_type";node.isTop=i==0;node.inSelectedTop=true;
            node.projectId="deep-project";node.projectName="Deep hierarchy";
            node.unresolved=i==13;node.unresolvedReason="Missing module definition";
            report.nodes<<node;
        }
        widget.updateDesignHierarchy(report);widget.setActiveTab(NavigationWidget::DesignTab);widget.show();
        auto* view=tree(widget);
        QFont font=view->font();
        if(font.pixelSize()>0)font.setPixelSize(qCeil(font.pixelSize()*1.5));
        else font.setPointSizeF(font.pointSizeF()*1.5);
        view->setFont(font);view->expandAll();
        auto* leaf=view->topLevelItem(0);while(leaf->childCount())leaf=leaf->child(0);
        view->setCurrentItem(leaf);view->scrollToItem(leaf);QTest::qWait(30);
        const auto rect=view->visualItemRect(leaf);
        QVERIFY(view->viewport()->rect().contains(rect.center()));
        QVERIFY(rect.height()>=QFontMetrics(view->font()).height());
        QVERIFY(leaf->toolTip(0).contains("Missing module definition"));
        for(bool dark:{false,true}) {
            ApplicationThemeManager::instance().setMode(dark?ThemeMode::Dark:ThemeMode::Light);
            capture(widget,dark?"deep-large-dark":"deep-large-light");
        }
        const auto output=qEnvironmentVariable("ZEROSLACK_DESIGN_REPORT");
        if(!output.isEmpty())put(output+"/deep-geometry.json",QJsonDocument(QJsonObject{
            {"depth",13},{"pointSize",view->font().pointSizeF()},{"pixelSize",view->font().pixelSize()},{"rowHeight",rect.height()},
            {"viewportWidth",view->viewport()->width()},{"dpr",widget.devicePixelRatioF()},
            {"leafRect",QJsonArray{rect.x(),rect.y(),rect.width(),rect.height()}}}).toJson());
    }

    void managerSnapshotAndFailure() {
        QTemporaryDir data;QVERIFY(data.isValid());
        const auto one=data.filePath("one.sv"),two=data.filePath("two.sv"),multi=data.filePath("multi.sv"),ambiguous=data.filePath("ambiguous.sv");
        QVERIFY(put(one,"module same; endmodule"));
        QVERIFY(put(two,"module same; endmodule"));
        QVERIFY(put(multi,"module first; endmodule module second; endmodule"));
        QVERIFY(put(ambiguous,"module duplicate; endmodule module duplicate; endmodule"));
        SemanticIndex index;
        index.setSnapshot(std::make_shared<SemanticIndexSnapshot>(SemanticIndexSnapshot::fromSymbolRecords(
            {module("same",one),module("same",two),module("first",multi),module("second",multi,30),
                module("duplicate",ambiguous),module("duplicate",ambiguous,30)}, {}, {},
            {{one,"module same; endmodule"},{two,"module same; endmodule"},{multi,"module first; endmodule module second; endmodule"}})));
        NavigationService service(&index);WorkspaceManager workspace;
        workspace.setRecentWorkspacePersistenceEnabledForTesting(false);
        NavigationWidget widget;widget.resize(380,650);widget.show();
        simdock::Workbench workbench;
        QVERIFY(workbench.setContext(data.path()).isEmpty());workbench.markDesignMigrationCompleted();
        NavigationManager manager;manager.setNavigationService(&service);manager.setNavigationWidget(&widget);
        manager.connectToWorkspaceManager(&workspace);manager.connectToWorkbench(&workbench);
        QVERIFY(workspace.openWorkspace(data.path()));widget.setActiveTab(NavigationWidget::DesignTab);
        QTRY_VERIFY_WITH_TIMEOUT(fileRow(widget,one) && !fileRow(widget,one)->data(0,ModuleRole).toString().isEmpty(),10000);
        QCOMPARE(allFiles(widget)->childCount(),4);
        QVERIFY(fileRow(widget,ambiguous));
        QVERIFY(!(fileRow(widget,ambiguous)->flags()&Qt::ItemIsEnabled));
        QVERIFY(!manager.setDesignTop("duplicate",ambiguous).isEmpty());
        QVERIFY(workbench.designRoots().isEmpty());
        clickRow(candidates(widget),fileRow(widget,one));
        QTRY_COMPARE_WITH_TIMEOUT(workbench.designRoots().size(),1,5000);
        QTRY_VERIFY_WITH_TIMEOUT(tree(widget)->topLevelItemCount()==1 && tree(widget)->isVisible(),5000);
        QVERIFY(tree(widget)->currentItem());
        QCOMPARE(workbench.designRoots().first().file,one);
        widget.findChild<QAbstractButton*>("addDesignProject")->click();
        QTRY_VERIFY_WITH_TIMEOUT(fileRow(widget,one) && !(fileRow(widget,one)->flags()&Qt::ItemIsEnabled),5000);
        QVERIFY(fileRow(widget,two)->flags()&Qt::ItemIsEnabled);
        clickRow(candidates(widget),fileRow(widget,two));
        QTRY_COMPARE_WITH_TIMEOUT(workbench.designRoots().size(),2,5000);
        // Existing right-click entry routes to the same inline selector.
        bool menuSelected=false;
        QTimer::singleShot(50,&widget,[&] {
            auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());
            if(!menu)return;
            for(auto* action:menu->actions())if(action->text().startsWith("Add design project")) {
                menuSelected=true;QTest::mouseClick(menu,Qt::LeftButton,Qt::NoModifier,menu->actionGeometry(action).center());return;
            }
            menu->close();
        });
        widget.designNodeContextMenuRequested({},widget.mapToGlobal(QPoint(20,150)));
        QVERIFY(menuSelected);QVERIFY(candidates(widget)->isVisible());
        widget.findChild<QAbstractButton*>("cancelDesignSelection")->click();
        widget.setSearchFilter(NavigationWidget::DesignTab,"hide-every-root");
        QVERIFY(chooseFileMenu(widget,multi,"second",true));
        QTRY_COMPARE_WITH_TIMEOUT(workbench.designRoots().size(),3,5000);
        QTRY_VERIFY_WITH_TIMEOUT(tree(widget)->currentItem() && tree(widget)->currentItem()->text(0).contains("second"),5000);
        QCOMPARE(widget.searchFilter(NavigationWidget::DesignTab),QString());
        widget.findChild<QAbstractButton*>("addDesignProject")->click();
        // A displayed candidate becoming stale fails in place without creating a project.
        search(widget)->setText("first");
        index.setSnapshot(std::make_shared<SemanticIndexSnapshot>());
        auto* row=fileRow(widget,multi);QVERIFY(row);row->setExpanded(true);
        clickRow(candidates(widget),row->child(0));
        QCOMPARE(workbench.designRoots().size(),3);
        QVERIFY(widget.findChild<QLabel*>("designSelectionError")->isVisible());
        QCOMPARE(search(widget)->text(),"first");
        // Changing workspace while a derivation is queued cannot publish the old list.
        QTemporaryDir other;QVERIFY(put(other.filePath("new.sv"),"module new_top; endmodule"));
        manager.refreshDesignHierarchy(true);
        QVERIFY(workbench.setContext(other.path()).isEmpty());workbench.markDesignMigrationCompleted();
        QVERIFY(workspace.openWorkspace(other.path()));
        QTRY_VERIFY_WITH_TIMEOUT(allFiles(widget) && allFiles(widget)->childCount()==1,10000);
        QCOMPARE(allFiles(widget)->child(0)->data(0,FileRole).toString(),other.filePath("new.sv"));
        QVERIFY(!fileRow(widget,one));
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

    void realMainWindowSuffixesAndPersistence() {
        ApplicationThemeManager::instance().setMode(ThemeMode::Light);
        QTemporaryDir data;QVERIFY(data.isValid());const auto workspace=data.filePath("workspace");
        QStringList files,names;const auto suffixes=WorkspaceConfigurationService::defaultFileExtensions();
        for(int i=0;i<suffixes.size();++i) {
            const auto name=QString("suffix_module_%1").arg(i);names<<name;
            const auto file=workspace+"/source"+suffixes[i];files<<file;
            QVERIFY(put(file,("module "+name+"; endmodule").toUtf8()));
        }
        qputenv("ZEROSLACK_SESSION_STORAGE_PATH",data.filePath("session.ini").toUtf8());
        qputenv("ZEROSLACK_EDITING_TIME_STORAGE_PATH",data.filePath("editing.json").toUtf8());
        MainWindow window;
        window.tabManager->setCrashRecoveryService(std::make_unique<CrashRecoveryService>(data.filePath("recovery")));
        window.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
        window.resize(1280,800);window.show();
        QVERIFY(window.workspaceManager->openWorkspace(workspace));
        auto* nav=window.findChild<NavigationWidget*>();QVERIFY(nav);
        auto* workbench=window.findChild<simdock::Workbench*>();QVERIFY(workbench);
        nav->setActiveTab(NavigationWidget::DesignTab);
        QTRY_COMPARE_WITH_TIMEOUT(window.workspaceManager->getSystemVerilogFiles().size(),7,15000);
        for(int i=0;i<files.size();++i) {
            QTRY_VERIFY_WITH_TIMEOUT(fileRow(*nav,files[i]) && fileRow(*nav,files[i])->data(0,ModuleRole).toString()==names[i],20000);
        }
        for(const auto& file:files)QVERIFY(window.tabManager->openFileInTab(file));
        QCOMPARE(window.tabManager->getOpenSystemVerilogFiles().size(),7);
        QCOMPARE(window.tabManager->workspaceSessionTabs(workspace).size(),7);
        capture(window,"mainwindow-empty-projects-selector");
        QSignalSpy scans(window.workspaceManager.get(),&WorkspaceManager::filesScanned);
        QSignalSpy refresh(window.navigationManager.get(),&NavigationManager::dataRefreshed);
        QSignalSpy filters(nav,&NavigationWidget::searchFilterChanged);
        search(*nav)->setText("suffix_module_6");search(*nav)->clear();
        QCOMPARE(scans.size(),0);QCOMPARE(refresh.size(),0);
        QCOMPARE(filters.size(),0);
        QSignalSpy choices(nav,&NavigationWidget::designModuleChosen);
        for(int i=0;i<files.size();++i) {
            if(i)nav->findChild<QAbstractButton*>("addDesignProject")->click();
            QTRY_VERIFY(candidates(*nav)->isVisible() && fileRow(*nav,files[i])
                && fileRow(*nav,files[i])->data(0,ModuleRole).toString()==names[i]
                && (fileRow(*nav,files[i])->flags()&Qt::ItemIsEnabled));
            if(i==0)QVERIFY(chooseFileMenu(*nav,files[i],names[i],false));
            else clickRow(candidates(*nav),fileRow(*nav,files[i]));
            QTest::qWait(100);
            if(workbench->designRoots().size()!=i+1) {
                capture(window,QString("add-failure-%1").arg(i));
                qWarning()<<"Choice count"<<choices.size()<<"last"<<(choices.isEmpty()?QList<QVariant>():choices.last())
                    <<"inline error"<<nav->findChild<QLabel*>("designSelectionError")->text();
            }
            QTRY_COMPARE_WITH_TIMEOUT(workbench->designRoots().size(),i+1,10000);
            QTRY_VERIFY_WITH_TIMEOUT(tree(*nav)->isVisible() && tree(*nav)->currentItem(),10000);
        }
        QCOMPARE(workbench->designRoots().size(),7);
        QTRY_COMPARE_WITH_TIMEOUT(tree(*nav)->topLevelItemCount(),7,10000);
        int active=0;
        for(int i=0;i<7;++i)active+=tree(*nav)->topLevelItem(i)->data(0,NavigationRowStyle::ActiveRole).toBool();
        QCOMPARE(active,1);
        QJsonArray buildInputs;
        auto* sourceList=window.findChild<QAbstractItemView*>("sourceList");
        QVERIFY(sourceList);
        for(int row=0;row<sourceList->model()->rowCount();++row)
            buildInputs.append(sourceList->model()->index(row,0).data(Qt::UserRole).toString());
        const auto reportDirectory=qEnvironmentVariable("ZEROSLACK_DESIGN_REPORT");
        if(!reportDirectory.isEmpty())put(reportDirectory+"/suffix-capabilities.json",QJsonDocument(QJsonObject{
            {"configuredSuffixes",QJsonArray::fromStringList(suffixes)},{"discoveredFiles",QJsonArray::fromStringList(files)},
            {"parsedModules",QJsonArray::fromStringList(names)},{"openHdlDocuments",7},{"sessionDocuments",7},
            {"savedDesignProjects",7},{"buildInputsDiscoveredRows",buildInputs}}).toJson());
        capture(window,"mainwindow-projects-light");
        ApplicationThemeManager::instance().setMode(ThemeMode::Dark);window.resize(980,720);
        capture(window,"mainwindow-projects-narrow-dark");
        const auto roots=workbench->designRoots();
        workbench->setDesignExpanded(roots.first().projectId,false);
        const auto other=data.filePath("other");QDir().mkpath(other);
        QVERIFY(window.workspaceManager->openWorkspace(other));
        QVERIFY(window.workspaceManager->openWorkspace(workspace));
        QTRY_COMPARE_WITH_TIMEOUT(workbench->designRoots().size(),7,10000);
        QCOMPARE(workbench->designRoots().first().expanded,false);
        // Each configured suffix also keeps files without a root and invalid content visible.
        for(int i=0;i<suffixes.size();++i) {
            QVERIFY(put(workspace+QString("/empty_%1").arg(i)+suffixes[i],"// no module definition"));
            QVERIFY(put(workspace+QString("/invalid_%1").arg(i)+suffixes[i],"this is not valid SystemVerilog ???"));
        }
        window.workspaceManager->refreshWorkspaceFiles();
        nav->beginDesignProjectSelection();
        QTRY_VERIFY_WITH_TIMEOUT(allFiles(*nav) && allFiles(*nav)->childCount()==21,20000);
        QJsonArray fileStates;
        for(int i=0;i<suffixes.size();++i) {
            QTRY_VERIFY_WITH_TIMEOUT(fileRow(*nav,workspace+QString("/empty_%1").arg(i)+suffixes[i])
                && fileRow(*nav,workspace+QString("/empty_%1").arg(i)+suffixes[i])->toolTip(0).contains("No module definitions"),20000);
            auto* empty=fileRow(*nav,workspace+QString("/empty_%1").arg(i)+suffixes[i]);QVERIFY(empty);
            QVERIFY(empty->data(0,ModuleRole).toString().isEmpty());
            auto* invalid=fileRow(*nav,workspace+QString("/invalid_%1").arg(i)+suffixes[i]);QVERIFY(invalid);
            QVERIFY(invalid->data(0,ModuleRole).toString().isEmpty());
            fileStates.append(QJsonObject{{"suffix",suffixes[i]},{"nonmoduleStatus",empty->text(0)},{"invalidStatus",invalid->text(0)}});
        }
        if(!reportDirectory.isEmpty())put(reportDirectory+"/nonmodule-capabilities.json",QJsonDocument(fileStates).toJson());
        capture(window,"mainwindow-nonmodule-files");
        for(const auto& root:workbench->designRoots())QVERIFY(workbench->removeDesignRoot(root.projectId).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(workbench->designRoots().isEmpty() && candidates(*nav)->isVisible(),10000);
        window.close();
    }
};

int main(int argc,char** argv) {
    QStandardPaths::setTestModeEnabled(true);QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
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
