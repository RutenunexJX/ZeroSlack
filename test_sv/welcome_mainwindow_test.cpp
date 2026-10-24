#include "applicationthememanager.h"
#include "mainwindow.h"
#include "welcomecard.h"
#include "workspacemanager.h"
#include <QtWidgets>
#include <QtTest>

class WelcomeMainWindowTest : public QObject {
    Q_OBJECT
    void capture(MainWindow& window, const QString& name) {
        const auto directory=qEnvironmentVariable("ZEROSLACK_TEST_ARTIFACT_DIR");
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
        auto& theme=ApplicationThemeManager::instance();QVERIFY(theme.selectBackend(UiStyleBackend::Ela));
        theme.setMode(ThemeMode::Light);theme.setAnimationsEnabled(true);theme.applyToApplication();
        auto window=std::make_unique<MainWindow>();window->resize(1400,900);window->show();window->activateWindow();
        window->workspaceManager->setWorkspaceAliasSelector([](QWidget*,const QString& suggested) { return suggested; });
        qInfo() << "MainWindow devicePixelRatio:" << window->devicePixelRatioF() << "logical size:" << window->size();
        auto* welcome=window->findChild<QScrollArea*>("welcomePage");QVERIFY(welcome);QTRY_VERIFY(welcome->isVisible());
        QList<WelcomeCard*> cards;
        for (int i=0;i<3;++i) {
            auto* card=dynamic_cast<WelcomeCard*>(window->findChild<QPushButton*>(QString("welcomeCard%1").arg(i)));
            QVERIFY(card);QVERIFY(card->property("welcomeResourceValid").toBool());cards.append(card);
        }
        QVERIFY(cards[0]->isEnabled());QVERIFY(!cards[1]->isEnabled() && !cards[2]->isEnabled());
        QCOMPARE(cards[0]->title(),QString("RTL Development"));QCOMPARE(cards[1]->title(),QString("Waveform Design"));QCOMPARE(cards[2]->title(),QString("Register Design"));
        QTest::qWait(100);capture(*window,"welcome-light");
        if (QGuiApplication::platformName()=="windows") {
            QTRY_VERIFY(window->isActiveWindow());
            for (auto* card : cards) {
                auto* animation=card->findChild<QVariantAnimation*>("welcomeMotion");QVERIFY(animation);
                QSignalSpy frames(animation,&QVariantAnimation::valueChanged);
                QEnterEvent enter({},{},{});QEvent leave(QEvent::Leave);
                QCoreApplication::sendEvent(card,&enter);QVERIFY(card->motionRunning());QTest::qWait(120);QVERIFY(!frames.isEmpty());
                QCoreApplication::sendEvent(card,&leave);QVERIFY(!card->motionRunning());frames.clear();QTest::qWait(100);QCOMPARE(frames.size(),0);
                QCoreApplication::sendEvent(card,&enter);QVERIFY(card->motionRunning());
                QEvent deactivate(QEvent::WindowDeactivate);QCoreApplication::sendEvent(window.get(),&deactivate);
                QVERIFY(!card->motionRunning());frames.clear();QTest::qWait(100);QCOMPARE(frames.size(),0);
                QCoreApplication::sendEvent(card,&enter);QVERIFY(card->motionRunning());
                welcome->hide();QVERIFY(!card->motionRunning());frames.clear();QTest::qWait(100);QCOMPARE(frames.size(),0);
                welcome->show();QVERIFY(!card->motionRunning());
                QCoreApplication::sendEvent(card,&enter);QVERIFY(card->motionRunning());
                theme.setAnimationsEnabled(false);QVERIFY(!card->motionRunning());
                QCoreApplication::sendEvent(card,&enter);QVERIFY(!card->motionRunning());
                theme.setAnimationsEnabled(true);QVERIFY(!card->motionRunning());
            }
            QEnterEvent enter({},{},{});QCoreApplication::sendEvent(cards[0],&enter);QVERIFY(cards[0]->motionRunning());
            window->showMinimized();QTRY_VERIFY(!cards[0]->motionRunning());
            window->showNormal();window->activateWindow();QTRY_VERIFY(window->isActiveWindow());
            QCoreApplication::sendEvent(cards[0],&enter);QTRY_VERIFY_WITH_TIMEOUT(!cards[0]->motionRunning(),3500);
        }
        theme.setMode(ThemeMode::Dark);QTest::qWait(100);capture(*window,"welcome-dark");
        qInfo() << "All three cards: lifecycle, animation switch, completion and dark theme checked";
        theme.setMode(ThemeMode::Light);window->resize(700,900);QTest::qWait(100);
        QCOMPARE(welcome->horizontalScrollBar()->maximum(),0);capture(*window,"welcome-narrow");
        window->resize(1400,900);QTest::qWait(100);
        const auto workspace=profile.filePath("Workspace 中文");QVERIFY(QDir().mkpath(workspace));
        QFile source(workspace+"/top.sv");QVERIFY(source.open(QIODevice::WriteOnly));source.write("module top; endmodule\n");source.close();
        const auto openUsing = [&](QPushButton* button) {
            qInfo() << "Opening workspace using" << button->objectName();
            bool picker=false;
            QTimer::singleShot(100,[&] {
                for(auto* top:QApplication::topLevelWidgets())if(auto* dialog=qobject_cast<QFileDialog*>(top)) {
                    picker=true;dialog->setDirectory(workspace);dialog->selectFile(workspace);QMetaObject::invokeMethod(dialog,"accept");
                }
            });
            button->click();QVERIFY(picker);QTRY_VERIFY(window->workspaceManager->isWorkspaceOpen());
            QTRY_VERIFY(!welcome->isVisible());for(auto* card:cards)QVERIFY(!card->motionRunning());
            window->workspaceManager->closeWorkspace();QTRY_VERIFY(welcome->isVisible());
        };
        openUsing(cards[0]);
        auto* open=window->findChild<QPushButton*>("welcomeOpenProjectButton");QVERIFY(open);openUsing(open);
        auto* recent=window->findChild<QToolButton*>("welcomeRecentProject");QVERIFY(recent);
        recent->click();QTRY_VERIFY(window->workspaceManager->isWorkspaceOpen());QTRY_VERIFY(!welcome->isVisible());
        qInfo() << "RTL, Open workspace and Recent workspaces completed";
        window->workspaceManager->closeWorkspace();QTRY_VERIFY(welcome->isVisible());
        QPointer<QVariantAnimation> animation=cards[0]->findChild<QVariantAnimation*>("welcomeMotion");
        if (QGuiApplication::platformName()=="windows") {window->activateWindow();QTRY_VERIFY(window->isActiveWindow());QEnterEvent enter({},{},{});QCoreApplication::sendEvent(cards[0],&enter);}
        window.reset();QVERIFY(animation.isNull());QTest::qWait(100);
    }
};
QTEST_MAIN(WelcomeMainWindowTest)
#include "welcome_mainwindow_test.moc"
