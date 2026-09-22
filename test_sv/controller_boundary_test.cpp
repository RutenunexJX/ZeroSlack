#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QString>
#include <QStringList>

#include <iostream>

namespace {
int checks = 0;
int failures = 0;

void check(bool condition, const QString& message)
{
    ++checks;
    if (condition)
        return;
    ++failures;
    std::cerr << "FAIL: "
              << message.toStdString()
              << '\n';
}

QString readSource(const QString& root,
                   const QString& relativePath)
{
    QFile file(QDir(root).absoluteFilePath(relativePath));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll());
}

int sourceLineCount(const QString& source)
{
    if (source.isEmpty())
        return 0;
    return source.count(QLatin1Char('\n'))
        + (source.endsWith(QLatin1Char('\n')) ? 0 : 1);
}

bool containsAll(const QString& source,
                 const QStringList& fragments)
{
    for (const QString& fragment : fragments) {
        if (!source.contains(fragment))
            return false;
    }
    return true;
}

bool containsNone(const QString& source,
                  const QStringList& fragments)
{
    for (const QString& fragment : fragments) {
        if (source.contains(fragment))
            return false;
    }
    return true;
}
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    const QString root = argc == 2
        ? QDir::cleanPath(
              QString::fromLocal8Bit(argv[1]))
        : QString();
    check(!root.isEmpty(),
          QStringLiteral("source root argument is present"));

    const QString runtimeHeader =
        readSource(root, QStringLiteral("src/editor/editorruntime.h"));
    const QString runtimeSource =
        readSource(root, QStringLiteral("src/editor/editorruntime.cpp"));
    const QString runtimeCommandsSource =
        readSource(
            root,
            QStringLiteral("src/editor/editorruntimecommands.cpp"));
    const QString selectionSource =
        readSource(
            root,
            QStringLiteral("src/editor/editorselection.cpp"));
    const QString lineOperationSource =
        readSource(
            root,
            QStringLiteral(
                "src/editor/editorlineoperationcontroller.cpp"));
    const QString commandModeSource =
        readSource(
            root,
            QStringLiteral(
                "src/commands/commandlayercoordinator.cpp"));
    const QString mainWindowSource =
        readSource(
            root,
            QStringLiteral("src/app/mainwindow.cpp"));
    const QString mainWindowHeader =
        readSource(
            root,
            QStringLiteral("src/app/mainwindow.h"));
    const QString workspaceSessionCoordinatorHeader =
        readSource(
            root,
            QStringLiteral(
                "src/workspace/workspacesessioncoordinator.h"));
    const QString workspaceSessionCoordinatorSource =
        readSource(
            root,
            QStringLiteral(
                "src/workspace/workspacesessioncoordinator.cpp"));
    const QString rtlActionCoordinatorHeader =
        readSource(
            root,
            QStringLiteral(
                "src/insights/rtlactioncoordinator.h"));
    const QString rtlActionCoordinatorSource =
        readSource(
            root,
            QStringLiteral(
                "src/insights/rtlactioncoordinator.cpp"));
    const QString editorSplitSource =
        readSource(
            root,
            QStringLiteral(
                "src/editor/editorsplitcontroller.cpp"));
    const QString tabManagerSource =
        readSource(
            root,
            QStringLiteral("src/documents/tabmanager.cpp"));
    const QString navigationConnectionsSource =
        readSource(
            root,
            QStringLiteral(
                "src/navigation/navigationmanagerconnections.cpp"));
    const QString navigationFileOperationsSource =
        readSource(
            root,
            QStringLiteral(
                "src/navigation/navigationmanagerfileoperations.cpp"));
    const QString panelLayoutSource =
        readSource(
            root,
            QStringLiteral(
                "src/ui/panellayoutcontroller.cpp"));
    const QString insightsHeader =
        readSource(
            root,
            QStringLiteral(
                "src/insights/rtlinsightspanelcoordinator.h"));
    const QString insightsSource =
        readSource(
            root,
            QStringLiteral(
                "src/insights/rtlinsightspanelcoordinator.cpp"));
    const QString insightsActionsSource =
        readSource(
            root,
            QStringLiteral(
                "src/insights/rtlinsightspanelactions.cpp"));
    const QString hotspotHeader =
        readSource(
            root,
            QStringLiteral(
                "src/insights/signalusagehotspotpanel.h"));
    const QString hotspotSource =
        readSource(
            root,
            QStringLiteral(
                "src/insights/signalusagehotspotpanel.cpp"));
    const QString hotspotActionsSource =
        readSource(
            root,
            QStringLiteral(
                "src/insights/signalusagehotspotpanelactions.cpp"));
    const QString focusHeader =
        readSource(
            root,
            QStringLiteral(
                "src/insights/insightfocuscontroller.h"));
    const QString focusSource =
        readSource(
            root,
            QStringLiteral(
                "src/insights/insightfocuscontroller.cpp"));
    const QString focusActionsSource =
        readSource(
            root,
            QStringLiteral(
                "src/insights/insightfocuscontrolleractions.cpp"));
    const QString rtlInsightsPresenterSource =
        readSource(
            root,
            QStringLiteral(
                "src/insights/rtlinsightspresenter.cpp"));
    const QString signalKernelGraphPanelSource =
        readSource(
            root,
            QStringLiteral(
                "src/insights/signalkernelgraphpanelcoordinator.cpp"));
    const QString cmake =
        readSource(root, QStringLiteral("CMakeLists.txt"));

    const QStringList editorModules = {
        QStringLiteral("src/editor/editortemplateslotcontroller"),
        QStringLiteral("src/editor/editorcolumnmodecontroller"),
        QStringLiteral("src/editor/editorsignalselectioncontroller"),
        QStringLiteral("src/editor/editorfolding"),
        QStringLiteral("src/editor/editorsourcenavigation"),
        QStringLiteral("src/editor/editorselection"),
        QStringLiteral("src/editor/editorlineoperationcontroller"),
    };
    for (const QString& module : editorModules) {
        check(!readSource(root, module + QStringLiteral(".h"))
                   .isEmpty()
                  && !readSource(
                          root,
                          module
                              + QStringLiteral(".cpp"))
                          .isEmpty(),
              module
                  + QStringLiteral(
                      " has a header and implementation"));
        check(cmake.contains(
                  module + QStringLiteral(".cpp")),
              module
                  + QStringLiteral(
                      " is an explicit build dependency"));
    }

    check(containsAll(
              runtimeHeader,
              {QStringLiteral(
                   "EditorTemplateSlotController"),
               QStringLiteral(
                   "EditorColumnModeController"),
               QStringLiteral(
                   "EditorSignalSelectionController"),
               QStringLiteral(
                   "EditorFoldingController"),
               QStringLiteral(
                   "EditorSourceNavigationUi")}),
          QStringLiteral(
              "editor runtime composes dedicated controllers"));
    check(containsNone(
              runtimeHeader,
              {QStringLiteral("struct TemplateSlotRange"),
               QStringLiteral("struct SelectedSignal"),
               QStringLiteral("templateSlotRanges"),
               QStringLiteral("columnAnchorLine"),
               QStringLiteral("virtualCursorLine ="),
               QStringLiteral("selectedSignals"),
               QStringLiteral(
                   "signalSelectionDragging")}),
          QStringLiteral(
              "editor runtime has no migrated mode state"));
    check(containsNone(
              runtimeSource,
              {QStringLiteral(
                   "templateSlotHighlightRanges("),
               QStringLiteral(
                   "bool hasColumnSelection("),
               QStringLiteral(
                   "paintColumnSelectionOverlay(")}),
          QStringLiteral(
              "editor runtime has no migrated mode helpers"));
    check(sourceLineCount(runtimeSource) <= 5000,
          QStringLiteral(
              "src/editor/editorruntime.cpp is at most 5000 lines"));
    check(containsNone(
              runtimeSource,
              {QStringLiteral(
                   "MyCodeEditorState::executeClipboardAction("),
               QStringLiteral(
                   "MyCodeEditorState::executeLineOperation("),
               QStringLiteral(
                   "MyCodeEditorState::selectSymbolOccurrences(")}),
          QStringLiteral(
              "editor runtime delegates reusable action execution"));
    check(containsAll(
              runtimeCommandsSource,
              {QStringLiteral(
                   "MyCodeEditorState::executeClipboardAction("),
               QStringLiteral(
                   "MyCodeEditorState::executeLineOperation("),
               QStringLiteral(
                   "MyCodeEditorState::selectSymbolOccurrences(")}),
          QStringLiteral(
              "editor command module owns reusable action execution"));
    check(containsNone(
              runtimeSource,
              {QStringLiteral(
                   "handleSmartSelectionExpansion("),
               QStringLiteral(
                   "handleSelectedSymbolOccurrenceNavigation(")}),
          QStringLiteral(
              "editor runtime has no physical-key selection helpers"));
    check(containsAll(
              selectionSource,
              {QStringLiteral(
                   "EditorSelection::expandSmartSelection("),
               QStringLiteral(
                   "EditorSelection::navigateSelectedSymbolOccurrence(")}),
          QStringLiteral(
              "selection controller owns structural selection navigation"));
    check(containsNone(
              runtimeSource,
              {QStringLiteral("handleMoveLineBlock("),
               QStringLiteral("LineBlockMoveRange")}),
          QStringLiteral(
              "editor runtime has no physical-key line-move implementation"));
    check(containsAll(
              lineOperationSource,
              {QStringLiteral(
                   "EditorLineOperationController::moveLines("),
               QStringLiteral("MoveLinesUp"),
               QStringLiteral("MoveLinesDown")}),
          QStringLiteral(
              "line-operation controller owns logical-line movement"));
    check(containsNone(
              commandModeSource,
              {QStringLiteral("Qt::Key_F24"),
               QStringLiteral("handleF24Event(")}),
          QStringLiteral(
              "Command Mode has no physical F24 ownership branch"));
    check(containsAll(
              commandModeSource,
              {QStringLiteral(
                   "effectiveActionShortcut("),
               QStringLiteral(
                   "ActionIds::ViewCommandMode"),
               QStringLiteral(
                   "ui.commandMode.show")}),
          QStringLiteral(
              "Command Mode resolves and executes its Registry entry Action"));
    check(containsNone(
              commandModeSource,
              {QStringLiteral("isColumnNumberShortcutKey"),
               QStringLiteral("event->key() != Qt::Key_C")}),
          QStringLiteral(
              "Column Number Tool has no physical Alt+C ownership branch"));
    check(containsAll(
              commandModeSource,
              {QStringLiteral(
                   "ActionIds::InsertColumnNumbers"),
               QStringLiteral(
                   "editor.columnNumbers.show"),
               QStringLiteral(
                   "matchesColumnNumberShortcut")}),
          QStringLiteral(
              "Column Number Tool resolves and executes its Registry Action"));

    check(containsNone(
              editorSplitSource,
              {QStringLiteral("EditorTabCommand"),
               QStringLiteral("tabCommandRequested"),
               QStringLiteral(
                   "menu.addAction(QStringLiteral(\"Close\"))"),
               QStringLiteral(
                   "menu.addAction(QStringLiteral(\"Split Left\"))"),
               QStringLiteral(
                   "menu.addAction(QStringLiteral(\"Lock Tab\"))")}),
          QStringLiteral(
              "Tab context menu has no hand-authored command enum or labels"));
    check(containsAll(
              editorSplitSource,
              {QStringLiteral(
                   "ActionSurface::TabContextMenu"),
               QStringLiteral("tabContextActions("),
               QStringLiteral("tabActionRequested("),
               QStringLiteral(
                   "\"actionId\", item.actionId"),
               QStringLiteral(
                   "\"executionRoute\"")}),
          QStringLiteral(
              "Tab context menu materializes Registry metadata and canonical ids"));
    check(containsAll(
              tabManagerSource,
              {QStringLiteral(
                   "setRegisteredTabActionRequestHandler("),
               QStringLiteral("requestTabAction("),
               QStringLiteral(
                   "executeRegisteredTabAction("),
               QStringLiteral(
                   "ActionIds::ViewEditorTabDuplicate"),
               QStringLiteral(
                   "ActionIds::ViewEditorTabToggleLocked")}),
          QStringLiteral(
              "TabManager dispatches Tab context requests through registered Actions"));
    check(containsAll(
              mainWindowSource,
              {QStringLiteral(
                   "setRegisteredTabActionRequestHandler("),
               QStringLiteral(
                   "ui.editorTabs.duplicateView"),
               QStringLiteral(
                   "executeRegisteredTabAction(")}),
          QStringLiteral(
              "MainWindow owns the registered Tab Action execution route"));

    check(containsNone(
              navigationConnectionsSource,
              {QStringLiteral("Go to Instantiation"),
               QStringLiteral("Go to Module Definition"),
               QStringLiteral("Set as Design Top")}),
          QStringLiteral(
              "Navigation hierarchy context menu has no hand-authored Action labels"));
    check(containsAll(
              navigationConnectionsSource,
              {QStringLiteral(
                   "designNodeContextActions("),
               QStringLiteral(
                   "requestDesignNodeAction("),
               QStringLiteral(
                   "ActionIds::NavigationDesignGoInstantiation"),
               QStringLiteral(
                   "ActionIds::NavigationDesignGoDefinition"),
               QStringLiteral(
                   "ActionIds::NavigationDesignSetTop"),
               QStringLiteral(
                   "\"actionId\", item.actionId")}),
          QStringLiteral(
              "Navigation hierarchy menu materializes and requests Registry Actions"));
    check(containsNone(
              navigationFileOperationsSource,
              {QStringLiteral("Set as Design Top")}),
          QStringLiteral(
              "Navigation file menu does not duplicate the Set Design Top label"));
    check(containsAll(
              navigationFileOperationsSource,
              {QStringLiteral(
                   "ActionIds::NavigationDesignSetTop"),
               QStringLiteral(
                   "navigation.design.goInstantiation"),
               QStringLiteral(
                   "navigation.design.goDefinition"),
               QStringLiteral(
                   "navigation.design.setTop"),
               QStringLiteral(
                   "navigateToDesignNodeFile(")}),
          QStringLiteral(
              "NavigationManager owns hierarchy Action route execution"));

    check(containsNone(
              panelLayoutSource,
              {QStringLiteral(
                   "QAction* pinAction = menu.addAction"),
               QStringLiteral(
                   "QAction* closeAction = menu.addAction"),
               QStringLiteral("selected == pinAction"),
               QStringLiteral("selected == closeAction")}),
          QStringLiteral(
              "bottom-page context menu has no private QAction branches"));
    check(containsAll(
              panelLayoutSource,
              {QStringLiteral(
                   "bottomToolDrawerButtonBar"),
               QStringLiteral(
                   "bottomPanelButton_%1"),
               QStringLiteral(
                   "QStackedWidget"),
               QStringLiteral(
                   "setBottomCollapsed(true)"),
               QStringLiteral(
                   "restoreEditorFocus()")})
              && containsNone(
                  panelLayoutSource,
                  {QStringLiteral(
                       "ActionSurface::PanelContextMenu"),
                   QStringLiteral(
                       "\"actionId\", item.actionId")}),
          QStringLiteral(
              "bottom drawer uses fixed buttons without a legacy page menu"));
    check(containsAll(
              mainWindowSource,
              {QStringLiteral(
                   "setRegisteredPanelActionRequestHandler("),
               QStringLiteral(
                   "ui.bottomPanel.pinned.toggle"),
               QStringLiteral(
                   "ui.bottomPanel.closeActive"),
               QStringLiteral(
                   "invocation.parameters")}),
          QStringLiteral(
              "MainWindow owns targeted bottom-page Action routes"));

    check(!workspaceSessionCoordinatorHeader.isEmpty()
              && !workspaceSessionCoordinatorSource.isEmpty()
              && cmake.contains(
                  QStringLiteral(
                      "src/workspace/workspacesessioncoordinator.cpp")),
          QStringLiteral(
              "workspace session coordinator is an explicit build boundary"));
    check(containsAll(
              workspaceSessionCoordinatorHeader,
              {QStringLiteral(
                   "class ZEROSLACK_API WorkspaceSessionCoordinator"),
               QStringLiteral("QTimer* saveTimer"),
               QStringLiteral("QSet<QString> cleanWorkspaceRoots"),
               QStringLiteral("openWorkspace("),
               QStringLiteral("switchWorkspace("),
               QStringLiteral("closeWorkspace(")})
              && containsAll(
                  workspaceSessionCoordinatorSource,
                  {QStringLiteral(
                       "&TabManager::workspaceSessionStateChanged"),
                   QStringLiteral(
                       "&WorkspaceManager::workspaceActivated"),
                   QStringLiteral(
                       "&WorkspaceManager::workspaceClosed"),
                   QStringLiteral(
                       "WorkspaceSessionCoordinator::flushScheduledSave")}),
          QStringLiteral(
              "workspace session coordinator owns lifecycle state timer and connections"));
    check(containsNone(
              mainWindowHeader,
              {QStringLiteral("workspaceSessionSaveTimer"),
               QStringLiteral("workspaceSessionCleanRoots"),
               QStringLiteral("captureWorkspaceSessionState("),
               QStringLiteral("saveWorkspaceSession("),
               QStringLiteral("restoreWorkspaceSession("),
               QStringLiteral("cleanWorkspaceSession("),
               QStringLiteral("scheduleWorkspaceSessionSave(")})
              && containsNone(
                  mainWindowSource,
                  {QStringLiteral(
                       "WorkspaceSessionStateService service"),
                   QStringLiteral(
                       "&TabManager::workspaceSessionStateChanged")}),
          QStringLiteral(
              "MainWindow has no parallel workspace session implementation"));

    check(!rtlActionCoordinatorHeader.isEmpty()
              && !rtlActionCoordinatorSource.isEmpty()
              && cmake.contains(
                  QStringLiteral(
                      "src/insights/rtlactioncoordinator.cpp")),
          QStringLiteral(
              "RTL action coordinator is an explicit build boundary"));
    check(containsAll(
              rtlActionCoordinatorHeader,
              {QStringLiteral(
                   "class ZEROSLACK_API RtlActionCoordinator"),
               QStringLiteral("TabManager* tabManager"),
               QStringLiteral(
                   "WorkspaceManager* workspaceManager"),
               QStringLiteral(
                   "SemanticDockCoordinator* semanticDocks"),
               QStringLiteral(
                   "PanelLayoutController* panelLayoutController"),
               QStringLiteral("QWidget* dialogParent"),
               QStringLiteral("resolveContext"),
               QStringLiteral("showPanel")})
              && !rtlActionCoordinatorHeader.contains(
                  QStringLiteral("MainWindow")),
          QStringLiteral(
              "RTL action coordinator uses explicit hosts and narrow callbacks"));
    check(containsAll(
              rtlActionCoordinatorSource,
              {QStringLiteral(
                   "RtlActionCoordinator::executeRtlRenameAction("),
               QStringLiteral(
                   "RtlActionCoordinator::executeRtlConnectionTransformAction("),
               QStringLiteral(
                   "RtlActionCoordinator::executeInstancePairConnectionAction("),
               QStringLiteral(
                   "RtlActionCoordinator::executeMultiSignalPropagationAction("),
               QStringLiteral(
                   "RtlHighRiskEditPanelCoordinator"),
               QStringLiteral(
                   "instancePairConnectionWorkflow()"),
               QStringLiteral(
                   "multiSignalPropagationWorkflow()"),
               QStringLiteral(
                   "rtlActionDocumentManager()"),
               QStringLiteral(
                   "selectInstancePair(")})
              && containsNone(
                  rtlActionCoordinatorSource,
                  {QStringLiteral("RtlRenamePlanner"),
                   QStringLiteral(
                       "RtlConnectionTransformPlanner"),
                   QStringLiteral(
                       "MultiSignalPropagationPlanner"),
                   QStringLiteral(
                       "WorkspaceEditTransactionService")}),
          QStringLiteral(
              "RTL action coordinator owns launch UI while reusing existing workflows"));
    const QStringList migratedRtlOrchestration = {
        QStringLiteral("executeRtlRenameAction("),
        QStringLiteral(
            "executeRtlConnectionTransformAction("),
        QStringLiteral(
            "executeInstancePairConnectionAction("),
        QStringLiteral(
            "executeMultiSignalPropagationAction("),
        QStringLiteral("captureRtlActionDocuments("),
        QStringLiteral("resolveRtlRenameSubject("),
        QStringLiteral("resolveRtlInstanceSubject("),
        QStringLiteral("selectInstancePair("),
        QStringLiteral("InstancePairUserSelection"),
    };
    check(containsNone(
              mainWindowHeader,
              migratedRtlOrchestration)
              && containsNone(
                  mainWindowSource,
                  migratedRtlOrchestration),
          QStringLiteral(
              "MainWindow has no parallel RTL action orchestration"));
    check(containsAll(
              mainWindowSource,
              {QStringLiteral(
                   "setupRtlActionCoordinator("),
               QStringLiteral(
                   "RtlActionCoordinator::handlesRoute(route)"),
               QStringLiteral(
                   "rtlActionCoordinator->execute(")}),
          QStringLiteral(
              "MainWindow only assembles and delegates owned RTL routes"));
    check(sourceLineCount(mainWindowSource) <= 7000,
          QStringLiteral(
              "src/app/mainwindow.cpp remains below the post-extraction ownership ceiling"));
    const QStringList insightModules = {
        QStringLiteral("src/insights/rtlinsightspanelviewstate"),
        QStringLiteral("src/insights/rtlinsightsgraphscenemapper"),
        QStringLiteral("src/insights/rtlinsightsgraphcontroller"),
        QStringLiteral("src/insights/rtlinsightspresenter"),
    };
    for (const QString& module : insightModules) {
        check(!readSource(root, module + QStringLiteral(".h"))
                   .isEmpty()
                  && !readSource(
                          root,
                          module
                              + QStringLiteral(".cpp"))
                          .isEmpty(),
              module
                  + QStringLiteral(
                      " has a header and implementation"));
        check(cmake.contains(
                  module + QStringLiteral(".cpp")),
              module
                  + QStringLiteral(
                      " is an explicit build dependency"));
    }

    check(containsAll(
              insightsHeader,
              {QStringLiteral(
                   "RtlInsightsPanelViewState"),
               QStringLiteral(
                   "RtlInsightsGraphController"),
               QStringLiteral(
                   "RtlInsightsPresenter"),
               QStringLiteral(
                   "public ActionExecutionHost"),
               QStringLiteral(
                   "executeActionRoute(")}),
          QStringLiteral(
              "Insight shell composes view state, graph controller, and presenter"));
    check(!insightsActionsSource.isEmpty()
              && cmake.contains(
                  QStringLiteral(
                      "src/insights/rtlinsightspanelactions.cpp"))
              && containsAll(
                  insightsActionsSource,
                  {QStringLiteral(
                       "ActionSurface::GraphPanel"),
                   QStringLiteral(
                       "requestGraphAction("),
                   QStringLiteral(
                       "insight.graph.jumpSelected"),
                   QStringLiteral(
                       "insight.graph.focusSelected")}),
          QStringLiteral(
              "Insight graph controls execute through dedicated Registry Action routes"));
    check(containsNone(
              insightsHeader,
              {QStringLiteral(
                   "QGraphicsScene* insightsGraphScene"),
               QStringLiteral(
                   "ModuleBlockDiagramReport currentModuleBlockReport"),
               QStringLiteral(
                   "QString currentGraphMode"),
               QStringLiteral(
                   "QTableWidget* graphTable")}),
          QStringLiteral(
              "Insight shell header has no presenter or graph state"));
    check(containsNone(
              insightsSource,
              {QStringLiteral(
                   "class RtlInsightGraphNodeItem"),
               QStringLiteral(
                   "class RtlInsightGraphEdgeItem"),
               QStringLiteral(
                   "renderFsmGraphLayoutScene("),
               QStringLiteral(
                   "renderModuleBlockDiagramScene("),
               QStringLiteral(
                   "graphMoreMenu->addAction(QStringLiteral(\"Jump\")"),
               QStringLiteral(
                   "graphMoreMenu->addAction(QStringLiteral(\"Focus\")"),
               QStringLiteral(
                   "graphMoreMenu->addAction(QStringLiteral(\"Set top\")")}),
          QStringLiteral(
              "Insight shell has no scene mapping or hand-authored graph Actions"));
    check(sourceLineCount(insightsSource) <= 900,
          QStringLiteral(
              "src/insights/rtlinsightspanelcoordinator.cpp is at most 900 lines"));
    check(containsAll(
              hotspotHeader,
              {QStringLiteral(
                   "public ActionExecutionHost"),
               QStringLiteral(
                   "requestGraphViewAction("),
               QStringLiteral(
                   "executeActionRoute(")})
              && !hotspotActionsSource.isEmpty()
              && cmake.contains(
                  QStringLiteral(
                      "src/insights/signalusagehotspotpanelactions.cpp")),
          QStringLiteral(
              "Usage Hotspot composes a dedicated graph-view Action host"));
    check(containsAll(
              hotspotSource,
              {QStringLiteral(
                   "bindGraphViewButton("),
               QStringLiteral(
                   "menu.addAction(fitViewAction)"),
               QStringLiteral(
                   "menu.addAction(centerCurrentViewAction)"),
               QStringLiteral(
                   "ActionIds::GraphViewFit"),
               QStringLiteral(
                   "ActionIds::GraphViewZoomIn"),
               QStringLiteral(
                   "ActionIds::GraphViewZoomOut")})
              && containsAll(
                  hotspotActionsSource,
                  {QStringLiteral(
                       "insight.graphView.fit"),
                   QStringLiteral(
                       "insight.graphView.centerCurrent"),
                   QStringLiteral(
                       "insight.graphView.resetLayout"),
                   QStringLiteral(
                       "executeAction(*descriptor, *this)")}),
          QStringLiteral(
              "Usage Hotspot toolbar, context menu, and Focus adapters share Registry routes"));
    check(containsNone(
              hotspotSource,
              {QStringLiteral(
                   "menu.addAction(QStringLiteral(\"Fit\"))"),
               QStringLiteral(
                   "menu.addAction(QStringLiteral(\"Center Current\"))"),
               QStringLiteral(
                   "menu.addAction(QStringLiteral(\"Zoom In\"))"),
               QStringLiteral(
                   "menu.addAction(QStringLiteral(\"Zoom Out\"))"),
               QStringLiteral(
                   "menu.addAction(QStringLiteral(\"Reset Layout\"))")}),
          QStringLiteral(
              "Usage Hotspot has no hand-authored graph context actions"));
    // Insight Focus was retired: its descriptors are asserted absent by
    // action_registry_test, and no production caller ever registered a panel
    // with it. These checks replace the three that described the retired
    // implementation, and fail if any part of it returns.
    check(focusHeader.isEmpty()
              && focusSource.isEmpty()
              && focusActionsSource.isEmpty()
              && !cmake.contains(
                  QStringLiteral("insightfocuscontroller"))
              && containsNone(
                  mainWindowHeader,
                  {QStringLiteral("InsightFocusController")})
              && containsNone(
                  mainWindowSource,
                  {QStringLiteral("InsightFocusController"),
                   QStringLiteral("insightFocusController")}),
          QStringLiteral(
              "Retired Insight Focus implementation is absent from sources and CMake"));
    check(containsNone(
              rtlInsightsPresenterSource,
              {QStringLiteral("insightFocusActive")})
              && containsNone(
                  signalKernelGraphPanelSource,
                  {QStringLiteral("insightFocusActive")}),
          QStringLiteral(
              "Insight panels no longer branch on the retired focus property"));
    // Every declared resource must have a reader. The keyword list was
    // superseded by the Slang keyword table and the three toolbar bitmaps by
    // the scalable icon set, so neither may return to a .qrc unread.
    const QString codeResources =
        readSource(root, QStringLiteral("code.qrc"));
    const QString imageResources =
        readSource(root, QStringLiteral("images.qrc"));
    check(!codeResources.isEmpty()
              && !imageResources.isEmpty()
              && containsNone(
                  codeResources,
                  {QStringLiteral("config/keywords.txt"),
                   QStringLiteral("prefix=\"/config\"")})
              && containsNone(
                  imageResources,
                  {QStringLiteral("images/workspace.png"),
                   QStringLiteral("images/open_file.png"),
                   QStringLiteral("images/new_file.png")}),
          QStringLiteral(
              "Superseded keyword and toolbar bitmap resources stay out of the .qrc files"));

    std::cout << (checks - failures)
              << "/" << checks
              << " controller boundary checks passed\n";
    return failures == 0 ? 0 : 1;
}
