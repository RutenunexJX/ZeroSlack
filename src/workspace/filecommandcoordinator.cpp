#include "filecommandcoordinator.h"

#include "mycodeeditor.h"
#include "tabmanager.h"
#include "workspacemanager.h"

#include <QAction>
#include <QCloseEvent>
#include <QFileDialog>
#include <QString>
#include <QWidget>

#include <utility>

namespace {
struct LineActionBinding {
    const char* route;
    bool (MyCodeEditor::*execute)(QString*);
};
const LineActionBinding lineActions[] = {
    {"editor.lines.duplicate", &MyCodeEditor::duplicateLines},
    {"editor.lines.delete", &MyCodeEditor::deleteLines},
    {"editor.lines.join", &MyCodeEditor::joinLines},
    {"editor.lines.moveUp", &MyCodeEditor::moveLinesUp},
    {"editor.lines.moveDown", &MyCodeEditor::moveLinesDown},
};
const LineActionBinding* lineAction(const QString& route) {
    for (const auto& binding : lineActions) if (route == QLatin1String(binding.route)) return &binding;
    return nullptr;
}
}
QStringList FileCommandCoordinator::lineActionRoutes()
{
    QStringList result;
    for (const auto& binding : lineActions) result.append(QString::fromLatin1(binding.route));
    return result;
}
bool FileCommandCoordinator::isLineAction(const QString& route) { return lineAction(route); }
ActionExecutionResult FileCommandCoordinator::executeLineAction(
    const QString& route, const QString& preferredViewId) const
{
    return executeLineAction(route, targets.tabManager, preferredViewId);
}
ActionExecutionResult FileCommandCoordinator::executeLineAction(const QString& route,
    TabManager* documents, const QString& preferredViewId, MyCodeEditor* preferredEditor)
{
    const auto* binding = lineAction(route);
    if (!binding) return {};
    ActionExecutionResult result;
    result.handled = true;
    auto* editor = !preferredViewId.isEmpty()
        ? (documents ? documents->editorActionTarget(preferredViewId) : nullptr)
        : preferredEditor ? preferredEditor : documents ? documents->editorActionTarget() : nullptr;
    if (!editor) {
        result.failureReason = preferredViewId.isEmpty() ? QStringLiteral("No editor tab is available.")
            : QStringLiteral("The requested editor view is no longer available.");
    } else if (editor->isReadOnly()) {
        result.failureReason = QStringLiteral("The editor is read-only.");
    } else {
        result.succeeded = (editor->*binding->execute)(&result.failureReason);
        if (!result.succeeded && result.failureReason.isEmpty())
            result.failureReason = QStringLiteral("The selected logical lines cannot perform this action.");
    }
    return result;
}

FileCommandCoordinator::FileCommandCoordinator(TabManager* tabManager,
                                               WorkspaceManager* workspaceManager,
                                               QObject* parent)
    : QObject(parent)
{
    targets.set(tabManager, workspaceManager);
    workspaceDirectorySelector = [](QWidget* dialogParent) {
        return QFileDialog::getExistingDirectory(
            dialogParent,
            QStringLiteral("Select Workspace Directory"));
    };
}

void FileCommandCoordinator::setWorkspaceDirectorySelector(
    WorkspaceDirectorySelector selector)
{
    workspaceDirectorySelector = std::move(selector);
}

void FileCommandCoordinator::setWorkspaceOpenHandler(
    WorkspaceOpenHandler handler)
{
    workspaceOpenHandler = std::move(handler);
}

void FileCommandCoordinator::CommandTargets::set(
    TabManager* newTabManager,
    WorkspaceManager* newWorkspaceManager)
{
    tabManager = newTabManager;
    workspaceManager = newWorkspaceManager;
}

void FileCommandCoordinator::CommandTargets::createNewTab() const
{
    if (tabManager)
        tabManager->createNewTab();
}

void FileCommandCoordinator::CommandTargets::openFile() const
{
    if (tabManager)
        tabManager->openFileInTab(QString());
}

bool FileCommandCoordinator::CommandTargets::saveEditor(
    const QString& preferredViewId,
    bool forceSaveAs) const
{
    if (!tabManager)
        return false;
    MyCodeEditor* editor =
        tabManager->editorActionTarget(preferredViewId);
    return editor
        && tabManager->saveEditorView(editor, forceSaveAs);
}

void FileCommandCoordinator::CommandTargets::openWorkspace(
    const QString& folderPath) const
{
    if (workspaceManager && !folderPath.isEmpty())
        workspaceManager->openWorkspaceFromUserSelection(folderPath);
}

bool FileCommandCoordinator::CommandTargets::
resolvePendingDocuments(QWidget* dialogParent) const
{
    return !tabManager
        || tabManager->resolvePendingDocuments(dialogParent);
}

void FileCommandCoordinator::CommandTargets::
finalizeNormalClose() const
{
    if (tabManager)
        tabManager->clearCrashRecoveryAfterNormalClose();
}

MyCodeEditor* FileCommandCoordinator::CommandTargets::
editorActionTarget(const QString& preferredViewId) const
{
    return tabManager
        ? tabManager->editorActionTarget(preferredViewId)
        : nullptr;
}

void FileCommandCoordinator::EditorCommandDispatcher::copy(
    MyCodeEditor* editor) const
{
    if (editor)
        editor->copy();
}

void FileCommandCoordinator::EditorCommandDispatcher::paste(
    MyCodeEditor* editor) const
{
    if (editor)
        editor->paste();
}

void FileCommandCoordinator::EditorCommandDispatcher::cut(
    MyCodeEditor* editor) const
{
    if (editor)
        editor->cut();
}

void FileCommandCoordinator::EditorCommandDispatcher::undo(
    MyCodeEditor* editor) const
{
    if (editor)
        editor->undo();
}

void FileCommandCoordinator::EditorCommandDispatcher::redo(
    MyCodeEditor* editor) const
{
    if (editor)
        editor->redo();
}

void FileCommandCoordinator::newFile()
{
    targets.createNewTab();
}

void FileCommandCoordinator::openFile()
{
    targets.openFile();
}

void FileCommandCoordinator::saveFile()
{
    saveEditor(QString(), false);
}

void FileCommandCoordinator::saveFileAs()
{
    saveEditor(QString(), true);
}

bool FileCommandCoordinator::saveEditor(
    const QString& preferredViewId,
    bool forceSaveAs)
{
    return targets.saveEditor(
        preferredViewId, forceSaveAs);
}

void FileCommandCoordinator::copy()
{
    editorCommands.copy(targets.editorActionTarget());
}

void FileCommandCoordinator::paste()
{
    editorCommands.paste(targets.editorActionTarget());
}

void FileCommandCoordinator::cut()
{
    editorCommands.cut(targets.editorActionTarget());
}

void FileCommandCoordinator::undo()
{
    editorCommands.undo(targets.editorActionTarget());
}

void FileCommandCoordinator::redo()
{
    editorCommands.redo(targets.editorActionTarget());
}

void FileCommandCoordinator::openDirectoryAsWorkspace()
{
    if (!workspaceDirectorySelector)
        return;

    const QString folderPath = workspaceDirectorySelector(
        qobject_cast<QWidget*>(parent()));
    if (folderPath.isEmpty())
        return;
    if (workspaceOpenHandler)
        workspaceOpenHandler(folderPath);
    else
        targets.openWorkspace(folderPath);
}

void FileCommandCoordinator::connectActions(
    QAction* newFileAction,
    QAction* openFileAction,
    QAction* saveFileAction,
    QAction* saveAsAction,
    QAction* copyAction,
    QAction* pasteAction,
    QAction* cutAction,
    QAction* undoAction,
    QAction* redoAction,
    QAction* openWorkspaceAction)
{
    if (newFileAction)
        connect(newFileAction, &QAction::triggered,
                this, &FileCommandCoordinator::newFile);
    if (openFileAction)
        connect(openFileAction, &QAction::triggered,
                this, &FileCommandCoordinator::openFile);
    if (saveFileAction)
        connect(saveFileAction, &QAction::triggered,
                this, &FileCommandCoordinator::saveFile);
    if (saveAsAction)
        connect(saveAsAction, &QAction::triggered,
                this, &FileCommandCoordinator::saveFileAs);
    if (copyAction)
        connect(copyAction, &QAction::triggered,
                this, &FileCommandCoordinator::copy);
    if (pasteAction)
        connect(pasteAction, &QAction::triggered,
                this, &FileCommandCoordinator::paste);
    if (cutAction)
        connect(cutAction, &QAction::triggered,
                this, &FileCommandCoordinator::cut);
    if (undoAction)
        connect(undoAction, &QAction::triggered,
                this, &FileCommandCoordinator::undo);
    if (redoAction)
        connect(redoAction, &QAction::triggered,
                this, &FileCommandCoordinator::redo);
    if (openWorkspaceAction)
        connect(openWorkspaceAction, &QAction::triggered,
                this, &FileCommandCoordinator::openDirectoryAsWorkspace);
}

void FileCommandCoordinator::handleCloseEvent(QCloseEvent* event, QWidget* dialogParent)
{
    if (!event)
        return;

    if (targets.resolvePendingDocuments(dialogParent)) {
        targets.finalizeNormalClose();
        event->accept();
    } else {
        event->ignore();
    }
}
