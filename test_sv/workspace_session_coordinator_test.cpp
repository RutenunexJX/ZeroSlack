#include "mycodeeditor.h"
#include "tabmanager.h"
#include "workspacemanager.h"
#include "workspacesessioncoordinator.h"
#include "workspacesessionstateservice.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>

#include <iostream>
#include <utility>

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

QString cleanPath(const QString& path)
{
    return QDir::cleanPath(
        QDir::fromNativeSeparators(
            QFileInfo(path).absoluteFilePath()));
}

bool writeFile(const QString& path, const QByteArray& content)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(content) == content.size();
}

void setCursor(MyCodeEditor* editor, int line, int column)
{
    if (!editor)
        return;
    QTextBlock block =
        editor->document()->findBlockByNumber(qMax(0, line - 1));
    if (!block.isValid())
        return;
    QTextCursor cursor(block);
    cursor.setPosition(
        qMin(block.position() + qMax(0, column - 1),
             block.position() + block.length() - 1));
    editor->setTextCursor(cursor);
}

QString savedGeometryMarker(
    const WorkspaceSessionStateService& service,
    const QString& workspaceRoot)
{
    const WorkspaceSessionRestoreResult result =
        service.load(workspaceRoot);
    return result.loaded
        ? QString::fromUtf8(result.state.ui.mainWindowGeometry)
        : QString();
}
}

int main(int argc, char* argv[])
{
    QStandardPaths::setTestModeEnabled(true);
    QApplication app(argc, argv);

    QTemporaryDir temp;
    check(temp.isValid(), "temporary root is valid");
    if (!temp.isValid())
        return 1;

    const QString workspaceA =
        QDir(temp.path()).absoluteFilePath(
            QStringLiteral("workspace-a"));
    const QString workspaceB =
        QDir(temp.path()).absoluteFilePath(
            QStringLiteral("workspace-b"));
    const QString fileA =
        QDir(workspaceA).absoluteFilePath(
            QStringLiteral("rtl/a.sv"));
    const QString fileB =
        QDir(workspaceB).absoluteFilePath(
            QStringLiteral("rtl/b.sv"));
    const QString sessionStore =
        QDir(temp.path()).absoluteFilePath(
            QStringLiteral("state/workspace-sessions.ini"));
    check(writeFile(
              fileA,
              "module a;\n  logic value_a;\nendmodule\n")
              && writeFile(
                  fileB,
                  "module b;\n  logic value_b;\nendmodule\n"),
          "workspace fixtures are created");

    QTabWidget tabWidget;
    TabManager tabManager(&tabWidget);
    WorkspaceManager workspaceManager;
    workspaceManager
        .setRecentWorkspacePersistenceEnabledForTesting(false);

    WorkspaceSessionUiState liveUi;
    WorkspaceSessionUiState restoredUi;
    int restoreUiCalls = 0;
    QString lastStatus;
    WorkspaceSessionUiBridge bridge;
    bridge.captureUiState =
        [&liveUi](bool rememberPanelState) {
            WorkspaceSessionUiState state = liveUi;
            if (!rememberPanelState) {
                state.mainWindowGeometry.clear();
                state.mainWindowState.clear();
                state.panelLayout = {};
            }
            return state;
        };
    bridge.restoreUiState =
        [&restoredUi, &restoreUiCalls](
            const WorkspaceSessionUiState& state,
            bool) {
            restoredUi = state;
            ++restoreUiCalls;
            return WorkspaceSessionUiRestoreResult{};
        };
    bridge.showStatus =
        [&lastStatus](const QString& message, int) {
            lastStatus = message;
        };

    WorkspaceSessionCoordinator coordinator(
        &workspaceManager,
        &tabManager,
        nullptr,
        std::move(bridge),
        sessionStore,
        35);
    coordinator.setRestoreOnActivation(false);
    QSignalSpy saveSpy(
        &coordinator,
        &WorkspaceSessionCoordinator::sessionSaveFinished);
    QSignalSpy restoreSpy(
        &coordinator,
        &WorkspaceSessionCoordinator::sessionRestoreFinished);

    check(workspaceManager.openWorkspace(workspaceA),
          "workspace A opens");
    tabManager.setWorkspaceScope({workspaceA}, workspaceA);
    check(workspaceManager.restoreSessionScanState({fileA}, true),
          "workspace A scan state is available");
    check(tabManager.openFileInTab(fileA),
          "workspace A file opens");
    setCursor(tabManager.getCurrentEditor(), 2, 4);
    tabManager.setTabGroupingMode(TabGroupingMode::Module);
    liveUi.mainWindowGeometry = QByteArrayLiteral("geometry-a");
    liveUi.mainWindowState = QByteArrayLiteral("state-a");
    liveUi.navigationFilesQuery = QStringLiteral("value_a");
    liveUi.navigationDesignQuery = QStringLiteral("u_a");
    liveUi.panelLayout.valid = true;
    liveUi.panelLayout.activeBottomPanel =
        QStringLiteral("problems");

    check(coordinator.saveBeforeWorkspaceTransition(),
          "coordinator saves workspace A");
    WorkspaceSessionStateService service(sessionStore);
    const WorkspaceSessionRestoreResult savedA =
        service.load(workspaceA);
    check(savedA.loaded
              && savedA.state.tabs.size() == 1
              && savedA.state.tabs.first().filePath
                     == cleanPath(fileA)
              && savedA.state.tabs.first().cursorLine == 2
              && savedA.state.tabs.first().cursorColumn == 4
              && savedA.state.ui.mainWindowGeometry
                     == QByteArrayLiteral("geometry-a")
              && savedA.state.ui.navigationFilesQuery
                     == QStringLiteral("value_a")
              && savedA.state.ui.panelLayout.valid
              && savedA.state.scannedFiles
                     == QStringList{cleanPath(fileA)}
              && savedA.state.scanComplete,
          "save captures tabs UI and scan state");

    check(tabManager.closeTabsInWorkspace(workspaceA),
          "workspace A tabs close before restore");
    restoredUi = {};
    liveUi.mainWindowGeometry = QByteArrayLiteral("changed");
    check(coordinator.restoreSession(),
          "coordinator restores workspace A");
    MyCodeEditor* restoredEditor = tabManager.getCurrentEditor();
    check(restoreSpy.size() == 1
              && restoreUiCalls == 1
              && restoredUi.mainWindowGeometry
                     == QByteArrayLiteral("geometry-a")
              && restoredUi.navigationDesignQuery
                     == QStringLiteral("u_a")
              && tabManager.editorCount() == 1
              && restoredEditor
              && restoredEditor->textCursor().blockNumber() == 1
              && restoredEditor->textCursor().positionInBlock() == 3
              && tabManager.tabGroupingMode()
                     == TabGroupingMode::Module,
          "restore reapplies UI tab cursor and grouping behavior");

    QApplication::processEvents();
    saveSpy.clear();
    for (int iteration = 0; iteration < 20; ++iteration) {
        tabManager.setTabGroupingMode(
            iteration % 2 == 0
                ? TabGroupingMode::None
                : TabGroupingMode::Module);
    }
    tabManager.setTabGroupingMode(TabGroupingMode::Workspace);
    QTest::qWait(120);
    check(saveSpy.size() == 1,
          "burst state changes collapse into one timer save");

    liveUi.mainWindowGeometry =
        QByteArrayLiteral("geometry-a-before-open-b");
    tabManager.setTabGroupingMode(TabGroupingMode::Module);
    check(coordinator.openWorkspace(workspaceB),
          "coordinator saves A and opens workspace B");
    tabManager.setWorkspaceScope(
        {workspaceA, workspaceB}, workspaceB);
    check(workspaceManager.restoreSessionScanState({fileB}, true),
          "workspace B scan state is available");
    check(tabManager.openFileInTab(fileB),
          "workspace B file opens");
    setCursor(tabManager.getCurrentEditor(), 3, 2);
    liveUi.mainWindowGeometry = QByteArrayLiteral("geometry-b");
    liveUi.navigationFilesQuery = QStringLiteral("value_b");
    check(coordinator.saveBeforeWorkspaceTransition(),
          "coordinator saves workspace B");
    check(savedGeometryMarker(service, workspaceA)
              == QStringLiteral("geometry-a-before-open-b")
              && savedGeometryMarker(service, workspaceB)
                     == QStringLiteral("geometry-b"),
          "workspace session partitions remain isolated");

    saveSpy.clear();
    liveUi.mainWindowGeometry =
        QByteArrayLiteral("geometry-b-before-switch");
    tabManager.setTabGroupingMode(TabGroupingMode::Workspace);
    check(coordinator.switchWorkspace(0),
          "coordinator flushes B and switches to A");
    tabManager.setWorkspaceScope(
        {workspaceA, workspaceB}, workspaceA);
    QTest::qWait(120);
    check(saveSpy.size() == 1,
          "switch produces one pre-transition save");
    check(!saveSpy.isEmpty()
              && cleanPath(saveSpy.first().at(0).toString())
                     == cleanPath(workspaceB),
          "switch save belongs to the old workspace root");
    check(savedGeometryMarker(service, workspaceB)
              == QStringLiteral("geometry-b-before-switch"),
          "switch flushes the latest old-workspace UI state");
    check(savedGeometryMarker(service, workspaceA)
              == QStringLiteral("geometry-a-before-open-b"),
          "switch does not overwrite the destination partition");

    check(coordinator.switchWorkspace(1),
          "coordinator switches to B for close ordering");
    tabManager.setWorkspaceScope(
        {workspaceA, workspaceB}, workspaceB);
    liveUi.mainWindowGeometry =
        QByteArrayLiteral("geometry-b-before-close");
    saveSpy.clear();
    check(coordinator.closeWorkspace(1),
          "coordinator saves and closes B before activating A");
    tabManager.setWorkspaceScope({workspaceA}, workspaceA);
    QTest::qWait(120);
    const WorkspaceSessionRestoreResult closedB =
        service.load(workspaceB);
    check(saveSpy.size() == 1,
          "close produces one pre-close save");
    check(closedB.loaded,
          "closed workspace session remains loadable");
    check(closedB.state.tabs.size() == 1,
          "close preserves the pre-close tab snapshot");
    check(closedB.state.ui.mainWindowGeometry
              == QByteArrayLiteral("geometry-b-before-close"),
          "close preserves the pre-close UI state");

    coordinator.clearSession();
    check(!service.sessionExists(workspaceA),
          "clean removes only the active workspace session");
    saveSpy.clear();
    tabManager.setTabGroupingMode(TabGroupingMode::None);
    tabManager.setTabGroupingMode(TabGroupingMode::Module);
    QTest::qWait(120);
    check(saveSpy.isEmpty()
              && !service.sessionExists(workspaceA),
          "clean suppression blocks automatic timer recreation");
    liveUi.mainWindowGeometry =
        QByteArrayLiteral("geometry-a-explicit-save");
    check(coordinator.saveSession(true)
              && service.sessionExists(workspaceA)
              && savedGeometryMarker(service, workspaceA)
                     == QStringLiteral(
                         "geometry-a-explicit-save"),
          "explicit save re-enables a cleaned workspace session");

    check(!lastStatus.isEmpty(),
          "coordinator reports user-visible lifecycle status");
    std::cout << (checks - failures) << "/" << checks
              << " workspace session coordinator checks passed\n";
    return failures == 0 ? 0 : 1;
}
