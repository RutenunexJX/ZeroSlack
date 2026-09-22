#include "applicationthememanager.h"
#include "globalcontrolpanel.h"
#include "testuistyle.h"
#include "uicontrols.h"
#include "workspaceconfigurationdialog.h"

#include <QActionGroup>
#include <QApplication>
#include <QHeaderView>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QStyleOptionViewItem>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <memory>

namespace {
void settle() { QApplication::processEvents(); QTest::qWait(20); }
bool usesEla() { return ApplicationThemeManager::instance().backend() == UiStyleBackend::Ela; }
int colorPixels(const QImage& image, QColor ink) {
    int count = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) {
            const auto p = image.pixelColor(x, y);
            if (qAbs(p.red() - ink.red()) + qAbs(p.green() - ink.green()) + qAbs(p.blue() - ink.blue()) < 35)
                ++count;
        }
    return count;
}
int styleCount() {
    int count = 0;
    for (auto* object : qApp->findChildren<QObject*>())
        if (object->inherits("ElaMenuStyle") || object->inherits("ElaListViewStyle")
            || object->inherits("ElaTableViewStyle")) ++count;
    return count;
}
}

class UiItemMenuContractTest final : public QObject {
    Q_OBJECT
private slots:
    void menusKeepActionsAndKeyboard() {
        QWidget host;
        QMenuBar bar(&host);
        auto* menu = UiControls::addMenu(&bar, "&Project");
        if (usesEla()) QVERIFY(menu->inherits("ElaMenu"));
        QAction shared("&Open", &host);
        shared.setShortcut(QKeySequence("Ctrl+O"));
        menu->addAction(&shared);
        auto* disabled = menu->addAction("Disabled action");
        disabled->setEnabled(false);
        auto* checked = menu->addAction("Checked action");
        checked->setCheckable(true);
        checked->setChecked(true);
        auto* child = UiControls::addMenu(menu, "Submenu");
        if (usesEla()) QVERIFY(child->inherits("ElaMenu"));
        child->addAction("Child action");
        menu->addSeparator();
        QSignalSpy activated(&shared, &QAction::triggered);
        QSignalSpy rejected(disabled, &QAction::triggered);
        host.resize(500, 300); host.show(); host.activateWindow(); settle();
        menu->popup(host.mapToGlobal(QPoint(40, 50))); settle();
        for (auto* action : menu->actions())
            if (!action->isSeparator()) QVERIFY(menu->actionGeometry(action).height() >= menu->fontMetrics().height());
        QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, menu->actionGeometry(disabled).center());
        QCOMPARE(rejected.count(), 0); QVERIFY(menu->isVisible());
        menu->setActiveAction(child->menuAction());
        QTest::keyClick(menu, Qt::Key_Right);
        QTRY_VERIFY(child->isVisible());
        QTest::keyClick(child, Qt::Key_Escape);
        QTRY_VERIFY(!child->isVisible()); QVERIFY(menu->isVisible());
        menu->setActiveAction(checked);
        QTest::keyClick(menu, Qt::Key_Return);
        QVERIFY(!checked->isChecked()); QVERIFY(!menu->isVisible());
        QTimer::singleShot(0, menu, [menu] { QTest::keyClick(menu, Qt::Key_O); });
        QTimer::singleShot(1500, menu, &QMenu::close);
        QCOMPARE(menu->exec(host.mapToGlobal(QPoint(40, 50))), &shared);
        QCOMPARE(activated.count(), 1);
        delete menu;
        QCOMPARE(shared.parent(), &host);
    }

    void listRolesWrappingAndChecks() {
        std::unique_ptr<QListWidget> list(UiControls::listWidget());
        QCOMPARE(list->property("zeroslackElaControl").toBool(), usesEla());
        const QColor ink(204, 34, 153);
        auto* item = new QListWidgetItem("signal_name\nrtl/long_path/top.sv:42", list.get());
        QFont font = list->font(); font.setPointSize(17); font.setItalic(true);
        item->setFont(font); item->setForeground(ink);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
        list->resize(460, 240); list->show(); list->activateWindow(); settle();
        QVERIFY(list->visualItemRect(item).height() >= QFontMetrics(font).height() * 2);
        QVERIFY(colorPixels(list->viewport()->grab().toImage(), ink) > 5);
        list->setCurrentItem(item); list->setFocus();
        QTest::keyClick(list.get(), Qt::Key_Space);
        QCOMPARE(item->checkState(), Qt::Checked);
        item->setCheckState(Qt::Unchecked);
        QStyleOptionViewItem option;
        option.initFrom(list.get()); option.rect = list->visualItemRect(item);
        option.features = QStyleOptionViewItem::HasDisplay | QStyleOptionViewItem::HasCheckIndicator;
        option.checkState = Qt::Unchecked;
        const QRect check = list->style()->subElementRect(QStyle::SE_ItemViewItemCheckIndicator, &option, list.get());
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, check.center());
        QCOMPARE(item->checkState(), Qt::Checked);
        for (auto mode : {ThemeMode::Dark, ThemeMode::CatppuccinMocha, ThemeMode::Light}) {
            ApplicationThemeManager::instance().setMode(mode); settle();
            QCOMPARE(item->font(), font);
            QCOMPARE(list->currentItem(), item);
            QCOMPARE(item->checkState(), Qt::Checked);
        }
    }

    void tableEditingAndModelSort() {
        std::unique_ptr<QTableWidget> table(UiControls::tableWidget(2, 2));
        QCOMPARE(table->property("zeroslackElaControl").toBool(), usesEla());
        table->setHorizontalHeaderLabels({"Name", "Value"});
        auto* value = new QTableWidgetItem("32");
        table->setItem(0, 0, new QTableWidgetItem("WIDTH")); table->setItem(0, 1, value);
        const QColor ink(204, 34, 153); value->setForeground(ink);
        auto* check = new QTableWidgetItem("Enable");
        check->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        check->setCheckState(Qt::Unchecked); table->setItem(1, 0, check);
        table->resize(420, 240); table->show(); table->activateWindow(); settle();
        QVERIFY(colorPixels(table->viewport()->grab().toImage(), ink) > 3);
        table->setCurrentItem(check); table->setFocus();
        QTest::keyClick(table.get(), Qt::Key_Space);
        QCOMPARE(check->checkState(), Qt::Checked);
        table->setCurrentItem(value); table->editItem(value); settle();
        auto* edit = table->findChild<QLineEdit*>(); QVERIFY(edit);
        edit->setText("64"); QTest::keyClick(edit, Qt::Key_Return); settle();
        QCOMPARE(value->text(), QString("64"));
        table.reset();

        QStandardItemModel model(0, 2);
        model.setHorizontalHeaderLabels({"Command", "Shortcut"});
        model.appendRow({new QStandardItem("Zulu"), new QStandardItem("Ctrl+Z")});
        model.appendRow({new QStandardItem("Alpha"), new QStandardItem("Ctrl+A")});
        std::unique_ptr<QTableView> view(UiControls::tableView());
        if (usesEla()) QVERIFY(view->inherits("ElaTableView"));
        view->setModel(&model); view->setSortingEnabled(true);
        view->sortByColumn(0, Qt::AscendingOrder);
        view->resize(430, 240); view->show(); settle();
        QCOMPARE(view->model(), &model);
        QCOMPARE(model.index(0, 0).data().toString(), QString("Alpha"));
        QCOMPARE(view->horizontalHeader()->sortIndicatorOrder(), Qt::AscendingOrder);
    }

    void actualWorkspaceAndCommandList() {
        WorkspaceConfigurationDialog dialog;
        WorkspaceConfiguration config;
        config.includeDirs = {"rtl", "include"};
        config.defines.insert("WIDTH", "32");
        dialog.setConfiguration(config);
        auto* defines = dialog.findChild<QTableWidget*>("workspaceDefinesTable"); QVERIFY(defines);
        auto* dirs = dialog.findChild<QListWidget*>("workspaceIncludeDirsList"); QVERIFY(dirs);
        QCOMPARE(defines->property("zeroslackElaControl").toBool(), usesEla());
        QCOMPARE(dirs->property("zeroslackElaControl").toBool(), usesEla());
        defines->item(0, 1)->setText("64");
        std::unique_ptr<QListWidgetItem> first(dirs->takeItem(0));
        dirs->insertItem(1, first.release());
        QCOMPARE(dialog.configuration().defines.value("WIDTH"), QString("64"));
        QCOMPARE(dialog.configuration().includeDirs, QStringList({"include", "rtl"}));

        GlobalControlPanel panel;
        GlobalControlItem item; item.title = "signal_name"; item.subtitle = "rtl/top.sv:42";
        panel.setItems({item});
        QWidget anchor; anchor.resize(900, 700); anchor.show(); settle();
        panel.showCentered(&anchor); settle();
        auto* results = panel.findChild<QListWidget*>("globalControlResultList"); QVERIFY(results);
        QCOMPARE(results->property("zeroslackElaControl").toBool(), usesEla());
        QVERIFY(results->visualItemRect(results->item(0)).height() >= results->fontMetrics().height() * 2);
        int activations = 0;
        panel.setItemActivatedHandler([&](const GlobalControlItem& selected) {
            if (selected.title == item.title) ++activations;
        });
        auto* input = panel.findChild<QLineEdit*>("globalControlSearchEdit"); QVERIFY(input);
        QTest::keyClick(input, Qt::Key_Return); settle();
        QCOMPARE(activations, 1);
        panel.close();
    }

    void visibleDestructionReleasesStyles() {
        QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); settle();
        const int baseline = styleCount();
        for (int cycle = 0; cycle < 3; ++cycle) {
            auto menu = std::unique_ptr<QMenu>(UiControls::menu());
            auto* child = UiControls::addMenu(menu.get(), "Child"); child->addAction("Action");
            menu->popup(QPoint(30, 30)); settle();
            menu->setActiveAction(child->menuAction()); QTest::keyClick(menu.get(), Qt::Key_Right); settle();
            auto list = std::unique_ptr<QListWidget>(UiControls::listWidget()); list->addItem("Visible"); list->show();
            auto table = std::unique_ptr<QTableView>(UiControls::tableView()); table->show(); settle();
            if (usesEla()) QVERIFY(styleCount() >= baseline + 4);
        }
        QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); settle();
        QCOMPARE(styleCount(), baseline);
    }
};

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QTemporaryDir profile; if (!profile.isValid()) return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, profile.path());
    if (!initializeUiStyleForTest()) return 3;
    ApplicationThemeManager::instance().applyToApplication();
    UiItemMenuContractTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "ui_item_menu_contract_test.moc"
