#include <QtTest>
#include "mainwindow.h"
#include "settingscenterpanel.h"
#include "settingsdialog.h"
#include "workspacemanager.h"
#include "workspacesessioncoordinator.h"
#include "workspacesessionstateservice.h"
#include "tabmanager.h"
#include "mycodeeditor.h"
#include "applicationthememanager.h"
#include "usertemplateservice.h"
#include "crashrecoveryservice.h"
#include "unsaveddocumentmanager.h"
#include "testuistyle.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QLabel>
#include <QListWidget>
#include <QBoxLayout>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTableView>
#include <QTextBlock>
#include <QToolBar>
#include <QToolButton>

namespace {
bool writeFile(const QString& path,const QByteArray& bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath()); QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes)==bytes.size();
}
void record(QWidget* widget,const QString& name) {
    const QString root=qEnvironmentVariable("ZEROSLACK_SETTINGS_EVIDENCE");
    if(root.isEmpty())return;
    QDir().mkpath(root);widget->grab().save(root+"/"+name+".png");
    const auto rect=[](QRect r){return QJsonArray{r.x(),r.y(),r.width(),r.height()};};
    QJsonObject state{{"geometry",rect(widget->geometry())},{"frameGeometry",rect(widget->frameGeometry())},
        {"dpr",widget->devicePixelRatioF()},{"fontPt",widget->font().pointSizeF()},
        {"platform",QGuiApplication::platformName()},{"available",rect(widget->screen()->availableGeometry())}};
    if(auto* panel=widget->findChild<SettingsCenterPanel*>()) {
        auto* categories=panel->findChild<QListWidget*>("settingsCenterCategoryList");
        state.insert("panelSize",QJsonArray{panel->width(),panel->height()});
        state.insert("layoutDirection",int(qobject_cast<QBoxLayout*>(panel->layout())->direction()));
        state.insert("categoryFont",categories->font().toString());
    }
    writeFile(root+"/"+name+".json",QJsonDocument(state).toJson());
}
void isolateMainWindow(MainWindow& window,const QString& root) {
    window.tabManager->setCrashRecoveryService(std::make_unique<CrashRecoveryService>(root+"/recovery"));
    window.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
    window.resize(1200,800);window.show();
}
bool closeWithDecision(SettingsDialog* host,bool discard,const std::function<void()>& action,bool reentry=false) {
    QTimer timer;timer.setSingleShot(true);bool seen=false;
    QObject::connect(&timer,&QTimer::timeout,host,[&]{
        auto* message=host->findChild<UiMessageDialog*>("settingsDiscardConfirmation");
        if(!message)return;
        auto* keep=message->findChild<QPushButton*>("settingsKeepEditing");
        auto* button=message->findChild<QPushButton*>(discard?"settingsDiscardUnapplied":"settingsKeepEditing");
        seen=button && keep && message->defaultButton()==keep && message->escapeButton()==keep;
        if(reentry) {
            host->close();host->showSettings();
            seen=seen && host->isVisible();
            seen=seen && host->findChildren<UiMessageDialog*>().size()==1;
        }
        if(button)button->click();else message->reject();
    });
    timer.start(0);action();return seen;
}
QCheckBox* makeAnalysisDraft(SettingsCenterPanel* panel) {
    panel->selectCategory("analysis");
    auto* enable=qobject_cast<QCheckBox*>(panel->fieldEditor("analysis.enabled"));
    auto* overrideCheck=panel->findChild<QCheckBox*>(SettingsCenterPanel::fieldOverrideObjectName("analysis.enabled"));
    if(overrideCheck)overrideCheck->setChecked(true);
    if(enable)enable->setChecked(!enable->isChecked());
    return enable;
}
}
class SettingsDialogTest : public QObject {
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> temporary;
private slots:
    void init() {
        temporary=std::make_unique<QTemporaryDir>();QVERIFY(temporary->isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temporary->path());
        QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,temporary->path());
        qputenv("ZEROSLACK_SESSION_STORAGE_PATH",temporary->filePath("sessions.ini").toUtf8());
        qputenv("ZEROSLACK_EDITING_TIME_STORAGE_PATH",temporary->filePath("editing.json").toUtf8());
        qputenv("ZEROSLACK_SIMDOCK_LEGACY_SETTINGS_PATH",temporary->filePath("legacy.ini").toUtf8());
        UserTemplateService::getInstance()->setGlobalTemplateFilePath(temporary->filePath("templates.json"));
        ApplicationThemeManager::instance().setMode(ThemeMode::Light);
    }
    void entriesAndIdentity() {
        MainWindow window;isolateMainWindow(window,temporary->path());
        QVERIFY(!window.findChild<SettingsDialog*>());
        auto* gear=window.findChild<QToolButton*>("settingsRailButton");QVERIFY(gear);gear->click();
        auto* host=window.findChild<SettingsDialog*>();QVERIFY(host);
        auto* panel=window.findChild<SettingsCenterPanel*>();QVERIFY(panel);
        QTRY_VERIFY(host->isVisible());QCOMPARE(panel->window(),host);QVERIFY(!host->isModal());
        QVERIFY(!(host->windowFlags() & Qt::WindowStaysOnTopHint));
        QVERIFY(!(host->windowFlags() & Qt::WindowMinMaxButtonsHint));
        QVERIFY(!window.tabManager->toolPage("settingsCenter"));
        QVERIFY(!window.findChild<QDockWidget*>("settingsContentHost"));
        auto* view=window.findChild<QAction*>("viewSettingsCenterAction");QVERIFY(view);
        panel->selectCategory("analysis");
        for(int i=0;i<3;++i)view->trigger();
        QCOMPARE(window.findChildren<SettingsDialog*>().size(),1);
        QCOMPARE(window.findChild<SettingsCenterPanel*>(),panel);
        QCOMPARE(panel->currentCategoryId(),QString("analysis"));QVERIFY(host->isVisible());
        gear->click();QVERIFY(!host->isVisible());view->trigger();QVERIFY(host->isVisible());
        QCOMPARE(window.findChild<SettingsDialog*>(),host);
        auto* editor=makeAnalysisDraft(panel);QVERIFY(editor);
        const bool value=editor->isChecked();view->trigger();QCOMPARE(editor->isChecked(),value);
        QVERIFY(panel->isScopeDirty(SettingsCenterScope::Global));
        for(auto* button:host->findChildren<QPushButton*>()) {
            QVERIFY(!button->autoDefault());QVERIFY(!button->isDefault());
        }
        QTest::keyClick(editor,Qt::Key_Return);
        QVERIFY(host->isVisible() && panel->isScopeDirty(SettingsCenterScope::Global));
        record(host,"entries");
        QVERIFY(closeWithDecision(host,true,[&]{host->close();},true));
        QVERIFY(!host->isVisible());QVERIFY(panel->unappliedScopes().isEmpty());QVERIFY(window.close());
    }
    void closePaths_data() {
        QTest::addColumn<int>("path");QTest::newRow("gear")<<0;QTest::newRow("close-button")<<1;
        QTest::newRow("window-close")<<2;QTest::newRow("dialog-escape")<<3;
    }
    void closePaths() {
        QFETCH(int,path);MainWindow window;isolateMainWindow(window,temporary->path());
        auto* gear=window.findChild<QToolButton*>("settingsRailButton");QVERIFY(gear);gear->click();
        auto* host=window.findChild<SettingsDialog*>();QVERIFY(host);
        auto* panel=host->findChild<SettingsCenterPanel*>();QVERIFY(panel);QVERIFY(makeAnalysisDraft(panel));
        const auto draft=panel->draftValues(SettingsCenterScope::Global);
        const auto request=[&]{
            if(path==0)gear->click();
            else if(path==1)host->findChild<QPushButton*>("settingsDialogCloseButton")->click();
            else if(path==2)host->close();
            else QTest::keyClick(host,Qt::Key_Escape);
        };
        QVERIFY(closeWithDecision(host,false,request));QVERIFY(host->isVisible());
        QCOMPARE(panel->draftValues(SettingsCenterScope::Global),draft);
        QCOMPARE(panel->currentCategoryId(),QString("analysis"));
        QVERIFY(closeWithDecision(host,true,request));QVERIFY(!host->isVisible());
        QVERIFY(panel->unappliedScopes().isEmpty());QVERIFY(window.close());
    }
    void pendingDelegateAndInvalidInput() {
        SettingsCenterService service;
        QVERIFY(service.saveGlobal({{"shortcut.overrides",QVariantMap{{"file.save","Ctrl+S"}}}}).saved);
        MainWindow window;isolateMainWindow(window,temporary->path());
        window.findChild<QToolButton*>("settingsRailButton")->click();
        auto* host=window.findChild<SettingsDialog*>();QVERIFY(host);
        auto* panel=host->findChild<SettingsCenterPanel*>();panel->selectCategory("shortcut");
        auto* table=panel->findChild<QTableView*>("settingsCenterStringMapView.shortcut.overrides");QVERIFY(table);
        host->activateWindow();
        QApplication::setActiveWindow(host);
        QTRY_VERIFY(host->isActiveWindow());
        table->setFocus();table->edit(table->model()->index(0,1));
        QTRY_VERIFY(table->findChild<QLineEdit*>());
        auto* editor=table->findChild<QLineEdit*>();editor->setFocus();
        QTRY_VERIFY(editor->hasFocus());
        editor->setText("Ctrl+Alt+S");
        QCOMPARE(table->model()->index(0,1).data().toString(),QString("Ctrl+S"));
        QVERIFY(closeWithDecision(host,false,[&]{host->close();}));
        QCOMPARE(panel->draftValues(SettingsCenterScope::Global).value("shortcut.overrides").toMap().value("file.save").toString(),QString("Ctrl+Alt+S"));
        panel->revertCurrentScope();
        auto* add=panel->findChild<QPushButton*>("settingsCenterStringMapAdd.shortcut.overrides");QVERIFY(add);add->click();
        QVERIFY(closeWithDecision(host,false,[&]{host->close();}));QVERIFY(!panel->unappliedScopes().isEmpty());
        panel->applyCurrentScope();QVERIFY(!panel->currentIssues().isEmpty());record(host,"invalid-shortcut");
        QVERIFY(closeWithDecision(host,true,[&]{host->close();}));
        QCOMPARE(service.load().globalValues.value("shortcut.overrides").toMap().value("file.save").toString(),QString("Ctrl+S"));
        QVERIFY(window.close());
    }
    void hostCloseCancellation() {
        const QString file=temporary->filePath("workspace/exit.sv");
        QVERIFY(writeFile(file,"module exit_fixture; endmodule\n"));
        MainWindow window;isolateMainWindow(window,temporary->path());QVERIFY(window.tabManager->openFileInTab(file));
        auto* editor=window.tabManager->getCurrentEditor();QVERIFY(editor);editor->insertPlainText("// unsaved\n");
        int documentPrompts=0;
        window.tabManager->unsavedDocumentManagerForTesting()->setDecisionProvider(
            [&](const auto&,QWidget*){++documentPrompts;return UnsavedDocumentBatchDecision::Cancel;});
        window.findChild<QToolButton*>("settingsRailButton")->click();
        auto* host=window.findChild<SettingsDialog*>();QVERIFY(host);
        auto* panel=host->findChild<SettingsCenterPanel*>();QVERIFY(makeAnalysisDraft(panel));
        const auto draft=panel->draftValues(SettingsCenterScope::Global);bool closed=true;
        QVERIFY(closeWithDecision(host,true,[&]{closed=window.close();}));
        QVERIFY(!closed && window.isVisible() && host->isVisible());QCOMPARE(documentPrompts,1);
        QCOMPARE(panel->draftValues(SettingsCenterScope::Global),draft);
        QVERIFY(closeWithDecision(host,false,[&]{closed=window.close();}));QVERIFY(!closed);QCOMPARE(documentPrompts,1);
        editor->document()->setModified(false);
        QVERIFY(closeWithDecision(host,true,[&]{closed=window.close();}));
        QVERIFY(closed);QVERIFY(panel->unappliedScopes().isEmpty());
    }
    void workspaceDraftsAndImmediatePersistence() {
        const QString a=temporary->filePath("a"), b=temporary->filePath("b");
        QDir().mkpath(a);QDir().mkpath(b);SettingsCenterService service;
        QVERIFY(service.saveGlobal({{"font.sizePt",14}}).saved);
        QVERIFY(service.saveWorkspace(a,{{"font.sizePt",20}}).saved);
        QVERIFY(service.saveWorkspace(b,{{"font.sizePt",22}}).saved);
        {
            MainWindow window;isolateMainWindow(window,temporary->path());
            auto* sessions=window.findChild<WorkspaceSessionCoordinator*>();QVERIFY(sessions);
            QVERIFY(sessions->openWorkspace(a));window.findChild<QToolButton*>("settingsRailButton")->click();
            auto* host=window.findChild<SettingsDialog*>();QVERIFY(host);
            auto* panel=host->findChild<SettingsCenterPanel*>();QVERIFY(panel);
            auto* font=qobject_cast<QSpinBox*>(panel->fieldEditor("font.sizePt"));QVERIFY(font);
            panel->setScope(SettingsCenterScope::Workspace);font->setValue(21);
            QVERIFY(sessions->openWorkspace(b));QCOMPARE(panel->workspaceRoot(),b);font->setValue(23);
            panel->setScope(SettingsCenterScope::Global);font->setValue(18);
            auto* theme=qobject_cast<QComboBox*>(panel->fieldEditor("appearance.theme"));QVERIFY(theme);
            theme->setCurrentText("Dark");
            QCOMPARE(service.load(a).globalValues.value("appearance.theme").toString(),QString("Dark"));
            QVERIFY(sessions->closeWorkspace(1));QVERIFY(sessions->closeWorkspace(0));
            QCOMPARE(panel->unappliedScopes().size(),3);
            const auto globalDraft=panel->draftValues(SettingsCenterScope::Global);
            window.findChild<QAction*>("viewSettingsCenterAction")->trigger();
            QCOMPARE(panel->draftValues(SettingsCenterScope::Global),globalDraft);
            QVERIFY(closeWithDecision(host,false,[&]{host->close();}));
            QCOMPARE(panel->unappliedScopes().size(),3);
            QVERIFY(closeWithDecision(host,true,[&]{host->close();}));QVERIFY(window.close());
        }
        QCOMPARE(service.load(a).globalValues.value("font.sizePt").toInt(),14);
        QCOMPARE(service.load(a).workspaceValues.value("font.sizePt").toInt(),20);
        QCOMPARE(service.load(b).workspaceValues.value("font.sizePt").toInt(),22);
        MainWindow reopened;isolateMainWindow(reopened,temporary->path());
        reopened.findChild<QToolButton*>("settingsRailButton")->click();
        auto* panel=reopened.findChild<SettingsCenterPanel*>();QVERIFY(panel);
        QCOMPARE(panel->effectiveValue("appearance.theme").toString(),QString("Dark"));
        QVERIFY(panel->unappliedScopes().isEmpty());QVERIFY(reopened.close());
    }
    void geometryRules() {
        using namespace SettingsDialogGeometry;
        const QList<Screen> screens{{"left","a",{-1920,-200,1920,1080}},{"main","b",{0,0,1280,720}},{"main","c",{1280,0,1280,720}}};
        QCOMPARE(selectScreen(screens,"left","a",1),0);QCOMPARE(selectScreen(screens,"removed","x",1),1);
        QCOMPARE(selectScreen(screens,"main","",1),1);QCOMPARE(selectScreen(screens,"main","c",1),2);
        QCOMPARE(selectScreen(screens,"missing","",-1),0);QCOMPARE(selectScreen({},"","",-1),-1);
        const QMargins frame(8,32,8,8);
        for(const QRect available:{QRect(-1920,-200,1920,1080),QRect(0,0,640,360)})
            for(const QRect saved:{QRect(),QRect(9999,9999,6000,4000),QRect(-4000,-900,500,400),QRect(1,1,-1,-1)}) {
                const auto fitted=constrain(saved,available,frame,{900,600});
                QVERIFY(fitted.isValid());QVERIFY(available.contains(fitted.marginsAdded(frame)));
            }
    }
    void legacyRestore_data() {
        QTest::addColumn<QString>("sample");
        QTest::newRow("old-settings-dock")<<QString("with-settings.ini");QTest::newRow("missing-layout")<<QString("without-layout.ini");
    }
    void legacyRestore() {
        QFETCH(QString,sample);
        const QString source=QStringLiteral(QT_TESTCASE_SOURCEDIR "/build/coordination/20261025-settings-dialog/implementation/legacy-fixture/");
        if(!QFileInfo::exists(source+sample))QSKIP("Generated old-host fixture is unavailable");
        QFile metadata(source+"legacy-metadata.json");QVERIFY(metadata.open(QIODevice::ReadOnly));
        const auto old=QJsonDocument::fromJson(metadata.readAll()).object();const QString workspace=old["workspace"].toString();
        const QString session=temporary->filePath("sessions.ini");QVERIFY(QFile::copy(source+sample,session));
        const auto saved=WorkspaceSessionStateService(session).load(workspace);
        QVERIFY(saved.loaded);QCOMPARE(saved.state.tabs.size(),1);
        QCOMPARE(WorkspaceSessionStateService::workspaceIdentity(workspace),old["workspaceIdentity"].toString());
        MainWindow window;
        auto* auxiliary=new QDockWidget("Migration auxiliary",&window);auxiliary->setObjectName("settingsMigrationAuxDock");
        auxiliary->setWidget(new QLabel("Auxiliary state",auxiliary));window.addDockWidget(Qt::RightDockWidgetArea,auxiliary);
        auto* toolbar=new QToolBar("Migration toolbar",&window);toolbar->setObjectName("settingsMigrationToolbar");
        toolbar->addAction("Fixture");window.addToolBar(Qt::BottomToolBarArea,toolbar);
        isolateMainWindow(window,temporary->path());
        auto* sessions=window.findChild<WorkspaceSessionCoordinator*>();QVERIFY(sessions);sessions->setRememberPanelState(true);
        QVERIFY(sessions->openWorkspace(workspace));QTRY_VERIFY(window.tabManager->getCurrentEditor());
        auto* editor=window.tabManager->getCurrentEditor();
        QCOMPARE(editor->textCursor().blockNumber()+1,31);QCOMPARE(editor->textCursor().positionInBlock()+1,6);
        QCOMPARE(window.tabManager->workspaceSessionTabs(workspace).size(),1);
        QVERIFY(!window.tabManager->toolPage("settingsCenter"));QVERIFY(!window.findChild<QDockWidget*>("settingsContentHost"));
        QVERIFY(!window.findChild<SettingsDialog*>());
        if(sample=="with-settings.ini") {
            QVERIFY(window.restoreState(saved.state.ui.mainWindowState));
            QCOMPARE(int(window.dockWidgetArea(auxiliary)),old["auxiliaryArea"].toInt());
            QCOMPARE(int(window.toolBarArea(toolbar)),old["toolbarArea"].toInt());
        } else QVERIFY(saved.state.ui.mainWindowState.isEmpty());
        record(&window,"restored-"+sample);QVERIFY(window.close());
    }
    void placementRestore() {
        QRect saved;
        {
            MainWindow window;isolateMainWindow(window,temporary->path());
            window.findChild<QToolButton*>("settingsRailButton")->click();
            auto* host=window.findChild<SettingsDialog*>();QVERIFY(host);
            host->setGeometry(120,100,600,420);QTest::qWait(50);saved=host->geometry();
            QVERIFY(host->close());QVERIFY(window.close());
        }
        {
            MainWindow window;isolateMainWindow(window,temporary->path());
            window.findChild<QToolButton*>("settingsRailButton")->click();
            auto* host=window.findChild<SettingsDialog*>();QVERIFY(host);
            QTRY_COMPARE(host->geometry(),saved);QVERIFY(host->close());QVERIFY(window.close());
        }
        QSettings settings(QSettings::defaultFormat(),QSettings::UserScope,"ZeroSlack","ZeroSlack");
        settings.setValue("ui/settingsDialog/geometry",QRect(-9000,-5000,9999,9999));
        settings.setValue("ui/settingsDialog/screenName","removed-screen");
        settings.setValue("ui/settingsDialog/screenSerialNumber","missing");settings.sync();
        MainWindow window;isolateMainWindow(window,temporary->path());
        window.findChild<QToolButton*>("settingsRailButton")->click();
        auto* host=window.findChild<SettingsDialog*>();QVERIFY(host);QTest::qWait(50);
        QVERIFY(QGuiApplication::primaryScreen()->availableGeometry().contains(host->frameGeometry()));
        QVERIFY(host->close());QVERIFY(window.close());
    }
    void layout_data() {
        QTest::addColumn<bool>("dark");QTest::addColumn<int>("width");QTest::addColumn<int>("fontPt");
        for(bool dark:{false,true})for(int width:{1000,460})for(int font:{10,16})
            QTest::newRow(qPrintable(QString("%1-%2-%3").arg(dark?"dark":"light").arg(width).arg(font)))<<dark<<width<<font;
    }
    void layout() {
        QFETCH(bool,dark);QFETCH(int,width);QFETCH(int,fontPt);
        MainWindow window;isolateMainWindow(window,temporary->path());
        ApplicationThemeManager::instance().setMode(dark?ThemeMode::Dark:ThemeMode::Light);
        window.findChild<QToolButton*>("settingsRailButton")->click();
        auto* host=window.findChild<SettingsDialog*>();QVERIFY(host);
        auto* panel=host->findChild<SettingsCenterPanel*>();QVERIFY(panel);
        QFont font=host->font();font.setPointSize(fontPt);host->setFont(font);
        for(auto* control:panel->findChildren<QWidget*>()) {
            QFont large=control->font();large.setPointSize(fontPt);control->setFont(large);
        }
        panel->selectCategory("analysis");
        host->resize(width,640);QTest::qWait(100);
        host->hide();host->showSettings();QTest::qWait(100);
        QVERIFY(host->screen()->availableGeometry().contains(host->frameGeometry()));
        auto* close=host->findChild<QPushButton*>("settingsDialogCloseButton");QVERIFY(close);QVERIFY(close->isVisible());
        QVERIFY(host->rect().contains(QRect(close->mapTo(host,QPoint()),close->size())));
        auto* outer=host->findChild<QScrollArea*>("settingsDialogScroll");QVERIFY(outer);
        auto* apply=panel->findChild<QPushButton*>("settingsCenterApplyButton");QVERIFY(apply);
        outer->ensureWidgetVisible(apply);QTest::qWait(30);
        QVERIFY(outer->viewport()->rect().intersects(QRect(apply->mapTo(outer->viewport(),QPoint()),apply->size())));
        const QString name=QString("layout-%1-%2-%3").arg(dark?"dark":"light").arg(width).arg(fontPt);
        record(host,name+"-actions");
        outer->verticalScrollBar()->setValue(0);outer->horizontalScrollBar()->setValue(0);
        QTest::qWait(30);record(host,name);
        const QRect placement=host->geometry();QVERIFY(host->close());
        QSettings settings(QSettings::defaultFormat(),QSettings::UserScope,"ZeroSlack","ZeroSlack");
        QCOMPARE(settings.value("ui/settingsDialog/geometry").toRect(),placement);QVERIFY(window.close());
    }
};
int main(int argc,char** argv) {
    QStandardPaths::setTestModeEnabled(true);QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
    QCoreApplication::setOrganizationName("ZeroSlackSettingsDialogTest");QCoreApplication::setApplicationName("ZeroSlackSettingsDialogTest");
    if(!initializeUiStyleForTest())return 2;ApplicationThemeManager::instance().setAnimationsEnabled(false);
    SettingsDialogTest test;return QTest::qExec(&test,argc,argv);
}
#include "settings_dialog_test.moc"

