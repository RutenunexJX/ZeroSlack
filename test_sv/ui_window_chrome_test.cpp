#include "applicationthememanager.h"
#include "mainwindow.h"
#include "mycodeeditor.h"
#include "navigationpanecoordinator.h"
#include "tabmanager.h"
#include "testuistyle.h"
#include "uiwindowchrome.h"
#include "workspacechrome.h"
#include <QAbstractAnimation>
#include <QClipboard>
#include <QCloseEvent>
#include <QEnterEvent>
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

namespace {
void settle() { QApplication::processEvents(); QTest::qWait(30); }
bool usesEla() { return ApplicationThemeManager::instance().backend() == UiStyleBackend::Ela; }
QAbstractButton* button(QWidget* host, const char* name) {
    return host->findChild<QAbstractButton*>(QLatin1String(name));
}
class CloseVetoWindow final : public QMainWindow {
public:
    bool allowClose = false;
    int attempts = 0;
protected:
    void closeEvent(QCloseEvent* event) override {
        ++attempts;
        event->setAccepted(allowClose);
    }
};
}

class UiWindowChromeTest final : public QObject {
    Q_OBJECT
private slots:
    void hostOwnershipAndCloseVeto() {
        CloseVetoWindow host;
        host.setWindowTitle("ZeroSlack");
        host.setContentsMargins(3, 42, 5, 6);
        const auto flags = host.windowFlags();
        const auto margins = host.contentsMargins();
        auto title = UiWindowChrome::createTitleBar(&host);
        QCOMPARE(title.widget->inherits("ElaAppBar"), usesEla());
        QCOMPARE(host.windowFlags(), flags);
        QCOMPARE(host.contentsMargins(), margins);
        host.resize(720, 420);
        title.widget->resize(720, title.widget->height());
        host.show(); settle();
        QCOMPARE(host.contentsMargins(), margins);
        auto* close = button(&host, "windowCloseButton"); QVERIFY(close);
        QTest::mouseClick(close, Qt::LeftButton); settle();
        QCOMPARE(host.attempts, 1);
        QVERIFY(host.isVisible()); QVERIFY(host.windowHandle()->isVisible());
        host.allowClose = true;
        close->setFocus(); QTest::keyClick(close, Qt::Key_Space); settle();
        QCOMPARE(host.attempts, 2); QVERIFY(!host.isVisible());
    }

    void titleGeometryStateAndNativeHover() {
        QMainWindow host;
        host.setCentralWidget(new QWidget(&host));
        auto* pane = new NavigationPaneCoordinator(&host);
        host.addDockWidget(Qt::LeftDockWidgetArea, pane->dock());
        host.setWindowTitle("ZeroSlack");
        new WorkspaceChrome(&host, pane, [] {});
        host.resize(800, 520); host.show(); settle();
        auto* title = host.findChild<QWidget*>("workspaceTitleBar"); QVERIFY(title);
        auto* label = host.findChild<QLabel*>("workspaceFilePath"); QVERIFY(label);
        auto* minimize = button(&host, "windowMinimizeButton"); QVERIFY(minimize);
        auto* maximize = button(&host, "windowMaximizeButton"); QVERIFY(maximize);
        auto* close = button(&host, "windowCloseButton"); QVERIFY(close);
        QVERIFY(label->width() >= label->fontMetrics().horizontalAdvance(host.windowTitle()));
        QVERIFY(!minimize->icon().isNull()); QVERIFY(!maximize->icon().isNull()); QVERIFY(!close->icon().isNull());
        if (usesEla()) {
            QVERIFY(minimize->inherits("ElaToolButton"));
            QVERIFY(maximize->inherits("ElaToolButton"));
            QVERIFY(close->inherits("ElaIconButton"));
        }
        pane->setExpanded(false); QTest::qWait(250); settle();
        auto* expand = button(&host, "expandProjectSidebarButton"); QVERIFY(expand);
        QVERIFY(expand->isVisible());
        QTest::mouseClick(expand, Qt::LeftButton); QTest::qWait(250); settle();
        QVERIFY(pane->dock()->isVisible()); QVERIFY(!expand->isVisible());
        host.setWindowTitle(QString("C:/long directory/%1/uart_engine.sv").arg(QString(200, 'x')));
        for (int width : {480, 800, 1200}) {
            host.resize(width, 520); settle();
            QCOMPARE(label->text(), host.windowTitle());
            QCOMPARE(title->geometry(), QRect(4, 4, host.width() - 8, title->height()));
            QCOMPARE(host.contentsMargins().top(), title->height() + 4);
            QList<QRect> occupied;
            for (QWidget* control : {static_cast<QWidget*>(label), static_cast<QWidget*>(minimize),
                                    static_cast<QWidget*>(maximize), static_cast<QWidget*>(close)}) {
                const QRect rect(control->mapTo(title, QPoint()), control->size());
                QVERIFY2(title->rect().contains(rect), qPrintable(control->objectName()));
                if (control != label) {
                    QVERIFY(control->width() >= control->minimumSizeHint().width());
                    QVERIFY(control->height() >= control->minimumSizeHint().height());
                }
                for (const auto& other : occupied) QVERIFY(!rect.intersects(other));
                occupied.append(rect);
            }
        }
        for (int i = 0; i < 3; ++i) {
            QTest::mouseClick(maximize, Qt::LeftButton); settle(); QVERIFY(host.isMaximized());
            const auto restoreIcon = maximize->icon().cacheKey();
            QTest::mouseClick(maximize, Qt::LeftButton); settle(); QVERIFY(!host.isMaximized());
            if (usesEla()) QVERIFY(restoreIcon != maximize->icon().cacheKey());
            QTest::mouseClick(minimize, Qt::LeftButton); settle(); QVERIFY(host.isMinimized());
            host.showNormal(); settle(); QVERIFY(!host.isMinimized());
        }
        QTest::mouseDClick(label, Qt::LeftButton); settle(); QVERIFY(host.isMaximized());
        QTest::mouseDClick(label, Qt::LeftButton); settle(); QVERIFY(!host.isMaximized());
        if (usesEla()) {
            maximize->clearFocus();
            QEvent leave(QEvent::Leave); QApplication::sendEvent(maximize, &leave);
            maximize->setAttribute(Qt::WA_UnderMouse, false);
            const QImage rest = maximize->grab().toImage();
            maximize->setProperty("nativeHovered", true); maximize->update(); settle();
            QVERIFY(maximize->grab().toImage() != rest);
            maximize->setProperty("nativeHovered", false);
        }
    }

    void pathMenuAndThemes() {
        QTemporaryDir files; QVERIFY(files.isValid());
        const auto path = files.filePath("with spaces & marks.sv");
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("module top; endmodule\n"); file.close();
        QMainWindow host;
        host.setWindowFilePath(path); host.setWindowTitle(path);
        new WorkspaceChrome(&host, nullptr, [] {});
        host.resize(720, 360); host.show(); settle();
        auto* label = host.findChild<QLabel*>("workspaceFilePath"); QVERIFY(label);
        QVERIFY(label->toolTip().contains(path));
        const auto originalClipboard = QApplication::clipboard()->text();
        bool menuChecked = false;
        QTimer::singleShot(40, &host, [&] {
            auto* popup = qobject_cast<QMenu*>(QApplication::activePopupWidget());
            if (!popup) return;
            menuChecked = popup->actions().size() == 2 && popup->actions()[0]->isEnabled()
                && popup->actions()[1]->isEnabled();
            popup->setActiveAction(popup->actions().first());
            QTest::keyClick(popup, Qt::Key_Return);
        });
        QTimer::singleShot(1500, &host, [] { if (auto* popup = QApplication::activePopupWidget()) popup->close(); });
        QMetaObject::invokeMethod(label, "customContextMenuRequested", Q_ARG(QPoint, label->rect().center()));
        QVERIFY(menuChecked);
        QCOMPARE(QApplication::clipboard()->text(), QDir::toNativeSeparators(path));
        QApplication::clipboard()->setText(originalClipboard);
        const auto labelFont = label->font();
        auto* close = button(&host, "windowCloseButton"); QVERIFY(close);
        QImage previous;
        for (auto mode : {ThemeMode::Light, ThemeMode::Dark, ThemeMode::CatppuccinLatte, ThemeMode::CatppuccinMocha}) {
            ApplicationThemeManager::instance().setMode(mode); settle();
            QCOMPARE(label->font(), labelFont);
            const auto image = close->grab().toImage();
            QVERIFY(!image.isNull());
            if (!previous.isNull()) QVERIFY(image != previous);
            previous = image;
        }
        ApplicationThemeManager::instance().setMode(ThemeMode::Light);
    }

    void actualUnsavedDocumentClose() {
        QTemporaryDir files; QVERIFY(files.isValid());
        const auto path = files.filePath("close_guard.sv");
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray source("module close_guard; endmodule\n");
        QCOMPARE(file.write(source), source.size()); file.close();
        MainWindow host;
        host.resize(1000, 650); host.show(); settle();
        QVERIFY(host.tabManager->openFileInTab(path));
        auto* editor = host.tabManager->getCurrentEditor(); QVERIFY(editor);
        const auto font = editor->font();
        editor->insertPlainText("// unsaved\n");
        QVERIFY(editor->document()->isModified());
        const auto modified = editor->toPlainText();
        int decisions = 0;
        auto answer = UnsavedDocumentBatchDecision::Cancel;
        host.tabManager->unsavedDocumentManagerForTesting()->setDecisionProvider(
            [&](const QList<PendingDocumentChange>& pending, QWidget*) {
                if (!pending.isEmpty()) ++decisions;
                return answer;
            });
        auto* close = button(&host, "windowCloseButton"); QVERIFY(close);
        QTest::mouseClick(close, Qt::LeftButton); settle();
        QCOMPARE(decisions, 1); QVERIFY(host.isVisible());
        QCOMPARE(editor->toPlainText(), modified); QCOMPARE(editor->font(), font);
        answer = UnsavedDocumentBatchDecision::DiscardAll;
        QTest::mouseClick(close, Qt::LeftButton); settle();
        QCOMPARE(decisions, 2); QVERIFY(!host.isVisible());
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), source);
    }

    void visibleDestructionDuringHover() {
        for (int i = 0; i < 4; ++i) {
            QPointer<CloseVetoWindow> host = new CloseVetoWindow;
            host->allowClose = true;
            host->setAttribute(Qt::WA_DeleteOnClose);
            host->setWindowTitle("Close during hover");
            auto title = UiWindowChrome::createTitleBar(host);
            host->resize(600, 300); title.widget->resize(600, title.widget->height()); host->show(); settle();
            auto* close = button(host, "windowCloseButton"); QVERIFY(close);
            QEnterEvent enter(QPointF(5, 5), QPointF(5, 5), QPointF(close->mapToGlobal(QPoint(5, 5))));
            QApplication::sendEvent(close, &enter);
            QList<QPointer<QAbstractAnimation>> animations;
            for (auto* animation : title.widget->findChildren<QAbstractAnimation*>()) animations.append(animation);
            close->click();
            QTRY_VERIFY(host.isNull());
            for (const auto& animation : animations) QVERIFY(animation.isNull());
        }
    }
};

int main(int argc, char** argv) {
    QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
    QTemporaryDir profile; if (!profile.isValid()) return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, profile.path());
    qputenv("ZEROSLACK_SESSION_STORAGE_PATH", (profile.path() + "/sessions.ini").toUtf8());
    if (!initializeUiStyleForTest()) return 3;
    ApplicationThemeManager::instance().applyToApplication();
    UiWindowChromeTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "ui_window_chrome_test.moc"
