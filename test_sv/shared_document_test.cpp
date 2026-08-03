#include "actionregistry.h"
#include "mycodeeditor.h"
#include "filecommandcoordinator.h"
#include "shareddocument.h"
#include "tabmanager.h"

#include <QApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStringList>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QTextStream>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdio>
#include <memory>

namespace {
int checks = 0;
int failures = 0;

void expect(const char* name, bool value)
{
    ++checks;
    if (!value)
        ++failures;
    std::printf("[%s] %s\n", value ? "PASS" : "FAIL", name);
}

bool writeFixture(const QString& fileName, const QString& text)
{
    if (!QDir().mkpath(QFileInfo(fileName).absolutePath()))
        return false;
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly
                   | QIODevice::Text
                   | QIODevice::Truncate)) {
        return false;
    }
    QTextStream(&file) << text;
    file.close();
    return true;
}

QString canonicalFixturePath(const QString& fileName)
{
    const QString canonical = QFileInfo(fileName).canonicalFilePath();
    return QDir::cleanPath(
        canonical.isEmpty()
            ? QFileInfo(fileName).absoluteFilePath()
            : canonical);
}

MyCodeEditor* editorForFile(TabManager& manager,
                            const QString& fileName)
{
    const QString expected = canonicalFixturePath(fileName);
    for (MyCodeEditor* editor : manager.openEditors()) {
        if (editor
            && canonicalFixturePath(editor->documentFileName())
                   == expected) {
            return editor;
        }
    }
    return nullptr;
}

QString tabTitleForEditor(TabManager& manager,
                          MyCodeEditor* editor)
{
    QTabWidget* group = manager.editorSplitController()
        ? manager.editorSplitController()->groupForPage(editor)
        : nullptr;
    const int index = group ? group->indexOf(editor) : -1;
    return group && index >= 0
        ? group->tabText(index)
        : QString();
}

QString fileAtTab(QTabWidget* group, int index)
{
    MyCodeEditor* editor = group
        ? qobject_cast<MyCodeEditor*>(group->widget(index))
        : nullptr;
    return editor
        ? canonicalFixturePath(editor->documentFileName())
        : QString();
}

void exerciseTabIdentityGroupingAndBatchClose()
{
    QTemporaryDir temp;
    const QString aRoot =
        temp.filePath(QStringLiteral("a_workspace"));
    const QString zRoot =
        temp.filePath(QStringLiteral("z_workspace"));
    const QString aFile =
        QDir(aRoot).filePath(QStringLiteral("a.sv"));
    const QString zFile =
        QDir(zRoot).filePath(QStringLiteral("z.sv"));
    const QString alphaTop = temp.filePath(
        QStringLiteral("alpha/common/top.sv"));
    const QString betaTop = temp.filePath(
        QStringLiteral("beta/common/top.sv"));
    const QString tailFile =
        temp.filePath(QStringLiteral("tail.sv"));
    const bool fixturesReady = temp.isValid()
        && writeFixture(
            aFile,
            QStringLiteral("module a_mod; endmodule\n"))
        && writeFixture(
            zFile,
            QStringLiteral("module z_mod; endmodule\n"))
        && writeFixture(
            alphaTop,
            QStringLiteral("module alpha_top; endmodule\n"))
        && writeFixture(
            betaTop,
            QStringLiteral("module beta_top; endmodule\n"))
        && writeFixture(
            tailFile,
            QStringLiteral("module tail_mod; endmodule\n"));
    expect("tab identity fixtures are writable", fixturesReady);
    if (!fixturesReady)
        return;

    QWidget host;
    auto* layout = new QVBoxLayout(&host);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* tabs = new QTabWidget(&host);
    layout->addWidget(tabs);
    TabManager manager(tabs);
    manager.setCrashRecoveryService(
        std::make_unique<CrashRecoveryService>(
            temp.filePath(QStringLiteral("recovery"))));
    manager.setWorkspaceScope({aRoot, zRoot}, aRoot);
    host.resize(720, 420);
    host.show();

    expect("workspace grouping fixtures open",
           manager.openFileInTab(zFile)
               && manager.openFileInTab(aFile)
               && manager.editorCount() == 2);
    QCoreApplication::processEvents();
    MyCodeEditor* aEditor = editorForFile(manager, aFile);
    MyCodeEditor* zEditor = editorForFile(manager, zFile);
    manager.setTabGroupingMode(TabGroupingMode::Module);
    QCoreApplication::processEvents();
    QTabWidget* group = aEditor
        ? manager.editorSplitController()->groupForPage(aEditor)
        : nullptr;
    expect("module grouping prefixes and sorts tabs",
           group
               && fileAtTab(group, 0)
                      == canonicalFixturePath(aFile)
               && tabTitleForEditor(manager, aEditor)
                      .startsWith(QStringLiteral("a_mod"))
               && tabTitleForEditor(manager, zEditor)
                      .startsWith(QStringLiteral("z_mod")));

    manager.setTabGroupingMode(TabGroupingMode::Workspace);
    QCoreApplication::processEvents();
    expect("workspace grouping sorts by workspace name",
           group
               && fileAtTab(group, 0)
                      == canonicalFixturePath(aFile));
    manager.setWorkspaceScope({aRoot}, aRoot);
    QCoreApplication::processEvents();
    expect("workspace scope changes regroup external tabs immediately",
           group
               && fileAtTab(group, 0)
                      == canonicalFixturePath(zFile)
               && tabTitleForEditor(manager, zEditor)
                      .startsWith(QStringLiteral("(external)")));

    manager.setTabGroupingMode(TabGroupingMode::None);
    expect("first same-name tab initially uses only its basename",
           manager.openFileInTab(alphaTop)
               && tabTitleForEditor(
                      manager,
                      editorForFile(manager, alphaTop))
                      == QStringLiteral("top.sv"));
    expect("opening a conflict expands both titles to shortest unique suffixes",
           manager.openFileInTab(betaTop)
               && tabTitleForEditor(
                      manager,
                      editorForFile(manager, alphaTop))
                      == QStringLiteral("alpha/common/top.sv")
               && tabTitleForEditor(
                      manager,
                      editorForFile(manager, betaTop))
                      == QStringLiteral("beta/common/top.sv"));

    expect("batch-close tail fixture opens",
           manager.openFileInTab(tailFile)
               && manager.editorCount() == 5);
    MyCodeEditor* alphaEditor = editorForFile(manager, alphaTop);
    MyCodeEditor* betaEditor = editorForFile(manager, betaTop);
    expect("batch-close fixture activation and lock succeed",
           alphaEditor
               && betaEditor
               && manager.activateOpenFile(alphaTop)
               && manager.setTabLocked(betaEditor, true));
    manager.closeTabsToRight();
    QCoreApplication::processEvents();
    expect("close-right removes unlocked tabs and preserves locked tabs",
           manager.editorCount() == 4
               && !editorForFile(manager, tailFile)
               && editorForFile(manager, betaTop)
               && manager.isTabLocked(betaEditor));
    manager.closeOtherTabs();
    QCoreApplication::processEvents();
    expect("close-others preserves the current and locked tabs",
           manager.editorCount() == 2
               && editorForFile(manager, alphaTop)
               && editorForFile(manager, betaTop));
    manager.setTabLocked(betaEditor, false);
    manager.closeOtherTabs();
    QCoreApplication::processEvents();
    expect("closing the last same-name conflict collapses the remaining title",
           manager.editorCount() == 1
               && tabTitleForEditor(manager, alphaEditor)
                      == QStringLiteral("top.sv"));

    manager.setTabLocked(alphaEditor, true);
    manager.openFileInTab(tailFile);
    manager.closeAllTabs();
    QCoreApplication::processEvents();
    expect("close-all preserves locked tabs",
           manager.editorCount() == 1
               && editorForFile(manager, alphaTop)
               && manager.isTabLocked(alphaEditor));
    manager.setTabLocked(alphaEditor, false);
    manager.closeAllTabs();
    QCoreApplication::processEvents();
    expect("close-all removes every unlocked tab",
           manager.editorCount() == 0);
}
}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);

    SharedDocumentRegistry registry;
    const QString fileName =
        QStringLiteral("shared_document_fixture.sv");
    const QString initialText = QStringLiteral(
        "module shared;\n"
        "  logic data; // %1\n"
        "endmodule\n")
        .arg(QString(240, QLatin1Char('x')));
    SharedDocument* document =
        registry.acquire(fileName, initialText);
    expect("registry creates one canonical document",
           document
               && registry.acquire(fileName,
                                   QStringLiteral("ignored"))
                      == document
               && registry.documents().size() == 1);

    MyCodeEditor left;
    MyCodeEditor right;
    SharedDocumentViewState leftState;
    leftState.cursorPosition = 0;
    leftState.anchorPosition = 0;
    SharedDocumentViewState rightState;
    rightState.cursorPosition = initialText.indexOf(
        QStringLiteral("data"));
    rightState.anchorPosition = rightState.cursorPosition;
    const QString leftViewId =
        document->attachView(&left, leftState);
    // A duplicated/restored view state can carry the source view identity.
    // The canonical Document must regenerate that collision centrally.
    rightState.viewId = leftViewId;
    const QString rightViewId =
        document->attachView(&right, rightState);

    expect("two views bind the same QTextDocument",
           !leftViewId.isEmpty()
               && !rightViewId.isEmpty()
               && leftViewId != rightViewId
               && left.document() == right.document()
               && left.document() == document->textDocument()
               && document->viewCount() == 2);
    expect("duplicate restored view identity is regenerated",
           !leftViewId.isEmpty()
               && !rightViewId.isEmpty()
               && leftViewId != rightViewId
               && document->viewState(&left).viewId
                      == leftViewId
               && document->viewState(&right).viewId
                      == rightViewId);
    expect("view cursors remain independent",
           left.textCursor().position() == 0
               && right.textCursor().position()
                      == rightState.cursorPosition);

    QSignalSpy revisionSpy(
        document,
        &SharedDocument::textRevisionChanged);
    QTextCursor insertion(left.document());
    insertion.setPosition(initialText.indexOf(
        QStringLiteral("data")));
    insertion.insertText(QStringLiteral("next_"));
    QCoreApplication::processEvents();
    const QString editedText =
        QString(initialText).insert(
            initialText.indexOf(QStringLiteral("data")),
            QStringLiteral("next_"));
    expect("one edit is visible in every view",
           left.toPlainText() == editedText
               && right.toPlainText() == editedText);
    expect("shared document owns one dirty and revision state",
           document->dirty()
               && document->textRevision() == 1
               && left.semanticDocumentRevision() == 1
               && right.semanticDocumentRevision() == 1
               && revisionSpy.size() == 1);

    right.undo();
    QCoreApplication::processEvents();
    expect("undo from either view reverts the shared stack",
           left.toPlainText() == initialText
               && right.toPlainText() == initialText
               && !document->dirty());
    left.redo();
    QCoreApplication::processEvents();
    expect("redo from either view advances the shared stack",
           left.toPlainText() == editedText
               && right.toPlainText() == editedText
               && document->dirty());
    document->markSaved();
    expect("markSaved updates the sole document state",
           !document->dirty()
               && document->savedTextRevision()
                      == document->textRevision());

    QTextCursor leftSelection(left.document());
    leftSelection.setPosition(0);
    leftSelection.setPosition(6, QTextCursor::KeepAnchor);
    left.setTextCursor(leftSelection);
    QTextCursor rightCursor(right.document());
    rightCursor.setPosition(
        qMax(0, right.document()->characterCount() - 1));
    right.setTextCursor(rightCursor);
    document->captureViewState(&left);
    document->captureViewState(&right);
    expect("selection belongs to the view, not the document",
           document->viewState(&left).anchorPosition == 0
               && document->viewState(&left).cursorPosition == 6
               && document->viewState(&right).cursorPosition
                      != document->viewState(&left).cursorPosition);

    document->setReadOnly(true);
    document->setExternalState(
        SharedDocumentExternalState::Conflict);
    expect("document status is shared across all views",
           left.isReadOnly()
               && right.isReadOnly()
               && document->externalState()
                      == SharedDocumentExternalState::Conflict);
    document->setReadOnly(false);

    expect("detaching preserves a safe independent document",
           document->detachView(&left)
               && left.document() != right.document()
               && left.textCursor().document()
                      == left.document()
               && left.toPlainText() == right.toPlainText()
               && document->viewCount() == 1);
    QTextCursor rightEdit(right.document());
    rightEdit.movePosition(QTextCursor::End);
    rightEdit.insertText(QStringLiteral("// right only\n"));
    QCoreApplication::processEvents();
    expect("detached view no longer observes shared edits",
           !right.toPlainText().endsWith(
               left.toPlainText())
               && !left.toPlainText().contains(
                   QStringLiteral("right only")));

    expect("registry refuses to release an attached document",
           !registry.releaseIfUnused(document));
    expect("last view can detach before registry release",
           document->detachView(&right)
               && document->viewCount() == 0);
    expect("unused document is released atomically",
           registry.releaseIfUnused(document)
               && registry.documents().isEmpty());

    QTemporaryDir temp;
    const QString managedFile =
        temp.filePath(QStringLiteral("shared_tabs.sv"));
    QFile managedFixture(managedFile);
    bool wroteFixture =
        managedFixture.open(
            QIODevice::WriteOnly
            | QIODevice::Text
            | QIODevice::Truncate);
    if (wroteFixture) {
        QTextStream(&managedFixture) << initialText;
        managedFixture.close();
    }
    expect("tab manager fixture is writable",
           temp.isValid() && wroteFixture);

    QWidget splitHost;
    auto* splitLayout = new QVBoxLayout(&splitHost);
    splitLayout->setContentsMargins(0, 0, 0, 0);
    auto* initialTabs = new QTabWidget(&splitHost);
    splitLayout->addWidget(initialTabs);
    TabManager manager(initialTabs);
    expect("tab grouping stable ids round-trip and reject unknown values",
           tabGroupingModeFromStableId(
               tabGroupingModeStableId(TabGroupingMode::None))
                   == TabGroupingMode::None
               && tabGroupingModeFromStableId(
                      tabGroupingModeStableId(TabGroupingMode::Module))
                      == TabGroupingMode::Module
               && tabGroupingModeFromStableId(
                      tabGroupingModeStableId(TabGroupingMode::Workspace))
                      == TabGroupingMode::Workspace
               && tabGroupingModeFromStableId(
                      QStringLiteral("future-mode"))
                      == TabGroupingMode::None);
    QTemporaryDir recoveryStorage;
    expect("recovery storage fixture is writable",
           recoveryStorage.isValid());
    manager.setCrashRecoveryService(
        std::make_unique<CrashRecoveryService>(
            recoveryStorage.path()));
    manager.setWorkspaceScope(
        {temp.path()},
        temp.path());
    manager.enableSplitLayout(&splitHost);
    splitHost.resize(900, 620);
    splitHost.show();
    expect("tab manager opens canonical file",
           manager.openFileInTab(managedFile)
               && manager.editorCount() == 1);
    MyCodeEditor* firstView = manager.getCurrentEditor();
    expect("right split creates a second shared view",
           manager.splitCurrentView(
               EditorSplitDirection::Right)
               && manager.splitCount() == 2
               && manager.editorCount() == 2);
    MyCodeEditor* secondView = manager.getCurrentEditor();
    SharedDocument* managedDocument =
        manager.sharedDocumentForEditor(firstView);
    expect("TabManager views share document dirty and undo state",
           firstView
               && secondView
               && firstView != secondView
               && managedDocument
               && managedDocument
                      == manager.sharedDocumentForEditor(secondView)
               && firstView->document() == secondView->document()
               && managedDocument->viewCount() == 2);

    QStringList requestedTabActionIds;
    manager.setRegisteredTabActionRequestHandler(
        [&manager, &requestedTabActionIds](
            const QString& actionId,
            QString* failureReason) {
            requestedTabActionIds.append(actionId);
            return manager.executeRegisteredTabAction(
                actionId, failureReason);
        });
    QTabWidget* firstGroup =
        manager.editorSplitController()
            ->groupForPage(firstView);
    const int firstIndex = firstGroup
        ? firstGroup->indexOf(firstView)
        : -1;
    QString tabActionFailure =
        QStringLiteral("stale failure");
    const int viewsBeforeDuplicate =
        manager.editorCount();
    const QString duplicateActionId =
        QString::fromLatin1(
            ActionIds::ViewEditorTabDuplicate);
    const bool duplicateRequested =
        manager.requestTabAction(
            duplicateActionId,
            firstGroup,
            firstIndex,
            &tabActionFailure);
    MyCodeEditor* duplicatedView =
        manager.getCurrentEditor();
    expect("Tab context duplicate uses the canonical registered Action",
           duplicateRequested
               && requestedTabActionIds.value(0)
                      == duplicateActionId
               && tabActionFailure.isEmpty()
               && duplicatedView
               && duplicatedView != firstView
               && duplicatedView != secondView
               && manager.editorCount()
                      == viewsBeforeDuplicate + 1
               && managedDocument->viewCount() == 3
               && manager.sharedDocumentForEditor(
                      duplicatedView)
                      == managedDocument
               && duplicatedView->document()
                      == firstView->document());

    const QString toggleLockActionId =
        QString::fromLatin1(
            ActionIds::ViewEditorTabToggleLocked);
    tabActionFailure = QStringLiteral("stale failure");
    expect("Tab context lock targets the clicked shared-document view",
           manager.requestTabAction(
               toggleLockActionId,
               firstGroup,
               firstIndex,
               &tabActionFailure)
               && requestedTabActionIds.value(1)
                      == toggleLockActionId
               && tabActionFailure.isEmpty()
               && manager.getCurrentEditor() == firstView
               && manager.isTabLocked(firstView)
               && firstView->property(
                      "editorTabLocked").toBool()
               && !manager.isTabLocked(secondView));
    expect("Tab context unlock uses the same registered Action route",
           manager.requestTabAction(
               toggleLockActionId,
               firstGroup,
               firstIndex,
               &tabActionFailure)
               && requestedTabActionIds.value(2)
                      == toggleLockActionId
               && tabActionFailure.isEmpty()
               && !manager.isTabLocked(firstView)
               && !firstView->property(
                       "editorTabLocked").toBool());

    QTabWidget* duplicatedGroup =
        manager.editorSplitController()
            ->groupForPage(duplicatedView);
    const int duplicatedIndex = duplicatedGroup
        ? duplicatedGroup->indexOf(duplicatedView)
        : -1;
    const QString closeActionId =
        QString::fromLatin1(
            ActionIds::ViewEditorTabClose);
    tabActionFailure = QStringLiteral("stale failure");
    expect("Tab context close returns to the original shared-view set",
           manager.requestTabAction(
               closeActionId,
               duplicatedGroup,
               duplicatedIndex,
               &tabActionFailure)
               && requestedTabActionIds.value(3)
                      == closeActionId
               && tabActionFailure.isEmpty()
               && manager.editorCount()
                      == viewsBeforeDuplicate
               && managedDocument->viewCount() == 2);
    // Restore the pre-fixture active split so subsequent index-based close
    // checks continue to target the original second view.
    QTabWidget* secondGroup =
        manager.editorSplitController()
            ->groupForPage(secondView);
    if (secondGroup) {
        manager.editorSplitController()
            ->setActiveGroup(secondGroup);
        secondGroup->setCurrentWidget(secondView);
    }

    QSignalSpy sessionStateSpy(
        &manager,
        &TabManager::workspaceSessionStateChanged);
    const int sessionChangesBeforeLock = sessionStateSpy.size();
    expect("tab lock transition schedules one workspace session update",
           manager.setTabLocked(firstView, true)
               && sessionStateSpy.size()
                      == sessionChangesBeforeLock + 1);
    expect("repeating the same tab lock state is persistence-idempotent",
           manager.setTabLocked(firstView, true)
               && sessionStateSpy.size()
                      == sessionChangesBeforeLock + 1);
    expect("tab unlock transition schedules one workspace session update",
           manager.setTabLocked(firstView, false)
               && sessionStateSpy.size()
                      == sessionChangesBeforeLock + 2);
    const int sessionChangesBeforeGrouping = sessionStateSpy.size();
    manager.setTabGroupingMode(TabGroupingMode::Module);
    const int sessionChangesAfterGrouping = sessionStateSpy.size();
    manager.setTabGroupingMode(TabGroupingMode::Module);
    expect("tab grouping schedules once and identical mode is idempotent",
           sessionChangesAfterGrouping
                   == sessionChangesBeforeGrouping + 1
               && sessionStateSpy.size()
                      == sessionChangesAfterGrouping);
    manager.setTabGroupingMode(TabGroupingMode::None);

    QSignalSpy managerEditedSpy(
        manager.getDocumentModel(),
        &DocumentModel::documentEdited);
    QTextCursor managedEdit(firstView->document());
    managedEdit.setPosition(
        initialText.indexOf(QStringLiteral("data")));
    managedEdit.insertText(QStringLiteral("shared_"));
    QCoreApplication::processEvents();
    expect("one shared edit publishes one DocumentModel edit",
           managerEditedSpy.size() == 1
               && firstView->toPlainText()
                      == secondView->toPlainText()
               && manager.hasUnsavedChanges());
    expect("dirty marker is visible in every view tab",
           manager.editorSplitController()
                       ->groupForPage(firstView)
                       ->tabText(
                           manager.editorSplitController()
                               ->groupForPage(firstView)
                               ->indexOf(firstView))
                       .contains(QStringLiteral("●"))
               && manager.editorSplitController()
                      ->groupForPage(secondView)
                      ->tabText(
                          manager.editorSplitController()
                              ->groupForPage(secondView)
                              ->indexOf(secondView))
                      .contains(QStringLiteral("●")));

    secondView->undo();
    QCoreApplication::processEvents();
    expect("undo in split view restores both panes",
           firstView->toPlainText() == initialText
               && secondView->toPlainText() == initialText
               && !managedDocument->dirty());
    firstView->redo();
    QCoreApplication::processEvents();
    managedDocument->markSaved();
    manager.getDocumentModel()->markSaved(firstView);

    managedDocument->setExternalState(
        SharedDocumentExternalState::Conflict);
    QCoreApplication::processEvents();
    expect("conflict marker and full-path tooltip are shared",
           manager.editorSplitController()
                       ->groupForPage(firstView)
                       ->tabText(
                           manager.editorSplitController()
                               ->groupForPage(firstView)
                               ->indexOf(firstView))
                       .contains(QStringLiteral("⚠"))
               && manager.editorSplitController()
                      ->groupForPage(secondView)
                      ->tabToolTip(
                          manager.editorSplitController()
                              ->groupForPage(secondView)
                              ->indexOf(secondView))
                      .contains(
                          QDir::toNativeSeparators(managedFile)));
    managedDocument->setExternalState(
        SharedDocumentExternalState::Current);

    const QString reassignedFile =
        temp.filePath(
            QStringLiteral(
                "shared_tabs_reassigned.sv"));
    QFile reassignedFixture(reassignedFile);
    if (reassignedFixture.open(
            QIODevice::WriteOnly
            | QIODevice::Text
            | QIODevice::Truncate)) {
        QTextStream(&reassignedFixture)
            << initialText;
        reassignedFixture.close();
    }
    manager.getDocumentModel()
        ->setDocumentFileName(
            firstView,
            reassignedFile);
    expect("DocumentModel identity change updates the shared document",
           managedDocument->fileName()
                   == QDir::cleanPath(
                       QFileInfo(
                           reassignedFile)
                           .absoluteFilePath())
               && QFileInfo(
                      reassignedFile)
                      .isFile()
               && firstView
                      ->documentFileName()
                      == secondView
                             ->documentFileName()
               && manager
                      .sharedDocumentForEditor(
                          secondView)
                      == managedDocument);

    manager.setTabLocked(secondView, true);
    const int lockedCount = manager.editorCount();
    manager.closeTab(
        manager.editorSplitController()
            ->groupForPage(secondView)
            ->indexOf(secondView));
    expect("locked tab rejects close",
           manager.editorCount() == lockedCount
               && manager.isTabLocked(secondView));
    manager.setTabLocked(secondView, false);
    manager.closeTab(
        manager.editorSplitController()
            ->groupForPage(secondView)
            ->indexOf(secondView));
    QCoreApplication::processEvents();
    expect("closing one view keeps the canonical document",
           manager.editorCount() == 1
               && managedDocument->viewCount() == 1);
    expect("recently closed view reopens into the same document",
           manager.reopenClosedTab()
               && manager.editorCount() == 2
               && managedDocument->viewCount() == 2
               && manager.sharedDocumentForEditor(
                      manager.getCurrentEditor())
                      == managedDocument);

    const QList<MyCodeEditor*> sessionViews =
        manager.openEditors();
    QHash<QString, int> expectedHorizontalScroll;
    for (int index = 0; index < sessionViews.size(); ++index) {
        MyCodeEditor* editor = sessionViews.at(index);
        editor->setLineWrapMode(QPlainTextEdit::NoWrap);
        expectedHorizontalScroll.insert(
            editor->property("editorViewId").toString(),
            index == 0 ? 29 : 73);
    }
    QCoreApplication::processEvents();
    bool horizontalRangesAvailable = sessionViews.size() == 2;
    const int sessionChangesBeforeHorizontalScroll =
        sessionStateSpy.size();
    for (MyCodeEditor* editor : sessionViews) {
        const int requested = expectedHorizontalScroll.value(
            editor->property("editorViewId").toString());
        QScrollBar* bar = editor->horizontalScrollBar();
        horizontalRangesAvailable = horizontalRangesAvailable
            && bar && bar->maximum() >= requested;
        if (bar)
            bar->setValue(requested);
    }
    expect("split views expose independent horizontal scroll ranges",
           horizontalRangesAvailable
               && sessionStateSpy.size()
                      > sessionChangesBeforeHorizontalScroll);

    const QList<WorkspaceSessionTabState> splitSession =
        manager.workspaceSessionTabs(temp.path());
    bool horizontalSessionMatches = splitSession.size() == 2;
    for (const WorkspaceSessionTabState& tab : splitSession) {
        horizontalSessionMatches = horizontalSessionMatches
            && expectedHorizontalScroll.contains(tab.viewId)
            && tab.horizontalScrollValue
                   == expectedHorizontalScroll.value(tab.viewId);
    }
    expect("workspace session records both view locations",
           splitSession.size() == 2
               && splitSession.at(0).groupIndex
                      != splitSession.at(1).groupIndex
               && !splitSession.at(0).viewId.isEmpty()
               && !splitSession.at(1).viewId.isEmpty()
               && horizontalSessionMatches);

    QWidget restoredSplitHost;
    auto* restoredSplitLayout =
        new QVBoxLayout(&restoredSplitHost);
    restoredSplitLayout->setContentsMargins(0, 0, 0, 0);
    auto* restoredInitialTabs =
        new QTabWidget(&restoredSplitHost);
    restoredSplitLayout->addWidget(restoredInitialTabs);
    TabManager restoredManager(restoredInitialTabs);
    restoredManager.setWorkspaceScope({temp.path()}, temp.path());
    restoredManager.enableSplitLayout(&restoredSplitHost);
    QObject::connect(
        &restoredManager,
        &TabManager::tabCreated,
        &restoredSplitHost,
        [](MyCodeEditor* editor) {
            if (!editor)
                return;
            editor->setLineWrapMode(QPlainTextEdit::NoWrap);
            if (QScrollBar* bar = editor->horizontalScrollBar())
                bar->setRange(0, qMax(200, bar->maximum()));
        });
    restoredSplitHost.resize(900, 620);
    restoredSplitHost.show();
    const QStringList restoredSessionFiles =
        restoredManager.restoreWorkspaceSessionTabs(
            temp.path(), splitSession);
    QCoreApplication::processEvents();
    bool restoredHorizontalScroll =
        restoredSessionFiles.size() == 2
        && restoredManager.editorCount() == 2;
    for (MyCodeEditor* editor : restoredManager.openEditors()) {
        const QString viewId =
            editor->property("editorViewId").toString();
        restoredHorizontalScroll = restoredHorizontalScroll
            && expectedHorizontalScroll.contains(viewId)
            && editor->horizontalScrollBar()
            && editor->horizontalScrollBar()->value()
                   == expectedHorizontalScroll.value(viewId);
    }
    expect("workspace session restores each view horizontal scroll",
           restoredHorizontalScroll);
    manager.toggleCurrentSplitMaximized();
    expect("current split maximizes temporarily",
           manager.editorSplitController()
               ->isGroupMaximized());
    manager.toggleCurrentSplitMaximized();
    expect("current split restores exactly",
           !manager.editorSplitController()
                ->isGroupMaximized());

    QTextCursor pendingEdit(managedDocument->textDocument());
    pendingEdit.movePosition(QTextCursor::End);
    pendingEdit.insertText(
        QStringLiteral("// pending local change\n"));
    const QString externalOnlyFile =
        temp.filePath(QStringLiteral("external_only.sv"));
    QFile externalOnlyFixture(externalOnlyFile);
    bool wroteExternalOnly =
        externalOnlyFixture.open(
            QIODevice::WriteOnly
            | QIODevice::Text
            | QIODevice::Truncate);
    if (wroteExternalOnly) {
        QTextStream(&externalOnlyFixture) << initialText;
        externalOnlyFixture.close();
    }
    expect("second pending-state fixture opens",
           wroteExternalOnly
               && manager.openFileInTab(externalOnlyFile));
    SharedDocument* externalOnlyDocument =
        manager.sharedDocumentForEditor(
            manager.getCurrentEditor());
    externalOnlyDocument->setExternalState(
        SharedDocumentExternalState::ExternallyModified);

    int centralizedPromptCount = 0;
    QList<PendingDocumentChange> reviewedChanges;
    manager.unsavedDocumentManagerForTesting()
        ->setDecisionProvider(
            [&centralizedPromptCount, &reviewedChanges](
                const QList<PendingDocumentChange>& changes,
                QWidget*) {
                ++centralizedPromptCount;
                reviewedChanges = changes;
                return UnsavedDocumentBatchDecision::DiscardAll;
            });
    expect("window close review centralizes dirty and external states",
           manager.resolvePendingDocuments(nullptr)
               && centralizedPromptCount == 1
               && reviewedChanges.size() == 2);
    bool sawUnsaved = false;
    bool sawExternal = false;
    for (const PendingDocumentChange& change : reviewedChanges) {
        sawUnsaved =
            sawUnsaved
            || change.kind
                   == PendingDocumentChangeKind::Unsaved;
        sawExternal =
            sawExternal
            || change.kind
                   == PendingDocumentChangeKind::
                       ExternallyModified;
    }
    expect("centralized review preserves explicit state classes",
           sawUnsaved && sawExternal);

    CrashRecoveryListResult recoveryList =
        manager.listCrashRecoveryCandidates(temp.path());
    expect("dirty document has a deterministic recovery snapshot",
           recoveryList.succeeded()
               && recoveryList.candidates.size() == 1);
    const CrashRecoveryCandidate pendingCandidate =
        recoveryList.candidates.isEmpty()
        ? CrashRecoveryCandidate()
        : recoveryList.candidates.first();
    const int documentEditCountBeforeRecoveryReads =
        managerEditedSpy.size();
    const CrashRecoveryReadResult comparison =
        manager.compareCrashRecoveryCandidate(
            pendingCandidate.recoveryId,
            temp.path());
    const CrashRecoveryRecoverResult recovered =
        manager.recoverCrashRecoveryText(
            pendingCandidate.recoveryId,
            temp.path());
    expect("comparison and recovery are structured read-only operations",
           comparison.status
                   == CrashRecoveryStatus::Success
               && recovered.status
                      == CrashRecoveryStatus::Success
               && recovered.text.contains(
                   QStringLiteral(
                       "pending local change"))
               && managerEditedSpy.size()
                      == documentEditCountBeforeRecoveryReads);

    const CrashRecoveryApplyResult dirtyApply =
        manager.applyCrashRecoveryCandidate(
            comparison.candidate);
    expect("recovery refuses to overwrite a dirty open document",
           dirtyApply.status
               == CrashRecoveryStatus::IdentityMismatch
               && managedDocument->textDocument()
                      ->toPlainText()
                      .contains(
                          QStringLiteral(
                              "pending local change")));

    QTabWidget restartedRecoveryTabs;
    TabManager restartedRecoveryManager(
        &restartedRecoveryTabs);
    QSignalSpy recoveryAvailableSpy(
        &restartedRecoveryManager,
        &TabManager::
            crashRecoveryCandidatesAvailable);
    restartedRecoveryManager
        .setCrashRecoveryService(
            std::make_unique<CrashRecoveryService>(
                recoveryStorage.path()));
    restartedRecoveryManager.setWorkspaceScope(
        {temp.path()},
        temp.path());
    expect("workspace activation reports abnormal-start candidates",
           recoveryAvailableSpy.size() == 1
               && recoveryAvailableSpy.first()
                      .at(1)
                      .toInt()
                      == 1);
    const CrashRecoveryApplyResult applied =
        restartedRecoveryManager
            .applyCrashRecoveryCandidate(
                comparison.candidate);
    QFile unchangedSource(reassignedFile);
    QString unchangedSourceText;
    if (unchangedSource.open(
            QIODevice::ReadOnly
            | QIODevice::Text)) {
        unchangedSourceText =
            QString::fromUtf8(
                unchangedSource.readAll());
        unchangedSource.close();
    }
    SharedDocument* recoveredDocument =
        restartedRecoveryManager
            .sharedDocumentForEditor(
                restartedRecoveryManager
                    .getCurrentEditor());
    expect("reviewed recovery applies only to dirty in-memory state",
           applied.status
                   == CrashRecoveryStatus::Success
               && applied.openedView
               && recoveredDocument
               && recoveredDocument->dirty()
               && recoveredDocument
                      ->externalState()
                      == (comparison.candidate
                                  .sourceState
                              == CrashRecoverySourceState::
                                  ExternallyModified
                          ? SharedDocumentExternalState::
                                Conflict
                          : SharedDocumentExternalState::
                                Current)
               && recoveredDocument
                      ->textDocument()
                      ->toPlainText()
                      .contains(
                          QStringLiteral(
                              "pending local change"))
               && !unchangedSourceText.contains(
                   QStringLiteral(
                       "pending local change")));
    expect("recovered text synchronizes document queries and editor cache",
           recoveredDocument
               && restartedRecoveryManager
                      .getPlainTextFromOpenFile(
                          reassignedFile)
                      == recovered.text
               && restartedRecoveryManager
                      .getCurrentEditor()
               && restartedRecoveryManager
                      .getCurrentEditor()
                      ->cachedDocumentText()
                      == recovered.text);
    expect("recovered document is reported unsaved at the reviewed revision",
           recoveredDocument
               && restartedRecoveryManager
                      .hasUnsavedChanges()
               && recoveredDocument
                      ->textRevision()
                      == static_cast<std::uint64_t>(
                          applied.documentRevision)
               && restartedRecoveryManager
                      .getCurrentEditor()
                      ->semanticDocumentRevision()
                      == recoveredDocument
                             ->textRevision());

    const CrashRecoveryOperationResult discarded =
        manager.discardCrashRecoveryCandidate(
            pendingCandidate.recoveryId,
            temp.path());
    expect("explicit discard removes the selected recovery only",
           discarded.succeeded()
               && manager
                      .listCrashRecoveryCandidates(
                          temp.path())
                      .candidates.isEmpty());

    QTextCursor firstRecoveryEdit(
        managedDocument->textDocument());
    firstRecoveryEdit.movePosition(QTextCursor::End);
    firstRecoveryEdit.insertText(
        QStringLiteral("// recovery cycle first\n"));
    recoveryList =
        manager.listCrashRecoveryCandidates(
            temp.path());
    const CrashRecoveryCandidate firstCycleCandidate =
        recoveryList.candidates.isEmpty()
        ? CrashRecoveryCandidate()
        : recoveryList.candidates.first();
    QTextCursor secondRecoveryEdit(
        managedDocument->textDocument());
    secondRecoveryEdit.movePosition(QTextCursor::End);
    secondRecoveryEdit.insertText(
        QStringLiteral("// recovery cycle second\n"));
    const CrashRecoveryListResult beforeCheckpoint =
        manager.listCrashRecoveryCandidates(
            temp.path());
    const int editsBeforeCheckpoint =
        managerEditedSpy.size();
    manager.checkpointCrashRecovery();
    const CrashRecoveryListResult afterCheckpoint =
        manager.listCrashRecoveryCandidates(
            temp.path());
    expect("revision cycle defers repeated writes until an explicit checkpoint",
           !firstCycleCandidate.recoveryId.isEmpty()
               && beforeCheckpoint.candidates.size() == 1
               && beforeCheckpoint.candidates.first()
                      .documentRevision
                      == firstCycleCandidate
                             .documentRevision
               && afterCheckpoint.candidates.size() == 1
               && afterCheckpoint.candidates.first()
                      .documentRevision
                      > firstCycleCandidate
                            .documentRevision
               && managerEditedSpy.size()
                      == editsBeforeCheckpoint);

    FileCommandCoordinator closeCoordinator(
        &manager,
        nullptr);
    QCloseEvent closeEvent;
    closeCoordinator.handleCloseEvent(
        &closeEvent,
        &splitHost);
    expect("accepted normal window close clears recovery snapshots",
           closeEvent.isAccepted()
               && manager
                      .listCrashRecoveryCandidates(
                          temp.path())
                      .candidates.isEmpty());

    exerciseTabIdentityGroupingAndBatchClose();

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
