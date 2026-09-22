#include "applicationthememanager.h"
#include "mainwindow.h"
#include "navigationwidget.h"
#include "testuistyle.h"
#include "workspacechrome.h"
#include "panelcompositor.h"
#ifdef ZEROSLACK_ENABLE_ELA
#include "ElaNavigationBar.h"
#endif
#include <QFile>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {
void settle() { QApplication::processEvents(); QTest::qWait(30); }
}

class UiSidebarContractTest final : public QObject {
    Q_OBJECT
private slots:
    void compositedSidebarMatchesLiveFrame() {
#ifdef ZEROSLACK_ENABLE_ELA
        if (ApplicationThemeManager::instance().backend() != UiStyleBackend::Ela) QSKIP("Ela backend only");
        QMainWindow host;
        host.setCentralWidget(new QWidget);
        auto* dock = new QDockWidget(&host);
        auto* title = new QWidget(dock); title->setFixedHeight(0);
        dock->setTitleBarWidget(title);
        auto* bar = new ElaNavigationBar(dock);
        bar->setCustomContent(new QLabel("Navigation content"));
        bar->setCustomContentWidthRange(200, 400);
        bar->setNavigationBarWidth(280);
        dock->setWidget(bar);
        host.addDockWidget(Qt::LeftDockWidgetArea, dock);
        host.resize(900, 620); host.show(); settle();
        const auto dockSize = dock->size();
        const auto expected = host.grab(dock->geometry()).toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        PanelCompositor compositor(&host);
        for (bool collapsed : {false, true}) {
            if (collapsed) {
                bar->setDisplayMode(ElaNavigationType::Minimal, false);
                dock->hide(); settle();
                QCOMPARE(bar->width(), 0);
            }
            // Hold the expanded endpoint, also when captured from a zero-width
            // collapsed bar. Include the frame outside the custom content body.
            QVERIFY(compositor.begin(dock, bar, dockSize.width(), dockSize.width(), 1, [] {}));
            QTest::qWait(20);
            const auto actual = compositor.grab(QRect(QPoint(), dockSize)).toImage()
                                    .convertToFormat(QImage::Format_ARGB32_Premultiplied);
            QCOMPARE(actual, expected);
            bar->setDisplayMode(ElaNavigationType::Maximal, false);
            dock->show(); host.resizeDocks({dock}, {dockSize.width()}, Qt::Horizontal);
            compositor.finish(); settle();
            QCOMPARE(compositor.snapshotBytes(), 0);
        }
#else
        QSKIP("Ela backend not built");
#endif
    }

    void motionResizeAndReversal() {
        QMainWindow host;
        auto* editor = new QPlainTextEdit;
        editor->setPlainText("module counter;\nendmodule\n");
        editor->document()->setModified(false);
        host.setCentralWidget(editor);
        auto* pane = new NavigationPaneCoordinator(&host);
        auto* dock = pane->dock();
        host.addDockWidget(Qt::LeftDockWidgetArea, dock);
        new WorkspaceChrome(&host, pane, [] {});
        host.resize(900, 620); host.show(); settle();
        auto* body = host.findChild<NavigationWidget*>(); QVERIFY(body);
        const int expanded = dock->width();
        const int editorWidth = editor->width();
        const int contentWidth = body->width();
        QVERIFY(expanded >= 200 && expanded <= 400);
        pane->setExpanded(false);
        QVERIFY(!pane->isExpanded()); QVERIFY(pane->isAnimating());
        auto* compositor = host.findChild<PanelCompositor*>();
        if (compositor) {
            QVERIFY(compositor->isActive());
            QVERIFY(compositor->snapshotBytes() > 0);
            QTRY_VERIFY_WITH_TIMEOUT(compositor->extent() < expanded * 3 / 4 && compositor->extent() > 0, 200);
        } else {
            QTRY_VERIFY_WITH_TIMEOUT(dock->width() < expanded * 3 / 4 && dock->width() > 0, 200);
        }
        QCOMPARE(body->width(), contentWidth);
        QVERIFY(editor->width() > editorWidth);
        QTRY_VERIFY(!pane->isAnimating());
        QVERIFY(dock->isHidden());
        QVERIFY(editor->width() >= editorWidth + expanded);
        pane->setExpanded(true);
        QTRY_VERIFY(!pane->isAnimating());
        QCOMPARE(dock->width(), expanded);

        host.resizeDocks({dock}, {340}, Qt::Horizontal); settle();
        QCOMPARE(dock->width(), 340);
        pane->setExpanded(false);
        QTRY_VERIFY(!pane->isAnimating());
        pane->setExpanded(true);
        QTRY_VERIFY(!pane->isAnimating());
        QCOMPARE(dock->width(), 340);
        for (int i = 0; i < 4; ++i) {
            pane->setExpanded(false);
            QTest::qWait(45);
            const qreal partialWidth = compositor ? compositor->extent() : dock->width();
            QVERIFY(partialWidth < 340);
            pane->setExpanded(true);
            if (compositor) QVERIFY(qAbs(compositor->extent() - partialWidth) <= 3);
            QTRY_VERIFY(!pane->isAnimating());
            QCOMPARE(dock->width(), 340);
        }
        pane->setExpanded(false);
        QTest::qWait(35);
        pane->setExpanded(false, false);
        QVERIFY(!pane->isAnimating()); QVERIFY(dock->isHidden());
        pane->setExpanded(true, false); settle();
        QCOMPARE(dock->width(), 340);
        QCOMPARE(editor->toPlainText(), QString("module counter;\nendmodule\n"));
        QVERIFY(!editor->document()->isModified());
        if (compositor) {
            QVERIFY(!compositor->isActive());
            QCOMPARE(compositor->snapshotBytes(), 0);
        }
#ifdef ZEROSLACK_ENABLE_ELA
        if (ApplicationThemeManager::instance().backend() == UiStyleBackend::Ela) {
            auto* bar = qobject_cast<ElaNavigationBar*>(dock->widget()); QVERIFY(bar);
            QCOMPARE(bar->customContent(), body);
            QVERIFY(bar->customHeader());
            QVERIFY(!host.findChild<QWidget*>("navigationSlidingContent"));
            QCOMPARE(pane->findChildren<QVariantAnimation*>().size(), 0);
        }
#endif
    }

    void compositedMotionKeepsLiveLayoutStableAndAcceptsInput() {
        if (ApplicationThemeManager::instance().backend() != UiStyleBackend::Ela) QSKIP("Ela backend only");
        QMainWindow host;
        auto* editor = new QPlainTextEdit;
        editor->setPlainText(QString("module counter;\n") + QString(50000, ' ') + "\nendmodule\n");
        editor->setLineWrapMode(QPlainTextEdit::NoWrap);
        host.setCentralWidget(editor);
        auto* pane = new NavigationPaneCoordinator(&host);
        host.addDockWidget(Qt::LeftDockWidgetArea, pane->dock());
        host.resize(1600, 1000); host.show(); host.activateWindow(); settle();
        auto* compositor = host.findChild<PanelCompositor*>(); QVERIFY(compositor);
        struct Resizes final : QObject {
            int count = 0;
            bool eventFilter(QObject*, QEvent* event) override {
                if (event->type() == QEvent::Resize) ++count;
                return false;
            }
        } resizes;
        editor->installEventFilter(&resizes);
        for (bool open : {false, true}) {
            resizes.count = 0;
            pane->setExpanded(open);
            const int preparedCount = resizes.count;
            const auto preparedSize = editor->size();
            QTest::qWait(80);
            QVERIFY(compositor->isActive());
            QCOMPARE(editor->size(), preparedSize);
            QCOMPARE(resizes.count, preparedCount);
            QTRY_VERIFY(!pane->isAnimating());
            QVERIFY2(resizes.count <= 1, qPrintable(QString("Expected <= 1 boundary resize, got %1, open=%2")
                .arg(resizes.count).arg(open)));
            QCOMPARE(compositor->snapshotBytes(), 0);
        }
        editor->setFocus();
        editor->moveCursor(QTextCursor::End);
        pane->setExpanded(false);
        QTest::keyClicks(editor, "input");
        QVERIFY(!pane->isAnimating());
        QVERIFY(!compositor->isActive());
        QVERIFY(editor->toPlainText().endsWith("input"));
        pane->setExpanded(true);
        host.resize(1300, 850);
        settle();
        QVERIFY(!pane->isAnimating());
        QVERIFY(!compositor->isActive());
        QVERIFY(!pane->dock()->isHidden());
        QVERIFY(host.rect().contains(editor->geometry()));
        pane->setExpanded(false); QTest::qWait(35);
        pane->showSearch();
        settle();
        auto* search = host.findChild<QLineEdit*>("navigationSearchLineEdit"); QVERIFY(search);
        QVERIFY(search->hasFocus());
        QVERIFY(!pane->isAnimating());
        QCOMPARE(compositor->snapshotBytes(), 0);

        auto* probe = new QPushButton("Click after motion", editor);
        probe->setGeometry(40, 40, 170, 32); probe->show(); settle();
        QSignalSpy clicked(probe, &QPushButton::clicked);
        const QPoint global = probe->mapToGlobal(probe->rect().center());
        pane->setExpanded(false, false); settle();
        pane->setExpanded(true);
        QVERIFY(compositor->isActive());
        QMouseEvent press(QEvent::MouseButtonPress, editor->mapFromGlobal(global), global,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(editor, &press);
        QVERIFY(!compositor->isActive());
        QMouseEvent release(QEvent::MouseButtonRelease, editor->mapFromGlobal(global), global,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(editor, &release);
        QCOMPARE(clicked.size(), 1);
    }

    void contentAndHeaderSurvive() {
        QTemporaryDir workspace; QVERIFY(workspace.isValid());
        const QString path = workspace.filePath("counter.sv");
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("module counter; endmodule\n"); file.close();
        QMainWindow host; host.setCentralWidget(new QWidget);
        auto* pane = new NavigationPaneCoordinator(&host);
        host.addDockWidget(Qt::LeftDockWidgetArea, pane->dock());
        new WorkspaceChrome(&host, pane, [] {});
        auto* body = host.findChild<NavigationWidget*>(); QVERIFY(body);
        body->setWorkspaceRoot(workspace.path()); body->updateFileHierarchy({path});
        pane->setSearchQueries("counter", "state");
        host.resize(850, 560); host.show(); settle();
        auto* tree = host.findChild<QTreeWidget*>("navigationFileTree"); QVERIFY(tree);
        auto items = tree->findItems("counter.sv", Qt::MatchExactly | Qt::MatchRecursive);
        QCOMPARE(items.size(), 1);
        auto* item = items.first(); tree->setCurrentItem(item); tree->scrollToItem(item);
        QSignalSpy opened(body, &NavigationWidget::fileDoubleClicked);
        pane->setExpanded(false); QTRY_VERIFY(!pane->isAnimating());
        pane->setExpanded(true); QTRY_VERIFY(!pane->isAnimating());
        QCOMPARE(pane->filesSearchQuery(), QString("counter"));
        QCOMPARE(tree->currentItem(), item);
        const auto point = tree->visualItemRect(item).center();
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, point);
        QTest::mouseDClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, point);
        QCOMPARE(opened.size(), 1);
        QCOMPARE(QFileInfo(opened.first().first().toString()).canonicalFilePath(), QFileInfo(path).canonicalFilePath());
        pane->showDesign(); settle();
        QCOMPARE(pane->designSearchQuery(), QString("state"));
        pane->showFiles(); settle();
        QCOMPARE(pane->filesSearchQuery(), QString("counter"));
        for (const auto* name : {"projectRailButton", "settingsRailButton", "collapseProjectSidebarButton"}) {
            auto* button = host.findChild<QToolButton*>(name); QVERIFY(button);
            QVERIFY(button->isVisible());
            QVERIFY(button->parentWidget()->rect().contains(button->geometry()));
        }
        pane->showSearch(); settle();
        auto* search = host.findChild<QLineEdit*>("navigationSearchLineEdit"); QVERIFY(search);
        QVERIFY(search->hasFocus());
    }

    void externalVisibilityAndSavedLayout() {
        QMainWindow host; host.setCentralWidget(new QWidget);
        auto* pane = new NavigationPaneCoordinator(&host);
        auto* dock = pane->dock(); host.addDockWidget(Qt::LeftDockWidgetArea, dock);
        host.resize(850, 580); host.show(); settle();
        host.resizeDocks({dock}, {355}, Qt::Horizontal); settle();
        const QByteArray visibleState = host.saveState();
        dock->hide(); settle(); QVERIFY(!pane->isExpanded());
        const QByteArray hiddenState = host.saveState();
        dock->show(); settle(); QVERIFY(pane->isExpanded());
        QCOMPARE(dock->width(), 355);
        host.hide(); settle(); host.show(); settle();
        QVERIFY(pane->isExpanded()); QVERIFY(!dock->isHidden());
        QVERIFY(host.restoreState(hiddenState)); settle();
        QVERIFY(!pane->isExpanded()); QVERIFY(dock->isHidden());
        QVERIFY(host.restoreState(visibleState)); settle();
        QVERIFY(pane->isExpanded()); QCOMPARE(dock->width(), 355);
        {
            QMainWindow restored; restored.setCentralWidget(new QWidget);
            auto* restoredPane = new NavigationPaneCoordinator(&restored);
            restored.addDockWidget(Qt::LeftDockWidgetArea, restoredPane->dock());
            restored.resize(850, 580);
            QVERIFY(restored.restoreState(visibleState));
            restored.show(); settle();
            QCOMPARE(restoredPane->dock()->width(), 355);
        }
        // Destruction during an unfinished transition must cancel its callbacks.
        auto* transient = new QMainWindow;
        auto* transientPane = new NavigationPaneCoordinator(transient);
        transient->setCentralWidget(new QWidget);
        transient->addDockWidget(Qt::LeftDockWidgetArea, transientPane->dock());
        transient->show(); settle(); transientPane->setExpanded(false);
        delete transient; QTest::qWait(300);
    }

    void mainWindowShortcut() {
        MainWindow host;
        host.resize(1100, 760); host.show(); host.activateWindow(); settle();
        auto* dock = host.findChild<QDockWidget*>("navigationDock"); QVERIFY(dock);
        const bool initiallyHidden = dock->isHidden();
        QTest::keyClick(&host, Qt::Key_1, Qt::ControlModifier);
        QTRY_COMPARE(dock->isHidden(), !initiallyHidden);
        QTest::qWait(300);
        QTest::keyClick(&host, Qt::Key_1, Qt::ControlModifier);
        QTRY_COMPARE(dock->isHidden(), initiallyHidden);
    }

    void nativeModesAndOwnership() {
#ifdef ZEROSLACK_ENABLE_ELA
        if (ApplicationThemeManager::instance().backend() != UiStyleBackend::Ela) QSKIP("Ela backend only");
        QWidget host;
        auto* row = new QHBoxLayout(&host);
        ElaNavigationBar bar;
        bar.setNavigationBarWidth(280);
        row->addWidget(&bar); row->addWidget(new QWidget, 1);
        host.resize(700, 450); host.show(); settle();
        for (auto mode : {ElaNavigationType::Compact, ElaNavigationType::Minimal, ElaNavigationType::Maximal}) {
            bar.setDisplayMode(mode);
            QTRY_VERIFY(!bar.isDisplayModeAnimating());
            QCOMPARE(bar.getDisplayMode(), mode);
            QCOMPARE(bar.width(), mode == ElaNavigationType::Compact ? 42 : mode == ElaNavigationType::Minimal ? 0 : 280);
        }
        QPointer<QWidget> first = new QWidget;
        bar.setCustomContent(first);
        bar.setCustomContentWidthRange(200, 400);
        bar.setNavigationBarWidth(330); settle();
        bar.setDisplayMode(ElaNavigationType::Minimal); QTest::qWait(35);
        auto* replacement = new QWidget;
        bar.setCustomContent(replacement);
        bar.setDisplayMode(ElaNavigationType::Maximal);
        QTRY_VERIFY(!bar.isDisplayModeAnimating());
        QTRY_VERIFY(first.isNull());
        QCOMPARE(bar.width(), 330); QCOMPARE(bar.customContent(), replacement);
        quint64 generation = 0;
        int delegatedDuration = 0;
        bar.setDisplayModeTransitionHandler([&](int, int duration, quint64 token) {
            delegatedDuration = duration;
            generation = token;
            return true;
        });
        bar.setDisplayMode(ElaNavigationType::Minimal);
        QCOMPARE(delegatedDuration, 255);
        const auto cancelled = generation;
        bar.setDisplayMode(ElaNavigationType::Maximal);
        QVERIFY(generation != cancelled);
        bar.finishDisplayModeTransition(cancelled);
        QVERIFY(bar.isDisplayModeAnimating());
        bar.finishDisplayModeTransition(generation);
        QVERIFY(!bar.isDisplayModeAnimating());
        QCOMPARE(bar.width(), 330);
#else
        QSKIP("Ela backend not built");
#endif
    }
};

int main(int argc, char** argv) {
    QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
    QTemporaryDir profile; if (!profile.isValid()) return 2;
    QCoreApplication::setApplicationName("ZeroSlack-Sidebar-Test");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, profile.path());
    qputenv("ZEROSLACK_SESSION_STORAGE_PATH", (profile.path() + "/sessions.ini").toUtf8());
    if (!initializeUiStyleForTest()) return 3;
    ApplicationThemeManager::instance().applyToApplication();
    UiSidebarContractTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "ui_sidebar_contract_test.moc"
