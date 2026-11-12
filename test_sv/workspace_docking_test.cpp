#include "testuistyle.h"
#include "applicationthememanager.h"
#include "mainwindow.h"
#include "tabmanager.h"
#include "contextworkspacecontroller.h"
#include "contextdockhost.h"
#include "contextfloatingwindow.h"
#include "contextrail.h"
#include "panellayoutcontroller.h"
#include "editorsplitcontroller.h"
#include "navigationpanecoordinator.h"
#include "temporaryeditorcontextprovider.h"
#include "../src/integrations/xips/xipscontextprovider.h"
#include "workspacesessionstateservice.h"
#include "ElaDrawerArea.h"
#include "ElaTabWidget.h"
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
#include <QTimer>
#include <QTabBar>
#include <QElapsedTimer>
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
bool drag(QWidget* handle, const QPoint& destination, const std::function<void()>& held = {},
          const std::function<QPoint()>& currentDestination = {}, const QPoint& localStart = {}) {
    if (!handle || !handle->isVisible()) {
        qWarning() << "Unavailable drag handle:" << handle;
        return false;
    }
    auto* top = handle->window(); top->raise(); top->activateWindow();
    QTest::qWait(50);
    const QPoint start = handle->mapToGlobal(localStart.isNull()
        ? QPoint(qMin(24, handle->width() / 2), handle->height() / 2) : localStart);
    QCursor::setPos(start); QTest::qWait(40);
    if (!mouseButton(true)) return false;
    const auto release = qScopeGuard([] { mouseButton(false); });
    // OLE runs a nested event loop during a shared-tab drag. Continue physical
    // pointer motion from a timer so that loop also receives the release.
    bool finished = false;
    int step = 0;
    QTimer motion;
    QObject::connect(&motion, &QTimer::timeout, &motion, [&] {
        ++step;
        const QPoint target = currentDestination ? currentDestination() : destination;
        QCursor::setPos(start + (target - start) * qMin(step, 20) / 20);
        if (step == 24) {
            if (held) held();
            mouseButton(false); finished = true; motion.stop();
        }
    });
    motion.start(16);
    QElapsedTimer elapsed; elapsed.start();
    while (!finished && elapsed.elapsed() < 5000) QTest::qWait(20);
    return finished;
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
        QCoreApplication::setOrganizationName("ZeroSlackWorkspaceDockingTest");
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
        QVERIFY(initializeUiStyleForTest());
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
        auto* registry = panels->tabHostController(); QVERIFY(registry);
        auto* primaryGroup = registry->regionGroup("bottom"); QVERIFY(primaryGroup);
        auto* primaryPage = primaryGroup->currentWidget(); QVERIFY(primaryPage);
        const QString xipsKey = xips.stableKey(), editorKey = editor.stableKey();
        QPointer<QWidget> xipsView = context->viewForResource(xipsKey);
        QPointer<QWidget> editorView = context->viewForResource(editorKey);
        QVERIFY(xipsView && editorView);
        QVERIFY(xipsView->findChild<QWidget*>("xipsBrowser"));
        settle(window);
        capture(&window, prefix + "-start");
        const auto detach = [&](const QString& key) {
            auto* page=host->sectionWidget(key);
            auto* sourceGroup=registry->groupForPage(page);
            if (!sourceGroup || !sourceGroup->tabBar()->isVisible()) return false;
            auto* bar=sourceGroup->tabBar();
            const auto tab=bar->tabRect(sourceGroup->indexOf(page));
            if (!bar->rect().contains(tab.center())) return false;
            // The title area is outside every registered tab drop target.
            const bool sent=drag(bar,window.mapToGlobal(QPoint(window.width()/2,15)),{},{},tab.center());
            settle(window);
            auto* group = qobject_cast<ElaTabWidget*>(registry->groupForPage(host->sectionWidget(key)));
            const bool floated = group && group->isFloatingTabWidget() && group->window()->isVisible();
            if (floated) capture(group->window(), prefix + "-floating");
            qInfo() << "float" << key << "Win32 input" << sent << "shared floating group" << floated;
            return sent && floated;
        };
        panels->setBottomCollapsed(true);
        QVERIFY(detach(xipsKey));
        QVERIFY(panels->isBottomCollapsed());
        auto* floating = qobject_cast<ElaTabWidget*>(registry->groupForPage(host->sectionWidget(xipsKey)));
        QVERIFY(floating && floating->isFloatingTabWidget() && floating->window()->isVisible());
        QVERIFY(floating->isAncestorOf(xipsView.data()));
        QCOMPARE(host->viewForResource(editorKey), editorView.data());
        auto* navigationDock = window.findChild<QDockWidget*>("navigationDock"); QVERIFY(navigationDock);
        QVERIFY(drag(floating->tabBar(), navigationDock->mapToGlobal(navigationDock->rect().center())));
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
        floating = qobject_cast<ElaTabWidget*>(registry->groupForPage(host->sectionWidget(xipsKey))); QVERIFY(floating);
        panels->setBottomCollapsed(false); settle(window);
        const auto primaryEdge = [=] { return primaryGroup->mapToGlobal(QPoint(primaryGroup->width() - 1, primaryGroup->height() / 2)); };
        QVERIFY(drag(floating->tabBar(), primaryEdge(), {}, primaryEdge));
        settle(window);
        QCOMPARE(host->resourceArea(xipsKey), Qt::BottomDockWidgetArea);
        QCOMPARE(host->viewForResource(xipsKey), xipsView.data());
        QVERIFY(primary->isVisible()); QCOMPARE(primaryGroup->currentWidget(), primaryPage);
        QVERIFY(detach(editorKey));
        floating = qobject_cast<ElaTabWidget*>(registry->groupForPage(host->sectionWidget(editorKey))); QVERIFY(floating);
        auto* bottomTarget = registry->groupForPage(host->sectionWidget(xipsKey)); QVERIFY(bottomTarget);
        const auto bottomEdge = [=] { return bottomTarget->mapToGlobal(QPoint(bottomTarget->width() - 1, bottomTarget->height() / 2)); };
        QVERIFY(drag(floating->tabBar(), bottomEdge(), {}, bottomEdge));
        settle(window);
        QCOMPARE(host->areaResourceCount(true), 2);
        QCOMPARE(host->viewForResource(editorKey), editorView.data());
        QVERIFY(!context->dockWidget()->isVisible());
        auto* xipsGroup = registry->groupForPage(host->sectionWidget(xipsKey));
        auto* editorGroup = registry->groupForPage(host->sectionWidget(editorKey));
        auto* outer = qobject_cast<QSplitter*>(primaryGroup->parentWidget());
        QVERIFY(outer && xipsGroup && editorGroup); QCOMPARE(outer->orientation(), Qt::Horizontal);
        QCOMPARE(outer->count(), 3); QCOMPARE(xipsGroup->parentWidget(), outer); QCOMPARE(editorGroup->parentWidget(), outer);
        const QRect primaryRect(primaryGroup->mapToGlobal(QPoint()), primaryGroup->size());
        const QRect contextRect(xipsGroup->mapToGlobal(QPoint()), xipsGroup->size());
        QVERIFY(!primaryRect.intersects(contextRect));
        QCOMPARE(primaryRect.top(), contextRect.top());
        QCOMPARE(primaryRect.height(), contextRect.height());
        const auto outerBefore = outer->sizes();
        auto* divider = outer->handle(1);
        QVERIFY(drag(divider, divider->mapToGlobal(divider->rect().center()) + QPoint(-70, 0)));
        settle(window); QVERIFY(outer->sizes() != outerBefore);
        const auto innerBefore = outer->sizes();
        divider = outer->handle(2);
        QVERIFY(drag(divider, divider->mapToGlobal(divider->rect().center()) + QPoint(55, 0)));
        settle(window); QVERIFY(outer->sizes() != innerBefore);
        capture(&window, prefix + "-bottom-horizontal");
        session.ui.contextWorkspace = context->captureState();
        session.ui.panelLayout = panels->layoutState();
        session.ui.pageLayout = registry->savePageLayout();
        QVERIFY(store.save(session).saved);
        restored = store.load(workspace.path()); QVERIFY(restored.loaded);
        QCOMPARE(restored.state.ui.contextWorkspace.bottomSplitState, session.ui.contextWorkspace.bottomSplitState);
        QCOMPARE(restored.state.ui.pageLayout, session.ui.pageLayout);
        QCOMPARE(restored.state.ui.contextWorkspace.dockSections, session.ui.contextWorkspace.dockSections);
        const auto sizes = outer->sizes();
        context->clearResources();
        QVERIFY(primary->isVisible()); QCOMPARE(primaryGroup->currentWidget(), primaryPage);
        panels->restoreLayoutState(restored.state.ui.panelLayout);
        QCOMPARE(context->restoreState(restored.state.ui.contextWorkspace).restoredResources, 2);
        QVERIFY(registry->restorePageLayout(restored.state.ui.pageLayout).isEmpty());
        settle(window);
        QCOMPARE(host->areaResourceCount(true), 2);
        outer = qobject_cast<QSplitter*>(registry->regionGroup("bottom")->parentWidget()); QVERIFY(outer);
        QCOMPARE(outer->sizes().size(), sizes.size());
        qInfo() << "Restored split sizes" << outer->sizes() << "saved" << sizes;
        for (int i = 0; i < sizes.size(); ++i) QVERIFY(qAbs(outer->sizes()[i] - sizes[i]) <= 2);
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
        auto* restoredContextGroup = registry->groupForPage(host->sectionWidget(xipsKey)); QVERIFY(restoredContextGroup);
        const int fullHeight = primary->height();
        panels->setAnimationsEnabled(true);
        panels->setBottomCollapsed(true);
        QTest::qWait(60);
        QVERIFY(primary->height() > 0 && primary->height() < fullHeight);
        QCOMPARE(primaryGroup->height(), restoredContextGroup->height());
        panels->setBottomCollapsed(false);
        QTRY_COMPARE(primary->height(), fullHeight);
        QCOMPARE(primaryGroup->height(), restoredContextGroup->height());
        panels->setAnimationsEnabled(false);
        registry->setActiveGroup(registry->regionGroup("bottom")); registry->equalizeSplitSizes();
        outer = qobject_cast<QSplitter*>(registry->regionGroup("bottom")->parentWidget()); QVERIFY(outer);
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
