#include "applicationthememanager.h"
#include "contextfloatingwindow.h"
#include "testuistyle.h"

#include <QAbstractButton>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QSignalSpy>
#include <QTest>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>
#include <QMouseEvent>
#ifdef ZEROSLACK_ENABLE_ELA
#include "ElaApplication.h"
#endif
#ifdef Q_OS_WIN
#include <QOperatingSystemVersion>
#include <qt_windows.h>
#include <dwmapi.h>
#endif

namespace {
ContextResource resource()
{
    ContextResource result;
    result.providerId = QStringLiteral("test");
    result.resourceId = QStringLiteral("module");
    result.uri = QUrl(QStringLiteral("test:/module"));
    result.title = QStringLiteral("Module Block Diagram: axi_uart_wrapper");
    return result;
}

QWidget* content()
{
    auto* view = new QWidget;
    auto* layout = new QVBoxLayout(view);
    layout->addWidget(new QLabel(QStringLiteral("axi_uart_wrapper"), view));
    layout->addWidget(new QLineEdit(QStringLiteral("retained content"), view));
    layout->addStretch();
    return view;
}
}

class ContextFloatingWindowTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QVERIFY(initializeUiStyleForTest());
        ApplicationThemeManager::instance().applyToApplication();
    }

    void ownerAndContentRemainIndependent()
    {
        QMainWindow owner;
        owner.setCentralWidget(new QWidget(&owner));
        owner.setContentsMargins(3, 17, 5, 7);
        owner.setWindowTitle(QStringLiteral("Main window"));
        owner.resize(700, 500);
        owner.show();
        const auto ownerFlags = owner.windowFlags();
        const auto ownerMargins = owner.contentsMargins();
        const auto ownerFrame = owner.frameGeometry();
        ContextFloatingWindow host(&owner, owner.centralWidget());
        QVERIFY(host.isWindow());
        QCOMPARE(host.windowType(), Qt::Tool);
        QCOMPARE(host.parentWidget(), &owner);
        QCOMPARE(host.windowModality(), Qt::NonModal);
        QVERIFY(!host.testAttribute(Qt::WA_DeleteOnClose));
        QCOMPARE(owner.windowFlags(), ownerFlags);
        QCOMPARE(owner.contentsMargins(), ownerMargins);
        QCOMPARE(owner.frameGeometry(), ownerFrame);
        QCOMPARE(owner.windowTitle(), QStringLiteral("Main window"));
#ifdef ZEROSLACK_ENABLE_ELA
        QVERIFY(qobject_cast<ElaDockWidget*>(&host));
        QVERIFY(host.windowFlags().testFlag(Qt::FramelessWindowHint));
        QCOMPARE(host.titleBarWidget(), host.titleBar());
        QVERIFY(owner.dockOptions().testFlag(QMainWindow::AnimatedDocks));
#endif
        QWidget* view = content();
        host.setView(resource(), view);
        QApplication::processEvents();
        QVERIFY(host.isVisible());
        QCOMPARE(host.windowHandle()->transientParent(), owner.windowHandle());
        QCOMPARE(host.windowTitle(), resource().title);
        QCOMPARE(view->window(), &host);
        QVERIFY(owner.isEnabled());

        QVERIFY(!host.findChild<QToolButton*>(QStringLiteral("contextFloatingPin")));
        QVERIFY(!host.findChild<QToolButton*>(QStringLiteral("contextFloatingFullView")));
        QVERIFY(!host.findChild<QToolButton*>(QStringLiteral("contextFloatingDrag")));
        host.setActionsAvailable(false, false);
        QVERIFY(!host.canDock());
        QVERIFY(host.titleBar()->isEnabled());
        host.setActionsAvailable(true, true);
        QVERIFY(host.canDock());

        QCOMPARE(host.takeView(), view);
        QVERIFY(!host.hasResource() && !host.isVisible());
        QVERIFY(!view->parentWidget());
        view->setParent(owner.centralWidget());
        QCOMPARE(view->findChild<QLineEdit*>()->text(), QStringLiteral("retained content"));
        host.setView(resource(), view);
        QCOMPARE(host.view(), view);
        QCOMPARE(owner.contentsMargins(), ownerMargins);
    }

    void closeUsesResourceLifecycle()
    {
        ContextFloatingWindow host(nullptr, nullptr);
        QPointer<QWidget> view = content();
        host.setView(resource(), view);
        QSignalSpy closed(&host, &ContextFloatingWindow::closeRequested);
        const auto closeWindow = [&] {
#ifdef ZEROSLACK_ENABLE_ELA
            host.findChild<QToolButton*>(QStringLiteral("contextFloatingClose"))->click();
#else
            host.close();
#endif
        };
        closeWindow();
        QCOMPARE(closed.count(), 1);
        QVERIFY(host.isVisible() && host.hasResource() && view);
        connect(&host, &ContextFloatingWindow::closeRequested, &host, &ContextFloatingWindow::clearView);
        closeWindow();
        QCOMPARE(closed.count(), 2);
        QVERIFY(!host.isVisible() && !host.hasResource());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(view.isNull());
        host.setView(resource(), content());
        QVERIFY(host.isVisible() && host.hasResource());
        host.close();
        QCOMPARE(closed.count(), 3);
        QVERIFY(!host.isVisible() && !host.hasResource());
        host.setView(resource(), content());
        auto* edit = host.view()->findChild<QLineEdit*>();
        host.activateWindow();
        edit->setFocus();
        QApplication::processEvents();
        QTest::keyClick(edit, Qt::Key_Escape);
        QCOMPARE(closed.count(), 4);
        QVERIFY(!host.hasResource());
    }

    void singleTitleBar_data()
    {
        QTest::addColumn<int>("width");
        for (int width : {280, 360, 580, 920})
            QTest::newRow(qPrintable(QString::number(width))) << width;
    }

    void singleTitleBar()
    {
#ifdef ZEROSLACK_ENABLE_ELA
        QFETCH(int, width);
        ContextFloatingWindow host(nullptr, nullptr);
        QWidget* view = content();
        const QString longTitle = QStringLiteral("axi_uart_wrapper_").repeated(10) + QStringLiteral(".sv*");
        view->setProperty("contextDisplayTitle", longTitle);
        host.setView(resource(), view);
        host.resize(width, 400);
        auto* bar = host.titleBar();
        auto* titleLabel = bar->findChild<QLabel*>();
        QVERIFY(bar);
        QPixmap icon(18, 18);
        icon.fill(Qt::red);
        host.setWindowIcon(QIcon(icon));
        QVERIFY(titleLabel);
        for (const bool fit : {true, false}) {
            view->setProperty("contextFitAvailable", fit);
            for (const bool full : {true, false}) {
                host.setActionsAvailable(true, full);
                QApplication::processEvents();
                QCOMPARE(host.width(), qBound(host.minimumWidth(), width, host.maximumWidth()));
                QCOMPARE(host.windowTitle(), longTitle);
                QCOMPARE(titleLabel->toolTip(), longTitle);
                QVERIFY(titleLabel->text() != longTitle);
                QList<QRect> occupied;
                for (auto* button : bar->findChildren<QAbstractButton*>()) {
                    if (!button->isVisible()) continue;
                    const QRect bounds(button->mapTo(bar, QPoint()), button->size());
                    QVERIFY2(bar->rect().contains(bounds), qPrintable(button->objectName()));
                    QVERIFY2(button->width() >= button->minimumSizeHint().width(), qPrintable(button->objectName()));
                    QVERIFY2(button->height() >= button->minimumSizeHint().height(), qPrintable(button->objectName()));
                    for (const QRect& other : occupied) QVERIFY(!other.intersects(bounds));
                    occupied.append(bounds);
                }
                const QRect titleBounds(titleLabel->mapTo(bar, QPoint()), titleLabel->size());
                for (const QRect& other : occupied) QVERIFY(!other.intersects(titleBounds));
                auto* fitButton = host.findChild<QToolButton*>(QStringLiteral("contextFloatingFit"));
                QCOMPARE(fitButton->isVisible(), fit);
                QCOMPARE(occupied.size(), fit ? 2 : 1);
                QCOMPARE(host.titleBar(), bar);
                // The view starts directly after the one title bar, without another action row.
                const int gap = view->mapTo(&host, QPoint()).y() - bar->geometry().bottom() - 1;
                QVERIFY(gap >= 0 && gap <= 2);
                QVERIFY(host.rect().contains(QRect(view->mapTo(&host, QPoint()), view->size())));
            }
        }
        view->setProperty("contextDisplayTitle", QStringLiteral("uart_top.sv*"));
        QCOMPARE(host.windowTitle(), QStringLiteral("uart_top.sv*"));
        view->setProperty("contextDisplayTitle", QVariant());
        QCOMPARE(host.windowTitle(), resource().title);
#endif
    }

    void windowControlsAndGeometry()
    {
        QMainWindow owner;
        owner.resize(700, 500);
        owner.show();
        ContextFloatingWindow host(&owner, nullptr);
        host.setView(resource(), content());
        QApplication::processEvents();
        const auto normalFrame = host.frameGeometry();
#ifdef ZEROSLACK_ENABLE_ELA
        auto* title = host.titleBar();
        QVERIFY(title);
        auto* button = title->findChild<QToolButton*>("contextFloatingClose");
        QVERIFY(button && button->isVisible());
        QVERIFY(title->rect().contains(QRect(button->mapTo(title, QPoint()), button->size())));
        const QRect viewRect(host.view()->mapTo(&host, QPoint()), host.view()->size());
        QVERIFY(host.rect().contains(viewRect));
        QVERIFY(viewRect.top() >= title->geometry().bottom());
        // QDockWidget's title double-click docks/undocks; it does not maximize.
        for (int i = 0; i < 3; ++i) {
            host.setFloating(false);
            QVERIFY(!host.isFloating()); QVERIFY(!owner.isMaximized());
            host.setFloating(true);
            QApplication::processEvents();
            QVERIFY(host.isFloating());
        }
        host.setGeometry(normalFrame);
#endif
        ContextWorkspaceState saved;
        host.captureGeometry(saved);
        QCOMPARE(QRect(saved.floatingX, saved.floatingY, saved.floatingWidth, saved.floatingHeight), normalFrame);
        ContextFloatingWindow restored(&owner, nullptr);
        restored.restoreGeometry(saved);
        restored.setView(resource(), content());
        QCOMPARE(restored.frameGeometry(), normalFrame);
        QCOMPARE(restored.backgroundOpacity(), 90);
        QCOMPARE(restored.windowOpacity(), 1.0);
    }

    void nativeDockGestureAndCancel()
    {
#ifdef ZEROSLACK_ENABLE_ELA
        QMainWindow owner;
        owner.setCentralWidget(new QWidget);
        owner.resize(1100, 750); owner.show();
        ContextFloatingWindow host(&owner, owner.centralWidget());
        QWidget* view = content();
        host.setView(resource(), view);
        QTest::qWait(40);
        QSignalSpy started(&host, &ContextFloatingWindow::titleDragStarted);
        QSignalSpy finished(&host, &ContextFloatingWindow::titleDragFinished);
        QSignalSpy docked(&host, &ContextFloatingWindow::nativeDocked);
        const QPoint origin = host.mapToGlobal(QPoint(24, 16));
        host.beginNativeDockDrag(origin);
        QVERIFY(host.isDockDragging());
        QCOMPARE(started.count(), 1);
        QTest::keyClick(&host, Qt::Key_Escape);
        QVERIFY(!host.isDockDragging());
        QCOMPARE(finished.count(), 1);
        QVERIFY(finished.last().at(1).toBool());
        QCOMPARE(host.view(), view); QVERIFY(host.isFloating());
        QTest::qWait(250);
        host.beginNativeDockDrag(origin);
        QVERIFY(host.isDockDragging());
        const QPoint destination = owner.centralWidget()->mapToGlobal(
            QPoint(owner.centralWidget()->width() - 2, owner.centralWidget()->height() / 2));
        QMouseEvent move(QEvent::MouseMove, host.mapFromGlobal(destination), destination,
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&host, &move);
        QMouseEvent release(QEvent::MouseButtonRelease, host.mapFromGlobal(destination), destination,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&host, &release);
        QTRY_VERIFY(!host.isDockDragging());
        QTRY_VERIFY(!host.isFloating());
        QTRY_COMPARE(docked.count(), 1);
        QCOMPARE(qvariant_cast<Qt::DockWidgetArea>(docked.first().first()), Qt::RightDockWidgetArea);
        QCOMPARE(host.view(), view);
        QCOMPARE(view->findChild<QLineEdit*>()->text(), QString("retained content"));
        host.setFloating(true);
        host.beginNativeDockDrag(host.mapToGlobal(QPoint(24, 16)));
        QWidget* retained = host.takeView();
        QVERIFY(!host.isDockDragging()); QCOMPARE(retained, view);
        delete retained;
#endif
    }

    void nativeFramelessGeometryAndHitTesting()
    {
#if defined(ZEROSLACK_ENABLE_ELA) && defined(Q_OS_WIN)
        if (QGuiApplication::platformName() != QStringLiteral("windows"))
            QSKIP("Native Windows platform required");
        const HWND foreground = GetForegroundWindow();
        class DockProbe : public ElaDockWidget {
        public: using ElaDockWidget::nativeEvent;
        } host;
        host.setGeometry(-16000, -16000, 600, 400);
        QApplication::processEvents();
        const auto hwnd = reinterpret_cast<HWND>(host.winId());
        QVERIFY(!IsWindowVisible(hwnd));

        NCCALCSIZE_PARAMS geometry{};
        geometry.rgrc[0] = RECT{100, 100, 700, 500};
        MSG message{};
        message.hwnd = hwnd;
        message.message = WM_NCCALCSIZE;
        message.wParam = TRUE;
        message.lParam = reinterpret_cast<LPARAM>(&geometry);
        qintptr result = 0;
        QVERIFY(host.nativeEvent("windows_generic_MSG", &message, &result));
        QCOMPARE(geometry.rgrc[0].left, LONG(100));
        QCOMPARE(geometry.rgrc[0].top, LONG(100));
        QCOMPARE(geometry.rgrc[0].right, LONG(700));
        QCOMPARE(geometry.rgrc[0].bottom, LONG(500));

        RECT client{};
        QVERIFY(GetClientRect(hwnd, &client));
        const LONG right = client.right - 1;
        const LONG bottom = client.bottom - 1;
        const QList<QPair<POINT, int>> edges{
            {{1, 1}, HTTOPLEFT}, {{right, 1}, HTTOPRIGHT},
            {{1, bottom}, HTBOTTOMLEFT}, {{right, bottom}, HTBOTTOMRIGHT},
            {{1, bottom / 2}, HTLEFT}, {{right, bottom / 2}, HTRIGHT},
            {{right / 2, 1}, HTTOP}, {{right / 2, bottom}, HTBOTTOM},
            {{right / 2, bottom / 2}, HTCLIENT}
        };
        for (const auto& edge : edges) {
            POINT point = edge.first;
            QVERIFY(ClientToScreen(hwnd, &point));
            message.message = WM_NCHITTEST;
            message.wParam = 0;
            message.lParam = MAKELPARAM(point.x, point.y);
            result = 0;
            QVERIFY(host.nativeEvent("windows_generic_MSG", &message, &result));
            QCOMPARE(result, qintptr(edge.second));
        }
        QVERIFY(!IsWindowVisible(hwnd));
        QCOMPARE(GetForegroundWindow(), foreground);
#else
        QSKIP("Ela on Windows required");
#endif
    }

    void acrylicIsWindowScoped()
    {
#if defined(ZEROSLACK_ENABLE_ELA) && defined(Q_OS_WIN)
        using namespace ElaApplicationType;
        const auto globalMode = eApp->getWindowDisplayMode();
        QWidget probe;
        probe.winId();
        if (QGuiApplication::platformName() != QStringLiteral("windows")) {
            QVERIFY(!eApp->applyWindowDisplayMode(&probe, Acrylic, Normal));
            ContextFloatingWindow host(nullptr, nullptr);
            QVERIFY(!host.hasAcrylicBackdrop());
            QCOMPARE(eApp->getWindowDisplayMode(), globalMode);
            return;
        }

        // Native API verification uses hidden HWNDs only: no show(), mouse or focus changes.
        constexpr DWORD backdropAttribute = 38;
        const auto hwnd = reinterpret_cast<HWND>(probe.winId());
        QWidget untouched;
        const auto untouchedHwnd = reinterpret_cast<HWND>(untouched.winId());
        DWORD untouchedMode = 0;
        const HRESULT untouchedResult = DwmGetWindowAttribute(untouchedHwnd, backdropAttribute,
            &untouchedMode, sizeof(untouchedMode));
        BOOL composition = FALSE;
        const bool supported = QOperatingSystemVersion::current()
                >= QOperatingSystemVersion(QOperatingSystemVersion::Windows, 10, 0, 22621)
            && SUCCEEDED(DwmIsCompositionEnabled(&composition)) && composition;
        const bool applied = eApp->applyWindowDisplayMode(&probe, Acrylic, Normal);
        QCOMPARE(applied, supported);
        if (applied) {
            DWORD actual = 0;
            QVERIFY(SUCCEEDED(DwmGetWindowAttribute(hwnd, backdropAttribute, &actual, sizeof(actual))));
            QCOMPARE(actual, DWORD(3));
        }
        QVERIFY(eApp->applyWindowDisplayMode(&probe, Normal, Acrylic));
        if (supported) {
            DWORD actual = 0;
            QVERIFY(SUCCEEDED(DwmGetWindowAttribute(hwnd, backdropAttribute, &actual, sizeof(actual))));
            QCOMPARE(actual, DWORD(1));
        }

        ContextFloatingWindow host(nullptr, nullptr);
        const auto originalTheme = ApplicationThemeManager::instance().mode();
        const auto handle = reinterpret_cast<HWND>(host.winId());
        for (const auto theme : {ThemeMode::Light, ThemeMode::Dark, ThemeMode::CatppuccinMocha}) {
            ApplicationThemeManager::instance().setMode(theme);
            QApplication::processEvents();
            if (supported) {
                DWORD actual = 0;
                QVERIFY(SUCCEEDED(DwmGetWindowAttribute(handle, backdropAttribute, &actual, sizeof(actual))));
                QCOMPARE(actual, host.hasAcrylicBackdrop() ? DWORD(3) : DWORD(1));
                const auto nativeDark = [handle] {
                    BOOL dark = FALSE;
                    return SUCCEEDED(DwmGetWindowAttribute(handle, 20, &dark, sizeof(dark)))
                        ? int(bool(dark)) : -1;
                };
                QTRY_COMPARE_WITH_TIMEOUT(nativeDark(), int(isDarkTheme(theme)), 1500);
            }
            QCOMPARE(host.winId(), WId(handle));
            QCOMPARE(host.windowOpacity(), 1.0);
            QVERIFY(!IsWindowVisible(handle));
        }
        ApplicationThemeManager::instance().setMode(originalTheme);
        DWORD stillUntouched = 0;
        QCOMPARE(DwmGetWindowAttribute(untouchedHwnd, backdropAttribute, &stillUntouched, sizeof(stillUntouched)),
                 untouchedResult);
        QCOMPARE(stillUntouched, untouchedMode);
        QCOMPARE(eApp->getWindowDisplayMode(), globalMode);
        QVERIFY(!IsWindowVisible(hwnd));
        QVERIFY(!IsWindowVisible(untouchedHwnd));
#else
        ContextFloatingWindow host(nullptr, nullptr);
        if (QGuiApplication::platformName() != QStringLiteral("windows"))
            QVERIFY(!host.hasAcrylicBackdrop());
#endif
    }
};

QTEST_MAIN(ContextFloatingWindowTest)
#include "context_floating_window_test.moc"
