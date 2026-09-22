#include "applicationthememanager.h"
#include "navigationwidget.h"
#include "settingscenterpanel.h"
#include "testuistyle.h"
#include "uicontrols.h"
#include "workspacehubview.h"

#include <QApplication>
#include <QListWidget>
#include <QPointer>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QTabBar>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QTreeWidget>
#include <QWheelEvent>
#include <memory>

namespace {
void settle() { QApplication::processEvents(); QTest::qWait(20); }
bool wheel(QWidget* target, QPoint angle, QPoint pixel = {}, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    const QPoint at = target->rect().center();
    QWheelEvent event(at, target->mapToGlobal(at), pixel, angle, Qt::NoButton, modifiers,
                      Qt::NoScrollPhase, false);
    event.setAccepted(false);
    QApplication::sendEvent(target, &event);
    return event.isAccepted();
}
QWidget* closeButton(QTabBar* bar, int index) {
    auto* button = bar->tabButton(index, QTabBar::RightSide);
    return button ? button : bar->tabButton(index, QTabBar::LeftSide);
}
bool usesEla() { return ApplicationThemeManager::instance().backend() == UiStyleBackend::Ela; }
}

class ElaMotionTest final : public QObject {
    Q_OBJECT
private slots:
    void wheelReversalPrecisionAndExternalNavigation() {
        std::unique_ptr<QListWidget> list(UiControls::listWidget());
        UiControls::enableSmoothScrolling(list.get());
        for (int i = 0; i < 200; ++i) list->addItem(QString("Resource %1").arg(i));
        list->resize(330, 230); list->show(); list->setCurrentRow(0); settle();
        auto* bar = list->verticalScrollBar();
        if (!usesEla()) {
            QVERIFY(!bar->inherits("ElaScrollBar"));
            return;
        }
        QVERIFY(bar->property("smoothWheelEnabled").toBool());
        QVERIFY(bar->maximum() > 1000);
        QVERIFY(wheel(list->viewport(), {0, -120}));
        QTest::qWait(45);
        const int intermediate = bar->value();
        QVERIFY(intermediate > 0);
        QTest::qWait(190);
        QVERIFY(bar->value() > intermediate);
        const int resting = bar->value();
        QTest::qWait(180); QCOMPARE(bar->value(), resting);

        bar->setValue(1000);
        for (int i = 0; i < 6; ++i) wheel(list->viewport(), {0, -120});
        QTest::qWait(40);
        const int reversingAt = bar->value();
        QVERIFY(reversingAt > 1000);
        wheel(list->viewport(), {0, 120});
        QTest::qWait(190);
        QVERIFY(bar->value() < reversingAt);

        bar->setValue(0);
        QVERIFY(wheel(list->viewport(), {}, {0, -37}));
        QCOMPARE(bar->value(), 37);
        QTest::qWait(190); QCOMPARE(bar->value(), 37);
        wheel(list->viewport(), {0, -120}); QTest::qWait(35);
        bar->setValue(bar->maximum());
        QTest::qWait(190); QCOMPARE(bar->value(), bar->maximum());
        QVERIFY(!wheel(bar, {0, -120}));

        list->setCurrentRow(list->count() - 1); settle();
        wheel(list->viewport(), {0, 120}); QTest::qWait(35);
        list->setFocus(); QTest::keyClick(list.get(), Qt::Key_Home);
        QTest::qWait(190);
        QCOMPARE(list->currentRow(), 0); QCOMPARE(bar->value(), 0);
        wheel(list->viewport(), {0, -120}); QTest::qWait(35);
        list->hide(); const int hiddenAt = bar->value();
        QTest::qWait(190); QCOMPARE(bar->value(), hiddenAt);
        list->show(); settle();
        wheel(list->viewport(), {0, -120}); QTest::qWait(35);
        list->clear(); settle(); QTest::qWait(190);
        QCOMPARE(bar->maximum(), 0); QCOMPARE(bar->value(), 0);
        list->addItems({"one", "two"}); settle();
        QCOMPARE(bar->maximum(), 0);
    }

    void horizontalScrollAndVisibleDestruction() {
        for (int i = 0; i < 4; ++i) {
            auto area = std::unique_ptr<QScrollArea>(UiControls::scrollArea());
            auto* content = new QWidget;
            content->setFixedSize(1600, 1600);
            area->setWidget(content);
            UiControls::enableSmoothScrolling(area.get());
            area->resize(350, 250); area->show(); settle();
            auto* bar = area->horizontalScrollBar();
            if (usesEla()) {
                QVERIFY(wheel(bar, {0, -120}, {}, Qt::ShiftModifier));
                QTest::qWait(190); QVERIFY(bar->value() > 0);
                const int start = bar->value();
                wheel(bar, {}, {-23, 0}); QCOMPARE(bar->value(), start + 23);
                wheel(area->viewport(), {0, -120});
            }
            QPointer<QScrollBar> guard(bar);
            area.reset(); settle(); QVERIFY(!guard);
        }
    }

    void tabOverflowKeepsSelectionGeometryAndOwnership() {
        std::unique_ptr<QTabWidget> tabs(UiControls::tabWidget());
        auto* bar = tabs->tabBar();
        tabs->setTabsClosable(true); tabs->setMovable(true);
        tabs->setElideMode(Qt::ElideNone);
        for (int i = 0; i < 20; ++i)
            tabs->addTab(new QWidget, QString(i % 2 ? "long_resource_%1.sv" : "r%1.sv").arg(i));
        bar->setTabVisible(4, false);
        tabs->resize(450, 240); tabs->show(); settle();
        if (!usesEla()) {
            QVERIFY(!bar->property("smoothScrollEnabled").toBool());
            return;
        }
        QVERIFY(bar->property("smoothScrollEnabled").toBool());
        QVERIFY(bar->usesScrollButtons());
        const int index = tabs->currentIndex();
        QPointer<QWidget> page = tabs->currentWidget();
        const QSize firstSize = bar->tabRect(0).size();
        const QSize secondSize = bar->tabRect(1).size();
        QVERIFY(firstSize.width() != secondSize.width());
        const int before = bar->tabRect(0).x();
        wheel(bar, {0, -120}); QTest::qWait(45);
        QVERIFY(bar->tabRect(0).x() < before);
        const int intermediate = bar->tabRect(0).x();
        QTest::qWait(190); QVERIFY(bar->tabRect(0).x() < intermediate);
        QCOMPARE(tabs->currentIndex(), index); QCOMPARE(tabs->currentWidget(), page.data());
        QCOMPARE(bar->tabRect(0).size(), firstSize); QCOMPARE(bar->tabRect(1).size(), secondSize);
        for (int i = 0; i < 4; ++i) wheel(bar, {0, -120});
        QTest::qWait(35); const int reversingAt = bar->tabRect(0).x();
        wheel(bar, {0, 120}); QTest::qWait(190);
        QVERIFY(bar->tabRect(0).x() > reversingAt);

        const int pixelStart = bar->tabRect(0).x();
        wheel(bar, {}, {-29, 0}); QCOMPARE(bar->tabRect(0).x(), pixelStart - 29);
        wheel(bar, {0, -120}); QTest::qWait(30);
        tabs->setCurrentIndex(19); settle();
        const QRect selectedRect = bar->tabRect(19);
        QVERIFY(bar->rect().intersects(selectedRect));
        QTest::qWait(190); QCOMPARE(bar->tabRect(19), selectedRect);
        auto* close = closeButton(bar, 19); QVERIFY(close);
        QVERIFY(selectedRect.contains(close->mapTo(bar, close->rect().center())));
        QSignalSpy closes(tabs.get(), &QTabWidget::tabCloseRequested);
        QPointer<QWidget> last = tabs->widget(19);
        QTest::mouseClick(close, Qt::LeftButton); settle();
        QCOMPARE(closes.count(), 1); QCOMPARE(tabs->count(), 20); QVERIFY(last);
        wheel(bar, {0, 120}); QTest::qWait(25);
        bar->moveTab(19, 0); settle(); QTest::qWait(190);
        QCOMPARE(tabs->widget(0), last.data());
        QVERIFY(bar->rect().intersects(bar->tabRect(tabs->currentIndex())));
        wheel(bar, {0, -120}); QTest::qWait(25);
        tabs->removeTab(0); settle(); QTest::qWait(190);
        QVERIFY(last); last->deleteLater(); settle();
        tabs->resize(6000, 240); settle();
        const int selected = tabs->currentIndex();
        wheel(bar, {0, -120}); QTest::qWait(190);
        QCOMPARE(tabs->currentIndex(), selected);
        QCOMPARE(bar->tabRect(0).x(), 0);
        tabs->resize(450, 240); settle();
        wheel(bar, {0, -120}); tabs.reset(); settle();
    }

    void actualViewsOptInAndTreeInterruption() {
        SettingsCenterService service;
        SettingsCenterPanel settings(&service);
        auto* categories = settings.findChild<QListWidget*>("settingsCenterCategoryList");
        QVERIFY(categories);
        QCOMPARE(categories->verticalScrollBar()->property("smoothWheelEnabled").toBool(), usesEla());
        for (auto* scroll : settings.findChildren<QScrollArea*>())
            QCOMPARE(scroll->verticalScrollBar()->property("smoothWheelEnabled").toBool(), usesEla());
        for (auto* table : settings.findChildren<QTableView*>())
            QCOMPARE(table->verticalScrollBar()->property("smoothWheelEnabled").toBool(), usesEla());
        WorkspaceHubView hub;
        auto* hubTree = hub.findChild<QTreeView*>("workspaceHubTree"); QVERIFY(hubTree);
        QCOMPARE(hubTree->isAnimated(), usesEla());
        QCOMPARE(hubTree->verticalScrollBar()->property("smoothWheelEnabled").toBool(), usesEla());
        NavigationWidget navigation;
        const auto trees = navigation.findChildren<QTreeWidget*>();
        QVERIFY(trees.size() >= 2);
        for (auto* tree : trees) {
            QCOMPARE(tree->isAnimated(), usesEla());
            QVERIFY(!tree->verticalScrollBar()->property("smoothWheelEnabled").toBool());
        }
        std::unique_ptr<QTreeWidget> tree(UiControls::treeWidget());
        UiControls::enableTreeTransitions(tree.get());
        tree->setHeaderHidden(true);
        auto* root = new QTreeWidgetItem(tree.get(), {"Root"});
        for (int i = 0; i < 30; ++i) new QTreeWidgetItem(root, {QString::number(i)});
        tree->resize(330, 260); tree->show(); tree->setCurrentItem(root); settle();
        QTest::keyClick(tree.get(), Qt::Key_Right); QTest::qWait(25);
        QTest::keyClick(tree.get(), Qt::Key_Left); QTest::qWait(250);
        QVERIFY(!root->isExpanded());
        QTest::keyClick(tree.get(), Qt::Key_Right); QTest::qWait(250);
        QVERIFY(root->isExpanded());
        QVERIFY(!tree->visualItemRect(root->child(0)).isEmpty());
        tree->collapseItem(root); QTest::qWait(20);
        tree->clear(); settle(); QTest::qWait(250);
        QCOMPARE(tree->topLevelItemCount(), 0);
    }
};

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QTemporaryDir profile; if (!profile.isValid()) return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, profile.path());
    if (!initializeUiStyleForTest()) return 3;
    ElaMotionTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "ela_motion_test.moc"
