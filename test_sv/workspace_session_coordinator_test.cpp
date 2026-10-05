#include "mycodeeditor.h"
#include "tabmanager.h"
#include "workspacemanager.h"
#include "workspacesessioncoordinator.h"
#include "workspacesessionstateservice.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QSaveFile>
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

void checkRestoredWorkspaceWatches()
{
    QTemporaryDir root;
    const QString a = root.filePath(QStringLiteral("a"));
    const QString b = root.filePath(QStringLiteral("b"));
    const QString aFile = a + QStringLiteral("/rtl/a.sv");
    const QString bFile = b + QStringLiteral("/rtl/b.sv");
    check(root.isValid() && writeFile(aFile, "module a; endmodule\n")
              && writeFile(bFile, "module b; endmodule\n"),
          "watch restoration fixtures are writable");
    WorkspaceManager manager;
    manager.setRecentWorkspacePersistenceEnabledForTesting(false);
    auto waitFor = [](const std::function<bool()>& ready) {
        QElapsedTimer timer;
        timer.start();
        while (!ready() && timer.elapsed() < 5000)
            QTest::qWait(10);
        return ready();
    };
    auto scanFinished = [&] {
        const auto entries = manager.workspaceEntries();
        const int active = manager.activeWorkspaceIndex();
        return active >= 0 && entries.at(active).scanComplete;
    };
    check(manager.openWorkspace(a) && waitFor(scanFinished)
              && manager.openWorkspace(b) && waitFor(scanFinished)
              && manager.switchWorkspace(0)
              && waitFor([&] { return !manager.isWorkspaceScanActive(); }),
          "loaded workspace watch restoration succeeds");
    auto* watcher = manager.findChild<QFileSystemWatcher*>();
    check(watcher && watcher->files().contains(cleanPath(aFile))
              && !watcher->files().contains(cleanPath(bFile)),
          "restored watches belong to the active workspace before scan readiness");
    QSignalSpy changed(&manager, &WorkspaceManager::fileChanged);
    check(writeFile(aFile, "module a; logic changed; endmodule\n")
              && waitFor([&] { return !changed.isEmpty(); }),
          "restored file watch still reports external edits");
    const QString added = a + QStringLiteral("/rtl/added.sv");
    check(writeFile(added, "module added; endmodule\n")
              && waitFor([&] {
                     return manager.getSystemVerilogFiles().contains(cleanPath(added));
                 }),
          "restored directory watch still discovers new source files");
    check(manager.switchWorkspace(1) && manager.switchWorkspace(0)
              && manager.closeWorkspace(0)
              && waitFor([&] { return !manager.isWorkspaceScanActive(); }),
          "rapid switch and close keep the remaining workspace active");
    check(watcher && watcher->files().contains(cleanPath(bFile))
              && !watcher->files().contains(cleanPath(aFile))
              && !watcher->files().contains(cleanPath(added)),
          "quick close cannot leave watches on the retired workspace");
}

void checkWatchNotificationOwnership()
{
    QTemporaryDir root;
    const QString a = root.filePath("a"), b = root.filePath("b");
    const QString aFile = a + "/a.sv", bFile = b + "/b.sv";
    const QString includeDir = root.filePath("external");
    const QString include = includeDir + "/config.svh";
    check(root.isValid() && writeFile(aFile, "module a; endmodule\n")
              && writeFile(bFile, "module b; endmodule\n")
              && writeFile(include, "`define CONFIG 1\n"),
          "watch ownership fixtures are writable");
    WorkspaceManager manager;
    manager.setRecentWorkspacePersistenceEnabledForTesting(false);
    auto waitFor = [](const std::function<bool()>& ready) {
        QElapsedTimer timer;
        timer.start();
        while (!ready() && timer.elapsed() < 5000) QTest::qWait(5);
        return ready();
    };
    check(manager.openWorkspace(a) && waitFor([&] { return !manager.isWorkspaceScanActive(); }),
          "watch ownership A is ready");
    auto* watcher = manager.findChild<QFileSystemWatcher*>();
    if (!watcher) { check(false, "native watcher exists"); return; }
    auto watched = [&](const QString& file) {
        return watcher->files().contains(cleanPath(file), Qt::CaseInsensitive);
    };
    QSignalSpy changed(&manager, &WorkspaceManager::fileChanged);
    QSignalSpy semanticChanged(&manager, &WorkspaceManager::semanticInputsChanged);
    auto deliverFile = [&](const QString& file, Qt::ConnectionType connection = Qt::DirectConnection) {
        return QMetaObject::invokeMethod(&manager, "onFileChanged", connection, Q_ARG(QString, file));
    };
    check(manager.openWorkspace(b) && deliverFile(aFile) && changed.isEmpty(),
          "obsolete A notification is rejected before native watch removal finishes");
    check(waitFor([&] { return !manager.isWorkspaceScanActive() && watched(bFile) && !watched(aFile); }),
          "watch ownership transfers to B");
    check(deliverFile(aFile, Qt::QueuedConnection), "late A callback is queued");
    QTest::qWait(30);
    check(changed.isEmpty() && !watched(aFile),
          "late A callback neither restores its watch nor emits a file change");

    QString alias = b + "/./b.sv";
#ifdef Q_OS_WIN
    alias = QDir::toNativeSeparators(alias.toUpper());
#endif
    check(deliverFile(alias) && changed.size() == 1 && changed.first().first().toString() == bFile,
          "current Windows case and separator aliases retain the owned path");
    QTest::qWait(20);
    check(watched(bFile), "alias callback preserves current native coverage");

    manager.applySemanticWatchPaths(b, {include}, {includeDir});
    check(waitFor([&] { return watched(include) && !semanticChanged.isEmpty(); }),
          "external semantic input watch is installed and revalidated");
    semanticChanged.clear();
    changed.clear();
    check(writeFile(include, "`define CONFIG 2\n")
              && waitFor([&] { return !semanticChanged.isEmpty(); }) && changed.isEmpty(),
          "real external include edits still invalidate semantics");
    semanticChanged.clear();
    check(writeFile(includeDir + "/previously_missing.svh", "`define FOUND 1\n")
              && waitFor([&] { return !semanticChanged.isEmpty(); }),
          "external include directory creation retains negative-lookup coverage");

    auto replace = [](const QString& file, const QByteArray& text) {
        // Windows can briefly deny replacement while a file observer is
        // querying metadata. Retry only fixture creation's rename/permission
        // errors; notification and rewatch assertions still run separately.
        for (int attempt = 0; attempt < 5; ++attempt) {
            QSaveFile saved(file);
            if (saved.open(QIODevice::WriteOnly) && saved.write(text) == text.size() && saved.commit())
                return true;
            std::cerr << "Atomic fixture save attempt " << (attempt + 1) << " failed: "
                      << qPrintable(file) << ": " << qPrintable(saved.errorString())
                      << " (error " << saved.error() << ")\n";
#ifdef Q_OS_WIN
            if (saved.error() == QFileDevice::RenameError || saved.error() == QFileDevice::PermissionsError) {
                QTest::qWait(20);
                continue;
            }
#endif
            return false;
        }
        return false;
    };
    changed.clear();
    check(replace(bFile, "module b; logic replaced; endmodule\n")
              && waitFor([&] { return !changed.isEmpty() && watched(bFile); }),
          "atomic replacement restores the current workspace file watch");
    changed.clear();
    check(writeFile(bFile, "module b; logic edited_again; endmodule\n")
              && waitFor([&] { return !changed.isEmpty(); }),
          "restored workspace watch reports the next real edit");
    semanticChanged.clear();
    check(replace(include, "`define CONFIG 3\n")
              && waitFor([&] { return !semanticChanged.isEmpty() && watched(include); }),
          "atomic replacement restores an external include watch");
    semanticChanged.clear();
    check(writeFile(include, "`define CONFIG 4\n")
              && waitFor([&] { return !semanticChanged.isEmpty(); }),
          "restored include watch reports the next real edit");

    manager.applySemanticWatchPaths(b, {}, {});
    semanticChanged.clear();
    check(deliverFile(include) && semanticChanged.isEmpty(),
          "retired external include loses notification ownership immediately");
    check(waitFor([&] { return !watched(include); }), "retired include native watch is removed");
    check(manager.closeWorkspace(0), "inactive A closes before final workspace closure");
    manager.closeWorkspace();
    changed.clear();
    semanticChanged.clear();
    check(deliverFile(bFile) && deliverFile(include), "callbacks after close are delivered to the guard");
    check(QMetaObject::invokeMethod(&manager, "onDirectoryChanged", Qt::QueuedConnection,
                                   Q_ARG(QString, b)), "late closed directory callback is queued");
    QTest::qWait(30);
    check(!manager.isWorkspaceOpen() && changed.isEmpty() && semanticChanged.isEmpty()
              && watcher->files().isEmpty() && watcher->directories().isEmpty()
              && !manager.isWorkspaceScanActive(),
          "close rejects late file and directory events without restoring retired coverage");
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

void checkAutomaticSaveFailureAndRecovery()
{
    QTemporaryDir temp;
    const QString workspace = temp.filePath("workspace");
    const QString source = workspace + "/top.sv";
    const QString blocker = temp.filePath("blocked");
    const QString store = blocker + "/session.ini";
    check(writeFile(source, "module top; endmodule\n") && writeFile(blocker, "block"),
        "session failure fixture prepared");
    WorkspaceManager manager;
    manager.setRecentWorkspacePersistenceEnabledForTesting(false);
    QTabWidget widget;
    TabManager tabs(&widget);
    QStringList messages;
    WorkspaceSessionUiBridge bridge;
    bridge.showStatus = [&](const QString& message, int) { messages.append(message); };
    WorkspaceSessionCoordinator coordinator(&manager, &tabs, nullptr, bridge, store, 25);
    coordinator.setRestoreOnActivation(false);
    check(manager.openWorkspace(workspace), "failure fixture workspace opens");
    QTest::qWait(30);
    messages.clear();
    QSignalSpy saves(&coordinator, &WorkspaceSessionCoordinator::sessionSaveFinished);
    coordinator.scheduleSessionSave();
    check(QTest::qWaitFor([&] { return !saves.isEmpty(); }, 1000), "automatic save attempted");
    check(!saves.isEmpty() && !saves.first().at(1).toBool(), "storage failure is reproducible");
    check(coordinator.lastSaveOutcome().status == WorkspaceSessionSaveStatus::Failed
        && coordinator.pendingSaveCount() == 1, "failed outcome retains one pending snapshot");
    check(!messages.isEmpty(), "automatic save failure is visible without a modal dialog");
    check(QFile::remove(blocker), "storage failure removed");
    check(QTest::qWaitFor([&] { return QFileInfo(store).isFile(); }, 1500),
        "pending automatic save retries after storage recovery");
    WorkspaceSessionStateService service(store);
    check(service.load(workspace).loaded, "retried session is loadable on next open");
    coordinator.clearSession();
    coordinator.scheduleSessionSave();
    QTest::qWait(150);
    check(!service.sessionExists(workspace), "clear cancels pending save recreation");
    check(coordinator.saveSessionResult(false).status == WorkspaceSessionSaveStatus::Skipped,
        "explicitly cleared auto-save has a skipped outcome");
}

void checkFailedSaveTransitionsAndBoundedRetries()
{
    QTemporaryDir temp;
    const auto a = temp.filePath("a");
    const auto b = temp.filePath("b");
    const auto blocker = temp.filePath("blocked");
    const auto store = blocker + "/state.ini";
    check(writeFile(a + "/a.sv", "module a; endmodule\n")
        && writeFile(b + "/b.sv", "module b; endmodule\n") && writeFile(blocker, "block"),
        "transition failure fixture prepared");
    WorkspaceManager manager;
    manager.setRecentWorkspacePersistenceEnabledForTesting(false);
    QTabWidget widget; TabManager tabs(&widget);
    QByteArray geometry("state-a");
    WorkspaceSessionUiBridge bridge;
    bridge.captureUiState = [&](bool) { WorkspaceSessionUiState state; state.mainWindowGeometry = geometry; return state; };
    WorkspaceSessionCoordinator coordinator(&manager, &tabs, nullptr, bridge, store, 25);
    coordinator.setRestoreOnActivation(false);
    check(manager.openWorkspace(a), "pending A opens");
    QTest::qWait(50);
    check(coordinator.openWorkspace(b), "failed session save keeps transition non-modal");
    geometry = "state-b";
    coordinator.saveSession(false);
    QSignalSpy saves(&coordinator, &WorkspaceSessionCoordinator::sessionSaveFinished);
    QTest::qWait(400);
    const int boundedCount = saves.size();
    QTest::qWait(400);
    check(saves.size() == boundedCount && coordinator.pendingSaveCount() == 2,
        "failed workspaces retain one snapshot each and stop retrying");
    coordinator.clearSession();
    check(coordinator.pendingSaveCount() == 1, "clearing B cancels only B's pending snapshot");
    check(QFile::remove(blocker), "transition storage recovered");
    coordinator.flushPendingSaves();
    WorkspaceSessionStateService service(store);
    check(savedGeometryMarker(service, a) == QStringLiteral("state-a")
        && !service.sessionExists(b) && coordinator.pendingSaveCount() == 0,
        "final flush preserves captured A and never recreates cleared B");
    check(coordinator.saveBeforeWorkspaceTransition(), "cleaned skip is a successful transition disposition");
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
    checkRestoredWorkspaceWatches();
    checkAutomaticSaveFailureAndRecovery();
    checkFailedSaveTransitionsAndBoundedRetries();
    checkWatchNotificationOwnership();
    std::cout << (checks - failures) << "/" << checks
              << " workspace session coordinator checks passed\n";
    return failures == 0 ? 0 : 1;
}
