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
        QVERIFY(qobject_cast<ElaWidget*>(&host));
        QVERIFY(!host.getIsStayTop());
        QCOMPARE(host.findChildren<ElaAppBar*>(QString(), Qt::FindDirectChildrenOnly).size(), 1);
        QVERIFY(owner.findChildren<ElaAppBar*>(QString(), Qt::FindDirectChildrenOnly).isEmpty());
#endif
        QWidget* view = content();
        host.setView(resource(), view);
        QApplication::processEvents();
        QVERIFY(host.isVisible());
        QCOMPARE(host.windowHandle()->transientParent(), owner.windowHandle());
        QCOMPARE(host.windowTitle(), resource().title);
        QCOMPARE(view->window(), &host);
        QVERIFY(owner.isEnabled());

        auto* pin = host.findChild<QToolButton*>(QStringLiteral("contextFloatingPin"));
        auto* fullView = host.findChild<QToolButton*>(QStringLiteral("contextFloatingFullView"));
        QVERIFY(pin && fullView);
        QSignalSpy pinSpy(&host, &ContextFloatingWindow::pinRequested);
        QSignalSpy fullViewSpy(&host, &ContextFloatingWindow::fullViewRequested);
        pin->click();
        fullView->click();
        QCOMPARE(pinSpy.count(), 1);
        QCOMPARE(fullViewSpy.count(), 1);
        host.setActionsAvailable(false, false);
        QVERIFY(pin->isHidden() && fullView->isHidden());
        QVERIFY(!host.sidebarDragHandle()->isEnabled());
        host.setActionsAvailable(true, true);

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
            host.findChild<ElaAppBar*>()->windowButton(ElaAppBarType::CloseButtonHint)->click();
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
        auto* title = host.findChild<ElaAppBar*>();
        QVERIFY(title);
        for (const auto type : {ElaAppBarType::MinimizeButtonHint,
                 ElaAppBarType::MaximizeButtonHint, ElaAppBarType::CloseButtonHint}) {
            auto* button = title->windowButton(type);
            QVERIFY(button && button->isVisible());
            QVERIFY(title->rect().contains(QRect(button->mapTo(title, QPoint()), button->size())));
        }
        const QRect viewRect(host.view()->mapTo(&host, QPoint()), host.view()->size());
        QVERIFY(host.rect().contains(viewRect));
        QVERIFY(viewRect.top() >= title->geometry().bottom());
        for (int i = 0; i < 3; ++i) {
            title->windowButton(ElaAppBarType::MaximizeButtonHint)->click();
            QApplication::processEvents();
            QVERIFY(host.isMaximized());
            QVERIFY(!owner.isMaximized());
            title->windowButton(ElaAppBarType::MaximizeButtonHint)->click();
            QApplication::processEvents();
            QVERIFY(!host.isMaximized());
            QCOMPARE(host.frameGeometry(), normalFrame);
            title->windowButton(ElaAppBarType::MinimizeButtonHint)->click();
            QApplication::processEvents();
            QVERIFY(host.isMinimized());
            QVERIFY(!owner.isMinimized());
            host.showNormal();
            QApplication::processEvents();
            QCOMPARE(host.frameGeometry(), normalFrame);
        }
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
