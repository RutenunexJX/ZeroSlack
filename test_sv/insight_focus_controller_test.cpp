#include "actionregistry.h"
#include "graphexportui.h"
#include "insightfocuscontroller.h"

#include <QApplication>
#include <QDockWidget>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QStackedWidget>
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

QWidget* makePanel(const QString& objectName)
{
    QWidget* panel = new QWidget;
    panel->setObjectName(objectName);
    auto* layout = new QVBoxLayout(panel);
    layout->addWidget(new QLabel(objectName, panel));
    layout->addStretch(1);
    return panel;
}
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    resetApplicationActionExecutionHistory();

    QMainWindow window;
    auto* stack = new QStackedWidget(&window);
    auto* editorPage = new QWidget(stack);
    editorPage->setObjectName(QStringLiteral("editorPage"));
    stack->addWidget(editorPage);
    window.setCentralWidget(stack);
    window.resize(1100, 760);

    InsightFocusController controller(stack, editorPage, &window);

    QDockWidget navigationDock(
        QStringLiteral("Navigation"),
        &window);
    navigationDock.setObjectName(
        QStringLiteral("focusTestNavigationDock"));
    navigationDock.setWidget(
        makePanel(QStringLiteral("navigationPanel")));
    window.addDockWidget(
        Qt::LeftDockWidgetArea,
        &navigationDock);

    QDockWidget firstDock(QStringLiteral("RTL Insights"), &window);
    firstDock.setObjectName(
        QStringLiteral("focusTestRtlDock"));
    QWidget* firstPanel = makePanel(QStringLiteral("firstInsightPanel"));
    firstDock.setWidget(firstPanel);
    window.addDockWidget(Qt::BottomDockWidgetArea, &firstDock);

    int fitCount = 0;
    int zoomInCount = 0;
    int zoomOutCount = 0;
    int inspectorCount = 0;
    int retainedZoom = 100;
    QString searchText = QStringLiteral("initial");

    InsightFocusPanelRegistration first;
    first.id = QStringLiteral("rtlInsights");
    first.title = QStringLiteral("RTL Insights");
    first.dock = &firstDock;
    first.fit = [&]() { ++fitCount; retainedZoom = 100; };
    first.zoomIn = [&]() { ++zoomInCount; retainedZoom += 10; };
    first.zoomOut = [&]() { ++zoomOutCount; retainedZoom -= 10; };
    first.setSearchText = [&](const QString& text) { searchText = text; };
    first.searchText = [&]() { return searchText; };
    first.showInspector = [&]() { ++inspectorCount; };
    check(controller.registerPanel(first),
          "first panel registration succeeds");
    check(firstPanel->findChild<QPushButton*>(
              QStringLiteral("insightFocusEnter.rtlInsights"))
              != nullptr,
          "panel receives an explicit Focus View entry button");

    QDockWidget secondDock(QStringLiteral("Wave Preview"), &window);
    secondDock.setObjectName(
        QStringLiteral("focusTestWaveDock"));
    QWidget* secondPanel = makePanel(QStringLiteral("secondInsightPanel"));
    secondDock.setWidget(secondPanel);
    window.addDockWidget(Qt::BottomDockWidgetArea, &secondDock);
    window.tabifyDockWidget(&firstDock, &secondDock);
    InsightFocusPanelRegistration second;
    second.id = QStringLiteral("wavePreview");
    second.title = QStringLiteral("Wave Preview");
    second.dock = &secondDock;
    second.fit = []() {};
    second.zoomIn = []() {};
    second.zoomOut = []() {};
    second.setSearchText = [](const QString&) {};
    second.searchText = []() { return QStringLiteral("wave"); };
    second.showInspector = []() {};
    check(controller.registerPanel(second),
          "second panel registration succeeds");

    window.show();
    QApplication::processEvents();
    firstDock.show();
    secondDock.show();
    navigationDock.show();
    firstDock.raise();
    firstPanel->setMinimumHeight(0);
    firstPanel->setMaximumHeight(0);
    secondPanel->setMinimumHeight(0);
    secondPanel->setMaximumHeight(0);
    QApplication::processEvents();
    const QByteArray dockStateBeforeFocus =
        window.saveState();
    const bool firstVisibleBeforeFocus =
        firstDock.isVisible();
    const bool secondVisibleBeforeFocus =
        secondDock.isVisible();
    const bool navigationVisibleBeforeFocus =
        navigationDock.isVisible();

    check(controller.enter(QStringLiteral("rtlInsights")),
          "Focus View enters registered panel");
    QApplication::processEvents();
    check(controller.isFocused(),
          "controller reports focused state");
    check(controller.focusedPanelId()
              == QStringLiteral("rtlInsights"),
          "focused panel id is stable");
    check(controller.focusedPanelWidget() == firstPanel,
          "Focus View reparents the exact panel widget");
    check(firstDock.widget() == nullptr,
          "dock does not retain a duplicate widget");
    check(!firstDock.isVisible() && !secondDock.isVisible(),
          "Focus View hides surrounding docks to preserve central geometry");
    check(stack->currentWidget() == controller.focusPage(),
          "central stack shows Focus View");
    QLabel* focusTitle =
        controller.focusPage()->findChild<QLabel*>(
            QStringLiteral("insightFocusTitle"));
    check(focusTitle
              && focusTitle->text()
                     == QStringLiteral(
                         "RTL Insights \u2014 Focus View"),
          "Focus View title uses a stable readable separator");
    check(firstPanel->width() >= 640
              && firstPanel->height() >= 360,
          "focused panel has readable typical-desktop geometry");
    check(firstPanel->maximumHeight()
              == QWIDGETSIZE_MAX,
          "Focus View temporarily overrides a collapsed panel constraint");

    QPushButton* fit =
        controller.focusPage()->findChild<QPushButton*>(
            QStringLiteral("insightFocusFitButton"));
    QPushButton* zoomIn =
        controller.focusPage()->findChild<QPushButton*>(
            QStringLiteral("insightFocusZoomInButton"));
    QPushButton* zoomOut =
        controller.focusPage()->findChild<QPushButton*>(
            QStringLiteral("insightFocusZoomOutButton"));
    QPushButton* inspector =
        controller.focusPage()->findChild<QPushButton*>(
            QStringLiteral("insightFocusInspectorButton"));
    QLineEdit* search =
        controller.focusPage()->findChild<QLineEdit*>(
            QStringLiteral("insightFocusSearchEdit"));
    check(fit && zoomIn && zoomOut && inspector && search,
          "Focus View exposes Fit, zoom, search, and Inspector controls");
    check(fit
              && zoomIn
              && zoomOut
              && fit->text() == QStringLiteral("Fit")
              && zoomIn->text()
                     == QStringLiteral("Zoom In")
              && zoomOut->text()
                     == QStringLiteral("Zoom Out")
              && fit->property(
                     GraphExportUi::kActionIdProperty)
                     .toString()
                     == QString::fromLatin1(
                         ActionIds::GraphViewFit)
              && zoomIn->property(
                     GraphExportUi::kExecutionRouteProperty)
                     .toString()
                     == QStringLiteral(
                         "insight.graphView.zoomIn")
              && zoomOut->property(
                     GraphExportUi::kActionIdProperty)
                     .toString()
                     == QString::fromLatin1(
                         ActionIds::GraphViewZoomOut),
          "Focus graph controls materialize Registry labels and routes");
    if (fit)
        fit->click();
    if (zoomIn)
        zoomIn->click();
    if (zoomOut)
        zoomOut->click();
    if (inspector)
        inspector->click();
    if (search)
        search->setText(QStringLiteral("state_q"));
    check(fitCount == 1
              && zoomInCount == 1
              && zoomOutCount == 1
              && inspectorCount == 1,
          "Focus View controls route to the registered single-state panel");
    check(!applicationActionExecutionHistory()
               .hasRepeatableAction(),
          "contextual Focus graph Actions preserve repeat history");
    check(searchText == QStringLiteral("state_q"),
          "Focus View search updates the panel search state");

    controller.leaveToEditor();
    QApplication::processEvents();
    check(stack->currentWidget() == editorPage,
          "Back returns to the editor page");
    check(firstDock.widget() == firstPanel,
          "Back restores the exact widget to its dock");
    check(firstDock.isVisible()
                  == firstVisibleBeforeFocus
              && secondDock.isVisible()
                     == secondVisibleBeforeFocus
              && navigationDock.isVisible()
                     == navigationVisibleBeforeFocus
              && window.saveState()
                     == dockStateBeforeFocus,
          "Back restores exact Dock visibility, tab order, and sizes");
    check(firstPanel->maximumHeight() == 0,
          "Back restores the original collapsed panel constraint");
    check(retainedZoom == 100
              && searchText == QStringLiteral("state_q"),
          "returning to the editor preserves panel zoom and search state");

    check(controller.enter(QStringLiteral("rtlInsights")),
          "same panel can re-enter Focus View");
    QApplication::processEvents();
    QLineEdit* restoredSearch =
        controller.focusPage()->findChild<QLineEdit*>(
            QStringLiteral("insightFocusSearchEdit"));
    check(restoredSearch
              && restoredSearch->text()
                     == QStringLiteral("state_q"),
          "re-entering Focus View restores shared search state");

    check(controller.enter(QStringLiteral("wavePreview")),
          "switching Focus View panels succeeds");
    QApplication::processEvents();
    check(firstDock.widget() == firstPanel
              && controller.focusedPanelWidget() == secondPanel
              && secondDock.widget() == nullptr
              && firstPanel->maximumHeight() == 0
              && secondPanel->maximumHeight()
                     == QWIDGETSIZE_MAX,
          "switching panels restores the first and reparents only the second");

    controller.returnToDock();
    QApplication::processEvents();
    check(stack->currentWidget() == editorPage
              && secondDock.widget() == secondPanel
              && secondDock.isVisible()
              && secondPanel->maximumHeight() == 0
              && navigationDock.isVisible()
                     == navigationVisibleBeforeFocus,
          "Return to Dock restores the same widget and reveals the dock");

    auto* transientDock = new QDockWidget(
        QStringLiteral("Transient Insight"), &window);
    transientDock->setObjectName(
        QStringLiteral("focusTestTransientDock"));
    transientDock->setWidget(
        makePanel(QStringLiteral("transientInsightPanel")));
    window.addDockWidget(Qt::BottomDockWidgetArea, transientDock);
    InsightFocusPanelRegistration transient;
    transient.id = QStringLiteral("transientInsight");
    transient.title = QStringLiteral("Transient Insight");
    transient.dock = transientDock;
    check(controller.registerPanel(transient),
          "transient panel registration succeeds");
    delete transientDock;
    QApplication::processEvents();
    check(!controller.enter(QStringLiteral("transientInsight")),
          "destroyed dock is rejected without dereferencing stale state");

    std::cout << (checks - failures) << "/" << checks
              << " Insight Focus controller checks passed\n";
    return failures == 0 ? 0 : 1;
}
