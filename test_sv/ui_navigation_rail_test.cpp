#include "applicationthememanager.h"
#include "contextrail.h"
#include "insightvisualstyle.h"
#include "panellayoutcontroller.h"
#include "roundedicons.h"
#include "testuistyle.h"
#include "uicontrols.h"
#include "workspacechrome.h"
#include <QContextMenuEvent>
#include <QEnterEvent>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

namespace {
void settle() { QApplication::processEvents(); QTest::qWait(20); }
bool usesEla() { return ApplicationThemeManager::instance().backend() == UiStyleBackend::Ela; }
}

class UiNavigationRailTest final : public QObject {
    Q_OBJECT
private slots:
    void actionsAndLifetime() {
        QMainWindow host;
        host.setCentralWidget(new QWidget);
        auto* rail = new ContextRail(&host);
        host.addToolBar(Qt::RightToolBarArea, rail);
        auto* separator = rail->addSeparator(); separator->setObjectName("contextFloatingFooter");
        auto* footer = rail->addRailAction(RoundedIcons::icon(RoundedIcons::Collapse), "Hide floating views");
        footer->setCheckable(true);
        QVERIFY(rail->addEntry({"files", "Files", "Browse files", RoundedIcons::icon(RoundedIcons::Folder)}));
        QVERIFY(rail->addEntry({"design", "Design", {}, RoundedIcons::icon(RoundedIcons::Hierarchy)}));
        QCOMPARE(rail->entryIds(), QStringList({"files", "design"}));
        host.resize(640, 460); host.show(); settle();
        auto* action = rail->actions().first();
        QPointer<QToolButton> button = qobject_cast<QToolButton*>(rail->widgetForAction(action));
        QVERIFY(button); QCOMPARE(button->inherits("ElaToolButton"), usesEla());
        QCOMPARE(rail->actions().last(), footer);
        QVERIFY(rail->actionGeometry(action).contains(button->geometry().center()));
        QVERIFY(button->size().expandedTo(button->minimumSizeHint()) == button->size());
        QSignalSpy activated(rail, &ContextRail::entryActivated);
        rail->setActiveEntryId("files"); QVERIFY(button->isChecked());
        button->setFocus(); QTest::keyClick(button, Qt::Key_Space);
        QCOMPARE(activated.size(), 1); QCOMPARE(activated.first().first().toString(), QString("files"));
        action->setEnabled(false); settle();
        QTest::mouseClick(button, Qt::LeftButton); QCOMPARE(activated.size(), 1);
        QVERIFY(!button->isEnabled());
        action->setEnabled(true); action->setToolTip("Updated tooltip");
        rail->setEntryIcon("files", RoundedIcons::icon(RoundedIcons::Settings));
        QCOMPARE(button->toolTip(), QString("Updated tooltip"));
        QCOMPARE(button->icon().cacheKey(), action->icon().cacheKey());
        action->setVisible(false); settle(); QVERIFY(!button->isVisible());
        action->setVisible(true); settle(); QVERIFY(button->isVisible());
        QSignalSpy menus(rail, &ContextRail::entryContextMenuRequested);
        const auto point = button->rect().center();
        QContextMenuEvent context(QContextMenuEvent::Mouse, point, button->mapToGlobal(point));
        QApplication::sendEvent(button, &context);
        QCOMPARE(menus.size(), 1); QCOMPARE(menus.first().first().toString(), QString("files"));
        QCOMPARE(menus.first().at(1).toPoint(), button->mapToGlobal(point));
        QCOMPARE(activated.size(), 1);
        QSignalSpy collapsed(footer, &QAction::triggered);
        auto* footerButton = qobject_cast<QToolButton*>(rail->widgetForAction(footer)); QVERIFY(footerButton);
        QTest::mouseClick(footerButton, Qt::LeftButton); QCOMPARE(collapsed.size(), 1); QVERIFY(footer->isChecked());
        footer->setText("Restore floating views");
        QCOMPARE(footerButton->text(), footer->text());
        rail->setActiveEntryId("files");
        QPointer<QAction> removed = action;
        QTest::mouseMove(button, point);
        QVERIFY(rail->removeEntry(" files "));
        QTRY_VERIFY(button.isNull()); QTRY_VERIFY(removed.isNull());
        QVERIFY(rail->activeEntryId().isEmpty());
        rail->clearEntries(); settle();
        QVERIFY(rail->isHidden()); QCOMPARE(rail->actions().size(), 2);
        QVERIFY(rail->addEntry({"new", "New", {}, {}})); settle();
        QVERIFY(rail->isVisible()); QCOMPARE(rail->actions().last(), footer);
    }

    void projectMenuAndSidebar() {
        QMainWindow host;
        host.setCentralWidget(new QWidget);
        auto* pane = new NavigationPaneCoordinator(&host);
        host.addDockWidget(Qt::LeftDockWidgetArea, pane->dock());
        host.menuBar()->addMenu("Workspace")->addAction("Open project");
        int settings = 0;
        new WorkspaceChrome(&host, pane, [&] { ++settings; });
        host.resize(800, 540); host.show(); settle();
        auto* project = host.findChild<QToolButton*>("projectRailButton"); QVERIFY(project);
        auto* setting = host.findChild<QToolButton*>("settingsRailButton"); QVERIFY(setting);
        auto* collapse = host.findChild<QToolButton*>("collapseProjectSidebarButton"); QVERIFY(collapse);
        for (auto* button : {project, setting, collapse}) {
            QCOMPARE(button->inherits("ElaToolButton"), usesEla());
            QCOMPARE(button->toolButtonStyle(), Qt::ToolButtonIconOnly);
            QVERIFY(!button->accessibleName().isEmpty());
            QVERIFY(button->parentWidget()->rect().contains(button->geometry()));
        }
        QVERIFY(project->width() <= setting->width() + 2);
        QTest::mouseClick(setting, Qt::LeftButton);
        setting->setFocus(); QTest::keyClick(setting, Qt::Key_Space);
        QCOMPARE(settings, 2);
        bool shown = false;
        connect(project->menu(), &QMenu::aboutToShow, &host, [&] {
            shown = true;
            QTimer::singleShot(10, project->menu(), [project] { QTest::keyClick(project->menu(), Qt::Key_Escape); });
        });
        QTest::mouseClick(project, Qt::LeftButton); settle();
        QVERIFY(shown); QVERIFY(!project->menu()->isVisible()); QVERIFY(!project->isDown());
        QTest::mouseClick(collapse, Qt::LeftButton);
        QTRY_VERIFY(!pane->isAnimating()); QVERIFY(!pane->isExpanded());
        auto* expand = host.findChild<QToolButton*>("expandProjectSidebarButton"); QVERIFY(expand);
        QTest::mouseClick(expand, Qt::LeftButton);
        QTRY_VERIFY(!pane->isAnimating()); QVERIFY(pane->isExpanded());
    }

    void badgeGeometryAndTheme() {
        QMainWindow host; host.setCentralWidget(new QWidget);
        PanelLayoutController controller(&host);
        auto* dock = new QDockWidget("Problems", &host); dock->setWidget(new QWidget);
        host.addDockWidget(Qt::BottomDockWidgetArea, dock);
        QVERIFY(controller.registerBottomPanel("problems", dock)); controller.finalize();
        host.resize(700, 500); host.show(); settle();
        auto* button = controller.buttonForPanel("problems"); QVERIFY(button);
        QCOMPARE(button->inherits("ElaToolButton"), usesEla());
        for (const QString count : {QString("1"), QString("999+")}) {
            controller.setPanelBadge("problems", count, "error"); settle();
            QVERIFY(button->width() >= button->sizeHint().width());
            QVERIFY(button->height() >= button->sizeHint().height());
            QVERIFY(button->parentWidget()->rect().contains(button->geometry()));
            QVERIFY(button->accessibleDescription().contains(count));
        }
        const auto baseFont = button->font();
        auto largeFont = baseFont; largeFont.setPointSize(26); button->setFont(largeFont); settle();
        QVERIFY(button->height() >= button->minimumSizeHint().height());
        QVERIFY(button->parentWidget()->rect().contains(button->geometry()));
        button->setFont(baseFont); settle();
        button->clearFocus();
        QImage previous;
        for (auto mode : {ThemeMode::Light, ThemeMode::Dark, ThemeMode::CatppuccinLatte, ThemeMode::CatppuccinMocha}) {
            ApplicationThemeManager::instance().setMode(mode); settle();
            const auto image = button->grab().toImage(); QVERIFY(!image.isNull());
            if (!previous.isNull()) QVERIFY(image != previous);
            previous = image;
            QVERIFY(button->parentWidget()->rect().contains(button->geometry()));
        }
        ApplicationThemeManager::instance().setMode(ThemeMode::Light);
        controller.setPanelBadge("problems", {}, {}); settle();
        QVERIFY(!button->property("hasBadge").toBool());
        QCOMPARE(button->property("badgeReserve").toInt(), 0);
    }
};

int main(int argc, char** argv) {
    QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
    QTemporaryDir profile; if (!profile.isValid()) return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, profile.path());
    if (!initializeUiStyleForTest()) return 3;
    ApplicationThemeManager::instance().applyToApplication();
    UiNavigationRailTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "ui_navigation_rail_test.moc"
