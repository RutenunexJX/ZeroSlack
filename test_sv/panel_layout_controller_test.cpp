#include "actionregistry.h"
#include "panellayoutcontroller.h"

#include <QApplication>
#include <QDockWidget>
#include <QLabel>
#include <QMainWindow>
#include <QPoint>
#include <QSize>
#include <QSizePolicy>
#include <QStyle>
#include <QTabBar>
#include <QWidget>

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

void processUiEvents()
{
    for (int iteration = 0; iteration < 3; ++iteration) {
        QApplication::sendPostedEvents();
        QApplication::processEvents();
    }
}

int visibleBottomContentHeight(QMainWindow* window,
                               QTabBar* bar)
{
    if (!window || !window->centralWidget()
        || !bar || !bar->isVisible()) {
        return -1;
    }
    QWidget* central = window->centralWidget();
    const int centralBottom =
        central->mapTo(window, QPoint(0, 0)).y()
        + central->height();
    const int tabTop = bar->mapTo(window, QPoint(0, 0)).y();
    const int separatorExtent = window->style()->pixelMetric(
        QStyle::PM_DockWidgetSeparatorExtent,
        nullptr,
        window);
    return qMax(0,
                tabTop - centralBottom
                    - qMax(0, separatorExtent));
}

class TallMinimumPanel final : public QLabel
{
public:
    explicit TallMinimumPanel(const QString& text,
                              QWidget* parent = nullptr)
        : QLabel(text, parent)
    {
        setMinimumHeight(180);
    }

    QSize minimumSizeHint() const override
    {
        return QSize(160, 180);
    }

    QSize sizeHint() const override
    {
        return QSize(320, 260);
    }
};

QDockWidget* makeDock(QMainWindow* window,
                      const QString& title,
                      const QString& objectName)
{
    auto* dock = new QDockWidget(title, window);
    dock->setObjectName(objectName);
    dock->setWidget(new QLabel(title, dock));
    window->addDockWidget(Qt::BottomDockWidgetArea, dock);
    return dock;
}

QDockWidget* makeTallDock(QMainWindow* window,
                          const QString& title,
                          const QString& objectName)
{
    auto* dock = new QDockWidget(title, window);
    dock->setObjectName(objectName);
    dock->setWidget(new TallMinimumPanel(title, dock));
    window->addDockWidget(Qt::BottomDockWidgetArea, dock);
    return dock;
}
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QMainWindow window;
    window.resize(900, 640);
    window.setCentralWidget(new QWidget(&window));

    auto* navigation = new QDockWidget(QStringLiteral("Navigation"), &window);
    navigation->setObjectName(QStringLiteral("navigationDock"));
    navigation->setWidget(new QLabel(QStringLiteral("Navigation"), navigation));
    window.addDockWidget(Qt::LeftDockWidgetArea, navigation);

    QDockWidget* problems =
        makeDock(&window,
                 QStringLiteral("Problems"),
                 QStringLiteral("problemsDock"));
    QDockWidget* activity =
        makeDock(&window,
                 QStringLiteral("Activity"),
                 QStringLiteral("activityDock"));
    window.tabifyDockWidget(problems, activity);

    PanelLayoutController controller(&window);
    controller.setNavigationDock(navigation);
    check(controller.registerBottomPanel(
              QStringLiteral("problems"), problems),
          "first bottom panel registers");
    check(controller.registerBottomPanel(
              QStringLiteral("activity"), activity),
          "second bottom panel registers");
    check(!controller.registerBottomPanel(
              QStringLiteral("activity"), activity),
          "duplicate bottom panel is rejected");
    controller.finalize();

    window.show();
    QApplication::processEvents();
    check(controller.bottomPanelIds()
              == QStringList{
                  QStringLiteral("problems"),
                  QStringLiteral("activity")},
          "registration defines a stable initial order");
    QTabBar* bottomTabs =
        window.findChild<QTabBar*>(
            QStringLiteral("bottomPanelTabBar"));
    check(bottomTabs && bottomTabs->isMovable()
              && bottomTabs->tabsClosable(),
          "bottom pages expose drag reorder and close controls");
    int stateChangeCount = 0;
    controller.setStateChangedHandler(
        [&stateChangeCount]() { ++stateChangeCount; });
    const QList<BottomPanelContextAction>
        initialPanelContext =
            controller.bottomPanelContextActions(
                QStringLiteral("problems"));
    const QStringList expectedPanelContextIds = {
        QString::fromLatin1(
            ActionIds::ViewBottomPanelPinned),
        QString::fromLatin1(
            ActionIds::ViewBottomPanelClose),
    };
    QStringList actualPanelContextIds;
    bool panelContextMetadataMatches =
        initialPanelContext.size()
        == expectedPanelContextIds.size();
    for (const BottomPanelContextAction& item :
         initialPanelContext) {
        actualPanelContextIds.append(item.actionId);
        const ActionDescriptor* descriptor =
            findActionById(item.actionId);
        panelContextMetadataMatches =
            panelContextMetadataMatches
            && descriptor
            && item.label
                   == descriptor
                          ->aliasForSurface(
                              ActionSurface::PanelContextMenu)
                          .label
            && item.executionRoute
                   == descriptor->executionRoute
            && item.enabled;
    }
    check(actualPanelContextIds
                  == expectedPanelContextIds
              && panelContextMetadataMatches,
          "bottom-page context model consumes Registry metadata");

    QStringList requestedPanelActionIds;
    QStringList requestedPanelIds;
    controller.setRegisteredPanelActionRequestHandler(
        [&controller,
         &requestedPanelActionIds,
         &requestedPanelIds](
            const QString& actionId,
            const QString& panelId,
            QString* failureReason) {
            requestedPanelActionIds.append(actionId);
            requestedPanelIds.append(panelId);
            bool succeeded = false;
            if (actionId
                == QString::fromLatin1(
                    ActionIds::ViewBottomPanelPinned)) {
                succeeded = controller.setPanelPinned(
                    panelId,
                    !controller.isPanelPinned(panelId));
            } else if (actionId
                       == QString::fromLatin1(
                           ActionIds::ViewBottomPanelClose)) {
                succeeded =
                    controller.closePanel(panelId);
            }
            if (failureReason) {
                if (succeeded)
                    failureReason->clear();
                else
                    *failureReason = QStringLiteral("failed");
            }
            return succeeded;
        });
    QString panelActionFailure =
        QStringLiteral("stale failure");
    const QString pinPanelActionId =
        QString::fromLatin1(
            ActionIds::ViewBottomPanelPinned);
    check(controller.requestBottomPanelAction(
              pinPanelActionId,
              QStringLiteral("problems"),
              &panelActionFailure)
              && panelActionFailure.isEmpty()
              && requestedPanelActionIds
                     == QStringList{pinPanelActionId}
              && requestedPanelIds
                     == QStringList{
                         QStringLiteral("problems")}
              && controller.isPanelPinned(
                     QStringLiteral("problems")),
          "bottom-page pin requests its canonical registered Action");

    const QList<BottomPanelContextAction>
        pinnedPanelContext =
            controller.bottomPanelContextActions(
                QStringLiteral("problems"));
    const QString closePanelActionId =
        QString::fromLatin1(
            ActionIds::ViewBottomPanelClose);
    panelActionFailure.clear();
    check(pinnedPanelContext.size() == 2
              && pinnedPanelContext.first().label
                     == QStringLiteral("Unpin Page")
              && !pinnedPanelContext.last().enabled
              && !controller.requestBottomPanelAction(
                  closePanelActionId,
                  QStringLiteral("problems"),
                  &panelActionFailure)
              && !panelActionFailure.isEmpty()
              && requestedPanelActionIds.size() == 1,
          "pinned page exposes dynamic label and blocks close before dispatch");

    panelActionFailure = QStringLiteral("stale failure");
    const bool contextFixtureRestored =
        controller.requestBottomPanelAction(
            pinPanelActionId,
            QStringLiteral("problems"),
            &panelActionFailure)
        && panelActionFailure.isEmpty()
        && controller.requestBottomPanelAction(
            closePanelActionId,
            QStringLiteral("problems"),
            &panelActionFailure)
        && panelActionFailure.isEmpty()
        && !controller.isPanelOpen(
            QStringLiteral("problems"))
        && controller.restorePanel(
            QStringLiteral("problems"));
    check(contextFixtureRestored
              && requestedPanelActionIds
                     == QStringList{
                         pinPanelActionId,
                         pinPanelActionId,
                         closePanelActionId}
              && requestedPanelIds
                     == QStringList{
                         QStringLiteral("problems"),
                         QStringLiteral("problems"),
                         QStringLiteral("problems")},
          "bottom-page unpin and close share the registered Action route");
    bottomTabs =
        window.findChild<QTabBar*>(
            QStringLiteral("bottomPanelTabBar"));
    activity->show();
    activity->raise();
    QApplication::processEvents();
    if (bottomTabs && bottomTabs->count() >= 2) {
        const int originalIndex = bottomTabs->currentIndex();
        const int selectedIndex =
            originalIndex == 0 ? 1 : 0;
        const QString selectedTitle =
            bottomTabs->tabText(selectedIndex);
        const int changesBeforeSelection = stateChangeCount;
        bottomTabs->setCurrentIndex(selectedIndex);
        QApplication::processEvents();
        check(stateChangeCount == changesBeforeSelection + 1
                  && controller.layoutState().activeBottomPanel
                         == (selectedTitle == QStringLiteral("Problems")
                                 ? QStringLiteral("problems")
                                 : QStringLiteral("activity")),
              "bottom page selection schedules active-page persistence once");
        bottomTabs->setCurrentIndex(selectedIndex);
        QApplication::processEvents();
        check(stateChangeCount == changesBeforeSelection + 1,
              "reselecting the active bottom page does not reschedule persistence");
        bottomTabs->setCurrentIndex(originalIndex);
        QApplication::processEvents();
    }
    const bool navigationVisibleBeforeToggle =
        controller.layoutState().navigationVisible;
    const int changesBeforeNavigationToggle = stateChangeCount;
    navigation->toggleViewAction()->trigger();
    QApplication::processEvents();
    check(stateChangeCount == changesBeforeNavigationToggle + 1
              && controller.layoutState().navigationVisible
                     != navigationVisibleBeforeToggle,
          "navigation visibility change schedules layout persistence once");
    navigation->toggleViewAction()->trigger();
    QApplication::processEvents();
    check(stateChangeCount == changesBeforeNavigationToggle + 2
              && controller.layoutState().navigationVisible
                     == navigationVisibleBeforeToggle,
          "navigation visibility restore schedules and preserves the original state");

    activity->raise();
    processUiEvents();
    window.resizeDocks({activity}, {173}, Qt::Vertical);
    processUiEvents();
    const int expandedDockHeight =
        visibleBottomContentHeight(&window, bottomTabs);
    check(expandedDockHeight > 64,
          "bottom panel accepts a user-selected expanded height");
    controller.setBottomCollapsed(true);
    processUiEvents();
    const int collapsedContentHeight =
        visibleBottomContentHeight(&window, bottomTabs);
    check(controller.isBottomCollapsed()
              && collapsedContentHeight <= 1
              && problems->widget()->maximumHeight() > 0
              && activity->widget()->maximumHeight() > 0,
          "collapse leaves no page content visible without locking expansion");
    check(collapsedContentHeight <= expandedDockHeight,
          "collapse never increases the bottom panel height");
    check(controller.layoutState().expandedBottomHeight
              == expandedDockHeight,
          "collapse records the exact current expanded height");
    controller.setBottomCollapsed(false);
    processUiEvents();
    check(!controller.isBottomCollapsed()
              && problems->widget()->maximumHeight() > 0
              && visibleBottomContentHeight(&window, bottomTabs)
                     == expandedDockHeight,
          "expansion restores panel constraints and exact height");

    check(controller.setPanelPinned(
              QStringLiteral("problems"), true)
              && controller.isPanelPinned(
                  QStringLiteral("problems")),
          "a bottom page can be pinned");
    check(!controller.closePanel(QStringLiteral("problems"))
              && controller.isPanelOpen(
                  QStringLiteral("problems")),
          "a pinned page cannot be closed");
    check(controller.setPanelPinned(
              QStringLiteral("problems"), false)
              && controller.closePanel(
                  QStringLiteral("problems"))
              && !controller.isPanelOpen(
                  QStringLiteral("problems")),
          "an unpinned page can be closed");
    check(controller.restorePanel(QStringLiteral("problems"))
              && controller.isPanelOpen(
                  QStringLiteral("problems")),
          "a closed page restores without duplication");
    const int dockCountBeforeRepeatedRestore =
        window.findChildren<QDockWidget*>(
            QString(),
            Qt::FindDirectChildrenOnly).size();
    QWidget* const problemsContentBeforeRepeatedRestore =
        problems->widget();
    for (int iteration = 0; iteration < 6; ++iteration)
        controller.restorePanel(QStringLiteral("problems"));
    check(window.findChildren<QDockWidget*>(
              QString(),
              Qt::FindDirectChildrenOnly).size()
              == dockCountBeforeRepeatedRestore
              && problems->widget()
                     == problemsContentBeforeRepeatedRestore,
          "repeated business restore reuses the registered page");
    activity->hide();
    QApplication::processEvents();
    const bool directHideTracked =
        !controller.isPanelOpen(
            QStringLiteral("activity"));
    activity->show();
    activity->raise();
    QApplication::processEvents();
    check(directHideTracked
              && controller.isPanelOpen(
                  QStringLiteral("activity")),
          "programmatic business visibility updates the registered page state");

    check(controller.movePanel(
              QStringLiteral("activity"), 0)
              && controller.bottomPanelIds().first()
                     == QStringLiteral("activity"),
          "a bottom page can be reordered");
    QApplication::processEvents();
    bottomTabs =
        window.findChild<QTabBar*>(
            QStringLiteral("bottomPanelTabBar"));
    check(bottomTabs
              && bottomTabs->count() >= 2
              && bottomTabs->tabText(0)
                     == QStringLiteral("Activity"),
          "programmatic reorder changes the real Qt tab order");

    controller.closePanel(QStringLiteral("activity"));
    controller.setBottomCollapsed(true);
    QApplication::processEvents();
    const PanelLayoutState beforeFocus =
        controller.layoutState();
    const int collapsedHeightBeforeFocus =
        problems->height();
    const bool navigationWasOpen =
        navigation->toggleViewAction()->isChecked();
    controller.setFocusModeActive(true);
    QApplication::processEvents();
    check(controller.isFocusModeActive()
              && !navigation->toggleViewAction()->isChecked()
              && !problems->toggleViewAction()->isChecked(),
          "focus mode hides navigation and open bottom pages together");
    controller.setFocusModeActive(false);
    QApplication::processEvents();
    check(!controller.isFocusModeActive()
              && navigation->toggleViewAction()->isChecked()
                     == navigationWasOpen
              && problems->toggleViewAction()->isChecked()
              && !activity->toggleViewAction()->isChecked(),
          "focus mode restores the exact prior open state");
    const PanelLayoutState afterFocus =
        controller.layoutState();
    check(afterFocus.bottomPanelOrder
                  == beforeFocus.bottomPanelOrder
              && afterFocus.closedBottomPanels
                     == beforeFocus.closedBottomPanels
              && afterFocus.pinnedBottomPanels
                     == beforeFocus.pinnedBottomPanels
              && afterFocus.activeBottomPanel
                     == beforeFocus.activeBottomPanel
              && afterFocus.expandedBottomHeight
                     == beforeFocus.expandedBottomHeight
              && afterFocus.bottomCollapsed
                     == beforeFocus.bottomCollapsed
              && afterFocus.navigationVisible
                     == beforeFocus.navigationVisible
              && problems->height()
                     == collapsedHeightBeforeFocus,
          "focus mode preserves order, active page, collapse, and height");

    const int passiveUpdateHeight = problems->height();
    activity->setWindowTitle(
        QStringLiteral("Activity (updated)"));
    if (QLabel* label =
            qobject_cast<QLabel*>(activity->widget())) {
        label->setText(
            QStringLiteral("Passive result update"));
    }
    QApplication::processEvents();
    check(!activity->isVisible()
              && controller.isBottomCollapsed()
              && problems->height() == passiveUpdateHeight,
          "passive result updates preserve closed and collapsed layout");

    PanelLayoutState state = controller.layoutState();
    state.pinnedBottomPanels = {QStringLiteral("problems")};
    state.closedBottomPanels = {QStringLiteral("activity")};
    state.bottomCollapsed = true;
    state.navigationVisible = false;
    state.expandedBottomHeight = 318;
    controller.restoreLayoutState(state);
    QApplication::processEvents();
    const PanelLayoutState restored = controller.layoutState();
    check(restored.valid
              && restored.bottomPanelOrder == state.bottomPanelOrder
              && restored.closedBottomPanels
                     == state.closedBottomPanels
              && restored.pinnedBottomPanels
                     == state.pinnedBottomPanels
              && restored.bottomCollapsed
              && restored.expandedBottomHeight == 318
              && !restored.navigationVisible,
          "workspace panel state restores atomically");

    for (int iteration = 0; iteration < 8; ++iteration) {
        controller.resetLayout();
        QApplication::processEvents();
    }
    const PanelLayoutState reset = controller.layoutState();
    check(reset.bottomPanelOrder
                  == QStringList{
                      QStringLiteral("problems"),
                      QStringLiteral("activity")}
              && reset.closedBottomPanels.isEmpty()
              && reset.pinnedBottomPanels.isEmpty()
              && !reset.bottomCollapsed
              && reset.navigationVisible,
          "repeated reset ignores synchronous Qt tab reorder signals");

    QMainWindow denseWindow;
    denseWindow.resize(1100, 720);
    denseWindow.setCentralWidget(new QWidget(&denseWindow));
    PanelLayoutController denseController(&denseWindow);
    QDockWidget* denseFirst = nullptr;
    const QStringList denseIds = {
        QStringLiteral("problems"),
        QStringLiteral("activity"),
        QStringLiteral("insights"),
        QStringLiteral("graph"),
        QStringLiteral("wave"),
        QStringLiteral("fold"),
    };
    for (const QString& id : denseIds) {
        QDockWidget* dock =
            makeDock(
                &denseWindow,
                id,
                id + QStringLiteral("Dock"));
        if (!denseFirst)
            denseFirst = dock;
        else
            denseWindow.tabifyDockWidget(
                denseFirst,
                dock);
        check(denseController.registerBottomPanel(
                  id,
                  dock),
              "dense bottom page registers");
    }
    denseController.finalize();
    denseWindow.show();
    QApplication::processEvents();
    denseController.closePanel(
        QStringLiteral("activity"));
    denseController.closePanel(
        QStringLiteral("wave"));
    for (int iteration = 0; iteration < 8; ++iteration) {
        denseController.resetLayout();
        QApplication::processEvents();
    }
    bool denseAreasCurrent = true;
    for (const QString& id : denseIds) {
        QDockWidget* dock = denseWindow.findChild<QDockWidget*>(
            id + QStringLiteral("Dock"));
        denseAreasCurrent =
            denseAreasCurrent
            && dock
            && denseWindow.dockWidgetArea(dock)
                   == Qt::BottomDockWidgetArea
            && denseController.isPanelOpen(id);
    }
    check(denseAreasCurrent
              && denseController.bottomPanelIds()
                     == denseIds,
          "six-page reset preserves Qt dock ownership and order");

    QMainWindow resizeWindow;
    resizeWindow.resize(980, 700);
    resizeWindow.setCentralWidget(new QWidget(&resizeWindow));
    QDockWidget* manualFirst = makeTallDock(
        &resizeWindow,
        QStringLiteral("Manual First"),
        QStringLiteral("manualFirstDock"));
    QDockWidget* manualSecond = makeTallDock(
        &resizeWindow,
        QStringLiteral("Manual Second"),
        QStringLiteral("manualSecondDock"));
    resizeWindow.tabifyDockWidget(manualFirst, manualSecond);
    PanelLayoutController resizeController(&resizeWindow);
    check(resizeController.registerBottomPanel(
              QStringLiteral("manualFirst"), manualFirst)
              && resizeController.registerBottomPanel(
                  QStringLiteral("manualSecond"), manualSecond),
          "minimum-size fixture registers as real tabified docks");
    resizeController.finalize();
    resizeWindow.show();
    manualSecond->raise();
    processUiEvents();

    QTabBar* resizeTabs =
        resizeWindow.findChild<QTabBar*>(
            QStringLiteral("bottomPanelTabBar"));
    if (resizeTabs) {
        for (int index = 0; index < resizeTabs->count(); ++index) {
            if (resizeTabs->tabText(index)
                == QStringLiteral("Manual Second")) {
                resizeTabs->setCurrentIndex(index);
                break;
            }
        }
        manualSecond->raise();
        processUiEvents();
    }
    check(resizeTabs
              && resizeTabs->isVisible()
              && resizeTabs->contextMenuPolicy()
                     == Qt::CustomContextMenu
              && manualFirst->widget()->minimumHeight() == 0
              && manualSecond->widget()->minimumHeight() == 0
              && manualFirst->widget()->sizePolicy().verticalPolicy()
                     == QSizePolicy::Ignored
              && manualSecond->widget()->sizePolicy().verticalPolicy()
                     == QSizePolicy::Ignored
              && resizeController.activeBottomPanelId()
                     == QStringLiteral("manualSecond"),
          "bottom pages ignore content minimum-size hints while preserving the tab bar");

    resizeWindow.resizeDocks(
        {manualSecond}, {260}, Qt::Vertical);
    processUiEvents();
    const int startingExpandedHeight =
        visibleBottomContentHeight(&resizeWindow, resizeTabs);
    int previousContentHeight = startingExpandedHeight;
    bool shrankContinuously = startingExpandedHeight > 64;
    const QList<int> requestedHeights = {180, 120, 70, 35, 1};
    for (int requestedHeight : requestedHeights) {
        resizeWindow.resizeDocks(
            {manualSecond}, {requestedHeight}, Qt::Vertical);
        processUiEvents();
        const int contentHeight =
            visibleBottomContentHeight(&resizeWindow, resizeTabs);
        shrankContinuously =
            shrankContinuously
            && contentHeight <= previousContentHeight;
        previousContentHeight = contentHeight;
    }
    const PanelLayoutState manuallyCollapsed =
        resizeController.layoutState();
    const int collapsedCentralHeight =
        resizeWindow.centralWidget()->height();
    const int collapsedBottomAreaHeight =
        resizeWindow.height() - collapsedCentralHeight;
    check(shrankContinuously
              && previousContentHeight <= 1
              && resizeTabs
              && collapsedBottomAreaHeight
                     <= resizeTabs->height() + 12
              && resizeController.isBottomCollapsed()
              && manuallyCollapsed.bottomCollapsed
              && manuallyCollapsed.expandedBottomHeight >= 64,
          "native dock resizing continuously reaches tab-only height and enters the shared collapsed state");
    check(resizeTabs
              && resizeTabs->isVisible()
              && manualSecond->toggleViewAction()->isChecked(),
          "manual collapse keeps the dock and its interactive tab bar visible");

    if (resizeTabs && resizeTabs->count() >= 2) {
        const int otherIndex =
            resizeTabs->currentIndex() == 0 ? 1 : 0;
        resizeTabs->setCurrentIndex(otherIndex);
        processUiEvents();
    }
    QDockWidget* selectedManualDock =
        resizeController.activeBottomPanelId()
                == QStringLiteral("manualFirst")
            ? manualFirst
            : manualSecond;
    check(resizeWindow.centralWidget()->height()
                  == collapsedCentralHeight
              && visibleBottomContentHeight(
                     &resizeWindow, resizeTabs) <= 1
              && resizeController.isBottomCollapsed(),
          "switching bottom pages at tab-only height preserves panel geometry");

    resizeWindow.resizeDocks(
        {selectedManualDock}, {260}, Qt::Vertical);
    processUiEvents();
    const int manuallyReopenedHeight =
        visibleBottomContentHeight(&resizeWindow, resizeTabs);
    check(manuallyReopenedHeight > 64
              && !resizeController.isBottomCollapsed(),
          "the native separator reopens a manually collapsed bottom panel");
    resizeWindow.resizeDocks(
        {selectedManualDock}, {1}, Qt::Vertical);
    processUiEvents();
    const PanelLayoutState recollapsedState =
        resizeController.layoutState();
    check(recollapsedState.bottomCollapsed
              && recollapsedState.expandedBottomHeight
                     == manuallyReopenedHeight,
          "manual collapse persists the last stable expanded dock height");

    resizeController.setBottomCollapsed(false);
    processUiEvents();
    check(!resizeController.isBottomCollapsed()
              && qAbs(visibleBottomContentHeight(
                          &resizeWindow, resizeTabs)
                      - manuallyReopenedHeight) <= 1,
          "explicit expansion uses the same persisted height as manual resizing");
    resizeController.setBottomCollapsed(true);
    processUiEvents();
    resizeWindow.resizeDocks(
        {selectedManualDock}, {260}, Qt::Vertical);
    processUiEvents();
    check(!resizeController.isBottomCollapsed()
              && visibleBottomContentHeight(
                     &resizeWindow, resizeTabs) > 4,
          "a panel collapsed explicitly can be dragged open from its tab bar");

    resizeController.setBottomCollapsed(true);
    processUiEvents();
    const PanelLayoutState savedManualState =
        resizeController.layoutState();
    QMainWindow restoredWindow;
    restoredWindow.resize(980, 700);
    restoredWindow.setCentralWidget(new QWidget(&restoredWindow));
    QDockWidget* restoredFirst = makeTallDock(
        &restoredWindow,
        QStringLiteral("Manual First"),
        QStringLiteral("restoredManualFirstDock"));
    QDockWidget* restoredSecond = makeTallDock(
        &restoredWindow,
        QStringLiteral("Manual Second"),
        QStringLiteral("restoredManualSecondDock"));
    restoredWindow.tabifyDockWidget(restoredFirst, restoredSecond);
    PanelLayoutController restoredController(&restoredWindow);
    restoredController.registerBottomPanel(
        QStringLiteral("manualFirst"), restoredFirst);
    restoredController.registerBottomPanel(
        QStringLiteral("manualSecond"), restoredSecond);
    restoredController.finalize();
    restoredWindow.show();
    processUiEvents();
    restoredController.restoreLayoutState(savedManualState);
    processUiEvents();
    QTabBar* restoredTabs =
        restoredWindow.findChild<QTabBar*>(
            QStringLiteral("bottomPanelTabBar"));
    QDockWidget* restoredActive =
        restoredController.activeBottomPanelId()
                == QStringLiteral("manualFirst")
            ? restoredFirst
            : restoredSecond;
    const PanelLayoutState roundTrippedManualState =
        restoredController.layoutState();
    check(restoredController.isBottomCollapsed()
              && restoredTabs
              && restoredTabs->isVisible()
              && visibleBottomContentHeight(
                     &restoredWindow, restoredTabs) <= 1
              && roundTrippedManualState.expandedBottomHeight
                     == savedManualState.expandedBottomHeight,
          "tab-only state and expanded height survive session restoration");
    restoredWindow.resizeDocks(
        {restoredActive}, {260}, Qt::Vertical);
    processUiEvents();
    check(!restoredController.isBottomCollapsed()
              && visibleBottomContentHeight(
                     &restoredWindow, restoredTabs) > 4,
          "a restored tab-only panel remains draggable upward");

    std::cout << (checks - failures) << "/" << checks
              << " panel layout checks passed\n";
    return failures == 0 ? 0 : 1;
}
