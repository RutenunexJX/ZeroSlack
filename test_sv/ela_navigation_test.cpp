#include "applicationthememanager.h"
#include "crashrecoveryservice.h"
#include "editorsplitcontroller.h"
#include "mycodeeditor.h"
#include "navigationwidget.h"
#include "tabmanager.h"
#include "uicontrols.h"
#include <QApplication>
#include <QFile>
#include <QHeaderView>
#include <QPointer>
#include <QScrollBar>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QStyleOptionViewItem>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTextDocument>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <memory>

namespace {
void settle() { QApplication::processEvents(); QTest::qWait(20); }
bool writeFile(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write("module counter; endmodule\n") > 0;
}
int colorPixels(const QImage& image, const QColor& color, int tolerance = 0) {
    int count = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) {
            const auto pixel = image.pixelColor(x, y);
            if (qAbs(pixel.red() - color.red()) + qAbs(pixel.green() - color.green())
                + qAbs(pixel.blue() - color.blue()) <= tolerance) ++count;
        }
    return count;
}
QWidget* closeButton(QTabWidget* tabs, int index) {
    QWidget* result = tabs->tabBar()->tabButton(index, QTabBar::RightSide);
    return result ? result : tabs->tabBar()->tabButton(index, QTabBar::LeftSide);
}
}

class ElaNavigationTest final : public QObject {
    Q_OBJECT
private slots:
    void treeModelRolesAndInput() {
        std::unique_ptr<QTreeWidget> tree(UiControls::treeWidget());
        QVERIFY(tree->property("zeroslackElaControl").toBool());
        tree->setHeaderHidden(true);
        tree->setColumnCount(2);
        tree->header()->setStretchLastSection(true);
        tree->setColumnWidth(0, 180);
        auto* root = new QTreeWidgetItem(tree.get(), {"Parent", "module"});
        auto* child = new QTreeWidgetItem(root, {"semantic_name", "child"});
        const QColor ink(213, 49, 173);
        child->setForeground(0, ink);
        QFont font = tree->font(); font.setPointSize(19); font.setItalic(true);
        child->setFont(0, font);
        child->setFlags(child->flags() | Qt::ItemIsUserCheckable);
        child->setCheckState(0, Qt::Unchecked);
        tree->resize(480, 240); tree->show(); tree->activateWindow(); settle();
        tree->setCurrentItem(root); tree->setFocus();
        QTest::keyClick(tree.get(), Qt::Key_Right); settle();
        // Item pixels become visible as Qt's native tree expansion completes.
        QTRY_VERIFY(colorPixels(tree->viewport()->grab().toImage(), ink) > 3);
        QVERIFY(root->isExpanded());
        QVERIFY(tree->visualItemRect(child).height() >= QFontMetrics(font).height());
        QVERIFY(colorPixels(tree->viewport()->grab().toImage(), ink) > 3);
        tree->setCurrentItem(child);
        QTest::keyClick(tree.get(), Qt::Key_Space); settle();
        QCOMPARE(child->checkState(0), Qt::Checked);
        child->setCheckState(0, Qt::Unchecked);
        const QRect row = tree->visualItemRect(child);
        QStyleOptionViewItem option;
        option.initFrom(tree.get()); option.rect = row;
        option.rect.setWidth(tree->columnWidth(0) - tree->indentation());
        option.features = QStyleOptionViewItem::HasCheckIndicator | QStyleOptionViewItem::HasDisplay;
        option.viewItemPosition = QStyleOptionViewItem::Beginning;
        option.checkState = Qt::Unchecked;
        const QRect indicator = tree->style()->subElementRect(QStyle::SE_ItemViewItemCheckIndicator, &option, tree.get());
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, indicator.center()); settle();
        QCOMPARE(child->checkState(0), Qt::Checked);
        tree->setCurrentItem(root);
        QTest::keyClick(tree.get(), Qt::Key_Left); settle();
        QVERIFY(!root->isExpanded());
    }

    void modelViewAndThemeLifetime() {
        QStandardItemModel model;
        for (int i = 0; i < 180; ++i) model.appendRow(new QStandardItem(QString("resource_%1").arg(i)));
        std::unique_ptr<QTreeView> tree(UiControls::treeView());
        QVERIFY(tree->inherits("ElaTreeView"));
        tree->setModel(&model); tree->setHeaderHidden(true);
        tree->resize(320, 200); tree->show(); settle();
        tree->setCurrentIndex(model.index(160, 0)); tree->scrollTo(tree->currentIndex()); settle();
        QVERIFY(tree->verticalScrollBar()->value() > 0);
        const auto font = tree->font();
        for (auto mode : {ThemeMode::Light, ThemeMode::Dark, ThemeMode::CatppuccinLatte, ThemeMode::CatppuccinMocha}) {
            ApplicationThemeManager::instance().setMode(mode); settle();
            QCOMPARE(tree->font(), font);
            QCOMPARE(tree->currentIndex(), model.index(160, 0));
            QVERIFY(!tree->grab().isNull());
        }
        ApplicationThemeManager::instance().setMode(ThemeMode::Light);
        // Visible destruction must not leave QWidget with a freed local style.
        tree.reset(); settle();
    }

    void treeAutomaticColumnWidth() {
        std::unique_ptr<QTreeWidget> tree(UiControls::treeWidget());
        tree->setSelectionMode(QAbstractItemView::NoSelection);
        tree->setColumnCount(2);
        tree->setHeaderLabels({"I", "Module"});
        tree->header()->setStretchLastSection(true);
        auto* root = new QTreeWidgetItem(tree.get(), {"top", "top_module"});
        new QTreeWidgetItem(root, {"u_counter", "counter"});
        const QColor ink(213, 49, 173);
        root->setForeground(0, ink);
        QFont font = tree->font(); font.setPointSize(16); font.setBold(true);
        root->setFont(0, font);
        QPixmap icon(16, 16); icon.fill(Qt::black); root->setIcon(0, QIcon(icon));
        tree->resize(440, 180); tree->show(); settle();
        tree->setColumnWidth(0, 220); settle();
        const int fullText = colorPixels(tree->viewport()->grab().toImage(), ink, 40);
        QVERIFY(fullText > 3);
        tree->resizeColumnToContents(0); settle();
        QCOMPARE(colorPixels(tree->viewport()->grab().toImage(), ink, 40), fullText);
    }

    void tabsOverflowMoveAndCloseOwnership() {
        std::unique_ptr<QTabWidget> tabs(UiControls::tabWidget());
        auto* bar = tabs->tabBar();
        QVERIFY(bar->inherits("ElaTabBar"));
        tabs->setTabsClosable(true); tabs->setMovable(true); tabs->setElideMode(Qt::ElideMiddle);
        for (int i = 0; i < 18; ++i) {
            tabs->addTab(new QWidget, QString("long_module_name_%1.sv").arg(i));
            bar->setTabData(i, i);
        }
        tabs->resize(380, 230); tabs->show(); tabs->activateWindow(); settle();
        tabs->setCurrentIndex(17); settle();
        QVERIFY(bar->usesScrollButtons());
        QVERIFY(bar->rect().intersects(bar->tabRect(17)));
        QWidget* close = closeButton(tabs.get(), 17); QVERIFY(close);
        QVERIFY(bar->rect().contains(QRect(close->mapTo(bar, QPoint()), close->size())));
        QPointer<QWidget> page(tabs->widget(17));
        QSignalSpy closes(tabs.get(), &QTabWidget::tabCloseRequested);
        QTest::mouseClick(close, Qt::LeftButton); settle();
        QCOMPARE(closes.count(), 1); QCOMPARE(tabs->count(), 18); QVERIFY(page);
        QSignalSpy moves(bar, &QTabBar::tabMoved);
        bar->moveTab(17, 0); settle();
        QCOMPARE(moves.count(), 1); QCOMPARE(tabs->widget(0), page.data());
        QCOMPARE(bar->tabData(0).toInt(), 17);
        bar->setFocus(); QTest::keyClick(bar, Qt::Key_Right); settle();
        QCOMPARE(tabs->currentIndex(), 1);
        tabs->removeTab(0); QVERIFY(page);
        page->deleteLater(); settle();
        tabs.reset(); settle();
    }

    void actualDocumentCloseAndSplit() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString first = dir.filePath("first.sv"), second = dir.filePath("second.sv");
        QVERIFY(writeFile(first) && writeFile(second));
        QWidget host;
        auto* layout = new QVBoxLayout(&host);
        auto* tabs = UiControls::editorTabWidget(&host); layout->addWidget(tabs);
        TabManager manager(tabs);
        manager.setCrashRecoveryService(std::make_unique<CrashRecoveryService>(dir.filePath("recovery")));
        manager.enableSplitLayout(&host);
        host.resize(800, 460); host.show(); settle();
        QVERIFY(manager.openFileInTab(first)); QVERIFY(manager.openFileInTab(second));
        QPointer<MyCodeEditor> editor(manager.getCurrentEditor()); QVERIFY(editor);
        editor->insertPlainText("// modified\n"); settle();
        QVERIFY(editor->document()->isModified());
        int decisions = 0;
        auto answer = UnsavedDocumentBatchDecision::Cancel;
        manager.unsavedDocumentManagerForTesting()->setDecisionProvider(
            [&](const QList<PendingDocumentChange>& changes, QWidget*) {
                if (!changes.isEmpty()) ++decisions;
                return answer;
            });
        QWidget* close = closeButton(tabs, tabs->indexOf(editor)); QVERIFY(close);
        QTest::mouseClick(close, Qt::LeftButton); settle();
        QCOMPARE(decisions, 1); QCOMPARE(manager.editorCount(), 2); QVERIFY(editor);
        const QString modified = editor->toPlainText();
        const QFont font = editor->font();
        QVERIFY(manager.moveCurrentViewToSplit(EditorSplitDirection::Right)); settle();
        QCOMPARE(manager.splitCount(), 2);
        auto* group = manager.editorSplitController()->groupForPage(editor);
        QVERIFY(group && group != tabs); QVERIFY(group->tabBar()->inherits("ElaTabBar"));
        QCOMPARE(editor->toPlainText(), modified); QCOMPARE(editor->font(), font);
        QVERIFY(manager.mergeCurrentSplit()); settle();
        QCOMPARE(manager.splitCount(), 1); QVERIFY(editor);
        group = manager.editorSplitController()->groupForPage(editor);
        group->setCurrentWidget(editor);
        answer = UnsavedDocumentBatchDecision::DiscardAll;
        close = closeButton(group, group->indexOf(editor)); QVERIFY(close);
        QTest::mouseClick(close, Qt::LeftButton); settle();
        QCOMPARE(decisions, 2); QCOMPARE(manager.editorCount(), 1);
    }

    void actualFileTreeActivation() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString path = dir.filePath("counter.sv"); QVERIFY(writeFile(path));
        NavigationWidget navigation;
        navigation.setWorkspaceRoot(dir.path()); navigation.updateFileHierarchy({path});
        navigation.resize(320, 400); navigation.show(); settle();
        auto* tree = navigation.findChild<QTreeWidget*>("navigationFileTree"); QVERIFY(tree);
        QVERIFY(tree->property("zeroslackElaControl").toBool());
        auto* tabs = navigation.findChild<QTabWidget*>(); QVERIFY(tabs && tabs->tabBar()->inherits("ElaTabBar"));
        const auto found = tree->findItems("counter.sv", Qt::MatchExactly | Qt::MatchRecursive);
        QCOMPARE(found.size(), 1); tree->scrollToItem(found.first()); settle();
        QSignalSpy activated(&navigation, &NavigationWidget::fileDoubleClicked);
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, tree->visualItemRect(found.first()).center());
        QTest::mouseDClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, tree->visualItemRect(found.first()).center()); settle();
        QCOMPARE(activated.count(), 1);
    }
};

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QTemporaryDir profile; if (!profile.isValid()) return 2;
    QCoreApplication::setApplicationName("ZeroSlack-Ela-Navigation-Test");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, profile.path());
    qputenv("ZEROSLACK_SESSION_STORAGE_PATH", (profile.path() + "/sessions.ini").toUtf8());
    auto& theme = ApplicationThemeManager::instance();
    if (!theme.selectBackend(UiStyleBackend::Ela)) return 3;
    theme.applyToApplication();
    ElaNavigationTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "ela_navigation_test.moc"
