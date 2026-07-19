#include "filecommandcoordinator.h"

#include "mycodeeditor.h"
#include "tabmanager.h"
#include "workspacemanager.h"

#include <QAction>
#include <QCloseEvent>
#include <QFileDialog>
#include <QMessageBox>
#include <QString>
#include <QWidget>

#include <utility>

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

void FileCommandCoordinator::CommandTargets::saveCurrentTab() const
{
    if (tabManager)
        tabManager->saveCurrentTab();
}

void FileCommandCoordinator::CommandTargets::saveAsCurrentTab() const
{
    if (tabManager)
        tabManager->saveAsCurrentTab();
}

void FileCommandCoordinator::CommandTargets::openWorkspace(
    const QString& folderPath) const
{
    if (workspaceManager && !folderPath.isEmpty())
        workspaceManager->openWorkspaceFromUserSelection(folderPath);
}

bool FileCommandCoordinator::CommandTargets::hasUnsavedChanges() const
{
    return tabManager && tabManager->hasUnsavedChanges();
}

MyCodeEditor* FileCommandCoordinator::CommandTargets::currentEditor() const
{
    return tabManager ? tabManager->getCurrentEditor() : nullptr;
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
    targets.saveCurrentTab();
}

void FileCommandCoordinator::saveFileAs()
{
    targets.saveAsCurrentTab();
}

void FileCommandCoordinator::copy()
{
    editorCommands.copy(targets.currentEditor());
}

void FileCommandCoordinator::paste()
{
    editorCommands.paste(targets.currentEditor());
}

void FileCommandCoordinator::cut()
{
    editorCommands.cut(targets.currentEditor());
}

void FileCommandCoordinator::undo()
{
    editorCommands.undo(targets.currentEditor());
}

void FileCommandCoordinator::redo()
{
    editorCommands.redo(targets.currentEditor());
}

void FileCommandCoordinator::openDirectoryAsWorkspace()
{
    if (!workspaceDirectorySelector)
        return;

    const QString folderPath = workspaceDirectorySelector(
        qobject_cast<QWidget*>(parent()));
    if (folderPath.isEmpty())
        return;
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

    if (!targets.hasUnsavedChanges()) {
        event->accept();
        return;
    }

    const QMessageBox::StandardButton answer = QMessageBox::question(
        dialogParent,
        "Warning",
        "There are unsaved changes. Quit?",
        QMessageBox::Yes | QMessageBox::No);
    if (answer == QMessageBox::Yes)
        event->accept();
    else
        event->ignore();
}
