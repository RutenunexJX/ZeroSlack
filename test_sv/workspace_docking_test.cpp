#include "applicationthememanager.h"
#include "mainwindow.h"
#include "tabmanager.h"
#include "contextworkspacecontroller.h"
#include "contextdockhost.h"
#include "contextfloatingwindow.h"
#include "contextrail.h"
#include "panellayoutcontroller.h"
#include "navigationpanecoordinator.h"
#include "temporaryeditorcontextprovider.h"
#include "../src/integrations/xips/xipscontextprovider.h"
#include "workspacesessionstateservice.h"
#include "ElaDrawerArea.h"
#include <QAction>
#include <QApplication>
#include <QCursor>
#include <QGridLayout>
#include <QLabel>
#include <QToolButton>
#include "uicontrols.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QScopeGuard>
#include <QScreen>
#include <QSettings>
#include <QSplitter>
#include <QTemporaryDir>
#include <QTest>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
void settle(MainWindow& window) {
    QTest::qWait(350);
    for (auto* drawer : window.findChildren<ElaDrawerArea*>()) drawer->finishDrawerAnimation();
    QApplication::processEvents();
}
bool mouseButton(bool down) {
#ifdef Q_OS_WIN
    INPUT event{}; event.type = INPUT_MOUSE;
    event.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    SetLastError(ERROR_SUCCESS);
    const bool sent = SendInput(1, &event, sizeof(event)) == 1;
    if (!sent) qWarning() << "Win32 mouse input rejected:" << GetLastError();
    return sent;
#else
    Q_UNUSED(down); return false;
#endif
}
void movePointer(const QPoint& destination) {
    const QPoint start = QCursor::pos();
    for (int i = 1; i <= 12; ++i) {
        QCursor::setPos(start + (destination - start) * i / 12);
        QTest::qWait(16);
    }
}
bool drag(QWidget* handle, const QPoint& destination, const std::function<void()>& held = {}) {
    if (!handle || !handle->isVisible()) {
        qWarning() << "Unavailable drag handle:" << handle;
        return false;
    }
    auto* top = handle->window(); top->raise(); top->activateWindow();
    QTest::qWait(50);
    const QPoint start = handle->mapToGlobal(QPoint(qMin(24, handle->width() / 2), handle->height() / 2));
    QCursor::setPos(start); QTest::qWait(40);
    if (!mouseButton(true)) return false;
    const auto release = qScopeGuard([] { mouseButton(false); });
    QTest::qWait(30);
    movePointer(start + QPoint(-32, 20));
    movePointer(destination);
    QTest::qWait(80);
    if (held) held();
    return true;
}
}

class WorkspaceDockingTest final : public QObject {
    Q_OBJECT
    QTemporaryDir profile;
    QString evidence;
    void capture(QWidget* widget, const QString& name) {
        if (!evidence.isEmpty()) widget->grab().save(evidence + '/' + name + ".png");
    }
private slots:
    void initTestCase() {
        QVERIFY(profile.isValid());
        QCoreApplication::setApplicationName("ZeroSlack-Workspace-Docking-Test");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile.path());
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, profile.path());
        qputenv("ZEROSLACK_SESSION_STORAGE_PATH", profile.filePath("sessions.ini").toUtf8());
        qputenv("XIPS_LIBRARY", profile.filePath("xips-library").toUtf8());
        QDir().mkpath(profile.filePath("xips-library"));
        evidence = qEnvironmentVariable("ZEROSLACK_UI_EVIDENCE_DIR");
        if (!evidence.isEmpty()) QDir().mkpath(evidence);
        auto& theme = ApplicationThemeManager::instance();
        QVERIFY(theme.selectBackend(UiStyleBackend::Ela));
        theme.setMode(ThemeMode::Light); theme.applyToApplication();
    }
    void nativeDrag_data() {
        QTest::addColumn<bool>("maximized");
        QTest::newRow("normal") << false;
        QTest::newRow("maximized") << true;
    }
    void nativeDrag() {
        if (QApplication::platformName() != "windows") QSKIP("Uses Win32 SendInput and the real xIPs DLL.");
        QVERIFY(!qEnvironmentVariableIsEmpty("XIPS_BROWSER_LIBRARY"));
        QFETCH(bool, maximized);
        const QString prefix = maximized ? "max" : "normal";
        const QPoint cursorBefore = QCursor::pos();
        const auto releaseInput = qScopeGuard([=] { mouseButton(false); QCursor::setPos(cursorBefore); });
        QTemporaryDir workspace; QVERIFY(workspace.isValid());
        const QString source = workspace.filePath("retained.sv");
        QFile file(source); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("module retained;\n  logic sample;\nendmodule\n"); file.close();
        MainWindow window;
        const auto failedCapture = qScopeGuard([&] { if (QTest::currentTestFailed()) capture(&window, prefix + "-failure"); });
        window.resize(1280, 800); window.move(80, 50);
        if (maximized) window.showMaximized(); else window.show();
        window.raise(); window.activateWindow();
        QVERIFY(window.tabManager->openFileInTab(source));
        auto* context = window.findChild<ContextWorkspaceController*>(); QVERIFY(context);
        context->setWorkspaceRoot(workspace.path());
        auto* navigation = window.findChild<NavigationPaneCoordinator*>(); QVERIFY(navigation);
        navigation->setExpanded(true, false);
        PanelLayoutController* panels = nullptr;
        for (auto* child : window.children()) if (auto* candidate = dynamic_cast<PanelLayoutController*>(child)) panels = candidate;
        QVERIFY(panels); panels->setAnimationsEnabled(false); panels->restorePanel("problems");
        auto* primary = panels->drawerContent(); QVERIFY(primary);
        const QString activePanel = panels->activeBottomPanelId();
        XipsContextProvider provider(nullptr, nullptr);
        const auto xips = provider.activationResource(workspace.path());
        const ContextPlacement pinned{ContextSurface::Docked, ContextPersistence::Kept, ContextBinding::Global};
        QVERIFY(context->openResource(xips, pinned));
        EditorLocation location; location.filePath = source;
        const auto editor = TemporaryEditorContextProvider::resourceForLocation(location, workspace.path());
        QVERIFY(context->openResource(editor, pinned));
        auto* host = context->dockHost();
        const QString xipsKey = xips.stableKey(), editorKey = editor.stableKey();
        QPointer<QWidget> xipsView = context->viewForResource(xipsKey);
        QPointer<QWidget> editorView = context->viewForResource(editorKey);
        QVERIFY(xipsView && editorView);
        QVERIFY(xipsView->findChild<QWidget*>("xipsBrowser"));
        settle(window);
        capture(&window, prefix + "-start");
        const auto detach = [&](const QString& key) {
            bool dragging = false;
            auto* grip = host->sectionDragHandle(key);
            const QPoint destination = window.centralWidget()->mapToGlobal(window.centralWidget()->rect().center());
            const bool sent = drag(grip, destination, [&] {
                auto* floating = context->floatingWindow();
                dragging = floating && floating->hasResource() && floating->isDockDragging();
                if (floating) capture(floating, prefix + "-floating");
            });
            settle(window);
            qInfo() << "detach" << key << "Win32 input" << sent << "native drag active" << dragging;
            return sent && dragging;
        };
        panels->setBottomCollapsed(true);
        QVERIFY(detach(xipsKey));
        QVERIFY(panels->isBottomCollapsed());
        auto* floating = context->floatingWindow();
        QVERIFY(floating && floating->isFloating() && floating->isVisible());
        QCOMPARE(floating->view(), xipsView.data());
        QCOMPARE(host->viewForResource(editorKey), editorView.data());
        auto* navigationDock = window.findChild<QDockWidget*>("navigationDock"); QVERIFY(navigationDock);
        QVERIFY(drag(floating->titleBar(), navigationDock->mapToGlobal(navigationDock->rect().center())));
        settle(window);
        QCOMPARE(host->resourceArea(xipsKey), Qt::LeftDockWidgetArea);
        QCOMPARE(host->viewForResource(xipsKey), xipsView.data());
        QCOMPARE(window.dockWidgetArea(context->leftDockWidget()), Qt::LeftDockWidgetArea);
        QVERIFY(context->leftDockWidget()->isVisible());
        QVERIFY(context->leftDockWidget()->height() >= 160);
        QVERIFY(xipsView->height() >= 100);
        QCOMPARE(host->resourceArea(editorKey), Qt::RightDockWidgetArea);
        QCOMPARE(host->viewForResource(editorKey), editorView.data());
        capture(&window, prefix + "-left");
        // Persist the new left area before moving the same native view onward.
        WorkspaceSessionState session; session.workspaceRoot = workspace.path();
        session.ui.contextWorkspace = context->captureState();
        session.ui.panelLayout = panels->layoutState();
        WorkspaceSessionStateService store(workspace.filePath("session.ini"));
        QVERIFY(store.save(session).saved);
        auto restored = store.load(workspace.path()); QVERIFY(restored.loaded);
        QVERIFY(restored.state.ui.contextWorkspace.leftDockVisible);
        QCOMPARE(restored.state.ui.contextWorkspace.leftDockHeight, context->leftDockWidget()->height());
        QCOMPARE(restored.state.ui.contextWorkspace.dockSections, session.ui.contextWorkspace.dockSections);
        panels->setBottomCollapsed(true);
        QVERIFY(detach(xipsKey));
        QVERIFY(panels->isBottomCollapsed());
        floating = context->floatingWindow(); QVERIFY(floating);
        QVERIFY(drag(floating->titleBar(), primary->mapToGlobal(primary->rect().center())));
        settle(window);
        QCOMPARE(host->resourceArea(xipsKey), Qt::BottomDockWidgetArea);
        QCOMPARE(host->viewForResource(xipsKey), xipsView.data());
        QVERIFY(primary->isVisible()); QCOMPARE(panels->activeBottomPanelId(), activePanel);
        QVERIFY(detach(editorKey));
        floating = context->floatingWindow(); QVERIFY(floating);
        QVERIFY(drag(floating->titleBar(), host->bottomWidget()->mapToGlobal(host->bottomWidget()->rect().center())));
        settle(window);
        QCOMPARE(host->areaResourceCount(true), 2);
        QCOMPARE(host->viewForResource(editorKey), editorView.data());
        auto* outer = window.findChild<QSplitter*>("bottomWorkspaceSplitter");
        auto* inner = window.findChild<QSplitter*>("contextBottomSplitter");
        QVERIFY(outer && inner); QCOMPARE(outer->orientation(), Qt::Horizontal); QCOMPARE(inner->orientation(), Qt::Horizontal);
        QCOMPARE(outer->count(), 2); QCOMPARE(inner->count(), 2);
        const QRect primaryRect(primary->mapToGlobal(QPoint()), primary->size());
        const QRect contextRect(host->bottomWidget()->mapToGlobal(QPoint()), host->bottomWidget()->size());
        QVERIFY(primaryRect.right() <= contextRect.left());
        QCOMPARE(primaryRect.top(), contextRect.top());
        QCOMPARE(primaryRect.height(), contextRect.height());
        const auto outerBefore = outer->sizes();
        auto* divider = outer->handle(1);
        QVERIFY(drag(divider, divider->mapToGlobal(divider->rect().center()) + QPoint(-70, 0)));
        settle(window); QVERIFY(outer->sizes() != outerBefore);
        const auto innerBefore = inner->sizes();
        divider = inner->handle(1);
        QVERIFY(drag(divider, divider->mapToGlobal(divider->rect().center()) + QPoint(55, 0)));
        settle(window); QVERIFY(inner->sizes() != innerBefore);
        capture(&window, prefix + "-bottom-horizontal");
        session.ui.contextWorkspace = context->captureState();
        session.ui.panelLayout = panels->layoutState();
        QVERIFY(store.save(session).saved);
        restored = store.load(workspace.path()); QVERIFY(restored.loaded);
        QCOMPARE(restored.state.ui.contextWorkspace.bottomSplitState, session.ui.contextWorkspace.bottomSplitState);
        QCOMPARE(restored.state.ui.contextWorkspace.dockSections, session.ui.contextWorkspace.dockSections);
        const auto sizes = outer->sizes();
        context->clearResources();
        QVERIFY(primary->isVisible()); QCOMPARE(panels->activeBottomPanelId(), activePanel);
        panels->restoreLayoutState(restored.state.ui.panelLayout);
        QCOMPARE(context->restoreState(restored.state.ui.contextWorkspace).restoredResources, 2);
        settle(window);
        QCOMPARE(host->areaResourceCount(true), 2);
        QVERIFY(qAbs(outer->sizes()[0] - sizes[0]) <= 2);
        QVERIFY(context->viewForResource(xipsKey)->findChild<QWidget*>("xipsBrowser"));
        capture(&window, prefix + "-restored");
        panels->setBottomCollapsed(true);
        const auto collapsedContext = context->captureState();
        const auto collapsedPanels = panels->layoutState();
        QCOMPARE(collapsedContext.bottomDockHeight, panels->panelHeight(activePanel));
        session.ui.contextWorkspace = collapsedContext;
        session.ui.panelLayout = collapsedPanels;
        QVERIFY(store.save(session).saved);
        restored = store.load(workspace.path()); QVERIFY(restored.loaded);
        QCOMPARE(restored.state.ui.contextWorkspace.bottomDockHeight, collapsedContext.bottomDockHeight);
        context->clearResources();
        panels->restoreLayoutState(restored.state.ui.panelLayout);
        QCOMPARE(context->restoreState(restored.state.ui.contextWorkspace).restoredResources, 2);
        QVERIFY(panels->isBottomCollapsed());
        panels->setBottomCollapsed(false); settle(window);
        QVERIFY(host->bottomWidget()->isVisible());
        QCOMPARE(host->areaResourceCount(true), 2);
        const int fullHeight = primary->height();
        panels->setAnimationsEnabled(true);
        panels->setBottomCollapsed(true);
        QTest::qWait(60);
        QVERIFY(primary->height() > 0 && primary->height() < fullHeight);
        QCOMPARE(primary->height(), host->bottomWidget()->height());
        panels->setBottomCollapsed(false);
        QTRY_COMPARE(primary->height(), fullHeight);
        QCOMPARE(primary->height(), host->bottomWidget()->height());
        panels->setAnimationsEnabled(false);
        panels->restoreBottomSplitState({});
        QVERIFY(qAbs(outer->sizes()[0] - outer->sizes()[1]) <= 2);
        // Restore a left-side session as well, retaining the Files/Design dock.
        auto leftState = session.ui.contextWorkspace;
        for (auto& section : leftState.dockSections) if (section.resourceKey == xipsKey) { section.bottom = false; section.left = true; }
        leftState.leftDockVisible = true;
        QCOMPARE(context->restoreState(leftState).restoredResources, 2);
        settle(window);
        QCOMPARE(host->resourceArea(xipsKey), Qt::LeftDockWidgetArea);
        QVERIFY(navigationDock->isVisible() && context->leftDockWidget()->isVisible());
        // Real rail assets at their production size, in both themes and checked states.
        for (const auto mode : {ThemeMode::Light, ThemeMode::Dark}) {
            ApplicationThemeManager::instance().setMode(mode); ApplicationThemeManager::instance().applyToApplication();
            QTest::qWait(100);
            capture(context->rail(), prefix + (mode == ThemeMode::Dark ? "-rail-dark" : "-rail-light"));
        }
        ApplicationThemeManager::instance().setMode(ThemeMode::Light);
        ApplicationThemeManager::instance().applyToApplication();
    }
    void railIconStates() {
        MainWindow window;
        auto* context = window.findChild<ContextWorkspaceController*>(); QVERIFY(context);
        const QStringList ids{"rtlInsight.kernel", "rtlInsight.block", "rtlInsight.hotspot", "rtlInsight.state", "xips"};
        const QStringList labels{"Kernel", "Module", "Hotspot", "State", "xIPs"};
        for (const auto mode : {ThemeMode::Light, ThemeMode::Dark}) {
            auto& theme = ApplicationThemeManager::instance();
            theme.setMode(mode); theme.applyToApplication();
            QWidget sheet;
            auto* grid = new QGridLayout(&sheet);
            grid->addWidget(new QLabel("20 px / Normal"), 0, 1);
            grid->addWidget(new QLabel("20 px / Selected"), 0, 2);
            for (int row = 0; row < ids.size(); ++row) {
                auto* action = window.findChild<QAction*>("contextRail." + ids[row]); QVERIFY(action);
                QVERIFY(!action->icon().isNull());
                grid->addWidget(new QLabel(labels[row]), row + 1, 0);
                for (int state = 0; state < 2; ++state) {
                    auto* button = UiControls::toolButton(&sheet);
                    button->setIcon(action->icon()); button->setIconSize(QSize(20, 20));
                    button->setCheckable(true); button->setChecked(state != 0); button->setFixedSize(40, 40);
                    grid->addWidget(button, row + 1, state + 1, Qt::AlignCenter);
                }
            }
            sheet.show(); QTest::qWait(80);
            capture(&sheet, mode == ThemeMode::Dark ? "icons-dark" : "icons-light");
        }
        ApplicationThemeManager::instance().setMode(ThemeMode::Light);
        ApplicationThemeManager::instance().applyToApplication();
    }
};
QTEST_MAIN(WorkspaceDockingTest)
#include "workspace_docking_test.moc"
