#include "actionregistry.h"
#include "applicationthememanager.h"
#include "insightvisualstyle.h"
#include "panellayoutcontroller.h"

#include <QApplication>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTabWidget>
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
            {QStringLiteral("rtlHighRiskEdit"), QStringLiteral("High+Diff")},
            {QStringLiteral("connections"), QStringLiteral("Connections")},
            {QStringLiteral("foldShelf"), QStringLiteral("Fold Shelf")},
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
        window.show();
        QApplication::processEvents();
    }

    QMainWindow window;
    QPlainTextEdit* editor = nullptr;
    PanelLayoutController* controller = nullptr;
    QHash<QString, PanelFixture> fixtures;
};

void verifyButtonContract(DrawerHarness& harness)
{
    const QStringList ids = {
        QStringLiteral("problems"),
        QStringLiteral("scopedSearch"),
        QStringLiteral("activity"),
        QStringLiteral("rtlHighRiskEdit"),
        QStringLiteral("connections"),
        QStringLiteral("foldShelf"),
    };
    const QStringList labels = {
        QStringLiteral("Problems"),
        QStringLiteral("Search"),
        QStringLiteral("Activity"),
        QStringLiteral("High+Diff"),
        QStringLiteral("Connections"),
        QStringLiteral("Shelf"),
    };
    check(harness.controller->bottomPanelIds() == ids,
          "six bottom buttons keep the fixed product order");
    for (int index = 0; index < ids.size(); ++index) {
        QToolButton* button =
            harness.controller->buttonForPanel(ids.at(index));
        check(button
                  && button->text() == labels.at(index)
                  && button->accessibleName() == labels.at(index)
                  && !button->icon().isNull()
                  && button->toolTip().contains(QStringLiteral("Ctrl+J")),
              "bottom button exposes icon text accessible name and shortcut tooltip");
    }
    check(harness.controller->resizeHandle()
              && harness.controller->resizeHandle()->height() >= 6,
          "drawer exposes an accessible resize hit target");
}

void verifyClickAndShortcutBehavior(DrawerHarness& harness)
{
    QToolButton* problems =
        harness.controller->buttonForPanel(QStringLiteral("problems"));
    QToolButton* search =
        harness.controller->buttonForPanel(QStringLiteral("scopedSearch"));
    QToolButton* activity =
        harness.controller->buttonForPanel(QStringLiteral("activity"));
    check(problems && problems->isChecked()
              && harness.controller->activeBottomPanelId()
                     == QStringLiteral("problems"),
          "first panel starts selected");

    QTest::mouseClick(problems, Qt::LeftButton);
    QApplication::processEvents();
    check(harness.controller->isBottomCollapsed()
              && harness.controller->buttonBar()->isVisibleTo(&harness.window)
              && !problems->isChecked(),
          "clicking the active button fully closes content but keeps the bar");
    QTest::mouseClick(problems, Qt::LeftButton);
    QApplication::processEvents();
    check(!harness.controller->isBottomCollapsed()
              && problems->isChecked(),
          "clicking the last button restores its panel");

    QTest::mouseClick(search, Qt::LeftButton);
    QApplication::processEvents();
    check(harness.controller->activeBottomPanelId()
                  == QStringLiteral("scopedSearch")
              && search->isChecked()
              && !problems->isChecked()
              && harness.controller->isPanelOpen(
                  QStringLiteral("scopedSearch"))
              && !harness.controller->isPanelOpen(
                  QStringLiteral("problems")),
          "switching buttons keeps exactly one content panel visible");

    QTest::mouseClick(activity, Qt::LeftButton);
    QApplication::processEvents();
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
    check(!harness.controller->animationsEnabled(),
          "bottom drawer height animation is disabled by default");
    harness.controller->restorePanel(QStringLiteral("problems"));
    harness.controller->resetPanelHeight(QStringLiteral("problems"));
    QApplication::processEvents();

    QToolButton* problems =
        harness.controller->buttonForPanel(QStringLiteral("problems"));
    QWidget* content = harness.controller->drawerContent();
    QWidget* buttonBar = harness.controller->buttonBar();
    check(problems && content && buttonBar,
          "click stability fixtures are available");
    if (!problems || !content || !buttonBar)
        return;

    const QRect windowGeometry = harness.window.geometry();
    const int buttonBarBottom =
        buttonBar->mapToGlobal(buttonBar->rect().bottomLeft()).y();
    QTest::mouseClick(problems, Qt::LeftButton);
    QApplication::processEvents();
    check(harness.controller->isBottomCollapsed()
              && harness.window.geometry() == windowGeometry
              && buttonBar->mapToGlobal(
                     buttonBar->rect().bottomLeft()).y()
                     == buttonBarBottom,
          "collapse click keeps the window and button bar stationary");

    QTest::mouseClick(problems, Qt::LeftButton);
    QApplication::processEvents();
    check(!harness.controller->isBottomCollapsed()
              && content->height()
                     == PanelLayoutController::kDefaultContentHeight
              && harness.window.geometry() == windowGeometry
              && buttonBar->mapToGlobal(
                     buttonBar->rect().bottomLeft()).y()
                     == buttonBarBottom,
          "expand click restores height without moving the window or buttons");

    QTest::mouseClick(problems, Qt::LeftButton);
    QTest::mouseClick(problems, Qt::LeftButton);
    QApplication::processEvents();
    check(!harness.controller->isBottomCollapsed()
              && problems->isChecked()
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
    check(harness.controller->isBottomCollapsed()
              && QApplication::focusWidget() == harness.editor,
          "panel-scoped Escape closes the drawer and restores editor focus");
    check(harness.editor->textCursor().anchor() == anchor
              && harness.editor->textCursor().position() == position
              && harness.editor->verticalScrollBar()->value() == scroll,
          "close preserves editor cursor selection and scroll");

    const int collapsedEditorHeight = harness.editor->height();
    harness.controller->setBottomCollapsed(false);
    QApplication::processEvents();
    check(harness.editor->height() < collapsedEditorHeight
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

void verifyPersistenceAndFocusIsolation(DrawerHarness& harness)
{
    const PanelLayoutState state = preparePersistentState(harness);
    check(state.bottomPanelHeights.value(QStringLiteral("connections")) == 330
              && state.bottomPanelViewStates
                     .value(QStringLiteral("connections"))
                     .contains(QStringLiteral("tabs")),
          "workspace state captures per-panel height and internal view state");

    const PanelLayoutState beforeFocus =
        harness.controller->layoutState();
    harness.controller->setFocusModeActive(true);
    QApplication::processEvents();
    const PanelLayoutState duringFocus =
        harness.controller->layoutState();
    check(!harness.controller->drawerDock()->isVisible()
              && duringFocus.activeBottomPanel
                     == beforeFocus.activeBottomPanel
              && duringFocus.bottomCollapsed
                     == beforeFocus.bottomCollapsed
              && duringFocus.bottomPanelHeights
                     == beforeFocus.bottomPanelHeights
              && duringFocus.navigationVisible
                     == beforeFocus.navigationVisible,
          "Focus Mode hides the drawer without polluting normal layout state");
    harness.controller->setFocusModeActive(false);
    QApplication::processEvents();
    check(harness.controller->activeBottomPanelId()
                  == beforeFocus.activeBottomPanel
              && !harness.controller->isBottomCollapsed(),
          "leaving Focus Mode restores the normal drawer state");

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
              && harness.controller->buttonForPanel(
                     QStringLiteral("problems"))
                     ->accessibleDescription()
                     .contains(QStringLiteral("3")),
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
    QToolButton* activityButton = harness.controller->buttonForPanel(
        QStringLiteral("activity"));
    const int reserve = activityButton->property(
        "badgeReserve").toInt();
    check(harness.controller->activeBottomPanelId() == activeBefore
              && QApplication::focusWidget() == focusBeforeBadge
              && reserve
                     >= activityButton->fontMetrics().horizontalAdvance(
                            QStringLiteral("Running background analysis"))
                            + 22
              && activityButton->sizeHint().width()
                     >= activityButton->fontMetrics().horizontalAdvance(
                            activityButton->text()) + reserve,
          "background Activity status reserves its full badge width without opening the panel or stealing focus");
    harness.controller->setPanelBadge(
        QStringLiteral("activity"), QString(), QString());
    check(harness.controller->panelBadgeText(
              QStringLiteral("activity")).isEmpty()
              && activityButton->property("badgeReserve").toInt() == 0,
          "empty Activity status produces no badge noise");

    ApplicationThemeManager& themeManager =
        ApplicationThemeManager::instance();
    const ThemeMode originalMode = themeManager.mode();
    themeManager.setMode(ThemeMode::Dark);
    QApplication::processEvents();
    QWidget* drawerRoot = harness.controller->buttonBar()->parentWidget();
    check(harness.controller->buttonBar()->isVisible()
              && harness.controller->buttonForPanel(
                     QStringLiteral("problems"))->devicePixelRatioF() > 0.0
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
    DrawerHarness harness;
    verifyButtonContract(harness);
    verifyClickAndShortcutBehavior(harness);
    verifyClickGeometryStability(harness);
    verifyFocusAndEditorPreservation(harness);
    verifySizing(harness);
    verifyPersistenceAndFocusIsolation(harness);
    verifyBadgesAndTheme(harness);

    std::cout << (checks - failures) << "/" << checks
              << " bottom tool drawer checks passed\n";
    return failures == 0 ? 0 : 1;
}
