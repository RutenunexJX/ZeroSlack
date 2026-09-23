#include "applicationthememanager.h"
#include "contextfloatingwindow.h"
#include "editorsplitcontroller.h"
#include "tabmanager.h"
#include "uicontrols.h"
#include "ElaAppBar.h"
#include "ElaDragHandle.h"
#include "ElaTabWidget.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QVBoxLayout>

namespace {
bool writeFile(const QString& path) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write("module test; endmodule\n") > 0;
}
struct Fixture {
    QWidget host;
    QTabWidget* initial = UiControls::editorTabWidget(&host);
    TabManager tabs{initial};
    Fixture() {
        tabs.enableSplitLayout(&host);
        host.resize(960, 640);
        host.show();
    }
};
void sendMouse(QWidget* target, QEvent::Type type, const QPoint& point,
               Qt::MouseButton button, Qt::MouseButtons buttons) {
    QMouseEvent event(type, QPointF(point), QPointF(target->mapToGlobal(point)),
                      button, buttons, Qt::NoModifier);
    QApplication::sendEvent(target, &event);
}
}

class ElaDocumentTabsTest final : public QObject {
    Q_OBJECT
private slots:
    void sharedDocumentSurvivesFloatAndReturn() {
        QTemporaryDir files;
        const auto path = files.filePath("project/top.sv");
        QVERIFY(writeFile(path));
        Fixture f;
        auto* shortcut = new QAction(&f.host);
        shortcut->setShortcut(QKeySequence("Ctrl+Alt+J"));
        f.host.addAction(shortcut);
        QVERIFY(qobject_cast<ElaTabWidget*>(f.initial));
        QVERIFY(qobject_cast<ElaTabWidget*>(f.initial)->hasHostedTabs());
        QVERIFY(f.tabs.openFileInTab(path));
        auto* original = f.tabs.getCurrentEditor();
        QVERIFY(f.tabs.splitCurrentView(EditorSplitDirection::Right));
        auto* copy = f.tabs.getCurrentEditor();
        copy->moveCursor(QTextCursor::End);
        copy->insertPlainText("// unsaved\n");
        const int cursor = copy->textCursor().position();
        auto* document = f.tabs.sharedDocumentForEditor(copy);
        auto* split = f.tabs.editorSplitController();
        QPointer<QTabWidget> floating = split->floatPage(copy, QPoint(100, 100));
        QVERIFY(floating);
        QVERIFY(qobject_cast<ElaTabWidget*>(floating)->isFloatingTabWidget());
        QVERIFY(floating->window()->findChild<ElaAppBar*>());
        QVERIFY(shortcut->associatedObjects().contains(floating->window()));
        QCOMPARE(f.tabs.editorCount(), 2);
        QCOMPARE(document, f.tabs.sharedDocumentForEditor(copy));
        QCOMPARE(copy->document(), original->document());
        QCOMPARE(copy->textCursor().position(), cursor);
        QCOMPARE(original->toPlainText(), copy->toPlainText());
        QVERIFY(f.tabs.hasUnsavedChanges());
        QPointer<QWidget> window(floating->window());
        QVERIFY(window->close());
        QTRY_VERIFY(window.isNull());
        QCOMPARE(split->groupForPage(copy), split->groupForPage(original));
        QCOMPARE(f.tabs.editorCount(), 2);
        QVERIFY(f.tabs.hasUnsavedChanges());
        copy->undo();
        QVERIFY(!original->toPlainText().contains("unsaved"));
    }

    void scopeTemporaryAndUnsavedClose() {
        QTemporaryDir files;
        const auto rootA = files.filePath("a");
        const auto rootB = files.filePath("b");
        const auto a = rootA + "/a.sv";
        const auto b = rootB + "/b.sv";
        const auto temp = files.filePath("scratch.v");
        QVERIFY(writeFile(a) && writeFile(b) && writeFile(temp));
        Fixture f;
        f.tabs.setWorkspaceScope({rootA, rootB}, rootA);
        QVERIFY(f.tabs.openFileInTab(a));
        auto* editorA = f.tabs.getCurrentEditor();
        editorA->insertPlainText("// dirty\n");
        auto* split = f.tabs.editorSplitController();
        QPointer<QTabWidget> floatingA = split->floatPage(editorA, QPoint(80, 80));
        QVERIFY(floatingA);
        auto* windowA = floatingA->window();
        QVERIFY(f.tabs.openFileInTab(temp));
        auto* temporary = f.tabs.getCurrentEditor();
        auto* floatingTemp = split->floatPage(temporary, QPoint(220, 150));
        QVERIFY(floatingTemp && floatingTemp != floatingA);
        f.tabs.setWorkspaceScope({rootA, rootB}, rootB);
        QVERIFY(!windowA->isVisible());
        QVERIFY(floatingTemp->window()->isVisible());
        QVERIFY(f.tabs.isTemporaryEditor(temporary));
        f.tabs.setWorkspaceScope({rootA, rootB}, rootA);
        QVERIFY(windowA->isVisible());
        QVERIFY(f.tabs.setTabLocked(editorA, true));
        emit floatingA->tabCloseRequested(floatingA->indexOf(editorA));
        QVERIFY(f.tabs.openEditors().contains(editorA));
        QVERIFY(f.tabs.setTabLocked(editorA, false));
        int reviews = 0;
        f.tabs.unsavedDocumentManagerForTesting()->setDecisionProvider(
            [&](const QList<PendingDocumentChange>& changes, QWidget*) {
                ++reviews;
                return reviews == 1 ? UnsavedDocumentBatchDecision::Cancel : UnsavedDocumentBatchDecision::DiscardAll;
            });
        emit floatingA->tabCloseRequested(floatingA->indexOf(editorA));
        QCOMPARE(reviews, 1);
        QVERIFY(f.tabs.openEditors().contains(editorA));
        QVERIFY(windowA->isVisible());
        emit floatingA->tabCloseRequested(floatingA->indexOf(editorA));
        QCOMPARE(reviews, 2);
        QTRY_VERIFY(floatingA.isNull());
        QCOMPARE(f.tabs.editorCount(), 1);
        QVERIFY(f.tabs.openEditors().contains(temporary));
    }

    void dragDropUsesElaAndRejectsForeignScope_data() {
        QTest::addColumn<int>("area");
        QTest::newRow("center") << 0;
        QTest::newRow("left") << 1;
        QTest::newRow("right") << 2;
        QTest::newRow("top") << 3;
        QTest::newRow("bottom") << 4;
    }
    void dragDropUsesElaAndRejectsForeignScope() {
        QFETCH(int, area);
        QWidget host;
        auto* initial = UiControls::editorTabWidget(&host);
        EditorSplitController split(initial);
        split.setHost(&host);
        auto* page = new QWidget;
        initial->addTab(page, "source");
        auto* second = new QWidget;
        initial->addTab(second, "keep");
        auto* destination = split.createSplit(initial, EditorSplitDirection::Right);
        QVERIFY(destination);
        destination->addTab(new QWidget, "destination");
        host.resize(1000, 700); host.show();
        QApplication::processEvents();
        QSignalSpy moved(&split, &EditorSplitController::pageMoved);
        bool started = false, entered = false, accepted = false;
        // The offscreen plugin does not run a native drag loop. Drive the real
        // Ela gesture, then deliver Qt drop events carrying its guarded MIME.
        connect(qobject_cast<ElaTabWidget*>(initial), &ElaTabWidget::hostedTabDragStarted,
                &host, [&](QDrag* drag) {
            started = true;
            QPoint target = destination->rect().center();
            if (area == 1) target.setX(8);
            if (area == 2) target.setX(destination->width() - 8);
            if (area == 3) target.setY(destination->tabBar()->height() + 8);
            if (area == 4) target.setY(destination->height() - 8);
            QDragEnterEvent enter(target, Qt::MoveAction, drag->mimeData(), Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(destination, &enter);
            entered = enter.isAccepted();
            QDropEvent drop(target, Qt::MoveAction, drag->mimeData(), Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(destination, &drop);
            accepted = drop.isAccepted();
        });
        QTabBar* bar = initial->tabBar();
        const QPoint press = bar->tabRect(0).center();
        QTest::mousePress(bar, Qt::LeftButton, Qt::NoModifier, press);
        sendMouse(bar, QEvent::MouseMove, press + QPoint(0, 80), Qt::NoButton, Qt::LeftButton);
        QTest::mouseRelease(bar, Qt::LeftButton, Qt::NoModifier, press);
        QVERIFY(started);
        QVERIFY(entered);
        QVERIFY(accepted);
        QCOMPARE(moved.count(), 1);
        QCOMPARE(split.groupCount(), area == 0 ? 2 : 3);
        QVERIFY(split.groupForPage(page) != initial);
        QCOMPARE(initial->widget(0), second);

        QWidget otherHost;
        auto* other = UiControls::editorTabWidget(&otherHost);
        EditorSplitController otherSplit(other);
        otherSplit.setHost(&otherHost);
        auto* source = qobject_cast<ElaTabWidget*>(split.groupForPage(page));
        QVERIFY(!source->transferHostedTab(page, qobject_cast<ElaTabWidget*>(other)));
        QVERIFY(source->indexOf(page) >= 0);
        QCOMPARE(other->count(), 0);
    }

    void emptyDragSourceLivesUntilCompletion() {
        QWidget host;
        QPointer<QTabWidget> initial = UiControls::editorTabWidget(&host);
        EditorSplitController split(initial);
        split.setHost(&host);
        auto* page = new QWidget;
        initial->addTab(page, "last source tab");
        auto* destination = split.createSplit(initial, EditorSplitDirection::Right);
        destination->addTab(new QWidget, "destination");
        host.resize(1000, 700); host.show();
        QApplication::processEvents();
        bool sourceAliveDuringDrop = false;
        connect(qobject_cast<ElaTabWidget*>(initial), &ElaTabWidget::hostedTabDragStarted,
                &host, [&](QDrag* drag) {
            const auto position = destination->rect().center();
            QDragEnterEvent enter(position, Qt::MoveAction, drag->mimeData(), Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(destination, &enter);
            QDropEvent drop(position, Qt::MoveAction, drag->mimeData(), Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(destination, &drop);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            sourceAliveDuringDrop = initial && split.groups().contains(initial);
        });
        QPointer<QTabBar> bar = initial->tabBar();
        const QPoint press = bar->tabRect(0).center();
        QTest::mousePress(bar, Qt::LeftButton, Qt::NoModifier, press);
        sendMouse(bar, QEvent::MouseMove, press + QPoint(0, 80), Qt::NoButton, Qt::LeftButton);
        if (bar) QTest::mouseRelease(bar, Qt::LeftButton, Qt::NoModifier, press);
        QVERIFY(sourceAliveDuringDrop);
        QTRY_VERIFY(initial.isNull());
        QCOMPARE(split.groupCount(), 1);
        QCOMPARE(split.groupForPage(page), destination);
        QVERIFY(destination->isVisible());
    }

    void escapeKeepsSourceAndContextUsesEla() {
        QWidget host;
        auto* initial = UiControls::editorTabWidget(&host);
        EditorSplitController split(initial);
        split.setHost(&host);
        auto* page = new QWidget;
        initial->addTab(page, "cancel");
        host.resize(800, 600); host.show();
        QApplication::processEvents();
        QSignalSpy created(&split, &EditorSplitController::groupCreated);
        bool started = false;
        connect(qobject_cast<ElaTabWidget*>(initial), &ElaTabWidget::hostedTabDragStarted,
                &host, [&](QDrag*) {
            started = true;
            QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
            QApplication::sendEvent(initial->tabBar(), &escape);
        });
        auto* bar = initial->tabBar();
        const QPoint press = bar->tabRect(0).center();
        QTest::mousePress(bar, Qt::LeftButton, Qt::NoModifier, press);
        sendMouse(bar, QEvent::MouseMove, press + QPoint(0, 80), Qt::NoButton, Qt::LeftButton);
        QTest::mouseRelease(bar, Qt::LeftButton, Qt::NoModifier, press);
        QVERIFY(started);
        QCOMPARE(created.count(), 0);
        QCOMPARE(initial->widget(0), page);
        ContextFloatingWindow floating(&host, &host);
        auto* appBar = floating.findChild<ElaAppBar*>();
        QVERIFY(appBar);
        QCOMPARE(floating.titleBar(), appBar);
        QVERIFY(appBar->isWindowMoveTrackingEnabled());
        QSignalSpy closed(&floating, &ContextFloatingWindow::closeRequested);
        appBar->windowButton(ElaAppBarType::CloseButtonHint)->click();
        QCOMPARE(closed.count(), 1);
    }
};

int main(int argc, char** argv) {
    QStandardPaths::setTestModeEnabled(true);
    QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
    QTemporaryDir profile;
    if (!profile.isValid()) return 2;
    QCoreApplication::setApplicationName("ZeroSlack-Ela-Document-Tabs-Test");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, profile.path());
    qputenv("ZEROSLACK_SESSION_STORAGE_PATH", (profile.path() + "/sessions.ini").toUtf8());
    auto& theme = ApplicationThemeManager::instance();
    if (!theme.selectBackend(UiStyleBackend::Ela)) return 3;
    theme.applyToApplication();
    ElaDocumentTabsTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "ela_document_tabs_test.moc"
