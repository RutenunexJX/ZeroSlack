#include "filecommandcoordinator.h"

#include "alternatecommandservice.h"

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
        editorCommands.copy(editor);
        break;
    case AlternateCommandAction::Paste:
        editorCommands.paste(editor);
        break;
    case AlternateCommandAction::Cut:
        editorCommands.cut(editor);
        break;
    case AlternateCommandAction::Undo:
        editorCommands.undo(editor);
        break;
    case AlternateCommandAction::Redo:
        editorCommands.redo(editor);
        break;
    case AlternateCommandAction::SelectAll:
        editorCommands.selectAll(editor);
        break;
    case AlternateCommandAction::Comment:
        editorCommands.comment(editor);
        break;
    case AlternateCommandAction::Uncomment:
        editorCommands.uncomment(editor);
        break;
    case AlternateCommandAction::Indent:
        editorCommands.indent(editor);
        break;
    case AlternateCommandAction::Unindent:
        editorCommands.unindent(editor);
        break;
    case AlternateCommandAction::Replace:
        editorCommands.replace(editor);
        break;
    case AlternateCommandAction::GotoLine:
        editorCommands.gotoLine(editor);
        break;
    case AlternateCommandAction::ClearRhs:
        editorCommands.clearRhs(editor);
        break;
    case AlternateCommandAction::Close:
    case AlternateCommandAction::Find:
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
