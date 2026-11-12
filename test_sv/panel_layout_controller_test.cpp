#include "actionregistry.h"
#include "applicationthememanager.h"
#include "insightvisualstyle.h"
#include "panellayoutcontroller.h"
#include "editorsplitcontroller.h"
#include "deferredpanel.h"
#include "testuistyle.h"

#include <QApplication>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTabBar>
#include <QMenu>
#include <QDir>
#include <QTest>
#include <QTextCursor>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <iostream>

namespace {
int checks = 0;
int failures = 0;

void check(bool condition, const char* message)
{
    ++checks;
    if (condition)
        return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

struct PanelFixture {
    QDockWidget* dock = nullptr;
    QWidget* root = nullptr;
    QTabWidget* tabs = nullptr;
    QTreeWidget* tree = nullptr;
    QPlainTextEdit* log = nullptr;
};

PanelFixture createPanel(QMainWindow* window,
                         const QString& id,
                         const QString& title)
{
    PanelFixture fixture;
    fixture.dock = new QDockWidget(title, window);
    fixture.dock->setObjectName(id + QStringLiteral("Dock"));
    fixture.root = new QWidget(fixture.dock);
    fixture.root->setObjectName(id + QStringLiteral("Panel"));
    auto* layout = new QVBoxLayout(fixture.root);
    layout->setContentsMargins(0, 0, 0, 0);
    fixture.tabs = new QTabWidget(fixture.root);
    fixture.tabs->setObjectName(id + QStringLiteral("InternalTabs"));

    fixture.tree = new QTreeWidget(fixture.tabs);
    fixture.tree->setObjectName(id + QStringLiteral("SelectionTree"));
    fixture.tree->setHeaderHidden(true);
    for (int index = 0; index < 24; ++index) {
        auto* item = new QTreeWidgetItem(fixture.tree);
        item->setText(0, QStringLiteral("item-%1").arg(index));
    }
    fixture.tabs->addTab(fixture.tree, QStringLiteral("Primary"));

    fixture.log = new QPlainTextEdit(fixture.tabs);
    fixture.log->setObjectName(id + QStringLiteral("ScrollLog"));
    QStringList lines;
    for (int index = 0; index < 120; ++index)
        lines.append(QStringLiteral("line-%1").arg(index));
    fixture.log->setPlainText(lines.join(QLatin1Char('\n')));
    fixture.tabs->addTab(fixture.log, QStringLiteral("Secondary"));
    layout->addWidget(fixture.tabs);
    fixture.dock->setWidget(fixture.root);
    window->addDockWidget(Qt::BottomDockWidgetArea, fixture.dock);
    return fixture;
}

class DrawerHarness
{
public:
    DrawerHarness()
    {
        window.resize(1080, 760);
        editor = new QPlainTextEdit(&window);
        editor->setObjectName(QStringLiteral("editor"));
        QStringList lines;
        for (int index = 0; index < 180; ++index)
            lines.append(QStringLiteral("editor-line-%1").arg(index));
        editor->setPlainText(lines.join(QLatin1Char('\n')));
        window.setCentralWidget(editor);

        controller = new PanelLayoutController(&window, &window);
        const QList<QPair<QString, QString>> descriptors = {
            {QStringLiteral("problems"), QStringLiteral("Problems")},
            {QStringLiteral("scopedSearch"), QStringLiteral("Search")},
            {QStringLiteral("activity"), QStringLiteral("Activity")},
            {QStringLiteral("rtlHighRiskEdit"), QStringLiteral("Change Preview")},
            {QStringLiteral("connections"), QStringLiteral("Connections")},
        };
        for (const auto& descriptor : descriptors) {
            PanelFixture fixture = createPanel(
                &window, descriptor.first, descriptor.second);
            fixtures.insert(descriptor.first, fixture);
            check(controller->registerBottomPanel(
                      descriptor.first, fixture.dock),
                  "bottom panel registers");
        }
        check(controller->registerBottomPanelAlias(
                  QStringLiteral("instancePairConnection"),
                  QStringLiteral("connections")),
              "instance-pair alias registers");
        check(controller->registerBottomPanelAlias(
                  QStringLiteral("multiSignalPropagation"),
                  QStringLiteral("connections")),
              "multi-signal alias registers");
        controller->finalize();
        window.show(); window.raise(); window.activateWindow();
        check(QTest::qWaitForWindowActive(&window), "native drawer host is active before keyboard input");
        QApplication::processEvents();
    }

    QMainWindow window;
    QPlainTextEdit* editor = nullptr;
    PanelLayoutController* controller = nullptr;
    QHash<QString, PanelFixture> fixtures;
};

void verifyIndependentSideColumns(DrawerHarness& harness)
{
    auto* navigation = new QDockWidget(QStringLiteral("Files"), &harness.window);
    navigation->setWidget(new QWidget(navigation));
    navigation->setMinimumWidth(160);
    auto* side = new QDockWidget(QStringLiteral("Context"), &harness.window);
    side->setWidget(new QWidget(side));
    side->setMinimumWidth(280);
    harness.window.addDockWidget(Qt::LeftDockWidgetArea, navigation);
    harness.window.addDockWidget(Qt::RightDockWidgetArea, side);
    harness.controller->setBottomCollapsed(false);
    QApplication::processEvents();
    const QRect expandedSide = side->geometry();
    auto* drawer = harness.controller->tabStrip()->parentWidget();
    const int drawerLeft = drawer->mapTo(&harness.window, QPoint()).x();
    check(drawerLeft >= navigation->geometry().right(),
          "bottom drawer starts after the independent file column");
    check(drawerLeft + drawer->width() <= side->geometry().left(),
          "bottom drawer ends before the right sidebar");
    harness.controller->setBottomCollapsed(true);
    QApplication::processEvents();
    check(side->geometry().height() == expandedSide.height(),
          "collapsing the drawer preserves sidebar height");
    harness.controller->setBottomCollapsed(false);
    QApplication::processEvents();
    check(side->geometry().height() == expandedSide.height(),
          "expanding the drawer preserves sidebar height");
    delete side;
    delete navigation;
    QApplication::processEvents();
}

QTabWidget* panelGroup(DrawerHarness& harness, const QString& id)
{
    return harness.controller->tabHostController()->groupForPage(harness.fixtures.value(id).root);
}
bool selected(DrawerHarness& harness, const QString& id)
{
    auto* group=panelGroup(harness,id);
    return group && group->currentWidget()==harness.fixtures.value(id).root && !harness.controller->isBottomCollapsed();
}
void clickPanel(DrawerHarness& harness, const QString& id)
{
    auto* group=panelGroup(harness,id);
    check(group != nullptr,"the clicked panel has a real registered tab");
    if (!group) return;
    auto* bar=group->tabBar(); const int index=group->indexOf(harness.fixtures.value(id).root);
    QTest::mouseClick(bar,Qt::LeftButton,Qt::NoModifier,bar->tabRect(index).center());
}
void verifyButtonContract(DrawerHarness& harness)
{
    const QStringList ids={"problems","scopedSearch","activity","rtlHighRiskEdit","connections"};
    const QStringList labels={"Problems","Search","Activity","Change Preview","Connections"};
    check(harness.controller->bottomPanelIds()==ids,"real bottom tabs keep the product order");
    for(int i=0;i<ids.size();++i) {
        auto* group=panelGroup(harness,ids[i]); auto* page=harness.fixtures.value(ids[i]).root;
        const int index=group ? group->indexOf(page) : -1;
        check(group && group->tabText(index)==labels[i] && !group->tabIcon(index).isNull()
                  && group->tabToolTip(index).contains("Ctrl+J")
                  && harness.controller->tabStrip()->isAncestorOf(group->tabBar())
                  && group->tabPosition()==QTabWidget::South,
              "the actual page tab carries its label icon tooltip and bottom position");
        check(!harness.window.findChild<QToolButton*>("bottomPanelButton_"+ids[i]),
              "no parallel simulated page button remains");
    }
    check(harness.controller->resizeHandle() && harness.controller->resizeHandle()->height()>=6,
          "drawer exposes an accessible resize hit target");
}

void verifyClickAndShortcutBehavior(DrawerHarness& harness)
{
    check(selected(harness,"problems") && harness.controller->activeBottomPanelId()=="problems",
          "first panel starts selected");
    clickPanel(harness,"problems"); QApplication::processEvents();
    check(harness.controller->isBottomCollapsed() && harness.controller->tabStrip()->isVisibleTo(&harness.window)
              && !selected(harness,"problems"),"clicking the active real tab closes content and retains the strip");
    clickPanel(harness,"problems"); QApplication::processEvents();
    check(selected(harness,"problems"),"clicking a collapsed tab restores its panel");
    clickPanel(harness,"scopedSearch"); QApplication::processEvents();
    check(harness.controller->activeBottomPanelId()=="scopedSearch" && selected(harness,"scopedSearch")
              && !selected(harness,"problems") && harness.controller->isPanelOpen("scopedSearch")
              && !harness.controller->isPanelOpen("problems"),"switching real tabs keeps one page selected within its group");
    clickPanel(harness,"activity"); QApplication::processEvents();
    const ActionDescriptor* ctrlJ = findActionById(
        QString::fromLatin1(ActionIds::ViewBottomPanelCollapsed));
    check(ctrlJ
              && ctrlJ->defaultShortcut
                     == QStringLiteral("Ctrl+J")
              && actionRegistryIsValid(),
          "Ctrl+J is registry-backed and has no shortcut conflict");
    auto* shortcutAction = new QAction(&harness.window);
    shortcutAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+J")));
    shortcutAction->setShortcutContext(Qt::ApplicationShortcut);
    harness.window.addAction(shortcutAction);
    QObject::connect(
        shortcutAction,
        &QAction::triggered,
        harness.controller,
        [controller = harness.controller]() {
            controller->toggleBottomCollapsed();
        });
    QTest::keyClick(&harness.window, Qt::Key_J, Qt::ControlModifier);
    QApplication::processEvents();
    check(harness.controller->isBottomCollapsed(),
          "Ctrl+J closes the active panel");
    QTest::keyClick(&harness.window, Qt::Key_J, Qt::ControlModifier);
    QApplication::processEvents();
    check(!harness.controller->isBottomCollapsed()
              && harness.controller->activeBottomPanelId()
                     == QStringLiteral("activity"),
          "Ctrl+J restores the last used panel");
}

QString cssColor(const QColor& color)
{
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(color.alpha());
}

void verifyClickGeometryStability(DrawerHarness& harness)
{
    check(harness.controller->animationsEnabled(),
          "Ela enables composited drawer motion");
    harness.controller->restorePanel(QStringLiteral("problems"));
    harness.controller->resetPanelHeight(QStringLiteral("problems"));
    QApplication::processEvents();

    QWidget* content=harness.controller->drawerContent();
    QWidget* buttonBar=harness.controller->tabStrip();
    check(panelGroup(harness,"problems") && content && buttonBar,"click stability fixtures are available");
    if (!content || !buttonBar) return;

    const QRect windowGeometry = harness.window.geometry();
    const int buttonBarBottom =
        buttonBar->mapToGlobal(buttonBar->rect().bottomLeft()).y();
    clickPanel(harness,"problems");
    QApplication::processEvents();
    check(harness.controller->isBottomCollapsed()
              && harness.window.geometry() == windowGeometry
              && buttonBar->mapToGlobal(
                     buttonBar->rect().bottomLeft()).y()
                     == buttonBarBottom,
          "collapse click keeps the window and button bar stationary");

    clickPanel(harness,"problems");
    QApplication::processEvents();
    check(!harness.controller->isBottomCollapsed()
              && content->height()
                     == PanelLayoutController::kDefaultContentHeight
              && harness.window.geometry() == windowGeometry
              && buttonBar->mapToGlobal(
                     buttonBar->rect().bottomLeft()).y()
                     == buttonBarBottom,
          "expand click restores height without moving the window or buttons");

    clickPanel(harness,"problems");
    clickPanel(harness,"problems");
    QApplication::processEvents();
    check(!harness.controller->isBottomCollapsed()
              && selected(harness,"problems")
              && content->height()
                     == PanelLayoutController::kDefaultContentHeight
              && harness.window.geometry() == windowGeometry,
          "rapid reverse click settles without top-level geometry jitter");
}

void verifyFocusAndEditorPreservation(DrawerHarness& harness)
{
    harness.controller->restorePanel(QStringLiteral("problems"));
    QTextCursor cursor(harness.editor->document());
    cursor.setPosition(90);
    cursor.setPosition(118, QTextCursor::KeepAnchor);
    harness.editor->setTextCursor(cursor);
    harness.editor->verticalScrollBar()->setValue(42);
    const int anchor = harness.editor->textCursor().anchor();
    const int position = harness.editor->textCursor().position();
    const int scroll = harness.editor->verticalScrollBar()->value();
    harness.editor->setFocus();
    QApplication::processEvents();

    QTest::keyClick(harness.editor, Qt::Key_Escape);
    QApplication::processEvents();
    check(!harness.controller->isBottomCollapsed(),
          "editor Escape does not close the bottom panel");

    QTreeWidget* panelTree =
        harness.fixtures.value(QStringLiteral("problems")).tree;
    panelTree->setFocus();
    QApplication::processEvents();
    QTest::keyClick(panelTree, Qt::Key_Escape);
    QApplication::processEvents();
    QTest::qWait(330);
    check(harness.controller->isBottomCollapsed()
              && QApplication::focusWidget() == harness.editor,
          "panel-scoped Escape closes the drawer and restores editor focus");
    check(harness.editor->textCursor().anchor() == anchor
              && harness.editor->textCursor().position() == position
              && harness.editor->verticalScrollBar()->value() == scroll,
          "close preserves editor cursor selection and scroll");

    const int collapsedEditorHeight = harness.editor->height();
    harness.controller->setBottomCollapsed(false);
    const bool reflowed = QTest::qWaitFor([&] {
        return harness.editor->height() < collapsedEditorHeight;
    }, 1000);
    check(reflowed
              && harness.editor->textCursor().anchor() == anchor
              && harness.editor->textCursor().position() == position,
          "expanded drawer reflows the editor without overlaying or resetting it");
}

void verifySizing(DrawerHarness& harness)
{
    harness.controller->restorePanel(QStringLiteral("problems"));
    harness.controller->setPanelHeight(QStringLiteral("problems"), 10);
    check(harness.controller->panelHeight(QStringLiteral("problems"))
              == PanelLayoutController::kMinimumContentHeight,
          "height clamps to the 160 px lower bound");
    harness.controller->setPanelHeight(QStringLiteral("problems"), 100000);
    check(harness.controller->panelHeight(QStringLiteral("problems"))
              == harness.controller->maximumContentHeight(),
          "height clamps to 55 percent of available window height");

    harness.controller->resetPanelHeight(QStringLiteral("problems"));
    harness.controller->setPanelHeight(QStringLiteral("scopedSearch"), 340);
    harness.controller->restorePanel(QStringLiteral("scopedSearch"));
    check(harness.controller->panelHeight(QStringLiteral("scopedSearch")) == 340,
          "search keeps its independent height");
    harness.controller->restorePanel(QStringLiteral("problems"));
    check(harness.controller->panelHeight(QStringLiteral("problems"))
              == PanelLayoutController::kDefaultContentHeight,
          "switching panels restores the selected panel height");

    QWidget* handle = harness.controller->resizeHandle();
    const QPoint center = handle->rect().center();
    QTest::mousePress(handle, Qt::LeftButton, Qt::NoModifier, center);
    QTest::mouseMove(handle, center + QPoint(0, 5000));
    QTest::mouseRelease(handle, Qt::LeftButton, Qt::NoModifier, center);
    check(harness.controller->panelHeight(QStringLiteral("problems"))
              == PanelLayoutController::kMinimumContentHeight,
          "drag resize obeys the lower bound");
    QTest::mouseDClick(handle, Qt::LeftButton, Qt::NoModifier, center);
    QApplication::processEvents();
    check(harness.controller->panelHeight(QStringLiteral("problems"))
              == PanelLayoutController::kDefaultContentHeight,
          "double-click restores the panel default height");
}

PanelLayoutState preparePersistentState(DrawerHarness& harness)
{
    harness.controller->restorePanel(QStringLiteral("connections"));
    PanelFixture connection =
        harness.fixtures.value(QStringLiteral("connections"));
    connection.tree->setCurrentItem(connection.tree->topLevelItem(13));
    connection.tabs->setCurrentIndex(1);
    connection.log->verticalScrollBar()->setValue(
        connection.log->verticalScrollBar()->maximum());
    harness.controller->setPanelHeight(QStringLiteral("connections"), 330);
    QApplication::processEvents();
    return harness.controller->layoutState();
}

void verifyPersistence(DrawerHarness& harness)
{
    const PanelLayoutState state = preparePersistentState(harness);
    check(state.bottomPanelHeights.value(QStringLiteral("connections")) == 330
              && state.bottomPanelViewStates
                     .value(QStringLiteral("connections"))
                     .contains(QStringLiteral("tabs")),
          "workspace state captures per-panel height and internal view state");

    DrawerHarness restored;
    restored.controller->restoreLayoutState(state);
    QApplication::processEvents();
    PanelFixture connection =
        restored.fixtures.value(QStringLiteral("connections"));
    check(restored.controller->activeBottomPanelId()
                  == QStringLiteral("connections")
              && restored.controller->panelHeight(
                     QStringLiteral("connections")) == 330
              && connection.tabs->currentIndex() == 1
              && connection.tree->currentItem()
              && connection.tree->currentItem()->text(0)
                     == QStringLiteral("item-13")
              && connection.log->verticalScrollBar()->value() > 0,
          "workspace restore reapplies height scroll selection and internal tab");

    PanelLayoutState legacy;
    legacy.valid = true;
    legacy.activeBottomPanel =
        QStringLiteral("instancePairConnection");
    legacy.expandedBottomHeight = 245;
    restored.controller->restoreLayoutState(legacy);
    check(restored.controller->activeBottomPanelId()
                  == QStringLiteral("connections")
              && restored.controller->panelHeight(
                     QStringLiteral("connections")) == 245,
          "legacy connection panel state migrates into Connections");
}

void verifyDeferredPersistence()
{
    QMainWindow window;
    window.resize(1080, 760);
    window.setCentralWidget(new QPlainTextEdit(&window));
    PanelLayoutController controller(&window, &window);
    auto eager = createPanel(&window, QStringLiteral("problems"),
                             QStringLiteral("Problems"));
    controller.registerBottomPanel(QStringLiteral("problems"), eager.dock);
    auto* dock = new QDockWidget(QStringLiteral("Connections"), &window);
    int creations = 0;
    QTabWidget* tabs = nullptr;
    QTreeWidget* tree = nullptr;
    auto* host = new DeferredPanel(dock, &window,
        [&](QWidget* parent) -> QWidget* {
            ++creations;
            tabs = new QTabWidget(parent);
            tabs->setObjectName(QStringLiteral("deferredTabs"));
            tabs->addTab(new QWidget(tabs), QStringLiteral("First"));
            tree = new QTreeWidget(tabs);
            tree->setObjectName(QStringLiteral("deferredTree"));
            for (int index = 0; index < 100; ++index)
                new QTreeWidgetItem(tree, {QString::number(index)});
            tabs->addTab(tree, QStringLiteral("Second"));
            return tabs;
        });
    dock->setWidget(host);
    window.addDockWidget(Qt::BottomDockWidgetArea, dock);
    controller.registerBottomPanel(QStringLiteral("connections"), dock);
    controller.finalize();
    PanelLayoutState state = controller.layoutState();
    state.valid = true;
    state.bottomCollapsed = true;
    const QVariantMap viewState = {
        {QStringLiteral("tabs"), QVariantMap{{QStringLiteral("deferredTabs"), 1}}},
        {QStringLiteral("selections"), QVariantMap{
            {QStringLiteral("deferredTree"), QVariantMap{
                {QStringLiteral("path"), QStringLiteral("73")},
                {QStringLiteral("column"), 0}}}}}
    };
    state.bottomPanelViewStates.insert(QStringLiteral("connections"), viewState);
    controller.restoreLayoutState(state);
    window.show();
    QApplication::processEvents();
    check(creations == 0 && !host->isCreated(),
          "hidden panel stays unconstructed through layout restore and startup");
    check(controller.layoutState().bottomPanelViewStates.value(
              QStringLiteral("connections")) == viewState,
          "saving before first open preserves deferred view state");
    controller.restorePanel(QStringLiteral("connections"));
    QApplication::processEvents();
    check(creations == 1 && tabs && tabs->currentIndex() == 1
              && tree->currentItem() == tree->topLevelItem(73)
              && tree->verticalScrollBar()->value() > 0,
          "first open creates content and restores its tab selection and scroll");
    tree->setCurrentItem(tree->topLevelItem(80));
    for (int index = 0; index < 3; ++index) {
        controller.closePanel(QStringLiteral("connections"));
        controller.restorePanel(QStringLiteral("connections"));
        QApplication::processEvents();
    }
    check(creations == 1 && tree->currentItem() == tree->topLevelItem(80),
          "reopening reuses content and retains its changed state");

    QObject* owner = new QObject;
    DeferredPanel cancelled(nullptr, owner,
        [&](QWidget*) -> QWidget* { ++creations; return nullptr; });
    delete owner;
    cancelled.ensureCreated();
    check(creations == 1,
          "destroyed coordinator cancels an unopened panel factory");
}

void verifySharedBottomStrip(DrawerHarness& harness)
{
    auto* registry=harness.controller->tabHostController();
    harness.controller->setAnimationsEnabled(false);
    harness.controller->restorePanel("activity");
    auto* activity=harness.fixtures.value("activity").root;
    auto* original=registry->groupForPage(activity);
    const QString identity=registry->pageId(activity);
    auto* second=registry->createSplit(original,EditorSplitDirection::Below);
    check(second && registry->movePage(activity,second),"bottom pages retain split content groups");
    QApplication::processEvents();
    auto* strip=harness.controller->tabStrip();
    check(original->tabBar()->parentWidget()==second->tabBar()->parentWidget()
              && strip->isAncestorOf(original->tabBar()) && strip->isAncestorOf(second->tabBar())
              && original->tabBar()->mapToGlobal(QPoint()).y()==second->tabBar()->mapToGlobal(QPoint()).y(),
          "split groups expose their own authoritative tabs in one bottom row");
    for(auto* group:{original,second}) {
        auto* stack=group->findChild<QStackedWidget*>(QString(),Qt::FindDirectChildrenOnly);
        check(stack && stack->geometry()==group->rect(),"external tab strips leave no hidden header or footer gap in content");
    }
    const auto saved=registry->savePageLayout();
    check(harness.controller->closePanel("activity") && registry->pageById(identity)==activity,
          "closing parks the same tool page and retains its identity");
    auto* reopen=strip->findChild<QToolButton*>("bottomRestorePanels");
    check(reopen && reopen->isEnabled() && reopen->menu(),"closed pages have a reachable menu in the same strip");
    if(reopen && reopen->menu()) {
        QMetaObject::invokeMethod(reopen->menu(),"aboutToShow");
        QAction* action=nullptr; for(auto* candidate:reopen->menu()->actions()) if(candidate->data().toString()=="activity") action=candidate;
        check(action!=nullptr,"the reopen menu derives its entries from parked pages");
        if(action) action->trigger();
    }
    check(registry->groupForPage(activity) && registry->pageId(activity)==identity,"reopening reuses the parked page without a duplicate");
    check(registry->restorePageLayout(saved).isEmpty(),"split page layout restores without warnings");
    QApplication::processEvents();
    check(registry->groups("bottom").size()==2 && registry->pageId(activity)==identity,
          "session restore keeps split groups and their page identities");
    harness.window.resize(640,760); QApplication::processEvents();
    for(auto* group:registry->groups("bottom"))
        check(strip->rect().contains(QRect(group->tabBar()->mapTo(strip,QPoint()),group->tabBar()->size())),
              "real tab bars stay inside the single strip at narrow width");
    const auto evidence=qEnvironmentVariable("ZEROSLACK_UI_REVIEW_DIR");
    if(!evidence.isEmpty()) {QDir().mkpath(evidence);harness.window.grab().save(evidence+"/bottom-split-tabs.png");}
    harness.window.resize(1080,760);
    auto groups=registry->groups("bottom");
    if(groups.size()>1) check(registry->mergeGroup(groups.last(),groups.first()),"split groups merge through the same page registry");
}

void verifyBadgesAndTheme(DrawerHarness& harness)
{
    harness.controller->restorePanel(QStringLiteral("problems"));
    harness.window.activateWindow();
    harness.editor->setFocus();
    QApplication::processEvents();
    QWidget* focusBeforeBadge = QApplication::focusWidget();
    QDockWidget* problemsDock = harness.fixtures.value(
        QStringLiteral("problems")).dock;
    problemsDock->setProperty("bottomBadgeText", QStringLiteral("3"));
    problemsDock->setProperty("bottomBadgeTone", QStringLiteral("error"));
    QApplication::processEvents();
    check(harness.controller->panelBadgeText(
                  QStringLiteral("problems")) == QStringLiteral("3")
              && harness.controller->panelBadgeTone(
                     QStringLiteral("problems")) == QStringLiteral("error")
              && panelGroup(harness,"problems")->tabToolTip(panelGroup(harness,"problems")->indexOf(harness.fixtures.value("problems").root)).contains("3"),
          "Problems exposes a compact count and severity badge");
    problemsDock->setProperty(
        "bottomBadgeTone", QStringLiteral("warning"));
    QApplication::processEvents();
    check(harness.controller->panelBadgeTone(
              QStringLiteral("problems")) == QStringLiteral("warning"),
          "Problems badge severity updates when the count is unchanged");

    const QString activeBefore =
        harness.controller->activeBottomPanelId();
    harness.controller->setPanelBadge(
        QStringLiteral("activity"),
        QStringLiteral("Running background analysis"),
        QStringLiteral("info"));
    QApplication::processEvents();
    auto* activityGroup=panelGroup(harness,"activity");
    const int activityIndex=activityGroup->indexOf(harness.fixtures.value("activity").root);
    check(harness.controller->activeBottomPanelId()==activeBefore && QApplication::focusWidget()==focusBeforeBadge
              && activityGroup->tabText(activityIndex).contains("Running background analysis")
              && activityGroup->tabToolTip(activityIndex).contains("Running background analysis"),
          "background status remains available in the real tab without stealing focus or opening its page");
    harness.controller->setPanelBadge("activity",QString(),QString());
    check(harness.controller->panelBadgeText("activity").isEmpty() && activityGroup->tabText(activityIndex)=="Activity",
          "empty status removes badge noise from the real tab");

    ApplicationThemeManager& themeManager =
        ApplicationThemeManager::instance();
    const ThemeMode originalMode = themeManager.mode();
    themeManager.setMode(ThemeMode::Dark);
    QApplication::processEvents();
    QWidget* drawerRoot = harness.controller->tabStrip()->parentWidget();
    check(harness.controller->tabStrip()->isVisible()
              && panelGroup(harness,"problems")->tabBar()->devicePixelRatioF() > 0.0
              && drawerRoot->styleSheet().contains(
                     cssColor(InsightVisualStyle::theme(
                         ThemeMode::Dark).surface.panel)),
          "actual dark-theme changes refresh and keep drawer controls available");
    themeManager.setMode(ThemeMode::Light);
    QApplication::processEvents();
    check(drawerRoot->styleSheet().contains(
              cssColor(InsightVisualStyle::theme(
                  ThemeMode::Light).surface.panel)),
          "light theme restores semantic drawer tokens");
    themeManager.setMode(originalMode);
}
}

int main(int argc, char* argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QApplication app(argc, argv);
    if (!initializeUiStyleForTest()) return 2;
    DrawerHarness harness;
    verifyIndependentSideColumns(harness);
    verifyButtonContract(harness);
    verifyClickAndShortcutBehavior(harness);
    verifyClickGeometryStability(harness);
    verifyFocusAndEditorPreservation(harness);
    verifySizing(harness);
    verifyPersistence(harness);
    verifyDeferredPersistence();
    verifyBadgesAndTheme(harness);
    verifySharedBottomStrip(harness);

    std::cout << (checks - failures) << "/" << checks
              << " bottom tool drawer checks passed\n";
    return failures == 0 ? 0 : 1;
}
