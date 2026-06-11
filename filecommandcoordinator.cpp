#include "filecommandcoordinator.h"

#include "alternatecommandservice.h"
#include "mycodeeditor.h"
#include "tabmanager.h"
#include "workspacemanager.h"

#include <QAction>
#include <QCloseEvent>
#include <QMessageBox>
#include <QString>

FileCommandCoordinator::FileCommandCoordinator(TabManager* tabManager,
                                               WorkspaceManager* workspaceManager,
                                               QObject* parent)
    : QObject(parent)
    , tabManager(tabManager)
    , workspaceManager(workspaceManager)
{
}

void FileCommandCoordinator::newFile()
{
    if (tabManager)
        tabManager->createNewTab();
}

void FileCommandCoordinator::openFile()
{
    if (tabManager)
        tabManager->openFileInTab(QString());
}

void FileCommandCoordinator::saveFile()
{
    if (tabManager)
        tabManager->saveCurrentTab();
}

void FileCommandCoordinator::saveFileAs()
{
    if (tabManager)
        tabManager->saveAsCurrentTab();
}

void FileCommandCoordinator::copy()
{
    MyCodeEditor* codeEditor = tabManager ? tabManager->getCurrentEditor() : nullptr;
    if (codeEditor)
        codeEditor->copy();
}

void FileCommandCoordinator::paste()
{
    MyCodeEditor* codeEditor = tabManager ? tabManager->getCurrentEditor() : nullptr;
    if (codeEditor)
        codeEditor->paste();
}

void FileCommandCoordinator::cut()
{
    MyCodeEditor* codeEditor = tabManager ? tabManager->getCurrentEditor() : nullptr;
    if (codeEditor)
        codeEditor->cut();
}

void FileCommandCoordinator::undo()
{
    MyCodeEditor* codeEditor = tabManager ? tabManager->getCurrentEditor() : nullptr;
    if (codeEditor)
        codeEditor->undo();
}

void FileCommandCoordinator::redo()
{
    MyCodeEditor* codeEditor = tabManager ? tabManager->getCurrentEditor() : nullptr;
    if (codeEditor)
        codeEditor->redo();
}

void FileCommandCoordinator::executeAlternateCommand(MyCodeEditor* editor,
                                                     AlternateCommandAction action)
{
    switch (action) {
    case AlternateCommandAction::Save:
        saveFile();
        break;
    case AlternateCommandAction::SaveAs:
        saveFileAs();
        break;
    case AlternateCommandAction::Open:
        openFile();
        break;
    case AlternateCommandAction::NewFile:
        newFile();
        break;
    case AlternateCommandAction::Copy:
        if (editor)
            editor->copy();
        break;
    case AlternateCommandAction::Paste:
        if (editor)
            editor->paste();
        break;
    case AlternateCommandAction::Cut:
        if (editor)
            editor->cut();
        break;
    case AlternateCommandAction::Undo:
        if (editor)
            editor->undo();
        break;
    case AlternateCommandAction::Redo:
        if (editor)
            editor->redo();
        break;
    case AlternateCommandAction::SelectAll:
        if (editor)
            editor->selectAll();
        break;
    case AlternateCommandAction::Comment:
        if (editor)
            editor->insertPlainText(QStringLiteral("// "));
        break;
    case AlternateCommandAction::Close:
    case AlternateCommandAction::Find:
    case AlternateCommandAction::Replace:
    case AlternateCommandAction::GotoLine:
    case AlternateCommandAction::Uncomment:
    case AlternateCommandAction::Indent:
    case AlternateCommandAction::Unindent:
    case AlternateCommandAction::None:
        break;
    }
}

void FileCommandCoordinator::executeAlternateCommandText(
    MyCodeEditor* editor,
    const QString& command)
{
    const AlternateCommandAction action =
        AlternateCommandService::getInstance()->commandAction(command);
    if (action != AlternateCommandAction::None)
        executeAlternateCommand(editor, action);
}

void FileCommandCoordinator::openDirectoryAsWorkspace()
{
    if (workspaceManager)
        workspaceManager->openWorkspace(QString());
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

    if (!tabManager || !tabManager->hasUnsavedChanges()) {
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
