#include "testuistyle.h"
#include "workspacemotion.h"
#include "mycodeeditor.h"
#include "workspacesessioncoordinator.h"
#include "applicationthememanager.h"
#include "mainwindow.h"
#include "workspacemanager.h"
#include "tabmanager.h"
#include "editorsplitcontroller.h"
#include <QtWidgets>
#include <QtTest>

class WelcomeMainWindowTest : public QObject {
    Q_OBJECT
    void capture(MainWindow& window, const QString& name) {
        const auto directory=qEnvironmentVariable("ZEROSLACK_UI_REVIEW_DIR");
        if (!directory.isEmpty()) { QVERIFY(QDir().mkpath(directory)); QVERIFY(window.grab().save(directory+'/'+name+".png")); }
    }
private slots:
    void welcomeLifecycleAndEntryWorkflow() {
        QTemporaryDir profile; QVERIFY(profile.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,profile.path());
        QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,profile.path());
        qputenv("ZEROSLACK_PROFILE_ROOT",profile.path().toUtf8());
        qputenv("ZEROSLACK_SESSION_STORAGE_PATH",profile.filePath("sessions.ini").toUtf8());
        qputenv("ZEROSLACK_EDITING_TIME_STORAGE_PATH",profile.filePath("editing-time.json").toUtf8());
        qputenv("ZEROSLACK_SIMULATION_CACHE_ROOT",profile.filePath("simulation").toUtf8());
        qputenv("XIPS_LIBRARY",profile.filePath("library").toUtf8());
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        auto& theme=ApplicationThemeManager::instance();QVERIFY(initializeUiStyleForTest());
        theme.setMode(ThemeMode::Light);theme.setAnimationsEnabled(true);theme.applyToApplication();
        auto window=std::make_unique<MainWindow>();window->resize(1400,900);window->show();window->activateWindow();
        window->workspaceManager->setWorkspaceAliasSelector([](QWidget*,const QString& suggested) { return suggested; });
        qInfo() << "MainWindow devicePixelRatio:" << window->devicePixelRatioF() << "logical size:" << window->size();
        auto* welcome=window->findChild<QScrollArea*>("welcomePage");QVERIFY(welcome);QTRY_VERIFY(welcome->isVisible());
        auto* tabs=window->findChild<TabManager*>();QVERIFY(tabs);
        tabs->createNewTab();QTRY_VERIFY(!welcome->isVisible());
        auto* registry=tabs->editorSplitController();auto* page=registry->activeGroup()->currentWidget();QVERIFY(page);
        QVERIFY(registry->movePage(page,registry->regionGroup("left")));
        QVERIFY(!registry->regionHasPages("main"));QVERIFY(!welcome->isVisible());
        QVERIFY(tabs->closeAllTabs());QTRY_VERIFY(welcome->isVisible());
        QVERIFY(!window->findChild<QWidget*>("welcomeCard0"));
        QVERIFY(!window->findChild<QWidget*>("welcomeCard1"));
        QVERIFY(!window->findChild<QWidget*>("welcomeCard2"));
        QVERIFY(!window->findChild<QWidget*>("welcomeOpenProjectButton"));
        QVERIFY(window->findChild<QLabel*>("welcomeRecentTitle")->isVisible());
        QVERIFY(window->findChild<QLabel*>("welcomeRecentEmpty")->isVisible());
        QTest::qWait(100);capture(*window,"welcome-empty-light");
        theme.setMode(ThemeMode::Dark);QTest::qWait(100);capture(*window,"welcome-empty-dark");
        theme.setMode(ThemeMode::Light);window->resize(700,900);QTest::qWait(100);
        QCOMPARE(welcome->horizontalScrollBar()->maximum(),0);capture(*window,"welcome-narrow");
        window->resize(1400,900);QTest::qWait(100);
        const auto workspace=profile.filePath("Workspace 中文");QVERIFY(QDir().mkpath(workspace));
        QFile source(workspace+"/top.sv");QVERIFY(source.open(QIODevice::WriteOnly));source.write("module top; endmodule\n");source.close();
        QAction* openAction=nullptr;
        for(auto* action:window->findChildren<QAction*>())
            if(action->property("executionRoute").toString()=="ui.workspace.open") {openAction=action;break;}
        QVERIFY(openAction);
        bool picker=false;
        QTimer::singleShot(100,[&] {
            for(auto* top:QApplication::topLevelWidgets())if(auto* dialog=qobject_cast<QFileDialog*>(top)) {
                picker = true;
                dialog->setDirectory(QFileInfo(workspace).absolutePath());
                dialog->selectFile(QFileInfo(workspace).fileName());
                QTimer::singleShot(50, dialog, [dialog, workspace] {
                    auto* name = dialog->findChild<QLineEdit*>("fileNameEdit");
                    if (!name) { dialog->reject(); return; }
                    name->setText(QFileInfo(workspace).fileName());
                    QMetaObject::invokeMethod(dialog, "accept");
                });
            }
        });
        openAction->trigger();QVERIFY(picker);QTRY_VERIFY(window->workspaceManager->isWorkspaceOpen());
        QCOMPARE(window->workspaceManager->getWorkspacePath(), workspace);
        QTRY_VERIFY(!welcome->isVisible());
        window->workspaceManager->closeWorkspace();QTRY_VERIFY(welcome->isVisible());
        QTRY_VERIFY(!window->findChild<QWidget*>("welcomeWorkspaceFade"));
        window->activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(window.get()));
        auto* recent=window->findChild<QToolButton*>("welcomeRecentProject");QVERIFY(recent);
        recent->click();QTRY_VERIFY(window->workspaceManager->isWorkspaceOpen());QTRY_VERIFY(!welcome->isVisible());
        if (WorkspaceMotion::enabled()) {
            auto* fade = window->findChild<QWidget*>("welcomeWorkspaceFade");
            QVERIFY(fade && fade->isVisible());
            QVERIFY(fade->testAttribute(Qt::WA_TransparentForMouseEvents));
            auto* animation = fade->findChild<QVariantAnimation*>();
            QVERIFY(animation);
            QCOMPARE(animation->duration(), 160);
            QTRY_VERIFY(!window->findChild<QWidget*>("welcomeWorkspaceFade"));
        }
        auto* session = window->findChild<WorkspaceSessionCoordinator*>();
        QVERIFY(session);
        registry->setActiveGroup(registry->regionGroup("main"));
        tabs->openFileInTab(source.fileName());
        auto* editor = tabs->getCurrentEditor(); QVERIFY(editor);
        auto cursor = editor->textCursor(); cursor.setPosition(3); editor->setTextCursor(cursor);
        auto* navigation = window->findChild<QDockWidget*>("navigationDock");
        QVERIFY(navigation);
        navigation->hide();
        QVERIFY(session->saveSession());
        QVERIFY(session->closeWorkspace(window->workspaceManager->activeWorkspaceIndex()));
        QTRY_VERIFY(welcome->isVisible());
        int restoredPosition = 3;
        if (WorkspaceMotion::enabled()) {
            welcome->findChild<QToolButton*>("welcomeRecentProject")->click();
            auto* fade = window->findChild<QWidget*>("welcomeWorkspaceFade");
            QVERIFY(fade && fade->isVisible());
            editor = tabs->getCurrentEditor();
            QVERIFY(editor && editor->hasFocus());
            QVERIFY(navigation->isHidden());
            QCOMPARE(editor->textCursor().position(), restoredPosition);
            const QString originalText = editor->toPlainText();
            QTest::keyClick(QApplication::focusWidget(), Qt::Key_Space);
            QString expectedText = originalText;
            expectedText.insert(restoredPosition, QLatin1Char(' '));
            QCOMPARE(editor->toPlainText(), expectedText);
            editor->undo();
            QCOMPARE(editor->toPlainText(), originalText);
            QCOMPARE(editor->textCursor().position(), restoredPosition);
            QTest::keyClick(QApplication::focusWidget(), Qt::Key_Right);
            ++restoredPosition;
            QCOMPARE(editor->textCursor().position(), restoredPosition);
            fade->findChild<QVariantAnimation*>()->setCurrentTime(65);
            capture(*window, "welcome-workspace-fade");
            QVERIFY(session->saveSession());
            QVERIFY(session->closeWorkspace(window->workspaceManager->activeWorkspaceIndex()));
            QTRY_VERIFY(welcome->isVisible() && !window->findChild<QWidget*>("welcomeWorkspaceFade"));
        }
        theme.setAnimationsEnabled(false);
        welcome->findChild<QToolButton*>("welcomeRecentProject")->click();
        QTRY_VERIFY(!welcome->isVisible());
        QVERIFY(!window->findChild<QWidget*>("welcomeWorkspaceFade"));
        editor = tabs->getCurrentEditor(); QVERIFY(editor);
        QCOMPARE(editor->textCursor().position(), restoredPosition);
        QTRY_VERIFY(editor->hasFocus());
        theme.setAnimationsEnabled(true);
        qInfo() << "Global open and recent workspace entries completed";
        QVERIFY(session->closeWorkspace(window->workspaceManager->activeWorkspaceIndex()));
        QTRY_VERIFY(welcome->isVisible());
        // A missing recent entry must stay actionable on the welcome page.
        QVERIFY(QDir().rename(workspace, workspace + "-moved"));
        welcome->findChild<QToolButton*>("welcomeRecentProject")->click();
        QVERIFY(welcome->isVisible() && !window->workspaceManager->isWorkspaceOpen());
        auto* error = welcome->findChild<QLabel*>("welcomeOpenError");
        QVERIFY(error && error->isVisible() && error->text().contains(QDir::toNativeSeparators(workspace)));
        QVERIFY(QDir().rename(workspace + "-moved", workspace));
        for(int i=0;i<7;++i) {
            const auto next=profile.filePath(QString("long workspace name %1/%2/项目").arg(i).arg(QString(50,'x')));
            QVERIFY(QDir().mkpath(next));QVERIFY(window->workspaceManager->openWorkspace(next));
            window->workspaceManager->closeWorkspace();
        }
        QTRY_VERIFY(welcome->isVisible());
        QVERIFY(!error->isVisible());
        QCOMPARE(welcome->findChildren<QToolButton*>("welcomeRecentProject").size(),5);
        QVERIFY(!welcome->findChild<QLabel*>("welcomeRecentEmpty"));
        for(int width:{700,1400}) {
            window->resize(width,900);QTest::qWait(100);
            QCOMPARE(welcome->horizontalScrollBar()->maximum(),0);
            for(auto* path:welcome->findChildren<QLabel*>("welcomeRecentPath")) {
                QCOMPARE(path->text(),path->property("fullPath").toString());
                QCOMPARE(path->toolTip(),path->text());
                QVERIFY(path->width()>0);
                QVERIFY(path->parentWidget()->rect().contains(path->geometry()));
            }
            capture(*window,QString("welcome-recent-%1").arg(width));
        }
        theme.setMode(ThemeMode::Dark);QTest::qWait(100);capture(*window,"welcome-recent-dark");
        window.reset();QTest::qWait(100);
    }
};
QTEST_MAIN(WelcomeMainWindowTest)
#include "welcome_mainwindow_test.moc"
