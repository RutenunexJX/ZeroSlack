#include "editorcursornavigation.h"

#include "mycodeeditor.h"
#include "sourcenavigationservice.h"

#include <QCursor>
#include <QGuiApplication>
#include <QTextCursor>
#include <QWidget>

void EditorCursorNavigation::moveMouseToCursor(MyCodeEditor* editor) const
{
    if (QGuiApplication::mouseButtons() != Qt::NoButton)
        return;

    if (editor->viewport() && editor->viewport()->isVisible()) {
        QCursor::setPos(
            editor->viewport()->mapToGlobal(
                editor->cursorRect().center()));
    }
}

void EditorCursorNavigation::applyLineTarget(
    MyCodeEditor* editor,
    const SourceLineNavigationTarget& target) const
{
    QTextCursor cursor = editor->textCursor();
    cursor.movePosition(QTextCursor::Start);
    for (int i = 0; i < target.lineMoves; ++i)
        cursor.movePosition(QTextCursor::Down);
    if (target.columnMoves > 0) {
        cursor.movePosition(
            QTextCursor::Right,
            QTextCursor::MoveAnchor,
            target.columnMoves);
    }
    editor->setTextCursor(cursor);
    editor->centerCursor();
    editor->setFocus();
    moveMouseToCursor(editor);
}
