#include "uicontrols.h"
#include "applicationthememanager.h"
#include <QApplication>
#include <QMenu>
#include <QScreen>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <memory>

class ContextPopupTest : public QObject {
    Q_OBJECT
private slots:
    void actionsAndCancellation() {
        std::unique_ptr<QMenu> menu(UiControls::menu());
        auto* action=menu->addAction("Copy");auto* disabled=menu->addAction("Unavailable");disabled->setEnabled(false);
        int calls=0;connect(action,&QAction::triggered,this,[&]{++calls;});
        connect(disabled,&QAction::triggered,this,[&]{calls+=100;});
        QTimer::singleShot(20,[&]{QTest::mouseClick(menu.get(),Qt::LeftButton,Qt::NoModifier,menu->actionGeometry(action).center());});
        QCOMPARE(menu->exec({300,250}),action);QCOMPARE(calls,1);
        QTimer::singleShot(20,[&]{QTest::mouseClick(menu.get(),Qt::LeftButton,Qt::NoModifier,menu->actionGeometry(disabled).center());QVERIFY(menu->isVisible());QTest::keyClick(menu.get(),Qt::Key_Escape);});
        QVERIFY(!menu->exec({300,250}));QCOMPARE(calls,1);
        QTimer::singleShot(20,[&]{QTest::keyClick(menu.get(),Qt::Key_Escape);});
        QVERIFY(!menu->exec({300,250}));QCOMPARE(calls,1);
    }
    void boundariesAndSubmenu() {
        std::unique_ptr<QMenu> menu(UiControls::menu());
        menu->addAction("Copy");menu->addSeparator();auto* submenu=UiControls::addMenu(menu.get(),"Block associations");submenu->addAction("Blocks");
        const QRect bounds=QGuiApplication::primaryScreen()->availableGeometry();
        for(const QPoint& corner:{bounds.topLeft(),bounds.topRight(),bounds.bottomLeft(),bounds.bottomRight()}) {
            menu->popup(corner);QTest::qWait(30);
            QVERIFY2(bounds.contains(menu->geometry()),qPrintable(QString("screen=%1,%2 %3x%4 menu=%5,%6 %7x%8")
                .arg(bounds.x()).arg(bounds.y()).arg(bounds.width()).arg(bounds.height())
                .arg(menu->x()).arg(menu->y()).arg(menu->width()).arg(menu->height())));
            menu->close();
        }
    }
};
int main(int argc,char** argv) {
    QApplication app(argc,argv);QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    ApplicationThemeManager::instance().applyToApplication();ContextPopupTest test;return QTest::qExec(&test,argc,argv);
}
#include "editor_context_popup_test.moc"
