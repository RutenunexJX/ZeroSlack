#include "editorradialmenu.h"
#include "applicationthememanager.h"
#include <QApplication>
#include <QDir>
#include <QFrame>
#include <QMenu>
#include <QScreen>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>

namespace {
const QStringList ids {
    "insight.moduleBlockDiagram", "insight.signalUsageHotspot", "insight.signalKernelGraph", "insight.stateTransitionGraph",
    "waveSimulation.runCurrentContext", "waveSimulation.observeSignal", "waveSimulation.revealSignalInResult",
    "pinloom.linkSelection", "pinloom.openLinkedContent", "pinloom.toggleBindingMarkers",
    "refactor.organizeSignalDeclarations", "refactor.createSignalDefinition", "refactor.editInstanceSlots", "refactor.exposeSignalToTop",
    "refactor.createAssignmentQueue", "edit.toggleSelectionCase", "edit.replaceSelectionWithSpaces", "view.temporaryEditor.open"
};
void populate(QMenu& menu) {
    for (const auto& id : ids) {
        auto* action=menu.addAction(id);
        action->setProperty("actionId",id);
        action->setProperty("executable",id!="insight.stateTransitionGraph");
    }
    for(const auto& id : {"edit.undo","edit.redo","edit.cut","edit.copy","edit.paste","select.all","source.goToDefinition","navigation.goLine"}) {
        auto* action=menu.addAction(id); action->setProperty("actionId",id);action->setProperty("executable",true);
    }
}
}
class RadialMenuTest : public QObject {
    Q_OBJECT
private slots:
    void geometryStatesAndIcons() {
        QMenu menu;populate(menu);
        QList<QImage> images;
        for(const auto& id : ids) {
            QVERIFY(!EditorRadialMenu::actionIcon(id).isNull());
            QImage image=EditorRadialMenu::actionIcon(id).pixmap(32,32).toImage();
            QCOMPARE(image.pixelColor(0,0).alpha(),0);
            QCOMPARE(image,EditorRadialMenu::actionIcon(id).pixmap(32,32).toImage());
            for(const auto& other : images) QVERIFY2(image!=other,qPrintable(id));
            images.append(image);
        }
        EditorRadialMenu popup(&menu);
        popup.popupAt(QPoint(300,250));QTest::qWait(20);
        for(int g=0;g<6;++g) {
            auto* category=popup.findChild<QToolButton*>(QStringLiteral("radialGroup.%1").arg(g));
            QVERIFY(category);category->click();
            auto* bar=popup.findChild<QFrame*>("radialActionBar");QVERIFY(bar);
            QVERIFY(popup.rect().contains(bar->geometry()));
            for(auto* button : bar->findChildren<QToolButton*>()) {
                QVERIFY(button->text().isEmpty());QCOMPARE(button->iconSize(),QSize(16,16));
                QVERIFY(!button->accessibleName().isEmpty());
                QVERIFY(ids.contains(button->objectName()));
                QVERIFY(bar->rect().contains(button->geometry()));
            }
            if(g==0) {
                auto* disabled=bar->findChild<QToolButton*>("insight.stateTransitionGraph");
                QVERIFY(disabled && !disabled->isEnabled());disabled->click();QVERIFY(popup.isVisible());
                auto* enabled=bar->findChild<QToolButton*>("insight.signalUsageHotspot");
                QVERIFY(enabled && enabled->isEnabled());
                const int y=enabled->y();
                for(auto* button : bar->findChildren<QToolButton*>())QCOMPARE(button->y(),y);
                QTest::mouseMove(&popup,bar->geometry().bottomLeft()+QPoint(1,3));
                QVERIFY(bar->isVisible());
            }
            if(g==3) {
                QSet<int> rows;for(auto* button : bar->findChildren<QToolButton*>())rows.insert(button->y());
                QCOMPARE(rows.size(),2);
            }
        }
        const QRect bounds=QGuiApplication::primaryScreen()->availableGeometry();
        for(const QPoint& corner : {bounds.topLeft(),bounds.topRight(),bounds.bottomLeft(),bounds.bottomRight()}) {
            popup.popupAt(corner);QVERIFY(bounds.contains(popup.geometry()));
        }
        popup.close();
    }
    void executionAndDismissal() {
        QMenu menu;populate(menu);
        int calls=0;
        connect(menu.actions()[1],&QAction::triggered,this,[&]{++calls;QVERIFY(!QApplication::activePopupWidget());});
        QTimer::singleShot(20,[&]{
            auto* popup=QApplication::activePopupWidget();QVERIFY(popup);
            popup->findChild<QToolButton*>("radialGroup.0")->click();
            popup->findChild<QToolButton*>("insight.signalUsageHotspot")->click();
        });
        EditorRadialMenu::exec(&menu,QPoint(300,250));QCOMPARE(calls,1);
        QTimer::singleShot(20,[]{QTest::keyClick(QApplication::activePopupWidget(),Qt::Key_Escape);});
        EditorRadialMenu::exec(&menu,QPoint(300,250));QCOMPARE(calls,1);
        QTimer::singleShot(20,[]{QTest::mouseClick(QApplication::activePopupWidget(),Qt::LeftButton,Qt::NoModifier,QPoint(1,1));});
        EditorRadialMenu::exec(&menu,QPoint(300,250));QCOMPARE(calls,1);
    }
    void screenshots() {
        const QString root=qEnvironmentVariable("ZEROSLACK_RADIAL_REVIEW_DIR");
        if(root.isEmpty())return;
        QVERIFY(QDir().mkpath(root));
        for(auto mode : {ThemeMode::Light,ThemeMode::Dark}) {
            ApplicationThemeManager::instance().setMode(mode);
            QMenu menu;populate(menu);EditorRadialMenu popup(&menu);popup.popupAt(QPoint(300,250));
            popup.findChild<QToolButton*>("radialGroup.0")->click();QTest::qWait(30);
            QVERIFY(popup.grab().save(root+(mode==ThemeMode::Light?"/light.png":"/dark.png")));
        }
        ApplicationThemeManager::instance().setMode(ThemeMode::Light);
    }
};
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    ApplicationThemeManager::instance().applyToApplication();
    RadialMenuTest test;return QTest::qExec(&test,argc,argv);
}
#include "editor_radial_menu_test.moc"
