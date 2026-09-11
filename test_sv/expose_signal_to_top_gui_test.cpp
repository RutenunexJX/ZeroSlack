#include "mainwindow.h"
#include <QLabel>
#include "editoractioncontextservice.h"
#include "editorhoverpopup.h"
#include "hierarchyservice.h"
#include "liveinsighttoolpage.h"
#include "semanticindex.h"
#include <QDir>
#include <QFileInfo>
#include "editorcoordinator.h"
#include "exposesignaltotoppreview.h"
#include "exposesignaltotopservice.h"
#include "formatterservice.h"
#include "mycodeeditor.h"
#include "semantic_fixture_records.h"
#include "semanticindexsnapshot.h"
#include "tabmanager.h"
#include "workspacemanager.h"
#include "workspaceeditdocumentmanager.h"
#include "workspaceedittransactionservice.h"

#include <rtledit/edit_plan.h>

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCompleter>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDockWidget>
#include <QFile>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSignalBlocker>
#include <QStatusBar>
#include "activitylogservice.h"
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTimer>

#include <cstdio>
#include <memory>

namespace {
int checks = 0;
int failures = 0;

void check(const char* name, bool condition)
{
    ++checks;
    failures += condition ? 0 : 1;
    std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", name);
}

std::shared_ptr<const SemanticIndexSnapshot> snapshot(
    const QList<SemanticSymbolRecord>& records,
    const QHash<QString, QString>& contents = {})
{
    return std::make_shared<const SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolRecords(
            records, {}, {}, contents));
}

std::shared_ptr<const SemanticIndexSnapshot> snapshotWithRelationships(
    const QList<SemanticSymbolRecord>& records,
    const QList<SemanticRelationship>& relationships,
    const QHash<QString, QString>& contents = {})
{
    return std::make_shared<const SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolRecords(
            records, relationships, {}, contents));
}


struct MenuActionState {
    bool menuShown = false;
    bool found = false;
    bool enabled = false;
    QString visibleText;
    QString statusTip;
    QString groupTitle;
};

struct ContextActionTriggerState {
    bool menuShown = false;
    bool found = false;
    bool enabled = false;
    bool triggered = false;
    QString actionId;
    QString executionRoute;
};

QAction* findMenuAction(
    QMenu* menu,
    const QString& objectName,
    QString* groupTitle = nullptr)
{
    if (!menu)
        return nullptr;
    for (QAction* action : menu->actions()) {
        if (action->objectName() == objectName) {
            if (groupTitle)
                *groupTitle = menu->title();
            return action;
        }
        if (QMenu* child = action->menu()) {
            if (QAction* found =
                    findMenuAction(child, objectName, groupTitle)) {
                return found;
            }
        }
    }
    return nullptr;
}

EditorHoverPopup* activeExposeSignalPreview(QWidget* host)
{
    if (!host)
        return nullptr;
    const QList<EditorHoverPopup*> peeks =
        host->findChildren<EditorHoverPopup*>(
            QString(), Qt::FindDirectChildrenOnly);
    for (EditorHoverPopup* peek : peeks) {
        if (peek
            && peek->property(
                   "exposeSignalToTopPreview")
                   .toBool()) {
            return peek;
        }
    }
    return nullptr;
}

int positionInside(const QString& text, const QString& needle)
{
    const int start = text.indexOf(needle);
    return start < 0 ? -1 : start + qMax(0, needle.size() / 2);
}

MenuActionState contextMenuState(
    MyCodeEditor* editor,
    const QString& clickNeedle,
    const QString& oldCursorNeedle)
{
    if (!editor)
        return {};

    const QString text = editor->cachedDocumentText();
    const int oldCursorPosition =
        positionInside(text, oldCursorNeedle);
    const int clickPosition =
        positionInside(text, clickNeedle);
    if (oldCursorPosition < 0 || clickPosition < 0)
        return {};

    QTextCursor oldCursor(editor->document());
    oldCursor.setPosition(oldCursorPosition);
    editor->setTextCursor(oldCursor);

    QTextCursor clickCursor(editor->document());
    clickCursor.setPosition(clickPosition);
    const QPoint clickPoint = editor->cursorRect(clickCursor).center();

    MenuActionState state;
    const QMetaObject::Connection menuObserved = QObject::connect(
        editor,
        &MyCodeEditor::sourceSymbolContextMenuRequested,
        editor,
        [&state](QMenu* menu, const EditorSemanticContext&) {
            state.menuShown = menu != nullptr;
            if (!menu)
                return;
            QTimer::singleShot(0, menu, [&state, menu]() {
                QAction* expose = findMenuAction(
                    menu,
                    QStringLiteral("exposeSignalToTopAction"),
                    &state.groupTitle);
                state.found = expose != nullptr;
                state.enabled = expose && expose->isEnabled();
                menu->close();
                state.visibleText = expose
                    ? expose->text() : QString();
                state.statusTip = expose
                    ? expose->statusTip() : QString();
            });
        });
    QContextMenuEvent event(
        QContextMenuEvent::Mouse,
        clickPoint,
        editor->viewport()->mapToGlobal(clickPoint));
    QApplication::sendEvent(editor->viewport(), &event);
    QObject::disconnect(menuObserved);
    QCoreApplication::processEvents();
    return state;
}

ContextActionTriggerState triggerContextMenuAction(
    MyCodeEditor* editor,
    const QString& clickNeedle,
    const QString& actionId)
{
    if (!editor)
        return {};

    const int clickPosition = positionInside(
        editor->cachedDocumentText(), clickNeedle);
    if (clickPosition < 0)
        return {};

    QTextCursor clickCursor(editor->document());
    clickCursor.setPosition(clickPosition);
    const QPoint clickPoint =
        editor->cursorRect(clickCursor).center();

    ContextActionTriggerState state;
    const QMetaObject::Connection menuObserved = QObject::connect(
        editor,
        &MyCodeEditor::sourceSymbolContextMenuRequested,
        editor,
        [&state, actionId](
            QMenu* menu,
            const EditorSemanticContext&) {
            state.menuShown = menu != nullptr;
            if (!menu)
                return;
            QTimer::singleShot(
                0,
                menu,
                [&state, menu, actionId]() {
                    QAction* action = findMenuAction(
                        menu, actionId);
                    state.found = action != nullptr;
                    state.enabled =
                        action && action->isEnabled();
                    state.actionId = action
                        ? action->property("actionId")
                              .toString()
                        : QString();
                    state.executionRoute = action
                        ? action->property(
                              "executionRoute")
                              .toString()
                        : QString();
                    if (state.enabled) {
                        action->trigger();
                        state.triggered = true;
                    }
                    menu->close();
                });
        });
    QContextMenuEvent event(
        QContextMenuEvent::Mouse,
        clickPoint,
        editor->viewport()->mapToGlobal(clickPoint));
    QApplication::sendEvent(editor->viewport(), &event);
    QObject::disconnect(menuObserved);
    QCoreApplication::processEvents();
    return state;
}

ContextActionTriggerState triggerEditorOwnedContextAction(
    MyCodeEditor* editor,
    const QString& clickNeedle,
    const QString& actionId)
{
    if (!editor)
        return {};
    const int clickPosition = positionInside(
        editor->cachedDocumentText(), clickNeedle);
    if (clickPosition < 0)
        return {};
    QTextCursor clickCursor(editor->document());
    clickCursor.setPosition(clickPosition);
    const QPoint clickPoint =
        editor->cursorRect(clickCursor).center();

    ContextActionTriggerState state;
    QTimer::singleShot(0, editor, [&]() {
        QMenu* menu = qobject_cast<QMenu*>(
            QApplication::activePopupWidget());
        state.menuShown = menu != nullptr;
        QAction* action = findMenuAction(
            menu, actionId);
        state.found = action != nullptr;
        state.enabled = action && action->isEnabled();
        state.actionId = action
            ? action->property("actionId").toString()
            : QString();
        state.executionRoute = action
            ? action->property("executionRoute")
                  .toString()
            : QString();
        if (state.enabled) {
            action->trigger();
            state.triggered = true;
        }
        if (menu)
            menu->close();
    });
    QTimer::singleShot(1000, editor, []() {
        if (QMenu* menu = qobject_cast<QMenu*>(
                QApplication::activePopupWidget())) {
            menu->close();
        }
    });
    QContextMenuEvent event(
        QContextMenuEvent::Mouse,
        clickPoint,
        editor->viewport()->mapToGlobal(clickPoint));
    QApplication::sendEvent(editor->viewport(), &event);
    QCoreApplication::processEvents();
    return state;
}

ExposeSignalToTopReport readyDialogReport(
    const QString& portName)
{
    ExposeSignalToTopReport report;
    report.status = ExposeSignalToTopReportStatus::Ready;
    report.failureReason =
        rtledit::ExposeSignalFailureReason::None;
    report.sourceInstancePath =
        QStringLiteral("top.u_mid.u_leaf");
    report.targetInstancePath = QStringLiteral("top");
    report.exportedPortName = portName;
    report.affectedModules = {
        QStringLiteral("leaf"),
        QStringLiteral("mid"),
        QStringLiteral("top")};
    report.affectedFiles = {
        QStringLiteral("leaf.sv"),
        QStringLiteral("mid.sv"),
        QStringLiteral("top.sv")};
    report.affectedInstancePaths = {
        QStringLiteral("top.u_mid.u_leaf"),
        QStringLiteral("top.u_mid"),
        QStringLiteral("top")};
    ExposeSignalHierarchyStepView first;
    first.index = 0;
    first.childInstancePath =
        QStringLiteral("top.u_mid.u_leaf");
    first.childModule = QStringLiteral("leaf");
    first.childInstance = QStringLiteral("u_leaf");
    first.parentInstancePath = QStringLiteral("top.u_mid");
    first.parentModule = QStringLiteral("mid");
    report.hierarchySteps.append(first);
    report.planResult.status =
        rtledit::ExposeSignalPlanStatus::Ready;
    report.transaction.status =
        rtledit::TransactionPrepareStatus::Ready;
    report.transaction.preview.status =
        rtledit::PreviewStatus::Built;
    report.transaction.sourceDiff.status =
        rtledit::SourceDiffStatus::Built;
    report.sourceDiff.status = rtledit::SourceDiffStatus::Built;
    report.renderedDiff =
        QStringLiteral("--- leaf.sv\n+++ leaf.sv\n+ output logic payload_out");
    return report;
}

void runEditorActionContextRegression()
{
    QTemporaryDir temp;
    check("action context fixture root is available", temp.isValid());
    if (!temp.isValid())
        return;

    const QString topFile =
        temp.filePath(QStringLiteral("top.sv"));
    const QString leafFile =
        temp.filePath(QStringLiteral("leaf.sv"));
    const QString topText =
        QStringLiteral("module top; leaf u_leaf(); leaf u_leaf2(); endmodule\n");
    const QString leafText =
        QStringLiteral("module leaf; logic payload; endmodule\n");
    for (const auto& source : {
             qMakePair(topFile, topText),
             qMakePair(leafFile, leafText)}) {
        QFile file(source.first);
        const bool written =
            file.open(QIODevice::WriteOnly | QIODevice::Text)
            && file.write(source.second.toUtf8())
                   == source.second.toUtf8().size();
        file.close();
        check("action context fixture source is written", written);
    }

    auto module = [](int id,
                     const QString& file,
                     const QString& text,
                     const QString& name) {
        return SemanticFixtureRecordBuilder(
                   name, SymbolTaxonomy::DeclarationKind::Module)
            .withFile(file)
            .withLocalHandle(id)
            .withLine(1)
            .withTextSpan(text.indexOf(name), name.size())
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Module)
            .record();
    };
    const SemanticSymbolRecord top =
        module(501, topFile, topText, QStringLiteral("top"));
    const SemanticSymbolRecord leaf =
        module(502, leafFile, leafText, QStringLiteral("leaf"));
    auto instance = [&](int id, const QString& name) {
        return SemanticFixtureRecordBuilder(
                   name, SymbolTaxonomy::DeclarationKind::Instance)
            .withFile(topFile)
            .withLocalHandle(id)
            .withLine(1)
            .withTextSpan(topText.indexOf(name), name.size())
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Inst)
            .withOwner(SymbolTaxonomy::SymbolOwnerScope::Module,
                       QStringLiteral("top"), top.stableKey)
            .withType(QStringLiteral("leaf"),
                      QStringLiteral("leaf"),
                      SymbolTaxonomy::DeclarationKind::Module)
            .record();
    };
    const SemanticSymbolRecord firstInstance =
        instance(503, QStringLiteral("u_leaf"));
    const SemanticSymbolRecord secondInstance =
        instance(504, QStringLiteral("u_leaf2"));

    SemanticIndex index;
    index.setSnapshot(snapshotWithRelationships(
        {top, leaf, firstInstance},
        {semanticFixtureRelationship(
            top,
            firstInstance,
            SymbolRelationshipEngine::INSTANTIATES)},
        {{topFile, topText}, {leafFile, leafText}}));
    HierarchyService hierarchy(&index);
    EditorActionContextService service(&index, &hierarchy);

    ProjectSnapshot workspaceProject;
    workspaceProject.workspaceRoot = temp.path();
    workspaceProject.topModule = QStringLiteral("top");
    workspaceProject.allFiles = {
        QDir::cleanPath(QDir::fromNativeSeparators(
            QFileInfo(topFile).absoluteFilePath())),
        QDir::cleanPath(QDir::fromNativeSeparators(
            QFileInfo(leafFile).absoluteFilePath()))};
    service.updateWorkspaceContext(workspaceProject);

    EditorActionContextQuery query;
    query.editorContext.fileName = leafFile;
    query.editorContext.moduleName = QStringLiteral("leaf");
    query.editorContext.documentRevision = 9;
    query.editorContext.hierarchyInstance = {
        temp.path(), QString(), QString()};
    query.semanticStatus.fileName = leafFile;
    query.semanticStatus.state = DocumentSemanticState::Current;
    query.semanticStatus.documentRevision = 9;

    const EditorActionContext filesContext = service.resolve(query);
    check("Files entry uniquely resolves the active hierarchy instance",
          filesContext.hierarchyCandidates.size() == 1
              && filesContext.hierarchyAutoResolved
              && filesContext.resolvedHierarchy.activeTopModule
                     == QStringLiteral("top")
              && filesContext.resolvedHierarchy.instancePath
                     == QStringLiteral("top.u_leaf"));
    check("action context exposes syntax and semantic revisions",
          filesContext.syntaxRevision == 9
              && filesContext.semanticSnapshotRevision
                     == index.snapshotRevision()
              && filesContext.semanticState
                     == EditorActionSemanticState::Current);

    query.editorContext.hierarchyInstance = {
        temp.path(),
        QStringLiteral("top"),
        QStringLiteral("top.u_leaf")};
    const EditorActionContext designContext = service.resolve(query);
    check("Files and Design entry produce the same candidate set",
          designContext.hierarchyCandidates
                  == filesContext.hierarchyCandidates
              && designContext.resolvedHierarchy
                     == filesContext.resolvedHierarchy);

    index.setSnapshot(snapshotWithRelationships(
        {top, leaf, firstInstance, secondInstance},
        {semanticFixtureRelationship(
             top,
             firstInstance,
             SymbolRelationshipEngine::INSTANTIATES),
         semanticFixtureRelationship(
             top,
             secondInstance,
             SymbolRelationshipEngine::INSTANTIATES)},
        {{topFile, topText}, {leafFile, leafText}}));
    query.editorContext.hierarchyInstance = {
        temp.path(), QString(), QString()};
    const EditorActionContext ambiguousContext = service.resolve(query);
    check("multiple hierarchy instances stay enterable and require selection",
          ambiguousContext.hierarchyCandidates.size() == 2
              && ambiguousContext.hierarchySelectionRequired
              && !ambiguousContext.hierarchyBound());
    check("multiple-instance context names each top and instance",
          ambiguousContext.hierarchyCandidates.size() == 2
              && ambiguousContext.hierarchyCandidates.at(0)
                     .displayText().contains(QStringLiteral("top"))
              && ambiguousContext.hierarchyCandidates.at(1)
                     .displayText().contains(QStringLiteral("top")));

    query.semanticStatus.state = DocumentSemanticState::Stale;
    const EditorActionContext staleContext = service.resolve(query);
    check("stale semantic state is continuously visible",
          staleContext.semanticState == EditorActionSemanticState::Stale
              && staleContext.compactText().contains(
                     QStringLiteral("stale")));
    query.semanticAnalysisActive = true;
    const EditorActionContext analyzingContext = service.resolve(query);
    check("active analysis supersedes stale display state",
          analyzingContext.semanticState
                  == EditorActionSemanticState::Analyzing
              && analyzingContext.compactText().contains(
                     QStringLiteral("analyzing")));
}
void runEditorActionContextStripRegression()
{
    MainWindow window;
    check("status bar and context chip are removed",
          window.findChild<QStatusBar*>() == nullptr
          && window.findChild<QLabel*>(QStringLiteral("editorActionContextChip")) == nullptr);
}

void runFileActionRegistryShellRegression()
{
    MainWindow window;
    window.resize(900, 600);
    window.show();
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);

    struct ExpectedAdapter {
        const char* id;
        const char* objectName;
        const char* route;
    };
    const ExpectedAdapter expectedAdapters[] = {
        {ActionIds::FileNew,
         "new_file",
         "ui.file.new"},
        {ActionIds::FileOpen,
         "open_file",
         "ui.file.open"},
        {ActionIds::FileSave,
         "save_file",
         "ui.file.save"},
        {ActionIds::FileSaveAs,
         "save_as",
         "ui.file.saveAs"},
        {ActionIds::WorkspaceOpen,
         "open_direction_as_workspace",
         "ui.workspace.open"},
    };
    bool adaptersComplete = true;
    for (const ExpectedAdapter& expected :
         expectedAdapters) {
        QAction* action =
            window.findChild<QAction*>(
                QString::fromLatin1(
                    expected.objectName));
        const ActionDescriptor* descriptor =
            findActionById(
                QString::fromLatin1(expected.id));
        adaptersComplete =
            adaptersComplete
            && action
            && descriptor
            && action->property("actionId")
                   .toString() == descriptor->id
            && action->property("executionRoute")
                   .toString()
                   == QString::fromLatin1(expected.route)
            && action->shortcut().toString(
                   QKeySequence::PortableText)
                   == effectiveActionShortcut(
                       descriptor->id)
            && action->shortcutContext()
                   == Qt::ApplicationShortcut
            && window.actions().contains(action);
    }
    check("file shortcuts are Action Registry adapters",
          adaptersComplete);
    check("legacy shadow edit and Escape actions are absent",
          !window.findChild<QAction*>(
              QStringLiteral("copy"))
              && !window.findChild<QAction*>(
                  QStringLiteral("paste"))
              && !window.findChild<QAction*>(
                  QStringLiteral("cut"))
              && !window.findChild<QAction*>(
                  QStringLiteral("undo"))
              && !window.findChild<QAction*>(
                  QStringLiteral("redo"))
              && !window.findChild<QAction*>(
                  QStringLiteral("exit"))
              && !window.findChild<QAction*>(
                  QStringLiteral("font"))
              && !window.findChild<QAction*>(
                  QStringLiteral("print")));
    check("simplified shell has no removed top-level entry or rail",
          !window.findChild<QMenu*>(
              QStringLiteral("searchMenu"))
              && !window.findChild<QMenu*>(
                  QStringLiteral("navigateMenu"))
              && !window.findChild<QMenu*>(
                  QStringLiteral("helpMenu"))
              && !window.findChild<QWidget*>(
                  QStringLiteral("workspaceTabBar"))
              && !window.findChild<QWidget*>(
                  QStringLiteral("shellNavigationRailDock")));

    QAction* navigationAction =
        window.findChild<QAction*>(
            QStringLiteral("viewNavigationAction"));
    QDockWidget* navigationDock =
        window.findChild<QDockWidget*>(
            QStringLiteral("navigationDock"));
    const bool navigationInitiallyVisible =
        navigationDock && navigationDock->isVisible();
    resetApplicationActionExecutionHistory();
    if (navigationAction)
        navigationAction->trigger();
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    check("Navigation shortcut is a Registry menu adapter",
          navigationAction
              && navigationDock
              && navigationAction->property("actionId")
                     .toString()
                     == QString::fromLatin1(
                         ActionIds::ViewNavigation)
              && navigationAction->shortcut().toString(
                     QKeySequence::PortableText)
                     == QStringLiteral("Ctrl+1")
              && navigationDock->isVisible()
                     != navigationInitiallyVisible
              && applicationActionExecutionHistory()
                     .lastActionId()
                     == QString::fromLatin1(
                         ActionIds::ViewNavigation));

    window.activateWindow();
    window.setFocus();
    QWidget* globalControlPanel =
        window.findChild<QWidget*>(
            QStringLiteral("globalControlPanel"));
    QVariantMap globalControlShortcutOverride;
    globalControlShortcutOverride.insert(
        QString::fromLatin1(
            ActionIds::ViewGlobalControl),
        QStringLiteral("Ctrl+Alt+Space"));
    QStringList globalControlShortcutIssues;
    const bool globalControlOverrideAccepted =
        configureActionShortcutOverrides(
            globalControlShortcutOverride,
            &globalControlShortcutIssues);
    resetApplicationActionExecutionHistory();
    QTest::keyClick(
        &window,
        Qt::Key_Space,
        Qt::ControlModifier);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    const bool oldGlobalControlShortcutInactive =
        globalControlPanel
        && !globalControlPanel->isVisible()
        && applicationActionExecutionHistory()
               .lastActionId().isEmpty();
    QTest::keyClick(
        &window,
        Qt::Key_Space,
        Qt::ControlModifier | Qt::AltModifier);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    configureActionShortcutOverrides({});
    check("Global Control shortcut executes its Registry route",
          globalControlOverrideAccepted
              && globalControlShortcutIssues.isEmpty()
              && oldGlobalControlShortcutInactive
              && globalControlPanel
              && globalControlPanel->isVisible()
              && applicationActionExecutionHistory()
                     .lastActionId()
                     == QString::fromLatin1(
                         ActionIds::ViewGlobalControl));
    if (globalControlPanel)
        globalControlPanel->hide();

    QAction* newFile =
        window.findChild<QAction*>(
            QStringLiteral("new_file"));
    const int editorsBefore =
        window.tabManager->editorCount();
    if (newFile)
        newFile->trigger();
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    check("registry New File route creates one editor",
          newFile
              && window.tabManager->editorCount()
                     == editorsBefore + 1);
    window.hide();
}

void runContextActionRegistryExecutionRegression()
{
    MainWindow window;
    window.resize(900, 600);
    window.show();
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);

    QTemporaryDir temp;
    check("context Action execution fixture root is available",
          temp.isValid());
    if (!temp.isValid())
        return;
    const QString fileName = temp.filePath(
        QStringLiteral("context_action_execution.sv"));
    const QString source = QStringLiteral(
        "module context_action_execution;\n"
        "logic first;\n"
        "logic second;\n"
        "logic third;\n"
        "assign first = first;\n"
        "assign first = second;\n"
        "`ifdef FEATURE\n"
        "logic guarded;\n"
        "`else\n"
        "logic fallback;\n"
        "`endif\n"
        "logic    format_me;\n"
        "logic join_a;\n"
        "logic join_b;\n"
        "logic delete_me;\n"
        "endmodule\n");
    QFile fixture(fileName);
    const bool written =
        fixture.open(QIODevice::WriteOnly | QIODevice::Text)
        && fixture.write(source.toUtf8())
               == source.toUtf8().size();
    fixture.close();
    check("context Action execution fixture is written", written);
    const bool opened = written
        && window.tabManager->openFileInTab(fileName);
    check("context Action execution fixture opens a real editor",
          opened);
    MyCodeEditor* editor =
        window.tabManager->getCurrentEditor();
    if (!editor)
        return;

    QTextCursor firstCursor(editor->document());
    firstCursor.setPosition(positionInside(
        source, QStringLiteral("logic first")));
    editor->setTextCursor(firstCursor);
    editor->setFocus();
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);

    QWidget* commandModePanel =
        window.findChild<QWidget*>(
            QStringLiteral("commandLayerPanel"));
    QLabel* commandModeTitle = commandModePanel
        ? commandModePanel->findChild<QLabel*>(
              QStringLiteral("commandLayerTitle"))
        : nullptr;
    QVariantMap commandModeShortcutOverride;
    commandModeShortcutOverride.insert(
        QString::fromLatin1(
            ActionIds::ViewCommandMode),
        QStringLiteral("Ctrl+F24"));
    QStringList commandModeShortcutIssues;
    const bool commandModeOverrideAccepted =
        configureActionShortcutOverrides(
            commandModeShortcutOverride,
            &commandModeShortcutIssues);
    resetApplicationActionExecutionHistory();
    QTest::keyPress(editor, Qt::Key_F24);
    QTest::keyRelease(editor, Qt::Key_F24);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    const bool oldCommandModeShortcutInactive =
        commandModePanel
        && !commandModePanel->isVisible();
    QTest::keyPress(
        editor,
        Qt::Key_F24,
        Qt::ControlModifier);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    const bool commandModeOverrideActive =
        commandModePanel
        && commandModePanel->isVisible()
        && commandModePanel->property(
               "entryActionId").toString()
               == QString::fromLatin1(
                   ActionIds::ViewCommandMode)
        && commandModeTitle
        && commandModeTitle->text().contains(
               QStringLiteral("Ctrl+F24"));
    QTest::keyRelease(
        editor,
        Qt::Key_F24,
        Qt::ControlModifier);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    configureActionShortcutOverrides({});
    check("Command Mode hold shortcut consumes its Registry Action",
          commandModeOverrideAccepted
              && commandModeShortcutIssues.isEmpty()
              && oldCommandModeShortcutInactive
              && commandModeOverrideActive
              && !commandModePanel->isVisible()
              && applicationActionExecutionHistory()
                     .lastActionId().isEmpty());

    const int firstSymbolStart =
        editor->toPlainText().indexOf(
            QStringLiteral("first"));
    QTextCursor smartSelectionCursor(
        editor->document());
    smartSelectionCursor.setPosition(
        firstSymbolStart + 2);
    editor->setTextCursor(smartSelectionCursor);
    resetApplicationActionExecutionHistory();
    QTest::keyClick(
        editor,
        Qt::Key_W,
        Qt::ControlModifier);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    const int expandedOccurrenceStart =
        editor->textCursor().selectionStart();
    check("smart selection shortcut executes its Registry Action",
          firstSymbolStart >= 0
              && editor->textCursor().selectedText()
                     == QStringLiteral("first")
              && expandedOccurrenceStart == firstSymbolStart
              && applicationActionExecutionHistory()
                     .lastActionId()
                     == QString::fromLatin1(
                         ActionIds::SelectExpandSmart));

    QTest::keyClick(
        editor,
        Qt::Key_E,
        Qt::ControlModifier);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    const int nextOccurrenceStart =
        editor->textCursor().selectionStart();
    const bool nextOccurrenceRouted =
        editor->textCursor().selectedText()
            == QStringLiteral("first")
        && nextOccurrenceStart > expandedOccurrenceStart
        && applicationActionExecutionHistory()
               .lastActionId()
               == QString::fromLatin1(
                   ActionIds::NavigationNextSelectedSymbolOccurrence);
    QTest::keyClick(
        editor,
        Qt::Key_Q,
        Qt::ControlModifier);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    check("selected-symbol navigation shortcuts execute Registry Actions",
          nextOccurrenceRouted
              && editor->textCursor().selectedText()
                     == QStringLiteral("first")
              && editor->textCursor().selectionStart()
                     == expandedOccurrenceStart
              && applicationActionExecutionHistory()
                     .lastActionId()
                     == QString::fromLatin1(
                         ActionIds::NavigationPreviousSelectedSymbolOccurrence));

    QTextCursor commandSelectionCursor(
        editor->document());
    const int secondSymbolStart =
        editor->toPlainText().indexOf(
            QStringLiteral("second"));
    commandSelectionCursor.setPosition(
        secondSymbolStart + 2);
    editor->setTextCursor(commandSelectionCursor);
    resetApplicationActionExecutionHistory();
    QTest::keyPress(editor, Qt::Key_F24);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    QTest::keyClicks(
        editor,
        QStringLiteral("expand selection"));
    QTest::keyClick(editor, Qt::Key_Return);
    QTest::keyRelease(editor, Qt::Key_F24);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    check("Command Mode uses the structural selection Action",
          secondSymbolStart >= 0
              && editor->textCursor().selectedText()
                     == QStringLiteral("second")
              && applicationActionExecutionHistory()
                     .lastActionId()
                     == QString::fromLatin1(
                         ActionIds::SelectExpandSmart));

    QTextCursor firstCaseCursor(editor->document());
    firstCaseCursor.setPosition(firstSymbolStart);
    firstCaseCursor.setPosition(
        firstSymbolStart + QStringLiteral("first").size(),
        QTextCursor::KeepAnchor);
    editor->setTextCursor(firstCaseCursor);
    editor->setFocus();
    resetApplicationActionExecutionHistory();
    const ContextActionTriggerState contextAction =
        triggerContextMenuAction(
            editor,
            QStringLiteral("first"),
            QString::fromLatin1(
                ActionIds::EditToggleSelectionCase));
    check("context selection-case action is a routed Action Registry adapter",
          contextAction.menuShown
              && contextAction.found
              && contextAction.enabled
              && contextAction.triggered
              && contextAction.actionId
                     == QString::fromLatin1(
                         ActionIds::EditToggleSelectionCase)
              && contextAction.executionRoute
                     == QStringLiteral(
                         "editor.edit.toggleSelectionCase"));
    check("context selection-case action executes through the MainWindow host",
          editor->toPlainText().contains(
              QStringLiteral("logic FIRST;"))
              && applicationActionExecutionHistory()
                     .lastActionId()
                     == QString::fromLatin1(
                         ActionIds::EditToggleSelectionCase));

    const int secondCaseStart =
        editor->toPlainText().indexOf(QStringLiteral("second"));
    QTextCursor secondCaseCursor(editor->document());
    secondCaseCursor.setPosition(secondCaseStart);
    secondCaseCursor.setPosition(
        secondCaseStart + QStringLiteral("second").size(),
        QTextCursor::KeepAnchor);
    editor->setTextCursor(secondCaseCursor);
    editor->setFocus();
    QTest::keyPress(editor, Qt::Key_F24);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    QWidget* commandPanel = window.findChild<QWidget*>(
        QStringLiteral("commandLayerPanel"));
    const bool commandLayerEntered =
        commandPanel && commandPanel->isVisible();
    QTest::keyClicks(editor, QStringLiteral("repeat action"));
    QTest::keyClick(editor, Qt::Key_Return);
    QTest::keyRelease(editor, Qt::Key_F24);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    check("Command Mode repeats the context selection-case Action",
          commandLayerEntered
              && editor->toPlainText().contains(
                  QStringLiteral("logic SECOND;"))
              && applicationActionExecutionHistory()
                     .lastActionId()
                     == QString::fromLatin1(
                         ActionIds::EditToggleSelectionCase));

    QTextCursor thirdCursor(editor->document());
    thirdCursor.setPosition(positionInside(
        editor->toPlainText(),
        QStringLiteral("logic third")));
    editor->setTextCursor(thirdCursor);
    editor->setFocus();
    resetApplicationActionExecutionHistory();
    QTest::keyClick(
        editor,
        Qt::Key_Slash,
        Qt::ControlModifier);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    const bool shortcutCommented =
        editor->toPlainText().contains(
            QStringLiteral("// logic third;"))
        && applicationActionExecutionHistory()
               .lastActionId()
               == QStringLiteral("format.commentLines");
    QTest::keyClick(
        editor,
        Qt::Key_Slash,
        Qt::ControlModifier
            | Qt::ShiftModifier);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    check("comment shortcuts execute canonical Registry Actions",
          shortcutCommented
              && editor->toPlainText().contains(
                  QStringLiteral("logic third;"))
              && applicationActionExecutionHistory()
                     .lastActionId()
                     == QStringLiteral(
                         "format.uncommentLines"));

    QTest::keyClick(
        editor,
        Qt::Key_BracketRight,
        Qt::ControlModifier);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    const bool shortcutIndented =
        editor->toPlainText().contains(
            QStringLiteral("    logic third;"))
        && applicationActionExecutionHistory()
               .lastActionId()
               == QStringLiteral("format.indentLines");
    QTest::keyClick(
        editor,
        Qt::Key_BracketLeft,
        Qt::ControlModifier);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    check("indent shortcuts execute canonical Registry Actions",
          shortcutIndented
              && editor->toPlainText().contains(
                  QStringLiteral("logic third;"))
              && applicationActionExecutionHistory()
                     .lastActionId()
                     == QStringLiteral(
                         "format.unindentLines"));

    const int assignmentFirst =
        editor->toPlainText().indexOf(
            QStringLiteral("first"),
            editor->toPlainText().indexOf(
                QStringLiteral("assign")));
    QTextCursor occurrenceCursor(editor->document());
    occurrenceCursor.setPosition(assignmentFirst + 2);
    editor->setTextCursor(occurrenceCursor);
    const auto invokeNextOccurrence = [editor]() {
        bool handled = false;
        emit editor->registeredActionRequested(
            QStringLiteral("select.nextSymbolOccurrence"),
            {},
            &handled);
        QCoreApplication::processEvents(
            QEventLoop::AllEvents, 50);
        return handled;
    };
    resetApplicationActionExecutionHistory();
    const bool firstOccurrenceHandled = invokeNextOccurrence();
    const bool firstOccurrenceRouted =
        firstOccurrenceHandled
        && editor->textCursor().selectedText()
            == QStringLiteral("first")
        && !editor->editorModeActiveForTest(
            EditorModeId::MultiCursor)
        && applicationActionExecutionHistory()
               .lastActionId()
               == QStringLiteral(
                   "select.nextSymbolOccurrence");
    const bool secondOccurrenceHandled = invokeNextOccurrence();
    check("next-occurrence command executes the canonical occurrence Action",
          firstOccurrenceRouted
              && secondOccurrenceHandled
              && editor->editorModeActiveForTest(
                  EditorModeId::MultiCursor)
              && applicationActionExecutionHistory()
                     .lastActionId()
                     == QStringLiteral(
                         "select.nextSymbolOccurrence"));
    resetApplicationActionExecutionHistory();
    QTest::keyClick(
        editor,
        Qt::Key_C,
        Qt::ControlModifier);
    check("multi-cursor clipboard shortcut executes through Registry",
          editor->editorModeActiveForTest(
              EditorModeId::MultiCursor)
              && QApplication::clipboard()->text()
                     == QStringLiteral("first\nfirst")
              && applicationActionExecutionHistory()
                     .lastActionId()
                     == QStringLiteral("edit.copy"));
    QTest::keyClick(editor, Qt::Key_Escape);

    occurrenceCursor.clearSelection();
    occurrenceCursor.setPosition(assignmentFirst + 2);
    editor->setTextCursor(occurrenceCursor);
    const QString beforeDuplicate = editor->toPlainText();
    const int duplicateLineEnd = beforeDuplicate.indexOf(
        QLatin1Char('\n'), assignmentFirst);
    const int duplicateLineStart = beforeDuplicate.lastIndexOf(
        QLatin1Char('\n'), assignmentFirst) + 1;
    const QString duplicateLine = beforeDuplicate.mid(
        duplicateLineStart,
        duplicateLineEnd - duplicateLineStart + 1);
    resetApplicationActionExecutionHistory();
    QTest::keyClick(
        editor,
        Qt::Key_D,
        Qt::ControlModifier);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    check("Ctrl+D executes the canonical duplicate Action",
          editor->toPlainText()
                  == QString(beforeDuplicate).insert(
                      duplicateLineEnd + 1,
                      duplicateLine)
              && applicationActionExecutionHistory()
                     .lastActionId()
                     == QString::fromLatin1(
                         ActionIds::EditDuplicateLines));
    editor->undo();
    check("Ctrl+D duplicate remains one undo transaction",
          editor->toPlainText() == beforeDuplicate);

    occurrenceCursor.clearSelection();
    occurrenceCursor.setPosition(assignmentFirst + 2);
    editor->setTextCursor(occurrenceCursor);
    resetApplicationActionExecutionHistory();
    QTest::keyClick(
        editor,
        Qt::Key_L,
        Qt::ControlModifier
            | Qt::ShiftModifier);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    check("scope-occurrence shortcut executes its Registry Action",
          editor->editorModeActiveForTest(
              EditorModeId::MultiCursor)
              && applicationActionExecutionHistory()
                     .lastActionId()
                     == QStringLiteral(
                         "select.allSymbolOccurrences"));
    QTest::keyClick(editor, Qt::Key_Escape);

    QTextCursor lineCursor(editor->document());
    lineCursor.setPosition(
        editor->toPlainText().indexOf(
            QStringLiteral("logic join_b")));
    editor->setTextCursor(lineCursor);
    resetApplicationActionExecutionHistory();
    QTest::keyClick(
        editor,
        Qt::Key_Up,
        Qt::AltModifier);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    const bool moveUpRouted =
        editor->toPlainText().indexOf(
            QStringLiteral("logic join_b"))
            < editor->toPlainText().indexOf(
                QStringLiteral("logic join_a"))
        && applicationActionExecutionHistory()
               .lastActionId()
               == QString::fromLatin1(
                   ActionIds::EditMoveLinesUp);
    QTest::keyClick(
        editor,
        Qt::Key_Down,
        Qt::AltModifier);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    check("line-move shortcuts execute canonical Registry Actions",
          moveUpRouted
              && editor->toPlainText().indexOf(
                     QStringLiteral("logic join_a"))
                     < editor->toPlainText().indexOf(
                         QStringLiteral("logic join_b"))
              && applicationActionExecutionHistory()
                     .lastActionId()
                     == QString::fromLatin1(
                         ActionIds::EditMoveLinesDown));

    QTest::keyPress(editor, Qt::Key_F24);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    QTest::keyClicks(
        editor,
        QStringLiteral("move lines up"));
    QTest::keyClick(editor, Qt::Key_Return);
    QTest::keyRelease(editor, Qt::Key_F24);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    const bool commandMoveUp =
        editor->toPlainText().indexOf(
            QStringLiteral("logic join_b"))
            < editor->toPlainText().indexOf(
                QStringLiteral("logic join_a"))
        && applicationActionExecutionHistory()
               .lastActionId()
               == QString::fromLatin1(
                   ActionIds::EditMoveLinesUp);
    QTest::keyPress(editor, Qt::Key_F24);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    QTest::keyClicks(
        editor,
        QStringLiteral("move lines down"));
    QTest::keyClick(editor, Qt::Key_Return);
    QTest::keyRelease(editor, Qt::Key_F24);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    check("Command Mode uses line-move Actions",
          commandMoveUp
              && editor->toPlainText().indexOf(
                     QStringLiteral("logic join_a"))
                     < editor->toPlainText().indexOf(
                         QStringLiteral("logic join_b"))
              && applicationActionExecutionHistory()
                     .lastActionId()
                     == QString::fromLatin1(
                         ActionIds::EditMoveLinesDown));

    lineCursor.setPosition(
        editor->toPlainText().indexOf(
            QStringLiteral("logic join_a")));
    editor->setTextCursor(lineCursor);
    resetApplicationActionExecutionHistory();
    QTest::keyClick(
        editor,
        Qt::Key_J,
        Qt::ControlModifier | Qt::ShiftModifier);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    const bool joinLinesRouted =
        editor->toPlainText().contains(
            QStringLiteral("logic join_a; logic join_b;"))
        && applicationActionExecutionHistory()
               .lastActionId()
               == QStringLiteral("edit.joinLines");
    lineCursor.clearSelection();
    lineCursor.setPosition(
        editor->toPlainText().indexOf(
            QStringLiteral("logic delete_me")));
    editor->setTextCursor(lineCursor);
    resetApplicationActionExecutionHistory();
    QTest::keyClick(
        editor,
        Qt::Key_D,
        Qt::ControlModifier | Qt::ShiftModifier);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    check("line-operation shortcuts execute canonical Registry Actions",
          joinLinesRouted
              && !editor->toPlainText().contains(
                  QStringLiteral("logic delete_me"))
              && applicationActionExecutionHistory()
                     .lastActionId()
                     == QStringLiteral("edit.deleteLines"));

    const int secondAssignment =
        editor->toPlainText().indexOf(
            QStringLiteral("assign"),
            editor->toPlainText().indexOf(
                QStringLiteral("assign")) + 1);
    const int secondAssignmentFirst =
        editor->toPlainText().indexOf(
            QStringLiteral("first"),
            secondAssignment);
    QTextCursor columnStart(editor->document());
    columnStart.setPosition(assignmentFirst);
    QTextCursor columnEnd(editor->document());
    columnEnd.setPosition(
        secondAssignmentFirst
        + QStringLiteral("first").size());
    editor->setTextCursor(columnStart);
    QTest::mouseClick(
        editor->viewport(),
        Qt::LeftButton,
        Qt::ShiftModifier | Qt::AltModifier,
        editor->cursorRect(columnEnd).center());
    const QStringList selectedColumnRows =
        editor->columnSelectionTexts();
    resetApplicationActionExecutionHistory();
    QTest::keyClick(
        editor,
        Qt::Key_C,
        Qt::ControlModifier);
    check("column clipboard shortcut executes through Registry",
          editor->columnSelectionActive()
              && selectedColumnRows.size() == 2
              && QApplication::clipboard()->text()
                     == selectedColumnRows.join(
                         QLatin1Char('\n'))
              && applicationActionExecutionHistory()
                     .lastActionId()
                     == QStringLiteral("edit.copy"));

    QWidget* columnNumberToolPanel =
        window.findChild<QWidget*>(
            QStringLiteral("columnNumberToolPanel"));
    QVariantMap columnNumberShortcutOverride;
    columnNumberShortcutOverride.insert(
        QString::fromLatin1(
            ActionIds::InsertColumnNumbers),
        QStringLiteral("Ctrl+Alt+C"));
    QStringList columnNumberShortcutIssues;
    const bool columnNumberOverrideAccepted =
        configureActionShortcutOverrides(
            columnNumberShortcutOverride,
            &columnNumberShortcutIssues);
    resetApplicationActionExecutionHistory();
    QTest::keyClick(
        editor,
        Qt::Key_C,
        Qt::AltModifier);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    const bool oldColumnNumberShortcutInactive =
        columnNumberToolPanel
        && !columnNumberToolPanel->isVisible()
        && applicationActionExecutionHistory()
               .lastActionId().isEmpty();
    QTest::keyClick(
        editor,
        Qt::Key_C,
        Qt::ControlModifier | Qt::AltModifier);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    configureActionShortcutOverrides({});
    check("Column Number Tool shortcut executes its Registry route",
          columnNumberOverrideAccepted
              && columnNumberShortcutIssues.isEmpty()
              && oldColumnNumberShortcutInactive
              && columnNumberToolPanel
              && columnNumberToolPanel->isVisible()
              && columnNumberToolPanel->property(
                     "entryActionId").toString()
                     == QString::fromLatin1(
                         ActionIds::InsertColumnNumbers)
              && applicationActionExecutionHistory()
                     .lastActionId().isEmpty());
    if (columnNumberToolPanel)
        columnNumberToolPanel->hide();
    editor->setFocus();
    QTest::keyClick(editor, Qt::Key_Escape);

    const int guardedPosition =
        editor->toPlainText().indexOf(
            QStringLiteral("guarded"));
    QTextCursor standardCursor(editor->document());
    standardCursor.setPosition(guardedPosition);
    standardCursor.setPosition(
        guardedPosition + QStringLiteral("guarded").size(),
        QTextCursor::KeepAnchor);
    editor->setTextCursor(standardCursor);
    editor->setFocus();
    resetApplicationActionExecutionHistory();
    const ContextActionTriggerState contextCopy =
        triggerContextMenuAction(
            editor,
            QStringLiteral("guarded"),
            QStringLiteral("edit.copy"));
    check("hidden standard context action stays hidden",
          contextCopy.menuShown
              && !contextCopy.found
              && !contextCopy.triggered
              && applicationActionExecutionHistory()
                     .lastActionId().isEmpty());

    standardCursor.setPosition(guardedPosition);
    standardCursor.setPosition(
        guardedPosition + QStringLiteral("guarded").size(),
        QTextCursor::KeepAnchor);
    editor->setTextCursor(standardCursor);
    resetApplicationActionExecutionHistory();
    QTest::keyClick(
        editor,
        Qt::Key_C,
        Qt::ControlModifier);
    const bool copyShortcutRouted =
        QApplication::clipboard()->text()
            == QStringLiteral("guarded")
        && applicationActionExecutionHistory()
               .lastActionId()
               == QStringLiteral("edit.copy");
    QTest::keyClick(
        editor,
        Qt::Key_X,
        Qt::ControlModifier);
    const bool cutShortcutRouted =
        !editor->toPlainText().contains(
            QStringLiteral("logic guarded;"))
        && applicationActionExecutionHistory()
               .lastActionId()
               == QStringLiteral("edit.cut");
    QTest::keyClick(
        editor,
        Qt::Key_V,
        Qt::ControlModifier);
    const bool pasteShortcutRouted =
        editor->toPlainText().contains(
            QStringLiteral("logic guarded;"))
        && applicationActionExecutionHistory()
               .lastActionId()
               == QStringLiteral("edit.paste");
    QTest::keyClick(
        editor,
        Qt::Key_Z,
        Qt::ControlModifier);
    const bool undoShortcutRouted =
        !editor->toPlainText().contains(
            QStringLiteral("logic guarded;"))
        && applicationActionExecutionHistory()
               .lastActionId()
               == QStringLiteral("edit.undo");
    QTest::keyClick(
        editor,
        Qt::Key_Y,
        Qt::ControlModifier);
    const bool redoShortcutRouted =
        editor->toPlainText().contains(
            QStringLiteral("logic guarded;"))
        && applicationActionExecutionHistory()
               .lastActionId()
               == QStringLiteral("edit.redo");
    QTest::keyClick(
        editor,
        Qt::Key_A,
        Qt::ControlModifier);
    const bool selectAllShortcutRouted =
        editor->textCursor().selectionStart() == 0
        && editor->textCursor().selectionEnd()
               == editor->toPlainText().size()
        && applicationActionExecutionHistory()
               .lastActionId()
               == QStringLiteral("select.all");
    check("standard edit shortcuts execute canonical Registry Actions",
          copyShortcutRouted
              && cutShortcutRouted
              && pasteShortcutRouted
              && undoShortcutRouted
              && redoShortcutRouted
              && selectAllShortcutRouted);

    standardCursor.clearSelection();
    standardCursor.setPosition(0);
    editor->setTextCursor(standardCursor);
    editor->setFocus();
    const int endmodulePosition =
        editor->toPlainText().indexOf(
            QStringLiteral("endmodule"));
    const int goLineTarget =
        editor->document()
            ->findBlock(endmodulePosition)
            .blockNumber() + 1;
    QTimer::singleShot(
        0,
        editor,
        [goLineTarget]() {
            auto* input = qobject_cast<QInputDialog*>(
                QApplication::activeModalWidget());
            if (!input)
                return;
            input->setIntValue(goLineTarget);
            input->accept();
        });
    resetApplicationActionExecutionHistory();
    QTest::keyClick(
        editor,
        Qt::Key_G,
        Qt::ControlModifier);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    check("Go to Line shortcut executes its Registry route",
          editor->textCursor().blockNumber() + 1
                  == goLineTarget
              && applicationActionExecutionHistory()
                     .lastActionId()
                     == QStringLiteral("navigation.goLine"));

    const auto visibleSearchBar = [editor](bool replace) -> QWidget* {
        auto* bar = editor->findChild<QWidget*>(QStringLiteral("editorFindBar"));
        if (!bar || !bar->isVisible() || bar->isWindow()) return nullptr;
        bool hasVisibleReplaceControl = false;
        for (const auto* child : bar->findChildren<QWidget*>()) {
            if (child->property("replaceControl").toBool() && child->isVisible())
                hasVisibleReplaceControl = true;
        }
        return hasVisibleReplaceControl == replace ? bar : nullptr;
    };
    editor->setFocus();
    resetApplicationActionExecutionHistory();
    QTest::keyClick(
        editor,
        Qt::Key_F,
        Qt::ControlModifier);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    QWidget* findDialog = visibleSearchBar(false);
    const bool findShortcutRouted = findDialog
        && applicationActionExecutionHistory()
               .lastActionId()
               == QStringLiteral("edit.find");
    if (findDialog)
        findDialog->close();
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    editor->setFocus();
    resetApplicationActionExecutionHistory();
    QTest::keyClick(
        editor,
        Qt::Key_H,
        Qt::ControlModifier);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    QWidget* replaceDialog = visibleSearchBar(true);
    check("Find and Replace shortcuts execute canonical Registry Actions",
          findShortcutRouted
              && replaceDialog
              && applicationActionExecutionHistory()
                     .lastActionId()
                     == QStringLiteral("edit.replace"));
    if (replaceDialog)
        replaceDialog->close();
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);

    MyCodeEditor overriddenShortcutEditor;
    overriddenShortcutEditor.setPlainText(
        QStringLiteral(
            "module override_shortcuts;\n"
            "logic renamed_signal;\n"
            "endmodule\n"));
    QTextCursor overriddenCursor(
        overriddenShortcutEditor.document());
    overriddenCursor.setPosition(
        overriddenShortcutEditor.toPlainText().indexOf(
            QStringLiteral("renamed_signal")) + 2);
    overriddenShortcutEditor.setTextCursor(overriddenCursor);
    bool definitionShortcutRequested = false;
    bool renameShortcutRequested = false;
    QObject::connect(
        &overriddenShortcutEditor,
        &MyCodeEditor::sourceSymbolActionRequested,
        &overriddenShortcutEditor,
        [&definitionShortcutRequested](
            SourceSymbolAction action,
            const EditorSemanticContext&) {
            definitionShortcutRequested =
                action == SourceSymbolAction::GoToDefinition;
        });
    QObject::connect(
        &overriddenShortcutEditor,
        &MyCodeEditor::editorStatusMessageRequested,
        &overriddenShortcutEditor,
        [&renameShortcutRequested](const QString& message) {
            if (!message.isEmpty())
                renameShortcutRequested = true;
        });
    QVariantMap semanticShortcutOverrides;
    semanticShortcutOverrides.insert(
        QStringLiteral("source.goToDefinition"),
        QStringLiteral("Ctrl+F12"));
    semanticShortcutOverrides.insert(
        QString::fromLatin1(ActionIds::RtlRename),
        QStringLiteral("Ctrl+Alt+R"));
    QStringList semanticShortcutIssues;
    const bool semanticOverridesAccepted =
        configureActionShortcutOverrides(
            semanticShortcutOverrides,
            &semanticShortcutIssues);
    QTest::keyClick(
        &overriddenShortcutEditor,
        Qt::Key_F12);
    QTest::keyClick(
        &overriddenShortcutEditor,
        Qt::Key_R,
        Qt::ControlModifier);
    const bool oldRenameShortcutInactive =
        !renameShortcutRequested;
    const bool oldSemanticShortcutsInactive =
        !definitionShortcutRequested
        && oldRenameShortcutInactive;
    QTest::keyClick(
        &overriddenShortcutEditor,
        Qt::Key_F12,
        Qt::ControlModifier);
    QTest::keyClick(
        &overriddenShortcutEditor,
        Qt::Key_R,
        Qt::ControlModifier | Qt::AltModifier);
    configureActionShortcutOverrides({});
    check("semantic shortcuts consume Registry overrides",
          semanticOverridesAccepted
              && semanticShortcutIssues.isEmpty()
              && oldSemanticShortcutsInactive
              && definitionShortcutRequested
              && renameShortcutRequested);

    editor->setFocus();
    const QString beforeShortcutFormat =
        editor->toPlainText();
    resetApplicationActionExecutionHistory();
    QTest::keyClick(
        editor,
        Qt::Key_I,
        Qt::ControlModifier | Qt::ShiftModifier);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    check("Format Document shortcut executes its Registry route",
          editor->toPlainText() != beforeShortcutFormat
              && editor->toPlainText().contains(
                  QStringLiteral("format_me"))
              && applicationActionExecutionHistory()
                     .lastActionId()
                     == QStringLiteral("format.document"));

    const QString slotFile = temp.filePath(
        QStringLiteral("context_slot_execution.sv"));
    const QString slotSource = QStringLiteral(
        "module context_slot_execution;\n"
        "  child u_first(.a(foo));\n"
        "  child u_second(.a(bar));\n"
        "endmodule\n");
    QFile slotFixture(slotFile);
    const bool slotWritten =
        slotFixture.open(
            QIODevice::WriteOnly | QIODevice::Text)
        && slotFixture.write(slotSource.toUtf8())
               == slotSource.toUtf8().size();
    slotFixture.close();
    check("context slot execution fixture is written",
          slotWritten);
    const bool slotOpened = slotWritten
        && window.tabManager->openFileInTab(slotFile);
    editor = window.tabManager->getCurrentEditor();
    check("context slot execution fixture opens a real editor",
          slotOpened && editor);
    if (!editor)
        return;

    QTextCursor oldSlotCursor(editor->document());
    oldSlotCursor.setPosition(0);
    editor->setTextCursor(oldSlotCursor);
    resetApplicationActionExecutionHistory();
    const ContextActionTriggerState slotAction =
        triggerContextMenuAction(
            editor,
            QStringLiteral("u_first"),
            QStringLiteral("refactor.editInstanceSlots"));
    check("context Slot action routes the right-click position",
          slotAction.menuShown
              && slotAction.found
              && slotAction.enabled
              && slotAction.triggered
              && slotAction.actionId
                     == QStringLiteral(
                         "refactor.editInstanceSlots")
              && slotAction.executionRoute
                     == QStringLiteral(
                         "editor.structure.editInstanceSlots")
              && editor->templateSlotModeActive()
              && editor->textCursor().selectedText()
                     == QStringLiteral("foo")
              && applicationActionExecutionHistory()
                     .lastActionId()
                     == QStringLiteral(
                         "refactor.editInstanceSlots"));

    QTest::keyClick(editor, Qt::Key_Escape);
    QTextCursor secondSlotCursor(editor->document());
    secondSlotCursor.setPosition(positionInside(
        editor->toPlainText(),
        QStringLiteral("u_second")));
    editor->setTextCursor(secondSlotCursor);
    editor->setFocus();
    QTest::keyPress(editor, Qt::Key_F24);
    QTest::keyClicks(editor, QStringLiteral("repeat action"));
    QTest::keyClick(editor, Qt::Key_Return);
    QTest::keyRelease(editor, Qt::Key_F24);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    check("repeated Slot action resolves the current cursor",
          editor->templateSlotModeActive()
              && editor->textCursor().selectedText()
                     == QStringLiteral("bar"));
    QTest::keyClick(editor, Qt::Key_Escape);

    const QString insightFile = temp.filePath(
        QStringLiteral("context_insight_execution.sv"));
    const QString insightSource = QStringLiteral(
        "module context_insight_execution;\n"
        "  logic payload;\n"
        "  assign payload = 1'b0;\n"
        "endmodule\n");
    QFile insightFixture(insightFile);
    const bool insightWritten =
        insightFixture.open(
            QIODevice::WriteOnly | QIODevice::Text)
        && insightFixture.write(insightSource.toUtf8())
               == insightSource.toUtf8().size();
    insightFixture.close();
    check("context Insight execution fixture is written",
          insightWritten);
    const bool insightOpened = insightWritten
        && window.tabManager->openFileInTab(insightFile);
    editor = window.tabManager->getCurrentEditor();
    check("context Insight execution fixture opens a real editor",
          insightOpened && editor);
    if (!editor)
        return;

    SemanticSymbolRecord payload =
        SemanticFixtureRecordBuilder(
            QStringLiteral("payload"),
            SymbolTaxonomy::DeclarationKind::Signal)
            .withFile(insightFile)
            .withLocalHandle(401)
            .withLine(2, 9)
            .withTextSpan(
                insightSource.indexOf(
                    QStringLiteral("payload")),
                7)
            .withCollectorKind(
                SymbolTaxonomy::CollectorKind::Logic)
            .inModule(
                QStringLiteral(
                    "context_insight_execution"))
            .record();
    SemanticIndex::getInstance()->setSnapshot(
        snapshot({payload},
                 {{insightFile, insightSource}}));
    QTextCursor oldInsightCursor(editor->document());
    oldInsightCursor.setPosition(0);
    editor->setTextCursor(oldInsightCursor);
    resetApplicationActionExecutionHistory();
    const ContextActionTriggerState insightAction =
        triggerContextMenuAction(
            editor,
            QStringLiteral("payload"),
            QStringLiteral("insight.signalKernelGraph"));
    QDockWidget* contextDock =
        window.findChild<QDockWidget*>(
            QStringLiteral("contextWorkspaceDock"));
    LiveInsightToolPage* kernelPage =
        dynamic_cast<LiveInsightToolPage*>(
            window.findChild<QWidget*>(
                QStringLiteral("liveInsightToolPage.kernel")));
    QDockWidget* legacyKernelDock =
        window.findChild<QDockWidget*>(
            QStringLiteral("signalKernelGraphDock"));
    check("context Insight executes through Registry into Live Insights",
          insightAction.menuShown
              && insightAction.found
              && insightAction.enabled
              && insightAction.triggered
              && insightAction.actionId
                     == QStringLiteral(
                         "insight.signalKernelGraph")
              && insightAction.executionRoute
                     == QStringLiteral(
                         "insight.signalKernel.showSymbol")
              && applicationActionExecutionHistory()
                     .lastActionId()
                     == QStringLiteral(
                         "insight.signalKernelGraph")
              && contextDock
              && contextDock->isVisible()
              && kernelPage
              && kernelPage->kind()
                     == LiveInsightKind::Kernel
              && legacyKernelDock == nullptr);

    const QString queueFile = temp.filePath(
        QStringLiteral("context_queue_execution.sv"));
    const QString queueSource = QStringLiteral(
        "module context_queue_execution;\n"
        "  logic sig0;\n"
        "  logic sig1;\n"
        "  always_comb begin\n"
        "    // queue_here\n"
        "  end\n"
        "endmodule\n");
    QFile queueFixture(queueFile);
    const bool queueWritten =
        queueFixture.open(
            QIODevice::WriteOnly | QIODevice::Text)
        && queueFixture.write(queueSource.toUtf8())
               == queueSource.toUtf8().size();
    queueFixture.close();
    check("context assignment queue fixture is written",
          queueWritten);
    const bool queueOpened = queueWritten
        && window.tabManager->openFileInTab(queueFile);
    editor = window.tabManager->getCurrentEditor();
    check("context assignment queue fixture opens a real editor",
          queueOpened && editor);
    if (!editor)
        return;

    SemanticSymbolRecord sig0 =
        SemanticFixtureRecordBuilder(
            QStringLiteral("sig0"),
            SymbolTaxonomy::DeclarationKind::Signal)
            .withFile(queueFile)
            .withLocalHandle(501)
            .withLine(2, 9)
            .withTextSpan(
                queueSource.indexOf(
                    QStringLiteral("sig0")),
                4)
            .withCollectorKind(
                SymbolTaxonomy::CollectorKind::Logic)
            .inModule(
                QStringLiteral(
                    "context_queue_execution"))
            .record();
    SemanticSymbolRecord sig1 =
        SemanticFixtureRecordBuilder(
            QStringLiteral("sig1"),
            SymbolTaxonomy::DeclarationKind::Signal)
            .withFile(queueFile)
            .withLocalHandle(502)
            .withLine(3, 9)
            .withTextSpan(
                queueSource.indexOf(
                    QStringLiteral("sig1")),
                4)
            .withCollectorKind(
                SymbolTaxonomy::CollectorKind::Logic)
            .inModule(
                QStringLiteral(
                    "context_queue_execution"))
            .record();
    SemanticIndex::getInstance()->setSnapshot(
        snapshot({sig0, sig1},
                 {{queueFile, queueSource}}));
    QString selectionReason;
    const bool selectionReady =
        editor->startSignalSelectionMode(
            &selectionReason)
        && editor->toggleSignalSelectionAtForTest(
            queueSource.indexOf(
                QStringLiteral("sig1")))
        && editor->toggleSignalSelectionAtForTest(
            queueSource.indexOf(
                QStringLiteral("sig0")));
    check("assignment queue selects semantic signals",
          selectionReady
              && editor->selectedSignalNames()
                     == QStringList{
                         QStringLiteral("sig0"),
                         QStringLiteral("sig1")});
    resetApplicationActionExecutionHistory();
    const ContextActionTriggerState queueAction =
        triggerEditorOwnedContextAction(
            editor,
            QStringLiteral("// queue_here"),
            QStringLiteral(
                "refactor.createAssignmentQueue"));
    check("assignment queue context action uses Registry host",
          queueAction.menuShown
              && queueAction.found
              && queueAction.enabled
              && queueAction.triggered
              && queueAction.actionId
                     == QStringLiteral(
                         "refactor.createAssignmentQueue")
              && queueAction.executionRoute
                     == QStringLiteral(
                         "editor.structure.createAssignmentQueue")
              && applicationActionExecutionHistory()
                     .lastActionId()
                     == QStringLiteral(
                         "refactor.createAssignmentQueue")
              && editor->toPlainText().contains(
                  QStringLiteral(
                      "    sig0 <= ;\n"
                      "    sig1 <= ;\n"
                      "    // queue_here"))
              && editor->templateSlotModeActive());
    QTest::keyClick(editor, Qt::Key_Escape);

    resetApplicationActionExecutionHistory();
    window.hide();
}

void runEditorViewTargetedActionRegression()
{
    MainWindow window;
    window.resize(900, 600);
    window.show();
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);

    QTemporaryDir temp;
    check("editor-view Action fixture root is available",
          temp.isValid());
    if (!temp.isValid())
        return;

    const QString targetFile =
        temp.filePath(QStringLiteral("target_view.sv"));
    const QString currentFile =
        temp.filePath(QStringLiteral("current_view.sv"));
    const QString targetSource = QStringLiteral(
        "module target_view;\n"
        "logic sig;\n"
        "logic delete_me;\n"
        "assign sig = sig;\n"
        "assign sig = sig + 1;\n"
        "endmodule\n");
    const QString currentSource = QStringLiteral(
        "module current_view;\n"
        "logic other;\n"
        "assign other = other;\n"
        "endmodule\n");
    const auto writeFixture = [](const QString& fileName,
                                 const QString& text) {
        QFile file(fileName);
        const QByteArray bytes = text.toUtf8();
        const bool written =
            file.open(QIODevice::WriteOnly | QIODevice::Text)
            && file.write(bytes) == bytes.size();
        file.close();
        return written;
    };
    const bool fixturesWritten =
        writeFixture(targetFile, targetSource)
        && writeFixture(currentFile, currentSource);
    check("editor-view Action fixtures are written",
          fixturesWritten);
    if (!fixturesWritten)
        return;

    const bool targetOpened =
        window.tabManager->openFileInTab(targetFile);
    MyCodeEditor* targetTab =
        window.tabManager->getCurrentEditor();
    const DocumentSnapshot targetSnapshot =
        window.tabManager->getDocumentForEditor(targetTab);
    const bool currentOpened =
        window.tabManager->openFileInTab(currentFile);
    MyCodeEditor* currentEditor =
        window.tabManager->getCurrentEditor();
    check("two editor-view Action fixture documents open",
          targetOpened && targetTab
              && !targetSnapshot.documentId.isEmpty()
              && currentOpened && currentEditor
              && currentEditor != targetTab);
    if (!targetTab || !currentEditor
        || targetSnapshot.documentId.isEmpty()) {
        return;
    }

    QWidget auxiliaryHost;
    auxiliaryHost.resize(720, 360);
    MyCodeEditor* auxiliary =
        window.tabManager->createAuxiliaryView(
            targetSnapshot.documentId,
            targetFile,
            &auxiliaryHost);
    if (auxiliary) {
        auxiliary->setGeometry(auxiliaryHost.rect());
        auxiliary->show();
    }
    auxiliaryHost.show();
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    check("auxiliary source View shares only the target document",
          auxiliary
              && auxiliary->document() == targetTab->document()
              && auxiliary->document()
                     != currentEditor->document());
    if (!auxiliary)
        return;

    QTextCursor currentCursor(currentEditor->document());
    currentCursor.setPosition(
        currentSource.indexOf(QStringLiteral("other")) + 1);
    currentEditor->setTextCursor(currentCursor);
    const int currentCursorBefore =
        currentEditor->textCursor().position();
    QTextCursor occurrenceCursor(auxiliary->document());
    const int targetAssignment = targetSource.indexOf(
        QStringLiteral("assign sig"));
    occurrenceCursor.setPosition(
        targetAssignment
        + QStringLiteral("assign ").size() + 1);
    auxiliary->setTextCursor(occurrenceCursor);
    auxiliary->setFocus();
    const auto invokeAuxiliaryNextOccurrence = [auxiliary]() {
        bool handled = false;
        emit auxiliary->registeredActionRequested(
            QStringLiteral("select.nextSymbolOccurrence"),
            {},
            &handled);
        QCoreApplication::processEvents(
            QEventLoop::AllEvents, 50);
        return handled;
    };
    resetApplicationActionExecutionHistory();
    const bool firstAuxiliaryOccurrenceHandled =
        invokeAuxiliaryNextOccurrence();
    const bool firstOccurrenceTargeted =
        firstAuxiliaryOccurrenceHandled
        && auxiliary->textCursor().selectedText()
            == QStringLiteral("sig")
        && !auxiliary->editorModeActiveForTest(
            EditorModeId::MultiCursor)
        && !currentEditor->textCursor().hasSelection()
        && currentEditor->textCursor().position()
               == currentCursorBefore
        && applicationActionExecutionHistory()
               .lastActionId()
               == QStringLiteral(
                   "select.nextSymbolOccurrence");
    const bool secondAuxiliaryOccurrenceHandled =
        invokeAuxiliaryNextOccurrence();
    check("next-occurrence command stays on the source View",
          firstOccurrenceTargeted
              && secondAuxiliaryOccurrenceHandled
              && auxiliary->editorModeActiveForTest(
                  EditorModeId::MultiCursor)
              && !currentEditor->editorModeActiveForTest(
                  EditorModeId::MultiCursor));
    QTest::keyClick(auxiliary, Qt::Key_Escape);

    occurrenceCursor.clearSelection();
    occurrenceCursor.setPosition(
        targetAssignment + QStringLiteral("assign ").size() + 1);
    auxiliary->setTextCursor(occurrenceCursor);
    const QString auxiliaryBeforeDuplicate =
        auxiliary->toPlainText();
    const int auxiliaryLineEnd = auxiliaryBeforeDuplicate.indexOf(
        QLatin1Char('\n'), targetAssignment);
    const int auxiliaryLineStart = auxiliaryBeforeDuplicate.lastIndexOf(
        QLatin1Char('\n'), targetAssignment) + 1;
    const QString auxiliaryLine = auxiliaryBeforeDuplicate.mid(
        auxiliaryLineStart,
        auxiliaryLineEnd - auxiliaryLineStart + 1);
    resetApplicationActionExecutionHistory();
    QTest::keyClick(auxiliary,
                    Qt::Key_D,
                    Qt::ControlModifier);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    check("Ctrl+D duplicates only the focused source View",
          auxiliary->toPlainText()
                  == QString(auxiliaryBeforeDuplicate).insert(
                      auxiliaryLineEnd + 1,
                      auxiliaryLine)
              && currentEditor->textCursor().position()
                     == currentCursorBefore
              && applicationActionExecutionHistory()
                     .lastActionId()
                     == QString::fromLatin1(
                         ActionIds::EditDuplicateLines));
    auxiliary->undo();
    check("source-View duplicate is one undo transaction",
          auxiliary->toPlainText()
                  == auxiliaryBeforeDuplicate);

    QTextCursor deleteCursor(auxiliary->document());
    deleteCursor.setPosition(
        auxiliary->toPlainText().indexOf(
            QStringLiteral("delete_me")) + 2);
    auxiliary->setTextCursor(deleteCursor);
    const QString currentBeforeDelete =
        currentEditor->toPlainText();
    resetApplicationActionExecutionHistory();
    QTest::keyClick(
        auxiliary,
        Qt::Key_D,
        Qt::ControlModifier | Qt::ShiftModifier);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    check("Ctrl+Shift+D deletes source-View lines without conflicting with Ctrl+D",
          !auxiliary->toPlainText().contains(
              QStringLiteral("delete_me"))
              && currentEditor->toPlainText()
                     == currentBeforeDelete
              && applicationActionExecutionHistory()
                     .lastActionId()
                     == QStringLiteral("edit.deleteLines"));

    const auto requestAction =
        [auxiliary](const QString& actionId) {
            resetApplicationActionExecutionHistory();
            bool handled = false;
            emit auxiliary->registeredActionRequested(
                actionId, {}, &handled);
            QCoreApplication::processEvents(
                QEventLoop::AllEvents, 50);
            return handled;
        };
    const QString formatInput = QStringLiteral(
        "module format_target;\n"
        "\tlogic    alpha;\n"
        "\tlogic      beta;\n"
        "\tassign alpha=beta;\n"
        "endmodule\n");
    auxiliary->setPlainText(formatInput);
    auxiliary->setPlainText(formatInput);
    QTextCursor documentCursor(auxiliary->document());
    documentCursor.setPosition(
        formatInput.indexOf(QStringLiteral("beta")) + 2);
    auxiliary->setTextCursor(documentCursor);
    QTextCursor peerDocumentCursor(targetTab->document());
    peerDocumentCursor.setPosition(
        formatInput.indexOf(QStringLiteral("alpha")) + 2);
    targetTab->setTextCursor(peerDocumentCursor);
    const FormatterReport documentReport =
        FormatterService::getInstance()->formatDocument(
            formatInput);
    const QString currentBeforeFormat =
        currentEditor->toPlainText();
    auxiliary->setFocus();
    resetApplicationActionExecutionHistory();
    QTest::keyClick(
        auxiliary,
        Qt::Key_I,
        Qt::ControlModifier | Qt::ShiftModifier);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    QTextCursor wordCursor = auxiliary->textCursor();
    wordCursor.select(QTextCursor::WordUnderCursor);
    QTextCursor peerWordCursor = targetTab->textCursor();
    peerWordCursor.select(QTextCursor::WordUnderCursor);
    const bool documentRouted =
        documentReport.changed
        && auxiliary->toPlainText()
               == documentReport.formattedText
        && currentEditor->toPlainText()
               == currentBeforeFormat
        && wordCursor.selectedText()
               == QStringLiteral("beta")
        && peerWordCursor.selectedText()
               == QStringLiteral("alpha")
        && applicationActionExecutionHistory()
               .lastActionId()
               == QStringLiteral("format.document");
    auxiliary->undo();
    const bool documentUndo =
        auxiliary->toPlainText() == formatInput;
    auxiliary->redo();
    const bool documentRedo =
        auxiliary->toPlainText()
            == documentReport.formattedText;
    resetApplicationActionExecutionHistory();
    QTest::keyClick(
        auxiliary,
        Qt::Key_I,
        Qt::ControlModifier | Qt::ShiftModifier);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 50);
    check("Format Document is deterministic, preserves its logical cursor, and reports idempotence",
          documentRouted && documentUndo && documentRedo
              && auxiliary->toPlainText()
                     == documentReport.formattedText
              && applicationActionExecutionHistory()
                     .lastActionId()
                     == QStringLiteral("format.document")
              && !ActivityLogService::getInstance()->events().isEmpty()
              && ActivityLogService::getInstance()->events().last().message
                     .contains(QStringLiteral("already formatted"),
                               Qt::CaseInsensitive));

    bool transitionStressOk = true;
    for (int iteration = 0; iteration < 12; ++iteration) {
        auxiliary->setPlainText(formatInput);
        QTextCursor cursor(auxiliary->document());
        cursor.setPosition(
            formatInput.indexOf(QStringLiteral("alpha")) + 2);
        auxiliary->setTextCursor(cursor);
        const FormatterReport expected =
            FormatterService::getInstance()
                ->formatDocument(formatInput);
        const bool formatRouted =
            requestAction(QStringLiteral("format.document"));
        const bool formatApplied =
            expected.changed
            && formatRouted
            && auxiliary->toPlainText()
                   == expected.formattedText;
        auxiliary->undo();
        const bool undoRestored =
            auxiliary->toPlainText() == formatInput;
        auxiliary->redo();
        const bool redoRestored =
            auxiliary->toPlainText()
                == expected.formattedText;
        transitionStressOk = transitionStressOk
            && formatApplied
            && undoRestored
            && redoRestored;
    }
    check("format document, undo/redo, and multi-View transitions remain deterministic",
          transitionStressOk);
}

void runCommandLayerCompletionPopupRegression()
{
    MainWindow window;
    window.resize(900, 600);
    window.show();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    const QString source = QStringLiteral(
        ";;p -\n"
        "module command_layer_completion;\n"
        "endmodule\n");
    QTemporaryDir temp;
    check("command layer completion fixture root is available",
          temp.isValid());
    if (!temp.isValid())
        return;
    const QString fileName =
        temp.filePath(QStringLiteral("command_layer_completion.sv"));
    QFile fixture(fileName);
    const bool written =
        fixture.open(QIODevice::WriteOnly | QIODevice::Text)
        && fixture.write(source.toUtf8()) == source.toUtf8().size();
    fixture.close();
    check("command layer completion fixture source is written", written);
    const bool opened = written
        && window.tabManager->openFileInTab(fileName);
    check("command layer completion fixture opens a real editor", opened);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    MyCodeEditor* editor = window.tabManager->getCurrentEditor();
    QWidget* commandPanel = window.findChild<QWidget*>(
        QStringLiteral("commandLayerPanel"));
    check("command layer completion fixture has an editor and panel",
          editor && commandPanel);
    if (!editor || !commandPanel)
        return;

    QTextCursor cursor(editor->document());
    cursor.setPosition(QStringLiteral(";;p -").size());
    editor->setTextCursor(cursor);
    editor->setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    QTest::keyClick(editor, Qt::Key_Tab);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QCompleter* completer = editor->findChild<QCompleter*>();
    QAbstractItemView* completionPopup =
        completer ? completer->popup() : nullptr;
    check("legacy inline completion remains inactive before COM",
          !completionPopup || !completionPopup->isVisible());
    const QString sourceAfterTab = editor->toPlainText();

    QTest::keyPress(editor, Qt::Key_F24);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    check("COM opens without a legacy editor completion popup",
          (!completionPopup || !completionPopup->isVisible())
              && commandPanel->isVisible());
    check("COM activation preserves source text",
          editor->toPlainText() == sourceAfterTab);

    QTest::keyRelease(editor, Qt::Key_F24);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    check("releasing F24 closes the command layer after popup replacement",
          !commandPanel->isVisible());
    window.hide();
}


void runPeekRegression()
{
    QString untouched = QStringLiteral("module leaf; endmodule");
    QWidget host;
    host.resize(920, 700);
    host.show();
    QApplication::processEvents();

    ExposeSignalToTopPreview cancelPreview(
        readyDialogReport(QStringLiteral("payload_out")),
        [](const QString& name) {
            ExposeSignalToTopReport report =
                readyDialogReport(name);
            report.renderedDiff =
                QStringLiteral("+ output logic %1").arg(name);
            return report;
        },
        &host);

    bool embedded = false;
    bool editableName = false;
    bool completePlan = false;
    bool applyEnabled = false;
    bool replanned = false;
    QTimer::singleShot(0, &host, [&]() {
        EditorHoverPopup* peek =
            activeExposeSignalPreview(&host);
        QLineEdit* nameEdit = peek
            ? peek->findChild<QLineEdit*>(
                  QStringLiteral("exposeSignalPortName"))
            : nullptr;
        QPlainTextEdit* plan = peek
            ? peek->findChild<QPlainTextEdit*>(
                  QStringLiteral("exposeSignalPlanPreview"))
            : nullptr;
        QPushButton* apply = peek
            ? peek->findChild<QPushButton*>(
                  QStringLiteral("peekAction.apply"))
            : nullptr;
        embedded = peek
            && peek->isVisible()
            && !peek->isWindow()
            && QApplication::activeModalWidget() == nullptr;
        editableName = nameEdit
            && !nameEdit->isReadOnly()
            && nameEdit->text()
                   == QStringLiteral("payload_out");
        completePlan = plan
            && plan->toPlainText().contains(
                   QStringLiteral("top.u_mid.u_leaf"))
            && plan->toPlainText().contains(
                   QStringLiteral("leaf, mid, top"))
            && plan->toPlainText().contains(
                   QStringLiteral("leaf.sv"));
        applyEnabled = apply && apply->isEnabled();
        if (nameEdit)
            nameEdit->setText(QStringLiteral("debug_out"));

        QTimer::singleShot(10, &host, [&]() {
            EditorHoverPopup* refreshed =
                activeExposeSignalPreview(&host);
            QPlainTextEdit* refreshedPlan = refreshed
                ? refreshed->findChild<QPlainTextEdit*>(
                      QStringLiteral(
                          "exposeSignalPlanPreview"))
                : nullptr;
            QPushButton* cancel = refreshed
                ? refreshed->findChild<QPushButton*>(
                      QStringLiteral("peekAction.cancel"))
                : nullptr;
            replanned = refreshedPlan
                && refreshedPlan->toPlainText().contains(
                       QStringLiteral("debug_out"));
            if (cancel)
                cancel->click();
        });
    });
    QTimer::singleShot(1000, &host, [&]() {
        if (EditorHoverPopup* peek =
                activeExposeSignalPreview(&host)) {
            peek->closePopup();
        }
    });
    const ExposeSignalToTopPreview::Result result =
        cancelPreview.exec();

    check("preview uses a non-modal embedded Peek",
          embedded);
    check("preview exposes editable final port",
          editableName);
    check("preview lists hierarchy, affected modules, and diff",
          completePlan && applyEnabled);
    check("editing port replans Peek content",
          replanned
              && cancelPreview.reportForApply().exportedPortName
                     == QStringLiteral("debug_out"));
    check("Cancel rejects and performs no mutation",
          result
                  == ExposeSignalToTopPreview::Result::Rejected
              && untouched
                  == QStringLiteral("module leaf; endmodule"));

    ExposeSignalToTopPreview planOnlyPreview(
        readyDialogReport(QStringLiteral("payload_plan")),
        [](const QString& name) {
            ExposeSignalToTopReport report =
                readyDialogReport(name);
            report.renderedDiff =
                QStringLiteral("+ output logic %1")
                    .arg(name);
            return report;
        },
        &host,
        ExposeSignalToTopPreview::Mode::PreviewOnly);
    bool planOnlySurface = false;
    QTimer::singleShot(0, &host, [&]() {
        EditorHoverPopup* peek =
            activeExposeSignalPreview(&host);
        QPushButton* apply = peek
            ? peek->findChild<QPushButton*>(
                  QStringLiteral("peekAction.apply"))
            : nullptr;
        QPushButton* close = peek
            ? peek->findChild<QPushButton*>(
                  QStringLiteral("peekAction.close"))
            : nullptr;
        QLineEdit* nameEdit = peek
            ? peek->findChild<QLineEdit*>(
                  QStringLiteral("exposeSignalPortName"))
            : nullptr;
        planOnlySurface = peek
            && peek->isVisible()
            && !apply
            && close
            && close->isEnabled()
            && nameEdit
            && !nameEdit->isReadOnly();
        if (nameEdit)
            nameEdit->setText(QStringLiteral("payload_review"));
        QTimer::singleShot(10, &host, [&]() {
            EditorHoverPopup* refreshed =
                activeExposeSignalPreview(&host);
            QPushButton* refreshedClose = refreshed
                ? refreshed->findChild<QPushButton*>(
                      QStringLiteral("peekAction.close"))
                : nullptr;
            if (refreshedClose)
                refreshedClose->click();
        });
    });
    QTimer::singleShot(1000, &host, [&]() {
        if (EditorHoverPopup* peek =
                activeExposeSignalPreview(&host)) {
            peek->closePopup();
        }
    });
    const ExposeSignalToTopPreview::Result planOnlyResult =
        planOnlyPreview.exec();
    check("plan-only Peek replans without exposing Apply",
          planOnlySurface
              && planOnlyResult
                     == ExposeSignalToTopPreview::Result::Accepted
              && planOnlyPreview.reportForApply()
                     .exportedPortName
                     == QStringLiteral("payload_review")
              && untouched
                     == QStringLiteral(
                         "module leaf; endmodule"));
}

void runMenuAvailabilityRegression()
{
    QTemporaryDir temp;
    check("context menu fixture root is available", temp.isValid());
    if (!temp.isValid())
        return;

    const QString fileName =
        temp.filePath(QStringLiteral("expose_menu.sv"));
    const QString text =
        QStringLiteral(
            "module leaf(\n"
            "  input logic clk\n"
            ");\n"
            "  logic [7:0] payload;\n"
            "endmodule\n");
    QFile file(fileName);
    const bool fileWritten =
        file.open(QIODevice::WriteOnly | QIODevice::Text)
        && file.write(text.toUtf8()) == text.toUtf8().size();
    file.close();
    check("context menu fixture source is written", fileWritten);
    if (!fileWritten)
        return;

    SemanticSymbolRecord signal =
        SemanticFixtureRecordBuilder(
            QStringLiteral("payload"),
            SymbolTaxonomy::DeclarationKind::Signal)
            .withFile(fileName)
            .withLocalHandle(301)
            .withLine(4, 15)
            .withTextSpan(text.indexOf(QStringLiteral("payload")), 7)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Logic)
            .inModule(QStringLiteral("leaf"))
            .record();
    SemanticSymbolRecord input =
        SemanticFixtureRecordBuilder(
            QStringLiteral("clk"),
            SymbolTaxonomy::DeclarationKind::Port)
            .withFile(fileName)
            .withLocalHandle(302)
            .withLine(2, 15)
            .withTextSpan(text.indexOf(QStringLiteral("clk")), 3)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::PortInput)
            .inModule(QStringLiteral("leaf"))
            .record();
    SemanticIndex::getInstance()->setSnapshot(
        snapshot({signal, input}, {{fileName, text}}));

    QTabWidget tabs;
    tabs.resize(640, 360);
    TabManager manager(&tabs);
    EditorCoordinator coordinator(&manager);
    check("context menu fixture opens in real TabManager",
          manager.openFileInTab(fileName));
    MyCodeEditor* editor = manager.getCurrentEditor();
    check("context menu fixture creates MyCodeEditor",
          editor != nullptr);
    if (!editor)
        return;
    coordinator.attachEditor(editor);
    tabs.show();
    QCoreApplication::processEvents();

    editor->setHierarchyInstanceContext({
        temp.path(),
        QStringLiteral("top"),
        QStringLiteral("top.u_leaf")});
    const MenuActionState bound =
        contextMenuState(editor,
                         QStringLiteral("payload"),
                         QStringLiteral("payload"));
    check("real context menu exposes Action entry",
          bound.menuShown && bound.found
              && bound.groupTitle == QStringLiteral("Refactor"));
    check("bound module signal enables real context menu Action",
          bound.enabled);

    editor->setHierarchyInstanceContext({
        temp.path(), QString(), QString()});
    const MenuActionState unbound =
        contextMenuState(editor,
                         QStringLiteral("payload"),
                         QStringLiteral("payload"));
    check("unbound instance keeps real context menu Action enterable",
          unbound.found && unbound.enabled
              && !unbound.visibleText.trimmed().isEmpty());

    editor->setHierarchyInstanceContext({
        temp.path(),
        QStringLiteral("top"),
        QStringLiteral("top.u_leaf")});
    const MenuActionState nonPropagatable =
        contextMenuState(editor,
                         QStringLiteral("clk"),
                         QStringLiteral("payload"));
    check("unsupported object keeps Action clickable with visible reason",
          nonPropagatable.found && nonPropagatable.enabled
              && nonPropagatable.visibleText
                     != QStringLiteral("Expose signal to top..."));

    const MenuActionState clickWins =
        contextMenuState(editor,
                         QStringLiteral("payload"),
                         QStringLiteral("clk"));
    check("right-click position, not old cursor, selects target",
          clickWins.found && clickWins.enabled);
}

void runQtApplyChainRegression()
{
    QTemporaryDir temp;
    const QString fileName =
        temp.filePath(QStringLiteral("top.sv"));
    const QString before =
        QStringLiteral("module top;\nendmodule\n");
    QFile file(fileName);
    file.open(QIODevice::WriteOnly | QIODevice::Text);
    file.write(before.toUtf8());
    file.close();

    QTabWidget tabs;
    TabManager manager(&tabs);
    check("fixture opens in real TabManager",
          manager.openFileInTab(fileName));
    MyCodeEditor* editor = manager.getCurrentEditor();
    tabs.resize(900, 620);
    tabs.show();
    QApplication::processEvents();
    QSignalSpy edited(manager.getDocumentModel(),
                      &DocumentModel::documentEdited);
    WorkspaceEditDocumentManager documents(&manager);
    const auto baseline =
        documents.snapshot(fileName.toUtf8().toStdString());
    check("Qt adapter captures editor baseline",
          baseline.has_value());
    QFile externalWrite(fileName);
    externalWrite.open(QIODevice::WriteOnly | QIODevice::Text);
    externalWrite.write(
        (before + QStringLiteral("// external\n")).toUtf8());
    externalWrite.close();
    const auto externallyModified =
        documents.snapshot(fileName.toUtf8().toStdString());
    check("Qt adapter version includes external file identity",
          baseline && externallyModified
              && externallyModified->version
                     != baseline->version
              && externallyModified->text == baseline->text);
    externalWrite.open(QIODevice::WriteOnly | QIODevice::Text);
    externalWrite.write(before.toUtf8());
    externalWrite.close();

    SemanticIndex::getInstance()->setSnapshot(snapshot({}));
    const std::uint64_t generation =
        SemanticIndex::getInstance()->snapshotRevision();
    rtledit::SemanticEditIntent intent;
    intent.kind = rtledit::SemanticEditKind::ExposeSignalToTop;
    intent.target.kind = rtledit::SemanticObjectKind::Signal;
    intent.target.qualifiedName = "top.trace";
    rtledit::WorkspaceTextEdit edit;
    edit.filePath = fileName.toUtf8().toStdString();
    edit.expectedDocumentVersion = baseline->version;
    edit.range = {{1, 0}, {1, 0}};
    edit.newText = "  logic trace;\n";
    rtledit::TextEditProvenance provenance;
    provenance.actionId = rtledit::kExposeSignalToTopActionId;
    provenance.anchorName = "gui.apply";
    provenance.description = "GUI apply chain regression";
    provenance.anchor.source =
        rtledit::AnchorResolutionSource::TreeSitter;
    provenance.editIndex = 0;
    auto plan = rtledit::makeWorkspaceEditPlan(
        intent, rtledit::RiskLevel::High,
        rtledit::PreviewPolicy::Diff,
        {edit}, {provenance});
    plan.semanticSnapshot.id = std::to_string(generation);
    plan.semanticIndexFilePaths = {
        fileName.toUtf8().toStdString()};

    ExposeSignalToTopReport report =
        readyDialogReport(QStringLiteral("trace"));
    report.planResult.plan.workspaceEdit = plan;
    report.transaction =
        WorkspaceEditTransactionService::getInstance()->prepare(
            plan,
            rtledit::SemanticIndexSnapshot{
                std::to_string(generation)},
            documents);
    report.sourceDiff = report.transaction.sourceDiff;
    ExposeSignalToTopPreview applyPreview(
        report, {}, editor);
    bool embeddedApplyPreview = false;
    QTimer::singleShot(0, editor, [&]() {
        EditorHoverPopup* peek =
            activeExposeSignalPreview(editor);
        QPushButton* applyButton = peek
            ? peek->findChild<QPushButton*>(
                  QStringLiteral("peekAction.apply"))
            : nullptr;
        embeddedApplyPreview = peek
            && peek->isVisible()
            && !peek->isWindow()
            && QApplication::activeModalWidget() == nullptr
            && applyButton
            && applyButton->isEnabled();
        if (applyButton)
            applyButton->click();
    });
    QTimer::singleShot(1000, editor, [&]() {
        if (EditorHoverPopup* peek =
                activeExposeSignalPreview(editor)) {
            peek->closePopup();
        }
    });
    const ExposeSignalToTopPreview::Result previewResult =
        applyPreview.exec();
    ExposeSignalToTopService service;
    const ExposeSignalToTopApplyReport applied =
        service.apply(applyPreview.reportForApply(), documents);
    check("Apply accepts the non-modal embedded Peek",
          embeddedApplyPreview
              && previewResult
                     == ExposeSignalToTopPreview::Result::Accepted);
    check("accepted Peek executes the rtleditcore transaction",
          applied.applied());
    QCoreApplication::processEvents();
    check("Apply follows editor incremental document chain",
          editor
              && editor->cachedDocumentText().contains(
                  QStringLiteral("logic trace"))
              && edited.count() > 0
              && manager.getDocumentForEditor(editor).textVersion > 0);
}
} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    runEditorActionContextRegression();
    runEditorActionContextStripRegression();
    runFileActionRegistryShellRegression();
    runContextActionRegistryExecutionRegression();
    runEditorViewTargetedActionRegression();
    runCommandLayerCompletionPopupRegression();
    runPeekRegression();
    runMenuAvailabilityRegression();
    runQtApplyChainRegression();
    std::printf("\n%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
